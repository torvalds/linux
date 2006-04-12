#ifndef __ARCH_SPARC64_PERCPU__
#define __ARCH_SPARC64_PERCPU__

#include <linux/compiler.h>

#ifdef CONFIG_SMP

extern void setup_per_cpu_areas(void);

extern unsigned long __per_cpu_base;
extern unsigned long __per_cpu_shift;
#define __per_cpu_offset(__cpu) \
	(__per_cpu_base + ((unsigned long)(__cpu) << __per_cpu_shift))

/* Separate out the type, so (int[3], foo) works. */
#define DEFINE_PER_CPU(type, name) \
    __attribute__((__section__(".data.percpu"))) __typeof__(type) per_cpu__##name

register unsigned long __local_per_cpu_offset asm("g5");

/* var is in discarded region: offset to particular copy we want */
#define per_cpu(var, cpu) (*RELOC_HIDE(&per_cpu__##var, __per_cpu_offset(cpu)))
#define __get_cpu_var(var) (*RELOC_HIDE(&per_cpu__##var, __local_per_cpu_offset))

/* A macro to avoid #include hell... */
#define percpu_modcopy(pcpudst, src, size)			\
do {								\
	unsigned int __i;					\
	for_each_possible_cpu(__i)				\
		memcpy((pcpudst)+__per_cpu_offset(__i),		\
		       (src), (size));				\
} while (0)
#else /* ! SMP */

#define DEFINE_PER_CPU(type, name) \
    __typeof__(type) per_cpu__##name

#define per_cpu(var, cpu)			(*((void)cpu, &per_cpu__##var))
#define __get_cpu_var(var)			per_cpu__##var

#endif	/* SMP */

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) per_cpu__##name

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(per_cpu__##var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(per_cpu__##var)

#endif /* __ARCH_SPARC64_PERCPU__ */
