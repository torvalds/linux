/*
 * Copyright (c) 2013-2015, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef PHY_QCOM_UFS_H_
#define PHY_QCOM_UFS_H_

#include "phy.h"

/**
 * ufs_qcom_phy_enable_dev_ref_clk() - Enable the device
 * ref clock.
 * @phy: reference to a generic phy.
 */
void ufs_qcom_phy_enable_dev_ref_clk(struct phy *phy);

/**
 * ufs_qcom_phy_disable_dev_ref_clk() - Disable the device
 * ref clock.
 * @phy: reference to a generic phy.
 */
void ufs_qcom_phy_disable_dev_ref_clk(struct phy *phy);

int ufs_qcom_phy_start_serdes(struct phy *phy);
int ufs_qcom_phy_set_tx_lane_enable(struct phy *phy, u32 tx_lanes);
int ufs_qcom_phy_calibrate_phy(struct phy *phy, bool is_rate_B);
int ufs_qcom_phy_is_pcs_ready(struct phy *phy);
void ufs_qcom_phy_save_controller_version(struct phy *phy,
			u8 major, u16 minor, u16 step);

#endif /* PHY_QCOM_UFS_H_ */
