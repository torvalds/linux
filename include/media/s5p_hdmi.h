/*
 * Driver header for S5P HDMI chip.
 *
 * Copyright (c) 2011 Samsung Electronics, Co. Ltd
 * Contact: Tomasz Stanislawski <t.stanislaws@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

