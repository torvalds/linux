#ifndef _ASM_IA64_PERCPU_H
#define _ASM_IA64_PERCPU_H

/*
 * Copyright (C) 2002-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#define PERCPU_ENOUGH_ROOM PERCPU_PAGE_SIZE

#ifdef __ASSEMBLY__
# define THIS_CPU(var)	(per_cpu__##var)  /* use this to mark accesses to per-CPU variables... */
#else /* !__ASSEMBLY__ */


#include <linux/threads.h>

#ifdef HAVE_MODEL_SMALL_ATTRIBUTE
# define PER_CPU_ATTRIBUTES	__attribute__((__model__ (__small__)))
#endif

#define DECLARE_PER_CPU(type, name)				\
	extern PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name

/*
 * Pretty much a literal copy of asm-generic/percpu.h, except that percpu_modcopy() is an
 * external routine, to avoid include-hell.
 */
#ifdef CONFIG_SMP

extern unsigned long __per_cpu_offset[NR_CPUS];
#define per_cpu_offset(x) (__per_cpu_offset[x])

/* Equal to __per_cpu_offset[smp_processor_id()], but faster to access: */
DECLARE_PER_CPU(unsigned long, local_per_cpu_offset);

#define per_cpu(var, cpu)  (*RELOC_HIDE(&per_cpu__##var, __per_cpu_offset[cpu]))
#define __get_cpu_var(var) (*RELOC_HIDE(&per_cpu__##var, __ia64_per_cpu_var(local_per_cpu_offset)))
#define __raw_get_cpu_var(var) (*RELOC_HIDE(&per_cpu__##var, __ia64_per_cpu_var(local_per_cpu_offset)))

extern void percpu_modcopy(void *pcpudst, const void *src, unsigned long size);
extern void setup_per_cpu_areas (void);
extern void *per_cpu_init(void);

#else /* ! SMP */

#define per_cpu(var, cpu)			(*((void)(cpu), &per_cpu__##var))
#define __get_cpu_var(var)			per_cpu__##var
#define __raw_get_cpu_var(var)			per_cpu__##var
#define per_cpu_init()				(__phys_per_cpu_start)

#endif	/* SMP */

/*
 * Be extremely careful when taking the address of this variable!  Due to virtual
 * remapping, it is different from the canonical address returned by __get_cpu_var(var)!
 * On the positive side, using __ia64_per_cpu_var() instead of __get_cpu_var() is slightly
 * more efficient.
 */
#define __ia64_per_cpu_var(var)	(per_cpu__##var)

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_PERCPU_H */
