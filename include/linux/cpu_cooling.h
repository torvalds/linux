/*
 *  linux/include/linux/cpu_cooling.h
 *
 *  Copyright (C) 2012	Samsung Electronics Co., Ltd(http://www.samsung.com)
 *  Copyright (C) 2012  Amit Daniel <amit.kachhap@linaro.org>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef __CPU_COOLING_H__
#define __CPU_COOLING_H__

#include <linux/thermal.h>

#define CPUFREQ_COOLING_START		0
#define CPUFREQ_COOLING_STOP		1

/**
 * struct freq_clip_table
 * @freq_clip_max: maximum frequency allowed for this cooling state.
 * @temp_level: Temperature level at which the temperature clipping will
 *	happen.
 * @mask_val: cpumask of the allowed cpu's where the clipping will take place.
 *
 * This structure is required to be filled and passed to the
 * cpufreq_cooling_unregister function.
 */
struct freq_clip_table {
	unsigned int freq_clip_max;
	unsigned int temp_level;
	const struct cpumask *mask_val;
};

/**
 * cputherm_register_notifier - Register a notifier with cpu cooling interface.
 * @nb:	struct notifier_block * with callback info.
 * @list: integer value for which notification is needed. possible values are
 *	CPUFREQ_COOLING_TYPE and CPUHOTPLUG_COOLING_TYPE.
 *
 * This exported function registers a driver with cpu cooling layer. The driver
 * will be notified when any cpu cooling action is called.
 */
int cputherm_register_notifier(struct notifier_block *nb, unsigned int list);

/**
 * cputherm_unregister_notifier - Un-register a notifier.
 * @nb:	struct notifier_block * with callback info.
 * @list: integer value for which notification is needed. values possible are
 *	CPUFREQ_COOLING_TYPE.
 *
 * This exported function un-registers a driver with cpu cooling layer.
 */
int cputherm_unregister_notifier(struct notifier_block *nb, unsigned int list);

#ifdef CONFIG_CPU_FREQ
/**
 * cpufreq_cooling_register - function to create cpufreq cooling device.
 * @tab_ptr: table ptr containing the maximum value of frequency to be clipped
 *	for each cooling state.
 * @tab_size: count of entries in the above table.
 * @mask_val: cpumask containing the allowed cpu's where frequency clipping can
 *	happen.
 */
struct thermal_cooling_device *cpufreq_cooling_register(
	struct freq_clip_table *tab_ptr, unsigned int tab_size);

/**
 * cpufreq_cooling_unregister - function to remove cpufreq cooling device.
 * @cdev: thermal cooling device pointer.
 */
void cpufreq_cooling_unregister(struct thermal_cooling_device *cdev);
#else /*!CONFIG_CPU_FREQ*/
static inline struct thermal_cooling_device *cpufreq_cooling_register(
	struct freq_clip_table *tab_ptr, unsigned int tab_size)
{
	return NULL;
}
static inline void cpufreq_cooling_unregister(
		struct thermal_cooling_device *cdev)
{
	return;
}
#endif	/*CONFIG_CPU_FREQ*/

#endif /* __CPU_COOLING_H__ */
