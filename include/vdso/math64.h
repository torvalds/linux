/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VDSO_MATH64_H
#define __VDSO_MATH64_H

static __always_inline u32
__iter_div_u64_rem(u64 dividend, u32 divisor, u64 *remainder)
{
	u32 ret = 0;

	while (dividend >= divisor) {
		/* The following asm() prevents the compiler from
		   optimising this loop into a modulo operation.  */
		asm("" : "+rm"(dividend));

		dividend -= divisor;
		ret++;
	}

	*remainder = dividend;

	return ret;
}

#endif /* __VDSO_MATH64_H */
