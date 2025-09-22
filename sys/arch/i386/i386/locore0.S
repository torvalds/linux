/*	$OpenBSD: locore0.S,v 1.12 2024/10/21 07:21:18 jsg Exp $	*/
/*	$NetBSD: locore.s,v 1.145 1996/05/03 19:41:19 christos Exp $	*/

/*-
 * Copyright (c) 1993, 1994, 1995 Charles M. Hannum.  All rights reserved.
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
 *	@(#)locore.s	7.3 (Berkeley) 5/13/91
 */

#include "assym.h"
#include "ksyms.h"

#include <machine/param.h>
#include <machine/pte.h>
#include <machine/specialreg.h>

#include <dev/isa/isareg.h>

/*
 * override user-land alignment before including asm.h
 */

#define	ALIGN_DATA	.align  4
#define	ALIGN_TEXT	.align  4,0x90	/* 4-byte boundaries, NOP-filled */
#define _ALIGN_TEXT	ALIGN_TEXT
#include <machine/asm.h>

/*
 * Initialization
 */
	.data

	.space 512,0xcc
tmpstk:


#define	RELOC(x)	((x) - KERNBASE)

	.text
	.globl	start
	.globl	kernel_text
	kernel_text = KERNTEXTOFF
start:	movw	$0x1234,0x472			# warm boot

	/*
	 * Load parameters from stack (howto, bootdev, unit, bootapiver, esym).
	 * note: (%esp) is return address of boot
	 * (If we want to hold onto /boot, it's physical %esp up to _end.)
	 */
	movl	4(%esp),%eax
	movl	%eax,RELOC(boothowto)
	movl	8(%esp),%eax
	movl	%eax,RELOC(bootdev)
	movl	16(%esp),%eax
	testl	%eax,%eax
	jz	1f
	addl	$KERNBASE,%eax
1:	movl	%eax,RELOC(esym)
	movl	$__kernel_bss_end, RELOC(ssym)

	movl	12(%esp),%eax
	movl	%eax,RELOC(bootapiver)
	movl	28(%esp), %eax
	movl	%eax, RELOC(bootargc)
	movl	32(%esp), %eax
	movl	%eax, RELOC(bootargv)

	/* First, reset the PSL. */
	pushl	$PSL_MBO
	popfl

	/* Clear segment registers; null until proc0 setup */
	xorl	%eax,%eax
	movw	%ax,%fs
	movw	%ax,%gs

	/* Find out our CPU type. */

.Ltry486:	/* Try to toggle identification flag; does not exist on early 486s. */
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
	jnz	.Ltry586
.Lis486:

	jmp	2f

.Ltry586:	/* Use the `cpuid' instruction. */
	xorl	%eax,%eax
	cpuid
	movl	%eax,RELOC(cpuid_level)
	movl	%ebx,RELOC(cpu_vendor)			# store vendor string
	movl	%edx,RELOC(cpu_vendor)+4
	movl	%ecx,RELOC(cpu_vendor)+8
	movl	$0,  RELOC(cpu_vendor)+12

	/*
	 * Determine if CPU has meltdown. Certain Intel CPUs do not properly
	 * respect page permissions when speculatively loading data into
	 * the cache ("Meltdown" CVE). These CPUs must utilize a secondary
	 * sanitized page table lacking kernel mappings when executing user
	 * processes, and may not use PG_G global PTEs for kernel VAs.
	 */
	movl	$0x1, RELOC(cpu_meltdown)
	movl	$0x0, RELOC(pg_g_kern)

	cmpl	$0x756e6547,%ebx	# "Genu"
	jne	.Lcpu_secure
	cmpl	$0x6c65746e,%ecx	# "ntel"
	jne	.Lcpu_secure
	cmpl	$0x49656e69,%edx	# "ineI"
	jne	.Lcpu_secure

	/*
	 * Intel CPU, now check if IA32_ARCH_CAPABILITIES is supported and
	 * if it says this CPU is safe.
	 */
	movl	$0x0,%eax
	cpuid
	cmpl	$0x7,%eax
	jl	.Lcpu_check_finished

        movl	$0x7,%eax
        cpuid
        testl	$SEFF0EDX_ARCH_CAP,%edx
        jz      .Lcpu_check_finished

        /* IA32_ARCH_CAPABILITIES MSR available, use it to check CPU security */
        movl	$MSR_ARCH_CAPABILITIES,%ecx
        rdmsr
        testl	$ARCH_CAP_RDCL_NO,%eax
        jz	.Lcpu_check_finished

.Lcpu_secure:
	movl	$0x0, RELOC(cpu_meltdown)
	movl	$PG_G, RELOC(pg_g_kern)

.Lcpu_check_finished:
	movl	$1,%eax
	xorl	%ecx,%ecx
	cpuid
	movl	%eax,RELOC(cpu_id)		# store cpu_id and features
	movl	%ebx,RELOC(cpu_miscinfo)
	movl	%edx,RELOC(cpu_feature)
	movl	%ecx,RELOC(cpu_ecxfeature)

	movl	RELOC(cpuid_level),%eax
	cmp	$2,%eax
	jl	1f

	movl	$2,%eax
	cpuid

	movl	%eax,RELOC(cpu_cache_eax)
	movl	%ebx,RELOC(cpu_cache_ebx)
	movl	%ecx,RELOC(cpu_cache_ecx)
	movl	%edx,RELOC(cpu_cache_edx)

	movl	$0x0a,%eax
	cpuid
	movl	%eax,RELOC(cpu_perf_eax)
	movl	%ebx,RELOC(cpu_perf_ebx)
	movl	%edx,RELOC(cpu_perf_edx)

1:
	/* Check if brand identification string is supported */
	movl	$0x80000000,%eax
	cpuid
	cmpl	$0x80000000,%eax
	jbe	2f
	movl	$0x80000001,%eax
	cpuid
	movl	%eax,RELOC(ecpu_eaxfeature)
	movl	%edx,RELOC(ecpu_feature)
	movl	%ecx,RELOC(ecpu_ecxfeature)
	movl	$0x80000002,%eax
	cpuid
	movl	%eax,RELOC(cpu_brandstr)
	movl	%ebx,RELOC(cpu_brandstr)+4
	movl	%ecx,RELOC(cpu_brandstr)+8
	movl	%edx,RELOC(cpu_brandstr)+12
	movl	$0x80000003,%eax
	cpuid
	movl	%eax,RELOC(cpu_brandstr)+16
	movl	%ebx,RELOC(cpu_brandstr)+20
	movl	%ecx,RELOC(cpu_brandstr)+24
	movl	%edx,RELOC(cpu_brandstr)+28
	movl	$0x80000004,%eax
	cpuid
	movl	%eax,RELOC(cpu_brandstr)+32
	movl	%ebx,RELOC(cpu_brandstr)+36
	movl	%ecx,RELOC(cpu_brandstr)+40
	andl	$0x00ffffff,%edx	/* Shouldn't be necessary */
	movl	%edx,RELOC(cpu_brandstr)+44

	movl	$0x80000007,%eax
	cpuid
	movl	%edx,RELOC(cpu_apmi_edx)

2:
	/*
	 * Finished with old stack; load new %esp now instead of later so we
	 * can trace this code without having to worry about the trace trap
	 * clobbering the memory test or the zeroing of the bss+bootstrap page
	 * tables.
	 *
	 * The boot program should check:
	 *	text+data <= &stack_variable - more_space_for_stack
	 *	text+data+bss+pad+space_for_page_tables <= end_of_memory
	 * Oops, the gdt is in the carcass of the boot program so clearing
	 * the rest of memory is still not possible.
	 */
	movl	$RELOC(tmpstk),%esp	# bootstrap stack end location

/*
 * Virtual address space of kernel:
 *
 * text | data | bss | [syms] | proc0 kstack | page dir     | Sysmap
 *			      0             1       2       6
 */
#define	PROC0STACK	((0)		* NBPG)
#define	PROC0PDIR	((  UPAGES)	* NBPG)
#define	SYSMAP		((4+UPAGES)	* NBPG)
#define	TABLESIZE	((4+UPAGES) * NBPG) /* + nkpde * NBPG */

	/* Find end of kernel image. */
	movl	$RELOC(end),%edi
#if (NKSYMS || defined(DDB))
	/* Save the symbols (if loaded). */
	movl	RELOC(esym),%eax
	testl	%eax,%eax
	jz	1f
	subl	$KERNBASE,%eax
	movl	%eax,%edi
1:
#endif

	/* Calculate where to start the bootstrap tables. */
	movl	%edi,%esi			# edi = esym ? esym : end
	addl	$PGOFSET, %esi			# page align up
	andl	$~PGOFSET, %esi

	/*
	 * Calculate the size of the kernel page table directory, and
	 * how many entries it will have.
	 */
	movl	RELOC(nkpde),%ecx		# get nkpde
	cmpl	$NKPTP_MIN,%ecx			# larger than min?
	jge	1f
	movl	$NKPTP_MIN,%ecx			# set at min
	jmp	2f
1:	cmpl	RELOC(nkptp_max),%ecx		# larger than max?
	jle	2f
	movl	RELOC(nkptp_max),%ecx
2:	movl	%ecx,RELOC(nkpde)		# and store it back

	/* Clear memory for bootstrap tables. */
	shll	$PGSHIFT,%ecx
	addl	$TABLESIZE,%ecx
	addl	%esi,%ecx			# end of tables
	subl	%edi,%ecx			# size of tables
	shrl	$2,%ecx
	xorl	%eax, %eax
	rep
	stosl

/*
 * fillkpt
 *	eax = pte (page frame | control | status)
 *	ebx = page table address
 *	ecx = number of pages to map
 */
#define	fillkpt		\
1:	movl	%eax,(%ebx)	; \
	addl	$NBPG,%eax	; /* increment physical address */ \
	addl	$4,%ebx		; /* next pte */ \
	loop	1b		;

/*
 * Build initial page tables.
 */
	/* Calculate end of text segment, rounded to a page. */
	leal	(RELOC(etext)+PGOFSET),%edx
	andl	$~PGOFSET,%edx

	/* Skip over the first 2MB. */
	movl	$RELOC(KERNTEXTOFF),%eax
	movl	%eax,%ecx
	shrl	$PGSHIFT,%ecx
	leal	(SYSMAP)(%esi,%ecx,4),%ebx

	/* Map the kernel text read-only. */
	movl	%edx,%ecx
	subl	%eax,%ecx
	shrl	$PGSHIFT,%ecx
	orl	$(PG_V|PG_KR),%eax
	fillkpt

	/* Map the data, BSS, and bootstrap tables read-write. */
	leal	(PG_V|PG_KW)(%edx),%eax
	movl	RELOC(nkpde),%ecx
	shll	$PGSHIFT,%ecx
	addl	$TABLESIZE,%ecx
	addl	%esi,%ecx				# end of tables
	subl	%edx,%ecx				# subtract end of text
	shrl	$PGSHIFT,%ecx
	fillkpt

	/* Map ISA I/O memory. */
	movl	$(IOM_BEGIN|PG_V|PG_KW/*|PG_N*/),%eax	# having these bits set
	movl	$(IOM_SIZE>>PGSHIFT),%ecx		# for this many pte s,
	fillkpt

/*
 * Construct a page table directory.
 */
	movl	RELOC(nkpde),%ecx			# count of pdes,
	leal	(PROC0PDIR+0*4)(%esi),%ebx		# where temp maps!
	leal	(SYSMAP+PG_V|PG_KW|PG_U|PG_M)(%esi),%eax # pte for KPT in proc 0
	fillkpt

/*
 * Map kernel PDEs: this is the real mapping used
 * after the temp mapping outlives its usefulness.
 */
	movl	RELOC(nkpde),%ecx			# count of pde s,
	leal	(PROC0PDIR+PDSLOT_KERN*4)(%esi),%ebx	# map them high
	leal	(SYSMAP+PG_V|PG_KW|PG_U|PG_M)(%esi),%eax # pte for KPT in proc 0
	fillkpt

	/* Install a PDE recursively mapping page directory as a page table! */
	leal	(PROC0PDIR+PG_V|PG_KW|PG_U|PG_M)(%esi),%eax # pte for ptd
	movl	%eax,(PROC0PDIR+PDSLOT_PTE*4)(%esi)	# recursive PD slot
	addl	$NBPG, %eax				# pte for ptd[1]
	movl    %eax,(PROC0PDIR+(PDSLOT_PTE+1)*4)(%esi) # recursive PD slot

	/* Save phys. addr of PTD, for libkvm. */
	leal	(PROC0PDIR)(%esi),%eax		# phys address of ptd in proc 0
	movl	%eax,RELOC(PTDpaddr)

	/* Load base of page directory and enable mapping. */
	movl	%eax,%cr3		# load ptd addr into mmu
	movl	%cr0,%eax		# get control word
					# enable paging & NPX emulation
	orl	$(CR0_PE|CR0_PG|CR0_NE|CR0_TS|CR0_EM|CR0_MP),%eax
	movl	%eax,%cr0		# and let's page NOW!

	pushl	$begin			# jump to high mem
	ret

begin:
	/* Now running relocated at KERNBASE.  Remove double mapping. */
	movl	nkpde,%ecx			# for this many pde s,
	leal	(PROC0PDIR+0*4)(%esi),%ebx	# which is where temp maps!
	addl	$(KERNBASE), %ebx	# now use relocated address
1:	movl	$0,(%ebx)
	addl	$4,%ebx	# next pde
	loop	1b

	/* Relocate atdevbase. */
	movl	nkpde,%edx
	shll	$PGSHIFT,%edx
	addl	$(TABLESIZE+KERNBASE),%edx
	addl	%esi,%edx
	movl	%edx,atdevbase

	/* Set up bootstrap stack. */
	leal	(PROC0STACK+KERNBASE)(%esi),%eax
	movl	%eax,proc0paddr
	leal	(USPACE-FRAMESIZE)(%eax),%esp
	leal	(PROC0PDIR)(%esi),%ebx	# phys address of ptd in proc 0
	movl	%ebx,PCB_CR3(%eax)	# pcb->pcb_cr3
	xorl	%ebp,%ebp		# mark end of frames

	movl	nkpde,%eax
	shll	$PGSHIFT,%eax
	addl	$TABLESIZE,%eax
	addl	%esi,%eax		# skip past stack and page tables
	pushl	%eax
	call	init386			# wire 386 chip for unix operation
	addl	$4,%esp

	call	main
	/* NOTREACHED */
