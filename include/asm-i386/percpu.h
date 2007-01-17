#ifndef __ARCH_I386_PERCPU__
#define __ARCH_I386_PERCPU__

#ifndef __ASSEMBLY__
#include <asm-generic/percpu.h>
#else

/*
 * PER_CPU finds an address of a per-cpu variable.
 *
 * Args:
 *    var - variable name
 *    cpu - 32bit register containing the current CPU number
 *
 * The resulting address is stored in the "cpu" argument.
 *
 * Example:
 *    PER_CPU(cpu_gdt_descr, %ebx)
 */
#ifdef CONFIG_SMP
#define PER_CPU(var, cpu) \
	movl __per_cpu_offset(,cpu,4), cpu;	\
	addl $per_cpu__/**/var, cpu;
#else /* ! SMP */
#define PER_CPU(var, cpu) \
	movl $per_cpu__/**/var, cpu;
#endif	/* SMP */

#endif /* !__ASSEMBLY__ */

#endif /* __ARCH_I386_PERCPU__ */
