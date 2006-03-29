/*
 * drivers/power/smp.c - Functions for stopping other CPUs.
 *
 * Copyright 2004 Pavel Machek <pavel@suse.cz>
 * Copyright (C) 2002-2003 Nigel Cunningham <ncunningham@clear.net.nz>
 *
 * This file is released under the GPLv2.
 */

#undef DEBUG

#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <asm/atomic.h>
#include <asm/tlbflush.h>

/* This is protected by pm_sem semaphore */
static cpumask_t frozen_cpus;

void disable_nonboot_cpus(void)
{
	int cpu, error;

	error = 0;
	cpus_clear(frozen_cpus);
	printk("Freezing cpus ...\n");
	for_each_online_cpu(cpu) {
		if (cpu == 0)
			continue;
		error = cpu_down(cpu);
		if (!error) {
			cpu_set(cpu, frozen_cpus);
			printk("CPU%d is down\n", cpu);
			continue;
		}
		printk("Error taking cpu %d down: %d\n", cpu, error);
	}
	BUG_ON(raw_smp_processor_id() != 0);
	if (error)
		panic("cpus not sleeping");
}

void enable_nonboot_cpus(void)
{
	int cpu, error;

	printk("Thawing cpus ...\n");
	for_each_cpu_mask(cpu, frozen_cpus) {
		error = cpu_up(cpu);
		if (!error) {
			printk("CPU%d is up\n", cpu);
			continue;
		}
		printk("Error taking cpu %d up: %d\n", cpu, error);
		panic("Not enough cpus");
	}
	cpus_clear(frozen_cpus);
}

