

#ifndef G__HPP
#define G__HPP

#include <string>
#include <cstdarg>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define __FILE_LINE__ " " __FILE__ ":" TOSTRING(__LINE__)
//usage: #pragma message("TODO: ..." __FILE_LINE__)

//#define DEFINE_TYPE_TAG(ClassName) \
//    static constexpr std::string_view k_type() noexcept { \
//        return "[" #ClassName "]"; \
//    } \
//    static std::string type() { \
//        return "[" #ClassName "]"; \
//    }

//#define IMPL_STATIC_TYPE_TAG(ClassName) \
//    static constexpr std::string_view _kt = "[" #ClassName "]"; \
//    static inline const std::string _t = std::string(_kt)

// ---- platform-specific debug helpers : the only thing that diverges here ----
#ifdef _WIN32
#define _X86_ // avoid: #error "No Target Architecture"
#include <debugapi.h>

// Clean up type names (remove "class "/"struct " prefixes for MSVC)
inline std::string demangle(const char* name) {
    std::string result = name;
    if (result.find("class ") == 0)
        result.erase(0, 6);
    else if (result.find("struct ") == 0)
        result.erase(0, 7);
    return result;
}

inline void DebugPrint(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

#else // POSIX (Linux, macOS)
#include <cstdio>

// typeid().name() is mangled on GCC/Clang; for debug logging just pass it through.
inline std::string demangle(const char* name) {
    return std::string(name);
}

inline void DebugPrint(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    fputs(buffer, stderr);
}
#endif

// Macro to get the cleaned class/type name of a variable
#define CLASS_NAME(var) demangle(typeid(var).name())

#endif // G__HPP
