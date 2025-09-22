/* $OpenBSD: sha1_amd64_shani.S,v 1.1 2024/12/06 11:57:18 jsing Exp $ */
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
 * SHA-1 implementation using the Intel SHA extensions:
 *
 * https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sha-extensions.html
 */

#define	ctx		%rdi
#define	in		%rsi
#define	num		%rdx

#define	end		%rbx

#define	xabcd_save	%xmm0
#define	xe_save		%xmm1

#define	xabcd		%xmm2
#define	xe0		%xmm3
#define	xe1		%xmm4

#define	xmsg0		%xmm5
#define	xmsg1		%xmm6
#define	xmsg2		%xmm7
#define	xmsg3		%xmm8

#define	xshufmask	%xmm9


#define sha1_message_schedule_load(idx, m, xmsg) \
	movdqu	(idx*16)(m), xmsg;					\
	pshufb	xshufmask, xmsg;

#define sha1_message_schedule_update(xm0, xm1, xm2, xm3) \
	sha1msg1 xm1, xm0;						\
	pxor	xm2, xm0;						\
	sha1msg2 xm3, xm0;

#define sha1_shani_round(fn, xmsg, xe, xe_next) \
	sha1nexte xmsg, xe;						\
	movdqa	xabcd, xe_next;						\
	sha1rnds4 fn, xe, xabcd;

#define sha1_shani_round_load(fn, idx, m, xmsg, xe, xe_next) \
	sha1_message_schedule_load(idx, m, xmsg);			\
	sha1_shani_round(fn, xmsg, xe, xe_next);

#define sha1_shani_round_update(fn, xm0, xm1, xm2, xm3, xe, xe_next) \
	sha1_message_schedule_update(xm0, xm1, xm2, xm3);		\
	sha1_shani_round(fn, xm0, xe, xe_next);


.text

/*
 * void sha1_block_shani(SHA256_CTX *ctx, const void *in, size_t num);
 *
 * Standard x86-64 ABI: rdi = ctx, rsi = in, rdx = num
 */
.align 16
.globl	sha1_block_shani
.type	sha1_block_shani,@function
sha1_block_shani:
	_CET_ENDBR

	/* Save callee save registers. */
	pushq	%rbx

	/* Compute end of message. */
	shlq	$6, num
	leaq	(in, num, 1), end

	/* Load endian shuffle mask. */
	movdqa	shufmask(%rip), xshufmask

	/* Load current hash state from context. */
	movdqu	(0*16)(ctx), xabcd
	pshufd	$0x1b, xabcd, xabcd	/* dcba -> abcd */
	pxor	xe0, xe0
	pinsrd	$3, (1*16)(ctx), xe0	/* e */

	jmp	.Lshani_block_loop

.align 16
.Lshani_block_loop:
	/* Save state for accumulation. */
	movdqa	xabcd, xabcd_save
	movdqa	xe0, xe_save

	/* Rounds 0 through 15 (four rounds at a time). */
	sha1_message_schedule_load(0, in, xmsg0);
	paddd	xmsg0, xe0
	movdqa	xabcd, xe1
	sha1rnds4 $0, xe0, xabcd

	sha1_shani_round_load($0, 1, in, xmsg1, xe1, xe0);
	sha1_shani_round_load($0, 2, in, xmsg2, xe0, xe1);
	sha1_shani_round_load($0, 3, in, xmsg3, xe1, xe0);

	/* Rounds 16 through 79 (four rounds at a time). */
	sha1_shani_round_update($0, xmsg0, xmsg1, xmsg2, xmsg3, xe0, xe1)
	sha1_shani_round_update($1, xmsg1, xmsg2, xmsg3, xmsg0, xe1, xe0)
	sha1_shani_round_update($1, xmsg2, xmsg3, xmsg0, xmsg1, xe0, xe1)
	sha1_shani_round_update($1, xmsg3, xmsg0, xmsg1, xmsg2, xe1, xe0)

	sha1_shani_round_update($1, xmsg0, xmsg1, xmsg2, xmsg3, xe0, xe1)
	sha1_shani_round_update($1, xmsg1, xmsg2, xmsg3, xmsg0, xe1, xe0)
	sha1_shani_round_update($2, xmsg2, xmsg3, xmsg0, xmsg1, xe0, xe1)
	sha1_shani_round_update($2, xmsg3, xmsg0, xmsg1, xmsg2, xe1, xe0)

	sha1_shani_round_update($2, xmsg0, xmsg1, xmsg2, xmsg3, xe0, xe1)
	sha1_shani_round_update($2, xmsg1, xmsg2, xmsg3, xmsg0, xe1, xe0)
	sha1_shani_round_update($2, xmsg2, xmsg3, xmsg0, xmsg1, xe0, xe1)
	sha1_shani_round_update($3, xmsg3, xmsg0, xmsg1, xmsg2, xe1, xe0)

	sha1_shani_round_update($3, xmsg0, xmsg1, xmsg2, xmsg3, xe0, xe1)
	sha1_shani_round_update($3, xmsg1, xmsg2, xmsg3, xmsg0, xe1, xe0)
	sha1_shani_round_update($3, xmsg2, xmsg3, xmsg0, xmsg1, xe0, xe1)
	sha1_shani_round_update($3, xmsg3, xmsg0, xmsg1, xmsg2, xe1, xe0)

	/* Accumulate hash state. */
	paddd	xabcd_save, xabcd
	sha1nexte xe_save, xe0

	addq	$64, in
	cmpq	end, in
	jb	.Lshani_block_loop

	/* Update stored hash context. */
	pshufd	$0x1b, xabcd, xabcd	/* abcd -> dcba */
	movdqu	xabcd, (0*16)(ctx)
	pextrd	$3, xe0, (1*16)(ctx)	/* e */

	/* Restore callee save registers. */
	popq	%rbx

	ret

.rodata

/*
 * Shuffle mask - byte reversal for little endian to big endian word conversion,
 * and reordering to abcd.
 */
.align	16
.type	shufmask,@object
shufmask:
.octa	0x000102030405060708090a0b0c0d0e0f
.size	shufmask,.-shufmask
