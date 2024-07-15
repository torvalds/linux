/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Microchip PolarFire SoC (MPFS)
 *
 * Copyright (c) 2020 Microchip Corporation. All rights reserved.
 *
 * Author: Conor Dooley <conor.dooley@microchip.com>
 *
 */

#ifndef __SOC_MPFS_H__
#define __SOC_MPFS_H__

#include <linux/types.h>
#include <linux/of_device.h>

struct mpfs_sys_controller;

struct mpfs_mss_msg {
	u8 cmd_opcode;
	u16 cmd_data_size;
	struct mpfs_mss_response *response;
	u8 *cmd_data;
	u16 mbox_offset;
	u16 resp_offset;
};

struct mpfs_mss_response {
	u32 resp_status;
	u32 *resp_msg;
	u16 resp_size;
};

#if IS_ENABLED(CONFIG_POLARFIRE_SOC_SYS_CTRL)

int mpfs_blocking_transaction(struct mpfs_sys_controller *mpfs_client, struct mpfs_mss_msg *msg);

struct mpfs_sys_controller *mpfs_sys_controller_get(struct device *dev);

struct mtd_info *mpfs_sys_controller_get_flash(struct mpfs_sys_controller *mpfs_client);

#endif /* if IS_ENABLED(CONFIG_POLARFIRE_SOC_SYS_CTRL) */

#if IS_ENABLED(CONFIG_MCHP_CLK_MPFS)
#if IS_ENABLED(CONFIG_RESET_POLARFIRE_SOC)
int mpfs_reset_controller_register(struct device *clk_dev, void __iomem *base);
#else
static inline int mpfs_reset_controller_register(struct device *clk_dev, void __iomem *base) { return 0; }
#endif /* if IS_ENABLED(CONFIG_RESET_POLARFIRE_SOC) */
#endif /* if IS_ENABLED(CONFIG_MCHP_CLK_MPFS) */

#endif /* __SOC_MPFS_H__ */
