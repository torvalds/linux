#ifndef _BLACKFIN_BYTEORDER_H
#define _BLACKFIN_BYTEORDER_H

#include <asm/types.h>
#include <linux/compiler.h>

#ifdef __GNUC__

static __inline__ __attribute_const__ __u32 ___arch__swahb32(__u32 xx)
{
	__u32 tmp;
	__asm__("%1 = %0 >> 8 (V);\n\t"
		"%0 = %0 << 8 (V);\n\t"
		"%0 = %0 | %1;\n\t"
		: "+d"(xx), "=&d"(tmp));
	return xx;
}

static __inline__ __attribute_const__ __u32 ___arch__swahw32(__u32 xx)
{
	__u32 rv;
	__asm__("%0 = PACK(%1.L, %1.H);\n\t": "=d"(rv): "d"(xx));
	return rv;
}

#define __arch__swahb32(x) ___arch__swahb32(x)
#define __arch__swahw32(x) ___arch__swahw32(x)
#define __arch__swab32(x) ___arch__swahb32(___arch__swahw32(x))

static __inline__ __attribute_const__ __u16 ___arch__swab16(__u16 xx)
{
	__u32 xw = xx;
	__asm__("%0 <<= 8;\n	%0.L = %0.L + %0.H (NS);\n": "+d"(xw));
	return (__u16)xw;
}

#define __arch__swab16(x) ___arch__swab16(x)

#endif

#if defined(__GNUC__) && !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif

#include <linux/byteorder/little_endian.h>

#endif				/* _BLACKFIN_BYTEORDER_H */
