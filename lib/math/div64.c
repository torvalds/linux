// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2003 Bernardo Innocenti <bernie@develer.com>
 *
 * Based on former do_div() implementation from asm-parisc/div64.h:
 *	Copyright (C) 1999 Hewlett-Packard Co
 *	Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 *
 * Generic C version of 64bit/32bit division and modulo, with
 * 64bit result and 32bit remainder.
 *
 * The fast case for (n>>32 == 0) is handled inline by do_div().
 *
 * Code generated for this function might be very inefficient
 * for some CPUs. __div64_32() can be overridden by linking arch-specific
 * assembly versions such as arch/ppc/lib/div64.S and arch/sh/lib/div64.S
 * or by defining a preprocessor macro in arch/include/asm/div64.h.
 */

#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/log2.h>

/* Not needed on 64bit architectures */
#if BITS_PER_LONG == 32

#ifndef __div64_32
uint32_t __attribute__((weak)) __div64_32(uint64_t *n, uint32_t base)
{
	uint64_t rem = *n;
	uint64_t b = base;
	uint64_t res, d = 1;
	uint32_t high = rem >> 32;

	/* Reduce the thing a bit first */
	res = 0;
	if (high >= base) {
		high /= base;
		res = (uint64_t) high << 32;
		rem -= (uint64_t) (high*base) << 32;
	}

	while ((int64_t)b > 0 && b < rem) {
		b = b+b;
		d = d+d;
	}

	do {
		if (rem >= b) {
			rem -= b;
			res += d;
		}
		b >>= 1;
		d >>= 1;
	} while (d);

	*n = res;
	return rem;
}
EXPORT_SYMBOL(__div64_32);
#endif

#ifndef div_s64_rem
s64 div_s64_rem(s64 dividend, s32 divisor, s32 *remainder)
{
	u64 quotient;

	if (dividend < 0) {
		quotient = div_u64_rem(-dividend, abs(divisor), (u32 *)remainder);
		*remainder = -*remainder;
		if (divisor > 0)
			quotient = -quotient;
	} else {
		quotient = div_u64_rem(dividend, abs(divisor), (u32 *)remainder);
		if (divisor < 0)
			quotient = -quotient;
	}
	return quotient;
}
EXPORT_SYMBOL(div_s64_rem);
#endif

/*
 * div64_u64_rem - unsigned 64bit divide with 64bit divisor and remainder
 * @dividend:	64bit dividend
 * @divisor:	64bit divisor
 * @remainder:  64bit remainder
 *
 * This implementation is a comparable to algorithm used by div64_u64.
 * But this operation, which includes math for calculating the remainder,
 * is kept distinct to avoid slowing down the div64_u64 operation on 32bit
 * systems.
 */
#ifndef div64_u64_rem
u64 div64_u64_rem(u64 dividend, u64 divisor, u64 *remainder)
{
	u32 high = divisor >> 32;
	u64 quot;

	if (high == 0) {
		u32 rem32;
		quot = div_u64_rem(dividend, divisor, &rem32);
		*remainder = rem32;
	} else {
		int n = fls(high);
		quot = div_u64(dividend >> n, divisor >> n);

		if (quot != 0)
			quot--;

		*remainder = dividend - quot * divisor;
		if (*remainder >= divisor) {
			quot++;
			*remainder -= divisor;
		}
	}

	return quot;
}
EXPORT_SYMBOL(div64_u64_rem);
#endif

/*
 * div64_u64 - unsigned 64bit divide with 64bit divisor
 * @dividend:	64bit dividend
 * @divisor:	64bit divisor
 *
 * This implementation is a modified version of the algorithm proposed
 * by the book 'Hacker's Delight'.  The original source and full proof
 * can be found here and is available for use without restriction.
 *
 * 'http://www.hackersdelight.org/hdcodetxt/divDouble.c.txt'
 */
#ifndef div64_u64
u64 div64_u64(u64 dividend, u64 divisor)
{
	u32 high = divisor >> 32;
	u64 quot;

	if (high == 0) {
		quot = div_u64(dividend, divisor);
	} else {
		int n = fls(high);
		quot = div_u64(dividend >> n, divisor >> n);

		if (quot != 0)
			quot--;
		if ((dividend - quot * divisor) >= divisor)
			quot++;
	}

	return quot;
}
EXPORT_SYMBOL(div64_u64);
#endif

#ifndef div64_s64
s64 div64_s64(s64 dividend, s64 divisor)
{
	s64 quot, t;

	quot = div64_u64(abs(dividend), abs(divisor));
	t = (dividend ^ divisor) >> 63;

	return (quot ^ t) - t;
}
EXPORT_SYMBOL(div64_s64);
#endif

#endif /* BITS_PER_LONG == 32 */

/*
 * Iterative div/mod for use when dividend is not expected to be much
 * bigger than divisor.
 */
#ifndef iter_div_u64_rem
u32 iter_div_u64_rem(u64 dividend, u32 divisor, u64 *remainder)
{
	return __iter_div_u64_rem(dividend, divisor, remainder);
}
EXPORT_SYMBOL(iter_div_u64_rem);
#endif

#if !defined(mul_u64_add_u64_div_u64) || defined(test_mul_u64_add_u64_div_u64)

#define mul_add(a, b, c) add_u64_u32(mul_u32_u32(a, b), c)

#if defined(__SIZEOF_INT128__) && !defined(test_mul_u64_add_u64_div_u64)
static inline u64 mul_u64_u64_add_u64(u64 *p_lo, u64 a, u64 b, u64 c)
{
	/* native 64x64=128 bits multiplication */
	u128 prod = (u128)a * b + c;

	*p_lo = prod;
	return prod >> 64;
}
#else
static inline u64 mul_u64_u64_add_u64(u64 *p_lo, u64 a, u64 b, u64 c)
{
	/* perform a 64x64=128 bits multiplication in 32bit chunks */
	u64 x, y, z;

	/* Since (x-1)(x-1) + 2(x-1) == x.x - 1 two u32 can be added to a u64 */
	x = mul_add(a, b, c);
	y = mul_add(a, b >> 32, c >> 32);
	y = add_u64_u32(y, x >> 32);
	z = mul_add(a >> 32, b >> 32, y >> 32);
	y = mul_add(a >> 32, b, y);
	*p_lo = (y << 32) + (u32)x;
	return add_u64_u32(z, y >> 32);
}
#endif

#ifndef BITS_PER_ITER
#define BITS_PER_ITER (__LONG_WIDTH__ >= 64 ? 32 : 16)
#endif

#if BITS_PER_ITER == 32
#define mul_u64_long_add_u64(p_lo, a, b, c) mul_u64_u64_add_u64(p_lo, a, b, c)
#define add_u64_long(a, b) ((a) + (b))
#else
#undef BITS_PER_ITER
#define BITS_PER_ITER 16
static inline u32 mul_u64_long_add_u64(u64 *p_lo, u64 a, u32 b, u64 c)
{
	u64 n_lo = mul_add(a, b, c);
	u64 n_med = mul_add(a >> 32, b, c >> 32);

	n_med = add_u64_u32(n_med, n_lo >> 32);
	*p_lo = n_med << 32 | (u32)n_lo;
	return n_med >> 32;
}

#define add_u64_long(a, b) add_u64_u32(a, b)
#endif

u64 mul_u64_add_u64_div_u64(u64 a, u64 b, u64 c, u64 d)
{
	unsigned long d_msig, q_digit;
	unsigned int reps, d_z_hi;
	u64 quotient, n_lo, n_hi;
	u32 overflow;

	n_hi = mul_u64_u64_add_u64(&n_lo, a, b, c);

	if (!n_hi)
		return div64_u64(n_lo, d);

	if (unlikely(n_hi >= d)) {
		/* trigger runtime exception if divisor is zero */
		if (d == 0) {
			unsigned long zero = 0;

			OPTIMIZER_HIDE_VAR(zero);
			return ~0UL/zero;
		}
		/* overflow: result is unrepresentable in a u64 */
		return ~0ULL;
	}

	/* Left align the divisor, shifting the dividend to match */
	d_z_hi = __builtin_clzll(d);
	if (d_z_hi) {
		d <<= d_z_hi;
		n_hi = n_hi << d_z_hi | n_lo >> (64 - d_z_hi);
		n_lo <<= d_z_hi;
	}

	reps = 64 / BITS_PER_ITER;
	/* Optimise loop count for small dividends */
	if (!(u32)(n_hi >> 32)) {
		reps -= 32 / BITS_PER_ITER;
		n_hi = n_hi << 32 | n_lo >> 32;
		n_lo <<= 32;
	}
#if BITS_PER_ITER == 16
	if (!(u32)(n_hi >> 48)) {
		reps--;
		n_hi = add_u64_u32(n_hi << 16, n_lo >> 48);
		n_lo <<= 16;
	}
#endif

	/* Invert the dividend so we can use add instead of subtract. */
	n_lo = ~n_lo;
	n_hi = ~n_hi;

	/*
	 * Get the most significant BITS_PER_ITER bits of the divisor.
	 * This is used to get a low 'guestimate' of the quotient digit.
	 */
	d_msig = (d >> (64 - BITS_PER_ITER)) + 1;

	/*
	 * Now do a 'long division' with BITS_PER_ITER bit 'digits'.
	 * The 'guess' quotient digit can be low and BITS_PER_ITER+1 bits.
	 * The worst case is dividing ~0 by 0x8000 which requires two subtracts.
	 */
	quotient = 0;
	while (reps--) {
		q_digit = (unsigned long)(~n_hi >> (64 - 2 * BITS_PER_ITER)) / d_msig;
		/* Shift 'n' left to align with the product q_digit * d */
		overflow = n_hi >> (64 - BITS_PER_ITER);
		n_hi = add_u64_u32(n_hi << BITS_PER_ITER, n_lo >> (64 - BITS_PER_ITER));
		n_lo <<= BITS_PER_ITER;
		/* Add product to negated divisor */
		overflow += mul_u64_long_add_u64(&n_hi, d, q_digit, n_hi);
		/* Adjust for the q_digit 'guestimate' being low */
		while (overflow < 0xffffffff >> (32 - BITS_PER_ITER)) {
			q_digit++;
			n_hi += d;
			overflow += n_hi < d;
		}
		quotient = add_u64_long(quotient << BITS_PER_ITER, q_digit);
	}

	/*
	 * The above only ensures the remainder doesn't overflow,
	 * it can still be possible to add (aka subtract) another copy
	 * of the divisor.
	 */
	if ((n_hi + d) > n_hi)
		quotient++;
	return quotient;
}
#if !defined(test_mul_u64_add_u64_div_u64)
EXPORT_SYMBOL(mul_u64_add_u64_div_u64);
#endif
#endif
