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
 */

/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

	.file	"atomic.s"

#define	_ASM
#include <sys/asm_linkage.h>

	ENTRY(atomic_add_64_nv)
	mov	%rsi, %rax		// %rax = delta addend
	lock
	  xaddq	%rsi, (%rdi)		// %rsi = old value, (%rdi) = sum
	addq	%rsi, %rax		// new value = original value + delta
	ret
	SET_SIZE(atomic_add_64_nv)

	ENTRY(atomic_or_8_nv)
	movb	(%rdi), %al	// %al = old value
1:
	movb	%sil, %cl
	orb	%al, %cl	// %cl = new value
	lock
	cmpxchgb %cl, (%rdi)	// try to stick it in
	jne	1b
	movzbl	%cl, %eax	// return new value
	ret
	SET_SIZE(atomic_or_8_nv)

	ENTRY(atomic_cas_32)
	movl	%esi, %eax
	lock
	cmpxchgl %edx, (%rdi)
	ret
	SET_SIZE(atomic_cas_32)

	ENTRY(atomic_cas_64)
	movq	%rsi, %rax
	lock
	cmpxchgq %rdx, (%rdi)
	ret
	SET_SIZE(atomic_cas_64)

	ENTRY(membar_producer)
	sfence
	ret
	SET_SIZE(membar_producer)
