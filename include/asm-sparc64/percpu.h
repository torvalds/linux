#ifndef __ARCH_SPARC64_PERCPU__
#define __ARCH_SPARC64_PERCPU__

#include <linux/compiler.h>

register unsigned long __local_per_cpu_offset asm("g5");

#ifdef CONFIG_SMP

#define setup_per_cpu_areas()			do { } while (0)
extern void real_setup_per_cpu_areas(void);

extern unsigned long __per_cpu_base;
extern unsigned long __per_cpu_shift;
#define __per_cpu_offset(__cpu) \
	(__per_cpu_base + ((unsigned long)(__cpu) << __per_cpu_shift))
#define per_cpu_offset(x) (__per_cpu_offset(x))

/* var is in discarded region: offset to particular copy we want */
#define per_cpu(var, cpu) (*RELOC_HIDE(&per_cpu__##var, __per_cpu_offset(cpu)))
#define __get_cpu_var(var) (*RELOC_HIDE(&per_cpu__##var, __local_per_cpu_offset))
#define __raw_get_cpu_var(var) (*RELOC_HIDE(&per_cpu__##var, __local_per_cpu_offset))

/* A macro to avoid #include hell... */
#define percpu_modcopy(pcpudst, src, size)			\
do {								\
	unsigned int __i;					\
	for_each_possible_cpu(__i)				\
		memcpy((pcpudst)+__per_cpu_offset(__i),		\
		       (src), (size));				\
} while (0)
#else /* ! SMP */

#define real_setup_per_cpu_areas()		do { } while (0)

#define per_cpu(var, cpu)			(*((void)cpu, &per_cpu__##var))
#define __get_cpu_var(var)			per_cpu__##var
#define __raw_get_cpu_var(var)			per_cpu__##var

#endif	/* SMP */

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) per_cpu__##name

#endif /* __ARCH_SPARC64_PERCPU__ */
