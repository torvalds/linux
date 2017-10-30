/** @file
 * IPRT - stdint.h wrapper (for backlevel compilers like MSC).
 */

/*
 * Copyright (C) 2009-2017 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef __iprt_stdint_h
#define __iprt_stdint_h

#include <iprt/cdefs.h>


/*
 * Use the stdint.h on systems that have one.
 */
#if !(defined(RT_OS_LINUX) && defined(__KERNEL__))  \
  && !(defined(RT_OS_FREEBSD) && defined(_KERNEL)) \
  && !(defined(RT_OS_NETBSD) && defined(_KERNEL)) \
  && RT_MSC_PREREQ_EX(RT_MSC_VER_VS2010, 1 /*non-msc*/) \
  && !defined(__IBMC__) \
  && !defined(__IBMCPP__) \
  && !defined(IPRT_NO_CRT) \
  && !defined(IPRT_DONT_USE_SYSTEM_STDINT_H) \
  && !defined(DOXYGEN_RUNNING)

# ifndef __STDC_CONSTANT_MACROS
#  define __STDC_CONSTANT_MACROS
# endif
# ifndef __STDC_LIMIT_MACROS
#  define __STDC_LIMIT_MACROS
# endif
# ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable:4668)
# endif
# include <stdint.h>
# ifdef _MSC_VER
#  pragma warning(pop)
# endif

# if defined(RT_OS_DARWIN) && defined(KERNEL) && defined(RT_ARCH_AMD64)
 /*
  * Kludge to fix the incorrect 32-bit constant macros in
  * Kernel.framework/Headers/stdin.h. uint32_t and int32_t are
  * int not long as these macros use, which is significant when
  * targeting AMD64. (10a222)
  */
#  undef  INT32_C
#  define INT32_C(Value)    (Value)
#  undef  UINT32_C
#  define UINT32_C(Value)   (Value ## U)
# endif /* 64-bit darwin kludge. */

#elif defined(RT_OS_FREEBSD) && defined(_KERNEL)

# ifndef __STDC_CONSTANT_MACROS
#  define __STDC_CONSTANT_MACROS
# endif
# ifndef __STDC_LIMIT_MACROS
#  define __STDC_LIMIT_MACROS
# endif
# include <sys/stdint.h>

#elif defined(RT_OS_NETBSD) && defined(_KERNEL)

# ifndef __STDC_CONSTANT_MACROS
#  define __STDC_CONSTANT_MACROS
# endif
# ifndef __STDC_LIMIT_MACROS
#  define __STDC_LIMIT_MACROS
# endif
# include <sys/stdint.h>

#else /* No system stdint.h */

/*
 * Define the types we use.
 * The linux kernel defines all these in linux/types.h, so skip it.
 */
# if !(defined(RT_OS_LINUX) && defined(__KERNEL__)) \
  || defined(IPRT_NO_CRT) \
  || defined(IPRT_DONT_USE_SYSTEM_STDINT_H) \
  || defined(DOXGEN_RUNNING)

    /* Simplify the [u]int64_t type detection mess. */
# undef IPRT_STDINT_USE_STRUCT_FOR_64_BIT_TYPES
# ifdef __IBMCPP__
#  if __IBMCPP__ < 350 && (defined(__WINDOWS__) || defined(_AIX) || defined(__OS2__))
#   define IPRT_STDINT_USE_STRUCT_FOR_64_BIT_TYPES
#  endif
# endif
# ifdef __IBMC__
#  if __IBMC__   < 350 && (defined(__WINDOWS__) || defined(_AIX) || defined(__OS2__))
#   define IPRT_STDINT_USE_STRUCT_FOR_64_BIT_TYPES
#  endif
# endif

    /* x-bit types */
#  if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) || defined(RT_ARCH_SPARC) || defined(RT_ARCH_SPARC64)
#   if !defined(_INT8_T_DECLARED)   && !defined(_INT8_T)
typedef signed char         int8_t;
#   endif
#   if !defined(_UINT8_T_DECLARED)  && !defined(_UINT8_T)
typedef unsigned char       uint8_t;
#   endif
#   if !defined(_INT16_T_DECLARED)  && !defined(_INT16_T)
typedef signed short        int16_t;
#   endif
#   if !defined(_UINT16_T_DECLARED) && !defined(_UINT16_T)
typedef unsigned short      uint16_t;
#   endif
#   if !defined(_INT32_T_DECLARED)  && !defined(_INT32_T)
#    if ARCH_BITS != 16
typedef signed int          int32_t;
#    else
typedef signed long         int32_t;
#    endif
#   endif
#   if !defined(_UINT32_T_DECLARED) && !defined(_UINT32_T)
#    if ARCH_BITS != 16
typedef unsigned int        uint32_t;
#    else
typedef unsigned long       uint32_t;
#    endif
#   endif
#   if defined(_MSC_VER)
#    if !defined(_INT64_T_DECLARED)  && !defined(_INT64_T)
typedef signed _int64       int64_t;
#    endif
#    if !defined(_UINT64_T_DECLARED) && !defined(_UINT64_T)
typedef unsigned _int64     uint64_t;
#    endif
#   elif defined(__WATCOMC__)
#    if !defined(_INT64_T_DECLARED)  && !defined(_INT64_T)
typedef signed __int64      int64_t;
#    endif
#    if !defined(_UINT64_T_DECLARED) && !defined(_UINT64_T)
typedef unsigned __int64    uint64_t;
#    endif
#   elif defined(IPRT_STDINT_USE_STRUCT_FOR_64_BIT_TYPES)
#    if !defined(_INT64_T_DECLARED)  && !defined(_INT64_T)
typedef struct { uint32_t lo; int32_t hi; }     int64_t;
#    endif
#    if !defined(_UINT64_T_DECLARED) && !defined(_UINT64_T)
typedef struct { uint32_t lo; uint32_t hi; }    uint64_t;
#    endif
#   else /* Use long long for 64-bit types */
#    if !defined(_INT64_T_DECLARED)  && !defined(_INT64_T)
typedef signed long long    int64_t;
#    endif
#    if !defined(_UINT64_T_DECLARED) && !defined(_UINT64_T)
typedef unsigned long long  uint64_t;
#    endif
#   endif

    /* max integer types */
#   if !defined(_INTMAX_T_DECLARED)  && !defined(_INTMAX_T)
typedef int64_t             intmax_t;
#   endif
#   if !defined(_UINTMAX_T_DECLARED) && !defined(_UINTMAX_T)
typedef uint64_t            uintmax_t;
#   endif

#  else
#   error "PORTME: Add architecture. Don't forget to check the [U]INTx_C() and [U]INTMAX_MIN/MAX macros."
#  endif

# endif /* !linux kernel or stuff */

    /* pointer <-> integer types */
# if !defined(_MSC_VER) || defined(DOXYGEN_RUNNING)
#  if ARCH_BITS == 32 \
   || defined(RT_OS_LINUX) \
   || defined(RT_OS_FREEBSD)
#   if !defined(_INTPTR_T_DECLARED)  && !defined(_INTPTR_T)
typedef signed long         intptr_t;
#   endif
#   if !defined(_UINTPTR_T_DECLARED) && !defined(_UINTPTR_T)
typedef unsigned long       uintptr_t;
#   endif
#  else
#   if !defined(_INTPTR_T_DECLARED)  && !defined(_INTPTR_T)
typedef int64_t             intptr_t;
#   endif
#   if !defined(_UINTPTR_T_DECLARED) && !defined(_UINTPTR_T)
typedef uint64_t            uintptr_t;
#   endif
#  endif
# endif /* !_MSC_VER */

#endif /* no system stdint.h */


/*
 * Make sure the [U]INTx_C(c) macros are present.
 * For In C++ source the system stdint.h may have skipped these if it was
 * included before we managed to define __STDC_CONSTANT_MACROS. (Kludge alert!)
 */
#if !defined(INT8_C) \
 || !defined(INT16_C) \
 || !defined(INT32_C) \
 || !defined(INT64_C) \
 || !defined(INTMAX_C) \
 || !defined(UINT8_C) \
 || !defined(UINT16_C) \
 || !defined(UINT32_C) \
 || !defined(UINT64_C) \
 || !defined(UINTMAX_C)
# define INT8_C(Value)      (Value)
# define INT16_C(Value)     (Value)
# define UINT8_C(Value)     (Value)
# define UINT16_C(Value)    (Value)
# if ARCH_BITS != 16
#  define INT32_C(Value)    (Value)
#  define UINT32_C(Value)   (Value ## U)
#  define INT64_C(Value)    (Value ## LL)
#  define UINT64_C(Value)   (Value ## ULL)
# else
#  define INT32_C(Value)    (Value ## L)
#  define UINT32_C(Value)   (Value ## UL)
#  define INT64_C(Value)    (Value ## LL)
#  define UINT64_C(Value)   (Value ## ULL)
# endif
# define INTMAX_C(Value)    INT64_C(Value)
# define UINTMAX_C(Value)   UINT64_C(Value)
#endif


/*
 * Make sure the INTx_MIN and [U]INTx_MAX macros are present.
 * For In C++ source the system stdint.h may have skipped these if it was
 * included before we managed to define __STDC_LIMIT_MACROS. (Kludge alert!)
 */
#if !defined(INT8_MIN) \
 || !defined(INT16_MIN) \
 || !defined(INT32_MIN) \
 || !defined(INT64_MIN) \
 || !defined(INT8_MAX) \
 || !defined(INT16_MAX) \
 || !defined(INT32_MAX) \
 || !defined(INT64_MAX) \
 || !defined(UINT8_MAX) \
 || !defined(UINT16_MAX) \
 || !defined(UINT32_MAX) \
 || !defined(UINT64_MAX)
# define INT8_MIN           (INT8_C(-0x7f)                - 1)
# define INT16_MIN          (INT16_C(-0x7fff)             - 1)
# define INT32_MIN          (INT32_C(-0x7fffffff)         - 1)
# define INT64_MIN          (INT64_C(-0x7fffffffffffffff) - 1)
# define INT8_MAX           INT8_C(0x7f)
# define INT16_MAX          INT16_C(0x7fff)
# define INT32_MAX          INT32_C(0x7fffffff)
# define INT64_MAX          INT64_C(0x7fffffffffffffff)
# define UINT8_MAX          UINT8_C(0xff)
# define UINT16_MAX         UINT16_C(0xffff)
# define UINT32_MAX         UINT32_C(0xffffffff)
# define UINT64_MAX         UINT64_C(0xffffffffffffffff)

# define INTMAX_MIN         INT64_MIN
# define INTMAX_MAX         INT64_MAX
# define UINTMAX_MAX        UINT64_MAX
#endif

#endif

