/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI PRM (Power & Reset Manager) platform data
 *
 * Copyright (C) 2019 Texas Instruments, Inc.
 *
 * Tero Kristo <t-kristo@ti.com>
 */

#ifndef _LINUX_PLATFORM_DATA_TI_PRM_H
#define _LINUX_PLATFORM_DATA_TI_PRM_H

struct clockdomain;

struct ti_prm_platform_data {
	void (*clkdm_deny_idle)(struct clockdomain *clkdm);
	void (*clkdm_allow_idle)(struct clockdomain *clkdm);
	struct clockdomain * (*clkdm_lookup)(const char *name);
};

#endif /* _LINUX_PLATFORM_DATA_TI_PRM_H */
