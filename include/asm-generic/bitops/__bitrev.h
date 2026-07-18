/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_BITOPS___BITREV_H_
#define _ASM_GENERIC_BITOPS___BITREV_H_

#include <asm/types.h>

extern u8 const byte_rev_table[256];
static __always_inline __attribute_const__ u8 generic___bitrev8(u8 byte)
{
	return byte_rev_table[byte];
}

static __always_inline __attribute_const__ u16 generic___bitrev16(u16 x)
{
	return (generic___bitrev8(x & 0xff) << 8) | generic___bitrev8(x >> 8);
}

static __always_inline __attribute_const__ u32 generic___bitrev32(u32 x)
{
	return (generic___bitrev16(x & 0xffff) << 16) | generic___bitrev16(x >> 16);
}

#endif /* _ASM_GENERIC_BITOPS___BITREV_H_ */
