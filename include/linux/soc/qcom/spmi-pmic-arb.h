/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */

#ifndef _SPMI_PMIC_ARB_H
#define _SPMI_PMIC_ARB_H

#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/types.h>

#if IS_ENABLED(CONFIG_SPMI_MSM_PMIC_ARB)
int spmi_pmic_arb_map_address(const struct device *dev, u32 spmi_address,
			      struct resource *res_out);
#else
static inline int spmi_pmic_arb_map_address(const struct device *dev,
				u32 spmi_address, struct resource *res_out)
{
	return -ENODEV;
}
#endif

#endif
