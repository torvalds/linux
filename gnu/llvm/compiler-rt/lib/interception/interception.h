//===-- interception.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Machinery for providing replacements/wrappers for system functions.
//===----------------------------------------------------------------------===//

#ifndef INTERCEPTION_H
#define INTERCEPTION_H

#include "sanitizer_common/sanitizer_asm.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

#if !SANITIZER_LINUX && !SANITIZER_FREEBSD && !SANITIZER_APPLE &&    \
    !SANITIZER_NETBSD && !SANITIZER_WINDOWS && !SANITIZER_FUCHSIA && \
    !SANITIZER_OPENBSD && !SANITIZER_SOLARIS
#  error "Interception doesn't work on this operating system."
#endif

// These typedefs should be used only in the interceptor definitions to replace
// the standard system types (e.g. SSIZE_T instead of ssize_t)
typedef __sanitizer::uptr    SIZE_T;
typedef __sanitizer::sptr    SSIZE_T;
typedef __sanitizer::sptr    PTRDIFF_T;
typedef __sanitizer::s64     INTMAX_T;
typedef __sanitizer::u64     UINTMAX_T;
typedef __sanitizer::OFF_T   OFF_T;
typedef __sanitizer::OFF64_T OFF64_T;

// How to add an interceptor:
// Suppose you need to wrap/replace system function (generally, from libc):
//      int foo(const char *bar, double baz);
// You'll need to:
//      1) define INTERCEPTOR(int, foo, const char *bar, double baz) { ... } in
//         your source file. See the notes below for cases when
//         INTERCEPTOR_WITH_SUFFIX(...) should be used instead.
//      2) Call "INTERCEPT_FUNCTION(foo)" prior to the first call of "foo".
//         INTERCEPT_FUNCTION(foo) evaluates to "true" iff the function was
//         intercepted successfully.
// You can access original function by calling REAL(foo)(bar, baz).
// By default, REAL(foo) will be visible only inside your interceptor, and if
// you want to use it in other parts of RTL, you'll need to:
//      3a) add DECLARE_REAL(int, foo, const char*, double) to a
//          header file.
// However, if the call "INTERCEPT_FUNCTION(foo)" and definition for
// INTERCEPTOR(..., foo, ...) are in different files, you'll instead need to:
//      3b) add DECLARE_REAL_AND_INTERCEPTOR(int, foo, const char*, double)
//          to a header file.

// Notes: 1. Things may not work properly if macro INTERCEPTOR(...) {...} or
//           DECLARE_REAL(...) are located inside namespaces.
//        2. On Mac you can also use: "OVERRIDE_FUNCTION(foo, zoo)" to
//           effectively redirect calls from "foo" to "zoo". In this case
//           you aren't required to implement
//           INTERCEPTOR(int, foo, const char *bar, double baz) {...}
//           but instead you'll have to add
//           DECLARE_REAL(int, foo, const char *bar, double baz) in your
//           source file (to define a pointer to overriden function).
//        3. Some Mac functions have symbol variants discriminated by
//           additional suffixes, e.g. _$UNIX2003 (see
//           https://developer.apple.com/library/mac/#releasenotes/Darwin/SymbolVariantsRelNotes/index.html
//           for more details). To intercept such functions you need to use the
//           INTERCEPTOR_WITH_SUFFIX(...) macro.

// How it works on Linux
// ---------------------
//
// To replace system functions on Linux we just need to declare functions with
// the same names in our library and then obtain the real function pointers
// using dlsym().
//
// There is one complication: a user may also intercept some of the functions we
// intercept. To allow for up to 3 interceptors (including ours) of a given
// function "func", the interceptor implementation is in ___interceptor_func,
// which is aliased by a weak function __interceptor_func, which in turn is
// aliased (via a trampoline) by weak wrapper function "func".
//
// Most user interceptors should define a foreign interceptor as follows:
//
//  - provide a non-weak function "func" that performs interception;
//  - if __interceptor_func exists, call it to perform the real functionality;
//  - if it does not exist, figure out the real function and call it instead.
//
// In rare cases, a foreign interceptor (of another dynamic analysis runtime)
// may be defined as follows (on supported architectures):
//
//  - provide a non-weak function __interceptor_func that performs interception;
//  - if ___interceptor_func exists, call it to perform the real functionality;
//  - if it does not exist, figure out the real function and call it instead;
//  - provide a weak function "func" that is an alias to __interceptor_func.
//
// With this protocol, sanitizer interceptors, foreign user interceptors, and
// foreign interceptors of other dynamic analysis runtimes, or any combination
// thereof, may co-exist simultaneously.
//
// How it works on Mac OS
// ----------------------
//
// This is not so on Mac OS, where the two-level namespace makes our replacement
// functions invisible to other libraries. This may be overcomed using the
// DYLD_FORCE_FLAT_NAMESPACE, but some errors loading the shared libraries in
// Chromium were noticed when doing so.
//
// Instead we create a dylib containing a __DATA,__interpose section that
// associates library functions with their wrappers. When this dylib is
// preloaded before an executable using DYLD_INSERT_LIBRARIES, it routes all the
// calls to interposed functions done through stubs to the wrapper functions.
//
// As it's decided at compile time which functions are to be intercepted on Mac,
// INTERCEPT_FUNCTION() is effectively a no-op on this system.

#if SANITIZER_APPLE
#include <sys/cdefs.h>  // For __DARWIN_ALIAS_C().

// Just a pair of pointers.
struct interpose_substitution {
  const __sanitizer::uptr replacement;
  const __sanitizer::uptr original;
};

// For a function foo() create a global pair of pointers { wrap_foo, foo } in
// the __DATA,__interpose section.
// As a result all the calls to foo() will be routed to wrap_foo() at runtime.
#define INTERPOSER(func_name) __attribute__((used))     \
const interpose_substitution substitution_##func_name[] \
    __attribute__((section("__DATA, __interpose"))) = { \
    { reinterpret_cast<const uptr>(WRAP(func_name)),    \
      reinterpret_cast<const uptr>(func_name) }         \
}

// For a function foo() and a wrapper function bar() create a global pair
// of pointers { bar, foo } in the __DATA,__interpose section.
// As a result all the calls to foo() will be routed to bar() at runtime.
#define INTERPOSER_2(func_name, wrapper_name) __attribute__((used)) \
const interpose_substitution substitution_##func_name[]             \
    __attribute__((section("__DATA, __interpose"))) = {             \
    { reinterpret_cast<const uptr>(wrapper_name),                   \
      reinterpret_cast<const uptr>(func_name) }                     \
}

# define WRAP(x) wrap_##x
# define TRAMPOLINE(x) WRAP(x)
# define INTERCEPTOR_ATTRIBUTE
# define DECLARE_WRAPPER(ret_type, func, ...)

#elif SANITIZER_WINDOWS
# define WRAP(x) __asan_wrap_##x
# define TRAMPOLINE(x) WRAP(x)
# define INTERCEPTOR_ATTRIBUTE __declspec(dllexport)
# define DECLARE_WRAPPER(ret_type, func, ...)         \
    extern "C" ret_type func(__VA_ARGS__);
# define DECLARE_WRAPPER_WINAPI(ret_type, func, ...)  \
    extern "C" __declspec(dllimport) ret_type __stdcall func(__VA_ARGS__);
#elif !SANITIZER_FUCHSIA  // LINUX, FREEBSD, NETBSD, SOLARIS
# define INTERCEPTOR_ATTRIBUTE __attribute__((visibility("default")))
# if ASM_INTERCEPTOR_TRAMPOLINE_SUPPORT
// Weak aliases of weak aliases do not work, therefore we need to set up a
// trampoline function. The function "func" is a weak alias to the trampoline
// (so that we may check if "func" was overridden), which calls the weak
// function __interceptor_func, which in turn aliases the actual interceptor
// implementation ___interceptor_func:
//
//    [wrapper "func": weak] --(alias)--> [TRAMPOLINE(func)]
//                                                |
//                     +--------(tail call)-------+
//                     |
//                     v
//      [__interceptor_func: weak] --(alias)--> [WRAP(func)]
//
// We use inline assembly to define most of this, because not all compilers
// support functions with the "naked" attribute with every architecture.
#  define WRAP(x) ___interceptor_ ## x
#  define TRAMPOLINE(x) __interceptor_trampoline_ ## x
#  if SANITIZER_FREEBSD || SANITIZER_NETBSD
// FreeBSD's dynamic linker (incompliantly) gives non-weak symbols higher
// priority than weak ones so weak aliases won't work for indirect calls
// in position-independent (-fPIC / -fPIE) mode.
#   define __ASM_WEAK_WRAPPER(func) ".globl " #func "\n"
#  else
#   define __ASM_WEAK_WRAPPER(func) ".weak " #func "\n"
#  endif  // SANITIZER_FREEBSD || SANITIZER_NETBSD
#  if defined(__arm__) || defined(__aarch64__)
#   define ASM_TYPE_FUNCTION_STR "%function"
#  else
#   define ASM_TYPE_FUNCTION_STR "@function"
#  endif
// Keep trampoline implementation in sync with sanitizer_common/sanitizer_asm.h
#  define DECLARE_WRAPPER(ret_type, func, ...)                                 \
     extern "C" ret_type func(__VA_ARGS__);                                    \
     extern "C" ret_type TRAMPOLINE(func)(__VA_ARGS__);                        \
     extern "C" ret_type __interceptor_##func(__VA_ARGS__)                     \
       INTERCEPTOR_ATTRIBUTE __attribute__((weak)) ALIAS(WRAP(func));          \
     asm(                                                                      \
       ".text\n"                                                               \
       __ASM_WEAK_WRAPPER(func)                                                \
       ".set " #func ", " SANITIZER_STRINGIFY(TRAMPOLINE(func)) "\n"           \
       ".globl " SANITIZER_STRINGIFY(TRAMPOLINE(func)) "\n"                    \
       ".type  " SANITIZER_STRINGIFY(TRAMPOLINE(func)) ", "                    \
         ASM_TYPE_FUNCTION_STR "\n"                                            \
       SANITIZER_STRINGIFY(TRAMPOLINE(func)) ":\n"                             \
       C_ASM_STARTPROC "\n"                                                    \
       C_ASM_TAIL_CALL(SANITIZER_STRINGIFY(TRAMPOLINE(func)),                  \
                       "__interceptor_"                                        \
                         SANITIZER_STRINGIFY(ASM_PREEMPTIBLE_SYM(func))) "\n"  \
       C_ASM_ENDPROC "\n"                                                      \
       ".size  " SANITIZER_STRINGIFY(TRAMPOLINE(func)) ", "                    \
            ".-" SANITIZER_STRINGIFY(TRAMPOLINE(func)) "\n"                    \
     );
# else  // ASM_INTERCEPTOR_TRAMPOLINE_SUPPORT
// Some architectures cannot implement efficient interceptor trampolines with
// just a plain jump due to complexities of resolving a preemptible symbol. In
// those cases, revert to just this scheme:
//
//    [wrapper "func": weak] --(alias)--> [WRAP(func)]
//
#  define WRAP(x) __interceptor_ ## x
#  define TRAMPOLINE(x) WRAP(x)
#  if SANITIZER_FREEBSD || SANITIZER_NETBSD
#   define __ATTRIBUTE_WEAK_WRAPPER
#  else
#   define __ATTRIBUTE_WEAK_WRAPPER __attribute__((weak))
#  endif  // SANITIZER_FREEBSD || SANITIZER_NETBSD
#  define DECLARE_WRAPPER(ret_type, func, ...)                                 \
     extern "C" ret_type func(__VA_ARGS__)                                     \
       INTERCEPTOR_ATTRIBUTE __ATTRIBUTE_WEAK_WRAPPER ALIAS(WRAP(func));
# endif  // ASM_INTERCEPTOR_TRAMPOLINE_SUPPORT
#endif

#if SANITIZER_FUCHSIA
// There is no general interception at all on Fuchsia.
// Sanitizer runtimes just define functions directly to preempt them,
// and have bespoke ways to access the underlying libc functions.
# include <zircon/sanitizer.h>
# define INTERCEPTOR_ATTRIBUTE __attribute__((visibility("default")))
# define REAL(x) __unsanitized_##x
# define DECLARE_REAL(ret_type, func, ...)
#elif !SANITIZER_APPLE
# define PTR_TO_REAL(x) real_##x
# define REAL(x) __interception::PTR_TO_REAL(x)
# define FUNC_TYPE(x) x##_type

# define DECLARE_REAL(ret_type, func, ...)            \
    typedef ret_type (*FUNC_TYPE(func))(__VA_ARGS__); \
    namespace __interception {                        \
    extern FUNC_TYPE(func) PTR_TO_REAL(func);         \
    }
# define ASSIGN_REAL(dst, src) REAL(dst) = REAL(src)
#else  // SANITIZER_APPLE
# define REAL(x) x
# define DECLARE_REAL(ret_type, func, ...) \
    extern "C" ret_type func(__VA_ARGS__);
# define ASSIGN_REAL(x, y)
#endif  // SANITIZER_APPLE

#if !SANITIZER_FUCHSIA
# define DECLARE_REAL_AND_INTERCEPTOR(ret_type, func, ...)  \
    DECLARE_REAL(ret_type, func, __VA_ARGS__)               \
    extern "C" ret_type TRAMPOLINE(func)(__VA_ARGS__);      \
    extern "C" ret_type WRAP(func)(__VA_ARGS__);
// Declare an interceptor and its wrapper defined in a different translation
// unit (ex. asm).
# define DECLARE_EXTERN_INTERCEPTOR_AND_WRAPPER(ret_type, func, ...)  \
    extern "C" ret_type TRAMPOLINE(func)(__VA_ARGS__);                \
    extern "C" ret_type WRAP(func)(__VA_ARGS__);                      \
    extern "C" ret_type func(__VA_ARGS__);
#else
# define DECLARE_REAL_AND_INTERCEPTOR(ret_type, func, ...)
# define DECLARE_EXTERN_INTERCEPTOR_AND_WRAPPER(ret_type, func, ...)
#endif

// Generally, you don't need to use DEFINE_REAL by itself, as INTERCEPTOR
// macros does its job. In exceptional cases you may need to call REAL(foo)
// without defining INTERCEPTOR(..., foo, ...). For example, if you override
// foo with an interceptor for other function.
#if !SANITIZER_APPLE && !SANITIZER_FUCHSIA
#  define DEFINE_REAL(ret_type, func, ...)            \
    typedef ret_type (*FUNC_TYPE(func))(__VA_ARGS__); \
    namespace __interception {                        \
    FUNC_TYPE(func) PTR_TO_REAL(func);                \
    }
#else
# define DEFINE_REAL(ret_type, func, ...)
#endif

#if SANITIZER_FUCHSIA

// We need to define the __interceptor_func name just to get
// sanitizer_common/scripts/gen_dynamic_list.py to export func.
// But we don't need to export __interceptor_func to get that.
#define INTERCEPTOR(ret_type, func, ...)                                \
  extern "C"[[ gnu::alias(#func), gnu::visibility("hidden") ]] ret_type \
      __interceptor_##func(__VA_ARGS__);                                \
  extern "C" INTERCEPTOR_ATTRIBUTE ret_type func(__VA_ARGS__)

#elif !SANITIZER_APPLE

#define INTERCEPTOR(ret_type, func, ...)        \
  DEFINE_REAL(ret_type, func, __VA_ARGS__)      \
  DECLARE_WRAPPER(ret_type, func, __VA_ARGS__)  \
  extern "C" INTERCEPTOR_ATTRIBUTE ret_type WRAP(func)(__VA_ARGS__)

// We don't need INTERCEPTOR_WITH_SUFFIX on non-Darwin for now.
#define INTERCEPTOR_WITH_SUFFIX(ret_type, func, ...) \
  INTERCEPTOR(ret_type, func, __VA_ARGS__)

#else  // SANITIZER_APPLE

#define INTERCEPTOR_ZZZ(suffix, ret_type, func, ...)  \
  extern "C" ret_type func(__VA_ARGS__) suffix;       \
  extern "C" ret_type WRAP(func)(__VA_ARGS__);        \
  INTERPOSER(func);                                   \
  extern "C" INTERCEPTOR_ATTRIBUTE ret_type WRAP(func)(__VA_ARGS__)

#define INTERCEPTOR(ret_type, func, ...) \
  INTERCEPTOR_ZZZ(/*no symbol variants*/, ret_type, func, __VA_ARGS__)

#define INTERCEPTOR_WITH_SUFFIX(ret_type, func, ...) \
  INTERCEPTOR_ZZZ(__DARWIN_ALIAS_C(func), ret_type, func, __VA_ARGS__)

// Override |overridee| with |overrider|.
#define OVERRIDE_FUNCTION(overridee, overrider) \
  INTERPOSER_2(overridee, WRAP(overrider))
#endif

#if SANITIZER_WINDOWS
# define INTERCEPTOR_WINAPI(ret_type, func, ...)                \
    typedef ret_type (__stdcall *FUNC_TYPE(func))(__VA_ARGS__); \
    namespace __interception {                                  \
      FUNC_TYPE(func) PTR_TO_REAL(func);                        \
    }                                                           \
    extern "C" INTERCEPTOR_ATTRIBUTE ret_type __stdcall WRAP(func)(__VA_ARGS__)
#endif

// ISO C++ forbids casting between pointer-to-function and pointer-to-object,
// so we use casting via an integral type __interception::uptr,
// assuming that system is POSIX-compliant. Using other hacks seem
// challenging, as we don't even pass function type to
// INTERCEPT_FUNCTION macro, only its name.
namespace __interception {
#if defined(_WIN64)
typedef unsigned long long uptr;
#else
typedef unsigned long uptr;
#endif  // _WIN64

#if defined(__ELF__) && !SANITIZER_FUCHSIA
// The use of interceptors makes many sanitizers unusable for static linking.
// Define a function, if called, will cause a linker error (undefined _DYNAMIC).
// However, -static-pie (which is not common) cannot be detected at link time.
extern uptr kDynamic[] asm("_DYNAMIC");
inline void DoesNotSupportStaticLinking() {
  [[maybe_unused]] volatile auto x = &kDynamic;
}
#else
inline void DoesNotSupportStaticLinking() {}
#endif
}  // namespace __interception

#define INCLUDED_FROM_INTERCEPTION_LIB

#if SANITIZER_LINUX || SANITIZER_FREEBSD || SANITIZER_NETBSD || \
    SANITIZER_OPENBSD || SANITIZER_SOLARIS

# include "interception_linux.h"
# define INTERCEPT_FUNCTION(func) INTERCEPT_FUNCTION_LINUX_OR_FREEBSD(func)
# define INTERCEPT_FUNCTION_VER(func, symver) \
    INTERCEPT_FUNCTION_VER_LINUX_OR_FREEBSD(func, symver)
#elif SANITIZER_APPLE
# include "interception_mac.h"
# define INTERCEPT_FUNCTION(func) INTERCEPT_FUNCTION_MAC(func)
# define INTERCEPT_FUNCTION_VER(func, symver) \
    INTERCEPT_FUNCTION_VER_MAC(func, symver)
#elif SANITIZER_WINDOWS
# include "interception_win.h"
# define INTERCEPT_FUNCTION(func) INTERCEPT_FUNCTION_WIN(func)
# define INTERCEPT_FUNCTION_VER(func, symver) \
    INTERCEPT_FUNCTION_VER_WIN(func, symver)
#endif

#undef INCLUDED_FROM_INTERCEPTION_LIB

#endif  // INTERCEPTION_H
