#ifndef __ASM_ARM_DIV64
#define __ASM_ARM_DIV64

#include <asm/system.h>
#include <linux/types.h>

/*
 * The semantics of do_div() are:
 *
 * uint32_t do_div(uint64_t *n, uint32_t base)
 * {
 * 	uint32_t remainder = *n % base;
 * 	*n = *n / base;
 * 	return remainder;
 * }
 *
 * In other words, a 64-bit dividend with a 32-bit divisor producing
 * a 64-bit result and a 32-bit remainder.  To accomplish this optimally
 * we call a special __do_div64 helper with completely non standard
 * calling convention for arguments and results (beware).
 */

#ifdef __ARMEB__
#define __xh "r0"
#define __xl "r1"
#else
#define __xl "r0"
#define __xh "r1"
#endif

#define __do_div_asm(n, base)					\
({								\
	register unsigned int __base      asm("r4") = base;	\
	register unsigned long long __n   asm("r0") = n;	\
	register unsigned long long __res asm("r2");		\
	register unsigned int __rem       asm(__xh);		\
	asm(	__asmeq("%0", __xh)				\
		__asmeq("%1", "r2")				\
		__asmeq("%2", "r0")				\
		__asmeq("%3", "r4")				\
		"bl	__do_div64"				\
		: "=r" (__rem), "=r" (__res)			\
		: "r" (__n), "r" (__base)			\
		: "ip", "lr", "cc");				\
	n = __res;						\
	__rem;							\
})

#if __GNUC__ < 4

/*
 * gcc versions earlier than 4.0 are simply too problematic for the
 * optimized implementation below. First there is gcc PR 15089 that
 * tend to trig on more complex constructs, spurious .global __udivsi3
 * are inserted even if none of those symbols are referenced in the
 * generated code, and those gcc versions are not able to do constant
 * propagation on long long values anyway.
 */
#define do_div(n, base) __do_div_asm(n, base)

#elif __GNUC__ >= 4

#include <asm/bug.h>

/*
 * If the divisor happens to be constant, we determine the appropriate
 * inverse at compile time to turn the division into a few inline
 * multiplications instead which is much faster. And yet only if compiling
 * for ARMv4 or higher (we need umull/umlal) and if the gcc version is
 * sufficiently recent to perform proper long long constant propagation.
 * (It is unfortunate that gcc doesn't perform all this internally.)
 */
#define do_div(n, base)							\
({									\
	unsigned int __r, __b = (base);					\
	if (!__builtin_constant_p(__b) || __b == 0 ||			\
	    (__LINUX_ARM_ARCH__ < 4 && (__b & (__b - 1)) != 0)) {	\
		/* non-constant divisor (or zero): slow path */		\
		__r = __do_div_asm(n, __b);				\
	} else if ((__b & (__b - 1)) == 0) {				\
		/* Trivial: __b is constant and a power of 2 */		\
		/* gcc does the right thing with this code.  */		\
		__r = n;						\
		__r &= (__b - 1);					\
		n /= __b;						\
	} else {							\
		/* Multiply by inverse of __b: n/b = n*(p/b)/p       */	\
		/* We rely on the fact that most of this code gets   */	\
		/* optimized away at compile time due to constant    */	\
		/* propagation and only a couple inline assembly     */	\
		/* instructions should remain. Better avoid any      */	\
		/* code construct that might prevent that.           */	\
		unsigned long long __res, __x, __t, __m, __n = n;	\
		unsigned int __c, __p, __z = 0;				\
		/* preserve low part of n for reminder computation */	\
		__r = __n;						\
		/* determine number of bits to represent __b */		\
		__p = 1 << __div64_fls(__b);				\
		/* compute __m = ((__p << 64) + __b - 1) / __b */	\
		__m = (~0ULL / __b) * __p;				\
		__m += (((~0ULL % __b + 1) * __p) + __b - 1) / __b;	\
		/* compute __res = __m*(~0ULL/__b*__b-1)/(__p << 64) */	\
		__x = ~0ULL / __b * __b - 1;				\
		__res = (__m & 0xffffffff) * (__x & 0xffffffff);	\
		__res >>= 32;						\
		__res += (__m & 0xffffffff) * (__x >> 32);		\
		__t = __res;						\
		__res += (__x & 0xffffffff) * (__m >> 32);		\
		__t = (__res < __t) ? (1ULL << 32) : 0;			\
		__res = (__res >> 32) + __t;				\
		__res += (__m >> 32) * (__x >> 32);			\
		__res /= __p;						\
		/* Now sanitize and optimize what we've got. */		\
		if (~0ULL % (__b / (__b & -__b)) == 0) {		\
			/* those cases can be simplified with: */	\
			__n /= (__b & -__b);				\
			__m = ~0ULL / (__b / (__b & -__b));		\
			__p = 1;					\
			__c = 1;					\
		} else if (__res != __x / __b) {			\
			/* We can't get away without a correction    */	\
			/* to compensate for bit truncation errors.  */	\
			/* To avoid it we'd need an additional bit   */	\
			/* to represent __m which would overflow it. */	\
			/* Instead we do m=p/b and n/b=(n*m+m)/p.    */	\
			__c = 1;					\
			/* Compute __m = (__p << 64) / __b */		\
			__m = (~0ULL / __b) * __p;			\
			__m += ((~0ULL % __b + 1) * __p) / __b;		\
		} else {						\
			/* Reduce __m/__p, and try to clear bit 31   */	\
			/* of __m when possible otherwise that'll    */	\
			/* need extra overflow handling later.       */	\
			unsigned int __bits = -(__m & -__m);		\
			__bits |= __m >> 32;				\
			__bits = (~__bits) << 1;			\
			/* If __bits == 0 then setting bit 31 is     */	\
			/* unavoidable.  Simply apply the maximum    */	\
			/* possible reduction in that case.          */	\
			/* Otherwise the MSB of __bits indicates the */	\
			/* best reduction we should apply.           */	\
			if (!__bits) {					\
				__p /= (__m & -__m);			\
				__m /= (__m & -__m);			\
			} else {					\
				__p >>= __div64_fls(__bits);		\
				__m >>= __div64_fls(__bits);		\
			}						\
			/* No correction needed. */			\
			__c = 0;					\
		}							\
		/* Now we have a combination of 2 conditions:        */	\
		/* 1) whether or not we need a correction (__c), and */	\
		/* 2) whether or not there might be an overflow in   */	\
		/*    the cross product (__m & ((1<<63) | (1<<31)))  */	\
		/* Select the best insn combination to perform the   */	\
		/* actual __m * __n / (__p << 64) operation.         */	\
		if (!__c) {						\
			asm (	"umull	%Q0, %R0, %1, %Q2\n\t"		\
				"mov	%Q0, #0"			\
				: "=&r" (__res)				\
				: "r" (__m), "r" (__n)			\
				: "cc" );				\
		} else if (!(__m & ((1ULL << 63) | (1ULL << 31)))) {	\
			__res = __m;					\
			asm (	"umlal	%Q0, %R0, %Q1, %Q2\n\t"		\
				"mov	%Q0, #0"			\
				: "+r" (__res)				\
				: "r" (__m), "r" (__n)			\
				: "cc" );				\
		} else {						\
			asm (	"umull	%Q0, %R0, %Q1, %Q2\n\t"		\
				"cmn	%Q0, %Q1\n\t"			\
				"adcs	%R0, %R0, %R1\n\t"		\
				"adc	%Q0, %3, #0"			\
				: "=&r" (__res)				\
				: "r" (__m), "r" (__n), "r" (__z)	\
				: "cc" );				\
		}							\
		if (!(__m & ((1ULL << 63) | (1ULL << 31)))) {		\
			asm (	"umlal	%R0, %Q0, %R1, %Q2\n\t"		\
				"umlal	%R0, %Q0, %Q1, %R2\n\t"		\
				"mov	%R0, #0\n\t"			\
				"umlal	%Q0, %R0, %R1, %R2"		\
				: "+r" (__res)				\
				: "r" (__m), "r" (__n)			\
				: "cc" );				\
		} else {						\
			asm (	"umlal	%R0, %Q0, %R2, %Q3\n\t"		\
				"umlal	%R0, %1, %Q2, %R3\n\t"		\
				"mov	%R0, #0\n\t"			\
				"adds	%Q0, %1, %Q0\n\t"		\
				"adc	%R0, %R0, #0\n\t"		\
				"umlal	%Q0, %R0, %R2, %R3"		\
				: "+r" (__res), "+r" (__z)		\
				: "r" (__m), "r" (__n)			\
				: "cc" );				\
		}							\
		__res /= __p;						\
		/* The reminder can be computed with 32-bit regs     */	\
		/* only, and gcc is good at that.                    */	\
		{							\
			unsigned int __res0 = __res;			\
			unsigned int __b0 = __b;			\
			__r -= __res0 * __b0;				\
		}							\
		/* BUG_ON(__r >= __b || __res * __b + __r != n); */	\
		n = __res;						\
	}								\
	__r;								\
})

/* our own fls implementation to make sure constant propagation is fine */
#define __div64_fls(bits)						\
({									\
	unsigned int __left = (bits), __nr = 0;				\
	if (__left & 0xffff0000) __nr += 16, __left >>= 16;		\
	if (__left & 0x0000ff00) __nr +=  8, __left >>=  8;		\
	if (__left & 0x000000f0) __nr +=  4, __left >>=  4;		\
	if (__left & 0x0000000c) __nr +=  2, __left >>=  2;		\
	if (__left & 0x00000002) __nr +=  1;				\
	__nr;								\
})

#endif

#endif
