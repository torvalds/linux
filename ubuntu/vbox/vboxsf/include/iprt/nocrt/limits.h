/** @file
 * IPRT / No-CRT - Our own limits header.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
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

#ifndef IPRT_INCLUDED_nocrt_limits_h
#define IPRT_INCLUDED_nocrt_limits_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

#define CHAR_BIT        8
#define SCHAR_MAX       0x7f
#define SCHAR_MIN       (-0x7f - 1)
#define UCHAR_MAX       0xff
#if 1 /* ASSUMES: signed char */
# define CHAR_MAX       SCHAR_MAX
# define CHAR_MIN       SCHAR_MIN
#else
# define CHAR_MAX       UCHAR_MAX
# define CHAR_MIN       0
#endif

#define WORD_BIT        16
#define USHRT_MAX       0xffff
#define SHRT_MAX        0x7fff
#define SHRT_MIN        (-0x7fff - 1)

/* ASSUMES 32-bit int */
#define UINT_MAX        0xffffffffU
#define INT_MAX         0x7fffffff
#define INT_MIN         (-0x7fffffff - 1)

#if defined(RT_ARCH_X86) || defined(RT_OS_WINDOWS) || defined(RT_ARCH_SPARC)
# define LONG_BIT       32
# define ULONG_MAX      0xffffffffU
# define LONG_MAX       0x7fffffff
# define LONG_MIN       (-0x7fffffff - 1)
#elif defined(RT_ARCH_AMD64) || defined(RT_ARCH_SPARC64)
# define LONG_BIT       64
# define ULONG_MAX      UINT64_C(0xffffffffffffffff)
# define LONG_MAX       INT64_C(0x7fffffffffffffff)
# define LONG_MIN       (INT64_C(-0x7fffffffffffffff) - 1)
#else
# error "PORTME"
#endif

#define LLONG_BIT       64
#define ULLONG_MAX      UINT64_C(0xffffffffffffffff)
#define LLONG_MAX       INT64_C(0x7fffffffffffffff)
#define LLONG_MIN       (INT64_C(-0x7fffffffffffffff) - 1)

#if ARCH_BITS == 32
# define SIZE_T_MAX     0xffffffffU
# define SSIZE_MAX      0x7fffffff
#elif ARCH_BITS == 64
# define SIZE_T_MAX     UINT64_C(0xffffffffffffffff)
# define SSIZE_MAX      INT64_C(0x7fffffffffffffff)
#else
# error "huh?"
#endif

/*#define OFF_MAX         __OFF_MAX
#define OFF_MIN         __OFF_MIN*/

#endif /* !IPRT_INCLUDED_nocrt_limits_h */

