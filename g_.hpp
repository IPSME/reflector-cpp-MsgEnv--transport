

#ifndef G__HPP
#define G__HPP

#include <string>
#include <cstdarg>

#define _X86_ // avoid: #error "No Target Architecture"
#include <debugapi.h>

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

#ifdef _WIN32
// Function to clean up type names (remove "class " or "struct " prefixes for MSVC)
inline std::string demangle(const char* name) {
    std::string result = name;
    // Remove "class " or "struct " prefixes (common in MSVC)
    if (result.find("class ") == 0) {
        result.erase(0, 6);
    }
    else if (result.find("struct ") == 0) {
        result.erase(0, 7);
    }
    return result;
}
#endif

// Macro to get the cleaned class/type name of a variable
#define CLASS_NAME(var) demangle(typeid(var).name())

inline void DebugPrint(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

#endif // G__HPP