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

# ---- sources : the two swappable picks are interpolated from the knobs ----
add_executable(${NAME}
    main.cpp
    cpp-msgenv-${MSGENV}.git/IPSME_MsgEnv.cpp            # MSGENV:    MQTT | sdbus
    cpp-${TRANSPORT}.git/asio_${ROLE}.cpp               # TRANSPORT+ROLE: asio_client | asio_server
    cpp-l4end-framing.git/l4end-framing.cpp
    cpp-msg_cache-dedup.git/msg_cache-dedup.cpp
    cpp-EventLog.git/InMemory_EventLog.cpp
    cpp-json-msg.git/json_msg+ack.cpp
    cpp-protocol-Discovery.git/JSON_MsgDiscovery.cpp
    cpp-protocol-Discovery.git/JSON_Echo.cpp
)

set_target_properties(${NAME} PROPERTIES OUTPUT_NAME "${NAME}")
target_compile_features(${NAME} PRIVATE cxx_std_20)

# the participant identity used in messages == the build NAME
target_compile_definitions(${NAME} PRIVATE BUILD_NAME="${NAME}")

# expose the role to the source as ROLE_SERVER / ROLE_CLIENT
string(TOUPPER "${ROLE}" ROLE_UC)
target_compile_definitions(${NAME} PRIVATE ROLE_${ROLE_UC})

# project-local include dirs (portable across platforms)
target_include_directories(${NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
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
    # TODO Linux: pkg-config mosquitto / sdbus.

endif()
