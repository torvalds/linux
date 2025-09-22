/*	$OpenBSD: vectors.S,v 1.4 2022/12/08 01:25:44 guenther Exp $	*/
/*	$NetBSD: vectors.S,v 1.4 2002/08/17 16:36:32 thorpej Exp $	*/

/*
 * Copyright (C) 1994-1997 Mark Brinicombe
 * Copyright (C) 1994 Brini
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Brini.
 * 4. The name of Brini may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL BRINI BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "assym.h"

#include <machine/asm.h>

/*
 * These are the exception vectors copied down to page 0.
 *
 * Note that FIQs are special; rather than using a level of
 * indirection, we actually copy the FIQ code down into the
 * vector page.
 */

	.text
	.align	2
	.global	page0, page0_data, page0_end
	.global fiqvector

page0:
	ldr	pc, .Lreset_target
	ldr	pc, .Lundefined_target
	ldr	pc, .Lswi_target
	ldr	pc, .Lprefetch_abort_target
	ldr	pc, .Ldata_abort_target
	ldr	pc, .Laddress_exception_target
	ldr	pc, .Lirq_target
#ifdef __ARM_FIQ_INDIRECT
	ldr	pc, .Lfiq_target
#else
.Lfiqvector:
	.set	fiqvector, . - page0
	subs	pc, lr, #4
	.org	.Lfiqvector + 0x100
#endif

page0_data:
.Lreset_target:
	.word	reset_entry

.Lundefined_target:
	.word	undefined_entry

.Lswi_target:
	.word	swi_entry

.Lprefetch_abort_target:
	.word	prefetch_abort_entry

.Ldata_abort_target:
	.word	data_abort_entry

.Laddress_exception_target:
	.word	address_exception_entry

.Lirq_target:
	.word	irq_entry

#ifdef __ARM_FIQ_INDIRECT
.Lfiq_target:
	.word	fiqvector
#else
	.word	0	/* pad it out */
#endif
page0_end:

#ifdef __ARM_FIQ_INDIRECT
	.data
	.align	2
fiqvector:
	subs	pc, lr, #4
	.org	fiqvector + 0x100
#endif
