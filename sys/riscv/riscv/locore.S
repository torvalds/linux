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
 *
 * $FreeBSD$
 */

#include "assym.inc"

#include <sys/syscall.h>
#include <machine/asm.h>
#include <machine/param.h>
#include <machine/trap.h>
#include <machine/riscvreg.h>
#include <machine/pte.h>

	.globl	kernbase
	.set	kernbase, KERNBASE

	/* Trap entries */
	.text

	/* Reset vector */
	.text
	.globl _start
_start:
	/* Get the physical address kernel loaded to */
	la	t0, virt_map
	ld	t1, 0(t0)
	sub	t1, t1, t0
	li	t2, KERNBASE
	sub	s9, t2, t1	/* s9 = physmem base */
	mv	s10, a0		/* s10 = hart id */
	mv	s11, a1		/* s11 = dtbp */

	/* Direct secondary cores to mpentry */
	bnez	s10, mpentry

	/*
	 * Page tables
	 */

	/* Add L1 entry for kernel */
	la	s1, pagetable_l1
	la	s2, pagetable_l2	/* Link to next level PN */
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
	la	s1, pagetable_l2
	srli	t4, s9, 21		/* Div physmem base by 2 MiB */
	li	t2, 512			/* Build 512 entries */
	add	t3, t4, t2
	li	t5, 0
2:
	li	t0, (PTE_KERN | PTE_X)
	slli	t2, t4, PTE_PPN1_S	/* << PTE_PPN1_S */
	or	t5, t0, t2
	sd	t5, (s1)		/* Store PTE entry to position */
	addi	s1, s1, PTE_SIZE

	addi	t4, t4, 1
	bltu	t4, t3, 2b

	/* Create an L1 page for early devmap */
	la	s1, pagetable_l1
	la	s2, pagetable_l2_devmap	/* Link to next level PN */
	srli	s2, s2, PAGE_SHIFT

	li	a5, (VM_MAX_KERNEL_ADDRESS - L2_SIZE)
	srli	a5, a5, L1_SHIFT	/* >> L1_SHIFT */
	andi	a5, a5, 0x1ff		/* & 0x1ff */
	li	t4, PTE_V
	slli	t5, s2, PTE_PPN0_S	/* (s2 << PTE_PPN0_S) */
	or	t6, t4, t5

	/* Store single level1 PTE entry to position */
	li	a6, PTE_SIZE
	mulw	a5, a5, a6
	add	t0, s1, a5
	sd	t6, (t0)

	/* Create an L2 page superpage for DTB */
	la	s1, pagetable_l2_devmap
	mv	s2, s11
	srli	s2, s2, PAGE_SHIFT

	li	t0, (PTE_KERN)
	slli	t2, s2, PTE_PPN0_S	/* << PTE_PPN0_S */
	or	t0, t0, t2

	/* Store PTE entry to position */
	li	a6, PTE_SIZE
	li	a5, 510
	mulw	a5, a5, a6
	add	t1, s1, a5
	sd	t0, (t1)

	/* Page tables END */

	/* Setup supervisor trap vector */
	la	t0, va
	sub	t0, t0, s9
	li	t1, KERNBASE
	add	t0, t0, t1
	csrw	stvec, t0

	/* Set page tables base register */
	la	s2, pagetable_l1
	srli	s2, s2, PAGE_SHIFT
	li	t0, SATP_MODE_SV39
	or	s2, s2, t0
	sfence.vma
	csrw	satp, s2

	.align 2
va:

	/* Setup supervisor trap vector */
	la	t0, cpu_exception_handler
	csrw	stvec, t0

	/* Ensure sscratch is zero */
	li	t0, 0
	csrw	sscratch, t0

	/* Initialize stack pointer */
	la	s3, initstack_end
	mv	sp, s3
	addi	sp, sp, -PCB_SIZE

	/* Clear BSS  */
	la	a0, _C_LABEL(__bss_start)
	la	s1, _C_LABEL(_end)
1:
	sd	zero, 0(a0)
	addi	a0, a0, 8
	bltu	a0, s1, 1b

	/* Fill riscv_bootparams */
	addi	sp, sp, -40

	la	t0, pagetable_l1
	sd	t0, 0(sp) /* kern_l1pt */
	sd	s9, 8(sp) /* kern_phys */

	la	t0, initstack_end
	sd	t0, 16(sp) /* kern_stack */

	li	t0, (VM_MAX_KERNEL_ADDRESS - 2 * L2_SIZE)
	sd	t0, 24(sp) /* dtbp_virt */
	sd	s11, 32(sp) /* dtbp_phys */

	mv	a0, sp
	call	_C_LABEL(initriscv)	/* Off we go */
	call	_C_LABEL(mi_startup)

	.align  4
initstack:
	.space  (PAGE_SIZE * KSTACK_PAGES)
initstack_end:

ENTRY(sigcode)
	mv	a0, sp
	addi	a0, a0, SF_UC

1:
	li	t0, SYS_sigreturn
	ecall

	/* sigreturn failed, exit */
	li	t0, SYS_exit
	ecall

	j	1b
END(sigcode)
	/* This may be copied to the stack, keep it 16-byte aligned */
	.align	3
esigcode:

	.data
	.align	3
	.global	szsigcode
szsigcode:
	.quad	esigcode - sigcode

	.align	12
pagetable_l1:
	.space	PAGE_SIZE
pagetable_l2:
	.space	PAGE_SIZE
pagetable_l2_devmap:
	.space	PAGE_SIZE

        .align 3
virt_map:
        .quad   virt_map

	/* Not in use, but required for linking. */
	.align 3
	.globl __global_pointer$
__global_pointer$:
	.space	8

	.globl init_pt_va
init_pt_va:
	.quad pagetable_l2	/* XXX: Keep page tables VA */

#ifndef SMP
ENTRY(mpentry)
1:
	wfi
	j	1b
END(mpentry)
#else
/*
 * mpentry(unsigned long)
 *
 * Called by a core when it is being brought online.
 */
ENTRY(mpentry)
	/*
	 * Calculate the offset to __riscv_boot_ap
	 * for the current core, cpuid is in a0.
	 */
	li	t1, 4
	mulw	t1, t1, a0
	/* Get the pointer */
	la	t0, __riscv_boot_ap
	add	t0, t0, t1

1:
	/* Wait the kernel to be ready */
	lw	t1, 0(t0)
	beqz	t1, 1b

	/* Setup stack pointer */
	la	t0, secondary_stacks
	li	t1, (PAGE_SIZE * KSTACK_PAGES)
	mulw	t1, t1, s10
	add	t0, t0, t1
	sub	t0, t0, s9
	li	t1, KERNBASE
	add	sp, t0, t1

	/* Setup supervisor trap vector */
	la	t0, mpva
	sub	t0, t0, s9
	li	t1, KERNBASE
	add	t0, t0, t1
	csrw	stvec, t0

	/* Set page tables base register */
	la	s2, pagetable_l1
	srli	s2, s2, PAGE_SHIFT
	li	t0, SATP_MODE_SV39
	or	s2, s2, t0
	sfence.vma
	csrw	satp, s2

	.align 2
mpva:
	/* Setup supervisor trap vector */
	la	t0, cpu_exception_handler
	csrw	stvec, t0

	/* Ensure sscratch is zero */
	li	t0, 0
	csrw	sscratch, t0

	call	init_secondary
END(mpentry)
#endif
