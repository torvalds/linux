/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_PSTATE_H_
#define	_MACHINE_PSTATE_H_

#define	PSTATE_AG		(1<<0)
#define	PSTATE_IE		(1<<1)
#define	PSTATE_PRIV		(1<<2)
#define	PSTATE_AM		(1<<3)
#define	PSTATE_PEF		(1<<4)
#define	PSTATE_RED		(1<<5)

#define	PSTATE_MM_SHIFT		(6)
#define	PSTATE_MM_SIZE		(2)
#define	PSTATE_MM_MASK		(((1<<PSTATE_MM_SIZE)-1)<<PSTATE_MM_SHIFT)
#define	PSTATE_MM_TSO		(0<<PSTATE_MM_SHIFT)
#define	PSTATE_MM_PSO		(1<<PSTATE_MM_SHIFT)
#define	PSTATE_MM_RMO		(2<<PSTATE_MM_SHIFT)

#define	PSTATE_TLE		(1<<8)
#define	PSTATE_CLE		(1<<9)
#define	PSTATE_MG		(1<<10)
#define	PSTATE_IG		(1<<11)

#define	PSTATE_MM		PSTATE_MM_TSO

#define	PSTATE_NORMAL		(PSTATE_MM | PSTATE_PEF | PSTATE_PRIV)
#define	PSTATE_ALT		(PSTATE_NORMAL | PSTATE_AG)
#define	PSTATE_INTR		(PSTATE_NORMAL | PSTATE_IG)
#define	PSTATE_MMU		(PSTATE_NORMAL | PSTATE_MG)

#define	PSTATE_KERNEL		(PSTATE_NORMAL | PSTATE_IE)

#define	PSTATE_SECURE(pstate) \
	(((pstate) & ~(PSTATE_AM|PSTATE_MM_MASK)) == (PSTATE_IE|PSTATE_PEF))

#endif /* !_MACHINE_PSTATE_H_ */
