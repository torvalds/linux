/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Header for EXYNOS PMU Driver support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SOC_EXYNOS_PMU_H
#define __LINUX_SOC_EXYNOS_PMU_H

enum sys_powerdown {
	SYS_AFTR,
	SYS_LPA,
	SYS_SLEEP,
	NUM_SYS_POWERDOWN,
};

extern void exynos_sys_powerdown_conf(enum sys_powerdown mode);

#endif /* __LINUX_SOC_EXYNOS_PMU_H */
