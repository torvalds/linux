/*	$NetBSD: cdefs.h,v 1.12 2006/08/27 19:04:30 matt Exp $	*/

/*-
 * SPDX-License-Identifier: MIT-CMU
 *
 * Copyright (c) 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD$
 */

#ifndef _MIPS_CDEFS_H_
#define	_MIPS_CDEFS_H_

/*
 * These are depreciated.  Use __mips_{o32,o64,n32,n64} instead.
 */
/*      MIPS Subprogram Interface Model */
#define _MIPS_SIM_ABIX32	4	/* 64 bit safe, ILP32 o32 model */
#define _MIPS_SIM_ABI64		3
#define _MIPS_SIM_NABI32	2	/* 64bit safe, ILP32 n32 model */
#define _MIPS_SIM_ABI32		1

#define _MIPS_BSD_API_LP32	_MIPS_SIM_ABI32
#define	_MIPS_BSD_API_LP32_64CLEAN	_MIPS_SIM_ABIX32
#define	_MIPS_BSD_API_LP64	_MIPS_SIM_ABI64

#define _MIPS_BSD_API_O32	_MIPS_SIM_ABI32
#define	_MIPS_BSD_API_O64	_MIPS_SIM_ABIX32
#define	_MIPS_BSD_API_N32	_MIPS_SIM_NABI32
#define	_MIPS_BSD_API_N64	_MIPS_SIM_ABI64

#define	_MIPS_SIM_NEWABI_P(abi)	((abi) == _MIPS_SIM_NABI32 || \
				 (abi) == _MIPS_SIM_ABI64)

#define	_MIPS_SIM_LP64_P(abi)	((abi) == _MIPS_SIM_ABIX32 || \
				 (abi) == _MIPS_SIM_ABI64)

#if defined(__mips_n64)
#define	_MIPS_BSD_API		_MIPS_BSD_API_N64
#elif defined(__mips_n32)
#define	_MIPS_BSD_API		_MIPS_BSD_API_N32
#elif defined(__mips_o64)
#define	_MIPS_BSD_API		_MIPS_BSD_API_O64
#else
#define	_MIPS_BSD_API		_MIPS_BSD_API_O32
#endif

#define	_MIPS_ISA_MIPS1		1
#define	_MIPS_ISA_MIPS2		2
#define	_MIPS_ISA_MIPS3		3
#define	_MIPS_ISA_MIPS4		4
#define	_MIPS_ISA_MIPS32	5
#define	_MIPS_ISA_MIPS64	6

#endif /* !_MIPS_CDEFS_H_ */
