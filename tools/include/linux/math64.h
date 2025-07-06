/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MATH64_H
#define _LINUX_MATH64_H

#include <linux/types.h>

#ifdef __x86_64__
static inline u64 mul_u64_u64_div64(u64 a, u64 b, u64 c)
{
	u64 q;

	asm ("mulq %2; divq %3" : "=a" (q)
				: "a" (a), "rm" (b), "rm" (c)
				: "rdx");

	return q;
}
#define mul_u64_u64_div64 mul_u64_u64_div64
#endif

#ifdef __SIZEOF_INT128__
static inline u64 mul_u64_u32_shr(u64 a, u32 b, unsigned int shift)
{
	return (u64)(((unsigned __int128)a * b) >> shift);
}

#else

#ifdef __i386__
static inline u64 mul_u32_u32(u32 a, u32 b)
{
	u32 high, low;

	asm ("mull %[b]" : "=a" (low), "=d" (high)
			 : [a] "a" (a), [b] "rm" (b) );

	return low | ((u64)high) << 32;
}
#else
static inline u64 mul_u32_u32(u32 a, u32 b)
{
	return (u64)a * b;
}
#endif

static inline u64 mul_u64_u32_shr(u64 a, u32 b, unsigned int shift)
{
	u32 ah, al;
	u64 ret;

	al = a;
	ah = a >> 32;

	ret = mul_u32_u32(al, b) >> shift;
	if (ah)
		ret += mul_u32_u32(ah, b) << (32 - shift);

	return ret;
}

#endif	/* __SIZEOF_INT128__ */

#ifndef mul_u64_u64_div64
static inline u64 mul_u64_u64_div64(u64 a, u64 b, u64 c)
{
	u64 quot, rem;

	quot = a / c;
	rem = a % c;

	return quot * b + (rem * b) / c;
}
#endif

static inline u64 div_u64(u64 dividend, u32 divisor)
{
	return dividend / divisor;
}

#endif /* _LINUX_MATH64_H */
