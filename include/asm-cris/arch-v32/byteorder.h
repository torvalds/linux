#ifndef _ASM_CRIS_ARCH_BYTEORDER_H
#define _ASM_CRIS_ARCH_BYTEORDER_H

#include <asm/types.h>

extern __inline__ __const__ __u32
___arch__swab32(__u32 x)
{
	__asm__ __volatile__ ("swapwb %0" : "=r" (x) : "0" (x));
	return (x);
}

extern __inline__ __const__ __u16
___arch__swab16(__u16 x)
{
	__asm__ __volatile__ ("swapb %0" : "=r" (x) : "0" (x));
	return (x);
}

#endif /* _ASM_CRIS_ARCH_BYTEORDER_H */
