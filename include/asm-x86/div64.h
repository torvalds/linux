#ifndef _ASM_X86_DIV64_H
#define _ASM_X86_DIV64_H

#ifdef CONFIG_X86_32

#include <linux/types.h>

/*
 * do_div() is NOT a C function. It wants to return
 * two values (the quotient and the remainder), but
 * since that doesn't work very well in C, what it
 * does is:
 *
 * - modifies the 64-bit dividend _in_place_
 * - returns the 32-bit remainder
 *
 * This ends up being the most efficient "calling
 * convention" on x86.
 */
#define do_div(n, base)						\
({								\
	unsigned long __upper, __low, __high, __mod, __base;	\
	__base = (base);					\
	asm("":"=a" (__low), "=d" (__high) : "A" (n));		\
	__upper = __high;					\
	if (__high) {						\
		__upper = __high % (__base);			\
		__high = __high / (__base);			\
	}							\
	asm("divl %2":"=a" (__low), "=d" (__mod)		\
	    : "rm" (__base), "0" (__low), "1" (__upper));	\
	asm("":"=A" (n) : "a" (__low), "d" (__high));		\
	__mod;							\
})

/*
 * (long)X = ((long long)divs) / (long)div
 * (long)rem = ((long long)divs) % (long)div
 *
 * Warning, this will do an exception if X overflows.
 */
#define div_long_long_rem(a, b, c) div_ll_X_l_rem(a, b, c)

static inline long div_ll_X_l_rem(long long divs, long div, long *rem)
{
	long dum2;
	asm("divl %2":"=a"(dum2), "=d"(*rem)
	    : "rm"(div), "A"(divs));

	return dum2;

}

static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
	union {
		u64 v64;
		u32 v32[2];
	} d = { dividend };
	u32 upper;

	upper = d.v32[1];
	d.v32[1] = 0;
	if (upper >= divisor) {
		d.v32[1] = upper / divisor;
		upper %= divisor;
	}
	asm ("divl %2" : "=a" (d.v32[0]), "=d" (*remainder) :
		"rm" (divisor), "0" (d.v32[0]), "1" (upper));
	return d.v64;
}
#define div_u64_rem	div_u64_rem

extern uint64_t div64_64(uint64_t dividend, uint64_t divisor);

#else
# include <asm-generic/div64.h>
#endif /* CONFIG_X86_32 */

#endif /* _ASM_X86_DIV64_H */
