/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */
#ifndef _ASM_BUGS_H
#define _ASM_BUGS_H

#include <linux/config.h>
#include <linux/delay.h>
#include <asm/cpu.h>
#include <asm/cpu-info.h>

extern void check_bugs32(void);
extern void check_bugs64(void);

static inline void check_bugs(void)
{
	unsigned int cpu = smp_processor_id();

	cpu_data[cpu].udelay_val = loops_per_jiffy;
	check_bugs32();
#ifdef CONFIG_64BIT
	check_bugs64();
#endif
}

#endif /* _ASM_BUGS_H */
