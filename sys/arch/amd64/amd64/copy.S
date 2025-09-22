/*	$OpenBSD: copy.S,v 1.21 2025/09/05 13:33:49 cludwig Exp $	*/
/*	$NetBSD: copy.S,v 1.1 2003/04/26 18:39:26 fvdl Exp $	*/

/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "assym.h"

#include <sys/errno.h>
#include <sys/syscall.h>

#include <machine/asm.h>
#include <machine/codepatch.h>

#define	DECLARE_ONFAULT(x)		\
	.pushsection .nofault.0, "a"	;\
	.quad	x			;\
	.popsection

/*
 * Copy routines from and to userland, plus a few more. See the
 * section 9 manpages for info. Some cases can be optimized more.
 *
 * I wonder if it's worthwhile to make these use SSE2 registers.
 */

ENTRY(kcopy)
	RETGUARD_SETUP(kcopy, r10)
	movq	CPUVAR(CURPCB),%rax
	pushq	PCB_ONFAULT(%rax)
	leaq	copy_fault(%rip),%r11
	movq	%r11, PCB_ONFAULT(%rax)

	xchgq	%rdi,%rsi
	movq	%rdx,%rcx

	movq	%rdi,%rax
	subq	%rsi,%rax
	cmpq	%rcx,%rax		# overlapping?
	jb	1f
	shrq	$3,%rcx			# nope, copy forward by 64-bit words
	rep
	movsq

	movq	%rdx,%rcx
	andl	$7,%ecx			# any bytes left?
	rep
	movsb

	movq	CPUVAR(CURPCB),%rdx
	popq	PCB_ONFAULT(%rdx)
	xorq	%rax,%rax
	RETGUARD_CHECK(kcopy, r10)
	ret
	lfence

1:	addq	%rcx,%rdi		# copy backward
	addq	%rcx,%rsi
	std
	andq	$7,%rcx			# any fractional bytes?
	decq	%rdi
	decq	%rsi
	rep
	movsb
	movq	%rdx,%rcx		# copy remainder by 64-bit words
	shrq	$3,%rcx
	subq	$7,%rsi
	subq	$7,%rdi
	rep
	movsq
	cld

	movq	CPUVAR(CURPCB),%rdx
	popq	PCB_ONFAULT(%rdx)
	xorq	%rax,%rax
	RETGUARD_CHECK(kcopy, r10)
	ret
	lfence

ENTRY(copyout)
	RETGUARD_SETUP(kcopy, r10)
	movq	CPUVAR(CURPCB),%rax
	pushq	PCB_ONFAULT(%rax)

	xchgq	%rdi,%rsi
	movq	%rdx,%rax

	movq	%rdi,%rdx
	addq	%rax,%rdx
	jc	copy_fault
	movq	$VM_MAXUSER_ADDRESS,%r8
	cmpq	%r8,%rdx
	ja	copy_fault

	movq	CPUVAR(CURPCB),%rdx
	leaq	copy_fault(%rip),%r11
	movq	%r11,PCB_ONFAULT(%rdx)
	SMAP_STAC
	movq	%rax,%rcx
	shrq	$3,%rcx
	rep
	movsq
	movb	%al,%cl
	shrb	$2,%cl
	andb	$1,%cl
	rep
	movsl
	movb	%al,%cl
	andb	$3,%cl
	rep
	movsb
	SMAP_CLAC
	popq	PCB_ONFAULT(%rdx)
	xorl	%eax,%eax
	RETGUARD_CHECK(kcopy, r10)
	ret
	lfence

ENTRY(_copyin)
	RETGUARD_SETUP(kcopy, r10)
	movq	CPUVAR(CURPCB),%rax
	pushq	PCB_ONFAULT(%rax)
	leaq	copy_fault(%rip),%r11
	movq	%r11,PCB_ONFAULT(%rax)
	SMAP_STAC
	xchgq	%rdi,%rsi
	movq	%rdx,%rax

	movq	%rsi,%rdx
	addq	%rax,%rdx
	jc	copy_fault
	movq	$VM_MAXUSER_ADDRESS,%r8
	cmpq	%r8,%rdx
	ja	copy_fault

3:	/* bcopy(%rsi, %rdi, %rax); */
	movq	%rax,%rcx
	shrq	$3,%rcx
	rep
	movsq
	movb	%al,%cl
	shrb	$2,%cl
	andb	$1,%cl
	rep
	movsl
	movb	%al,%cl
	andb	$3,%cl
	rep
	movsb

	SMAP_CLAC
	movq	CPUVAR(CURPCB),%rdx
	popq	PCB_ONFAULT(%rdx)
	xorl	%eax,%eax
	RETGUARD_CHECK(kcopy, r10)
	ret
	lfence

NENTRY(copy_fault)
DECLARE_ONFAULT(copy_fault)
	cld
	SMAP_CLAC
	movq	CPUVAR(CURPCB),%rdx
	popq	PCB_ONFAULT(%rdx)
	movl	$EFAULT,%eax
	RETGUARD_CHECK(kcopy, r10)
	ret
	lfence

ENTRY(copyoutstr)
	RETGUARD_SETUP(copyoutstr, r10)
	xchgq	%rdi,%rsi
	movq	%rdx,%r8
	movq	%rcx,%r9

	movq	CPUVAR(CURPCB),%rax
	pushq	PCB_ONFAULT(%rax)
	leaq	copystr_fault(%rip),%r11
	movq	%r11,PCB_ONFAULT(%rax)
	SMAP_STAC
	/*
	 * Get min(%rdx, VM_MAXUSER_ADDRESS-%rdi).
	 */
	movq	$VM_MAXUSER_ADDRESS,%rax
	subq	%rdi,%rax
	jbe	copystr_fault			/* die if CF == 1 || ZF == 1 */
	cmpq	%rdx,%rax
	jae	1f
	movq	%rax,%rdx
	movq	%rax,%r8

1:	incq	%rdx

1:	decq	%rdx
	jz	2f
	lodsb
	stosb
	testb	%al,%al
	jnz	1b

	/* Success -- 0 byte reached. */
	decq	%rdx
	xorq	%rax,%rax
	jmp	copystr_return

2:	/* rdx is zero -- return EFAULT or ENAMETOOLONG. */
	movq	$VM_MAXUSER_ADDRESS,%r11
	cmpq	%r11,%rdi
	jae	copystr_fault
	movl	$ENAMETOOLONG,%eax
	jmp	copystr_return

ENTRY(_copyinstr)
	RETGUARD_SETUP(copyoutstr, r10)
	xchgq	%rdi,%rsi
	movq	%rdx,%r8
	movq	%rcx,%r9

	movq	CPUVAR(CURPCB),%rcx
	pushq	PCB_ONFAULT(%rcx)
	leaq	copystr_fault(%rip),%r11
	movq	%r11,PCB_ONFAULT(%rcx)
	SMAP_STAC

	/*
	 * Get min(%rdx, VM_MAXUSER_ADDRESS-%rsi).
	 */
	movq	$VM_MAXUSER_ADDRESS,%rax
	subq	%rsi,%rax
	jbe	copystr_fault			/* die if CF == 1 || ZF == 1 */
	cmpq	%rdx,%rax
	jae	1f
	movq	%rax,%rdx
	movq	%rax,%r8

1:	incq	%rdx

1:	decq	%rdx
	jz	2f
	lodsb
	stosb
	testb	%al,%al
	jnz	1b

	/* Success -- 0 byte reached. */
	decq	%rdx
	xorq	%rax,%rax
	jmp	copystr_return

2:	/* edx is zero -- return EFAULT or ENAMETOOLONG. */
	movq	$VM_MAXUSER_ADDRESS,%r11
	cmpq	%r11,%rsi
	jae	copystr_fault
	movl	$ENAMETOOLONG,%eax
	jmp	copystr_return

ENTRY(copystr_fault)
DECLARE_ONFAULT(copystr_fault)
	movl	$EFAULT,%eax
copystr_return:
	SMAP_CLAC
	/* Set *lencopied and return %eax. */
	movq	CPUVAR(CURPCB),%rcx
	popq	PCB_ONFAULT(%rcx)
	testq	%r9,%r9
	jz	8f
	subq	%rdx,%r8
	movq	%r8,(%r9)
8:
	RETGUARD_CHECK(copyoutstr, r10)
	ret
	lfence

CODEPATCH_CODE(_stac,	stac)
CODEPATCH_CODE(_clac,	clac)
