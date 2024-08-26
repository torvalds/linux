// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Crypto library utility functions
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <asm/unaligned.h>
#include <crypto/utils.h>
#include <linux/module.h>

/*
 * XOR @len bytes from @src1 and @src2 together, writing the result to @dst
 * (which may alias one of the sources).  Don't call this directly; call
 * crypto_xor() or crypto_xor_cpy() instead.
 */
void __crypto_xor(u8 *dst, const u8 *src1, const u8 *src2, unsigned int len)
{
	int relalign = 0;

	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)) {
		int size = sizeof(unsigned long);
		int d = (((unsigned long)dst ^ (unsigned long)src1) |
			 ((unsigned long)dst ^ (unsigned long)src2)) &
			(size - 1);

		relalign = d ? 1 << __ffs(d) : size;

		/*
		 * If we care about alignment, process as many bytes as
		 * needed to advance dst and src to values whose alignments
		 * equal their relative alignment. This will allow us to
		 * process the remainder of the input using optimal strides.
		 */
		while (((unsigned long)dst & (relalign - 1)) && len > 0) {
			*dst++ = *src1++ ^ *src2++;
			len--;
		}
	}

	while (IS_ENABLED(CONFIG_64BIT) && len >= 8 && !(relalign & 7)) {
		if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)) {
			u64 l = get_unaligned((u64 *)src1) ^
				get_unaligned((u64 *)src2);
			put_unaligned(l, (u64 *)dst);
		} else {
			*(u64 *)dst = *(u64 *)src1 ^ *(u64 *)src2;
		}
		dst += 8;
		src1 += 8;
		src2 += 8;
		len -= 8;
	}

	while (len >= 4 && !(relalign & 3)) {
		if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)) {
			u32 l = get_unaligned((u32 *)src1) ^
				get_unaligned((u32 *)src2);
			put_unaligned(l, (u32 *)dst);
		} else {
			*(u32 *)dst = *(u32 *)src1 ^ *(u32 *)src2;
		}
		dst += 4;
		src1 += 4;
		src2 += 4;
		len -= 4;
	}

	while (len >= 2 && !(relalign & 1)) {
		if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)) {
			u16 l = get_unaligned((u16 *)src1) ^
				get_unaligned((u16 *)src2);
			put_unaligned(l, (u16 *)dst);
		} else {
			*(u16 *)dst = *(u16 *)src1 ^ *(u16 *)src2;
		}
		dst += 2;
		src1 += 2;
		src2 += 2;
		len -= 2;
	}

	while (len--)
		*dst++ = *src1++ ^ *src2++;
}
EXPORT_SYMBOL_GPL(__crypto_xor);

MODULE_DESCRIPTION("Crypto library utility functions");
MODULE_LICENSE("GPL");
