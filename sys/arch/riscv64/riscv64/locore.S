/*	$OpenBSD: locore.S,v 1.21 2025/01/22 18:18:58 jca Exp $	*/

/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "assym.h"

#include <sys/syscall.h>
#include <machine/asm.h>
#include <machine/param.h>
#include <machine/vmparam.h>
#include <machine/riscvreg.h>
#include <machine/pte.h>

	.globl	kernbase
	.set	kernbase, KERNBASE

	.text
/*
 * Main entry point.  Arguments are as follows:
 *  - a0 = esym
 *  - a2 = dtbp
 *
 * It is expected that only a single CPU will enter here.
 */
	.globl _start_kern_bootstrap
_start_kern_bootstrap:

	/* Set the global pointer */
.option push
.option norelax
	lla	gp, __global_pointer$
.option pop

	/*
	 * Page tables setup
	 */

	/* Get the kernel's load address */
	jal	get_physmem

	/* Add L1 entry for kernel */
	lla	s1, pagetable_l1
	lla	s2, pagetable_l2	/* Link to next level PN */
	srli	s2, s2, PAGE_SHIFT

	li	a5, KERNBASE
	srli	a5, a5, L1_SHIFT	/* >> L1_SHIFT */
	andi	a5, a5, 0x1ff		/* & 0x1ff */
	li	t4, PTE_V
	slli	t5, s2, PTE_PPN0_S	/* (s2 << PTE_PPN0_S) */
	or	t6, t4, t5

	/* Store L1 PTE entry to position */
	li	a6, PTE_SIZE
	mulw	a5, a5, a6
	add	t0, s1, a5
	sd	t6, (t0)

	/* Level 2 superpages (512 x 2MiB) */
	lla	s1, pagetable_l2
	srli	t4, s9, L2_SHIFT	/* Div physmem base by 2 MiB */
	li	t2, 512			/* Build 512 entries */
	add	t3, t4, t2
	li	t0, (PTE_KERN | PTE_X)
1:
	slli	t2, t4, PTE_PPN1_S	/* << PTE_PPN1_S */
	or	t5, t0, t2
	sd	t5, (s1)		/* Store PTE entry to position */
	addi	s1, s1, PTE_SIZE

	addi	t4, t4, 1
	bltu	t4, t3, 1b

	/* Page tables END */

	/* Setup supervisor trap vector */
	lla	t0, va
	sub	t0, t0, s9
	li	t1, KERNBASE
	add	t0, t0, t1
	csrw	stvec, t0

	/* Set page tables base register */
	lla	s2, pagetable_l1
	srli	s2, s2, PAGE_SHIFT
	li	t0, SATP_MODE_SV39
	or	s2, s2, t0
	sfence.vma
	csrw	satp, s2

	.align 2
va:
	sfence.vma

	/* Set the global pointer again, this time with the virtual address. */
.option push
.option norelax
	lla	gp, __global_pointer$
.option pop

	/* Setup supervisor trap vector */
	la	t0, cpu_exception_handler
	csrw	stvec, t0

	/* Ensure sscratch is zero */
	li	t0, 0
	csrw	sscratch, t0

	/* Initialize stack pointer */
	la	sp, initstack_end

	/* Clear frame pointer */
	mv	s0, zero

	/* Allocate space for riscv_bootparams */
	addi	sp, sp, -RISCV_BOOTPARAMS_SIZEOF
	andi	sp, sp, ~STACKALIGNBYTES

	/* Clear BSS */
	la	t0, __bss_start
	la	t1, _end
1:
	sd	zero, 0(t0)
	addi	t0, t0, 8
	bltu	t0, t1, 1b

	/* Fill riscv_bootparams */
	la	t0, pagetable_l1
	sd	t0, RISCV_BOOTPARAMS_KERN_L1PT(sp)
	sd	s9, RISCV_BOOTPARAMS_KERN_PHYS(sp)
	la	t0, initstack
	sd	t0, RISCV_BOOTPARAMS_KERN_STACK(sp)
	sd	a1, RISCV_BOOTPARAMS_DTBP_PHYS(sp)

	/* Set esym to virtual address of symbol table end */
	lla	t0, esym
	sub	t1, a0, s9
	li	t2, KERNBASE
	add	t1, t1, t2
	sd	t1, 0(t0)

	mv	a0, sp
	call	initriscv		/* Off we go */
	call	main

/*
 * Get the physical address the kernel is loaded to. Returned in s9.
 */
get_physmem:
	lla	t0, virt_map	/* physical address of virt_map */
	ld	t1, 0(t0)	/* virtual address of virt_map */
	sub	t1, t1, t0	/* calculate phys->virt delta */
	li	t2, KERNBASE
	sub	s9, t2, t1	/* s9 = physmem base */
	ret

	.data
	.align	4
initstack:
	.space	USPACE
initstack_end:

	.rodata
ENTRY(sigcode)
	mv	a0, sp
	addi	a0, a0, SF_SC
	li	t0, SYS_sigreturn
	.globl	sigcodecall
sigcodecall:
	ecall
	.globl sigcoderet
sigcoderet:
	unimp
END(sigcode)
	.globl esigcode
esigcode:

	.globl	sigfill
sigfill:
	unimp
esigfill:
	.globl	sigfillsiz
sigfillsiz:
	.word	esigfill - sigfill

	.data
	.global esym
esym:
	.quad	end

	.align	12
pagetable_l1:
	.space	PAGE_SIZE
pagetable_l2:
	.space	PAGE_SIZE

	.align 3
virt_map:
	.quad	virt_map

#ifdef MULTIPROCESSOR

	.text
	.globl cpu_hatch
cpu_hatch:
	ld	tp, CI_SELF(a1)
	ld	a2, CI_SATP(a1)

	/* Set the global pointer */
.option push
.option norelax
	lla	gp, __global_pointer$
.option pop

	/* Setup stack pointer */
	ld	sp, CI_INITSTACK_END(a1)

	/* Get the kernel's load address */
	jal	get_physmem

	/* Setup supervisor trap vector */
	lla	t0, mpva
	sub	t0, t0, s9
	li	t1, KERNBASE
	add	t0, t0, t1
	csrw	stvec, t0

	/* Set page tables base register */
	lla	s2, pagetable_l1
	srli	s2, s2, PAGE_SHIFT
	li	t0, SATP_MODE_SV39
	or	s2, s2, t0
	sfence.vma
	csrw	satp, s2

	.align 2
mpva:
	sfence.vma

	/* Set the global pointer again, this time with the virtual address. */
.option push
.option norelax
	lla	gp, __global_pointer$
.option pop

	/* Setup supervisor trap vector */
	la	t0, cpu_exception_handler
	csrw	stvec, t0

	/* Ensure sscratch is zero */
	li	t0, 0
	csrw	sscratch, t0

	/* Switch to real kernel page tables */
	csrw	satp, a2
	sfence.vma

	call	cpu_start_secondary

#endif
