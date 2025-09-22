/*	$OpenBSD: exception.S,v 1.11 2022/12/08 01:25:44 guenther Exp $	*/
/*	$NetBSD: exception.S,v 1.13 2003/10/31 16:30:15 scw Exp $	*/

/*
 * Copyright (c) 1994-1997 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
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
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * exception.S
 *
 * Low level handlers for exception vectors
 *
 * Created      : 24/09/94
 *
 * Based on kate/display/abort.s
 */

#include "assym.h"

#include <machine/asm.h>
#include <machine/frame.h>
#include <arm/armreg.h>

	.text	
	.align	2

#define RESTOREVFP							 \
	ldr	r0, [sp]		/* Get the SPSR from stack */	;\
	and	r0, r0, #(PSR_MODE)	/* Returning to USR mode? */	;\
	cmp	r0, #(PSR_USR32_MODE)					;\
	bne	1f							;\
	bl	vfp_enable						;\
1:

AST_LOCALS

/*
 * reset_entry:
 *
 *	Handler for Reset exception.
 */
ASENTRY_NP(reset_entry)
	adr	r0, Lreset_panicmsg
	mov	r1, lr
	bl	panic
	/* NOTREACHED */
Lreset_panicmsg:
	.asciz	"Reset vector called, LR = 0x%08x"
	.balign	4

/*
 * swi_entry
 *
 *	Handler for the Software Interrupt exception.
 */
ASENTRY_NP(swi_entry)
	PUSHFRAME

	mov	r0, sp			/* Pass the frame to any function */
	bl	swi_handler		/* It's a SWI ! */

	DO_AST
	PULLFRAME
	movs	pc, lr			/* Exit */
	dsb	nsh
	isb

/*
 * prefetch_abort_entry:
 *
 *	Handler for the Prefetch Abort exception.
 */
ASENTRY_NP(prefetch_abort_entry)
        sub     lr, lr, #0x00000004     /* Adjust the lr */

	PUSHFRAMEINSVC

	ldr	r1, Lprefetch_abort_handler_address
	adr	lr, exception_exit
 	mov	r0, sp			/* pass the stack pointer as r0 */
	ldr	pc, [r1]

Lprefetch_abort_handler_address:
	.word	prefetch_abort_handler_address

	.data
	.global	prefetch_abort_handler_address

prefetch_abort_handler_address:
	.word	abortprefetch

	.text
abortprefetch:
        adr     r0, abortprefetchmsg
	b	panic

abortprefetchmsg:
        .asciz  "abortprefetch"
        .align  2

/*
 * data_abort_entry:
 *
 *	Handler for the Data Abort exception.
 */
ASENTRY_NP(data_abort_entry)
        sub     lr, lr, #0x00000008     /* Adjust the lr */

	PUSHFRAMEINSVC			/* Push trap frame and switch */
					/* to SVC32 mode */

	ldr	r1, Ldata_abort_handler_address
	adr	lr, exception_exit
	mov	r0, sp			/* pass the stack pointer as r0 */
	ldr	pc, [r1]

Ldata_abort_handler_address:
	.word	data_abort_handler_address

	.data
	.global	data_abort_handler_address
data_abort_handler_address:
	.word	abortdata

	.text
abortdata:
        adr     r0, abortdatamsg
	b	panic

abortdatamsg:
        .asciz  "abortdata"
        .align  2

/*
 * address_exception_entry:
 *
 *	Handler for the Address Exception exception.
 *
 *	NOTE: This exception isn't really used on arm32.  We
 *	print a warning message to the console and then treat
 *	it like a Data Abort.
 */
ASENTRY_NP(address_exception_entry)
	mrs	r1, cpsr
	mrs	r2, spsr
	mov	r3, lr
	adr	r0, Laddress_exception_msg
	bl	printf			/* XXX CLOBBERS LR!! */
	b	data_abort_entry
Laddress_exception_msg:
	.asciz	"Address Exception CPSR=0x%08x SPSR=0x%08x LR=0x%08x\n"
	.balign	4

/*
 * General exception exit handler
 * (Placed here to be within range of all the references to it)
 *
 * It exits straight away if not returning to USR mode.
 * This loops around delivering any pending ASTs.
 * Interrupts are disabled at suitable points to avoid ASTs
 * being posted between testing and exit to user mode.
 *
 * This function uses PULLFRAMEFROMSVCANDEXIT and DO_AST thus should
 * only be called if the exception handler used PUSHFRAMEINSVC.
 */

exception_exit:
	DO_AST
	RESTOREVFP
	PULLFRAMEFROMSVCANDEXIT

/*
 * undefined_entry:
 *
 *	Handler for the Undefined Instruction exception.
 *
 *	We indirect the undefined vector via the handler address
 *	in the data area.  Entry to the undefined handler must
 *	look like direct entry from the vector.
 */
ASENTRY_NP(undefined_entry)
	stmfd	sp!, {r0, r1}
	ldr	r0, Lundefined_handler_indirection
	ldr	r1, [sp], #0x0004
	str	r1, [r0, #0x0000]
	ldr	r1, [sp], #0x0004
	str	r1, [r0, #0x0004]
	ldmia	r0, {r0, r1, pc}

Lundefined_handler_indirection:
	.word	Lundefined_handler_indirection_data

/*
 * assembly bounce code for calling the kernel
 * undefined instruction handler. This uses
 * a standard trap frame and is called in SVC mode.
 */

ENTRY_NP(undefinedinstruction_bounce)
	PUSHFRAMEINSVC

	mov	r0, sp
	adr	lr, exception_exit
	b	undefinedinstruction

	.data
	.align	2

/*
 * Indirection data
 * 2 words use for preserving r0 and r1
 * 3rd word contains the undefined handler address.
 */

Lundefined_handler_indirection_data:
	.word	0
	.word	0

	.global	undefined_handler_address
undefined_handler_address:
	.word	undefinedinstruction_bounce
