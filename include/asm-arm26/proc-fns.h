/*
 *  linux/include/asm-arm26/proc-fns.h
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASSEMBLY__

#include <asm/page.h>

/*
 * Don't change this structure - ASM code
 * relies on it.
 */
extern struct processor {
	/* check for any bugs */
	void (*_check_bugs)(void);
	/* Set up any processor specifics */
	void (*_proc_init)(void);
	/* Disable any processor specifics */
	void (*_proc_fin)(void);
	/* set the MEMC hardware mappings */
	void (*_set_pgd)(pgd_t *pgd);
	/* XCHG */
	unsigned long (*_xchg_1)(unsigned long x, volatile void *ptr);
	unsigned long (*_xchg_4)(unsigned long x, volatile void *ptr);
} processor;

extern const struct processor arm2_processor_functions;
extern const struct processor arm250_processor_functions;
extern const struct processor arm3_processor_functions;

#define cpu_check_bugs()			processor._check_bugs()
#define cpu_proc_init()				processor._proc_init()
#define cpu_proc_fin()				processor._proc_fin()
#define cpu_do_idle()				do { } while (0)
#define cpu_switch_mm(pgd,mm)			processor._set_pgd(pgd)
#define cpu_xchg_1(x,ptr)			processor._xchg_1(x,ptr)
#define cpu_xchg_4(x,ptr)			processor._xchg_4(x,ptr)


//FIXME - these shouldnt be in proc-fn.h
extern void cpu_memc_update_all(pgd_t *pgd);
extern void cpu_memc_update_entry(pgd_t *pgd, unsigned long phys_pte, unsigned long log_addr);

#endif
