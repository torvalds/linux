/*
 * Copyright (c) Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_COMPILER_H
#define ZSTD_COMPILER_H

#include "portability_macros.h"

/*-*******************************************************
*  Compiler specifics
*********************************************************/
/* force inlining */

#if !defined(ZSTD_NO_INLINE)
#if (defined(__GNUC__) && !defined(__STRICT_ANSI__)) || defined(__cplusplus) || defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
#  define INLINE_KEYWORD inline
#else
#  define INLINE_KEYWORD
#endif

#define FORCE_INLINE_ATTR __attribute__((always_inline))

#else

#define INLINE_KEYWORD
#define FORCE_INLINE_ATTR

#endif

/*
  On MSVC qsort requires that functions passed into it use the __cdecl calling conversion(CC).
  This explicitly marks such functions as __cdecl so that the code will still compile
  if a CC other than __cdecl has been made the default.
*/
#define WIN_CDECL

/*
 * FORCE_INLINE_TEMPLATE is used to define C "templates", which take constant
 * parameters. They must be inlined for the compiler to eliminate the constant
 * branches.
 */
#define FORCE_INLINE_TEMPLATE static INLINE_KEYWORD FORCE_INLINE_ATTR
/*
 * HINT_INLINE is used to help the compiler generate better code. It is *not*
 * used for "templates", so it can be tweaked based on the compilers
 * performance.
 *
 * gcc-4.8 and gcc-4.9 have been shown to benefit from leaving off the
 * always_inline attribute.
 *
 * clang up to 5.0.0 (trunk) benefit tremendously from the always_inline
 * attribute.
 */
#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 4 && __GNUC_MINOR__ >= 8 && __GNUC__ < 5
#  define HINT_INLINE static INLINE_KEYWORD
#else
#  define HINT_INLINE static INLINE_KEYWORD FORCE_INLINE_ATTR
#endif

/* UNUSED_ATTR tells the compiler it is okay if the function is unused. */
#define UNUSED_ATTR __attribute__((unused))

/* force no inlining */
#define FORCE_NOINLINE static __attribute__((__noinline__))


/* target attribute */
#define TARGET_ATTRIBUTE(target) __attribute__((__target__(target)))

/* Target attribute for BMI2 dynamic dispatch.
 * Enable lzcnt, bmi, and bmi2.
 * We test for bmi1 & bmi2. lzcnt is included in bmi1.
 */
#define BMI2_TARGET_ATTRIBUTE TARGET_ATTRIBUTE("lzcnt,bmi,bmi2")

/* prefetch
 * can be disabled, by declaring NO_PREFETCH build macro */
#if ( (__GNUC__ >= 4) || ( (__GNUC__ == 3) && (__GNUC_MINOR__ >= 1) ) )
#  define PREFETCH_L1(ptr)  __builtin_prefetch((ptr), 0 /* rw==read */, 3 /* locality */)
#  define PREFETCH_L2(ptr)  __builtin_prefetch((ptr), 0 /* rw==read */, 2 /* locality */)
#elif defined(__aarch64__)
#  define PREFETCH_L1(ptr)  __asm__ __volatile__("prfm pldl1keep, %0" ::"Q"(*(ptr)))
#  define PREFETCH_L2(ptr)  __asm__ __volatile__("prfm pldl2keep, %0" ::"Q"(*(ptr)))
#else
#  define PREFETCH_L1(ptr) (void)(ptr)  /* disabled */
#  define PREFETCH_L2(ptr) (void)(ptr)  /* disabled */
#endif  /* NO_PREFETCH */

#define CACHELINE_SIZE 64

#define PREFETCH_AREA(p, s)  {            \
    const char* const _ptr = (const char*)(p);  \
    size_t const _size = (size_t)(s);     \
    size_t _pos;                          \
    for (_pos=0; _pos<_size; _pos+=CACHELINE_SIZE) {  \
        PREFETCH_L2(_ptr + _pos);         \
    }                                     \
}

/* vectorization
 * older GCC (pre gcc-4.3 picked as the cutoff) uses a different syntax,
 * and some compilers, like Intel ICC and MCST LCC, do not support it at all. */
#if !defined(__INTEL_COMPILER) && !defined(__clang__) && defined(__GNUC__) && !defined(__LCC__)
#  if (__GNUC__ == 4 && __GNUC_MINOR__ > 3) || (__GNUC__ >= 5)
#    define DONT_VECTORIZE __attribute__((optimize("no-tree-vectorize")))
#  else
#    define DONT_VECTORIZE _Pragma("GCC optimize(\"no-tree-vectorize\")")
#  endif
#else
#  define DONT_VECTORIZE
#endif

/* Tell the compiler that a branch is likely or unlikely.
 * Only use these macros if it causes the compiler to generate better code.
 * If you can remove a LIKELY/UNLIKELY annotation without speed changes in gcc
 * and clang, please do.
 */
#define LIKELY(x) (__builtin_expect((x), 1))
#define UNLIKELY(x) (__builtin_expect((x), 0))

#if __has_builtin(__builtin_unreachable) || (defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)))
#  define ZSTD_UNREACHABLE { assert(0), __builtin_unreachable(); }
#else
#  define ZSTD_UNREACHABLE { assert(0); }
#endif

/* disable warnings */

/*Like DYNAMIC_BMI2 but for compile time determination of BMI2 support*/


/* compile time determination of SIMD support */

/* C-language Attributes are added in C23. */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ > 201710L) && defined(__has_c_attribute)
# define ZSTD_HAS_C_ATTRIBUTE(x) __has_c_attribute(x)
#else
# define ZSTD_HAS_C_ATTRIBUTE(x) 0
#endif

/* Only use C++ attributes in C++. Some compilers report support for C++
 * attributes when compiling with C.
 */
#define ZSTD_HAS_CPP_ATTRIBUTE(x) 0

/* Define ZSTD_FALLTHROUGH macro for annotating switch case with the 'fallthrough' attribute.
 * - C23: https://en.cppreference.com/w/c/language/attributes/fallthrough
 * - CPP17: https://en.cppreference.com/w/cpp/language/attributes/fallthrough
 * - Else: __attribute__((__fallthrough__))
 */
#define ZSTD_FALLTHROUGH fallthrough

/*-**************************************************************
*  Alignment check
*****************************************************************/

/* this test was initially positioned in mem.h,
 * but this file is removed (or replaced) for linux kernel
 * so it's now hosted in compiler.h,
 * which remains valid for both user & kernel spaces.
 */

#ifndef ZSTD_ALIGNOF
/* covers gcc, clang & MSVC */
/* note : this section must come first, before C11,
 * due to a limitation in the kernel source generator */
#  define ZSTD_ALIGNOF(T) __alignof(T)

#endif /* ZSTD_ALIGNOF */

/*-**************************************************************
*  Sanitizer
*****************************************************************/



#endif /* ZSTD_COMPILER_H */
