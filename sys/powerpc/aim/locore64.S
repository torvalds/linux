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
 *
 * $FreeBSD$
 */

#include "assym.inc"

#include <sys/syscall.h>

#include <machine/trap.h>
#include <machine/param.h>
#include <machine/spr.h>
#include <machine/asm.h>
#include <machine/vmparam.h>

#ifdef _CALL_ELF
.abiversion _CALL_ELF
#endif

/* Glue for linker script */
.globl  kernbase
.set    kernbase, KERNBASE

/*
 * Globals
 */
	.data
	.align 3
GLOBAL(__startkernel)
	.llong	begin
GLOBAL(__endkernel)
	.llong	end
GLOBAL(can_wakeup)
	.llong	0x0

	.align	4
#define	TMPSTKSZ	16384		/* 16K temporary stack */
GLOBAL(tmpstk)
	.space	TMPSTKSZ

TOC_ENTRY(tmpstk)
TOC_ENTRY(can_wakeup)

#ifdef KDB
#define TRAPSTKSZ       4096            /* 4k trap stack */
GLOBAL(trapstk)
        .space        TRAPSTKSZ
TOC_ENTRY(trapstk)
#endif


/*
 * Entry point for bootloaders that do not fully implement ELF and start
 * at the beginning of the image (kexec, notably). In its own section so
 * that it ends up before any linker-generated call stubs and actually at
 * the beginning of the image. kexec on some systems also enters at
 * (start of image) + 0x60, so put a spin loop there.
 */
	.section ".text.kboot", "x", @progbits
kbootentry:
	b __start
. = kbootentry + 0x40	/* Magic address used in platform layer */
	.global smp_spin_sem
ap_kexec_spin_sem:
	.long   -1
. = kbootentry + 0x60	/* Entry point for kexec APs */
ap_kexec_start:		/* At 0x60 past start, copied to 0x60 by kexec */
	/* r3 set to CPU ID by kexec */

	/* Invalidate icache for low-memory copy and jump there */
	li	%r0,0x80
	dcbst	0,%r0
	sync
	icbi	0,%r0
	isync
	ba	0x80			/* Absolute branch to next inst */

. = kbootentry + 0x80			/* Aligned to cache line */
1:	or	31,31,31		/* yield */
	sync
	lwz	%r1,0x40(0)		/* Spin on ap_kexec_spin_sem */
	cmpw	%r1,%r3			/* Until it equals our CPU ID */
	bne	1b
	
	/* Released */
	or	2,2,2			/* unyield */

	/* Make sure that it will be software reset. Clear SRR1 */
	li	%r1,0
	mtsrr1	%r1
	ba	EXC_RST

/*
 * Now start the real text section
 */

	.text
	.globl	btext
btext:

/*
 * Main kernel entry point.
 *
 * Calling convention:
 * r3: Flattened Device Tree pointer (or zero)
 * r4: ignored
 * r5: OF client interface pointer (or zero)
 * r6: Loader metadata pointer (or zero)
 * r7: Magic cookie (0xfb5d104d) to indicate that r6 has loader metadata
 */
	.text
ASENTRY_NOPROF(__start)
	/* Set 64-bit mode if not yet set before branching to C */
	mfmsr	%r20
	li	%r21,1
	insrdi	%r20,%r21,1,0
	mtmsrd	%r20
	isync
	nop	/* Make this block a multiple of 8 bytes */

	/* Set up the TOC pointer */
	b	0f
	.align 3
0:	nop
	bl	1f
	.llong	__tocbase + 0x8000 - .
1:	mflr	%r2
	ld	%r1,0(%r2)
	add	%r2,%r1,%r2

	/* Get load offset */
	ld	%r31,-0x8000(%r2) /* First TOC entry is TOC base */
	subf    %r31,%r31,%r2	/* Subtract from real TOC base to get base */

	/* Set up the stack pointer */
	bl	1f
	.llong	tmpstk + TMPSTKSZ - 96 - .
1:	mflr	%r30
	ld	%r1,0(%r30)
	add	%r1,%r1,%r30
	nop

	/* Relocate kernel */
	std	%r3,48(%r1)
	std	%r4,56(%r1)
	std	%r5,64(%r1)
	std	%r6,72(%r1)
	std	%r7,80(%r1)

	bl	1f
	.llong _DYNAMIC-.
1:	mflr	%r3
	ld	%r4,0(%r3)
	add	%r3,%r4,%r3
	mr	%r4,%r31
	bl	elf_reloc_self
	nop
	ld	%r3,48(%r1)
	ld	%r4,56(%r1)
	ld	%r5,64(%r1)
	ld	%r6,72(%r1)
	ld	%r7,80(%r1)

	/* Begin CPU init */
	mr	%r4,%r2 /* Replace ignored r4 with tocbase for trap handlers */
	bl	powerpc_init
	nop

	/* Set stack pointer to new value and branch to mi_startup */
	mr	%r1, %r3
	li	%r3, 0
	std	%r3, 0(%r1)
	bl	mi_startup
	nop

	/* Unreachable */
	b	.

ASENTRY_NOPROF(__restartkernel)
	/*
	 * r3-r7: arguments to go to __start
	 * r8: offset from current kernel address to apply
	 * r9: MSR to set when (atomically) jumping to __start + r8
	 */
	mtsrr1	%r9
	bl	1f
1:	mflr	%r25
	add	%r25,%r8,%r25
	addi	%r25,%r25,2f-1b
	mtsrr0	%r25
	rfid
2:	bl	__start
	nop

#include <powerpc/aim/trap_subr64.S>
