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

#ifndef __CPUCORE_COOLING_H__
#define __CPUCORE_COOLING_H__

#include <linux/thermal.h>
#include <linux/cpumask.h>
struct cpucore_cooling_device {
	int id;
	struct thermal_cooling_device *cool_dev;
	unsigned int cpucore_state;
	unsigned int cpucore_val;
	int max_cpu_core_num;
	int stop_flag;
};
#define CPU_STOP 0x80000000
#ifdef CONFIG_CPUCORE_THERMAL

/**
 * cpucore_cooling_register - function to create cpucore cooling device.
 * @clip_cpus: cpumask of cpus where the frequency constraints will happen
 */
struct thermal_cooling_device * cpucore_cooling_register(void);

/**
 * cpucore_cooling_unregister - function to remove cpucore cooling device.
 * @cdev: thermal cooling device pointer.
 */
void cpucore_cooling_unregister(struct thermal_cooling_device *cdev);


#else /* !CONFIG_CPU_THERMAL */
static inline struct thermal_cooling_device *
cpucore_cooling_register(void)
{
	return NULL;
}
static inline
void cpucore_cooling_unregister(struct thermal_cooling_device *cdev)
{
	return;
}
#endif	/* CONFIG_CPU_THERMAL */

#endif /* __CPU_COOLING_H__ */
