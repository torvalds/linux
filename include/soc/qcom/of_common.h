/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef __QCOM_OF_H
#define __QCOM_OF_H

#include <linux/of.h>

/**
 * of_fdt_get_ddrtype - Return the type of ddr (4/5) on the current device
 *
 * On match, returns a non-zero positive value which matches the ddr type.
 * Otherwise returns -ENOENT.
 */
static inline int of_fdt_get_ddrtype(void)
{
	int ret;
	u32 ddr_type;
	struct device_node *mem_node;

	mem_node = of_find_node_by_path("/memory");
	if (!mem_node)
		return -ENOENT;

	ret = of_property_read_u32(mem_node, "ddr_device_type", &ddr_type);
	of_node_put(mem_node);
	if (ret < 0)
		return -ENOENT;

	return ddr_type;
}

#endif
