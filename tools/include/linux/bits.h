/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_BITS_H
#define __LINUX_BITS_H

#include <linux/const.h>
#include <vdso/bits.h>
#include <uapi/linux/bits.h>
#include <asm/bitsperlong.h>

#define BIT_MASK(nr)		(UL(1) << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BIT_ULL_MASK(nr)	(ULL(1) << ((nr) % BITS_PER_LONG_LONG))
#define BIT_ULL_WORD(nr)	((nr) / BITS_PER_LONG_LONG)
#define BITS_PER_BYTE		8

/*
 * Create a contiguous bitmask starting at bit position @l and ending at
 * position @h. For example
 * GENMASK_ULL(39, 21) gives us the 64bit vector 0x000000ffffe00000.
 */
#if !defined(__ASSEMBLY__)
#include <linux/build_bug.h>
#include <linux/compiler.h>
#define GENMASK_INPUT_CHECK(h, l) BUILD_BUG_ON_ZERO(const_true((l) > (h)))
#else
/*
 * BUILD_BUG_ON_ZERO is not available in h files included from asm files,
 * disable the input check if that is the case.
 */
#define GENMASK_INPUT_CHECK(h, l) 0
#endif

#define GENMASK(h, l) \
	(GENMASK_INPUT_CHECK(h, l) + __GENMASK(h, l))
#define GENMASK_ULL(h, l) \
	(GENMASK_INPUT_CHECK(h, l) + __GENMASK_ULL(h, l))

#if !defined(__ASSEMBLY__)
/*
 * Missing asm support
 *
 * __GENMASK_U128() depends on _BIT128() which would not work
 * in the asm code, as it shifts an 'unsigned __int128' data
 * type instead of direct representation of 128 bit constants
 * such as long and unsigned long. The fundamental problem is
 * that a 128 bit constant will get silently truncated by the
 * gcc compiler.
 */
#define GENMASK_U128(h, l) \
	(GENMASK_INPUT_CHECK(h, l) + __GENMASK_U128(h, l))
#endif

#endif	/* __LINUX_BITS_H */
