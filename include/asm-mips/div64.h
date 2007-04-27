/*
 * Copyright (C) 2000, 2004  Maciej W. Rozycki
 * Copyright (C) 2003, 07 Ralf Baechle (ralf@linux-mips.org)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _ASM_DIV64_H
#define _ASM_DIV64_H

#include <linux/types.h>

#if (_MIPS_SZLONG == 32)

#include <asm/compiler.h>

/*
 * No traps on overflows for any of these...
 */

#define do_div64_32(res, high, low, base) ({ \
	unsigned long __quot, __mod; \
	unsigned long __cf, __tmp, __tmp2, __i; \
	\
	__asm__(".set	push\n\t" \
		".set	noat\n\t" \
		".set	noreorder\n\t" \
		"move	%2, $0\n\t" \
		"move	%3, $0\n\t" \
		"b	1f\n\t" \
		" li	%4, 0x21\n" \
		"0:\n\t" \
		"sll	$1, %0, 0x1\n\t" \
		"srl	%3, %0, 0x1f\n\t" \
		"or	%0, $1, %5\n\t" \
		"sll	%1, %1, 0x1\n\t" \
		"sll	%2, %2, 0x1\n" \
		"1:\n\t" \
		"bnez	%3, 2f\n\t" \
		" sltu	%5, %0, %z6\n\t" \
		"bnez	%5, 3f\n" \
		"2:\n\t" \
		" addiu	%4, %4, -1\n\t" \
		"subu	%0, %0, %z6\n\t" \
		"addiu	%2, %2, 1\n" \
		"3:\n\t" \
		"bnez	%4, 0b\n\t" \
		" srl	%5, %1, 0x1f\n\t" \
		".set	pop" \
		: "=&r" (__mod), "=&r" (__tmp), "=&r" (__quot), "=&r" (__cf), \
		  "=&r" (__i), "=&r" (__tmp2) \
		: "Jr" (base), "0" (high), "1" (low)); \
	\
	(res) = __quot; \
	__mod; })

#define do_div(n, base) ({ \
	unsigned long long __quot; \
	unsigned long __mod; \
	unsigned long long __div; \
	unsigned long __upper, __low, __high, __base; \
	\
	__div = (n); \
	__base = (base); \
	\
	__high = __div >> 32; \
	__low = __div; \
	__upper = __high; \
	\
	if (__high) \
		__asm__("divu	$0, %z2, %z3" \
			: "=h" (__upper), "=l" (__high) \
			: "Jr" (__high), "Jr" (__base) \
			: GCC_REG_ACCUM); \
	\
	__mod = do_div64_32(__low, __upper, __low, __base); \
	\
	__quot = __high; \
	__quot = __quot << 32 | __low; \
	(n) = __quot; \
	__mod; })

extern uint64_t div64_64(uint64_t dividend, uint64_t divisor);
#endif /* (_MIPS_SZLONG == 32) */

#if (_MIPS_SZLONG == 64)

/*
 * Hey, we're already 64-bit, no
 * need to play games..
 */
#define do_div(n, base) ({ \
	unsigned long __quot; \
	unsigned int __mod; \
	unsigned long __div; \
	unsigned int __base; \
	\
	__div = (n); \
	__base = (base); \
	\
	__mod = __div % __base; \
	__quot = __div / __base; \
	\
	(n) = __quot; \
	__mod; })

static inline uint64_t div64_64(uint64_t dividend, uint64_t divisor)
{
	return dividend / divisor;
}

#endif /* (_MIPS_SZLONG == 64) */

#endif /* _ASM_DIV64_H */
