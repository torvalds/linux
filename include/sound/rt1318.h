/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/rt1318.h -- Platform data for RT1318
 *
 * Copyright 2024 Realtek Semiconductor Corp.
 */

#ifndef __LINUX_SND_RT1318_H
#define __LINUX_SND_RT1318_H

struct rt1318_platform_data {
	unsigned int init_r0_l;
	unsigned int init_r0_r;
};

#endif
