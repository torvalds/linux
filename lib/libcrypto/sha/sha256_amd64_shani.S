/* $OpenBSD: sha256_amd64_shani.S,v 1.1 2024/11/16 15:31:36 jsing Exp $ */
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

/*
 * SHA-256 implementation using the Intel SHA extensions:
 *
 * https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sha-extensions.html
 */

#define	ctx		%rdi
#define	in		%rsi
#define	num		%rdx

#define	end		%rbx

#define	k256		%rbp

#define	xmsg		%xmm0

#define	xhs0		%xmm1
#define	xhs1		%xmm2

#define	xabef		%xmm3
#define	xcdgh		%xmm4

#define	xmsgtmp0	%xmm6
#define	xmsgtmp1	%xmm7
#define	xmsgtmp2	%xmm8
#define	xmsgtmp3	%xmm9
#define	xmsgtmp4	%xmm10

#define	xshufmask	%xmm11

#define	xtmp0		%xmm12

#define sha256_message_schedule_load(idx, m, xmsgtmp) \
	movdqu	(idx*16)(m), xmsg;					\
	pshufb	xshufmask, xmsg;					\
	movdqa	xmsg, xmsgtmp;

#define sha256_message_schedule_update(xmt0, xmt1, xmt2, xmt3) \
	sha256msg1 xmt1, xmt0;						\
	movdqa	xmt3, xmsgtmp4;						\
	palignr	$4, xmt2, xmsgtmp4;					\
	paddd	xmsgtmp4, xmt0;						\
	sha256msg2 xmt3, xmt0;

#define sha256_shani_round(idx) \
	paddd	(idx*16)(k256), xmsg;					\
	sha256rnds2 xmsg, xhs0, xhs1;					\
	pshufd	$0x0e, xmsg, xmsg;					\
	sha256rnds2 xmsg, xhs1, xhs0;

#define sha256_shani_round_load(idx, m, xmsgtmp) \
	sha256_message_schedule_load(idx, m, xmsgtmp);			\
	sha256_shani_round(idx);

#define sha256_shani_round_update(idx, xmt0, xmt1, xmt2, xmt3) \
	sha256_message_schedule_update(xmt0, xmt1, xmt2, xmt3);		\
	movdqa	xmt0, xmsg;						\
	sha256_shani_round(idx);

.text

/*
 * void sha256_block_shani(SHA256_CTX *ctx, const void *in, size_t num);
 *
 * Standard x86-64 ABI: rdi = ctx, rsi = in, rdx = num
 */
.align 16
.globl	sha256_block_shani
.type	sha256_block_shani,@function
sha256_block_shani:
	_CET_ENDBR

	/* Save callee save registers. */
	pushq	%rbx
	pushq	%rbp

	/* Compute end of message. */
	shlq	$6, num
	leaq	(in, num, 1), end

	/* Address of SHA-256 constants. */
	leaq	K256(%rip), k256

	/* Load endian shuffle mask. */
	movdqa	shufmask(%rip), xshufmask

	/* Load current hash state from context. */
	movdqu	(0*16)(ctx), xhs0	/* dcba */
	movdqu	(1*16)(ctx), xhs1	/* hgfe */

	/* Rearrange words to construct abef/cdgh. */
	pshufd	$0xb1, xhs0, xhs0	/* cdab */
	pshufd	$0x1b, xhs1, xhs1	/* efgh */
	movdqa	xhs0, xtmp0
	palignr	$8, xhs1, xhs0		/* abef */
	pblendw	$0xf0, xtmp0, xhs1	/* cdgh */

	jmp	.Lshani_block_loop

.align 16
.Lshani_block_loop:
	/* Save state for accumulation. */
	movdqa	xhs0, xabef
	movdqa	xhs1, xcdgh

	/* Rounds 0 through 15 (four rounds at a time). */
	sha256_shani_round_load(0, in, xmsgtmp0)
	sha256_shani_round_load(1, in, xmsgtmp1)
	sha256_shani_round_load(2, in, xmsgtmp2)
	sha256_shani_round_load(3, in, xmsgtmp3)

	/* Rounds 16 through 63 (four rounds at a time). */
	sha256_shani_round_update(4, xmsgtmp0, xmsgtmp1, xmsgtmp2, xmsgtmp3)
	sha256_shani_round_update(5, xmsgtmp1, xmsgtmp2, xmsgtmp3, xmsgtmp0)
	sha256_shani_round_update(6, xmsgtmp2, xmsgtmp3, xmsgtmp0, xmsgtmp1)
	sha256_shani_round_update(7, xmsgtmp3, xmsgtmp0, xmsgtmp1, xmsgtmp2)

	sha256_shani_round_update(8, xmsgtmp0, xmsgtmp1, xmsgtmp2, xmsgtmp3)
	sha256_shani_round_update(9, xmsgtmp1, xmsgtmp2, xmsgtmp3, xmsgtmp0)
	sha256_shani_round_update(10, xmsgtmp2, xmsgtmp3, xmsgtmp0, xmsgtmp1)
	sha256_shani_round_update(11, xmsgtmp3, xmsgtmp0, xmsgtmp1, xmsgtmp2)

	sha256_shani_round_update(12, xmsgtmp0, xmsgtmp1, xmsgtmp2, xmsgtmp3)
	sha256_shani_round_update(13, xmsgtmp1, xmsgtmp2, xmsgtmp3, xmsgtmp0)
	sha256_shani_round_update(14, xmsgtmp2, xmsgtmp3, xmsgtmp0, xmsgtmp1)
	sha256_shani_round_update(15, xmsgtmp3, xmsgtmp0, xmsgtmp1, xmsgtmp2)

	/* Accumulate hash state. */
	paddd	xabef, xhs0
	paddd	xcdgh, xhs1

	addq	$64, in
	cmpq	end, in
	jb	.Lshani_block_loop

	/* Rearrange words to construct dcba/hgfe. */
	pshufd	$0x1b, xhs0, xhs0	/* feba */
	pshufd	$0xb1, xhs1, xhs1	/* dchg */
	movdqa	xhs0, xtmp0
	pblendw	$0xf0, xhs1, xhs0	/* dcba */
	palignr	$8, xtmp0, xhs1		/* hgfe */

	/* Update stored hash context. */
	movdqu	xhs0, (0*16)(ctx)
	movdqu	xhs1, (1*16)(ctx)

	/* Restore callee save registers. */
	popq	%rbp
	popq	%rbx

	ret

.rodata

/*
 * Shuffle mask - little endian to big endian word conversion.
 */
.align	16
.type	shufmask,@object
shufmask:
.octa	0x0c0d0e0f08090a0b0405060700010203
.size	shufmask,.-shufmask

/*
 * SHA-256 constants - see FIPS 180-4 section 4.2.2.
 */
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
