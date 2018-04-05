/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Information for the Marvell Armada MMP camera
 */

#include <media/v4l2-mediabus.h>

enum dphy3_algo {
	DPHY3_ALGO_DEFAULT = 0,
	DPHY3_ALGO_PXA910,
	DPHY3_ALGO_PXA2128
};

struct mmp_camera_platform_data {
	struct platform_device *i2c_device;
	int sensor_power_gpio;
	int sensor_reset_gpio;
	enum v4l2_mbus_type bus_type;
	int mclk_min;	/* The minimal value of MCLK */
	int mclk_src;	/* which clock source the MCLK derives from */
	int mclk_div;	/* Clock Divider Value for MCLK */
	/*
	 * MIPI support
	 */
	int dphy[3];		/* DPHY: CSI2_DPHY3, CSI2_DPHY5, CSI2_DPHY6 */
	enum dphy3_algo dphy3_algo;	/* algos for calculate CSI2_DPHY3 */
	int lane;		/* ccic used lane number; 0 means DVP mode */
	int lane_clk;
};
