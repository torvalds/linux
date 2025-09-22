/*	$OpenBSD: locore0.S,v 1.5 2024/10/24 17:37:06 gkoehler Exp $	*/
/*	$NetBSD: locore.S,v 1.2 1996/10/16 19:33:09 ws Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "assym.h"

#include <sys/syscall.h>

#include <machine/asm.h>
#include <machine/param.h>
#include <machine/pmap.h>
#include <machine/psl.h>
#include <machine/trap.h>

#define GET_CPUINFO(r)	mfsprg r,0

#define	INTSTK		(8*1024)	/* 8K interrupt stack */
#define	SPILLSTK	(1*1024)
#define	DDBSTK		(7*1024)

/*
 * Globals
 */
	.globl	esym,proc0paddr
	.type	esym,@object
	.type	proc0paddr,@object
	.data
esym:			.long	0	/* end of symbol table */
proc0paddr:		.long	0	/* proc0 p_addr */

	.globl	fwargsave
fwargsave:
			.long 0
			.long 0

	.text
/*
 * Startup entry
 */
_ENTRY(kernel_text)
/* arguments to start
 * r1 - stack provided by firmware/bootloader
 * r3 - unused
 * r4 - unused
 * r5 - firmware pointer (NULL for PPC1bug)
 * r6 - arg list
 * r7 - length
 */
	.globl	start
	.type start,@function
start:
	lis	%r3, fwargsave@ha
	stw	%r6, fwargsave@l(%r3)
	stw	%r7, fwargsave@l+4(%r3)
	bl	ofw_init
	li	%r0,0
	mtmsr	%r0			/* Disable FPU/MMU/exceptions */
	isync

/* compute end of kernel memory */
	lis	%r8,_end@ha
	addi	%r8,%r8,_end@l
	lis	%r3, fwargsave@ha
	lwz	%r6, fwargsave@l(%r3)
	lwz	%r7, fwargsave@l+4(%r3)
#if defined(DDB)
	cmpwi	%r6, 0
	beq	1f
	add	%r9,%r6,%r7
	lwz	%r9, -4(%r9)
	cmpwi   %r9,0
	beq	1f
	lis	%r8,esym@ha
	stw	%r9,esym@l(%r8)
	mr	%r8, %r9
1:
#endif
	li	%r9,PGOFSET
	add	%r8,%r8,%r9
	andc	%r8,%r8,%r9
	lis	%r9,cpu_info@ha
	addi	%r9,%r9,cpu_info@l
	mtsprg	0,%r9
	addi	%r8,%r8,INTSTK
	stw	%r8,CI_INTSTK(%r9)
	addi	%r8,%r8,SPILLSTK+DDBSTK	/* leave room for spillstk and ddbstk */
	lis	%r9,proc0paddr@ha
	stw	%r8,proc0paddr@l(%r9)
	addi	%r1,%r8,USPACE-FRAMELEN	/* stackpointer for proc0 */
	mr	%r4,%r1			/* end of mem reserved for kernel */
	li	%r0,0
	stwu	%r0,-16(%r1)		/* end of stack chain */

	lis	%r3,start@ha
	addi	%r3,%r3,start@l
	mr	%r5,%r6			/* args string */
	bl	initppc
	bl	main
	b	OF_exit
