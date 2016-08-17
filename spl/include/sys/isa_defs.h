/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef	_SPL_ISA_DEFS_H
#define	_SPL_ISA_DEFS_H

/* x86_64 arch specific defines */
#if defined(__x86_64) || defined(__x86_64__)

#if !defined(__x86_64)
#define __x86_64
#endif

#if !defined(__amd64)
#define __amd64
#endif

#if !defined(__x86)
#define __x86
#endif

#if !defined(_LP64)
#define _LP64
#endif

/* i386 arch specific defines */
#elif defined(__i386) || defined(__i386__)

#if !defined(__i386)
#define __i386
#endif

#if !defined(__x86)
#define __x86
#endif

#if !defined(_ILP32)
#define _ILP32
#endif

/* powerpc (ppc64) arch specific defines */
#elif defined(__powerpc) || defined(__powerpc__) || defined(__powerpc64__)

#if !defined(__powerpc)
#define __powerpc
#endif

#if !defined(__powerpc__)
#define __powerpc__
#endif

#if defined(__powerpc64__)
#if !defined(_LP64)
#define _LP64
#endif
#else
#if !defined(_ILP32)
#define _ILP32
#endif
#endif

/* arm arch specific defines */
#elif defined(__arm) || defined(__arm__) || defined(__aarch64__)

#if !defined(__arm)
#define __arm
#endif

#if !defined(__arm__)
#define __arm__
#endif

#if defined(__aarch64__)
#if !defined(_LP64)
#define _LP64
#endif
#else
#if !defined(_ILP32)
#define _ILP32
#endif
#endif

#if defined(__ARMEL__) || defined(__AARCH64EL__)
#define _LITTLE_ENDIAN
#else
#define _BIG_ENDIAN
#endif

/* sparc arch specific defines */
#elif defined(__sparc) || defined(__sparc__)

#if !defined(__sparc)
#define __sparc
#endif

#if !defined(__sparc__)
#define __sparc__
#endif

#if defined(__arch64__)
#if !defined(_LP64)
#define _LP64
#endif
#else
#if !defined(_ILP32)
#define _ILP32
#endif
#endif

#define _BIG_ENDIAN
#define _SUNOS_VTOC_16

/* s390 arch specific defines */
#elif defined(__s390__)
#if defined(__s390x__)
#if !defined(_LP64)
#define	_LP64
#endif
#else
#if !defined(_ILP32)
#define	_ILP32
#endif
#endif

#define	_BIG_ENDIAN

/* MIPS arch specific defines */
#elif defined(__mips__)

#if defined(__MIPSEB__)
#define	_BIG_ENDIAN
#elif defined(__MIPSEL__)
#define	_LITTLE_ENDIAN
#else
#error MIPS no endian specified
#endif

#ifndef _LP64
#define	_ILP32
#endif

#define	_SUNOS_VTOC_16

#else
/*
 * Currently supported:
 * x86_64, i386, arm, powerpc, s390, sparc, and mips
 */
#error "Unsupported ISA type"
#endif

#if defined(_ILP32) && defined(_LP64)
#error "Both _ILP32 and _LP64 are defined"
#endif

#if !defined(_ILP32) && !defined(_LP64)
#error "Neither _ILP32 or _LP64 are defined"
#endif

#include <sys/byteorder.h>

#if defined(__LITTLE_ENDIAN) && !defined(_LITTLE_ENDIAN)
#define _LITTLE_ENDIAN __LITTLE_ENDIAN
#endif

#if defined(__BIG_ENDIAN) && !defined(_BIG_ENDIAN)
#define _BIG_ENDIAN __BIG_ENDIAN
#endif

#if defined(_LITTLE_ENDIAN) && defined(_BIG_ENDIAN)
#error "Both _LITTLE_ENDIAN and _BIG_ENDIAN are defined"
#endif

#if !defined(_LITTLE_ENDIAN) && !defined(_BIG_ENDIAN)
#error "Neither _LITTLE_ENDIAN or _BIG_ENDIAN are defined"
#endif

#endif	/* _SPL_ISA_DEFS_H */
