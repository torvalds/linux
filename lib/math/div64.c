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
u32 iter_div_u64_rem(u64 dividend, u32 divisor, u64 *remainder)
{
	return __iter_div_u64_rem(dividend, divisor, remainder);
}
EXPORT_SYMBOL(iter_div_u64_rem);

#ifndef mul_u64_u64_div_u64
u64 mul_u64_u64_div_u64(u64 a, u64 b, u64 c)
{
	if (ilog2(a) + ilog2(b) <= 62)
		return div64_u64(a * b, c);

#if defined(__SIZEOF_INT128__)

	/* native 64x64=128 bits multiplication */
	u128 prod = (u128)a * b;
	u64 n_lo = prod, n_hi = prod >> 64;

#else

	/* perform a 64x64=128 bits multiplication manually */
	u32 a_lo = a, a_hi = a >> 32, b_lo = b, b_hi = b >> 32;
	u64 x, y, z;

	x = (u64)a_lo * b_lo;
	y = (u64)a_lo * b_hi + (u32)(x >> 32);
	z = (u64)a_hi * b_hi + (u32)(y >> 32);
	y = (u64)a_hi * b_lo + (u32)y;
	z += (u32)(y >> 32);
	x = (y << 32) + (u32)x;

	u64 n_lo = x, n_hi = z;

#endif

	int shift = __builtin_ctzll(c);

	/* try reducing the fraction in case the dividend becomes <= 64 bits */
	if ((n_hi >> shift) == 0) {
		u64 n = (n_lo >> shift) | (n_hi << (64 - shift));

		return div64_u64(n, c >> shift);
		/*
		 * The remainder value if needed would be:
		 *   res = div64_u64_rem(n, c >> shift, &rem);
		 *   rem = (rem << shift) + (n_lo - (n << shift));
		 */
	}

	if (n_hi >= c) {
		/* overflow: result is unrepresentable in a u64 */
		return -1;
	}

	/* Do the full 128 by 64 bits division */

	shift = __builtin_clzll(c);
	c <<= shift;

	int p = 64 + shift;
	u64 res = 0;
	bool carry;

	do {
		carry = n_hi >> 63;
		shift = carry ? 1 : __builtin_clzll(n_hi);
		if (p < shift)
			break;
		p -= shift;
		n_hi <<= shift;
		n_hi |= n_lo >> (64 - shift);
		n_lo <<= shift;
		if (carry || (n_hi >= c)) {
			n_hi -= c;
			res |= 1ULL << p;
		}
	} while (n_hi);
	/* The remainder value if needed would be n_hi << p */

	return res;
}
EXPORT_SYMBOL(mul_u64_u64_div_u64);
#endif
