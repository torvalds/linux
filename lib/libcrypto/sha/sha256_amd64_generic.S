/* $OpenBSD: sha256_amd64_generic.S,v 1.3 2024/11/16 12:34:16 jsing Exp $ */
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

#define	hs0		%r8d
#define	hs1		%r9d
#define	hs2		%r10d
#define	hs3		%r11d
#define	hs4		%r12d
#define	hs5		%r13d
#define	hs6		%r14d
#define	hs7		%r15d

#define	k256		%rbp

#define	tmp0		%eax
#define	tmp1		%ebx
#define	tmp2		%ecx
#define	tmp3		%edx

/*
 * Load message into wt, storing a copy in the message schedule:
 *
 *  Wt = Mt
 */
#define sha256_message_schedule_load(idx, m, w, wt) \
	movl	(m, round, 4), wt;				\
	bswapl	wt;						\
	movl	wt, ((idx&0xf)*4)(w);

/*
 * Update message schedule and return current value in wt:
 *
 *  Wt = sigma1(W(t-2)) + W(t-7) + sigma0(W(t-15)) + W(t-16)
 *
 *  sigma0(x) = ror(x, 7) ^ ror(x, 18) ^ (x >> 3)
 *  sigma1(x) = ror(x, 17) ^ ror(x, 19) ^ (x >> 10)
 */
#define sha256_message_schedule_update(idx, w, wt) \
	movl	(((idx-2)&0xf)*4)(w), wt;	/* sigma1 */	\
	movl	wt, tmp1;			/* sigma1 */	\
	rorl	$(19-17), tmp1;			/* sigma1 */	\
	xorl	wt, tmp1;			/* sigma1 */	\
	rorl	$17, tmp1;			/* sigma1 */	\
	shrl	$10, wt;			/* sigma1 */	\
	xorl	tmp1, wt;			/* sigma1 */	\
	\
	addl	(((idx-7)&0xf)*4)(w), wt;	/* Wt-7 */	\
	addl	(((idx-16)&0xf)*4)(w), wt;	/* Wt-16 */	\
	\
	movl	(((idx-15)&0xf)*4)(w), tmp2;	/* sigma0 */	\
	movl	tmp2, tmp3;			/* sigma0 */	\
	rorl	$(18-7), tmp2;			/* sigma0 */	\
	xorl	tmp3, tmp2;			/* sigma0 */	\
	rorl	$7, tmp2;			/* sigma0 */	\
	shrl	$3, tmp3;			/* sigma0 */	\
	xorl	tmp3, tmp2;			/* sigma0 */	\
	addl	tmp2, wt;			/* sigma0 */	\
	\
	movl	wt, ((idx&0xf)*4)(w);

/*
 * Compute a SHA-256 round:
 *
 *  T1 = h + Sigma1(e) + Ch(e, f, g) + Kt + Wt
 *  T2 = Sigma0(a) + Maj(a, b, c)
 *
 *  Sigma0(x) = ror(x, 2) ^ ror(x, 13) ^ ror(x, 22)
 *  Sigma1(x) = ror(x, 6) ^ ror(x, 11) ^ ror(x, 25)
 *  Ch(x, y, z) = (x & y) ^ (~x & z) = ((y ^ z) & x) ^ z
 *  Maj(x, y, z) = (x & y) ^ (x & z) ^ (y & z) = ((y ^ z) & x) ^ (y & z)
 *
 * Upon completion d = d + T1, h = T1 + T2, pending rotation.
 */
#define sha256_round(idx, a, b, c, d, e, f, g, h, k, w, wt) \
	addl	wt, h;				/* T1 Wt */	\
	addl	(k256, round, 4), h;		/* T1 Kt */	\
	\
	movl	e, tmp1;			/* T1 Sigma1 */	\
	rorl	$(25-11), tmp1;			/* T1 Sigma1 */	\
	xorl	e, tmp1;			/* T1 Sigma1 */ \
	rorl	$(11-6), tmp1;			/* T1 Sigma1 */	\
	xorl	e, tmp1;			/* T1 Sigma1 */ \
	rorl	$6, tmp1;			/* T1 Sigma1 */	\
	addl	tmp1, h;			/* T1 Sigma1 */	\
	\
	movl	f, tmp2;			/* T1 Ch */	\
	xorl	g, tmp2;			/* T1 Ch */	\
	andl	e, tmp2;			/* T1 Ch */	\
	xorl	g, tmp2;			/* T1 Ch */	\
	addl	tmp2, h;			/* T1 Ch */	\
	\
	addl	h, d;				/* d += T1 */	\
	\
	movl	a, tmp1;			/* T2 Sigma0 */	\
	rorl	$(22-13), tmp1;			/* T2 Sigma0 */	\
	xorl	a, tmp1;			/* T2 Sigma0 */	\
	rorl	$(13-2), tmp1;			/* T2 Sigma0 */	\
	xorl	a, tmp1;			/* T2 Sigma0 */	\
	rorl	$2, tmp1;			/* T2 Sigma0 */	\
	addl	tmp1, h;			/* T2 Sigma0 */	\
	\
	movl	b, tmp2;			/* T2 Maj */	\
	xorl	c, tmp2;			/* T2 Maj */	\
	andl	a, tmp2;			/* T2 Maj */	\
	movl	b, tmp3;			/* T2 Maj */	\
	andl	c, tmp3;			/* T2 Maj */	\
	xorl	tmp2, tmp3;			/* T2 Maj */	\
	addl	tmp3, h;			/* T2 Maj */	\
	\
	addq	$1, round;

#define sha256_round_load(idx, a, b, c, d, e, f, g, h) \
	sha256_message_schedule_load(idx, in, %rsp, tmp0) \
	sha256_round(idx, a, b, c, d, e, f, g, h, k256, %rsp, tmp0)

#define sha256_round_update(idx, a, b, c, d, e, f, g, h) \
	sha256_message_schedule_update(idx, %rsp, tmp0) \
	sha256_round(idx, a, b, c, d, e, f, g, h, k256, %rsp, tmp0)

.text

/*
 * void sha256_block_generic(SHA256_CTX *ctx, const void *in, size_t num);
 *
 * Standard x86-64 ABI: rdi = ctx, rsi = in, rdx = num
 */
.align 16
.globl	sha256_block_generic
.type	sha256_block_generic,@function
sha256_block_generic:
	_CET_ENDBR

	/* Save callee save registers. */
	pushq	%rbx
	pushq	%rbp
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	/* Allocate space for message schedule, context pointer and end of message. */
	movq	%rsp, %rax
	subq	$(64+3*8), %rsp
	andq	$~63, %rsp
	movq	%rax, (64+2*8)(%rsp)
	movq	ctx, (64+1*8)(%rsp)

	/* Compute and store end of message. */
	shlq	$6, num
	leaq	(in, num, 1), %rbx
	movq	%rbx, (64+0*8)(%rsp)

	/* Address of SHA-256 constants. */
	leaq	K256(%rip), k256

	/* Load current hash state from context. */
	movl	(0*4)(ctx), hs0
	movl	(1*4)(ctx), hs1
	movl	(2*4)(ctx), hs2
	movl	(3*4)(ctx), hs3
	movl	(4*4)(ctx), hs4
	movl	(5*4)(ctx), hs5
	movl	(6*4)(ctx), hs6
	movl	(7*4)(ctx), hs7

	jmp	.Lblock_loop0

.align 16
.Lblock_loop0:
	mov	$0, round

	/* Round 0 through 15. */
	sha256_round_load(0, hs0, hs1, hs2, hs3, hs4, hs5, hs6, hs7)
	sha256_round_load(1, hs7, hs0, hs1, hs2, hs3, hs4, hs5, hs6)
	sha256_round_load(2, hs6, hs7, hs0, hs1, hs2, hs3, hs4, hs5)
	sha256_round_load(3, hs5, hs6, hs7, hs0, hs1, hs2, hs3, hs4)
	sha256_round_load(4, hs4, hs5, hs6, hs7, hs0, hs1, hs2, hs3)
	sha256_round_load(5, hs3, hs4, hs5, hs6, hs7, hs0, hs1, hs2)
	sha256_round_load(6, hs2, hs3, hs4, hs5, hs6, hs7, hs0, hs1)
	sha256_round_load(7, hs1, hs2, hs3, hs4, hs5, hs6, hs7, hs0)
	sha256_round_load(8, hs0, hs1, hs2, hs3, hs4, hs5, hs6, hs7)
	sha256_round_load(9, hs7, hs0, hs1, hs2, hs3, hs4, hs5, hs6)
	sha256_round_load(10, hs6, hs7, hs0, hs1, hs2, hs3, hs4, hs5)
	sha256_round_load(11, hs5, hs6, hs7, hs0, hs1, hs2, hs3, hs4)
	sha256_round_load(12, hs4, hs5, hs6, hs7, hs0, hs1, hs2, hs3)
	sha256_round_load(13, hs3, hs4, hs5, hs6, hs7, hs0, hs1, hs2)
	sha256_round_load(14, hs2, hs3, hs4, hs5, hs6, hs7, hs0, hs1)
	sha256_round_load(15, hs1, hs2, hs3, hs4, hs5, hs6, hs7, hs0)

	jmp	.Lblock_loop16

.align 16
.Lblock_loop16:
	/* Round 16 through 63. */
	sha256_round_update(16, hs0, hs1, hs2, hs3, hs4, hs5, hs6, hs7)
	sha256_round_update(17, hs7, hs0, hs1, hs2, hs3, hs4, hs5, hs6)
	sha256_round_update(18, hs6, hs7, hs0, hs1, hs2, hs3, hs4, hs5)
	sha256_round_update(19, hs5, hs6, hs7, hs0, hs1, hs2, hs3, hs4)
	sha256_round_update(20, hs4, hs5, hs6, hs7, hs0, hs1, hs2, hs3)
	sha256_round_update(21, hs3, hs4, hs5, hs6, hs7, hs0, hs1, hs2)
	sha256_round_update(22, hs2, hs3, hs4, hs5, hs6, hs7, hs0, hs1)
	sha256_round_update(23, hs1, hs2, hs3, hs4, hs5, hs6, hs7, hs0)
	sha256_round_update(24, hs0, hs1, hs2, hs3, hs4, hs5, hs6, hs7)
	sha256_round_update(25, hs7, hs0, hs1, hs2, hs3, hs4, hs5, hs6)
	sha256_round_update(26, hs6, hs7, hs0, hs1, hs2, hs3, hs4, hs5)
	sha256_round_update(27, hs5, hs6, hs7, hs0, hs1, hs2, hs3, hs4)
	sha256_round_update(28, hs4, hs5, hs6, hs7, hs0, hs1, hs2, hs3)
	sha256_round_update(29, hs3, hs4, hs5, hs6, hs7, hs0, hs1, hs2)
	sha256_round_update(30, hs2, hs3, hs4, hs5, hs6, hs7, hs0, hs1)
	sha256_round_update(31, hs1, hs2, hs3, hs4, hs5, hs6, hs7, hs0)

	cmp	$64, round
	jb	.Lblock_loop16

	movq	(64+1*8)(%rsp), ctx

	/* Add intermediate state to hash state. */
	addl	(0*4)(ctx), hs0
	addl	(1*4)(ctx), hs1
	addl	(2*4)(ctx), hs2
	addl	(3*4)(ctx), hs3
	addl	(4*4)(ctx), hs4
	addl	(5*4)(ctx), hs5
	addl	(6*4)(ctx), hs6
	addl	(7*4)(ctx), hs7

	/* Store new hash state to context. */
	movl	hs0, (0*4)(ctx)
	movl	hs1, (1*4)(ctx)
	movl	hs2, (2*4)(ctx)
	movl	hs3, (3*4)(ctx)
	movl	hs4, (4*4)(ctx)
	movl	hs5, (5*4)(ctx)
	movl	hs6, (6*4)(ctx)
	movl	hs7, (7*4)(ctx)

	addq	$64, in
	cmpq	(64+0*8)(%rsp), in
	jb	.Lblock_loop0

	movq	(64+2*8)(%rsp), %rsp

	/* Restore callee save registers. */
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbp
	popq	%rbx

	ret

/*
 * SHA-256 constants - see FIPS 180-4 section 4.2.2.
 */
.rodata
.align	64
.type	K256,@object
K256:
.long	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
.long	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
.long	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
.long	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
.long	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
.long	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
.long	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
.long	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
.long	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
.long	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
.long	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
.long	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
.long	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
.long	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
.long	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
.long	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
.size	K256,.-K256
