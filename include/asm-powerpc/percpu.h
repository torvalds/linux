#ifndef _ASM_POWERPC_PERCPU_H_
#define _ASM_POWERPC_PERCPU_H_
#ifdef __powerpc64__
#include <linux/compiler.h>

/*
 * Same as asm-generic/percpu.h, except that we store the per cpu offset
 * in the paca. Based on the x86-64 implementation.
 */

#ifdef CONFIG_SMP

#include <asm/paca.h>

#define __per_cpu_offset(cpu) (paca[cpu].data_offset)
#define __my_cpu_offset() get_paca()->data_offset

/* Separate out the type, so (int[3], foo) works. */
#define DEFINE_PER_CPU(type, name) \
    __attribute__((__section__(".data.percpu"))) __typeof__(type) per_cpu__##name

/* var is in discarded region: offset to particular copy we want */
#define per_cpu(var, cpu) (*RELOC_HIDE(&per_cpu__##var, __per_cpu_offset(cpu)))
#define __get_cpu_var(var) (*RELOC_HIDE(&per_cpu__##var, __my_cpu_offset()))

/* A macro to avoid #include hell... */
#define percpu_modcopy(pcpudst, src, size)			\
do {								\
	unsigned int __i;					\
	for (__i = 0; __i < NR_CPUS; __i++)			\
		if (cpu_possible(__i))				\
			memcpy((pcpudst)+__per_cpu_offset(__i),	\
			       (src), (size));			\
} while (0)

extern void setup_per_cpu_areas(void);

#else /* ! SMP */

#define DEFINE_PER_CPU(type, name) \
    __typeof__(type) per_cpu__##name

#define per_cpu(var, cpu)			(*((void)(cpu), &per_cpu__##var))
#define __get_cpu_var(var)			per_cpu__##var

#endif	/* SMP */

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) per_cpu__##name

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(per_cpu__##var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(per_cpu__##var)

#else
#include <asm-generic/percpu.h>
#endif

#endif /* _ASM_POWERPC_PERCPU_H_ */
