/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_DIV64_H
#define _ASM_GENERIC_DIV64_H
/*
 * Copyright (C) 2003 Bernardo Innocenti <bernie@develer.com>
 * Based on former asm-ppc/div64.h and asm-m68knommu/div64.h
 *
 * Optimization for constant divisors on 32-bit machines:
 * Copyright (C) 2006-2015 Nicolas Pitre
 *
 * The semantics of do_div() is, in C++ notation, observing that the name
 * is a function-like macro and the n parameter has the semantics of a C++
 * reference:
 *
 * uint32_t do_div(uint64_t &n, uint32_t base)
 * {
 * 	uint32_t remainder = n % base;
 * 	n = n / base;
 * 	return remainder;
 * }
 *
 * NOTE: macro parameter n is evaluated multiple times,
 *       beware of side effects!
 */

#include <linux/types.h>
#include <linux/compiler.h>

#if BITS_PER_LONG == 64

/**
 * do_div - returns 2 values: calculate remainder and update new dividend
 * @n: uint64_t dividend (will be updated)
 * @base: uint32_t divisor
 *
 * Summary:
 * ``uint32_t remainder = n % base;``
 * ``n = n / base;``
 *
 * Return: (uint32_t)remainder
 *
 * NOTE: macro parameter @n is evaluated multiple times,
 * beware of side effects!
 */
# define do_div(n,base) ({					\
	uint32_t __base = (base);				\
	uint32_t __rem;						\
	__rem = ((uint64_t)(n)) % __base;			\
	(n) = ((uint64_t)(n)) / __base;				\
	__rem;							\
 })

#elif BITS_PER_LONG == 32

#include <linux/log2.h>

/*
 * If the divisor happens to be constant, we determine the appropriate
 * inverse at compile time to turn the division into a few inline
 * multiplications which ought to be much faster.
 *
 * (It is unfortunate that gcc doesn't perform all this internally.)
 */

#define __div64_const32(n, ___b)					\
({									\
	/*								\
	 * Multiplication by reciprocal of b: n / b = n * (p / b) / p	\
	 *								\
	 * We rely on the fact that most of this code gets optimized	\
	 * away at compile time due to constant propagation and only	\
	 * a few multiplication instructions should remain.		\
	 * Hence this monstrous macro (static inline doesn't always	\
	 * do the trick here).						\
	 */								\
	uint64_t ___res, ___x, ___t, ___m, ___n = (n);			\
	uint32_t ___p;							\
	bool ___bias = false;						\
									\
	/* determine MSB of b */					\
	___p = 1 << ilog2(___b);					\
									\
	/* compute m = ((p << 64) + b - 1) / b */			\
	___m = (~0ULL / ___b) * ___p;					\
	___m += (((~0ULL % ___b + 1) * ___p) + ___b - 1) / ___b;	\
									\
	/* one less than the dividend with highest result */		\
	___x = ~0ULL / ___b * ___b - 1;					\
									\
	/* test our ___m with res = m * x / (p << 64) */		\
	___res = (___m & 0xffffffff) * (___x & 0xffffffff);		\
	___t = (___m & 0xffffffff) * (___x >> 32) + (___res >> 32);	\
	___res = (___m >> 32) * (___x >> 32) + (___t >> 32);		\
	___t = (___m >> 32) * (___x & 0xffffffff) + (___t & 0xffffffff);\
	___res = (___res + (___t >> 32)) / ___p;			\
									\
	/* Now validate what we've got. */				\
	if (___res != ___x / ___b) {					\
		/*							\
		 * We can't get away without a bias to compensate	\
		 * for bit truncation errors.  To avoid it we'd need an	\
		 * additional bit to represent m which would overflow	\
		 * a 64-bit variable.					\
		 *							\
		 * Instead we do m = p / b and n / b = (n * m + m) / p.	\
		 */							\
		___bias = true;						\
		/* Compute m = (p << 64) / b */				\
		___m = (~0ULL / ___b) * ___p;				\
		___m += ((~0ULL % ___b + 1) * ___p) / ___b;		\
	}								\
									\
	/* Reduce m / p to help avoid overflow handling later. */	\
	___p /= (___m & -___m);						\
	___m /= (___m & -___m);						\
									\
	/*								\
	 * Perform (m_bias + m * n) / (1 << 64).			\
	 * From now on there will be actual runtime code generated.	\
	 */								\
	___res = __arch_xprod_64(___m, ___n, ___bias);			\
									\
	___res /= ___p;							\
})

#ifndef __arch_xprod_64
/*
 * Default C implementation for __arch_xprod_64()
 *
 * Prototype: uint64_t __arch_xprod_64(const uint64_t m, uint64_t n, bool bias)
 * Semantic:  retval = ((bias ? m : 0) + m * n) >> 64
 *
 * The product is a 128-bit value, scaled down to 64 bits.
 * Hoping for compile-time optimization of  conditional code.
 * Architectures may provide their own optimized assembly implementation.
 */
#ifdef CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE
static __always_inline
#else
static inline
#endif
uint64_t __arch_xprod_64(const uint64_t m, uint64_t n, bool bias)
{
	uint32_t m_lo = m;
	uint32_t m_hi = m >> 32;
	uint32_t n_lo = n;
	uint32_t n_hi = n >> 32;
	uint64_t x, y;

	/* Determine if overflow handling can be dispensed with. */
	bool no_ovf = __builtin_constant_p(m) &&
		      ((m >> 32) + (m & 0xffffffff) < 0x100000000);

	if (no_ovf) {
		x = (uint64_t)m_lo * n_lo + (bias ? m : 0);
		x >>= 32;
		x += (uint64_t)m_lo * n_hi;
		x += (uint64_t)m_hi * n_lo;
		x >>= 32;
		x += (uint64_t)m_hi * n_hi;
	} else {
		x = (uint64_t)m_lo * n_lo + (bias ? m_lo : 0);
		y = (uint64_t)m_lo * n_hi + (uint32_t)(x >> 32) + (bias ? m_hi : 0);
		x = (uint64_t)m_hi * n_hi + (uint32_t)(y >> 32);
		y = (uint64_t)m_hi * n_lo + (uint32_t)y;
		x += (uint32_t)(y >> 32);
	}

	return x;
}
#endif

#ifndef __div64_32
extern uint32_t __div64_32(uint64_t *dividend, uint32_t divisor);
#endif

/* The unnecessary pointer compare is there
 * to check for type safety (n must be 64bit)
 */
# define do_div(n,base) ({				\
	uint32_t __base = (base);			\
	uint32_t __rem;					\
	(void)(((typeof((n)) *)0) == ((uint64_t *)0));	\
	if (__builtin_constant_p(__base) &&		\
	    is_power_of_2(__base)) {			\
		__rem = (n) & (__base - 1);		\
		(n) >>= ilog2(__base);			\
	} else if (__builtin_constant_p(__base) &&	\
		   __base != 0) {			\
		uint32_t __res_lo, __n_lo = (n);	\
		(n) = __div64_const32(n, __base);	\
		/* the remainder can be computed with 32-bit regs */ \
		__res_lo = (n);				\
		__rem = __n_lo - __res_lo * __base;	\
	} else if (likely(((n) >> 32) == 0)) {		\
		__rem = (uint32_t)(n) % __base;		\
		(n) = (uint32_t)(n) / __base;		\
	} else {					\
		__rem = __div64_32(&(n), __base);	\
	}						\
	__rem;						\
 })

#else /* BITS_PER_LONG == ?? */

# error do_div() does not yet support the C64

#endif /* BITS_PER_LONG */

#endif /* _ASM_GENERIC_DIV64_H */
