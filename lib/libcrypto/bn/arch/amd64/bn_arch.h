/*	$OpenBSD: bn_arch.h,v 1.19 2025/09/01 15:15:44 jsing Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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

#include <openssl/bn.h>

#ifndef HEADER_BN_ARCH_H
#define HEADER_BN_ARCH_H

#ifndef OPENSSL_NO_ASM

#define HAVE_BN_ADD
#define HAVE_BN_ADD_WORDS

#define HAVE_BN_DIV_WORDS

#define HAVE_BN_MOD_ADD_WORDS
#define HAVE_BN_MOD_SUB_WORDS

#define HAVE_BN_MUL_COMBA4
#define HAVE_BN_MUL_COMBA6
#define HAVE_BN_MUL_COMBA8
#define HAVE_BN_MUL_WORDS
#define HAVE_BN_MULW_ADD_WORDS
#define HAVE_BN_MULW_WORDS

#define HAVE_BN_SQR_COMBA4
#define HAVE_BN_SQR_COMBA6
#define HAVE_BN_SQR_COMBA8
#define HAVE_BN_SQR_WORDS

#define HAVE_BN_SUB
#define HAVE_BN_SUB_WORDS

#define HAVE_BN_WORD_CLZ

#if defined(__GNUC__)

#define HAVE_BN_DIV_REM_WORDS_INLINE

static inline void
bn_div_rem_words_inline(BN_ULONG h, BN_ULONG l, BN_ULONG d, BN_ULONG *out_q,
    BN_ULONG *out_r)
{
	BN_ULONG q, r;

	/*
	 * Unsigned division of %rdx:%rax by d with quotient being stored in
	 * %rax and remainder in %rdx.
	 */
	__asm__ volatile ("divq %4"
	    : "=a"(q), "=d"(r)
	    : "d"(h), "a"(l), "rm"(d)
	    : "cc");

	*out_q = q;
	*out_r = r;
}

#define HAVE_BN_MULW

static inline void
bn_mulw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r1, r0;

	/*
	 * Unsigned multiplication of %rax, with the double word result being
	 * stored in %rdx:%rax.
	 */
	__asm__ ("mulq %3"
	    : "=d"(r1), "=a"(r0)
	    : "a"(a), "rm"(b)
	    : "cc");

	*out_r1 = r1;
	*out_r0 = r0;
}

#define HAVE_BN_SUBW

static inline void
bn_subw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_borrow, BN_ULONG *out_r0)
{
	BN_ULONG borrow, r0;

	__asm__ (
	    "subq   %3, %1 \n"
	    "setb   %b0 \n"
	    "and    $1, %0 \n"
	    : "=r"(borrow), "=r"(r0)
	    : "1"(a), "rm"(b)
	    : "cc");

	*out_borrow = borrow;
	*out_r0 = r0;
}

#endif /* __GNUC__ */

#endif
#endif
