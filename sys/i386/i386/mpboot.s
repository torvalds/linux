/*-
 * Copyright (c) 1995 Jack F. Vogel
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * mpboot.s:	FreeBSD machine support for the Intel MP Spec
 *		multiprocessor systems.
 *
 * $FreeBSD$
 */

#include "opt_pmap.h"

#include <machine/asmacros.h>		/* miscellaneous asm macros */
#include <x86/apicreg.h>
#include <machine/specialreg.h>

#include "assym.inc"

/*
 * this code MUST be enabled here and in mp_machdep.c
 * it follows the very early stages of AP boot by placing values in CMOS ram.
 * it NORMALLY will never be needed and thus the primitive method for enabling.
 *
#define CHECK_POINTS
 */

#if defined(CHECK_POINTS)

#define CMOS_REG	(0x70)
#define CMOS_DATA	(0x71)

#define CHECKPOINT(A,D)		\
	movb	$(A),%al ;	\
	outb	%al,$CMOS_REG ;	\
	movb	$(D),%al ;	\
	outb	%al,$CMOS_DATA

#else

#define CHECKPOINT(A,D)

#endif /* CHECK_POINTS */


/*
 * the APs enter here from their trampoline code (bootMP, below)
 */
	.p2align 4

NON_GPROF_ENTRY(MPentry)
	CHECKPOINT(0x36, 3)
	/*
	 * Enable features on this processor.  We don't support SMP on
	 * CPUs older than a Pentium, so we know that we can use the cpuid
	 * instruction.
	 */
	movl	$1,%eax
	cpuid					/* Retrieve features */
	movl	%cr4,%eax
	testl	$CPUID_PSE,%edx
	jz 1f
	orl	$CR4_PSE,%eax			/* Enable PSE  */
1:	testl	$CPUID_PGE,%edx
	jz 2f
	orl	$CR4_PGE,%eax			/* Enable PGE  */
2:	testl	$CPUID_VME,%edx
	jz 3f
	orl	$CR4_VME,%eax			/* Enable VME  */
3:	movl	%eax,%cr4

	/* Now enable paging mode */
	cmpl	$0, pae_mode
	je	4f
	movl	IdlePDPT, %eax
	movl	%eax, %cr3
	movl	%cr4, %eax
	orl	$CR4_PAE, %eax
	movl	%eax, %cr4
	movl	$0x80000000, %eax
	cpuid
	movl	$0x80000001, %ebx
	cmpl	%ebx, %eax
	jb	5f
	movl	%ebx, %eax
	cpuid
	testl	$AMDID_NX, %edx
	je	5f
	movl	$MSR_EFER, %ecx
	rdmsr
	orl	$EFER_NXE,%eax
	wrmsr
	jmp	5f
4:	movl	IdlePTD_nopae, %eax
	movl	%eax,%cr3	
5:	movl	%cr0,%eax
	orl	$CR0_PE|CR0_PG,%eax		/* enable paging */
	movl	%eax,%cr0			/* let the games begin! */
	movl	bootSTK,%esp			/* boot stack end loc. */

	pushl	$mp_begin			/* jump to high mem */
	ret

	/*
	 * Wait for the booting CPU to signal startup
	 */
mp_begin:	/* now running relocated at KERNBASE */
	CHECKPOINT(0x37, 4)
	call	init_secondary			/* load i386 tables */

/*
 * This is the embedded trampoline or bootstrap that is
 * copied into 'real-mode' low memory, it is where the
 * secondary processor "wakes up". When it is executed
 * the processor will eventually jump into the routine
 * MPentry, which resides in normal kernel text above
 * 1Meg.		-jackv
 */

	.data
	ALIGN_DATA				/* just to be sure */

BOOTMP1:

NON_GPROF_ENTRY(bootMP)
	.code16		
	cli
	CHECKPOINT(0x34, 1)
	/* First guarantee a 'clean slate' */
	xorl	%eax, %eax
	movl	%eax, %ebx
	movl	%eax, %ecx
 	movl	%eax, %edx
	movl	%eax, %esi
	movl	%eax, %edi

	/* set up data segments */
	mov	%cs, %ax
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %fs
	mov	%ax, %gs
	mov	%ax, %ss
	mov	$(boot_stk-bootMP), %esp

	/* Now load the global descriptor table */
	lgdt	MP_GDTptr-bootMP

	/* Enable protected mode */
	movl	%cr0, %eax
	orl	$CR0_PE, %eax
	movl	%eax, %cr0 

	/*
	 * make intrasegment jump to flush the processor pipeline and
	 * reload CS register
	 */
	pushl	$0x18
	pushl	$(protmode-bootMP)
	lretl

       .code32		
protmode:
	CHECKPOINT(0x35, 2)

	/*
	 * we are NOW running for the first time with %eip
	 * having the full physical address, BUT we still
	 * are using a segment descriptor with the origin
	 * not matching the booting kernel.
	 *
 	 * SO NOW... for the BIG Jump into kernel's segment
	 * and physical text above 1 Meg.
	 */
	mov	$0x10, %ebx
	movw	%bx, %ds
	movw	%bx, %es
	movw	%bx, %fs
	movw	%bx, %gs
	movw	%bx, %ss

	.globl	bigJump
bigJump:
	/* this will be modified by mpInstallTramp() */
	ljmp	$0x08, $0			/* far jmp to MPentry() */
	
dead:	hlt /* We should never get here */
	jmp	dead

/*
 * MP boot strap Global Descriptor Table
 */
	.p2align 4
	.globl	MP_GDT
	.globl	bootCodeSeg
	.globl	bootDataSeg
MP_GDT:

nulldesc:		/* offset = 0x0 */

	.word	0x0	
	.word	0x0	
	.byte	0x0	
	.byte	0x0	
	.byte	0x0	
	.byte	0x0	

kernelcode:		/* offset = 0x08 */

	.word	0xffff	/* segment limit 0..15 */
	.word	0x0000	/* segment base 0..15 */
	.byte	0x0	/* segment base 16..23; set for 0K */
	.byte	0x9f	/* flags; Type	*/
	.byte	0xcf	/* flags; Limit	*/
	.byte	0x0	/* segment base 24..32 */

kerneldata:		/* offset = 0x10 */

	.word	0xffff	/* segment limit 0..15 */
	.word	0x0000	/* segment base 0..15 */
	.byte	0x0	/* segment base 16..23; set for 0k */
	.byte	0x93	/* flags; Type  */
	.byte	0xcf	/* flags; Limit */
	.byte	0x0	/* segment base 24..32 */

bootcode:		/* offset = 0x18 */

	.word	0xffff	/* segment limit 0..15 */
bootCodeSeg:		/* this will be modified by mpInstallTramp() */
	.word	0x0000	/* segment base 0..15 */
	.byte	0x00	/* segment base 16...23; set for 0x000xx000 */
	.byte	0x9e	/* flags; Type  */
	.byte	0xcf	/* flags; Limit */
	.byte	0x0	/*segment base 24..32 */

bootdata:		/* offset = 0x20 */

	.word	0xffff	
bootDataSeg:		/* this will be modified by mpInstallTramp() */
	.word	0x0000	/* segment base 0..15 */
	.byte	0x00	/* segment base 16...23; set for 0x000xx000 */
	.byte	0x92	
	.byte	0xcf	
	.byte	0x0		

/*
 * GDT pointer for the lgdt call
 */
	.globl	mp_gdtbase

MP_GDTptr:	
mp_gdtlimit:
	.word	0x0028		
mp_gdtbase:		/* this will be modified by mpInstallTramp() */
	.long	0

	.space	0x100	/* space for boot_stk - 1st temporary stack */
boot_stk:

BOOTMP2:
	.globl	bootMP_size
bootMP_size:
	.long	BOOTMP2 - BOOTMP1
