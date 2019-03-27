/*	$NetBSD: cpuregs.h,v 1.70 2006/05/15 02:26:54 simonb Exp $	*/

/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)machConst.h 8.1 (Berkeley) 6/10/93
 *
 * machConst.h --
 *
 *	Machine dependent constants.
 *
 *	Copyright (C) 1989 Digital Equipment Corporation.
 *	Permission to use, copy, modify, and distribute this software and
 *	its documentation for any purpose and without fee is hereby granted,
 *	provided that the above copyright notice appears in all copies.
 *	Digital Equipment Corporation makes no representations about the
 *	suitability of this software for any purpose.  It is provided "as is"
 *	without express or implied warranty.
 *
 * from: Header: /sprite/src/kernel/mach/ds3100.md/RCS/machConst.h,
 *	v 9.2 89/10/21 15:55:22 jhh Exp	 SPRITE (DECWRL)
 * from: Header: /sprite/src/kernel/mach/ds3100.md/RCS/machAddrs.h,
 *	v 1.2 89/08/15 18:28:21 rab Exp	 SPRITE (DECWRL)
 * from: Header: /sprite/src/kernel/vm/ds3100.md/RCS/vmPmaxConst.h,
 *	v 9.1 89/09/18 17:33:00 shirriff Exp  SPRITE (DECWRL)
 *
 * $FreeBSD$
 */

#ifndef _MIPS_CCA_H_
#define	_MIPS_CCA_H_

/*
 * Cache Coherency Attributes:
 *	UC:	Uncached.
 *	UA:	Uncached accelerated.
 *	C:	Cacheable, coherency unspecified.
 *	CNC:	Cacheable non-coherent.
 *	CC:	Cacheable coherent.
 *	CCS:	Cacheable coherent, shared read.
 *	CCE:	Cacheable coherent, exclusive read.
 *	CCEW:	Cacheable coherent, exclusive write.
 *	CCUOW:	Cacheable coherent, update on write.
 *
 * Note that some bits vary in meaning across implementations (and that the
 * listing here is no doubt incomplete) and that the optimal cached mode varies
 * between implementations.  0x02 is required to be UC and 0x03 is required to
 * be a least C.
 *
 * We define the following logical bits:
 * 	UNCACHED:
 * 		The optimal uncached mode for the target CPU type.  This must
 * 		be suitable for use in accessing memory-mapped devices.
 * 	CACHED:	The optional cached mode for the target CPU type.
 */

#define	MIPS_CCA_UC		0x02	/* Uncached. */
#define	MIPS_CCA_C		0x03	/* Cacheable, coherency unspecified. */

#if defined(CPU_R4000) || defined(CPU_R10000)
#define	MIPS_CCA_CNC	0x03
#define	MIPS_CCA_CCE	0x04
#define	MIPS_CCA_CCEW	0x05

#ifdef CPU_R4000
#define	MIPS_CCA_CCUOW	0x06
#endif

#ifdef CPU_R10000
#define	MIPS_CCA_UA	0x07
#endif

#define	MIPS_CCA_CACHED	MIPS_CCA_CCEW
#endif /* defined(CPU_R4000) || defined(CPU_R10000) */

#if defined(CPU_SB1)
#define	MIPS_CCA_CC	0x05	/* Cacheable Coherent. */
#endif

#if defined(CPU_MIPS74K)
#define	MIPS_CCA_UNCACHED	0x02
#define	MIPS_CCA_CACHED		0x03
#endif

/*
 * 1004K and 1074K cores, as well as interAptiv and proAptiv cores, support
 * Cacheable Coherent CCAs 0x04 and 0x05, as well as Cacheable non-Coherent
 * CCA 0x03 and Uncached Accelerated CCA 0x07
 */
#if defined(CPU_MIPS1004K) || defined(CPU_MIPS1074K) ||	\
    defined(CPU_INTERAPTIV) || defined(CPU_PROAPTIV)
#define	MIPS_CCA_CNC		0x03
#define	MIPS_CCA_CCE		0x04
#define	MIPS_CCA_CCS		0x05
#define	MIPS_CCA_UA		0x07

/* We use shared read CCA for CACHED CCA */
#define	MIPS_CCA_CACHED		MIPS_CCA_CCS
#endif

#if defined(CPU_XBURST)
#define	MIPS_CCA_UA		0x01
#define	MIPS_CCA_WC		MIPS_CCA_UA
#endif

#ifndef	MIPS_CCA_UNCACHED
#define	MIPS_CCA_UNCACHED	MIPS_CCA_UC
#endif

/*
 * If we don't know which cached mode to use and there is a cache coherent
 * mode, use it.  If there is not a cache coherent mode, use the required
 * cacheable mode.
 */
#ifndef MIPS_CCA_CACHED
#ifdef MIPS_CCA_CC
#define	MIPS_CCA_CACHED	MIPS_CCA_CC
#else
#define	MIPS_CCA_CACHED	MIPS_CCA_C
#endif
#endif

#endif
