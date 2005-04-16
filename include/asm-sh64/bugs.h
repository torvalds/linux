#ifndef __ASM_SH64_BUGS_H
#define __ASM_SH64_BUGS_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/bugs.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 *
 */

/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

/*
 * I don't know of any Super-H bugs yet.
 */

#include <asm/processor.h>

static void __init check_bugs(void)
{
	extern char *get_cpu_subtype(void);
	extern unsigned long loops_per_jiffy;

	cpu_data->loops_per_jiffy = loops_per_jiffy;

	printk("CPU: %s\n", get_cpu_subtype());
}
#endif /* __ASM_SH64_BUGS_H */
