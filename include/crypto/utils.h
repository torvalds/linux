/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cryptographic utilities
 *
 * Copyright (c) 2023 Herbert Xu <herbert@gondor.apana.org.au>
 */
#ifndef _CRYPTO_UTILS_H
#define _CRYPTO_UTILS_H

#include <asm/unaligned.h>
#include <linux/compiler_attributes.h>
#include <linux/types.h>

void __crypto_xor(u8 *dst, const u8 *src1, const u8 *src2, unsigned int size);

static inline void crypto_xor(u8 *dst, const u8 *src, unsigned int size)
{
	if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) &&
	    __builtin_constant_p(size) &&
	    (size % sizeof(unsigned long)) == 0) {
		unsigned long *d = (unsigned long *)dst;
		unsigned long *s = (unsigned long *)src;
		unsigned long l;

		while (size > 0) {
			l = get_unaligned(d) ^ get_unaligned(s++);
			put_unaligned(l, d++);
			size -= sizeof(unsigned long);
		}
	} else {
		__crypto_xor(dst, dst, src, size);
	}
}

static inline void crypto_xor_cpy(u8 *dst, const u8 *src1, const u8 *src2,
				  unsigned int size)
{
	if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) &&
	    __builtin_constant_p(size) &&
	    (size % sizeof(unsigned long)) == 0) {
		unsigned long *d = (unsigned long *)dst;
		unsigned long *s1 = (unsigned long *)src1;
		unsigned long *s2 = (unsigned long *)src2;
		unsigned long l;

		while (size > 0) {
			l = get_unaligned(s1++) ^ get_unaligned(s2++);
			put_unaligned(l, d++);
			size -= sizeof(unsigned long);
		}
	} else {
		__crypto_xor(dst, src1, src2, size);
	}
}

noinline unsigned long __crypto_memneq(const void *a, const void *b, size_t size);

/**
 * crypto_memneq - Compare two areas of memory without leaking
 *		   timing information.
 *
 * @a: One area of memory
 * @b: Another area of memory
 * @size: The size of the area.
 *
 * Returns 0 when data is equal, 1 otherwise.
 */
static inline int crypto_memneq(const void *a, const void *b, size_t size)
{
	return __crypto_memneq(a, b, size) != 0UL ? 1 : 0;
}

#endif	/* _CRYPTO_UTILS_H */
