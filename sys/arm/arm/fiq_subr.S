/*	$NetBSD: fiq_subr.S,v 1.3 2002/04/12 18:50:31 thorpej Exp $	*/

/*-
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include <machine/armreg.h>

/*
 * MODE_CHANGE_NOP should be inserted between a mode change and a
 * banked register (R8--R15) access.
 */
#if defined(CPU_ARM2) || defined(CPU_ARM250)
#define	MODE_CHANGE_NOP	mov	r0, r0
#else
#define	MODE_CHANGE_NOP	/* Data sheet says ARM3 doesn't need it */
#endif

#define	SWITCH_TO_FIQ_MODE						\
	mrs	r2, cpsr					;	\
	mov	r3, r2						;	\
	bic	r2, r2, #(PSR_MODE)				;	\
	orr	r2, r2, #(PSR_FIQ32_MODE)			;	\
	msr	cpsr_fsxc, r2

#define	BACK_TO_SVC_MODE						\
	msr	cpsr_fsxc, r3

/*
 * fiq_getregs:
 *
 *	Fetch the FIQ mode banked registers into the fiqhandler
 *	structure.
 */
ENTRY(fiq_getregs)
	SWITCH_TO_FIQ_MODE

	stmia	r0, {r8-r13}

	BACK_TO_SVC_MODE
	RET
END(fiq_getregs)

/*
 * fiq_setregs:
 *
 *	Load the FIQ mode banked registers from the fiqhandler
 *	structure.
 */
ENTRY(fiq_setregs)
	SWITCH_TO_FIQ_MODE

	ldmia	r0, {r8-r13}

	BACK_TO_SVC_MODE
	RET
END(fiq_setregs)

