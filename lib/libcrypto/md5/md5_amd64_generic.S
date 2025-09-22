/*	$OpenBSD: md5_amd64_generic.S,v 1.1 2025/01/24 13:35:04 jsing Exp $ */
/*
 * Copyright (c) 2025 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef __CET__
#include <cet.h>
#else
#define _CET_ENDBR
#endif

#define	ctx		%rdi
#define	in		%rsi
#define	num		%rdx

#define	end		%rbp

#define	A		%eax
#define	B		%ebx
#define	C		%ecx
#define	D		%edx

#define	AA		%r8d
#define	BB		%r9d
#define	CC		%r10d
#define	DD		%r11d

#define	tmp0		%r12d
#define	tmp1		%r13d

/*
 * Compute MD5 round 1 as:
 *
 *   a = b + rol(a + F(b, c, d) + x + t, s)
 *   F(x, y, z) = (x & y) | (~x & z)
 *              = ((y ^ z) & x) ^ z
 */
#define md5_round1(a, b, c, d, x, t, s) \
	addl	(x*4)(in), a;					\
	movl	c, tmp0;					\
	xorl	d, tmp0;					\
	andl	b, tmp0;					\
	xorl	d, tmp0;					\
	leal	t(tmp0, a), a;					\
	roll	$s, a;						\
	addl	b, a;

/*
 * Compute MD5 round 2 as:
 *
 *   a = b + rol(a + G(b, c, d) + x + t, s)
 *   G(x, y, z) = (x & z) | (y & ~z)
 */
#define md5_round2(a, b, c, d, x, t, s) \
	addl	(x*4)(in), a;					\
	movl	d, tmp0;					\
	xorl	$-1, tmp0;					\
	andl	c, tmp0;					\
	addl	tmp0, a;					\
	movl	d, tmp1;					\
	andl	b, tmp1;					\
	leal	t(tmp1, a), a;					\
	roll	$s, a;						\
	addl	b, a;

/*
 * Compute MD5 round 3 as:
 *
 *   a = b + rol(a + H(b, c, d) + x + t, s)
 *   H(x, y, z) = x ^ y ^ z;
 */
#define md5_round3(a, b, c, d, x, t, s) \
	addl	(x*4)(in), a;					\
	movl	d, tmp0;					\
	xorl	c, tmp0;					\
	xorl	b, tmp0;					\
	leal    t(tmp0, a), a;					\
	roll	$s, a;						\
	addl	b, a;

/*
 * Compute MD5 round 4 as:
 *
 *   a = b + rol(a + I(b, c, d) + x + t, s)
 *   I(x, y, z) = y ^ (x | ~z)
 */
#define md5_round4(a, b, c, d, x, t, s) \
	addl	(x*4)(in), a;					\
	movl	d, tmp0;					\
	xorl	$-1, tmp0;					\
	orl	b, tmp0;					\
	xorl	c, tmp0;					\
	leal    t(tmp0, a), a;					\
	roll	$s, a;						\
	addl	b, a;

.text

/*
 * void md5_block_data_order(MD5_CTX *ctx, const void *in, size_t num);
 *
 * Standard x86-64 ABI: rdi = ctx, rsi = in, rdx = num
 */
.align 16
.globl	md5_block_data_order
.type	md5_block_data_order,@function
md5_block_data_order:
	_CET_ENDBR

	/* Save callee save registers. */
	pushq	%rbx
	pushq	%rbp
	pushq	%r12
	pushq	%r13

	/* Compute end of message. */
	shlq	$6, num
	leaq	(in, num, 1), end

	/* Load current hash state from context. */
	movl	(0*4)(ctx), AA
	movl	(1*4)(ctx), BB
	movl	(2*4)(ctx), CC
	movl	(3*4)(ctx), DD

	jmp	.Lblock_loop

.align 16
.Lblock_loop:
	movl	AA, A
	movl	BB, B
	movl	CC, C
	movl	DD, D

	md5_round1(A, B, C, D, 0, 0xd76aa478L, 7);
	md5_round1(D, A, B, C, 1, 0xe8c7b756L, 12);
	md5_round1(C, D, A, B, 2, 0x242070dbL, 17);
	md5_round1(B, C, D, A, 3, 0xc1bdceeeL, 22);
	md5_round1(A, B, C, D, 4, 0xf57c0fafL, 7);
	md5_round1(D, A, B, C, 5, 0x4787c62aL, 12);
	md5_round1(C, D, A, B, 6, 0xa8304613L, 17);
	md5_round1(B, C, D, A, 7, 0xfd469501L, 22);
	md5_round1(A, B, C, D, 8, 0x698098d8L, 7);
	md5_round1(D, A, B, C, 9, 0x8b44f7afL, 12);
	md5_round1(C, D, A, B, 10, 0xffff5bb1L, 17);
	md5_round1(B, C, D, A, 11, 0x895cd7beL, 22);
	md5_round1(A, B, C, D, 12, 0x6b901122L, 7);
	md5_round1(D, A, B, C, 13, 0xfd987193L, 12);
	md5_round1(C, D, A, B, 14, 0xa679438eL, 17);
	md5_round1(B, C, D, A, 15, 0x49b40821L, 22);

	md5_round2(A, B, C, D, 1, 0xf61e2562L, 5);
	md5_round2(D, A, B, C, 6, 0xc040b340L, 9);
	md5_round2(C, D, A, B, 11, 0x265e5a51L, 14);
	md5_round2(B, C, D, A, 0, 0xe9b6c7aaL, 20);
	md5_round2(A, B, C, D, 5, 0xd62f105dL, 5);
	md5_round2(D, A, B, C, 10, 0x02441453L, 9);
	md5_round2(C, D, A, B, 15, 0xd8a1e681L, 14);
	md5_round2(B, C, D, A, 4, 0xe7d3fbc8L, 20);
	md5_round2(A, B, C, D, 9, 0x21e1cde6L, 5);
	md5_round2(D, A, B, C, 14, 0xc33707d6L, 9);
	md5_round2(C, D, A, B, 3, 0xf4d50d87L, 14);
	md5_round2(B, C, D, A, 8, 0x455a14edL, 20);
	md5_round2(A, B, C, D, 13, 0xa9e3e905L, 5);
	md5_round2(D, A, B, C, 2, 0xfcefa3f8L, 9);
	md5_round2(C, D, A, B, 7, 0x676f02d9L, 14);
	md5_round2(B, C, D, A, 12, 0x8d2a4c8aL, 20);

	md5_round3(A, B, C, D, 5, 0xfffa3942L, 4);
	md5_round3(D, A, B, C, 8, 0x8771f681L, 11);
	md5_round3(C, D, A, B, 11, 0x6d9d6122L, 16);
	md5_round3(B, C, D, A, 14, 0xfde5380cL, 23);
	md5_round3(A, B, C, D, 1, 0xa4beea44L, 4);
	md5_round3(D, A, B, C, 4, 0x4bdecfa9L, 11);
	md5_round3(C, D, A, B, 7, 0xf6bb4b60L, 16);
	md5_round3(B, C, D, A, 10, 0xbebfbc70L, 23);
	md5_round3(A, B, C, D, 13, 0x289b7ec6L, 4);
	md5_round3(D, A, B, C, 0, 0xeaa127faL, 11);
	md5_round3(C, D, A, B, 3, 0xd4ef3085L, 16);
	md5_round3(B, C, D, A, 6, 0x04881d05L, 23);
	md5_round3(A, B, C, D, 9, 0xd9d4d039L, 4);
	md5_round3(D, A, B, C, 12, 0xe6db99e5L, 11);
	md5_round3(C, D, A, B, 15, 0x1fa27cf8L, 16);
	md5_round3(B, C, D, A, 2, 0xc4ac5665L, 23);

	md5_round4(A, B, C, D, 0, 0xf4292244L, 6);
	md5_round4(D, A, B, C, 7, 0x432aff97L, 10);
	md5_round4(C, D, A, B, 14, 0xab9423a7L, 15);
	md5_round4(B, C, D, A, 5, 0xfc93a039L, 21);
	md5_round4(A, B, C, D, 12, 0x655b59c3L, 6);
	md5_round4(D, A, B, C, 3, 0x8f0ccc92L, 10);
	md5_round4(C, D, A, B, 10, 0xffeff47dL, 15);
	md5_round4(B, C, D, A, 1, 0x85845dd1L, 21);
	md5_round4(A, B, C, D, 8, 0x6fa87e4fL, 6);
	md5_round4(D, A, B, C, 15, 0xfe2ce6e0L, 10);
	md5_round4(C, D, A, B, 6, 0xa3014314L, 15);
	md5_round4(B, C, D, A, 13, 0x4e0811a1L, 21);
	md5_round4(A, B, C, D, 4, 0xf7537e82L, 6);
	md5_round4(D, A, B, C, 11, 0xbd3af235L, 10);
	md5_round4(C, D, A, B, 2, 0x2ad7d2bbL, 15);
	md5_round4(B, C, D, A, 9, 0xeb86d391L, 21);

	/* Add intermediate state to hash state. */
	addl	A, AA
	addl	B, BB
	addl	C, CC
	addl	D, DD

	addq	$64, in
	cmpq	end, in
	jb	.Lblock_loop

	/* Store new hash state to context. */
	movl	AA, (0*4)(ctx)
	movl	BB, (1*4)(ctx)
	movl	CC, (2*4)(ctx)
	movl	DD, (3*4)(ctx)

	/* Restore callee save registers. */
	popq	%r13
	popq	%r12
	popq	%rbp
	popq	%rbx

	ret
