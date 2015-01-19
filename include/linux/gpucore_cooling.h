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

#ifndef __GPUCORE_COOLING_H__
#define __GPUCORE_COOLING_H__

#include <linux/thermal.h>
#include <linux/cpumask.h>
struct gpucore_cooling_device {
	int id;
	struct thermal_cooling_device *cool_dev;
	unsigned int gpucore_state;
	unsigned int gpucore_val;
	 int max_gpu_core_num;
	unsigned int (*set_max_pp_num)(unsigned int);
	int stop_flag;
};
#define GPU_STOP 0x80000000

#ifdef CONFIG_GPUCORE_THERMAL

/**
 * gpucore_cooling_register - function to create gpucore cooling device.
 * @clip_cpus: cpumask of cpus where the frequency constraints will happen
 */
struct thermal_cooling_device * gpucore_cooling_register(struct gpucore_cooling_device *);

/**
 * gpucore_cooling_unregister - function to remove gpucore cooling device.
 * @cdev: thermal cooling device pointer.
 */
void gpucore_cooling_unregister(struct thermal_cooling_device *cdev);
struct gpucore_cooling_device * gpucore_cooling_alloc(void);

#else /* !CONFIG_CPU_THERMAL */
inline struct gpucore_cooling_device * gpucore_cooling_alloc(void)
{
	return NULL;
}

inline struct thermal_cooling_device * gpucore_cooling_register(struct gpucore_cooling_device *gcd)
{
	return NULL;
}
inline void gpucore_cooling_unregister(struct thermal_cooling_device *cdev)
{
	return;
}
#endif	/* CONFIG_CPU_THERMAL */

#endif /* __CPU_COOLING_H__ */
