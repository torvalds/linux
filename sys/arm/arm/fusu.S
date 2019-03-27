/*	$NetBSD: fusu.S,v 1.10 2003/12/01 13:34:44 rearnsha Exp $	*/

/*-
 * Copyright (c) 1996-1998 Mark Brinicombe.
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
 *	This product includes software developed by Mark Brinicombe
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
 */

#include <machine/asm.h>
#include <machine/armreg.h>
#include "assym.inc"
__FBSDID("$FreeBSD$");

	.syntax	unified

#if __ARM_ARCH >= 6
#define GET_PCB(tmp) \
	mrc p15, 0, tmp, c13, c0, 4; \
	add	tmp, tmp, #(TD_PCB)
#else
.Lcurpcb:
	.word	_C_LABEL(__pcpu) + PC_CURPCB
#define GET_PCB(tmp) \
	ldr	tmp, .Lcurpcb
#endif

/*
 * casueword32(volatile uint32_t *base, uint32_t oldval, uint32_t *oldvalp,
 *    uint32_t newval);
 */

ENTRY(casueword)
EENTRY_NP(casueword32)
	stmfd	sp!, {r4, r5, r6}

	ldr	r4, =(VM_MAXUSER_ADDRESS-3)
	cmp	r0, r4
	mvncs	r0, #0
	bcs	2f

	GET_PCB(r6)
	ldr	r6, [r6]

#ifdef DIAGNOSTIC
	teq	r6, #0x00000000
	ldmfdeq	sp!, {r4, r5, r6}
	beq	.Lfusupcbfault
#endif

	adr	r4, .Lcasuwordfault
	str	r4, [r6, #PCB_ONFAULT]

#if __ARM_ARCH >= 6
1:
	ldrex	r4, [r0]
	cmp	r4, r1
	strexeq	r5, r3, [r0]
	cmpeq	r5, #1
	beq	1b
#else
	ldrt	r4, [r0]
	cmp	r4, r1
	strteq	r3, [r0]
#endif
	str	r4, [r2]
	mov	r0, #0
	str	r0, [r6, #PCB_ONFAULT]
2:
	ldmfd	sp!, {r4, r5, r6}
	RET
EEND(casueword32)
END(casueword)

/*
 * Handle faults from casuword.  Clean up and return -1.
 */

.Lcasuwordfault:
	mov	r0, #0x00000000
	str	r0, [r6, #PCB_ONFAULT]
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6}
	RET

/*
 * fueword(caddr_t uaddr, long *val);
 * Fetch an int from the user's address space.
 */

ENTRY(fueword)
EENTRY_NP(fueword32)
	ldr	r3, =(VM_MAXUSER_ADDRESS-3)
	cmp	r0, r3
	mvncs	r0, #0
	RETc(cs)

	GET_PCB(r2)
	ldr	r2, [r2]

#ifdef DIAGNOSTIC
	teq	r2, #0x00000000
	beq	.Lfusupcbfault
#endif

	adr	r3, .Lfusufault
	str	r3, [r2, #PCB_ONFAULT]

	ldrt	r3, [r0]
	str	r3, [r1]

	mov	r0, #0x00000000
	str	r0, [r2, #PCB_ONFAULT]
	RET
EEND(fueword32)
END(fueword)

/*
 * fusword(caddr_t uaddr);
 * Fetch a short from the user's address space.
 */

ENTRY(fusword)
	ldr	r3, =(VM_MAXUSER_ADDRESS-1)
	cmp	r0, r3
	mvncs	r0, #0
	RETc(cs)

	GET_PCB(r2)
	ldr	r2, [r2]

#ifdef DIAGNOSTIC
	teq	r2, #0x00000000
	beq	.Lfusupcbfault
#endif

	adr	r1, .Lfusufault
	str	r1, [r2, #PCB_ONFAULT]

	ldrbt	r3, [r0], #1
	ldrbt	ip, [r0]
#ifdef __ARMEB__
	orr	r0, ip, r3, asl #8
#else
	orr	r0, r3, ip, asl #8
#endif
	mov	r1, #0x00000000
	str	r1, [r2, #PCB_ONFAULT]
	RET
END(fusword)

/*
 * fubyte(caddr_t uaddr);
 * Fetch a byte from the user's address space.
 */

ENTRY(fubyte)
	ldr	r3, =VM_MAXUSER_ADDRESS
	cmp	r0, r3
	mvncs	r0, #0
	RETc(cs)

	GET_PCB(r2)
	ldr	r2, [r2]

#ifdef DIAGNOSTIC
	teq	r2, #0x00000000
	beq	.Lfusupcbfault
#endif

	adr	r1, .Lfusufault
	str	r1, [r2, #PCB_ONFAULT]

	ldrbt	r3, [r0]

	mov	r1, #0x00000000
	str	r1, [r2, #PCB_ONFAULT]
	mov	r0, r3
	RET
END(fubyte)

/*
 * Handle faults from [fs]u*().  Clean up and return -1.
 */

.Lfusufault:
	mov	r0, #0x00000000
	str	r0, [r2, #PCB_ONFAULT]
	mvn	r0, #0x00000000
	RET

#ifdef DIAGNOSTIC
/*
 * Handle earlier faults from [fs]u*(), due to no pcb
 */

.Lfusupcbfault:
	mov	r1, r0
	adr	r0, fusupcbfaulttext
	b	_C_LABEL(panic)

fusupcbfaulttext:
	.asciz	"Yikes - no valid PCB during fusuxxx() addr=%08x\n"
	.align	2
#endif

/*
 * suword(caddr_t uaddr, int x);
 * Store an int in the user's address space.
 */

ENTRY(suword)
EENTRY_NP(suword32)
	ldr	r3, =(VM_MAXUSER_ADDRESS-3)
	cmp	r0, r3
	mvncs	r0, #0
	RETc(cs)

	GET_PCB(r2)
	ldr	r2, [r2]

#ifdef DIAGNOSTIC
	teq	r2, #0x00000000
	beq	.Lfusupcbfault
#endif

	adr	r3, .Lfusufault
	str	r3, [r2, #PCB_ONFAULT]

	strt	r1, [r0]

	mov	r0, #0x00000000
	str	r0, [r2, #PCB_ONFAULT]
	RET
EEND(suword32)
END(suword)

/*
 * susword(caddr_t uaddr, short x);
 * Store a short in the user's address space.
 */

ENTRY(susword)
	ldr	r3, =(VM_MAXUSER_ADDRESS-1)
	cmp	r0, r3
	mvncs	r0, #0
	RETc(cs)

	GET_PCB(r2)
	ldr	r2, [r2]

#ifdef DIAGNOSTIC
	teq	r2, #0x00000000
	beq	.Lfusupcbfault
#endif

	adr	r3, .Lfusufault
	str	r3, [r2, #PCB_ONFAULT]

#ifdef __ARMEB__
	mov	ip, r1, lsr #8
	strbt	ip, [r0], #1
#else
	strbt	r1, [r0], #1
	mov	r1, r1, lsr #8
#endif
	strbt	r1, [r0]

	mov	r0, #0x00000000
	str	r0, [r2, #PCB_ONFAULT]
	RET
END(susword)

/*
 * subyte(caddr_t uaddr, char x);
 * Store a byte in the user's address space.
 */

ENTRY(subyte)
	ldr	r3, =VM_MAXUSER_ADDRESS
	cmp	r0, r3
	mvncs	r0, #0
	RETc(cs)

	GET_PCB(r2)
	ldr	r2, [r2]


#ifdef DIAGNOSTIC
	teq	r2, #0x00000000
	beq	.Lfusupcbfault
#endif

	adr	r3, .Lfusufault
	str	r3, [r2, #PCB_ONFAULT]

	strbt	r1, [r0]
	mov	r0, #0x00000000
	str	r0, [r2, #PCB_ONFAULT]
	RET
END(subyte)
