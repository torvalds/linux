/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Portions Copyright 2008 John Birrell <jb@freebsd.org>
 *
 * $FreeBSD$
 *
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#define _ASM

#include <machine/asmacros.h>
#include <sys/cpuvar_defs.h>
#include <sys/dtrace.h>

#include "assym.inc"

#define INTR_POP				\
	MEXITCOUNT;				\
	movq	TF_RDI(%rsp),%rdi;		\
	movq	TF_RSI(%rsp),%rsi;		\
	movq	TF_RDX(%rsp),%rdx;		\
	movq	TF_RCX(%rsp),%rcx;		\
	movq	TF_R8(%rsp),%r8;		\
	movq	TF_R9(%rsp),%r9;		\
	movq	TF_RAX(%rsp),%rax;		\
	movq	TF_RBX(%rsp),%rbx;		\
	movq	TF_RBP(%rsp),%rbp;		\
	movq	TF_R10(%rsp),%r10;		\
	movq	TF_R11(%rsp),%r11;		\
	movq	TF_R12(%rsp),%r12;		\
	movq	TF_R13(%rsp),%r13;		\
	movq	TF_R14(%rsp),%r14;		\
	movq	TF_R15(%rsp),%r15;		\
	testb	$SEL_RPL_MASK,TF_CS(%rsp);	\
	jz	1f;				\
	cli;					\
	swapgs;					\
1:	addq	$TF_RIP,%rsp;


	ENTRY(dtrace_invop_start)

	/*
	 * #BP traps with %rip set to the next address. We need to decrement
	 * the value to indicate the address of the int3 (0xcc) instruction
	 * that we substituted.
	 */
	movq	TF_RIP(%rsp), %rdi
	decq	%rdi
	movq	%rsp, %rsi
	movq	TF_RAX(%rsp), %rdx
	call	dtrace_invop
	ALTENTRY(dtrace_invop_callsite)
	cmpl	$DTRACE_INVOP_PUSHL_EBP, %eax
	je	bp_push
	cmpl	$DTRACE_INVOP_LEAVE, %eax
	je	bp_leave
	cmpl	$DTRACE_INVOP_NOP, %eax
	je	bp_nop
	cmpl	$DTRACE_INVOP_RET, %eax
	je	bp_ret

	/* When all else fails handle the trap in the usual way. */
	jmpq	*dtrace_invop_calltrap_addr

bp_push:
	/*
	 * We must emulate a "pushq %rbp".  To do this, we pull the stack
	 * down 8 bytes, and then store the base pointer.
	 */
	INTR_POP
	subq	$16, %rsp		/* make room for %rbp */
	pushq	%rax			/* push temp */
	movq	24(%rsp), %rax		/* load calling RIP */
	movq	%rax, 8(%rsp)		/* store calling RIP */
	movq	32(%rsp), %rax		/* load calling CS */
	movq	%rax, 16(%rsp)		/* store calling CS */
	movq	40(%rsp), %rax		/* load calling RFLAGS */
	movq	%rax, 24(%rsp)		/* store calling RFLAGS */
	movq	48(%rsp), %rax		/* load calling RSP */
	subq	$8, %rax		/* make room for %rbp */
	movq	%rax, 32(%rsp)		/* store calling RSP */
	movq	56(%rsp), %rax		/* load calling SS */
	movq	%rax, 40(%rsp)		/* store calling SS */
	movq	32(%rsp), %rax		/* reload calling RSP */
	movq	%rbp, (%rax)		/* store %rbp there */
	popq	%rax			/* pop off temp */
	iretq				/* return from interrupt */
	/*NOTREACHED*/

bp_leave:
	/*
	 * We must emulate a "leave", which is the same as a "movq %rbp, %rsp"
	 * followed by a "popq %rbp".  This is quite a bit simpler on amd64
	 * than it is on i386 -- we can exploit the fact that the %rsp is
	 * explicitly saved to effect the pop without having to reshuffle
	 * the other data pushed for the trap.
	 */
	INTR_POP
	pushq	%rax			/* push temp */
	movq	8(%rsp), %rax		/* load calling RIP */
	movq	%rax, 8(%rsp)		/* store calling RIP */
	movq	(%rbp), %rax		/* get new %rbp */
	addq	$8, %rbp		/* adjust new %rsp */
	movq	%rbp, 32(%rsp)		/* store new %rsp */
	movq	%rax, %rbp		/* set new %rbp */
	popq	%rax			/* pop off temp */
	iretq				/* return from interrupt */
	/*NOTREACHED*/

bp_nop:
	/* We must emulate a "nop". */
	INTR_POP
	iretq
	/*NOTREACHED*/

bp_ret:
	INTR_POP
	pushq	%rax			/* push temp */
	movq	32(%rsp), %rax		/* load %rsp */
	movq	(%rax), %rax		/* load calling RIP */
	movq	%rax, 8(%rsp)		/* store calling RIP */
	addq	$8, 32(%rsp)		/* adjust new %rsp */
	popq	%rax			/* pop off temp */
	iretq				/* return from interrupt */
	/*NOTREACHED*/

	END(dtrace_invop_start)

/*
void dtrace_invop_init(void)
*/
	ENTRY(dtrace_invop_init)
	movq	$dtrace_invop_start, dtrace_invop_jump_addr(%rip)
	ret
	END(dtrace_invop_init)

/*
void dtrace_invop_uninit(void)
*/
	ENTRY(dtrace_invop_uninit)
	movq	$0, dtrace_invop_jump_addr(%rip)
	ret
	END(dtrace_invop_uninit)

/*
greg_t dtrace_getfp(void)
*/
	ENTRY(dtrace_getfp)
	movq	%rbp, %rax
	ret
	END(dtrace_getfp)

/*
uint32_t
dtrace_cas32(uint32_t *target, uint32_t cmp, uint32_t new)
*/
	ENTRY(dtrace_cas32)
	movl	%esi, %eax
	lock
	cmpxchgl %edx, (%rdi)
	ret
	END(dtrace_cas32)

/*
void *
dtrace_casptr(void *target, void *cmp, void *new)
*/
	ENTRY(dtrace_casptr)
	movq	%rsi, %rax
	lock
	cmpxchgq %rdx, (%rdi)
	ret
	END(dtrace_casptr)

/*
uintptr_t
dtrace_caller(int aframes)
*/
	ENTRY(dtrace_caller)
	movq	$-1, %rax
	ret
	END(dtrace_caller)

/*
void
dtrace_copy(uintptr_t src, uintptr_t dest, size_t size)
*/
	ENTRY(dtrace_copy_nosmap)
	pushq	%rbp
	movq	%rsp, %rbp

	xchgq	%rdi, %rsi		/* make %rsi source, %rdi dest */
	movq	%rdx, %rcx		/* load count */
	repz				/* repeat for count ... */
	smovb				/*   move from %ds:rsi to %ed:rdi */
	leave
	ret
	END(dtrace_copy_nosmap)

	ENTRY(dtrace_copy_smap)
	pushq	%rbp
	movq	%rsp, %rbp

	xchgq	%rdi, %rsi		/* make %rsi source, %rdi dest */
	movq	%rdx, %rcx		/* load count */
	stac
	repz				/* repeat for count ... */
	smovb				/*   move from %ds:rsi to %ed:rdi */
	clac
	leave
	ret
	END(dtrace_copy_smap)

/*
void
dtrace_copystr(uintptr_t uaddr, uintptr_t kaddr, size_t size,
    volatile uint16_t *flags)
*/
	ENTRY(dtrace_copystr_nosmap)
	pushq	%rbp
	movq	%rsp, %rbp

0:
	movb	(%rdi), %al		/* load from source */
	movb	%al, (%rsi)		/* store to destination */
	addq	$1, %rdi		/* increment source pointer */
	addq	$1, %rsi		/* increment destination pointer */
	subq	$1, %rdx		/* decrement remaining count */
	cmpb	$0, %al
	je	2f
	testq	$0xfff, %rdx		/* test if count is 4k-aligned */
	jnz	1f			/* if not, continue with copying */
	testq	$CPU_DTRACE_BADADDR, (%rcx) /* load and test dtrace flags */
	jnz	2f
1:
	cmpq	$0, %rdx
	jne	0b
2:
	leave
	ret

	END(dtrace_copystr_nosmap)

	ENTRY(dtrace_copystr_smap)
	pushq	%rbp
	movq	%rsp, %rbp

	stac
0:
	movb	(%rdi), %al		/* load from source */
	movb	%al, (%rsi)		/* store to destination */
	addq	$1, %rdi		/* increment source pointer */
	addq	$1, %rsi		/* increment destination pointer */
	subq	$1, %rdx		/* decrement remaining count */
	cmpb	$0, %al
	je	2f
	testq	$0xfff, %rdx		/* test if count is 4k-aligned */
	jnz	1f			/* if not, continue with copying */
	testq	$CPU_DTRACE_BADADDR, (%rcx) /* load and test dtrace flags */
	jnz	2f
1:
	cmpq	$0, %rdx
	jne	0b
2:
	clac
	leave
	ret

	END(dtrace_copystr_smap)

/*
uintptr_t
dtrace_fulword(void *addr)
*/
	ENTRY(dtrace_fulword_nosmap)
	movq	(%rdi), %rax
	ret
	END(dtrace_fulword_nosmap)

	ENTRY(dtrace_fulword_smap)
	stac
	movq	(%rdi), %rax
	clac
	ret
	END(dtrace_fulword_smap)

/*
uint8_t
dtrace_fuword8_nocheck(void *addr)
*/
	ENTRY(dtrace_fuword8_nocheck_nosmap)
	xorq	%rax, %rax
	movb	(%rdi), %al
	ret
	END(dtrace_fuword8_nocheck_nosmap)

	ENTRY(dtrace_fuword8_nocheck_smap)
	stac
	xorq	%rax, %rax
	movb	(%rdi), %al
	clac
	ret
	END(dtrace_fuword8_nocheck_smap)

/*
uint16_t
dtrace_fuword16_nocheck(void *addr)
*/
	ENTRY(dtrace_fuword16_nocheck_nosmap)
	xorq	%rax, %rax
	movw	(%rdi), %ax
	ret
	END(dtrace_fuword16_nocheck_nosmap)

	ENTRY(dtrace_fuword16_nocheck_smap)
	stac
	xorq	%rax, %rax
	movw	(%rdi), %ax
	clac
	ret
	END(dtrace_fuword16_nocheck_smap)

/*
uint32_t
dtrace_fuword32_nocheck(void *addr)
*/
	ENTRY(dtrace_fuword32_nocheck_nosmap)
	xorq	%rax, %rax
	movl	(%rdi), %eax
	ret
	END(dtrace_fuword32_nocheck_nosmap)

	ENTRY(dtrace_fuword32_nocheck_smap)
	stac
	xorq	%rax, %rax
	movl	(%rdi), %eax
	clac
	ret
	END(dtrace_fuword32_nocheck_smap)

/*
uint64_t
dtrace_fuword64_nocheck(void *addr)
*/
	ENTRY(dtrace_fuword64_nocheck_nosmap)
	movq	(%rdi), %rax
	ret
	END(dtrace_fuword64_nocheck_nosmap)

	ENTRY(dtrace_fuword64_nocheck_smap)
	stac
	movq	(%rdi), %rax
	clac
	ret
	END(dtrace_fuword64_nocheck_smap)

/*
void
dtrace_probe_error(dtrace_state_t *state, dtrace_epid_t epid, int which,
    int fault, int fltoffs, uintptr_t illval)
*/
	ENTRY(dtrace_probe_error)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$0x8, %rsp
	movq	%r9, (%rsp)
	movq	%r8, %r9
	movq	%rcx, %r8
	movq	%rdx, %rcx
	movq	%rsi, %rdx
	movq	%rdi, %rsi
	movl	dtrace_probeid_error(%rip), %edi
	call	dtrace_probe
	addq	$0x8, %rsp
	leave
	ret
	END(dtrace_probe_error)

/*
void
dtrace_membar_producer(void)
*/
	ENTRY(dtrace_membar_producer)
	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	END(dtrace_membar_producer)

/*
void
dtrace_membar_consumer(void)
*/
	ENTRY(dtrace_membar_consumer)
	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	END(dtrace_membar_consumer)

/*
dtrace_icookie_t
dtrace_interrupt_disable(void)
*/
	ENTRY(dtrace_interrupt_disable)
	pushfq
	popq	%rax
	cli
	ret
	END(dtrace_interrupt_disable)

/*
void
dtrace_interrupt_enable(dtrace_icookie_t cookie)
*/
	ENTRY(dtrace_interrupt_enable)
	pushq	%rdi
	popfq
	ret
	END(dtrace_interrupt_enable)
