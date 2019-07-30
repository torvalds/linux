/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver header for S5P HDMI chip.
 *
 * Copyright (c) 2011 Samsung Electronics, Co. Ltd
 * Contact: Tomasz Stanislawski <t.stanislaws@samsung.com>
 */

#ifndef S5P_HDMI_H
#define S5P_HDMI_H

struct i2c_board_info;

/**
 * @hdmiphy_bus: controller id for HDMIPHY bus
 * @hdmiphy_info: template for HDMIPHY I2C device
 * @mhl_bus: controller id for MHL control bus
 * @mhl_info: template for MHL I2C device
 * @hpd_gpio: GPIO for Hot-Plug-Detect pin
 *
 * NULL pointer for *_info fields indicates that
 * the corresponding chip is not present
 */
struct s5p_hdmi_platform_data {
	int hdmiphy_bus;
	struct i2c_board_info *hdmiphy_info;
	int mhl_bus;
	struct i2c_board_info *mhl_info;
	int hpd_gpio;
};

#endif /* S5P_HDMI_H */
