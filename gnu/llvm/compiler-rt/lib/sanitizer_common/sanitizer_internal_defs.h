//===-- sanitizer_internal_defs.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer.
// It contains macro used in run-time libraries code.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_DEFS_H
#define SANITIZER_DEFS_H

#include "sanitizer_platform.h"
#include "sanitizer_redefine_builtins.h"

// GCC does not understand __has_feature.
#if !defined(__has_feature)
#define __has_feature(x) 0
#endif

#ifndef SANITIZER_DEBUG
# define SANITIZER_DEBUG 0
#endif

#define SANITIZER_STRINGIFY_(S) #S
#define SANITIZER_STRINGIFY(S) SANITIZER_STRINGIFY_(S)

// Only use SANITIZER_*ATTRIBUTE* before the function return type!
#if SANITIZER_WINDOWS
#if SANITIZER_IMPORT_INTERFACE
# define SANITIZER_INTERFACE_ATTRIBUTE __declspec(dllimport)
#else
# define SANITIZER_INTERFACE_ATTRIBUTE __declspec(dllexport)
#endif
# define SANITIZER_WEAK_ATTRIBUTE
#  define SANITIZER_WEAK_IMPORT
#elif SANITIZER_GO
# define SANITIZER_INTERFACE_ATTRIBUTE
# define SANITIZER_WEAK_ATTRIBUTE
#  define SANITIZER_WEAK_IMPORT
#else
# define SANITIZER_INTERFACE_ATTRIBUTE __attribute__((visibility("default")))
# define SANITIZER_WEAK_ATTRIBUTE  __attribute__((weak))
#  if SANITIZER_APPLE
#    define SANITIZER_WEAK_IMPORT extern "C" __attribute((weak_import))
#  else
#    define SANITIZER_WEAK_IMPORT extern "C" SANITIZER_WEAK_ATTRIBUTE
#  endif  // SANITIZER_APPLE
#endif    // SANITIZER_WINDOWS

//--------------------------- WEAK FUNCTIONS ---------------------------------//
// When working with weak functions, to simplify the code and make it more
// portable, when possible define a default implementation using this macro:
//
// SANITIZER_INTERFACE_WEAK_DEF(<return_type>, <name>, <parameter list>)
//
// For example:
//   SANITIZER_INTERFACE_WEAK_DEF(bool, compare, int a, int b) { return a > b; }
//
#if SANITIZER_WINDOWS
#include "sanitizer_win_defs.h"
# define SANITIZER_INTERFACE_WEAK_DEF(ReturnType, Name, ...)                   \
  WIN_WEAK_EXPORT_DEF(ReturnType, Name, __VA_ARGS__)
#else
# define SANITIZER_INTERFACE_WEAK_DEF(ReturnType, Name, ...)                   \
  extern "C" SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE            \
  ReturnType Name(__VA_ARGS__)
#endif

// SANITIZER_SUPPORTS_WEAK_HOOKS means that we support real weak functions that
// will evaluate to a null pointer when not defined.
#ifndef SANITIZER_SUPPORTS_WEAK_HOOKS
#if (SANITIZER_LINUX || SANITIZER_SOLARIS) && !SANITIZER_GO
# define SANITIZER_SUPPORTS_WEAK_HOOKS 1
// Before Xcode 4.5, the Darwin linker doesn't reliably support undefined
// weak symbols.  Mac OS X 10.9/Darwin 13 is the first release only supported
// by Xcode >= 4.5.
#elif SANITIZER_APPLE && \
    __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1090 && !SANITIZER_GO
# define SANITIZER_SUPPORTS_WEAK_HOOKS 1
#else
# define SANITIZER_SUPPORTS_WEAK_HOOKS 0
#endif
#endif // SANITIZER_SUPPORTS_WEAK_HOOKS
// For some weak hooks that will be called very often and we want to avoid the
// overhead of executing the default implementation when it is not necessary,
// we can use the flag SANITIZER_SUPPORTS_WEAK_HOOKS to only define the default
// implementation for platforms that doesn't support weak symbols. For example:
//
//   #if !SANITIZER_SUPPORT_WEAK_HOOKS
//     SANITIZER_INTERFACE_WEAK_DEF(bool, compare_hook, int a, int b) {
//       return a > b;
//     }
//   #endif
//
// And then use it as: if (compare_hook) compare_hook(a, b);
//----------------------------------------------------------------------------//


// We can use .preinit_array section on Linux to call sanitizer initialization
// functions very early in the process startup (unless PIC macro is defined).
//
// On FreeBSD, .preinit_array functions are called with rtld_bind_lock writer
// lock held. It will lead to dead lock if unresolved PLT functions (which helds
// rtld_bind_lock reader lock) are called inside .preinit_array functions.
//
// FIXME: do we have anything like this on Mac?
#ifndef SANITIZER_CAN_USE_PREINIT_ARRAY
#if (SANITIZER_LINUX || SANITIZER_FUCHSIA || SANITIZER_NETBSD) && !defined(PIC)
#define SANITIZER_CAN_USE_PREINIT_ARRAY 1
// Before Solaris 11.4, .preinit_array is fully supported only with GNU ld.
// FIXME: Check for those conditions.
#elif SANITIZER_SOLARIS && !defined(PIC)
# define SANITIZER_CAN_USE_PREINIT_ARRAY 1
#else
# define SANITIZER_CAN_USE_PREINIT_ARRAY 0
#endif
#endif  // SANITIZER_CAN_USE_PREINIT_ARRAY

// GCC does not understand __has_feature
#if !defined(__has_feature)
# define __has_feature(x) 0
#endif

// Older GCCs do not understand __has_attribute.
#if !defined(__has_attribute)
# define __has_attribute(x) 0
#endif

#if !defined(__has_cpp_attribute)
#  define __has_cpp_attribute(x) 0
#endif

// For portability reasons we do not include stddef.h, stdint.h or any other
// system header, but we do need some basic types that are not defined
// in a portable way by the language itself.
namespace __sanitizer {

#if defined(_WIN64)
// 64-bit Windows uses LLP64 data model.
typedef unsigned long long uptr;
typedef signed long long sptr;
#else
#  if (SANITIZER_WORDSIZE == 64) || SANITIZER_APPLE || SANITIZER_WINDOWS
typedef unsigned long uptr;
typedef signed long sptr;
#  else
typedef unsigned int uptr;
typedef signed int sptr;
#  endif
#endif  // defined(_WIN64)
#if defined(__x86_64__)
// Since x32 uses ILP32 data model in 64-bit hardware mode, we must use
// 64-bit pointer to unwind stack frame.
typedef unsigned long long uhwptr;
#else
typedef uptr uhwptr;
#endif
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef signed long long s64;
#if SANITIZER_WINDOWS
// On Windows, files are HANDLE, which is a synonim of void*.
// Use void* to avoid including <windows.h> everywhere.
typedef void* fd_t;
typedef unsigned error_t;
#else
typedef int fd_t;
typedef int error_t;
#endif
#if SANITIZER_SOLARIS && !defined(_LP64)
typedef long pid_t;
#else
typedef int pid_t;
#endif

#if SANITIZER_FREEBSD || SANITIZER_NETBSD || SANITIZER_APPLE ||             \
    (SANITIZER_SOLARIS && (defined(_LP64) || _FILE_OFFSET_BITS == 64)) || \
    (SANITIZER_LINUX && !SANITIZER_GLIBC && !SANITIZER_ANDROID) ||        \
    (SANITIZER_LINUX && (defined(__x86_64__) || defined(__hexagon__)))
typedef u64 OFF_T;
#else
typedef uptr OFF_T;
#endif
typedef u64  OFF64_T;

#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ usize;
#else
// Since we use this for operator new, usize must match the real size_t, but on
// 32-bit Windows the definition of uptr does not actually match uintptr_t or
// size_t because we are working around typedef mismatches for the (S)SIZE_T
// types used in interception.h.
// Until the definition of uptr has been fixed we have to special case Win32.
#  if SANITIZER_WINDOWS && SANITIZER_WORDSIZE == 32
typedef unsigned int usize;
#  else
typedef uptr usize;
#  endif
#endif

typedef u64 tid_t;

// ----------- ATTENTION -------------
// This header should NOT include any other headers to avoid portability issues.

// Common defs.
#define INTERFACE_ATTRIBUTE SANITIZER_INTERFACE_ATTRIBUTE
#define SANITIZER_WEAK_DEFAULT_IMPL \
  extern "C" SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE NOINLINE
#define SANITIZER_WEAK_CXX_DEFAULT_IMPL \
  extern "C++" SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE NOINLINE

// Platform-specific defs.
#if defined(_MSC_VER)
# define ALWAYS_INLINE __forceinline
// FIXME(timurrrr): do we need this on Windows?
# define ALIAS(x)
# define ALIGNED(x) __declspec(align(x))
# define FORMAT(f, a)
# define NOINLINE __declspec(noinline)
# define NORETURN __declspec(noreturn)
# define THREADLOCAL   __declspec(thread)
# define LIKELY(x) (x)
# define UNLIKELY(x) (x)
# define PREFETCH(x) /* _mm_prefetch(x, _MM_HINT_NTA) */ (void)0
# define WARN_UNUSED_RESULT
#else  // _MSC_VER
# define ALWAYS_INLINE inline __attribute__((always_inline))
# define ALIAS(x) __attribute__((alias(SANITIZER_STRINGIFY(x))))
// Please only use the ALIGNED macro before the type.
// Using ALIGNED after the variable declaration is not portable!
# define ALIGNED(x) __attribute__((aligned(x)))
# define FORMAT(f, a)  __attribute__((format(printf, f, a)))
# define NOINLINE __attribute__((noinline))
# define NORETURN  __attribute__((noreturn))
# define THREADLOCAL   __thread
# define LIKELY(x)     __builtin_expect(!!(x), 1)
# define UNLIKELY(x)   __builtin_expect(!!(x), 0)
# if defined(__i386__) || defined(__x86_64__)
// __builtin_prefetch(x) generates prefetchnt0 on x86
#  define PREFETCH(x) __asm__("prefetchnta (%0)" : : "r" (x))
# else
#  define PREFETCH(x) __builtin_prefetch(x)
# endif
# define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#endif  // _MSC_VER

#if !defined(_MSC_VER) || defined(__clang__)
# define UNUSED __attribute__((unused))
# define USED __attribute__((used))
#else
# define UNUSED
# define USED
#endif

#if !defined(_MSC_VER) || defined(__clang__) || MSC_PREREQ(1900)
# define NOEXCEPT noexcept
#else
# define NOEXCEPT throw()
#endif

#if __has_cpp_attribute(clang::fallthrough)
#  define FALLTHROUGH [[clang::fallthrough]]
#elif __has_cpp_attribute(fallthrough)
#  define FALLTHROUGH [[fallthrough]]
#else
#  define FALLTHROUGH
#endif

#if __has_attribute(uninitialized)
#  define UNINITIALIZED __attribute__((uninitialized))
#else
#  define UNINITIALIZED
#endif

// Unaligned versions of basic types.
typedef ALIGNED(1) u16 uu16;
typedef ALIGNED(1) u32 uu32;
typedef ALIGNED(1) u64 uu64;
typedef ALIGNED(1) s16 us16;
typedef ALIGNED(1) s32 us32;
typedef ALIGNED(1) s64 us64;

#if SANITIZER_WINDOWS
}  // namespace __sanitizer
typedef unsigned long DWORD;
namespace __sanitizer {
typedef DWORD thread_return_t;
# define THREAD_CALLING_CONV __stdcall
#else  // _WIN32
typedef void* thread_return_t;
# define THREAD_CALLING_CONV
#endif  // _WIN32
typedef thread_return_t (THREAD_CALLING_CONV *thread_callback_t)(void* arg);

// NOTE: Functions below must be defined in each run-time.
void NORETURN Die();

void NORETURN CheckFailed(const char *file, int line, const char *cond,
                          u64 v1, u64 v2);

// Check macro
#define RAW_CHECK_MSG(expr, msg, ...)          \
  do {                                         \
    if (UNLIKELY(!(expr))) {                   \
      const char* msgs[] = {msg, __VA_ARGS__}; \
      for (const char* m : msgs) RawWrite(m);  \
      Die();                                   \
    }                                          \
  } while (0)

#define RAW_CHECK(expr) RAW_CHECK_MSG(expr, #expr "\n", )
#define RAW_CHECK_VA(expr, ...) RAW_CHECK_MSG(expr, #expr "\n", __VA_ARGS__)

#define CHECK_IMPL(c1, op, c2) \
  do { \
    __sanitizer::u64 v1 = (__sanitizer::u64)(c1); \
    __sanitizer::u64 v2 = (__sanitizer::u64)(c2); \
    if (UNLIKELY(!(v1 op v2))) \
      __sanitizer::CheckFailed(__FILE__, __LINE__, \
        "(" #c1 ") " #op " (" #c2 ")", v1, v2); \
  } while (false) \
/**/

#define CHECK(a)       CHECK_IMPL((a), !=, 0)
#define CHECK_EQ(a, b) CHECK_IMPL((a), ==, (b))
#define CHECK_NE(a, b) CHECK_IMPL((a), !=, (b))
#define CHECK_LT(a, b) CHECK_IMPL((a), <,  (b))
#define CHECK_LE(a, b) CHECK_IMPL((a), <=, (b))
#define CHECK_GT(a, b) CHECK_IMPL((a), >,  (b))
#define CHECK_GE(a, b) CHECK_IMPL((a), >=, (b))

#if SANITIZER_DEBUG
#define DCHECK(a)       CHECK(a)
#define DCHECK_EQ(a, b) CHECK_EQ(a, b)
#define DCHECK_NE(a, b) CHECK_NE(a, b)
#define DCHECK_LT(a, b) CHECK_LT(a, b)
#define DCHECK_LE(a, b) CHECK_LE(a, b)
#define DCHECK_GT(a, b) CHECK_GT(a, b)
#define DCHECK_GE(a, b) CHECK_GE(a, b)
#else
#define DCHECK(a)
#define DCHECK_EQ(a, b)
#define DCHECK_NE(a, b)
#define DCHECK_LT(a, b)
#define DCHECK_LE(a, b)
#define DCHECK_GT(a, b)
#define DCHECK_GE(a, b)
#endif

#define UNREACHABLE(msg) do { \
  CHECK(0 && msg); \
  Die(); \
} while (0)

#define UNIMPLEMENTED() UNREACHABLE("unimplemented")

#define COMPILER_CHECK(pred) static_assert(pred, "")

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

// Limits for integral types. We have to redefine it in case we don't
// have stdint.h (like in Visual Studio 9).
#undef __INT64_C
#undef __UINT64_C
#if SANITIZER_WORDSIZE == 64
# define __INT64_C(c)  c ## L
# define __UINT64_C(c) c ## UL
#else
# define __INT64_C(c)  c ## LL
# define __UINT64_C(c) c ## ULL
#endif  // SANITIZER_WORDSIZE == 64
#undef INT32_MIN
#define INT32_MIN              (-2147483647-1)
#undef INT32_MAX
#define INT32_MAX              (2147483647)
#undef UINT32_MAX
#define UINT32_MAX             (4294967295U)
#undef INT64_MIN
#define INT64_MIN              (-__INT64_C(9223372036854775807)-1)
#undef INT64_MAX
#define INT64_MAX              (__INT64_C(9223372036854775807))
#undef UINT64_MAX
#define UINT64_MAX             (__UINT64_C(18446744073709551615))
#undef UINTPTR_MAX
#if SANITIZER_WORDSIZE == 64
# define UINTPTR_MAX           (18446744073709551615UL)
#else
# define UINTPTR_MAX           (4294967295U)
#endif  // SANITIZER_WORDSIZE == 64

enum LinkerInitialized { LINKER_INITIALIZED = 0 };

#if !defined(_MSC_VER) || defined(__clang__)
#  define GET_CALLER_PC()                              \
    ((__sanitizer::uptr)__builtin_extract_return_addr( \
        __builtin_return_address(0)))
#  define GET_CURRENT_FRAME() ((__sanitizer::uptr)__builtin_frame_address(0))
inline void Trap() {
  __builtin_trap();
}
#else
extern "C" void* _ReturnAddress(void);
extern "C" void* _AddressOfReturnAddress(void);
# pragma intrinsic(_ReturnAddress)
# pragma intrinsic(_AddressOfReturnAddress)
#  define GET_CALLER_PC() ((__sanitizer::uptr)_ReturnAddress())
// CaptureStackBackTrace doesn't need to know BP on Windows.
#  define GET_CURRENT_FRAME() \
    (((__sanitizer::uptr)_AddressOfReturnAddress()) + sizeof(__sanitizer::uptr))

extern "C" void __ud2(void);
#  pragma intrinsic(__ud2)
inline void Trap() {
  __ud2();
}
#endif

#define HANDLE_EINTR(res, f)                                       \
  {                                                                \
    int rverrno;                                                   \
    do {                                                           \
      res = (f);                                                   \
    } while (internal_iserror(res, &rverrno) && rverrno == EINTR); \
  }

// Forces the compiler to generate a frame pointer in the function.
#define ENABLE_FRAME_POINTER              \
  do {                                    \
    volatile __sanitizer::uptr enable_fp; \
    enable_fp = GET_CURRENT_FRAME();      \
    (void)enable_fp;                      \
  } while (0)

// Internal thread identifier allocated by ThreadRegistry.
typedef u32 Tid;
constexpr Tid kInvalidTid = -1;
constexpr Tid kMainTid = 0;

// Stack depot stack identifier.
typedef u32 StackID;
const StackID kInvalidStackID = 0;

}  // namespace __sanitizer

namespace __asan {
using namespace __sanitizer;
}
namespace __dsan {
using namespace __sanitizer;
}
namespace __dfsan {
using namespace __sanitizer;
}
namespace __lsan {
using namespace __sanitizer;
}
namespace __msan {
using namespace __sanitizer;
}
namespace __hwasan {
using namespace __sanitizer;
}
namespace __tsan {
using namespace __sanitizer;
}
namespace __scudo {
using namespace __sanitizer;
}
namespace __ubsan {
using namespace __sanitizer;
}
namespace __xray {
using namespace __sanitizer;
}
namespace __interception {
using namespace __sanitizer;
}
namespace __hwasan {
using namespace __sanitizer;
}
namespace __memprof {
using namespace __sanitizer;
}

#endif  // SANITIZER_DEFS_H
