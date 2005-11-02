/*
 *  linux/include/asm-arm/cpu.h
 *
 *  Copyright (C) 2004-2005 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_CPU_H
#define __ASM_ARM_CPU_H

#include <linux/config.h>
#include <linux/percpu.h>

struct cpuinfo_arm {
	struct cpu	cpu;
#ifdef CONFIG_SMP
	struct task_struct *idle;
	unsigned int	loops_per_jiffy;
#endif
};

DECLARE_PER_CPU(struct cpuinfo_arm, cpu_data);

#endif
