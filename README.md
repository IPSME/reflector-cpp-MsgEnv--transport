# reflector-cpp-MsgEnv--transport

A single, configurable reflector that bridges a **message environment** (MQTT, sdbus, …)
with a **transport** (asio client, asio server, ws, …). Instead of forking the whole
project for every combination, you check out once and choose the pieces — and the
output name — as build variables.

```
checkout  →  choose MsgEnv  →  choose transport  →  choose role  →  choose name  →  compile
```

---

## Quick start

```sh
git clone --recursive <repo>
cmake -B build -G "Visual Studio 17 2022" -A x64 \
      -DNAME=my-reflector -DMSGENV=MQTT -DTRANSPORT=asio -DROLE=client
cmake --build build --config Debug          # -> build/Debug/my-reflector.exe
```

Drop `-G "Visual Studio 17 2022"` to use the default generator. Open `build/<NAME>.sln`
in Visual Studio if you want the IDE — it is generated *from* these variables, so you
never hand-edit project GUIDs.

### The knobs (`CMakeLists.txt`)

| Variable     | Default     | Meaning                                            |
|--------------|-------------|----------------------------------------------------|
| `NAME`       | `reflector` | project **and** output `.exe` name (one variable)  |
| `MSGENV`     | `MQTT`      | message environment (`MQTT` \| `sdbus` \| `NSDNC`) — selects `cpp-msgenv-${MSGENV}.git` |
| `TRANSPORT`  | `asio`      | transport library — selects `cpp-${TRANSPORT}.git` |
| `ROLE`       | `client`    | `client` \| `server` — selects `asio_${ROLE}.cpp`  |

The swappable picks are pure string interpolation into the source paths:

```cmake
add_executable(${NAME}
    main.cpp
    cpp-msgenv-${MSGENV}.git/IPSME_MsgEnv.cpp     # MQTT | sdbus
    cpp-${TRANSPORT}.git/asio_${ROLE}.cpp         # asio_client | asio_server
    ...)
set_target_properties(${NAME} PROPERTIES OUTPUT_NAME "${NAME}")
```

Adding a new transport or msgenv later = a new sibling folder, **no `CMakeLists.txt`
edit** — just point the knob at it.

> **On the output name:** with raw MSBuild the output `.exe` is `$(TargetName)`, *not*
> the project name, so even a `.vcxproj` can vary it via a property
> (`<TargetName>$(ReflectorName)</TargetName>` + `/p:ReflectorName=foo`). What MSBuild
> can't cleanly vary is the *project display name* (it's the filename, plus a baked-in
> GUID). CMake sidesteps both: `-DNAME=` drives the project name, the `.sln`/`.vcxproj`
> filenames, and the `.exe` — all from one variable.

### Prerequisites

- Visual Studio 2022 (MSVC v143).
- **mosquitto** dev libs at `C:/Program Files/mosquitto` (override with `-DMOSQUITTO=`).
- **vcpkg** tree at `C:/Users/dev/vcpkg.git/installed/x64-windows` providing
  `nlohmann-json`, `nlohmann-json-schema-validator`, `jsoncons`, `asio`
  (override with `-DVCPKG_ROOT=`).

---

## Configuring & building

**You don't need `-D` on every build** — `-D` is a *configure-time* thing. Once you've run
`cmake -B build -D...` once, the values are saved in `build/CMakeCache.txt`, and after that:

```sh
cmake -B build -G "Visual Studio 17 2022" -A x64 -DNAME=... -DROLE=...   # once
cmake --build build --config Debug                                       # repeat freely, no -D
```

Repeated builds need zero `-D`. You only touch `-D` again when you want to **change** a knob
(or clean + re-edit the defaults).

**Editing the `set(... CACHE ...)` defaults works, but only on a *fresh* configure.** That's
the catch: `CACHE STRING` means "use this *unless already cached*." So if `build/` already
exists, editing `set(NAME "foo" ...)` is ignored — the old cached value wins. To make an
edited default take effect, configure fresh:

```sh
./clean.sh                 # (or: rm -rf build)  -> clears the cache
cmake -B build -G "..."    # fresh configure picks up the new defaults
cmake --build build
```

That's exactly why the clean command pairs with this: **edit defaults → clean → configure →
build**, no `-D` anywhere.

> Don't add `FORCE` to the `set()` to dodge this — `FORCE` would also stop `-D` from ever
> overriding. The plain `CACHE STRING` is what keeps both paths working.

**Named combos without either dance** — that's what `CMakePresets.json` is for:

```jsonc
{ "configurePresets": [
  { "name": "client-mqtt", "generator": "Visual Studio 17 2022",
    "binaryDir": "build/client-mqtt",
    "cacheVariables": { "NAME": "reflector-client", "MSGENV": "MQTT", "ROLE": "client" } },
  { "name": "server-mqtt", "inherits": "client-mqtt",
    "binaryDir": "build/server-mqtt",
    "cacheVariables": { "NAME": "reflector-server", "ROLE": "server" } }
]}
```

then just `cmake --preset client-mqtt && cmake --build build/client-mqtt`.

## Cleaning

`./clean.sh` (macOS / Linux / Git-Bash) or `clean.cmd` (Windows) removes all CMake-generated
project files (Xcode / Visual Studio / Makefiles), build trees and caches — leaving only the
base source + `CMakeLists.txt` + README.

---

## Architecture: one shared core, thin per-variant shells

Everything identical across variants lives in a shared **reflector core**; each variant
is just the two libraries it names. The goal is to avoid the combinatorial fork
(X-Y, X-Z, W-Y, W-Z = four repos for a 2×2 — and four places to fix every bug).

```
SHARED (written once):
  cpp-reflector-core.git/    reflector.hpp     ← bridge logic (deframe/dedup/publish ⇄ enframe/write)
  cpp-asio.git/              asio_client.{h,cpp}, asio_server.{h,cpp}  ← transports, same read-cb/write API
  cpp-msgenv-MQTT.git/       IPSME_MsgEnv.{h,cpp}     ← msgenv impl A (MQTT / mosquitto)
  cpp-msgenv-sdbus.git/      IPSME_MsgEnv.{h,cpp}     ← msgenv impl B (sdbus / Linux)
  cpp-msgenv-NSDNC.git/      IPSME_MsgEnv.{h,cpp}     ← msgenv impl C (NSDistributedNotificationCenter / macOS)
  cpp-l4end-framing.git/     cpp-msg_cache-dedup.git/ ← shared helpers

THIN per-variant (the only thing that differs):
  main.cpp (~15 lines)  +  CMake knobs
```

### The core (sketch)

```cpp
// cpp-reflector-core.git/reflector.hpp
// Transport = anything with .start() and .write(std::string); delivers read bytes via the cb you give it.
// MsgEnv    = anything with .publish(const char*) and .subscribe(cb, void*).
template <typename Transport, typename MsgEnv>
class Reflector {
    duplicate          _dedup;
    std::vector<char>  _buf;
    Transport&         _transport;
    MsgEnv&            _msgenv;
public:
    Reflector(Transport& t, MsgEnv& m) : _transport(t), _msgenv(m) {}

    void on_read(const char* data, std::size_t len) {           // asio -> msgenv
        l4end::deframe(&_buf, data, len, [this](std::string msg){
            if (_dedup.exists(msg)) return;
            _msgenv.publish(msg.c_str());
        });
    }
    bool on_msg(std::string msg) {                              // msgenv -> asio
        _dedup.cache(msg, t_entry_context(30s));
        _transport.write(l4end::enframe(msg));
        return true;
    }
    void start() {
        _msgenv.subscribe(&trampoline, this);                  // void* user-data carries `this`
        _transport.start();
    }
private:
    static void trampoline(const char* m, void* self) { static_cast<Reflector*>(self)->on_msg(m); }
};
```

### The thin shell (sketch) — the only thing that differs per variant

```cpp
#include "cpp-reflector-core.git/reflector.hpp"
#include "cpp-msgenv-MQTT.git/IPSME_MsgEnv.h"
#include "cpp-asio.git/asio_client.h"            // server variant: asio_server.h

int main() {
    mosquitto_lib_init();
    IPSME_MsgEnv msgenv;
    asio_client  transport;                       // server variant: asio_server transport(4999);
    Reflector    reflector(transport, msgenv);
    transport.set_read_cb([&](const char* d, std::size_t n){ reflector.on_read(d, n); });
    reflector.start();
    /* loop: msgenv.process_msgs(); */
    mosquitto_lib_cleanup();
}
```

### Why this works

- **A variant = pick 2 libs**: two `#include`s + two CMake knobs. No forked logic.
- **Template core = zero runtime cost** (no vtables); the lib is fixed per binary. If you
  ever need to choose at *runtime*, swap the template for a common base
  (`ITransport`/`IMsgEnv`) — same layout, just virtual calls.
- The read callback is wired in `main` (not the transport ctor) so `Reflector` and the
  transport can reference each other — the one plumbing tweak vs. today (move the read
  callback to a `set_read_cb()`/`start(cb)`).

### The one discipline it depends on

The interchangeable libs must keep **byte-identical public APIs** — every `IPSME_MsgEnv`
exposes the same `publish`/`subscribe`/`process_msgs`, every transport the same
`start`/`write` + read callback. That contract is what lets the core compile against any
combination unchanged.

---

## Status

- `CMakeLists.txt` builds the **MQTT / asio / client** combo today (verified).
- `main.cpp` is currently the client project's `main` **as-is** — not yet reduced to the
  thin shell above, so `ROLE`/`MSGENV` are effectively pinned to `client`/`MQTT` until
  the core is extracted and `main` is slimmed. The knobs and layout are in place for that
  next step.
