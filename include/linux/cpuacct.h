/* include/linux/cpuacct.h
 *
 * Copyright (C) 2010 Google, Inc.
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

#ifndef _CPUACCT_H_
#define _CPUACCT_H_

#include <linux/cgroup.h>

#ifdef CONFIG_CGROUP_CPUACCT

/*
 * Platform specific CPU frequency hooks for cpuacct. These functions are
 * called from the scheduler.
 */
struct cpuacct_charge_calls {
	/*
	 * Platforms can take advantage of this data and use
	 * per-cpu allocations if necessary.
	 */
	void (*init) (void **cpuacct_data);
	void (*charge) (void *cpuacct_data,  u64 cputime, unsigned int cpu);
	void (*cpufreq_show) (void *cpuacct_data, struct cgroup_map_cb *cb);
	/* Returns power consumed in milliWatt seconds */
	u64 (*power_usage) (void *cpuacct_data);
};

int cpuacct_charge_register(struct cpuacct_charge_calls *fn);

#endif /* CONFIG_CGROUP_CPUACCT */

#endif // _CPUACCT_H_
