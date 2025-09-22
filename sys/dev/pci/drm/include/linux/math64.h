/* Public domain. */

#ifndef _LINUX_MATH64_H
#define _LINUX_MATH64_H

#include <sys/types.h>
#include <asm/div64.h>

static inline uint64_t
div_u64(uint64_t x, uint32_t y)
{
	return (x / y);
}

static inline int64_t
div_s64(int64_t x, int64_t y)
{
	return (x / y);
}

static inline uint64_t
div64_u64(uint64_t x, uint64_t y)
{
	return (x / y);
}

static inline uint64_t
div64_u64_rem(uint64_t x, uint64_t y, uint64_t *rem)
{
	*rem = x % y;
	return (x / y);
}

static inline uint64_t
div_u64_rem(uint64_t x, uint32_t y, uint32_t *rem)
{
	*rem = x % y;
	return (x / y);
}

static inline int64_t
div64_s64(int64_t x, int64_t y)
{
	return (x / y);
}

static inline uint64_t
mul_u32_u32(uint32_t x, uint32_t y)
{
	return (uint64_t)x * y;
}

static inline uint64_t
mul_u64_u32_div(uint64_t x, uint32_t y, uint32_t div)
{
	return (x * y) / div;
}

#define DIV64_U64_ROUND_UP(x, y)	\
({					\
	uint64_t _t = (y);		\
	div64_u64((x) + _t - 1, _t);	\
})

static inline uint64_t
mul_u64_u32_shr(uint64_t x, uint32_t y, unsigned int shift)
{
	uint32_t hi, lo;
	hi = x >> 32;
	lo = x & 0xffffffff;

	return (mul_u32_u32(lo, y) >> shift) +
	    (mul_u32_u32(hi, y) << (32 - shift));
}

#endif
