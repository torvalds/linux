/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __CPUIDLE_EXYNOS_H
#define __CPUIDLE_EXYNOS_H

struct cpuidle_exynos_data {
	int (*cpu0_enter_aftr)(void);
	int (*cpu1_powerdown)(void);
	void (*pre_enter_aftr)(void);
	void (*post_enter_aftr)(void);
};

#endif
