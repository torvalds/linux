/* $FreeBSD$ */

/*-
 * Copyright (C) 2010-2016 Nathan Whitehorn
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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

#include "assym.inc"

#include <sys/syscall.h>

#include <machine/trap.h>
#include <machine/param.h>
#include <machine/spr.h>
#include <machine/asm.h>
#include <machine/vmparam.h>
#include "opt_platform.h"

/* Locate the per-CPU data structure */
#define GET_CPUINFO(r)  \
        mfsprg0  r

/*
 * Compiled KERNBASE location and the kernel load address
 */
        .globl  kernbase
        .set    kernbase, KERNBASE

/*
 * Globals
 */
	.data
	.align 3
GLOBAL(__startkernel)
	.long	begin
GLOBAL(__endkernel)
	.long	end
	.align	4
#define	TMPSTKSZ	8192		/* 8K temporary stack */
GLOBAL(tmpstk)
	.space	TMPSTKSZ

#ifdef KDB
#define TRAPSTKSZ       4096            /* 4k trap stack */
GLOBAL(trapstk)
        .space        TRAPSTKSZ
#endif

	.text
	.globl	btext
btext:

/*
 * Main kernel entry point.
 */
	.text
	.globl	__start
__start:
	/* Figure out where we are */
	bl	1f
	.long	_DYNAMIC-.
	.long	_GLOBAL_OFFSET_TABLE_-.
	.long	tmpstk-.
1:	mflr	%r30

	/* Set up temporary stack pointer */
	lwz	%r1,8(%r30)
	add	%r1,%r1,%r30
	addi	%r1,%r1,(8+TMPSTKSZ-40)

	/* Relocate self */
	stw	%r3,16(%r1)
	stw	%r4,20(%r1)
	stw	%r5,24(%r1)
	stw	%r6,28(%r1)
	stw	%r7,32(%r1)

	lwz	%r3,0(%r30) /* _DYNAMIC in %r3 */
	add	%r3,%r3,%r30
	lwz	%r4,4(%r30) /* GOT pointer */
	add	%r4,%r4,%r30
	lwz	%r4,4(%r4)  /* got[0] is _DYNAMIC link addr */
	subf	%r4,%r4,%r3 /* subtract to calculate relocbase */
	bl	elf_reloc_self
	
	lwz	%r3,16(%r1)
	lwz	%r4,20(%r1)
	lwz	%r5,24(%r1)
	lwz	%r6,28(%r1)
	lwz	%r7,32(%r1)

	/* MD setup */
	bl	powerpc_init

	/* Set stack pointer to new value and branch to mi_startup */
	mr	%r1, %r3
	li	%r3, 0
	stw	%r3, 0(%r1)
	bl	mi_startup

	/* mi_startup() does not return */
	b	.

#include <powerpc/aim/trap_subr32.S>
