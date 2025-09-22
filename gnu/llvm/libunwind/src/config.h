//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  Defines macros used within libunwind project.
//
//===----------------------------------------------------------------------===//


#ifndef LIBUNWIND_CONFIG_H
#define LIBUNWIND_CONFIG_H

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <__libunwind_config.h>

// Platform specific configuration defines.
#ifdef __APPLE__
  #if defined(FOR_DYLD)
    #define _LIBUNWIND_SUPPORT_COMPACT_UNWIND 1
  #else
    #define _LIBUNWIND_SUPPORT_COMPACT_UNWIND 1
    #define _LIBUNWIND_SUPPORT_DWARF_UNWIND 1
  #endif
#elif defined(_WIN32)
  #ifdef __SEH__
    #define _LIBUNWIND_SUPPORT_SEH_UNWIND 1
  #else
    #define _LIBUNWIND_SUPPORT_DWARF_UNWIND 1
  #endif
#elif defined(_LIBUNWIND_IS_BAREMETAL)
  #if !defined(_LIBUNWIND_ARM_EHABI)
    #define _LIBUNWIND_SUPPORT_DWARF_UNWIND 1
    #define _LIBUNWIND_SUPPORT_DWARF_INDEX 1
  #endif
#elif defined(__BIONIC__) && defined(_LIBUNWIND_ARM_EHABI)
  // For ARM EHABI, Bionic didn't implement dl_iterate_phdr until API 21. After
  // API 21, dl_iterate_phdr exists, but dl_unwind_find_exidx is much faster.
  #define _LIBUNWIND_USE_DL_UNWIND_FIND_EXIDX 1
#elif defined(_AIX)
// The traceback table at the end of each function is used for unwinding.
#define _LIBUNWIND_SUPPORT_TBTAB_UNWIND 1
#elif defined(__HAIKU__)
  #if defined(_LIBUNWIND_USE_HAIKU_BSD_LIB)
    #define _LIBUNWIND_USE_DL_ITERATE_PHDR 1
  #endif
  #define _LIBUNWIND_SUPPORT_DWARF_UNWIND 1
  #define _LIBUNWIND_SUPPORT_DWARF_INDEX 1
#else
  // Assume an ELF system with a dl_iterate_phdr function.
  #define _LIBUNWIND_USE_DL_ITERATE_PHDR 1
  #if !defined(_LIBUNWIND_ARM_EHABI)
    #define _LIBUNWIND_SUPPORT_DWARF_UNWIND 1
    #define _LIBUNWIND_SUPPORT_DWARF_INDEX 1
  #endif
#endif

#if defined(_LIBUNWIND_HIDE_SYMBOLS)
  // The CMake file passes -fvisibility=hidden to control ELF/Mach-O visibility.
  #define _LIBUNWIND_EXPORT
  #define _LIBUNWIND_HIDDEN
#else
  #if !defined(__ELF__) && !defined(__MACH__) && !defined(_AIX)
    #define _LIBUNWIND_EXPORT __declspec(dllexport)
    #define _LIBUNWIND_HIDDEN
  #else
    #define _LIBUNWIND_EXPORT __attribute__((visibility("default")))
    #define _LIBUNWIND_HIDDEN __attribute__((visibility("hidden")))
  #endif
#endif

#define STR(a) #a
#define XSTR(a) STR(a)
#define SYMBOL_NAME(name) XSTR(__USER_LABEL_PREFIX__) #name

#if defined(__APPLE__)
#if defined(_LIBUNWIND_HIDE_SYMBOLS)
#define _LIBUNWIND_ALIAS_VISIBILITY(name) __asm__(".private_extern " name);
#else
#define _LIBUNWIND_ALIAS_VISIBILITY(name)
#endif
#define _LIBUNWIND_WEAK_ALIAS(name, aliasname)                                 \
  __asm__(".globl " SYMBOL_NAME(aliasname));                                   \
  __asm__(SYMBOL_NAME(aliasname) " = " SYMBOL_NAME(name));                     \
  _LIBUNWIND_ALIAS_VISIBILITY(SYMBOL_NAME(aliasname))
#elif defined(__ELF__) || defined(_AIX) || defined(__wasm__)
#define _LIBUNWIND_WEAK_ALIAS(name, aliasname)                                 \
  extern "C" _LIBUNWIND_EXPORT __typeof(name) aliasname                        \
      __attribute__((weak, alias(#name)));
#elif defined(_WIN32)
#if defined(__MINGW32__)
#define _LIBUNWIND_WEAK_ALIAS(name, aliasname)                                 \
  extern "C" _LIBUNWIND_EXPORT __typeof(name) aliasname                        \
      __attribute__((alias(#name)));
#else
#define _LIBUNWIND_WEAK_ALIAS(name, aliasname)                                 \
  __pragma(comment(linker, "/alternatename:" SYMBOL_NAME(aliasname) "="        \
                                             SYMBOL_NAME(name)))               \
  extern "C" _LIBUNWIND_EXPORT __typeof(name) aliasname;
#endif
#else
#error Unsupported target
#endif

// Apple/armv7k defaults to DWARF/Compact unwinding, but its libunwind also
// needs to include the SJLJ APIs.
#if (defined(__APPLE__) && defined(__arm__)) || defined(__USING_SJLJ_EXCEPTIONS__)
#define _LIBUNWIND_BUILD_SJLJ_APIS
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(__powerpc__) ||        \
    (!defined(__APPLE__) && defined(__arm__)) || defined(__aarch64__) ||       \
    defined(__mips__) || defined(__riscv) || defined(__hexagon__) ||           \
    defined(__sparc__) || defined(__s390x__) || defined(__loongarch__)
#if !defined(_LIBUNWIND_BUILD_SJLJ_APIS)
#define _LIBUNWIND_BUILD_ZERO_COST_APIS
#endif
#endif

#ifndef _LIBUNWIND_REMEMBER_HEAP_ALLOC
#if defined(_LIBUNWIND_REMEMBER_STACK_ALLOC) || defined(__APPLE__) ||          \
    defined(__linux__) || defined(__ANDROID__) || defined(__MINGW32__) ||      \
    defined(_LIBUNWIND_IS_BAREMETAL)
#define _LIBUNWIND_REMEMBER_ALLOC(_size) __builtin_alloca(_size)
#define _LIBUNWIND_REMEMBER_FREE(_ptr)                                         \
  do {                                                                         \
  } while (0)
#elif defined(_WIN32)
#define _LIBUNWIND_REMEMBER_ALLOC(_size) _malloca(_size)
#define _LIBUNWIND_REMEMBER_FREE(_ptr) _freea(_ptr)
#define _LIBUNWIND_REMEMBER_CLEANUP_NEEDED
#else
#define _LIBUNWIND_REMEMBER_ALLOC(_size) malloc(_size)
#define _LIBUNWIND_REMEMBER_FREE(_ptr) free(_ptr)
#define _LIBUNWIND_REMEMBER_CLEANUP_NEEDED
#endif
#else /* _LIBUNWIND_REMEMBER_HEAP_ALLOC */
#define _LIBUNWIND_REMEMBER_ALLOC(_size) malloc(_size)
#define _LIBUNWIND_REMEMBER_FREE(_ptr) free(_ptr)
#define _LIBUNWIND_REMEMBER_CLEANUP_NEEDED
#endif

#if defined(NDEBUG) && defined(_LIBUNWIND_IS_BAREMETAL)
#define _LIBUNWIND_ABORT(msg)                                                  \
  do {                                                                         \
    abort();                                                                   \
  } while (0)
#else
#define _LIBUNWIND_ABORT(msg)                                                  \
  do {                                                                         \
    fprintf(stderr, "libunwind: %s - %s\n", __func__, msg);                    \
    fflush(stderr);                                                            \
    abort();                                                                   \
  } while (0)
#endif

#if defined(NDEBUG) && defined(_LIBUNWIND_IS_BAREMETAL)
#define _LIBUNWIND_LOG0(msg)
#define _LIBUNWIND_LOG(msg, ...)
#else
#define _LIBUNWIND_LOG0(msg) do {                                              \
    fprintf(stderr, "libunwind: " msg "\n");                                   \
    fflush(stderr);                                                            \
  } while (0)
#define _LIBUNWIND_LOG(msg, ...) do {                                          \
    fprintf(stderr, "libunwind: " msg "\n", __VA_ARGS__);                      \
    fflush(stderr);                                                            \
  } while (0)
#endif

#if defined(NDEBUG)
  #define _LIBUNWIND_LOG_IF_FALSE(x) x
#else
  #define _LIBUNWIND_LOG_IF_FALSE(x)                                           \
    do {                                                                       \
      bool _ret = x;                                                           \
      if (!_ret)                                                               \
        _LIBUNWIND_LOG("" #x " failed in %s", __FUNCTION__);                   \
    } while (0)
#endif

// Macros that define away in non-Debug builds
#ifdef NDEBUG
  #define _LIBUNWIND_DEBUG_LOG(msg, ...)
  #define _LIBUNWIND_TRACE_API(msg, ...)
  #define _LIBUNWIND_TRACING_UNWINDING (0)
  #define _LIBUNWIND_TRACING_DWARF (0)
  #define _LIBUNWIND_TRACE_UNWINDING(msg, ...)
  #define _LIBUNWIND_TRACE_DWARF(...)
#else
  #ifdef __cplusplus
    extern "C" {
  #endif
    extern  bool logAPIs(void);
    extern  bool logUnwinding(void);
    extern  bool logDWARF(void);
  #ifdef __cplusplus
    }
  #endif
  #define _LIBUNWIND_DEBUG_LOG(msg, ...)  _LIBUNWIND_LOG(msg, __VA_ARGS__)
  #define _LIBUNWIND_TRACE_API(msg, ...)                                       \
    do {                                                                       \
      if (logAPIs())                                                           \
        _LIBUNWIND_LOG(msg, __VA_ARGS__);                                      \
    } while (0)
  #define _LIBUNWIND_TRACING_UNWINDING logUnwinding()
  #define _LIBUNWIND_TRACING_DWARF logDWARF()
  #define _LIBUNWIND_TRACE_UNWINDING(msg, ...)                                 \
    do {                                                                       \
      if (logUnwinding())                                                      \
        _LIBUNWIND_LOG(msg, __VA_ARGS__);                                      \
    } while (0)
  #define _LIBUNWIND_TRACE_DWARF(...)                                          \
    do {                                                                       \
      if (logDWARF())                                                          \
        fprintf(stderr, __VA_ARGS__);                                          \
    } while (0)
#endif

#ifdef __cplusplus
// Used to fit UnwindCursor and Registers_xxx types against unw_context_t /
// unw_cursor_t sized memory blocks.
#if defined(_LIBUNWIND_IS_NATIVE_ONLY)
# define COMP_OP ==
#else
# define COMP_OP <=
#endif
template <typename _Type, typename _Mem>
struct check_fit {
  template <typename T>
  struct blk_count {
    static const size_t count =
      (sizeof(T) + sizeof(uint64_t) - 1) / sizeof(uint64_t);
  };
  static const bool does_fit =
    (blk_count<_Type>::count COMP_OP blk_count<_Mem>::count);
};
#undef COMP_OP
#endif // __cplusplus

#endif // LIBUNWIND_CONFIG_H
