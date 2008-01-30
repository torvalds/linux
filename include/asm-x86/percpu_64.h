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
#define __my_cpu_offset() read_pda(data_offset)

#define per_cpu_offset(x) (__per_cpu_offset(x))

/* var is in discarded region: offset to particular copy we want */
#define per_cpu(var, cpu) (*({				\
	extern int simple_identifier_##var(void);	\
	RELOC_HIDE(&per_cpu__##var, __per_cpu_offset(cpu)); }))
#define __get_cpu_var(var) (*({				\
	extern int simple_identifier_##var(void);	\
	RELOC_HIDE(&per_cpu__##var, __my_cpu_offset()); }))
#define __raw_get_cpu_var(var) (*({			\
	extern int simple_identifier_##var(void);	\
	RELOC_HIDE(&per_cpu__##var, __my_cpu_offset()); }))

/* A macro to avoid #include hell... */
#define percpu_modcopy(pcpudst, src, size)			\
do {								\
	unsigned int __i;					\
	for_each_possible_cpu(__i)				\
		memcpy((pcpudst)+__per_cpu_offset(__i),		\
		       (src), (size));				\
} while (0)

extern void setup_per_cpu_areas(void);

#else /* ! SMP */

#define per_cpu(var, cpu)			(*((void)(cpu), &per_cpu__##var))
#define __get_cpu_var(var)			per_cpu__##var
#define __raw_get_cpu_var(var)			per_cpu__##var

#endif	/* SMP */

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) per_cpu__##name

#endif /* _ASM_X8664_PERCPU_H_ */
