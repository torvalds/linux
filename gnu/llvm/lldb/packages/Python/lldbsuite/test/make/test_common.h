// This header is included in all the test programs (C and C++) and provides a
// hook for dealing with platform-specifics.

#if defined(_WIN32) || defined(_WIN64)
#define LLDB_DYLIB_EXPORT __declspec(dllexport)
#define LLDB_DYLIB_IMPORT __declspec(dllimport)
#else
#define LLDB_DYLIB_EXPORT
#define LLDB_DYLIB_IMPORT
#endif

#ifdef COMPILING_LLDB_TEST_DLL
#define LLDB_TEST_API LLDB_DYLIB_EXPORT
#else
#define LLDB_TEST_API LLDB_DYLIB_IMPORT
#endif

#if defined(_WIN32)
#define LLVM_PRETTY_FUNCTION __FUNCSIG__
#else
#define LLVM_PRETTY_FUNCTION LLVM_PRETTY_FUNCTION
#endif


// On some systems (e.g., some versions of linux) it is not possible to attach to a process
// without it giving us special permissions. This defines the lldb_enable_attach macro, which
// should perform any such actions, if needed by the platform. This is a macro instead of a
// function to avoid the need for complex linking of the test programs.
#if defined(__linux__)
#include <sys/prctl.h>

// Android API <= 16 does not have these defined.
#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif
#ifndef PR_SET_PTRACER_ANY
#define PR_SET_PTRACER_ANY ((unsigned long)-1)
#endif

// For now we execute on best effort basis.  If this fails for some reason, so be it.
#define lldb_enable_attach()                                                          \
    do                                                                                \
    {                                                                                 \
        const int prctl_result = prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);  \
        (void)prctl_result;                                                           \
    } while (0)

#else // not linux

#define lldb_enable_attach()

#endif
