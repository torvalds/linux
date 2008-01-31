#ifndef __ARCH_S390_PERCPU__
#define __ARCH_S390_PERCPU__

#include <linux/compiler.h>
#include <asm/lowcore.h>

/*
 * s390 uses its own implementation for per cpu data, the offset of
 * the cpu local data area is cached in the cpu's lowcore memory.
 * For 64 bit module code s390 forces the use of a GOT slot for the
 * address of the per cpu variable. This is needed because the module
 * may be more than 4G above the per cpu area.
 */
#if defined(__s390x__) && defined(MODULE)

#define SHIFT_PERCPU_PTR(ptr,offset) (({			\
	extern int simple_identifier_##var(void);	\
	unsigned long *__ptr;				\
	asm ( "larl %0, %1@GOTENT"		\
	    : "=a" (__ptr) : "X" (ptr) );		\
	(typeof(ptr))((*__ptr) + (offset));	}))

#else

#define SHIFT_PERCPU_PTR(ptr, offset) (({				\
	extern int simple_identifier_##var(void);		\
	unsigned long __ptr;					\
	asm ( "" : "=a" (__ptr) : "0" (ptr) );			\
	(typeof(ptr)) (__ptr + (offset)); }))

#endif

#define __my_cpu_offset S390_lowcore.percpu_offset

#include <asm-generic/percpu.h>

#endif /* __ARCH_S390_PERCPU__ */
