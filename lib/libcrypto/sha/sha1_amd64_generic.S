/* $OpenBSD: sha1_amd64_generic.S,v 1.2 2025/01/18 02:56:07 jsing Exp $ */
/*
 * Copyright (c) 2024 Joel Sing <jsing@openbsd.org>
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

#define	hs0		%r8d
#define	hs1		%r9d
#define	hs2		%r10d
#define	hs3		%r11d
#define	hs4		%r12d

#define	tmp0		%eax
#define	tmp1		%ebx
#define	tmp2		%ecx
#define	tmp3		%edx

/*
 * Load message into wt, storing a copy in the message schedule:
 *
 *  Wt = Mt
 */
#define sha1_message_schedule_load(idx, m, w, wt) \
	movl	((idx&0xf)*4)(m), wt;				\
	bswapl	wt;						\
	movl	wt, ((idx&0xf)*4)(w);

/*
 * Update message schedule and return current value in wt:
 *
 *  W0 = rol(W13 ^ W8 ^ W2 ^ W0, 1)
 */
#define sha1_message_schedule_update(idx, w, wt) \
	movl	(((idx-3)&0xf)*4)(w), wt;	/* W13 */	\
	xorl	(((idx-8)&0xf)*4)(w), wt;	/* W8 */	\
	xorl	(((idx-14)&0xf)*4)(w), wt;	/* W2 */	\
	xorl	(((idx)&0xf)*4)(w), wt;		/* W0 */	\
	roll	$1, wt;						\
	\
	movl	wt, ((idx&0xf)*4)(w);

/*
 * Compute a SHA-1 round without logic function:
 *
 *  T = rol(a, 5) + e + Kt + Wt
 *
 * The caller is required to compute the appropriate logic function
 * (Ch, Maj, Parity) and add it to e.
 *
 * Upon completion b = rol(b, 30), e = T, pending rotation.
 */
#define sha1_round(a, b, c, d, e, kt, wt) \
	leal	kt(wt, e, 1), e;		/* Kt + Wt */	\
	\
	movl	a, tmp1;			/* rol(a, 5) */	\
	roll	$5, tmp1;					\
	addl	tmp1, e;					\
	\
	roll	$30, b;				/* rol(b, 30) */

/*
 * Compute a SHA-1 round with Ch:
 *
 *  T = rol(a, 5) + Ch(b, c, d) + e + Kt + Wt
 *
 *  Ch(x, y, z) = (x & y) ^ (~x & z) = ((y ^ z) & x) ^ z
 *
 * Upon completion b = rol(b, 30), e = T, pending rotation.
 */
#define sha1_round_ch(a, b, c, d, e, kt, wt) \
	movl	c, tmp2;			/* Ch */	\
	xorl	d, tmp2;			/* Ch */	\
	andl	b, tmp2;			/* Ch */	\
	xorl	d, tmp2;			/* Ch */	\
	addl	tmp2, e;			/* Ch */	\
	\
	sha1_round(a, b, c, d, e, kt, wt);

/*
 * Compute a SHA-1 round with Parity:
 *
 *  T = rol(a, 5) + Parity(b, c, d) + e + Kt + Wt
 *
 *  Parity(x, y, z) = x ^ y ^ z
 *
 * Upon completion b = rol(b, 30), e = T, pending rotation.
 */
#define sha1_round_parity(a, b, c, d, e, kt, wt) \
	movl	b, tmp2;			/* Parity */	\
	xorl	c, tmp2;			/* Parity */	\
	xorl	d, tmp2;			/* Parity */	\
	addl	tmp2, e;			/* Parity */	\
	\
	sha1_round(a, b, c, d, e, kt, wt);

/*
 * Compute a SHA-1 round with Maj:
 *
 *  T = rol(a, 5) + Maj(b, c, d) + e + Kt + Wt
 *
 *  Maj(x, y, z) = (x & y) ^ (x & z) ^ (y & z) = ((y ^ z) & x) ^ (y & z)
 *
 * Upon completion b = rol(b, 30), e = T, pending rotation.
 */
#define sha1_round_maj(a, b, c, d, e, kt, wt) \
	movl	c, tmp2;			/* Maj */	\
	xorl	d, tmp2;			/* Maj */	\
	andl	b, tmp2;			/* Maj */	\
	movl	c, tmp3;			/* Maj */	\
	andl	d, tmp3;			/* Maj */	\
	xorl	tmp2, tmp3;			/* Maj */	\
	addl	tmp3, e;			/* Maj */	\
	\
	sha1_round(a, b, c, d, e, kt, wt);

#define sha1_round1_load(idx, a, b, c, d, e) \
	sha1_message_schedule_load(idx, in, %rsp, tmp0) \
	sha1_round_ch(a, b, c, d, e, 0x5a827999, tmp0)

#define sha1_round1_update(idx, a, b, c, d, e) \
	sha1_message_schedule_update(idx, %rsp, tmp0) \
	sha1_round_ch(a, b, c, d, e, 0x5a827999, tmp0)

#define sha1_round2_update(idx, a, b, c, d, e) \
	sha1_message_schedule_update(idx, %rsp, tmp0) \
	sha1_round_parity(a, b, c, d, e, 0x6ed9eba1, tmp0)

#define sha1_round3_update(idx, a, b, c, d, e) \
	sha1_message_schedule_update(idx, %rsp, tmp0) \
	sha1_round_maj(a, b, c, d, e, 0x8f1bbcdc, tmp0)

#define sha1_round4_update(idx, a, b, c, d, e) \
	sha1_message_schedule_update(idx, %rsp, tmp0) \
	sha1_round_parity(a, b, c, d, e, 0xca62c1d6, tmp0)

.text

/*
 * void sha1_block_generic(SHA1_CTX *ctx, const void *in, size_t num);
 *
 * Standard x86-64 ABI: rdi = ctx, rsi = in, rdx = num
 */
.align 16
.globl	sha1_block_generic
.type	sha1_block_generic,@function
sha1_block_generic:
	_CET_ENDBR

	/* Save callee save registers. */
	pushq	%rbx
	pushq	%rbp
	pushq	%r12

	/* Allocate space for message schedule. */
	movq	%rsp, %rax
	subq	$(64+1*8), %rsp
	andq	$~63, %rsp
	movq	%rax, (64+0*8)(%rsp)

	/* Compute end of message. */
	shlq	$6, num
	leaq	(in, num, 1), end

	/* Load current hash state from context. */
	movl	(0*4)(ctx), hs0
	movl	(1*4)(ctx), hs1
	movl	(2*4)(ctx), hs2
	movl	(3*4)(ctx), hs3
	movl	(4*4)(ctx), hs4

	jmp	.Lblock_loop

.align 16
.Lblock_loop:

	/* Round 0 through 15. */
	sha1_round1_load(0, hs0, hs1, hs2, hs3, hs4)
	sha1_round1_load(1, hs4, hs0, hs1, hs2, hs3)
	sha1_round1_load(2, hs3, hs4, hs0, hs1, hs2)
	sha1_round1_load(3, hs2, hs3, hs4, hs0, hs1)
	sha1_round1_load(4, hs1, hs2, hs3, hs4, hs0)
	sha1_round1_load(5, hs0, hs1, hs2, hs3, hs4)
	sha1_round1_load(6, hs4, hs0, hs1, hs2, hs3)
	sha1_round1_load(7, hs3, hs4, hs0, hs1, hs2)
	sha1_round1_load(8, hs2, hs3, hs4, hs0, hs1)
	sha1_round1_load(9, hs1, hs2, hs3, hs4, hs0)
	sha1_round1_load(10, hs0, hs1, hs2, hs3, hs4)
	sha1_round1_load(11, hs4, hs0, hs1, hs2, hs3)
	sha1_round1_load(12, hs3, hs4, hs0, hs1, hs2)
	sha1_round1_load(13, hs2, hs3, hs4, hs0, hs1)
	sha1_round1_load(14, hs1, hs2, hs3, hs4, hs0)
	sha1_round1_load(15, hs0, hs1, hs2, hs3, hs4)

	/* Round 16 through 31. */
	sha1_round1_update(16, hs4, hs0, hs1, hs2, hs3);
	sha1_round1_update(17, hs3, hs4, hs0, hs1, hs2);
	sha1_round1_update(18, hs2, hs3, hs4, hs0, hs1);
	sha1_round1_update(19, hs1, hs2, hs3, hs4, hs0);
	sha1_round2_update(20, hs0, hs1, hs2, hs3, hs4);
	sha1_round2_update(21, hs4, hs0, hs1, hs2, hs3);
	sha1_round2_update(22, hs3, hs4, hs0, hs1, hs2);
	sha1_round2_update(23, hs2, hs3, hs4, hs0, hs1);
	sha1_round2_update(24, hs1, hs2, hs3, hs4, hs0);
	sha1_round2_update(25, hs0, hs1, hs2, hs3, hs4);
	sha1_round2_update(26, hs4, hs0, hs1, hs2, hs3);
	sha1_round2_update(27, hs3, hs4, hs0, hs1, hs2);
	sha1_round2_update(28, hs2, hs3, hs4, hs0, hs1);
	sha1_round2_update(29, hs1, hs2, hs3, hs4, hs0);
	sha1_round2_update(30, hs0, hs1, hs2, hs3, hs4);
	sha1_round2_update(31, hs4, hs0, hs1, hs2, hs3);

	/* Round 32 through 47. */
	sha1_round2_update(32, hs3, hs4, hs0, hs1, hs2);
	sha1_round2_update(33, hs2, hs3, hs4, hs0, hs1);
	sha1_round2_update(34, hs1, hs2, hs3, hs4, hs0);
	sha1_round2_update(35, hs0, hs1, hs2, hs3, hs4);
	sha1_round2_update(36, hs4, hs0, hs1, hs2, hs3);
	sha1_round2_update(37, hs3, hs4, hs0, hs1, hs2);
	sha1_round2_update(38, hs2, hs3, hs4, hs0, hs1);
	sha1_round2_update(39, hs1, hs2, hs3, hs4, hs0);
	sha1_round3_update(40, hs0, hs1, hs2, hs3, hs4);
	sha1_round3_update(41, hs4, hs0, hs1, hs2, hs3);
	sha1_round3_update(42, hs3, hs4, hs0, hs1, hs2);
	sha1_round3_update(43, hs2, hs3, hs4, hs0, hs1);
	sha1_round3_update(44, hs1, hs2, hs3, hs4, hs0);
	sha1_round3_update(45, hs0, hs1, hs2, hs3, hs4);
	sha1_round3_update(46, hs4, hs0, hs1, hs2, hs3);
	sha1_round3_update(47, hs3, hs4, hs0, hs1, hs2);

	/* Round 48 through 63. */
	sha1_round3_update(48, hs2, hs3, hs4, hs0, hs1);
	sha1_round3_update(49, hs1, hs2, hs3, hs4, hs0);
	sha1_round3_update(50, hs0, hs1, hs2, hs3, hs4);
	sha1_round3_update(51, hs4, hs0, hs1, hs2, hs3);
	sha1_round3_update(52, hs3, hs4, hs0, hs1, hs2);
	sha1_round3_update(53, hs2, hs3, hs4, hs0, hs1);
	sha1_round3_update(54, hs1, hs2, hs3, hs4, hs0);
	sha1_round3_update(55, hs0, hs1, hs2, hs3, hs4);
	sha1_round3_update(56, hs4, hs0, hs1, hs2, hs3);
	sha1_round3_update(57, hs3, hs4, hs0, hs1, hs2);
	sha1_round3_update(58, hs2, hs3, hs4, hs0, hs1);
	sha1_round3_update(59, hs1, hs2, hs3, hs4, hs0);
	sha1_round4_update(60, hs0, hs1, hs2, hs3, hs4);
	sha1_round4_update(61, hs4, hs0, hs1, hs2, hs3);
	sha1_round4_update(62, hs3, hs4, hs0, hs1, hs2);
	sha1_round4_update(63, hs2, hs3, hs4, hs0, hs1);

	/* Round 64 through 79. */
	sha1_round4_update(64, hs1, hs2, hs3, hs4, hs0);
	sha1_round4_update(65, hs0, hs1, hs2, hs3, hs4);
	sha1_round4_update(66, hs4, hs0, hs1, hs2, hs3);
	sha1_round4_update(67, hs3, hs4, hs0, hs1, hs2);
	sha1_round4_update(68, hs2, hs3, hs4, hs0, hs1);
	sha1_round4_update(69, hs1, hs2, hs3, hs4, hs0);
	sha1_round4_update(70, hs0, hs1, hs2, hs3, hs4);
	sha1_round4_update(71, hs4, hs0, hs1, hs2, hs3);
	sha1_round4_update(72, hs3, hs4, hs0, hs1, hs2);
	sha1_round4_update(73, hs2, hs3, hs4, hs0, hs1);
	sha1_round4_update(74, hs1, hs2, hs3, hs4, hs0);
	sha1_round4_update(75, hs0, hs1, hs2, hs3, hs4);
	sha1_round4_update(76, hs4, hs0, hs1, hs2, hs3);
	sha1_round4_update(77, hs3, hs4, hs0, hs1, hs2);
	sha1_round4_update(78, hs2, hs3, hs4, hs0, hs1);
	sha1_round4_update(79, hs1, hs2, hs3, hs4, hs0);

	/* Add intermediate state to hash state. */
	addl	(0*4)(ctx), hs0
	addl	(1*4)(ctx), hs1
	addl	(2*4)(ctx), hs2
	addl	(3*4)(ctx), hs3
	addl	(4*4)(ctx), hs4

	/* Store new hash state to context. */
	movl	hs0, (0*4)(ctx)
	movl	hs1, (1*4)(ctx)
	movl	hs2, (2*4)(ctx)
	movl	hs3, (3*4)(ctx)
	movl	hs4, (4*4)(ctx)

	addq	$64, in
	cmpq	end, in
	jb	.Lblock_loop

	movq	(64+0*8)(%rsp), %rsp

	/* Restore callee save registers. */
	popq	%r12
	popq	%rbp
	popq	%rbx

	ret
