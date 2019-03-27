/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
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
 *	from: @(#)locore.s	7.3 (Berkeley) 5/13/91
 * $FreeBSD$
 *
 *		originally from: locore.s, by William F. Jolitz
 *
 *		Substantially rewritten by David Greenman, Rod Grimes,
 *			Bruce Evans, Wolfgang Solfrank, Poul-Henning Kamp
 *			and many others.
 */

#include "opt_bootp.h"
#include "opt_nfsroot.h"
#include "opt_pmap.h"

#include <sys/reboot.h>

#include <machine/asmacros.h>
#include <machine/cputypes.h>
#include <machine/psl.h>
#include <machine/pmap.h>
#include <machine/specialreg.h>

#include "assym.inc"

/*
 * Compiled KERNBASE location and the kernel load address, now identical.
 */
	.globl	kernbase
	.set	kernbase,KERNBASE
	.globl	kernload
	.set	kernload,KERNLOAD

/*
 * Globals
 */
	.data
	ALIGN_DATA			/* just to be sure */

	.space	0x2000			/* space for tmpstk - temporary stack */
tmpstk:

	.globl	bootinfo
bootinfo:	.space	BOOTINFO_SIZE	/* bootinfo that we can handle */

	.text
/**********************************************************************
 *
 * This is where the bootblocks start us, set the ball rolling...
 *
 */
NON_GPROF_ENTRY(btext)

/* Tell the bios to warmboot next time */
	movw	$0x1234,0x472

/* Set up a real frame in case the double return in newboot is executed. */
	xorl	%ebp,%ebp
	pushl	%ebp
	movl	%esp, %ebp

/* Don't trust what the BIOS gives for eflags. */
	pushl	$PSL_KERNEL
	popfl

/*
 * Don't trust what the BIOS gives for %fs and %gs.  Trust the bootstrap
 * to set %cs, %ds, %es and %ss.
 */
	mov	%ds, %ax
	mov	%ax, %fs
	mov	%ax, %gs

/*
 * Clear the bss.  Not all boot programs do it, and it is our job anyway.
 *
 * XXX we don't check that there is memory for our bss and page tables
 * before using it.
 *
 * Note: we must be careful to not overwrite an active gdt or idt.  They
 * inactive from now until we switch to new ones, since we don't load any
 * more segment registers or permit interrupts until after the switch.
 */
	movl	$__bss_end,%ecx
	movl	$__bss_start,%edi
	subl	%edi,%ecx
	xorl	%eax,%eax
	cld
	rep
	stosb

	call	recover_bootinfo

/* Get onto a stack that we can trust. */
/*
 * XXX this step is delayed in case recover_bootinfo needs to return via
 * the old stack, but it need not be, since recover_bootinfo actually
 * returns via the old frame.
 */
	movl	$tmpstk,%esp

	call	identify_cpu
	call	pmap_cold

	/* set up bootstrap stack */
	movl	proc0kstack,%eax	/* location of in-kernel stack */

	/*
	 * Only use bottom page for init386().  init386() calculates the
	 * PCB + FPU save area size and returns the true top of stack.
	 */
	leal	PAGE_SIZE(%eax),%esp

	xorl	%ebp,%ebp		/* mark end of frames */

	pushl	physfree		/* value of first for init386(first) */
	call	init386			/* wire 386 chip for unix operation */

	/*
	 * Clean up the stack in a way that db_numargs() understands, so
	 * that backtraces in ddb don't underrun the stack.  Traps for
	 * inaccessible memory are more fatal than usual this early.
	 */
	addl	$4,%esp

	/* Switch to true top of stack. */
	movl	%eax,%esp

	call	mi_startup		/* autoconfiguration, mountroot etc */
	/* NOTREACHED */
	addl	$0,%esp			/* for db_numargs() again */

/**********************************************************************
 *
 * Recover the bootinfo passed to us from the boot program
 *
 */
recover_bootinfo:
	/*
	 * This code is called in different ways depending on what loaded
	 * and started the kernel.  This is used to detect how we get the
	 * arguments from the other code and what we do with them.
	 *
	 * Old disk boot blocks:
	 *	(*btext)(howto, bootdev, cyloffset, esym);
	 *	[return address == 0, and can NOT be returned to]
	 *	[cyloffset was not supported by the FreeBSD boot code
	 *	 and always passed in as 0]
	 *	[esym is also known as total in the boot code, and
	 *	 was never properly supported by the FreeBSD boot code]
	 *
	 * Old diskless netboot code:
	 *	(*btext)(0,0,0,0,&nfsdiskless,0,0,0);
	 *	[return address != 0, and can NOT be returned to]
	 *	If we are being booted by this code it will NOT work,
	 *	so we are just going to halt if we find this case.
	 *
	 * New uniform boot code:
	 *	(*btext)(howto, bootdev, 0, 0, 0, &bootinfo)
	 *	[return address != 0, and can be returned to]
	 *
	 * There may seem to be a lot of wasted arguments in here, but
	 * that is so the newer boot code can still load very old kernels
	 * and old boot code can load new kernels.
	 */

	/*
	 * The old style disk boot blocks fake a frame on the stack and
	 * did an lret to get here.  The frame on the stack has a return
	 * address of 0.
	 */
	cmpl	$0,4(%ebp)
	je	olddiskboot

	/*
	 * We have some form of return address, so this is either the
	 * old diskless netboot code, or the new uniform code.  That can
	 * be detected by looking at the 5th argument, if it is 0
	 * we are being booted by the new uniform boot code.
	 */
	cmpl	$0,24(%ebp)
	je	newboot

	/*
	 * Seems we have been loaded by the old diskless boot code, we
	 * don't stand a chance of running as the diskless structure
	 * changed considerably between the two, so just halt.
	 */
	 hlt

	/*
	 * We have been loaded by the new uniform boot code.
	 * Let's check the bootinfo version, and if we do not understand
	 * it we return to the loader with a status of 1 to indicate this error
	 */
newboot:
	movl	28(%ebp),%ebx		/* &bootinfo.version */
	movl	BI_VERSION(%ebx),%eax
	cmpl	$1,%eax			/* We only understand version 1 */
	je	1f
	movl	$1,%eax			/* Return status */
	leave
	/*
	 * XXX this returns to our caller's caller (as is required) since
	 * we didn't set up a frame and our caller did.
	 */
	ret

1:
	/*
	 * If we have a kernelname copy it in
	 */
	movl	BI_KERNELNAME(%ebx),%esi
	cmpl	$0,%esi
	je	2f			/* No kernelname */
	movl	$MAXPATHLEN,%ecx	/* Brute force!!! */
	movl	$kernelname,%edi
	cmpb	$'/',(%esi)		/* Make sure it starts with a slash */
	je	1f
	movb	$'/',(%edi)
	incl	%edi
	decl	%ecx
1:
	cld
	rep
	movsb

2:
	/*
	 * Determine the size of the boot loader's copy of the bootinfo
	 * struct.  This is impossible to do properly because old versions
	 * of the struct don't contain a size field and there are 2 old
	 * versions with the same version number.
	 */
	movl	$BI_ENDCOMMON,%ecx	/* prepare for sizeless version */
	testl	$RB_BOOTINFO,8(%ebp)	/* bi_size (and bootinfo) valid? */
	je	got_bi_size		/* no, sizeless version */
	movl	BI_SIZE(%ebx),%ecx
got_bi_size:

	/*
	 * Copy the common part of the bootinfo struct
	 */
	movl	%ebx,%esi
	movl	$bootinfo,%edi
	cmpl	$BOOTINFO_SIZE,%ecx
	jbe	got_common_bi_size
	movl	$BOOTINFO_SIZE,%ecx
got_common_bi_size:
	cld
	rep
	movsb

#ifdef NFS_ROOT
#ifndef BOOTP_NFSV3
	/*
	 * If we have a nfs_diskless structure copy it in
	 */
	movl	BI_NFS_DISKLESS(%ebx),%esi
	cmpl	$0,%esi
	je	olddiskboot
	movl	$nfs_diskless,%edi
	movl	$NFSDISKLESS_SIZE,%ecx
	cld
	rep
	movsb
	movl	$nfs_diskless_valid,%edi
	movl	$1,(%edi)
#endif
#endif

	/*
	 * The old style disk boot.
	 *	(*btext)(howto, bootdev, cyloffset, esym);
	 * Note that the newer boot code just falls into here to pick
	 * up howto and bootdev, cyloffset and esym are no longer used
	 */
olddiskboot:
	movl	8(%ebp),%eax
	movl	%eax,boothowto
	movl	12(%ebp),%eax
	movl	%eax,bootdev

	ret


/**********************************************************************
 *
 * Identify the CPU and initialize anything special about it
 *
 */
ENTRY(identify_cpu)

	pushl	%ebx

	/* Try to toggle alignment check flag; does not exist on 386. */
	pushfl
	popl	%eax
	movl	%eax,%ecx
	orl	$PSL_AC,%eax
	pushl	%eax
	popfl
	pushfl
	popl	%eax
	xorl	%ecx,%eax
	andl	$PSL_AC,%eax
	pushl	%ecx
	popfl

	testl	%eax,%eax
	jnz	try486

	/* NexGen CPU does not have aligment check flag. */
	pushfl
	movl	$0x5555, %eax
	xorl	%edx, %edx
	movl	$2, %ecx
	clc
	divl	%ecx
	jz	trynexgen
	popfl
	movl	$CPU_386,cpu
	jmp	3f

trynexgen:
	popfl
	movl	$CPU_NX586,cpu
	movl	$0x4778654e,cpu_vendor		# store vendor string
	movl	$0x72446e65,cpu_vendor+4
	movl	$0x6e657669,cpu_vendor+8
	movl	$0,cpu_vendor+12
	jmp	3f

try486:	/* Try to toggle identification flag; does not exist on early 486s. */
	pushfl
	popl	%eax
	movl	%eax,%ecx
	xorl	$PSL_ID,%eax
	pushl	%eax
	popfl
	pushfl
	popl	%eax
	xorl	%ecx,%eax
	andl	$PSL_ID,%eax
	pushl	%ecx
	popfl

	testl	%eax,%eax
	jnz	trycpuid
	movl	$CPU_486,cpu

	/*
	 * Check Cyrix CPU
	 * Cyrix CPUs do not change the undefined flags following
	 * execution of the divide instruction which divides 5 by 2.
	 *
	 * Note: CPUID is enabled on M2, so it passes another way.
	 */
	pushfl
	movl	$0x5555, %eax
	xorl	%edx, %edx
	movl	$2, %ecx
	clc
	divl	%ecx
	jnc	trycyrix
	popfl
	jmp	3f		/* You may use Intel CPU. */

trycyrix:
	popfl
	/*
	 * IBM Bluelighting CPU also doesn't change the undefined flags.
	 * Because IBM doesn't disclose the information for Bluelighting
	 * CPU, we couldn't distinguish it from Cyrix's (including IBM
	 * brand of Cyrix CPUs).
	 */
	movl	$0x69727943,cpu_vendor		# store vendor string
	movl	$0x736e4978,cpu_vendor+4
	movl	$0x64616574,cpu_vendor+8
	jmp	3f

trycpuid:	/* Use the `cpuid' instruction. */
	xorl	%eax,%eax
	cpuid					# cpuid 0
	movl	%eax,cpu_high			# highest capability
	movl	%ebx,cpu_vendor			# store vendor string
	movl	%edx,cpu_vendor+4
	movl	%ecx,cpu_vendor+8
	movb	$0,cpu_vendor+12

	movl	$1,%eax
	cpuid					# cpuid 1
	movl	%eax,cpu_id			# store cpu_id
	movl	%ebx,cpu_procinfo		# store cpu_procinfo
	movl	%edx,cpu_feature		# store cpu_feature
	movl	%ecx,cpu_feature2		# store cpu_feature2
	rorl	$8,%eax				# extract family type
	andl	$15,%eax
	cmpl	$5,%eax
	jae	1f

	/* less than Pentium; must be 486 */
	movl	$CPU_486,cpu
	jmp	3f
1:
	/* a Pentium? */
	cmpl	$5,%eax
	jne	2f
	movl	$CPU_586,cpu
	jmp	3f
2:
	/* Greater than Pentium...call it a Pentium Pro */
	movl	$CPU_686,cpu
3:
	popl	%ebx
	ret
END(identify_cpu)

#ifdef XENHVM
/* Xen Hypercall page */
	.text
.p2align PAGE_SHIFT, 0x90	/* Hypercall_page needs to be PAGE aligned */

NON_GPROF_ENTRY(hypercall_page)
	.skip	0x1000, 0x90	/* Fill with "nop"s */
#endif
