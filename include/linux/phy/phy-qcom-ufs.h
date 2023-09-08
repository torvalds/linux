/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2021, Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef PHY_QCOM_UFS_H_
#define PHY_QCOM_UFS_H_

#include "phy.h"

void ufs_qcom_phy_ctrl_rx_linecfg(struct phy *generic_phy, bool ctrl);
void ufs_qcom_phy_set_tx_lane_enable(struct phy *generic_phy, u32 tx_lanes);
int ufs_qcom_phy_get_tx_hs_equalizer(struct phy *generic_phy, u32 gear, u32 *val);
void ufs_qcom_phy_dbg_register_dump(struct phy *generic_phy);
void ufs_qcom_phy_dbg_register_save(struct phy *generic_phy);
void ufs_qcom_phy_set_src_clk_h8_enter(struct phy *generic_phy);
void ufs_qcom_phy_set_src_clk_h8_exit(struct phy *generic_phy);
int ufs_qcom_phy_save_controller_version(struct phy *generic_phy,
					u8 major, u16 minor, u16 step);

#endif /* PHY_QCOM_UFS_H_ */

