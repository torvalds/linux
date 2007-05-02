#ifndef __ARCH_I386_PERCPU__
#define __ARCH_I386_PERCPU__

#ifdef __ASSEMBLY__

/*
 * PER_CPU finds an address of a per-cpu variable.
 *
 * Args:
 *    var - variable name
 *    reg - 32bit register
 *
 * The resulting address is stored in the "reg" argument.
 *
 * Example:
 *    PER_CPU(cpu_gdt_descr, %ebx)
 */
#ifdef CONFIG_SMP
#define PER_CPU(var, reg)				\
	movl %fs:per_cpu__##this_cpu_off, reg;		\
	lea per_cpu__##var(reg), reg
#define PER_CPU_VAR(var)	%fs:per_cpu__##var
#else /* ! SMP */
#define PER_CPU(var, reg)			\
	movl $per_cpu__##var, reg
#define PER_CPU_VAR(var)	per_cpu__##var
#endif	/* SMP */

#else /* ...!ASSEMBLY */

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
/* Same as generic implementation except for optimized local access. */
#define __GENERIC_PER_CPU

/* This is used for other cpus to find our section. */
extern unsigned long __per_cpu_offset[];

#define per_cpu_offset(x) (__per_cpu_offset[x])

/* Separate out the type, so (int[3], foo) works. */
#define DECLARE_PER_CPU(type, name) extern __typeof__(type) per_cpu__##name
#define DEFINE_PER_CPU(type, name) \
    __attribute__((__section__(".data.percpu"))) __typeof__(type) per_cpu__##name

/* We can use this directly for local CPU (faster). */
DECLARE_PER_CPU(unsigned long, this_cpu_off);

/* var is in discarded region: offset to particular copy we want */
#define per_cpu(var, cpu) (*({				\
	extern int simple_indentifier_##var(void);	\
	RELOC_HIDE(&per_cpu__##var, __per_cpu_offset[cpu]); }))

#define __raw_get_cpu_var(var) (*({					\
	extern int simple_indentifier_##var(void);			\
	RELOC_HIDE(&per_cpu__##var, x86_read_percpu(this_cpu_off));	\
}))

#define __get_cpu_var(var) __raw_get_cpu_var(var)

/* A macro to avoid #include hell... */
#define percpu_modcopy(pcpudst, src, size)			\
do {								\
	unsigned int __i;					\
	for_each_possible_cpu(__i)				\
		memcpy((pcpudst)+__per_cpu_offset[__i],		\
		       (src), (size));				\
} while (0)

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(per_cpu__##var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(per_cpu__##var)

/* fs segment starts at (positive) offset == __per_cpu_offset[cpu] */
#define __percpu_seg "%%fs:"
#else  /* !SMP */
#include <asm-generic/percpu.h>
#define __percpu_seg ""
#endif	/* SMP */

/* For arch-specific code, we can use direct single-insn ops (they
 * don't give an lvalue though). */
extern void __bad_percpu_size(void);

#define percpu_to_op(op,var,val)				\
	do {							\
		typedef typeof(var) T__;			\
		if (0) { T__ tmp__; tmp__ = (val); }		\
		switch (sizeof(var)) {				\
		case 1:						\
			asm(op "b %1,"__percpu_seg"%0"		\
			    : "+m" (var)			\
			    :"ri" ((T__)val));			\
			break;					\
		case 2:						\
			asm(op "w %1,"__percpu_seg"%0"		\
			    : "+m" (var)			\
			    :"ri" ((T__)val));			\
			break;					\
		case 4:						\
			asm(op "l %1,"__percpu_seg"%0"		\
			    : "+m" (var)			\
			    :"ri" ((T__)val));			\
			break;					\
		default: __bad_percpu_size();			\
		}						\
	} while (0)

#define percpu_from_op(op,var)					\
	({							\
		typeof(var) ret__;				\
		switch (sizeof(var)) {				\
		case 1:						\
			asm(op "b "__percpu_seg"%1,%0"		\
			    : "=r" (ret__)			\
			    : "m" (var));			\
			break;					\
		case 2:						\
			asm(op "w "__percpu_seg"%1,%0"		\
			    : "=r" (ret__)			\
			    : "m" (var));			\
			break;					\
		case 4:						\
			asm(op "l "__percpu_seg"%1,%0"		\
			    : "=r" (ret__)			\
			    : "m" (var));			\
			break;					\
		default: __bad_percpu_size();			\
		}						\
		ret__; })

#define x86_read_percpu(var) percpu_from_op("mov", per_cpu__##var)
#define x86_write_percpu(var,val) percpu_to_op("mov", per_cpu__##var, val)
#define x86_add_percpu(var,val) percpu_to_op("add", per_cpu__##var, val)
#define x86_sub_percpu(var,val) percpu_to_op("sub", per_cpu__##var, val)
#define x86_or_percpu(var,val) percpu_to_op("or", per_cpu__##var, val)
#endif /* !__ASSEMBLY__ */

#endif /* __ARCH_I386_PERCPU__ */
