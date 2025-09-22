/*	$OpenBSD: bn_arch.h,v 1.13 2023/07/24 10:21:29 jsing Exp $ */
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

#if defined(__GNUC__)

#define HAVE_BN_CLZW

static inline int
bn_clzw(BN_ULONG w)
{
	BN_ULONG n;

	__asm__ ("clz   %[n], %[w]"
	    : [n]"=r"(n)
	    : [w]"r"(w));

	return n;
}

#define HAVE_BN_ADDW

static inline void
bn_addw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG carry, r0;

	__asm__ (
	    "adds  %[r0], %[a], %[b] \n"
	    "cset  %[carry], cs \n"
	    : [carry]"=r"(carry), [r0]"=r"(r0)
	    : [a]"r"(a), [b]"r"(b)
	    : "cc");

	*out_r1 = carry;
	*out_r0 = r0;
}

#define HAVE_BN_ADDW_ADDW

static inline void
bn_addw_addw(BN_ULONG a, BN_ULONG b, BN_ULONG c, BN_ULONG *out_r1,
    BN_ULONG *out_r0)
{
	BN_ULONG carry, r0;

	__asm__ (
	    "adds  %[r0], %[a], %[b] \n"
	    "cset  %[carry], cs \n"
	    "adds  %[r0], %[r0], %[c] \n"
	    "cinc  %[carry], %[carry], cs \n"
	    : [carry]"=&r"(carry), [r0]"=&r"(r0)
	    : [a]"r"(a), [b]"r"(b), [c]"r"(c)
	    : "cc");

	*out_r1 = carry;
	*out_r0 = r0;
}

#define HAVE_BN_QWADDQW

static inline void
bn_qwaddqw(BN_ULONG a3, BN_ULONG a2, BN_ULONG a1, BN_ULONG a0, BN_ULONG b3,
    BN_ULONG b2, BN_ULONG b1, BN_ULONG b0, BN_ULONG carry, BN_ULONG *out_carry,
    BN_ULONG *out_r3, BN_ULONG *out_r2, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r3, r2, r1, r0;

	__asm__ (
	    "adds  xzr, %[carry], #-1 \n"
	    "adcs  %[r0], %[a0], %[b0] \n"
	    "adcs  %[r1], %[a1], %[b1] \n"
	    "adcs  %[r2], %[a2], %[b2] \n"
	    "adcs  %[r3], %[a3], %[b3] \n"
	    "cset  %[carry], cs \n"
	    : [carry]"+r"(carry), [r3]"=&r"(r3), [r2]"=&r"(r2),
		[r1]"=&r"(r1), [r0]"=&r"(r0)
	    : [a3]"r"(a3), [a2]"r"(a2), [a1]"r"(a1), [a0]"r"(a0),
		[b3]"r"(b3), [b2]"r"(b2), [b1]"r"(b1), [b0]"r"(b0)
	    : "cc");

	*out_carry = carry;
	*out_r3 = r3;
	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}

#define HAVE_BN_MULW

static inline void
bn_mulw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r1, r0;

	/* Unsigned multiplication using a umulh/mul pair. */
	__asm__ (
	    "umulh %[r1], %[a], %[b] \n"
	    "mul   %[r0], %[a], %[b] \n"
	    : [r1]"=&r"(r1), [r0]"=r"(r0)
	    : [a]"r"(a), [b]"r"(b));

	*out_r1 = r1;
	*out_r0 = r0;
}

#define HAVE_BN_MULW_ADDW

static inline void
bn_mulw_addw(BN_ULONG a, BN_ULONG b, BN_ULONG c, BN_ULONG *out_r1,
    BN_ULONG *out_r0)
{
	BN_ULONG r1, r0;

	__asm__ (
	    "umulh  %[r1], %[a], %[b] \n"
	    "mul    %[r0], %[a], %[b] \n"
	    "adds   %[r0], %[r0], %[c] \n"
	    "adc    %[r1], %[r1], xzr \n"
	    : [r1]"=&r"(r1), [r0]"=&r"(r0)
	    : [a]"r"(a), [b]"r"(b), [c]"r"(c)
	    : "cc");

	*out_r1 = r1;
	*out_r0 = r0;
}

#define HAVE_BN_MULW_ADDW_ADDW

static inline void
bn_mulw_addw_addw(BN_ULONG a, BN_ULONG b, BN_ULONG c, BN_ULONG d,
    BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r1, r0;

	__asm__ (
	    "umulh  %[r1], %[a], %[b] \n"
	    "mul    %[r0], %[a], %[b] \n"
	    "adds   %[r0], %[r0], %[c] \n"
	    "adc    %[r1], %[r1], xzr \n"
	    "adds   %[r0], %[r0], %[d] \n"
	    "adc    %[r1], %[r1], xzr \n"
	    : [r1]"=&r"(r1), [r0]"=&r"(r0)
	    : [a]"r"(a), [b]"r"(b), [c]"r"(c), [d]"r"(d)
	    : "cc");

	*out_r1 = r1;
	*out_r0 = r0;
}

#define HAVE_BN_MULW_ADDTW

static inline void
bn_mulw_addtw(BN_ULONG a, BN_ULONG b, BN_ULONG c2, BN_ULONG c1, BN_ULONG c0,
    BN_ULONG *out_r2, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r2, r1, r0;

	__asm__ (
	    "umulh  %[r1], %[a], %[b] \n"
	    "mul    %[r0], %[a], %[b] \n"
	    "adds   %[r0], %[r0], %[c0] \n"
	    "adcs   %[r1], %[r1], %[c1] \n"
	    "adc    %[r2], xzr, %[c2] \n"
	    : [r2]"=&r"(r2), [r1]"=&r"(r1), [r0]"=&r"(r0)
	    : [a]"r"(a), [b]"r"(b), [c2]"r"(c2), [c1]"r"(c1), [c0]"r"(c0)
	    : "cc");

	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}

#define HAVE_BN_MUL2_MULW_ADDTW

static inline void
bn_mul2_mulw_addtw(BN_ULONG a, BN_ULONG b, BN_ULONG c2, BN_ULONG c1, BN_ULONG c0,
    BN_ULONG *out_r2, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r2, r1, r0, x1, x0;

	__asm__ (
	    "umulh  %[x1], %[a], %[b] \n"
	    "mul    %[x0], %[a], %[b] \n"
	    "adds   %[r0], %[c0], %[x0] \n"
	    "adcs   %[r1], %[c1], %[x1] \n"
	    "adc    %[r2], xzr, %[c2] \n"
	    "adds   %[r0], %[r0], %[x0] \n"
	    "adcs   %[r1], %[r1], %[x1] \n"
	    "adc    %[r2], xzr, %[r2] \n"
	    : [r2]"=&r"(r2), [r1]"=&r"(r1), [r0]"=&r"(r0), [x1]"=&r"(x1),
		[x0]"=&r"(x0)
	    : [a]"r"(a), [b]"r"(b), [c2]"r"(c2), [c1]"r"(c1), [c0]"r"(c0)
	    : "cc");

	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}

#define HAVE_BN_QWMULW_ADDW

static inline void
bn_qwmulw_addw(BN_ULONG a3, BN_ULONG a2, BN_ULONG a1, BN_ULONG a0, BN_ULONG b,
    BN_ULONG c, BN_ULONG *out_r4, BN_ULONG *out_r3, BN_ULONG *out_r2,
    BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r4, r3, r2, r1, r0;

	__asm__ (
	    "umulh  %[r1], %[a0], %[b] \n"
	    "mul    %[r0], %[a0], %[b] \n"
	    "adds   %[r0], %[r0], %[c] \n"
	    "umulh  %[r2], %[a1], %[b] \n"
	    "mul     %[c], %[a1], %[b] \n"
	    "adcs   %[r1], %[r1], %[c] \n"
	    "umulh  %[r3], %[a2], %[b] \n"
	    "mul     %[c], %[a2], %[b] \n"
	    "adcs   %[r2], %[r2], %[c] \n"
	    "umulh  %[r4], %[a3], %[b] \n"
	    "mul     %[c], %[a3], %[b] \n"
	    "adcs   %[r3], %[r3], %[c] \n"
	    "adc    %[r4], %[r4], xzr  \n"
	    : [c]"+&r"(c), [r4]"=&r"(r4), [r3]"=&r"(r3), [r2]"=&r"(r2),
		[r1]"=&r"(r1), [r0]"=&r"(r0)
	    : [a3]"r"(a3), [a2]"r"(a2), [a1]"r"(a1), [a0]"r"(a0), [b]"r"(b)
	    : "cc");

	*out_r4 = r4;
	*out_r3 = r3;
	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}

#define HAVE_BN_QWMULW_ADDQW_ADDW

static inline void
bn_qwmulw_addqw_addw(BN_ULONG a3, BN_ULONG a2, BN_ULONG a1, BN_ULONG a0,
    BN_ULONG b, BN_ULONG c3, BN_ULONG c2, BN_ULONG c1, BN_ULONG c0, BN_ULONG d,
    BN_ULONG *out_r4, BN_ULONG *out_r3, BN_ULONG *out_r2, BN_ULONG *out_r1,
    BN_ULONG *out_r0)
{
	BN_ULONG r4, r3, r2, r1, r0;

	__asm__ (
	    "umulh  %[r1], %[a0], %[b]  \n"
	    "mul    %[r0], %[a0], %[b]  \n"
	    "adds   %[r0], %[r0], %[d]  \n"
	    "umulh  %[r2], %[a1], %[b]  \n"
	    "mul     %[d], %[a1], %[b]  \n"
	    "adcs   %[r1], %[r1], %[d]  \n"
	    "umulh  %[r3], %[a2], %[b]  \n"
	    "mul     %[d], %[a2], %[b]  \n"
	    "adcs   %[r2], %[r2], %[d]  \n"
	    "umulh  %[r4], %[a3], %[b]  \n"
	    "mul     %[d], %[a3], %[b]  \n"
	    "adcs   %[r3], %[r3], %[d]  \n"
	    "adc    %[r4], %[r4], xzr   \n"
	    "adds   %[r0], %[r0], %[c0] \n"
	    "adcs   %[r1], %[r1], %[c1] \n"
	    "adcs   %[r2], %[r2], %[c2] \n"
	    "adcs   %[r3], %[r3], %[c3] \n"
	    "adc    %[r4], %[r4], xzr   \n"
	    : [d]"+&r"(d), [r4]"=&r"(r4), [r3]"=&r"(r3), [r2]"=&r"(r2),
		[r1]"=&r"(r1), [r0]"=&r"(r0)
	    : [a3]"r"(a3), [a2]"r"(a2), [a1]"r"(a1), [a0]"r"(a0), [b]"r"(b),
		[c3]"r"(c3), [c2]"r"(c2), [c1]"r"(c1), [c0]"r"(c0)
	    : "cc");

	*out_r4 = r4;
	*out_r3 = r3;
	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}

#define HAVE_BN_SUBW

static inline void
bn_subw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_borrow, BN_ULONG *out_r0)
{
	BN_ULONG borrow, r0;

	__asm__ (
	    "subs  %[r0], %[a], %[b] \n"
	    "cset  %[borrow], cc \n"
	    : [borrow]"=r"(borrow), [r0]"=r"(r0)
	    : [a]"r"(a), [b]"r"(b)
	    : "cc");

	*out_borrow = borrow;
	*out_r0 = r0;
}

#define HAVE_BN_SUBW_SUBW

static inline void
bn_subw_subw(BN_ULONG a, BN_ULONG b, BN_ULONG c, BN_ULONG *out_borrow,
    BN_ULONG *out_r0)
{
	BN_ULONG borrow, r0;

	__asm__ (
	    "subs  %[r0], %[a], %[b] \n"
	    "cset  %[borrow], cc \n"
	    "subs  %[r0], %[r0], %[c] \n"
	    "cinc  %[borrow], %[borrow], cc \n"
	    : [borrow]"=&r"(borrow), [r0]"=&r"(r0)
	    : [a]"r"(a), [b]"r"(b), [c]"r"(c)
	    : "cc");

	*out_borrow = borrow;
	*out_r0 = r0;
}

#define HAVE_BN_QWSUBQW

static inline void
bn_qwsubqw(BN_ULONG a3, BN_ULONG a2, BN_ULONG a1, BN_ULONG a0, BN_ULONG b3,
    BN_ULONG b2, BN_ULONG b1, BN_ULONG b0, BN_ULONG borrow, BN_ULONG *out_borrow,
    BN_ULONG *out_r3, BN_ULONG *out_r2, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r3, r2, r1, r0;

	__asm__ (
	    "subs  xzr, xzr, %[borrow] \n"
	    "sbcs  %[r0], %[a0], %[b0] \n"
	    "sbcs  %[r1], %[a1], %[b1] \n"
	    "sbcs  %[r2], %[a2], %[b2] \n"
	    "sbcs  %[r3], %[a3], %[b3] \n"
	    "cset  %[borrow], cc \n"
	    : [borrow]"+r"(borrow), [r3]"=&r"(r3), [r2]"=&r"(r2),
		[r1]"=&r"(r1), [r0]"=&r"(r0)
	    : [a3]"r"(a3), [a2]"r"(a2), [a1]"r"(a1), [a0]"r"(a0),
		[b3]"r"(b3), [b2]"r"(b2), [b1]"r"(b1), [b0]"r"(b0)
	    : "cc");

	*out_borrow = borrow;
	*out_r3 = r3;
	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}

#endif /* __GNUC__ */

#endif
#endif
