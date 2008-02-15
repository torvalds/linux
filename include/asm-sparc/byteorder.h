#ifndef _SPARC_BYTEORDER_H
#define _SPARC_BYTEORDER_H

#include <asm/types.h>
#include <asm/asi.h>

#ifdef __GNUC__

#ifdef CONFIG_SPARC32
#define __SWAB_64_THRU_32__
#endif

#ifdef CONFIG_SPARC64

static inline __u16 ___arch__swab16p(const __u16 *addr)
{
	__u16 ret;

	__asm__ __volatile__ ("lduha [%1] %2, %0"
			      : "=r" (ret)
			      : "r" (addr), "i" (ASI_PL));
	return ret;
}

static inline __u32 ___arch__swab32p(const __u32 *addr)
{
	__u32 ret;

	__asm__ __volatile__ ("lduwa [%1] %2, %0"
			      : "=r" (ret)
			      : "r" (addr), "i" (ASI_PL));
	return ret;
}

static inline __u64 ___arch__swab64p(const __u64 *addr)
{
	__u64 ret;

	__asm__ __volatile__ ("ldxa [%1] %2, %0"
			      : "=r" (ret)
			      : "r" (addr), "i" (ASI_PL));
	return ret;
}

#define __arch__swab16p(x) ___arch__swab16p(x)
#define __arch__swab32p(x) ___arch__swab32p(x)
#define __arch__swab64p(x) ___arch__swab64p(x)

#endif /* CONFIG_SPARC64 */

#define __BYTEORDER_HAS_U64__

#endif

#include <linux/byteorder/big_endian.h>

#endif /* _SPARC_BYTEORDER_H */
