/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1996 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from BSDI $Id: ktr.h,v 1.10.2.7 2000/03/16 21:44:42 cp Exp $
 * $FreeBSD$
 */

#ifndef _MACHINE_KTR_H_
#define _MACHINE_KTR_H_

#include <sys/ktr.h>

#ifdef LOCORE

/*
 * XXX could really use another register ...
 */
#define	ATR(desc, r1, r2, r3, l1, l2) \
	.sect	.rodata ; \
l1:	.asciz	desc ; \
	.previous ; \
	SET(ktr_idx, r2, r1) ; \
	lduw	[r1], r2 ; \
l2:	add	r2, 1, r3 ; \
	set	KTR_ENTRIES - 1, r1 ; \
	and	r3, r1, r3 ; \
	set	ktr_idx, r1 ; \
	casa	[r1] ASI_N, r2, r3 ; \
	cmp	r2, r3 ; \
	bne	%icc, l2 ## b ; \
	 mov	r3, r2 ; \
	SET(ktr_buf, r3, r1) ; \
	ldx	[r1], r1 ; \
	mulx	r2, KTR_SIZEOF, r2 ; \
	add	r1, r2, r1 ; \
	rd	%tick, r2 ; \
	stx	r2, [r1 + KTR_TIMESTAMP] ; \
	lduw	[PCPU(CPUID)], r2 ; \
	stw	r2, [r1 + KTR_CPU] ; \
	stw	%g0, [r1 + KTR_LINE] ; \
	stx	%g0, [r1 + KTR_FILE] ; \
	SET(l1 ## b, r3, r2) ; \
	stx	r2, [r1 + KTR_DESC]

#define CATR(mask, desc, r1, r2, r3, l1, l2, l3) \
	setx	mask, r3, r1 ; \
	setx	ktr_mask, r3, r2 ; \
	ldx	[r2], r2 ; \
	and	r2, r1, r1 ; \
	brz	r1, l3 ## f ; \
	 nop ; \
	lduw	[PCPU(CPUID)], r2 ; \
	mov	_NCPUBITS, r3 ; \
	udivx	r2, r3, r2 ; \
	srl	r2, 0, r2 ; \
	sllx	r2, PTR_SHIFT, r2 ; \
	SET(ktr_cpumask, r3, r1) ; \
	ldx	[r1 + r2], r1 ; \
	lduw	[PCPU(CPUID)], r2 ; \
	mov	_NCPUBITS, r3 ; \
	udivx	r2, r3, r2 ; \
	srl	r2, 0, r2 ; \
	smul	r2, r3, r3 ; \
	lduw	[PCPU(CPUID)], r2 ; \
	sub	r2, r3, r3 ; \
	mov	1, r2 ; \
	sllx	r2, r3, r2 ; \
	andn	r1, r2, r1 ; \
	brz	r1, l3 ## f ; \
	 nop ; \
	ATR(desc, r1, r2, r3, l1, l2)

#endif /* LOCORE */

#endif /* !_MACHINE_KTR_H_ */
