#ifndef _ASM_POWERPC_SYNCH_H 
#define _ASM_POWERPC_SYNCH_H 
#ifdef __KERNEL__

#include <linux/stringify.h>

#ifdef __powerpc64__
#define __SUBARCH_HAS_LWSYNC
#endif

#ifdef __SUBARCH_HAS_LWSYNC
#    define LWSYNC	lwsync
#else
#    define LWSYNC	sync
#endif

#ifdef CONFIG_SMP
#define ISYNC_ON_SMP	"\n\tisync\n"
#define LWSYNC_ON_SMP	__stringify(LWSYNC) "\n"
#else
#define ISYNC_ON_SMP
#define LWSYNC_ON_SMP
#endif

static inline void eieio(void)
{
	__asm__ __volatile__ ("eieio" : : : "memory");
}

static inline void isync(void)
{
	__asm__ __volatile__ ("isync" : : : "memory");
}

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_SYNCH_H */
