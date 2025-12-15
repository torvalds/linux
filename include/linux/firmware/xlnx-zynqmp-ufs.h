/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Firmware layer for UFS APIs.
 *
 * Copyright (c) 2025 Advanced Micro Devices, Inc.
 */

#ifndef __FIRMWARE_XLNX_ZYNQMP_UFS_H__
#define __FIRMWARE_XLNX_ZYNQMP_UFS_H__

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE)
int zynqmp_pm_is_mphy_tx_rx_config_ready(bool *is_ready);
int zynqmp_pm_is_sram_init_done(bool *is_done);
int zynqmp_pm_set_sram_bypass(void);
int zynqmp_pm_get_ufs_calibration_values(u32 *val);
#else
static inline int zynqmp_pm_is_mphy_tx_rx_config_ready(bool *is_ready)
{
	return -ENODEV;
}

static inline int zynqmp_pm_is_sram_init_done(bool *is_done)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_sram_bypass(void)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_ufs_calibration_values(u32 *val)
{
	return -ENODEV;
}
#endif

#endif /* __FIRMWARE_XLNX_ZYNQMP_UFS_H__ */
