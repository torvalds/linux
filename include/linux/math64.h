/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MATH64_H
#define _LINUX_MATH64_H

#include <linux/types.h>
#include <linux/math.h>
#include <asm/div64.h>
#include <vdso/math64.h>

#if BITS_PER_LONG == 64

#define div64_long(x, y) div64_s64((x), (y))
#define div64_ul(x, y)   div64_u64((x), (y))

/**
 * div_u64_rem - unsigned 64bit divide with 32bit divisor with remainder
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 32bit divisor
 * @remainder: pointer to unsigned 32bit remainder
 *
 * Return: sets ``*remainder``, then returns dividend / divisor
 *
 * This is commonly provided by 32bit archs to provide an optimized 64bit
 * divide.
 */
static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}

/**
 * div_s64_rem - signed 64bit divide with 32bit divisor with remainder
 * @dividend: signed 64bit dividend
 * @divisor: signed 32bit divisor
 * @remainder: pointer to signed 32bit remainder
 *
 * Return: sets ``*remainder``, then returns dividend / divisor
 */
static inline s64 div_s64_rem(s64 dividend, s32 divisor, s32 *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}

/**
 * div64_u64_rem - unsigned 64bit divide with 64bit divisor and remainder
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 64bit divisor
 * @remainder: pointer to unsigned 64bit remainder
 *
 * Return: sets ``*remainder``, then returns dividend / divisor
 */
static inline u64 div64_u64_rem(u64 dividend, u64 divisor, u64 *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}

/**
 * div64_u64 - unsigned 64bit divide with 64bit divisor
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 64bit divisor
 *
 * Return: dividend / divisor
 */
static inline u64 div64_u64(u64 dividend, u64 divisor)
{
	return dividend / divisor;
}

/**
 * div64_s64 - signed 64bit divide with 64bit divisor
 * @dividend: signed 64bit dividend
 * @divisor: signed 64bit divisor
 *
 * Return: dividend / divisor
 */
static inline s64 div64_s64(s64 dividend, s64 divisor)
{
	return dividend / divisor;
}

#elif BITS_PER_LONG == 32

#define div64_long(x, y) div_s64((x), (y))
#define div64_ul(x, y)   div_u64((x), (y))

#ifndef div_u64_rem
static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
	*remainder = do_div(dividend, divisor);
	return dividend;
}
#endif

#ifndef div_s64_rem
extern s64 div_s64_rem(s64 dividend, s32 divisor, s32 *remainder);
#endif

#ifndef div64_u64_rem
extern u64 div64_u64_rem(u64 dividend, u64 divisor, u64 *remainder);
#endif

#ifndef div64_u64
extern u64 div64_u64(u64 dividend, u64 divisor);
#endif

#ifndef div64_s64
extern s64 div64_s64(s64 dividend, s64 divisor);
#endif

#endif /* BITS_PER_LONG */

/**
 * div_u64 - unsigned 64bit divide with 32bit divisor
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 32bit divisor
 *
 * This is the most common 64bit divide and should be used if possible,
 * as many 32bit archs can optimize this variant better than a full 64bit
 * divide.
 *
 * Return: dividend / divisor
 */
#ifndef div_u64
static inline u64 div_u64(u64 dividend, u32 divisor)
{
	u32 remainder;
	return div_u64_rem(dividend, divisor, &remainder);
}
#endif

/**
 * div_s64 - signed 64bit divide with 32bit divisor
 * @dividend: signed 64bit dividend
 * @divisor: signed 32bit divisor
 *
 * Return: dividend / divisor
 */
#ifndef div_s64
static inline s64 div_s64(s64 dividend, s32 divisor)
{
	s32 remainder;
	return div_s64_rem(dividend, divisor, &remainder);
}
#endif

u32 iter_div_u64_rem(u64 dividend, u32 divisor, u64 *remainder);

#ifndef mul_u32_u32
/*
 * Many a GCC version messes this up and generates a 64x64 mult :-(
 */
static inline u64 mul_u32_u32(u32 a, u32 b)
{
	return (u64)a * b;
}
#endif

#if defined(CONFIG_ARCH_SUPPORTS_INT128) && defined(__SIZEOF_INT128__)

#ifndef mul_u64_u32_shr
static __always_inline u64 mul_u64_u32_shr(u64 a, u32 mul, unsigned int shift)
{
	return (u64)(((unsigned __int128)a * mul) >> shift);
}
#endif /* mul_u64_u32_shr */

#ifndef mul_u64_u64_shr
static __always_inline u64 mul_u64_u64_shr(u64 a, u64 mul, unsigned int shift)
{
	return (u64)(((unsigned __int128)a * mul) >> shift);
}
#endif /* mul_u64_u64_shr */

#else

#ifndef mul_u64_u32_shr
static __always_inline u64 mul_u64_u32_shr(u64 a, u32 mul, unsigned int shift)
{
	u32 ah = a >> 32, al = a;
	u64 ret;

	ret = mul_u32_u32(al, mul) >> shift;
	if (ah)
		ret += mul_u32_u32(ah, mul) << (32 - shift);
	return ret;
}
#endif /* mul_u64_u32_shr */

#ifndef mul_u64_u64_shr
static inline u64 mul_u64_u64_shr(u64 a, u64 b, unsigned int shift)
{
	union {
		u64 ll;
		struct {
#ifdef __BIG_ENDIAN
			u32 high, low;
#else
			u32 low, high;
#endif
		} l;
	} rl, rm, rn, rh, a0, b0;
	u64 c;

	a0.ll = a;
	b0.ll = b;

	rl.ll = mul_u32_u32(a0.l.low, b0.l.low);
	rm.ll = mul_u32_u32(a0.l.low, b0.l.high);
	rn.ll = mul_u32_u32(a0.l.high, b0.l.low);
	rh.ll = mul_u32_u32(a0.l.high, b0.l.high);

	/*
	 * Each of these lines computes a 64-bit intermediate result into "c",
	 * starting at bits 32-95.  The low 32-bits go into the result of the
	 * multiplication, the high 32-bits are carried into the next step.
	 */
	rl.l.high = c = (u64)rl.l.high + rm.l.low + rn.l.low;
	rh.l.low = c = (c >> 32) + rm.l.high + rn.l.high + rh.l.low;
	rh.l.high = (c >> 32) + rh.l.high;

	/*
	 * The 128-bit result of the multiplication is in rl.ll and rh.ll,
	 * shift it right and throw away the high part of the result.
	 */
	if (shift == 0)
		return rl.ll;
	if (shift < 64)
		return (rl.ll >> shift) | (rh.ll << (64 - shift));
	return rh.ll >> (shift & 63);
}
#endif /* mul_u64_u64_shr */

#endif

#ifndef mul_s64_u64_shr
static inline u64 mul_s64_u64_shr(s64 a, u64 b, unsigned int shift)
{
	u64 ret;

	/*
	 * Extract the sign before the multiplication and put it back
	 * afterwards if needed.
	 */
	ret = mul_u64_u64_shr(abs(a), b, shift);

	if (a < 0)
		ret = -((s64) ret);

	return ret;
}
#endif /* mul_s64_u64_shr */

#ifndef mul_u64_u32_div
static inline u64 mul_u64_u32_div(u64 a, u32 mul, u32 divisor)
{
	union {
		u64 ll;
		struct {
#ifdef __BIG_ENDIAN
			u32 high, low;
#else
			u32 low, high;
#endif
		} l;
	} u, rl, rh;

	u.ll = a;
	rl.ll = mul_u32_u32(u.l.low, mul);
	rh.ll = mul_u32_u32(u.l.high, mul) + rl.l.high;

	/* Bits 32-63 of the result will be in rh.l.low. */
	rl.l.high = do_div(rh.ll, divisor);

	/* Bits 0-31 of the result will be in rl.l.low.	*/
	do_div(rl.ll, divisor);

	rl.l.high = rh.l.low;
	return rl.ll;
}
#endif /* mul_u64_u32_div */

u64 mul_u64_u64_div_u64(u64 a, u64 mul, u64 div);

/**
 * DIV64_U64_ROUND_UP - unsigned 64bit divide with 64bit divisor rounded up
 * @ll: unsigned 64bit dividend
 * @d: unsigned 64bit divisor
 *
 * Divide unsigned 64bit dividend by unsigned 64bit divisor
 * and round up.
 *
 * Return: dividend / divisor rounded up
 */
#define DIV64_U64_ROUND_UP(ll, d)	\
	({ u64 _tmp = (d); div64_u64((ll) + _tmp - 1, _tmp); })

/**
 * DIV_U64_ROUND_UP - unsigned 64bit divide with 32bit divisor rounded up
 * @ll: unsigned 64bit dividend
 * @d: unsigned 32bit divisor
 *
 * Divide unsigned 64bit dividend by unsigned 32bit divisor
 * and round up.
 *
 * Return: dividend / divisor rounded up
 */
#define DIV_U64_ROUND_UP(ll, d)		\
	({ u32 _tmp = (d); div_u64((ll) + _tmp - 1, _tmp); })

/**
 * DIV64_U64_ROUND_CLOSEST - unsigned 64bit divide with 64bit divisor rounded to nearest integer
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 64bit divisor
 *
 * Divide unsigned 64bit dividend by unsigned 64bit divisor
 * and round to closest integer.
 *
 * Return: dividend / divisor rounded to nearest integer
 */
#define DIV64_U64_ROUND_CLOSEST(dividend, divisor)	\
	({ u64 _tmp = (divisor); div64_u64((dividend) + _tmp / 2, _tmp); })

/**
 * DIV_U64_ROUND_CLOSEST - unsigned 64bit divide with 32bit divisor rounded to nearest integer
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 32bit divisor
 *
 * Divide unsigned 64bit dividend by unsigned 32bit divisor
 * and round to closest integer.
 *
 * Return: dividend / divisor rounded to nearest integer
 */
#define DIV_U64_ROUND_CLOSEST(dividend, divisor)	\
	({ u32 _tmp = (divisor); div_u64((u64)(dividend) + _tmp / 2, _tmp); })

/**
 * DIV_S64_ROUND_CLOSEST - signed 64bit divide with 32bit divisor rounded to nearest integer
 * @dividend: signed 64bit dividend
 * @divisor: signed 32bit divisor
 *
 * Divide signed 64bit dividend by signed 32bit divisor
 * and round to closest integer.
 *
 * Return: dividend / divisor rounded to nearest integer
 */
#define DIV_S64_ROUND_CLOSEST(dividend, divisor)(	\
{							\
	s64 __x = (dividend);				\
	s32 __d = (divisor);				\
	((__x > 0) == (__d > 0)) ?			\
		div_s64((__x + (__d / 2)), __d) :	\
		div_s64((__x - (__d / 2)), __d);	\
}							\
)

/**
 * roundup_u64 - Round up a 64bit value to the next specified 32bit multiple
 * @x: the value to up
 * @y: 32bit multiple to round up to
 *
 * Rounds @x to the next multiple of @y. For 32bit @x values, see roundup and
 * the faster round_up() for powers of 2.
 *
 * Return: rounded up value.
 */
static inline u64 roundup_u64(u64 x, u32 y)
{
	return DIV_U64_ROUND_UP(x, y) * y;
}
#endif /* _LINUX_MATH64_H */
