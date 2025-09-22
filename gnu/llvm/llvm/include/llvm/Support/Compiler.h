//===-- llvm/Support/Compiler.h - Compiler abstraction support --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines several macros, based on the current compiler.  This allows
// use of compiler-specific features in a way that remains portable. This header
// can be included from either C or C++.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COMPILER_H
#define LLVM_SUPPORT_COMPILER_H

#include "llvm/Config/llvm-config.h"

#include <stddef.h>

#if defined(_MSC_VER)
#include <sal.h>
#endif

#ifndef __has_feature
# define __has_feature(x) 0
#endif

#ifndef __has_extension
# define __has_extension(x) 0
#endif

#ifndef __has_attribute
# define __has_attribute(x) 0
#endif

#ifndef __has_builtin
# define __has_builtin(x) 0
#endif

#ifndef __has_include
# define __has_include(x) 0
#endif

// Only use __has_cpp_attribute in C++ mode. GCC defines __has_cpp_attribute in
// C mode, but the :: in __has_cpp_attribute(scoped::attribute) is invalid.
#ifndef LLVM_HAS_CPP_ATTRIBUTE
#if defined(__cplusplus) && defined(__has_cpp_attribute)
# define LLVM_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
# define LLVM_HAS_CPP_ATTRIBUTE(x) 0
#endif
#endif

/// \macro LLVM_GNUC_PREREQ
/// Extend the default __GNUC_PREREQ even if glibc's features.h isn't
/// available.
#ifndef LLVM_GNUC_PREREQ
# if defined(__GNUC__) && defined(__GNUC_MINOR__) && defined(__GNUC_PATCHLEVEL__)
#  define LLVM_GNUC_PREREQ(maj, min, patch) \
    ((__GNUC__ << 20) + (__GNUC_MINOR__ << 10) + __GNUC_PATCHLEVEL__ >= \
     ((maj) << 20) + ((min) << 10) + (patch))
# elif defined(__GNUC__) && defined(__GNUC_MINOR__)
#  define LLVM_GNUC_PREREQ(maj, min, patch) \
    ((__GNUC__ << 20) + (__GNUC_MINOR__ << 10) >= ((maj) << 20) + ((min) << 10))
# else
#  define LLVM_GNUC_PREREQ(maj, min, patch) 0
# endif
#endif

/// \macro LLVM_MSC_PREREQ
/// Is the compiler MSVC of at least the specified version?
/// The common \param version values to check for are:
/// * 1910: VS2017, version 15.1 & 15.2
/// * 1911: VS2017, version 15.3 & 15.4
/// * 1912: VS2017, version 15.5
/// * 1913: VS2017, version 15.6
/// * 1914: VS2017, version 15.7
/// * 1915: VS2017, version 15.8
/// * 1916: VS2017, version 15.9
/// * 1920: VS2019, version 16.0
/// * 1921: VS2019, version 16.1
/// * 1922: VS2019, version 16.2
/// * 1923: VS2019, version 16.3
/// * 1924: VS2019, version 16.4
/// * 1925: VS2019, version 16.5
/// * 1926: VS2019, version 16.6
/// * 1927: VS2019, version 16.7
/// * 1928: VS2019, version 16.8 + 16.9
/// * 1929: VS2019, version 16.10 + 16.11
/// * 1930: VS2022, version 17.0
#ifdef _MSC_VER
#define LLVM_MSC_PREREQ(version) (_MSC_VER >= (version))

// We require at least VS 2019.
#if !defined(LLVM_FORCE_USE_OLD_TOOLCHAIN)
#if !LLVM_MSC_PREREQ(1920)
#error LLVM requires at least VS 2019.
#endif
#endif

#else
#define LLVM_MSC_PREREQ(version) 0
#endif

/// LLVM_LIBRARY_VISIBILITY - If a class marked with this attribute is linked
/// into a shared library, then the class should be private to the library and
/// not accessible from outside it.  Can also be used to mark variables and
/// functions, making them private to any shared library they are linked into.
/// On PE/COFF targets, library visibility is the default, so this isn't needed.
///
/// LLVM_EXTERNAL_VISIBILITY - classes, functions, and variables marked with
/// this attribute will be made public and visible outside of any shared library
/// they are linked in to.

#if LLVM_HAS_CPP_ATTRIBUTE(gnu::visibility)
#define LLVM_ATTRIBUTE_VISIBILITY_HIDDEN [[gnu::visibility("hidden")]]
#define LLVM_ATTRIBUTE_VISIBILITY_DEFAULT [[gnu::visibility("default")]]
#elif __has_attribute(visibility)
#define LLVM_ATTRIBUTE_VISIBILITY_HIDDEN __attribute__((visibility("hidden")))
#define LLVM_ATTRIBUTE_VISIBILITY_DEFAULT __attribute__((visibility("default")))
#else
#define LLVM_ATTRIBUTE_VISIBILITY_HIDDEN
#define LLVM_ATTRIBUTE_VISIBILITY_DEFAULT
#endif


#if (!(defined(_WIN32) || defined(__CYGWIN__)) ||                              \
     (defined(__MINGW32__) && defined(__clang__)))
#define LLVM_LIBRARY_VISIBILITY LLVM_ATTRIBUTE_VISIBILITY_HIDDEN
#if defined(LLVM_BUILD_LLVM_DYLIB) || defined(LLVM_BUILD_SHARED_LIBS)
#define LLVM_EXTERNAL_VISIBILITY LLVM_ATTRIBUTE_VISIBILITY_DEFAULT
#else
#define LLVM_EXTERNAL_VISIBILITY
#endif
#else
#define LLVM_LIBRARY_VISIBILITY
#define LLVM_EXTERNAL_VISIBILITY
#endif

#if defined(__GNUC__)
#define LLVM_PREFETCH(addr, rw, locality) __builtin_prefetch(addr, rw, locality)
#else
#define LLVM_PREFETCH(addr, rw, locality)
#endif

#if __has_attribute(used)
#define LLVM_ATTRIBUTE_USED __attribute__((__used__))
#else
#define LLVM_ATTRIBUTE_USED
#endif

#if defined(__clang__)
#define LLVM_DEPRECATED(MSG, FIX) __attribute__((deprecated(MSG, FIX)))
#else
#define LLVM_DEPRECATED(MSG, FIX) [[deprecated(MSG)]]
#endif

// clang-format off
#if defined(__clang__) || defined(__GNUC__)
#define LLVM_SUPPRESS_DEPRECATED_DECLARATIONS_PUSH                             \
  _Pragma("GCC diagnostic push")                                               \
  _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define LLVM_SUPPRESS_DEPRECATED_DECLARATIONS_POP                              \
  _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#define LLVM_SUPPRESS_DEPRECATED_DECLARATIONS_PUSH                             \
  _Pragma("warning(push)")                                                     \
  _Pragma("warning(disable : 4996)")
#define LLVM_SUPPRESS_DEPRECATED_DECLARATIONS_POP                              \
  _Pragma("warning(pop)")
#else
#define LLVM_SUPPRESS_DEPRECATED_DECLARATIONS_PUSH
#define LLVM_SUPPRESS_DEPRECATED_DECLARATIONS_POP
#endif
// clang-format on

// Indicate that a non-static, non-const C++ member function reinitializes
// the entire object to a known state, independent of the previous state of
// the object.
//
// The clang-tidy check bugprone-use-after-move recognizes this attribute as a
// marker that a moved-from object has left the indeterminate state and can be
// reused.
#if LLVM_HAS_CPP_ATTRIBUTE(clang::reinitializes)
#define LLVM_ATTRIBUTE_REINITIALIZES [[clang::reinitializes]]
#else
#define LLVM_ATTRIBUTE_REINITIALIZES
#endif

// Some compilers warn about unused functions. When a function is sometimes
// used or not depending on build settings (e.g. a function only called from
// within "assert"), this attribute can be used to suppress such warnings.
//
// However, it shouldn't be used for unused *variables*, as those have a much
// more portable solution:
//   (void)unused_var_name;
// Prefer cast-to-void wherever it is sufficient.
#if __has_attribute(unused)
#define LLVM_ATTRIBUTE_UNUSED __attribute__((__unused__))
#else
#define LLVM_ATTRIBUTE_UNUSED
#endif

// FIXME: Provide this for PE/COFF targets.
#if __has_attribute(weak) && !defined(__MINGW32__) && !defined(__CYGWIN__) &&  \
    !defined(_WIN32)
#define LLVM_ATTRIBUTE_WEAK __attribute__((__weak__))
#else
#define LLVM_ATTRIBUTE_WEAK
#endif

// Prior to clang 3.2, clang did not accept any spelling of
// __has_attribute(const), so assume it is supported.
#if defined(__clang__) || defined(__GNUC__)
// aka 'CONST' but following LLVM Conventions.
#define LLVM_READNONE __attribute__((__const__))
#else
#define LLVM_READNONE
#endif

#if __has_attribute(pure) || defined(__GNUC__)
// aka 'PURE' but following LLVM Conventions.
#define LLVM_READONLY __attribute__((__pure__))
#else
#define LLVM_READONLY
#endif

#if __has_attribute(minsize)
#define LLVM_ATTRIBUTE_MINSIZE __attribute__((minsize))
#else
#define LLVM_ATTRIBUTE_MINSIZE
#endif

#if __has_builtin(__builtin_expect) || defined(__GNUC__)
#define LLVM_LIKELY(EXPR) __builtin_expect((bool)(EXPR), true)
#define LLVM_UNLIKELY(EXPR) __builtin_expect((bool)(EXPR), false)
#else
#define LLVM_LIKELY(EXPR) (EXPR)
#define LLVM_UNLIKELY(EXPR) (EXPR)
#endif

/// LLVM_ATTRIBUTE_NOINLINE - On compilers where we have a directive to do so,
/// mark a method "not for inlining".
#if __has_attribute(noinline)
#define LLVM_ATTRIBUTE_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define LLVM_ATTRIBUTE_NOINLINE __declspec(noinline)
#else
#define LLVM_ATTRIBUTE_NOINLINE
#endif

/// LLVM_ATTRIBUTE_ALWAYS_INLINE - On compilers where we have a directive to do
/// so, mark a method "always inline" because it is performance sensitive.
#if __has_attribute(always_inline)
#define LLVM_ATTRIBUTE_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define LLVM_ATTRIBUTE_ALWAYS_INLINE __forceinline
#else
#define LLVM_ATTRIBUTE_ALWAYS_INLINE inline
#endif

/// LLVM_ATTRIBUTE_NO_DEBUG - On compilers where we have a directive to do
/// so, mark a method "no debug" because debug info makes the debugger
/// experience worse.
#if __has_attribute(nodebug)
#define LLVM_ATTRIBUTE_NODEBUG __attribute__((nodebug))
#else
#define LLVM_ATTRIBUTE_NODEBUG
#endif

#if __has_attribute(returns_nonnull)
#define LLVM_ATTRIBUTE_RETURNS_NONNULL __attribute__((returns_nonnull))
#elif defined(_MSC_VER)
#define LLVM_ATTRIBUTE_RETURNS_NONNULL _Ret_notnull_
#else
#define LLVM_ATTRIBUTE_RETURNS_NONNULL
#endif

/// LLVM_ATTRIBUTE_RESTRICT - Annotates a pointer to tell the compiler that
/// it is not aliased in the current scope.
#if defined(__clang__) || defined(__GNUC__) || defined(_MSC_VER)
#define LLVM_ATTRIBUTE_RESTRICT __restrict
#else
#define LLVM_ATTRIBUTE_RESTRICT
#endif

/// \macro LLVM_ATTRIBUTE_RETURNS_NOALIAS Used to mark a function as returning a
/// pointer that does not alias any other valid pointer.
#ifdef __GNUC__
#define LLVM_ATTRIBUTE_RETURNS_NOALIAS __attribute__((__malloc__))
#elif defined(_MSC_VER)
#define LLVM_ATTRIBUTE_RETURNS_NOALIAS __declspec(restrict)
#else
#define LLVM_ATTRIBUTE_RETURNS_NOALIAS
#endif

/// LLVM_FALLTHROUGH - Mark fallthrough cases in switch statements.
#if defined(__cplusplus) && __cplusplus > 201402L && LLVM_HAS_CPP_ATTRIBUTE(fallthrough)
#define LLVM_FALLTHROUGH [[fallthrough]]
#elif LLVM_HAS_CPP_ATTRIBUTE(gnu::fallthrough)
#define LLVM_FALLTHROUGH [[gnu::fallthrough]]
#elif __has_attribute(fallthrough)
#define LLVM_FALLTHROUGH __attribute__((fallthrough))
#elif LLVM_HAS_CPP_ATTRIBUTE(clang::fallthrough)
#define LLVM_FALLTHROUGH [[clang::fallthrough]]
#else
#define LLVM_FALLTHROUGH
#endif

/// LLVM_REQUIRE_CONSTANT_INITIALIZATION - Apply this to globals to ensure that
/// they are constant initialized.
#if LLVM_HAS_CPP_ATTRIBUTE(clang::require_constant_initialization)
#define LLVM_REQUIRE_CONSTANT_INITIALIZATION                                   \
  [[clang::require_constant_initialization]]
#else
#define LLVM_REQUIRE_CONSTANT_INITIALIZATION
#endif

/// LLVM_GSL_OWNER - Apply this to owning classes like SmallVector to enable
/// lifetime warnings.
#if LLVM_HAS_CPP_ATTRIBUTE(gsl::Owner)
#define LLVM_GSL_OWNER [[gsl::Owner]]
#else
#define LLVM_GSL_OWNER
#endif

/// LLVM_GSL_POINTER - Apply this to non-owning classes like
/// StringRef to enable lifetime warnings.
#if LLVM_HAS_CPP_ATTRIBUTE(gsl::Pointer)
#define LLVM_GSL_POINTER [[gsl::Pointer]]
#else
#define LLVM_GSL_POINTER
#endif

#if LLVM_HAS_CPP_ATTRIBUTE(nodiscard) >= 201907L
#define LLVM_CTOR_NODISCARD [[nodiscard]]
#else
#define LLVM_CTOR_NODISCARD
#endif

/// LLVM_EXTENSION - Support compilers where we have a keyword to suppress
/// pedantic diagnostics.
#ifdef __GNUC__
#define LLVM_EXTENSION __extension__
#else
#define LLVM_EXTENSION
#endif

/// LLVM_BUILTIN_UNREACHABLE - On compilers which support it, expands
/// to an expression which states that it is undefined behavior for the
/// compiler to reach this point.  Otherwise is not defined.
///
/// '#else' is intentionally left out so that other macro logic (e.g.,
/// LLVM_ASSUME_ALIGNED and llvm_unreachable()) can detect whether
/// LLVM_BUILTIN_UNREACHABLE has a definition.
#if __has_builtin(__builtin_unreachable) || defined(__GNUC__)
# define LLVM_BUILTIN_UNREACHABLE __builtin_unreachable()
#elif defined(_MSC_VER)
# define LLVM_BUILTIN_UNREACHABLE __assume(false)
#endif

/// LLVM_BUILTIN_TRAP - On compilers which support it, expands to an expression
/// which causes the program to exit abnormally.
#if __has_builtin(__builtin_trap) || defined(__GNUC__)
# define LLVM_BUILTIN_TRAP __builtin_trap()
#elif defined(_MSC_VER)
// The __debugbreak intrinsic is supported by MSVC, does not require forward
// declarations involving platform-specific typedefs (unlike RaiseException),
// results in a call to vectored exception handlers, and encodes to a short
// instruction that still causes the trapping behavior we want.
# define LLVM_BUILTIN_TRAP __debugbreak()
#else
# define LLVM_BUILTIN_TRAP *(volatile int*)0x11 = 0
#endif

/// LLVM_BUILTIN_DEBUGTRAP - On compilers which support it, expands to
/// an expression which causes the program to break while running
/// under a debugger.
#if __has_builtin(__builtin_debugtrap)
# define LLVM_BUILTIN_DEBUGTRAP __builtin_debugtrap()
#elif defined(_MSC_VER)
// The __debugbreak intrinsic is supported by MSVC and breaks while
// running under the debugger, and also supports invoking a debugger
// when the OS is configured appropriately.
# define LLVM_BUILTIN_DEBUGTRAP __debugbreak()
#else
// Just continue execution when built with compilers that have no
// support. This is a debugging aid and not intended to force the
// program to abort if encountered.
# define LLVM_BUILTIN_DEBUGTRAP
#endif

/// \macro LLVM_ASSUME_ALIGNED
/// Returns a pointer with an assumed alignment.
#if __has_builtin(__builtin_assume_aligned) || defined(__GNUC__)
# define LLVM_ASSUME_ALIGNED(p, a) __builtin_assume_aligned(p, a)
#elif defined(LLVM_BUILTIN_UNREACHABLE)
# define LLVM_ASSUME_ALIGNED(p, a) \
           (((uintptr_t(p) % (a)) == 0) ? (p) : (LLVM_BUILTIN_UNREACHABLE, (p)))
#else
# define LLVM_ASSUME_ALIGNED(p, a) (p)
#endif

/// \macro LLVM_PACKED
/// Used to specify a packed structure.
/// LLVM_PACKED(
///    struct A {
///      int i;
///      int j;
///      int k;
///      long long l;
///   });
///
/// LLVM_PACKED_START
/// struct B {
///   int i;
///   int j;
///   int k;
///   long long l;
/// };
/// LLVM_PACKED_END
#ifdef _MSC_VER
# define LLVM_PACKED(d) __pragma(pack(push, 1)) d __pragma(pack(pop))
# define LLVM_PACKED_START __pragma(pack(push, 1))
# define LLVM_PACKED_END   __pragma(pack(pop))
#else
# define LLVM_PACKED(d) d __attribute__((packed))
# define LLVM_PACKED_START _Pragma("pack(push, 1)")
# define LLVM_PACKED_END   _Pragma("pack(pop)")
#endif

/// \macro LLVM_MEMORY_SANITIZER_BUILD
/// Whether LLVM itself is built with MemorySanitizer instrumentation.
#if __has_feature(memory_sanitizer)
# define LLVM_MEMORY_SANITIZER_BUILD 1
# include <sanitizer/msan_interface.h>
# define LLVM_NO_SANITIZE_MEMORY_ATTRIBUTE __attribute__((no_sanitize_memory))
#else
# define LLVM_MEMORY_SANITIZER_BUILD 0
# define __msan_allocated_memory(p, size)
# define __msan_unpoison(p, size)
# define LLVM_NO_SANITIZE_MEMORY_ATTRIBUTE
#endif

/// \macro LLVM_ADDRESS_SANITIZER_BUILD
/// Whether LLVM itself is built with AddressSanitizer instrumentation.
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
# define LLVM_ADDRESS_SANITIZER_BUILD 1
#if __has_include(<sanitizer/asan_interface.h>)
# include <sanitizer/asan_interface.h>
#else
// These declarations exist to support ASan with MSVC. If MSVC eventually ships
// asan_interface.h in their headers, then we can remove this.
#ifdef __cplusplus
extern "C" {
#endif
void __asan_poison_memory_region(void const volatile *addr, size_t size);
void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
#ifdef __cplusplus
} // extern "C"
#endif
#endif
#else
# define LLVM_ADDRESS_SANITIZER_BUILD 0
# define __asan_poison_memory_region(p, size)
# define __asan_unpoison_memory_region(p, size)
#endif

/// \macro LLVM_HWADDRESS_SANITIZER_BUILD
/// Whether LLVM itself is built with HWAddressSanitizer instrumentation.
#if __has_feature(hwaddress_sanitizer)
#define LLVM_HWADDRESS_SANITIZER_BUILD 1
#else
#define LLVM_HWADDRESS_SANITIZER_BUILD 0
#endif

/// \macro LLVM_THREAD_SANITIZER_BUILD
/// Whether LLVM itself is built with ThreadSanitizer instrumentation.
#if __has_feature(thread_sanitizer) || defined(__SANITIZE_THREAD__)
# define LLVM_THREAD_SANITIZER_BUILD 1
#else
# define LLVM_THREAD_SANITIZER_BUILD 0
#endif

#if LLVM_THREAD_SANITIZER_BUILD
// Thread Sanitizer is a tool that finds races in code.
// See http://code.google.com/p/data-race-test/wiki/DynamicAnnotations .
// tsan detects these exact functions by name.
#ifdef __cplusplus
extern "C" {
#endif
void AnnotateHappensAfter(const char *file, int line, const volatile void *cv);
void AnnotateHappensBefore(const char *file, int line, const volatile void *cv);
void AnnotateIgnoreWritesBegin(const char *file, int line);
void AnnotateIgnoreWritesEnd(const char *file, int line);
#ifdef __cplusplus
}
#endif

// This marker is used to define a happens-before arc. The race detector will
// infer an arc from the begin to the end when they share the same pointer
// argument.
# define TsanHappensBefore(cv) AnnotateHappensBefore(__FILE__, __LINE__, cv)

// This marker defines the destination of a happens-before arc.
# define TsanHappensAfter(cv) AnnotateHappensAfter(__FILE__, __LINE__, cv)

// Ignore any races on writes between here and the next TsanIgnoreWritesEnd.
# define TsanIgnoreWritesBegin() AnnotateIgnoreWritesBegin(__FILE__, __LINE__)

// Resume checking for racy writes.
# define TsanIgnoreWritesEnd() AnnotateIgnoreWritesEnd(__FILE__, __LINE__)
#else
# define TsanHappensBefore(cv)
# define TsanHappensAfter(cv)
# define TsanIgnoreWritesBegin()
# define TsanIgnoreWritesEnd()
#endif

/// \macro LLVM_NO_SANITIZE
/// Disable a particular sanitizer for a function.
#if __has_attribute(no_sanitize)
#define LLVM_NO_SANITIZE(KIND) __attribute__((no_sanitize(KIND)))
#else
#define LLVM_NO_SANITIZE(KIND)
#endif

/// Mark debug helper function definitions like dump() that should not be
/// stripped from debug builds.
/// Note that you should also surround dump() functions with
/// `#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)` so they do always
/// get stripped in release builds.
// FIXME: Move this to a private config.h as it's not usable in public headers.
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
#define LLVM_DUMP_METHOD LLVM_ATTRIBUTE_NOINLINE LLVM_ATTRIBUTE_USED
#else
#define LLVM_DUMP_METHOD LLVM_ATTRIBUTE_NOINLINE
#endif

/// \macro LLVM_PRETTY_FUNCTION
/// Gets a user-friendly looking function signature for the current scope
/// using the best available method on each platform.  The exact format of the
/// resulting string is implementation specific and non-portable, so this should
/// only be used, for example, for logging or diagnostics.
#if defined(_MSC_VER)
#define LLVM_PRETTY_FUNCTION __FUNCSIG__
#elif defined(__GNUC__) || defined(__clang__)
#define LLVM_PRETTY_FUNCTION __PRETTY_FUNCTION__
#else
#define LLVM_PRETTY_FUNCTION __func__
#endif

/// \macro LLVM_THREAD_LOCAL
/// A thread-local storage specifier which can be used with globals,
/// extern globals, and static globals.
///
/// This is essentially an extremely restricted analog to C++11's thread_local
/// support. It uses thread_local if available, falling back on gcc __thread
/// if not. __thread doesn't support many of the C++11 thread_local's
/// features. You should only use this for PODs that you can statically
/// initialize to some constant value. In almost all circumstances this is most
/// appropriate for use with a pointer, integer, or small aggregation of
/// pointers and integers.
#if LLVM_ENABLE_THREADS
#if __has_feature(cxx_thread_local) || defined(_MSC_VER)
#define LLVM_THREAD_LOCAL thread_local
#else
// Clang, GCC, and other compatible compilers used __thread prior to C++11 and
// we only need the restricted functionality that provides.
#define LLVM_THREAD_LOCAL __thread
#endif
#else // !LLVM_ENABLE_THREADS
// If threading is disabled entirely, this compiles to nothing and you get
// a normal global variable.
#define LLVM_THREAD_LOCAL
#endif

/// \macro LLVM_ENABLE_EXCEPTIONS
/// Whether LLVM is built with exception support.
#if __has_feature(cxx_exceptions)
#define LLVM_ENABLE_EXCEPTIONS 1
#elif defined(__GNUC__) && defined(__EXCEPTIONS)
#define LLVM_ENABLE_EXCEPTIONS 1
#elif defined(_MSC_VER) && defined(_CPPUNWIND)
#define LLVM_ENABLE_EXCEPTIONS 1
#endif

/// \macro LLVM_NO_PROFILE_INSTRUMENT_FUNCTION
/// Disable the profile instrument for a function.
#if __has_attribute(no_profile_instrument_function)
#define LLVM_NO_PROFILE_INSTRUMENT_FUNCTION                                    \
  __attribute__((no_profile_instrument_function))
#else
#define LLVM_NO_PROFILE_INSTRUMENT_FUNCTION
#endif

/// \macro LLVM_PREFERRED_TYPE
/// Adjust type of bit-field in debug info.
#if __has_attribute(preferred_type)
#define LLVM_PREFERRED_TYPE(T) __attribute__((preferred_type(T)))
#else
#define LLVM_PREFERRED_TYPE(T)
#endif

#endif
