#ifndef _M68K_BYTEORDER_H
#define _M68K_BYTEORDER_H

#include <asm/types.h>
#include <linux/compiler.h>

#ifdef __GNUC__

static __inline__ __attribute_const__ __u32 ___arch__swab32(__u32 val)
{
	__asm__("rolw #8,%0; swap %0; rolw #8,%0" : "=d" (val) : "0" (val));
	return val;
}
#define __arch__swab32(x) ___arch__swab32(x)

#endif

#if defined(__GNUC__) && !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif

#include <linux/byteorder/big_endian.h>

#endif /* _M68K_BYTEORDER_H */
