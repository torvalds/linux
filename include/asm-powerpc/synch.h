#ifndef _ASM_POWERPC_SYNCH_H 
#define _ASM_POWERPC_SYNCH_H 

#include <linux/config.h>

#ifdef __powerpc64__
#define __SUBARCH_HAS_LWSYNC
#endif

#ifdef __SUBARCH_HAS_LWSYNC
#    define LWSYNC	lwsync
#else
#    define LWSYNC	sync
#endif


/*
 * Arguably the bitops and *xchg operations don't imply any memory barrier
 * or SMP ordering, but in fact a lot of drivers expect them to imply
 * both, since they do on x86 cpus.
 */
#ifdef CONFIG_SMP
#define EIEIO_ON_SMP	"eieio\n"
#define ISYNC_ON_SMP	"\n\tisync"
#define SYNC_ON_SMP	__stringify(LWSYNC) "\n"
#else
#define EIEIO_ON_SMP
#define ISYNC_ON_SMP
#define SYNC_ON_SMP
#endif

static inline void eieio(void)
{
	__asm__ __volatile__ ("eieio" : : : "memory");
}

static inline void isync(void)
{
	__asm__ __volatile__ ("isync" : : : "memory");
}

#ifdef CONFIG_SMP
#define eieio_on_smp()	eieio()
#define isync_on_smp()	isync()
#else
#define eieio_on_smp()	__asm__ __volatile__("": : :"memory")
#define isync_on_smp()	__asm__ __volatile__("": : :"memory")
#endif

#endif	/* _ASM_POWERPC_SYNCH_H */

