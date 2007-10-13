/*
 * include/asm-powerpc/paca.h
 *
 * This control block defines the PACA which defines the processor
 * specific data for each logical processor on the system.
 * There are some pointers defined that are utilized by PLIC.
 *
 * C 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_PACA_H
#define _ASM_POWERPC_PACA_H
#ifdef __KERNEL__

#include	<asm/types.h>
#include	<asm/lppaca.h>
#include	<asm/mmu.h>

register struct paca_struct *local_paca asm("r13");

#if defined(CONFIG_DEBUG_PREEMPT) && defined(CONFIG_SMP)
extern unsigned int debug_smp_processor_id(void); /* from linux/smp.h */
/*
 * Add standard checks that preemption cannot occur when using get_paca():
 * otherwise the paca_struct it points to may be the wrong one just after.
 */
#define get_paca()	((void) debug_smp_processor_id(), local_paca)
#else
#define get_paca()	local_paca
#endif

#define get_lppaca()	(get_paca()->lppaca_ptr)
#define get_slb_shadow()	(get_paca()->slb_shadow_ptr)

struct task_struct;

/*
 * Defines the layout of the paca.
 *
 * This structure is not directly accessed by firmware or the service
 * processor except for the first two pointers that point to the
 * lppaca area and the ItLpRegSave area for this CPU.  The lppaca
 * object is currently contained within the PACA but it doesn't need
 * to be.
 */
struct paca_struct {
	/*
	 * Because hw_cpu_id, unlike other paca fields, is accessed
	 * routinely from other CPUs (from the IRQ code), we stick to
	 * read-only (after boot) fields in the first cacheline to
	 * avoid cacheline bouncing.
	 */

	/*
	 * MAGIC: These first two pointers can't be moved - they're
	 * accessed by the firmware
	 */
	struct lppaca *lppaca_ptr;	/* Pointer to LpPaca for PLIC */
#ifdef CONFIG_PPC_ISERIES
	void *reg_save_ptr; /* Pointer to LpRegSave for PLIC */
#endif /* CONFIG_PPC_ISERIES */

	/*
	 * MAGIC: the spinlock functions in arch/powerpc/lib/locks.c 
	 * load lock_token and paca_index with a single lwz
	 * instruction.  They must travel together and be properly
	 * aligned.
	 */
	u16 lock_token;			/* Constant 0x8000, used in locks */
	u16 paca_index;			/* Logical processor number */

	u64 kernel_toc;			/* Kernel TOC address */
	u64 stab_real;			/* Absolute address of segment table */
	u64 stab_addr;			/* Virtual address of segment table */
	void *emergency_sp;		/* pointer to emergency stack */
	u64 data_offset;		/* per cpu data offset */
	s16 hw_cpu_id;			/* Physical processor number */
	u8 cpu_start;			/* At startup, processor spins until */
					/* this becomes non-zero. */
	struct slb_shadow *slb_shadow_ptr;

	/*
	 * Now, starting in cacheline 2, the exception save areas
	 */
	/* used for most interrupts/exceptions */
	u64 exgen[10] __attribute__((aligned(0x80)));
	u64 exmc[10];		/* used for machine checks */
	u64 exslb[10];		/* used for SLB/segment table misses
 				 * on the linear mapping */

	mm_context_t context;
	u16 vmalloc_sllp;
	u16 slb_cache_ptr;
	u16 slb_cache[SLB_CACHE_ENTRIES];

	/*
	 * then miscellaneous read-write fields
	 */
	struct task_struct *__current;	/* Pointer to current */
	u64 kstack;			/* Saved Kernel stack addr */
	u64 stab_rr;			/* stab/slb round-robin counter */
	u64 saved_r1;			/* r1 save for RTAS calls */
	u64 saved_msr;			/* MSR saved here by enter_rtas */
	u16 trap_save;			/* Used when bad stack is encountered */
	u8 soft_enabled;		/* irq soft-enable flag */
	u8 hard_enabled;		/* set if irqs are enabled in MSR */
	u8 io_sync;			/* writel() needs spin_unlock sync */

	/* Stuff for accurate time accounting */
	u64 user_time;			/* accumulated usermode TB ticks */
	u64 system_time;		/* accumulated system TB ticks */
	u64 startpurr;			/* PURR/TB value snapshot */
};

extern struct paca_struct paca[];

void setup_boot_paca(void);

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_PACA_H */
