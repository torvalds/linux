/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	from: @(#)sbusvar.h	8.1 (Berkeley) 6/11/93
 *	from: NetBSD: sbusvar.h,v 1.15 2008/04/28 20:23:36 martin Exp
 *
 * $FreeBSD$
 */

#ifndef _SPARC64_SBUS_SBUSVAR_H_
#define	_SPARC64_SBUS_SBUSVAR_H_

/*
 * Macros for probe order
 */
#define	SBUS_ORDER_FIRST	10
#define	SBUS_ORDER_NORMAL	20

/*
 * PROM-reported DMA burst sizes for the SBus
 */
#define	SBUS_BURST_1		(1 << 0)
#define	SBUS_BURST_2		(1 << 1)
#define	SBUS_BURST_4		(1 << 2)
#define	SBUS_BURST_8		(1 << 3)
#define	SBUS_BURST_16		(1 << 4)
#define	SBUS_BURST_32		(1 << 5)
#define	SBUS_BURST_64		(1 << 6)
#define	SBUS_BURST_MASK		((1 << SBUS_BURST_SIZE) - 1)
#define	SBUS_BURST_SIZE		16
#define	SBUS_BURST64_MASK	(SBUS_BURST_MASK << SBUS_BURST64_SHIFT)
#define	SBUS_BURST64_SHIFT	16

/* Used if no burst sizes are specified for the bus. */
#define	SBUS_BURST_DEF \
	(SBUS_BURST_1 | SBUS_BURST_2 | SBUS_BURST_4 | SBUS_BURST_8 | 	\
	SBUS_BURST_16 | SBUS_BURST_32 | SBUS_BURST_64)
#define	SBUS_BURST64_DEF \
	(SBUS_BURST_8 | SBUS_BURST_16 | SBUS_BURST_32 | SBUS_BURST_64)

enum sbus_device_ivars {
	SBUS_IVAR_BURSTSZ,
	SBUS_IVAR_CLOCKFREQ,
	SBUS_IVAR_IGN,
	SBUS_IVAR_SLOT,
};

/*
 * Simplified accessors for sbus devices
 */
#define	SBUS_ACCESSOR(var, ivar, type) \
	__BUS_ACCESSOR(sbus, var, SBUS, ivar, type)

SBUS_ACCESSOR(burstsz,		BURSTSZ,	int)
SBUS_ACCESSOR(clockfreq,	CLOCKFREQ,	int)
SBUS_ACCESSOR(ign,		IGN,		int)
SBUS_ACCESSOR(slot,		SLOT,		int)

#undef SBUS_ACCESSOR

#endif /* _SPARC64_SBUS_SBUSVAR_H_ */
