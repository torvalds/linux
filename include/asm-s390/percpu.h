#ifndef __ARCH_S390_PERCPU__
#define __ARCH_S390_PERCPU__

#include <linux/compiler.h>
#include <asm/lowcore.h>

#define __GENERIC_PER_CPU

/*
 * s390 uses its own implementation for per cpu data, the offset of
 * the cpu local data area is cached in the cpu's lowcore memory.
 * For 64 bit module code s390 forces the use of a GOT slot for the
 * address of the per cpu variable. This is needed because the module
 * may be more than 4G above the per cpu area.
 */
#if defined(__s390x__) && defined(MODULE)

#define __reloc_hide(var,offset) \
  (*({ unsigned long *__ptr; \
       asm ( "larl %0,per_cpu__"#var"@GOTENT" \
             : "=a" (__ptr) : "X" (per_cpu__##var) ); \
       (typeof(&per_cpu__##var))((*__ptr) + (offset)); }))

#else

#define __reloc_hide(var, offset) \
  (*({ unsigned long __ptr; \
       asm ( "" : "=a" (__ptr) : "0" (&per_cpu__##var) ); \
       (typeof(&per_cpu__##var)) (__ptr + (offset)); }))

#endif

#ifdef CONFIG_SMP

extern unsigned long __per_cpu_offset[NR_CPUS];

/* Separate out the type, so (int[3], foo) works. */
#define DEFINE_PER_CPU(type, name) \
    __attribute__((__section__(".data.percpu"))) \
    __typeof__(type) per_cpu__##name

#define __get_cpu_var(var) __reloc_hide(var,S390_lowcore.percpu_offset)
#define per_cpu(var,cpu) __reloc_hide(var,__per_cpu_offset[cpu])

/* A macro to avoid #include hell... */
#define percpu_modcopy(pcpudst, src, size)			\
do {								\
	unsigned int __i;					\
	for_each_possible_cpu(__i)				\
		memcpy((pcpudst)+__per_cpu_offset[__i],		\
		       (src), (size));				\
} while (0)

#else /* ! SMP */

#define DEFINE_PER_CPU(type, name) \
    __typeof__(type) per_cpu__##name

#define __get_cpu_var(var) __reloc_hide(var,0)
#define per_cpu(var,cpu) __reloc_hide(var,0)

#endif /* SMP */

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) per_cpu__##name

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(per_cpu__##var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(per_cpu__##var)

#endif /* __ARCH_S390_PERCPU__ */
