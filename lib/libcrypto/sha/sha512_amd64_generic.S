/* $OpenBSD: sha512_amd64_generic.S,v 1.1 2024/11/16 14:56:39 jsing Exp $ */
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

#define	round		%rdi

#define	hs0		%r8
#define	hs1		%r9
#define	hs2		%r10
#define	hs3		%r11
#define	hs4		%r12
#define	hs5		%r13
#define	hs6		%r14
#define	hs7		%r15

#define	k512		%rbp

#define	tmp0		%rax
#define	tmp1		%rbx
#define	tmp2		%rcx
#define	tmp3		%rdx

/*
 * Load message into wt, storing a copy in the message schedule:
 *
 *  Wt = Mt
 */
#define sha512_message_schedule_load(idx, m, w, wt) \
	movq	(m, round, 8), wt;				\
	bswapq	wt;						\
	movq	wt, ((idx&0xf)*8)(w);

/*
 * Update message schedule and return current value in wt:
 *
 *  Wt = sigma1(W(t-2)) + W(t-7) + sigma0(W(t-15)) + W(t-16)
 *
 *  sigma0(x) = ror(x, 1) ^ ror(x, 8) ^ (x >> 7)
 *  sigma1(x) = ror(x, 19) ^ ror(x, 61) ^ (x >> 6)
 *
 */
#define sha512_message_schedule_update(idx, w, wt) \
	movq	(((idx-2)&0xf)*8)(w), wt;	/* sigma1 */	\
	movq	wt, tmp1;			/* sigma1 */	\
	rorq	$(61-19), tmp1;			/* sigma1 */	\
	xorq	wt, tmp1;			/* sigma1 */	\
	rorq	$19, tmp1;			/* sigma1 */	\
	shrq	$6, wt;				/* sigma1 */	\
	xorq	tmp1, wt;			/* sigma1 */	\
	\
	addq	(((idx-7)&0xf)*8)(w), wt;	/* Wt-7 */	\
	addq	(((idx-16)&0xf)*8)(w), wt;	/* Wt-16 */	\
	\
	movq	(((idx-15)&0xf)*8)(w), tmp2;	/* sigma0 */	\
	movq	tmp2, tmp3;			/* sigma0 */	\
	rorq	$(8-1), tmp2;			/* sigma0 */	\
	xorq	tmp3, tmp2;			/* sigma0 */	\
	rorq	$1, tmp2;			/* sigma0 */	\
	shrq	$7, tmp3;			/* sigma0 */	\
	xorq	tmp3, tmp2;			/* sigma0 */	\
	addq	tmp2, wt;			/* sigma0 */	\
	\
	movq	wt, ((idx&0xf)*8)(w);

/*
 * Compute a SHA-512 round:
 *
 *  T1 = h + Sigma1(e) + Ch(e, f, g) + Kt + Wt
 *  T2 = Sigma0(a) + Maj(a, b, c)
 *
 *  Sigma0(x) = ror(x, 28) ^ ror(x, 34) ^ ror(x, 39)
 *  Sigma1(x) = ror(x, 14) ^ ror(x, 18) ^ ror(x, 41)
 *  Ch(x, y, z) = (x & y) ^ (~x & z) = ((y ^ z) & x) ^ z
 *  Maj(x, y, z) = (x & y) ^ (x & z) ^ (y & z) = ((y ^ z) & x) ^ (y & z)
 *
 * Upon completion d = d + T1, h = T1 + T2, pending rotation.
 */
#define sha512_round(idx, a, b, c, d, e, f, g, h, k, w, wt) \
	addq	wt, h;				/* T1 Wt */	\
	addq	(k512, round, 8), h;		/* T1 Kt */	\
	\
	movq	e, tmp1;			/* T1 Sigma1 */	\
	rorq	$(41-18), tmp1;			/* T1 Sigma1 */	\
	xorq	e, tmp1;			/* T1 Sigma1 */ \
	rorq	$(18-14), tmp1;			/* T1 Sigma1 */	\
	xorq	e, tmp1;			/* T1 Sigma1 */ \
	rorq	$14, tmp1;			/* T1 Sigma1 */	\
	addq	tmp1, h;			/* T1 Sigma1 */	\
	\
	movq	f, tmp2;			/* T1 Ch */	\
	xorq	g, tmp2;			/* T1 Ch */	\
	andq	e, tmp2;			/* T1 Ch */	\
	xorq	g, tmp2;			/* T1 Ch */	\
	addq	tmp2, h;			/* T1 Ch */	\
	\
	addq	h, d;				/* d += T1 */	\
	\
	movq	a, tmp1;			/* T2 Sigma0 */	\
	rorq	$(39-34), tmp1;			/* T2 Sigma0 */	\
	xorq	a, tmp1;			/* T2 Sigma0 */	\
	rorq	$(34-28), tmp1;			/* T2 Sigma0 */	\
	xorq	a, tmp1;			/* T2 Sigma0 */	\
	rorq	$28, tmp1;			/* T2 Sigma0 */	\
	addq	tmp1, h;			/* T2 Sigma0 */	\
	\
	movq	b, tmp2;			/* T2 Maj */	\
	xorq	c, tmp2;			/* T2 Maj */	\
	andq	a, tmp2;			/* T2 Maj */	\
	movq	b, tmp3;			/* T2 Maj */	\
	andq	c, tmp3;			/* T2 Maj */	\
	xorq	tmp2, tmp3;			/* T2 Maj */	\
	addq	tmp3, h;			/* T2 Maj */	\
	\
	addq	$1, round;

#define sha512_round_load(idx, a, b, c, d, e, f, g, h) \
	sha512_message_schedule_load(idx, in, %rsp, tmp0) \
	sha512_round(idx, a, b, c, d, e, f, g, h, k512, %rsp, tmp0)

#define sha512_round_update(idx, a, b, c, d, e, f, g, h) \
	sha512_message_schedule_update(idx, %rsp, tmp0) \
	sha512_round(idx, a, b, c, d, e, f, g, h, k512, %rsp, tmp0)

.text

/*
 * void sha512_block_generic(SHA512_CTX *ctx, const void *in, size_t num);
 *
 * Standard x86-64 ABI: rdi = ctx, rsi = in, rdx = num
 */
.align 16
.globl	sha512_block_generic
.type	sha512_block_generic,@function
sha512_block_generic:
	_CET_ENDBR

	/* Save callee save registers. */
	pushq	%rbx
	pushq	%rbp
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	/* Allocate space for message schedule and context pointer. */
	movq	%rsp, %rax
	subq	$(128+3*8), %rsp
	andq	$~63, %rsp
	movq	%rax, (128+2*8)(%rsp)
	movq	ctx, (128+1*8)(%rsp)

	/* Compute and store end of message. */
	shlq	$7, num
	leaq	(in, num, 1), %rbx
	movq	%rbx, (128+0*8)(%rsp)

	/* Address of SHA-512 constants. */
	leaq	K512(%rip), k512

	/* Load current hash state from context. */
	movq	(0*8)(ctx), hs0
	movq	(1*8)(ctx), hs1
	movq	(2*8)(ctx), hs2
	movq	(3*8)(ctx), hs3
	movq	(4*8)(ctx), hs4
	movq	(5*8)(ctx), hs5
	movq	(6*8)(ctx), hs6
	movq	(7*8)(ctx), hs7

	jmp	.Lblock_loop0

.align 16
.Lblock_loop0:
	mov	$0, round

	/* Round 0 through 15. */
	sha512_round_load(0, hs0, hs1, hs2, hs3, hs4, hs5, hs6, hs7)
	sha512_round_load(1, hs7, hs0, hs1, hs2, hs3, hs4, hs5, hs6)
	sha512_round_load(2, hs6, hs7, hs0, hs1, hs2, hs3, hs4, hs5)
	sha512_round_load(3, hs5, hs6, hs7, hs0, hs1, hs2, hs3, hs4)
	sha512_round_load(4, hs4, hs5, hs6, hs7, hs0, hs1, hs2, hs3)
	sha512_round_load(5, hs3, hs4, hs5, hs6, hs7, hs0, hs1, hs2)
	sha512_round_load(6, hs2, hs3, hs4, hs5, hs6, hs7, hs0, hs1)
	sha512_round_load(7, hs1, hs2, hs3, hs4, hs5, hs6, hs7, hs0)
	sha512_round_load(8, hs0, hs1, hs2, hs3, hs4, hs5, hs6, hs7)
	sha512_round_load(9, hs7, hs0, hs1, hs2, hs3, hs4, hs5, hs6)
	sha512_round_load(10, hs6, hs7, hs0, hs1, hs2, hs3, hs4, hs5)
	sha512_round_load(11, hs5, hs6, hs7, hs0, hs1, hs2, hs3, hs4)
	sha512_round_load(12, hs4, hs5, hs6, hs7, hs0, hs1, hs2, hs3)
	sha512_round_load(13, hs3, hs4, hs5, hs6, hs7, hs0, hs1, hs2)
	sha512_round_load(14, hs2, hs3, hs4, hs5, hs6, hs7, hs0, hs1)
	sha512_round_load(15, hs1, hs2, hs3, hs4, hs5, hs6, hs7, hs0)

	jmp	.Lblock_loop16

.align 16
.Lblock_loop16:
	/* Round 16 through 79. */
	sha512_round_update(16, hs0, hs1, hs2, hs3, hs4, hs5, hs6, hs7)
	sha512_round_update(17, hs7, hs0, hs1, hs2, hs3, hs4, hs5, hs6)
	sha512_round_update(18, hs6, hs7, hs0, hs1, hs2, hs3, hs4, hs5)
	sha512_round_update(19, hs5, hs6, hs7, hs0, hs1, hs2, hs3, hs4)
	sha512_round_update(20, hs4, hs5, hs6, hs7, hs0, hs1, hs2, hs3)
	sha512_round_update(21, hs3, hs4, hs5, hs6, hs7, hs0, hs1, hs2)
	sha512_round_update(22, hs2, hs3, hs4, hs5, hs6, hs7, hs0, hs1)
	sha512_round_update(23, hs1, hs2, hs3, hs4, hs5, hs6, hs7, hs0)
	sha512_round_update(24, hs0, hs1, hs2, hs3, hs4, hs5, hs6, hs7)
	sha512_round_update(25, hs7, hs0, hs1, hs2, hs3, hs4, hs5, hs6)
	sha512_round_update(26, hs6, hs7, hs0, hs1, hs2, hs3, hs4, hs5)
	sha512_round_update(27, hs5, hs6, hs7, hs0, hs1, hs2, hs3, hs4)
	sha512_round_update(28, hs4, hs5, hs6, hs7, hs0, hs1, hs2, hs3)
	sha512_round_update(29, hs3, hs4, hs5, hs6, hs7, hs0, hs1, hs2)
	sha512_round_update(30, hs2, hs3, hs4, hs5, hs6, hs7, hs0, hs1)
	sha512_round_update(31, hs1, hs2, hs3, hs4, hs5, hs6, hs7, hs0)

	cmp	$80, round
	jb	.Lblock_loop16

	movq	(128+1*8)(%rsp), ctx

	/* Add intermediate state to hash state. */
	addq	(0*8)(ctx), hs0
	addq	(1*8)(ctx), hs1
	addq	(2*8)(ctx), hs2
	addq	(3*8)(ctx), hs3
	addq	(4*8)(ctx), hs4
	addq	(5*8)(ctx), hs5
	addq	(6*8)(ctx), hs6
	addq	(7*8)(ctx), hs7

	/* Store new hash state to context. */
	movq	hs0, (0*8)(ctx)
	movq	hs1, (1*8)(ctx)
	movq	hs2, (2*8)(ctx)
	movq	hs3, (3*8)(ctx)
	movq	hs4, (4*8)(ctx)
	movq	hs5, (5*8)(ctx)
	movq	hs6, (6*8)(ctx)
	movq	hs7, (7*8)(ctx)

	addq	$128, in
	cmpq	(128+0*8)(%rsp), in
	jb	.Lblock_loop0

	movq	(128+2*8)(%rsp), %rsp

	/* Restore callee save registers. */
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbp
	popq	%rbx

	ret

/*
 * SHA-512 constants - see FIPS 180-4 section 4.2.3.
 */
.rodata
.align	64
.type	K512,@object
K512:
.quad	0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc
.quad	0x3956c25bf348b538, 0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118
.quad	0xd807aa98a3030242, 0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2
.quad	0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235, 0xc19bf174cf692694
.quad	0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65
.quad	0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5
.quad	0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4
.quad	0xc6e00bf33da88fc2, 0xd5a79147930aa725, 0x06ca6351e003826f, 0x142929670a0e6e70
.quad	0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df
.quad	0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b
.quad	0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30
.quad	0xd192e819d6ef5218, 0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8
.quad	0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8
.quad	0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3
.quad	0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec
.quad	0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b
.quad	0xca273eceea26619c, 0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178
.quad	0x06f067aa72176fba, 0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b
.quad	0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c
.quad	0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817
.size	K512,.-K512
