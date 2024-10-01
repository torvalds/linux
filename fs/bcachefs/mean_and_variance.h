/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MEAN_AND_VARIANCE_H_
#define MEAN_AND_VARIANCE_H_

#include <linux/types.h>
#include <linux/limits.h>
#include <linux/math.h>
#include <linux/math64.h>

#define SQRT_U64_MAX 4294967295ULL

/*
 * u128_u: u128 user mode, because not all architectures support a real int128
 * type
 *
 * We don't use this version in userspace, because in userspace we link with
 * Rust and rustc has issues with u128.
 */

#if defined(__SIZEOF_INT128__) && defined(__KERNEL__) && !defined(CONFIG_PARISC)

typedef struct {
	unsigned __int128 v;
} __aligned(16) u128_u;

static inline u128_u u64_to_u128(u64 a)
{
	return (u128_u) { .v = a };
}

static inline u64 u128_lo(u128_u a)
{
	return a.v;
}

static inline u64 u128_hi(u128_u a)
{
	return a.v >> 64;
}

static inline u128_u u128_add(u128_u a, u128_u b)
{
	a.v += b.v;
	return a;
}

static inline u128_u u128_sub(u128_u a, u128_u b)
{
	a.v -= b.v;
	return a;
}

static inline u128_u u128_shl(u128_u a, s8 shift)
{
	a.v <<= shift;
	return a;
}

static inline u128_u u128_square(u64 a)
{
	u128_u b = u64_to_u128(a);

	b.v *= b.v;
	return b;
}

#else

typedef struct {
	u64 hi, lo;
} __aligned(16) u128_u;

/* conversions */

static inline u128_u u64_to_u128(u64 a)
{
	return (u128_u) { .lo = a };
}

static inline u64 u128_lo(u128_u a)
{
	return a.lo;
}

static inline u64 u128_hi(u128_u a)
{
	return a.hi;
}

/* arithmetic */

static inline u128_u u128_add(u128_u a, u128_u b)
{
	u128_u c;

	c.lo = a.lo + b.lo;
	c.hi = a.hi + b.hi + (c.lo < a.lo);
	return c;
}

static inline u128_u u128_sub(u128_u a, u128_u b)
{
	u128_u c;

	c.lo = a.lo - b.lo;
	c.hi = a.hi - b.hi - (c.lo > a.lo);
	return c;
}

static inline u128_u u128_shl(u128_u i, s8 shift)
{
	u128_u r;

	r.lo = i.lo << (shift & 63);
	if (shift < 64)
		r.hi = (i.hi << (shift & 63)) | (i.lo >> (-shift & 63));
	else {
		r.hi = i.lo << (-shift & 63);
		r.lo = 0;
	}
	return r;
}

static inline u128_u u128_square(u64 i)
{
	u128_u r;
	u64  h = i >> 32, l = i & U32_MAX;

	r =             u128_shl(u64_to_u128(h*h), 64);
	r = u128_add(r, u128_shl(u64_to_u128(h*l), 32));
	r = u128_add(r, u128_shl(u64_to_u128(l*h), 32));
	r = u128_add(r,          u64_to_u128(l*l));
	return r;
}

#endif

static inline u128_u u64s_to_u128(u64 hi, u64 lo)
{
	u128_u c = u64_to_u128(hi);

	c = u128_shl(c, 64);
	c = u128_add(c, u64_to_u128(lo));
	return c;
}

u128_u u128_div(u128_u n, u64 d);

struct mean_and_variance {
	s64	n;
	s64	sum;
	u128_u	sum_squares;
};

/* expontentially weighted variant */
struct mean_and_variance_weighted {
	s64	mean;
	u64	variance;
};

/**
 * fast_divpow2() - fast approximation for n / (1 << d)
 * @n: numerator
 * @d: the power of 2 denominator.
 *
 * note: this rounds towards 0.
 */
static inline s64 fast_divpow2(s64 n, u8 d)
{
	return (n + ((n < 0) ? ((1 << d) - 1) : 0)) >> d;
}

/**
 * mean_and_variance_update() - update a mean_and_variance struct @s1 with a new sample @v1
 * and return it.
 * @s1: the mean_and_variance to update.
 * @v1: the new sample.
 *
 * see linked pdf equation 12.
 */
static inline void
mean_and_variance_update(struct mean_and_variance *s, s64 v)
{
	s->n++;
	s->sum += v;
	s->sum_squares = u128_add(s->sum_squares, u128_square(abs(v)));
}

s64 mean_and_variance_get_mean(struct mean_and_variance s);
u64 mean_and_variance_get_variance(struct mean_and_variance s1);
u32 mean_and_variance_get_stddev(struct mean_and_variance s);

void mean_and_variance_weighted_update(struct mean_and_variance_weighted *s,
		s64 v, bool initted, u8 weight);

s64 mean_and_variance_weighted_get_mean(struct mean_and_variance_weighted s,
		u8 weight);
u64 mean_and_variance_weighted_get_variance(struct mean_and_variance_weighted s,
		u8 weight);
u32 mean_and_variance_weighted_get_stddev(struct mean_and_variance_weighted s,
		u8 weight);

#endif // MEAN_AND_VAIRANCE_H_
