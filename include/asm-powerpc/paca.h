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

#include	<linux/config.h>
#include	<asm/types.h>
#include	<asm/lppaca.h>
#include	<asm/iseries/it_lp_reg_save.h>
#include	<asm/mmu.h>

register struct paca_struct *local_paca asm("r13");
#define get_paca()	local_paca

struct task_struct;

/*
 * Defines the layout of the paca.
 *
 * This structure is not directly accessed by firmware or the service
 * processor except for the first two pointers that point to the
 * lppaca area and the ItLpRegSave area for this CPU.  Both the
 * lppaca and ItLpRegSave objects are currently contained within the
 * PACA but they do not need to be.
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
	struct ItLpRegSave *reg_save_ptr; /* Pointer to LpRegSave for PLIC */

	/*
	 * MAGIC: the spinlock functions in arch/ppc64/lib/locks.c
	 * load lock_token and paca_index with a single lwz
	 * instruction.  They must travel together and be properly
	 * aligned.
	 */
	u16 lock_token;			/* Constant 0x8000, used in locks */
	u16 paca_index;			/* Logical processor number */

	u32 default_decr;		/* Default decrementer value */
	u64 kernel_toc;			/* Kernel TOC address */
	u64 stab_real;			/* Absolute address of segment table */
	u64 stab_addr;			/* Virtual address of segment table */
	void *emergency_sp;		/* pointer to emergency stack */
	s16 hw_cpu_id;			/* Physical processor number */
	u8 cpu_start;			/* At startup, processor spins until */
					/* this becomes non-zero. */

	/*
	 * Now, starting in cacheline 2, the exception save areas
	 */
	/* used for most interrupts/exceptions */
	u64 exgen[10] __attribute__((aligned(0x80)));
	u64 exmc[10];		/* used for machine checks */
	u64 exslb[10];		/* used for SLB/segment table misses
 				 * on the linear mapping */
#ifdef CONFIG_PPC_64K_PAGES
	pgd_t *pgdir;
#endif /* CONFIG_PPC_64K_PAGES */

	mm_context_t context;
	u16 slb_cache[SLB_CACHE_ENTRIES];
	u16 slb_cache_ptr;

	/*
	 * then miscellaneous read-write fields
	 */
	struct task_struct *__current;	/* Pointer to current */
	u64 kstack;			/* Saved Kernel stack addr */
	u64 stab_rr;			/* stab/slb round-robin counter */
	u64 next_jiffy_update_tb;	/* TB value for next jiffy update */
	u64 saved_r1;			/* r1 save for RTAS calls */
	u64 saved_msr;			/* MSR saved here by enter_rtas */
	u8 proc_enabled;		/* irq soft-enable flag */

	/* not yet used */
	u64 exdsi[8];		/* used for linear mapping hash table misses */

	/*
	 * iSeries structure which the hypervisor knows about -
	 * this structure should not cross a page boundary.
	 * The vpa_init/register_vpa call is now known to fail if the
	 * lppaca structure crosses a page boundary.
	 * The lppaca is also used on POWER5 pSeries boxes.
	 * The lppaca is 640 bytes long, and cannot readily change
	 * since the hypervisor knows its layout, so a 1kB
	 * alignment will suffice to ensure that it doesn't
	 * cross a page boundary.
	 */
	struct lppaca lppaca __attribute__((__aligned__(0x400)));
#ifdef CONFIG_PPC_ISERIES
	struct ItLpRegSave reg_save;
#endif
};

extern struct paca_struct paca[];

#endif /* _ASM_POWERPC_PACA_H */
