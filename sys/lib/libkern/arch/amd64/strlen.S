/*	$OpenBSD: strlen.S,v 1.10 2022/12/07 19:26:39 guenther Exp $	*/
/*	$NetBSD: strlen.S,v 1.6 2014/03/22 19:16:34 jakllsch Exp $	*/

/*-
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David Laight.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Inspired by a version written by J.T. Conklin <jtc@acorntoolworks.com>
 * (Only the long comment really remains his work!)
 */

#include <machine/asm.h>

/*
 * There are many well known branch-free sequences which are used
 * for determining whether a zero-byte is contained within a word.
 * These sequences are generally much more efficient than loading
 * and comparing each byte individually.
 *
 * The expression [1,2]:
 *
 * (1)  ~(((x & 0x7f....7f) + 0x7f....7f) | (x | 0x7f....7f))
 *
 * evaluates to a non-zero value if any of the bytes in the
 * original word is zero.
 *
 * It also has the useful property that bytes in the result word
 * that correspond to non-zero bytes in the original word have
 * the value 0x00, while bytes corresponding to zero bytes have
 * the value 0x80. This allows calculation of the first (and
 * last) occurrence of a zero byte within the word (useful for C's
 * str* primitives) by counting the number of leading (or
 * trailing) zeros and dividing the result by 8.  On machines
 * without (or with slow) clz() / ctz() instructions, testing
 * each byte in the result word for zero is necessary.
 *
 * This typically takes 4 instructions (5 on machines without
 * "not-or") not including those needed to load the constant.
 *
 *
 * The expression:
 *
 * (2)  ((x - 0x01....01) & 0x80....80 & ~x)
 *
 * evaluates to a non-zero value if any of the bytes in the
 * original word is zero.
 *
 * On little endian machines, the first byte in the result word
 * that corresponds to a zero byte in the original byte is 0x80,
 * so clz() can be used as above.  On big endian machines, and
 * little endian machines without (or with a slow) clz() insn,
 * testing each byte in the original for zero is necessary.
 *
 * This typically takes 3 instructions (4 on machines without
 * "and with complement") not including those needed to load
 * constants.
 *
 *
 * The expression:
 *
 * (3)  ((x - 0x01....01) & 0x80....80)
 *
 * always evaluates to a non-zero value if any of the bytes in
 * the original word is zero or has the top bit set.
 * For strings that are likely to only contain 7-bit ascii these
 * false positives will be rare.
 *
 * To account for possible false positives, each byte of the
 * original word must be checked when the expression evaluates to
 * a non-zero value.  However, because it is simpler than those
 * presented above, code that uses it will be faster as long as
 * the rate of false positives is low.
 *
 * This is likely, because the false positive can only occur
 * if the most significant bit of a byte within the word is set.
 * The expression will never fail for typical 7-bit ASCII strings.
 *
 * This typically takes 2 instructions not including those needed
 * to load constants.
 *
 *
 * [1] Henry S. Warren Jr., "Hacker's Delight", Addison-Westley 2003
 *
 * [2] International Business Machines, "The PowerPC Compiler Writer's
 *     Guide", Warthman Associates, 1996
 */

ENTRY(strlen)
	RETGUARD_SETUP(strlen, r10)
	movabsq	$0x0101010101010101,%r8

	test	$7,%dil
	movq	%rdi,%rax		/* Buffer, %rdi unchanged */
	movabsq	$0x8080808080808080,%r9
	jnz	10f			/* Jump if misaligned */

	_ALIGN_TEXT
1:
	movq	(%rax),%rdx		/* get bytes to check */
2:
	addq	$8,%rax
	mov	%rdx,%rcx		/* save for later check */
	subq	%r8,%rdx		/* alg (3) above first */
	not	%rcx			/* Invert of data */
	andq	%r9,%rdx
	je	1b			/* jump if all 0x01-0x80 */

	/* Do check from alg (2) above - loops for 0x81..0xff bytes */
	andq	%rcx,%rdx
	je	1b

	/* Since we are LE, use bit scan for first 0x80 byte */
	sub	%rdi,%rax		/* length to next word */
	bsf	%rdx,%rdx		/* 7, 15, 23 ... 63 */
	shr	$3,%rdx			/* 0, 1, 2 ... 7 */
	lea	-8(%rax,%rdx),%rax
	RETGUARD_CHECK(strlen, r10)
	ret
	lfence

/* Misaligned, read aligned word and make low bytes non-zero */
	_ALIGN_TRAPS
10:
	mov	%al,%cl
	mov	$1,%rsi
	and	$7,%cl			/* offset into word 1..7 */
	and	$~7,%al			/* start of word with buffer */
	shl	$3,%cl			/* bit count 8, 16 .. 56 */
	movq	(%rax),%rdx		/* first data in high bytes */
	shl	%cl,%rsi
	dec	%rsi
	or	%rsi,%rdx		/* low bytes now non-zero */
	jmp	2b
END(strlen)
