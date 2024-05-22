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

#if defined(CONFIG_ARCH_SUPPORTS_INT128) && defined(__SIZEOF_INT128__)

#ifndef mul_u64_u32_add_u64_shr
static __always_inline u64 mul_u64_u32_add_u64_shr(u64 a, u32 mul, u64 b, unsigned int shift)
{
	return (u64)((((unsigned __int128)a * mul) + b) >> shift);
}
#endif /* mul_u64_u32_add_u64_shr */

#else

#ifndef mul_u64_u32_add_u64_shr
#ifndef mul_u32_u32
static inline u64 mul_u32_u32(u32 a, u32 b)
{
	return (u64)a * b;
}
#define mul_u32_u32 mul_u32_u32
#endif
static __always_inline u64 mul_u64_u32_add_u64_shr(u64 a, u32 mul, u64 b, unsigned int shift)
{
	u32 ah = a >> 32, al = a;
	bool ovf;
	u64 ret;

	ovf = __builtin_add_overflow(mul_u32_u32(al, mul), b, &ret);
	ret >>= shift;
	if (ovf && shift)
		ret += 1ULL << (64 - shift);
	if (ah)
		ret += mul_u32_u32(ah, mul) << (32 - shift);

	return ret;
}
#endif /* mul_u64_u32_add_u64_shr */

#endif

#endif /* __VDSO_MATH64_H */
