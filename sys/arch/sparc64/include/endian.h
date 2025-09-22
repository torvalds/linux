/*	$OpenBSD: endian.h,v 1.7 2014/10/22 23:56:47 dlg Exp $	*/

#ifndef _MACHINE_ENDIAN_H_
#define _MACHINE_ENDIAN_H_

#define	_BYTE_ORDER _BIG_ENDIAN

#ifdef _KERNEL

#define __ASI_P_L	0x88 /* == ASI_PRIMARY_LITTLE */

static inline __uint16_t
__mswap16(volatile const __uint16_t *m)
{
	__uint16_t v;

	__asm("lduha [%1] %2, %0 ! %3"
	    : "=r" (v)
	    : "r" (m), "n" (__ASI_P_L), "m" (*m));

	return (v);
}

static inline __uint32_t
__mswap32(volatile const __uint32_t *m)
{
	__uint32_t v;

	__asm("lduwa [%1] %2, %0 ! %3"
	    : "=r" (v)
	    : "r" (m), "n" (__ASI_P_L), "m" (*m));

	return (v);
}

static inline __uint64_t
__mswap64(volatile const __uint64_t *m)
{
	__uint64_t v;

	__asm("ldxa [%1] %2, %0 ! %3"
	    : "=r" (v)
	    : "r" (m), "n" (__ASI_P_L), "m" (*m));

	return (v);
}

static inline void
__swapm16(volatile __uint16_t *m, __uint16_t v)
{
	__asm("stha %1, [%2] %3 ! %0"
	    : "=m" (*m)
	    : "r" (v), "r" (m), "n" (__ASI_P_L));
}

static inline void
__swapm32(volatile __uint32_t *m, __uint32_t v)
{
	__asm("stwa %1, [%2] %3 ! %0"
	    : "=m" (*m)
	    : "r" (v), "r" (m), "n" (__ASI_P_L));
}

static inline void
__swapm64(volatile __uint64_t *m, __uint64_t v)
{
	__asm("stxa %1, [%2] %3 ! %0"
	    : "=m" (*m)
	    : "r" (v), "r" (m), "n" (__ASI_P_L));
}

#undef __ASI_P_L

#define __HAVE_MD_SWAPIO

#endif  /* _KERNEL */

#define __STRICT_ALIGNMENT

#ifndef __FROM_SYS__ENDIAN
#include <sys/endian.h>
#endif

#endif /* _MACHINE_ENDIAN_H_ */
