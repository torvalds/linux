/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/rt286.h -- Platform data for RT286
 *
 * Copyright 2013 Realtek Microelectronics
 */

#ifndef __LINUX_SND_RT298_H
#define __LINUX_SND_RT298_H

struct rt298_platform_data {
	bool cbj_en; /*combo jack enable*/
	bool gpio2_en; /*GPIO2 enable*/
	bool suspend_power_off; /* power is off during suspend */
};

#endif
