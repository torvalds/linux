/*
 * Switch a MMU context.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_MMU_CONTEXT_H
#define _ASM_MMU_CONTEXT_H

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

/*
 * For the fast tlb miss handlers, we keep a per cpu array of pointers
 * to the current pgd for each processor. Also, the proc. id is stuffed
 * into the context register.
 */
extern unsigned long pgd_current[];

#define TLBMISS_HANDLER_SETUP_PGD(pgd) \
	pgd_current[smp_processor_id()] = (unsigned long)(pgd)

#ifdef CONFIG_32BIT
#define TLBMISS_HANDLER_SETUP()						\
	write_c0_context((unsigned long) smp_processor_id() << 25);	\
	TLBMISS_HANDLER_SETUP_PGD(swapper_pg_dir)
#endif
#ifdef CONFIG_64BIT
#define TLBMISS_HANDLER_SETUP()						\
	write_c0_context((unsigned long) smp_processor_id() << 26);	\
	TLBMISS_HANDLER_SETUP_PGD(swapper_pg_dir)
#endif

#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)

#define ASID_INC	0x40
#define ASID_MASK	0xfc0

#elif defined(CONFIG_CPU_R8000)

#define ASID_INC	0x10
#define ASID_MASK	0xff0

#elif defined(CONFIG_CPU_RM9000)

#define ASID_INC	0x1
#define ASID_MASK	0xfff

#else /* FIXME: not correct for R6000 */

#define ASID_INC	0x1
#define ASID_MASK	0xff

#endif

#define cpu_context(cpu, mm)	((mm)->context[cpu])
#define cpu_asid(cpu, mm)	(cpu_context((cpu), (mm)) & ASID_MASK)
#define asid_cache(cpu)		(cpu_data[cpu].asid_cache)

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

/*
 *  All unused by hardware upper bits will be considered
 *  as a software asid extension.
 */
#define ASID_VERSION_MASK  ((unsigned long)~(ASID_MASK|(ASID_MASK-1)))
#define ASID_FIRST_VERSION ((unsigned long)(~ASID_VERSION_MASK) + 1)

static inline void
get_new_mmu_context(struct mm_struct *mm, unsigned long cpu)
{
	unsigned long asid = asid_cache(cpu);

	if (! ((asid += ASID_INC) & ASID_MASK) ) {
		if (cpu_has_vtag_icache)
			flush_icache_all();
		local_flush_tlb_all();	/* start new asid cycle */
		if (!asid)		/* fix version if needed */
			asid = ASID_FIRST_VERSION;
	}
	cpu_context(cpu, mm) = asid_cache(cpu) = asid;
}

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */
static inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	int i;

	for (i = 0; i < num_online_cpus(); i++)
		cpu_context(i, mm) = 0;

	return 0;
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
                             struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();
	unsigned long flags;

	local_irq_save(flags);

	/* Check if our ASID is of an older version and thus invalid */
	if ((cpu_context(cpu, next) ^ asid_cache(cpu)) & ASID_VERSION_MASK)
		get_new_mmu_context(next, cpu);

	write_c0_entryhi(cpu_context(cpu, next));
	TLBMISS_HANDLER_SETUP_PGD(next->pgd);

	/*
	 * Mark current->active_mm as not "active" anymore.
	 * We don't want to mislead possible IPI tlb flush routines.
	 */
	cpu_clear(cpu, prev->cpu_vm_mask);
	cpu_set(cpu, next->cpu_vm_mask);

	local_irq_restore(flags);
}

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
static inline void destroy_context(struct mm_struct *mm)
{
}

#define deactivate_mm(tsk,mm)	do { } while (0)

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void
activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	unsigned long flags;
	unsigned int cpu = smp_processor_id();

	local_irq_save(flags);

	/* Unconditionally get a new ASID.  */
	get_new_mmu_context(next, cpu);

	write_c0_entryhi(cpu_context(cpu, next));
	TLBMISS_HANDLER_SETUP_PGD(next->pgd);

	/* mark mmu ownership change */
	cpu_clear(cpu, prev->cpu_vm_mask);
	cpu_set(cpu, next->cpu_vm_mask);

	local_irq_restore(flags);
}

/*
 * If mm is currently active_mm, we can't really drop it.  Instead,
 * we will get a new one for it.
 */
static inline void
drop_mmu_context(struct mm_struct *mm, unsigned cpu)
{
	unsigned long flags;

	local_irq_save(flags);

	if (cpu_isset(cpu, mm->cpu_vm_mask))  {
		get_new_mmu_context(mm, cpu);
		write_c0_entryhi(cpu_asid(cpu, mm));
	} else {
		/* will get a new context next time */
		cpu_context(cpu, mm) = 0;
	}

	local_irq_restore(flags);
}

#endif /* _ASM_MMU_CONTEXT_H */
