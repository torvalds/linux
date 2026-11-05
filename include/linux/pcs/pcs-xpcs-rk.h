/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PCS_XPCS_ROCKCHIP_H
#define __LINUX_PCS_XPCS_ROCKCHIP_H

#include <linux/device.h>
#include <linux/of.h>
#include <linux/pcs/pcs-xpcs.h>

struct dw_xpcs *xpcs_rk_create(struct device *dev, struct device_node *pcs, int port);

#endif /* __LINUX_PCS_XPCS_ROCKCHIP_H */
