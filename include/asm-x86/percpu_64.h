#ifndef _ASM_X8664_PERCPU_H_
#define _ASM_X8664_PERCPU_H_
#include <linux/compiler.h>

/* Same as asm-generic/percpu.h, except that we store the per cpu offset
   in the PDA. Longer term the PDA and every per cpu variable
   should be just put into a single section and referenced directly
   from %gs */

#ifdef CONFIG_SMP

#include <asm/pda.h>

#define __per_cpu_offset(cpu) (cpu_pda(cpu)->data_offset)
#define __my_cpu_offset read_pda(data_offset)

#define per_cpu_offset(x) (__per_cpu_offset(x))

/* A macro to avoid #include hell... */
#define percpu_modcopy(pcpudst, src, size)			\
do {								\
	unsigned int __i;					\
	for_each_possible_cpu(__i)				\
		memcpy((pcpudst)+__per_cpu_offset(__i),		\
		       (src), (size));				\
} while (0)

#endif	/* SMP */

#include <asm-generic/percpu.h>

#endif /* _ASM_X8664_PERCPU_H_ */
