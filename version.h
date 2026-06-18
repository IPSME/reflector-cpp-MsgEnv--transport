// version.h
#pragma once

#define VERSION_MAJOR     0
#define VERSION_MINOR     0
#define VERSION_PATCH     0
#define VERSION_BUILD     0       // will be auto-incremented
#define VERSION_SUFFIX    ""

// Helper macros for resource compiler
#define _TOSTR(x)   #x
#define TOSTR(x)    _TOSTR(x)

#define VER_FILE_VERSION  VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD
#define VER_FILE_VERSION_STR \
    TOSTR(VERSION_MAJOR) "." TOSTR(VERSION_MINOR) "." TOSTR(VERSION_PATCH) "-" TOSTR(VERSION_BUILD) VERSION_SUFFIX "\0"

#define VER_PRODUCT_VERSION     VER_FILE_VERSION
#define VER_PRODUCT_VERSION_STR VER_FILE_VERSION_STR
