/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_ASM_GENERIC_BITOPS___FFS_H_
#define _TOOLS_LINUX_ASM_GENERIC_BITOPS___FFS_H_

#include <asm/types.h>
#include <asm/bitsperlong.h>

/**
 * __ffs - find first bit in word.
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static __always_inline unsigned int __ffs(unsigned long word)
{
	unsigned int num = 0;

#if __BITS_PER_LONG == 64
	if ((word & 0xffffffff) == 0) {
		num += 32;
		word >>= 32;
	}
#endif
	if ((word & 0xffff) == 0) {
		num += 16;
		word >>= 16;
	}
	if ((word & 0xff) == 0) {
		num += 8;
		word >>= 8;
	}
	if ((word & 0xf) == 0) {
		num += 4;
		word >>= 4;
	}
	if ((word & 0x3) == 0) {
		num += 2;
		word >>= 2;
	}
	if ((word & 0x1) == 0)
		num += 1;
	return num;
}

#endif /* _TOOLS_LINUX_ASM_GENERIC_BITOPS___FFS_H_ */
