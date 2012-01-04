/*
 * Copyright (C) 2011 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_CPU_PM_H
#define _LINUX_CPU_PM_H

#include <linux/kernel.h>
#include <linux/notifier.h>

/*
 * When a CPU goes to a low power state that turns off power to the CPU's
 * power domain, the contents of some blocks (floating point coprocessors,
 * interrupt controllers, caches, timers) in the same power domain can
 * be lost.  The cpm_pm notifiers provide a method for platform idle, suspend,
 * and hotplug implementations to notify the drivers for these blocks that
 * they may be reset.
 *
 * All cpu_pm notifications must be called with interrupts disabled.
 *
 * The notifications are split into two classes: CPU notifications and CPU
 * cluster notifications.
 *
 * CPU notifications apply to a single CPU and must be called on the affected
 * CPU.  They are used to save per-cpu context for affected blocks.
 *
 * CPU cluster notifications apply to all CPUs in a single power domain. They
 * are used to save any global context for affected blocks, and must be called
 * after all the CPUs in the power domain have been notified of the low power
 * state.
 */

/*
 * Event codes passed as unsigned long val to notifier calls
 */
enum cpu_pm_event {
	/* A single cpu is entering a low power state */
	CPU_PM_ENTER,

	/* A single cpu failed to enter a low power state */
	CPU_PM_ENTER_FAILED,

	/* A single cpu is exiting a low power state */
	CPU_PM_EXIT,

	/* A cpu power domain is entering a low power state */
	CPU_CLUSTER_PM_ENTER,

	/* A cpu power domain failed to enter a low power state */
	CPU_CLUSTER_PM_ENTER_FAILED,

	/* A cpu power domain is exiting a low power state */
	CPU_CLUSTER_PM_EXIT,
};

#ifdef CONFIG_CPU_PM
int cpu_pm_register_notifier(struct notifier_block *nb);
int cpu_pm_unregister_notifier(struct notifier_block *nb);
int cpu_pm_enter(void);
int cpu_pm_exit(void);
int cpu_cluster_pm_enter(void);
int cpu_cluster_pm_exit(void);

#else

static inline int cpu_pm_register_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int cpu_pm_unregister_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int cpu_pm_enter(void)
{
	return 0;
}

static inline int cpu_pm_exit(void)
{
	return 0;
}

static inline int cpu_cluster_pm_enter(void)
{
	return 0;
}

static inline int cpu_cluster_pm_exit(void)
{
	return 0;
}
#endif
#endif
