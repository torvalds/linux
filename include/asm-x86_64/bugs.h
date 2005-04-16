/*
 *  include/asm-x86_64/bugs.h
 *
 *  Copyright (C) 1994  Linus Torvalds
 *  Copyright (C) 2000  SuSE
 *
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/msr.h>
#include <asm/pda.h>

extern void alternative_instructions(void);

static void __init check_bugs(void)
{
	identify_cpu(&boot_cpu_data);
#if !defined(CONFIG_SMP)
	printk("CPU: ");
	print_cpu_info(&boot_cpu_data);
#endif
	alternative_instructions(); 
}
