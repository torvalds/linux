#ifndef _PPC_BYTEORDER_H
#define _PPC_BYTEORDER_H

#include <asm/types.h>
#include <linux/compiler.h>

#ifdef __GNUC__
#ifdef __KERNEL__

extern __inline__ unsigned ld_le16(const volatile unsigned short *addr)
{
	unsigned val;

	__asm__ __volatile__ ("lhbrx %0,0,%1" : "=r" (val) : "r" (addr), "m" (*addr));
	return val;
}

extern __inline__ void st_le16(volatile unsigned short *addr, const unsigned val)
{
	__asm__ __volatile__ ("sthbrx %1,0,%2" : "=m" (*addr) : "r" (val), "r" (addr));
}

extern __inline__ unsigned ld_le32(const volatile unsigned *addr)
{
	unsigned val;

	__asm__ __volatile__ ("lwbrx %0,0,%1" : "=r" (val) : "r" (addr), "m" (*addr));
	return val;
}

extern __inline__ void st_le32(volatile unsigned *addr, const unsigned val)
{
	__asm__ __volatile__ ("stwbrx %1,0,%2" : "=m" (*addr) : "r" (val), "r" (addr));
}

static __inline__ __attribute_const__ __u16 ___arch__swab16(__u16 value)
{
	__u16 result;

	__asm__("rlwimi %0,%2,8,16,23" : "=&r" (result) : "0" (value >> 8), "r" (value));
	return result;
}

static __inline__ __attribute_const__ __u32 ___arch__swab32(__u32 value)
{
	__u32 result;

	__asm__("rlwimi %0,%2,24,16,23" : "=&r" (result) : "0" (value>>24), "r" (value));
	__asm__("rlwimi %0,%2,8,8,15"   : "=&r" (result) : "0" (result),    "r" (value));
	__asm__("rlwimi %0,%2,24,0,7"   : "=&r" (result) : "0" (result),    "r" (value));

	return result;
}
#define __arch__swab32(x) ___arch__swab32(x)
#define __arch__swab16(x) ___arch__swab16(x)

/* The same, but returns converted value from the location pointer by addr. */
#define __arch__swab16p(addr) ld_le16(addr)
#define __arch__swab32p(addr) ld_le32(addr)

/* The same, but do the conversion in situ, ie. put the value back to addr. */
#define __arch__swab16s(addr) st_le16(addr,*addr)
#define __arch__swab32s(addr) st_le32(addr,*addr)

#endif /* __KERNEL__ */

#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif

#endif /* __GNUC__ */

#include <linux/byteorder/big_endian.h>

#endif /* _PPC_BYTEORDER_H */
