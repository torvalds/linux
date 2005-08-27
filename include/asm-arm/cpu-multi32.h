/*
 *  linux/include/asm-arm/cpu-multi32.h
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/page.h>

struct mm_struct;

/*
 * Don't change this structure - ASM code
 * relies on it.
 */
extern struct processor {
	/* MISC
	 * get data abort address/flags
	 */
	void (*_data_abort)(unsigned long pc);
	/*
	 * Set up any processor specifics
	 */
	void (*_proc_init)(void);
	/*
	 * Disable any processor specifics
	 */
	void (*_proc_fin)(void);
	/*
	 * Special stuff for a reset
	 */
	void (*reset)(unsigned long addr) __attribute__((noreturn));
	/*
	 * Idle the processor
	 */
	int (*_do_idle)(void);
	/*
	 * Processor architecture specific
	 */
	/*
	 * clean a virtual address range from the
	 * D-cache without flushing the cache.
	 */
	void (*dcache_clean_area)(void *addr, int size);

	/*
	 * Set the page table
	 */
	void (*switch_mm)(unsigned long pgd_phys, struct mm_struct *mm);
	/*
	 * Set a PTE
	 */
	void (*set_pte)(pte_t *ptep, pte_t pte);
} processor;

#define cpu_proc_init()			processor._proc_init()
#define cpu_proc_fin()			processor._proc_fin()
#define cpu_reset(addr)			processor.reset(addr)
#define cpu_do_idle()			processor._do_idle()
#define cpu_dcache_clean_area(addr,sz)	processor.dcache_clean_area(addr,sz)
#define cpu_set_pte(ptep, pte)		processor.set_pte(ptep, pte)
#define cpu_do_switch_mm(pgd,mm)	processor.switch_mm(pgd,mm)
