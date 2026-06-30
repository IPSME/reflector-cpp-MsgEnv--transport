cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Configuration knobs.  Drive everything from the command line, e.g.:
#
#   cmake -B build -G "Visual Studio 17 2022" -A x64 ^
#         -DNAME=my-reflector -DMSGENV=MQTT -DTRANSPORT=asio -DROLE=client
#   cmake --build build --config Debug
#
# Output is  <NAME>.exe  and the VS project/solution are named <NAME> too.
# ---------------------------------------------------------------------------
set(NAME      "reflector" CACHE STRING "project + output (.exe) name")
set(MSGENV    "MQTT"      CACHE STRING "message environment:  MQTT | sdbus | NSDNC")
set(TRANSPORT "asio"      CACHE STRING "transport library:    asio | ws")
set(ROLE      "client"    CACHE STRING "role:                 client | server")

project(${NAME} CXX)

# ---- external dependency locations (per-machine; override with -D...) ----
set(VCPKG_ROOT "C:/Users/dev/vcpkg.git/installed/x64-windows" CACHE PATH "vcpkg installed tree")
set(MOSQUITTO  "C:/Program Files/mosquitto"                   CACHE PATH "mosquitto install root")

message(STATUS "reflector: NAME=${NAME}  MSGENV=${MSGENV}  TRANSPORT=${TRANSPORT}  ROLE=${ROLE}")

# the asio transport units were renamed (client -> tcp_client, server -> broadcast_server);
# map the ROLE knob to the corresponding unit stem.
if (ROLE STREQUAL "server")
    set(ASIO_UNIT "asio_broadcast_server")
else()
    set(ASIO_UNIT "asio_tcp_client")
endif()

# the on-wire protocol this reflector's transport speaks -- the MessagingEnv touch filter serves only
# participants of THIS protocol. It is a property of the chosen TRANSPORT, so it is derived here and
# injected into the source as BUILD_PROTOCOL.
if (TRANSPORT STREQUAL "ws")
    set(PROTOCOL "ws")          # TODO: confirm the exact ws wire-protocol string when the ws transport lands
else()
    set(PROTOCOL "tcp+l4end")   # asio TCP + l4end framing
endif()

# ---- sources : the two swappable picks are interpolated from the knobs ----
add_executable(${NAME}
    main.cpp
    cpp-msgenv-${MSGENV}.git/IPSME_MsgEnv.cpp            # MSGENV:    MQTT | sdbus
    cpp-${TRANSPORT}.git/${ASIO_UNIT}.cpp               # TRANSPORT+ROLE: asio_tcp_client | asio_broadcast_server
    cpp-l4end-framing.git/l4end-framing.cpp
    cpp-msg_cache-dedup.git/msg_cache-dedup.cpp
    cpp-json-msg.git/json_msg+ack.cpp
    cpp-protocol-Discovery.git/JSON_MsgDiscovery.cpp
    cpp-protocol-Discovery.git/JSON_Echo.cpp
)

set_target_properties(${NAME} PROPERTIES OUTPUT_NAME "${NAME}")
target_compile_features(${NAME} PRIVATE cxx_std_20)

# identity + the chosen pieces, exposed to the source as string macros
target_compile_definitions(${NAME} PRIVATE
    BUILD_NAME="${NAME}"
    BUILD_TRANSPORT="${TRANSPORT}"
    BUILD_MSGENV="${MSGENV}"
    BUILD_PROTOCOL="${PROTOCOL}")

# expose the role to the source as ROLE_SERVER / ROLE_CLIENT
string(TOUPPER "${ROLE}" ROLE_UC)
target_compile_definitions(${NAME} PRIVATE ROLE_${ROLE_UC})

# project-local include dirs (portable across platforms)
target_include_directories(${NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    cpp-msgenv-${MSGENV}.git
    cpp-msg_cache-dedup.git
    cpp-l4end-framing.git
    cpp-asio.git
)

# ---------------------------------------------------------------------------
# Platform-specific dependencies.
#
# NOTE: the *sources* are not yet portable (Windows-isms in main.cpp / g_.hpp /
# json_msg+ack.cpp: <windows.h>, <rpc.h>, <debugapi.h>, MSVC pragmas, ...).
# The non-Windows branches below exist only so the project still *generates*
# (e.g. `cmake -G Xcode` on macOS); they are not expected to compile/link yet.
# ---------------------------------------------------------------------------
if (WIN32)
    target_compile_definitions(${NAME} PRIVATE _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
    target_include_directories(${NAME} PRIVATE "${VCPKG_ROOT}/include" "${MOSQUITTO}/devel")
    target_link_directories(${NAME} PRIVATE "${MOSQUITTO}/devel" "${VCPKG_ROOT}/lib")
    target_link_libraries(${NAME} PRIVATE mosquitto rpcrt4 nlohmann_json_schema_validator)

    # copy mosquitto runtime DLLs next to the exe (like the vcxproj post-build)
    file(GLOB MOSQ_DLLS "${MOSQUITTO}/*.dll")
    add_custom_command(TARGET ${NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MOSQ_DLLS} "$<TARGET_FILE_DIR:${NAME}>"
        VERBATIM)

elseif (APPLE)
    # TODO macOS: Homebrew mosquitto, libuuid/CFUUID instead of rpcrt4, and
    # -framework Foundation (+ OBJCXX / .mm) for the NSDNC msgenv.
    # Sources are not portable yet — this branch just lets the Xcode project generate.

elseif (UNIX)
    # Linux (Docker) build. Dependencies come from distro packages plus
    # json-schema-validator built from source (not packaged in Debian):
    #   libasio-dev  libmosquitto-dev  nlohmann-json3-dev  uuid-dev
    find_package(Threads REQUIRED)
    find_package(nlohmann_json_schema_validator REQUIRED)

    # standalone asio (no Boost); enable the POSIX SIGINT/SIGTERM handler in main.cpp
    target_compile_definitions(${NAME} PRIVATE ASIO_STANDALONE POSIX_SIGNAL)

    # message-env library is conditional on the selected MSGENV
    if (MSGENV STREQUAL "sdbus")
        set(MSGENV_LIB systemd)        # libsystemd (sd-bus); do NOT link mosquitto
    else()
        set(MSGENV_LIB mosquitto)
    endif()

    target_link_libraries(${NAME} PRIVATE
        Threads::Threads
        ${MSGENV_LIB}
        uuid
        nlohmann_json_schema_validator)

endif()
