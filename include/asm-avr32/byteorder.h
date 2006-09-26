/*
 * AVR32 endian-conversion functions.
 */
#ifndef __ASM_AVR32_BYTEORDER_H
#define __ASM_AVR32_BYTEORDER_H

#include <asm/types.h>
#include <linux/compiler.h>

#ifdef __CHECKER__
extern unsigned long __builtin_bswap_32(unsigned long x);
extern unsigned short __builtin_bswap_16(unsigned short x);
#endif

#define __arch__swab32(x) __builtin_bswap_32(x)
#define __arch__swab16(x) __builtin_bswap_16(x)

#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
# define __BYTEORDER_HAS_U64__
# define __SWAB_64_THRU_32__
#endif

#include <linux/byteorder/big_endian.h>

#endif /* __ASM_AVR32_BYTEORDER_H */
