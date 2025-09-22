/*	$OpenBSD: copystr.S,v 1.12 2023/01/31 15:18:54 deraadt Exp $	*/
/*	$NetBSD: copystr.S,v 1.8 2002/10/13 14:54:48 bjh21 Exp $	*/

/*
 * Copyright (c) 1995 Mark Brinicombe.
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
 *	This product includes software developed by Mark Brinicombe.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * copystr.S
 *
 * optimised and fault protected copystr functions
 *
 * Created      : 16/05/95
 */

#include "assym.h"

#include <sys/errno.h>
	
#include <machine/asm.h>
#include <arm/sysreg.h>

	.text
	.align	2
#ifdef MULTIPROCESSOR
.Lcpu_info:
	.word	cpu_info
#else
.Lcpu_info_primary:
	.word	cpu_info_primary
#endif

#define SAVE_REGS	stmfd	sp!, {r4-r6}
#define RESTORE_REGS	ldmfd	sp!, {r4-r6}

/*
 * r0 - user space address
 * r1 - kernel space address
 * r2 - maxlens
 * r3 - lencopied
 *
 * Copy string from user space to kernel space
 */
ENTRY(_copyinstr)
	SAVE_REGS

	teq	r2, #0x00000000
	mov	r6, #0x00000000
	moveq	r0, #ENAMETOOLONG
	beq	2f

	/* Get curcpu from TPIDRPRW. */
	mrc	CP15_TPIDRPRW(r4)
	ldr	r4, [r4, #CI_CURPCB]

#ifdef DEBUG
	teq	r4, #0x00000000
	beq	.Lcopystrpcbfault
#endif

	adr	r5, .Lcopystrfault
	str	r5, [r4, #PCB_ONFAULT]

1:	ldrbt	r5, [r0], #0x0001
	add	r6, r6, #0x00000001
	teq	r5, #0x00000000
	strb	r5, [r1], #0x0001
	teqne	r6, r2
	bne	1b

	mov	r0, #0x00000000
	str	r0, [r4, #PCB_ONFAULT]

	teq	r5, #0x00000000
	moveq	r0, #0x00000000
	movne	r0, #ENAMETOOLONG

2:	teq	r3, #0x00000000
	strne	r6, [r3]

	RESTORE_REGS
	mov	pc, lr

/*
 * r0 - kernel space address
 * r1 - user space address
 * r2 - maxlens
 * r3 - lencopied
 *
 * Copy string from kernel space to user space
 */
ENTRY(copyoutstr)
	SAVE_REGS

	teq	r2, #0x00000000
	mov	r6, #0x00000000
	moveq	r0, #ENAMETOOLONG
	beq	2f

	/* Get curcpu from TPIDRPRW. */
	mrc	CP15_TPIDRPRW(r4)
	ldr	r4, [r4, #CI_CURPCB]

#ifdef DEBUG
	teq	r4, #0x00000000
	beq	.Lcopystrpcbfault
#endif

	adr	r5, .Lcopystrfault
	str	r5, [r4, #PCB_ONFAULT]

1:	ldrb	r5, [r0], #0x0001
	add	r6, r6, #0x00000001
	teq	r5, #0x00000000
	strbt	r5, [r1], #0x0001
	teqne	r6, r2
	bne	1b

	mov	r0, #0x00000000
	str	r0, [r4, #PCB_ONFAULT]

	teq	r5, #0x00000000
	moveq	r0, #0x00000000
	movne	r0, #ENAMETOOLONG

2:	teq	r3, #0x00000000
	strne	r6, [r3]

	RESTORE_REGS
	mov	pc, lr

/* A fault occurred during the copy */
.Lcopystrfault:
	mov	r1, #0x00000000
	str	r1, [r4, #PCB_ONFAULT]
	RESTORE_REGS
	mov	pc, lr

#ifdef DEBUG
.Lcopystrpcbfault:
	mov	r2, r1
	mov	r1, r0
	adr	r0, Lcopystrpcbfaulttext
	bic	sp, sp, #7			/* align stack to 8 bytes */
	b	panic

Lcopystrpcbfaulttext:
	.asciz	"No valid PCB during copyinoutstr() addr1=%08x addr2=%08x\n"
	.align	2
#endif
