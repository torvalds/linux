#ifndef __ARCH_SPARC64_PERCPU__
#define __ARCH_SPARC64_PERCPU__

#include <linux/compiler.h>

register unsigned long __local_per_cpu_offset asm("g5");

#ifdef CONFIG_SMP

extern void real_setup_per_cpu_areas(void);

extern unsigned long __per_cpu_base;
extern unsigned long __per_cpu_shift;
#define __per_cpu_offset(__cpu) \
	(__per_cpu_base + ((unsigned long)(__cpu) << __per_cpu_shift))
#define per_cpu_offset(x) (__per_cpu_offset(x))

#define __my_cpu_offset __local_per_cpu_offset

#else /* ! SMP */

#define real_setup_per_cpu_areas()		do { } while (0)

#endif	/* SMP */

#include <asm-generic/percpu.h>

#endif /* __ARCH_SPARC64_PERCPU__ */
