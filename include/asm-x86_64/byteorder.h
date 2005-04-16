#ifndef _X86_64_BYTEORDER_H
#define _X86_64_BYTEORDER_H

#include <asm/types.h>
#include <linux/compiler.h>

#ifdef __GNUC__

static __inline__ __attribute_const__ __u64 ___arch__swab64(__u64 x)
{
	__asm__("bswapq %0" : "=r" (x) : "0" (x));
	return x;
}

static __inline__ __attribute_const__ __u32 ___arch__swab32(__u32 x)
{
	__asm__("bswapl %0" : "=r" (x) : "0" (x));
	return x;
}

/* Do not define swab16.  Gcc is smart enough to recognize "C" version and
   convert it into rotation or exhange.  */

#define __arch__swab32(x) ___arch__swab32(x)
#define __arch__swab64(x) ___arch__swab64(x)

#endif /* __GNUC__ */

#define __BYTEORDER_HAS_U64__

#include <linux/byteorder/little_endian.h>

#endif /* _X86_64_BYTEORDER_H */
