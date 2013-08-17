/* include/media/exynos_camera.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * The header file related to camera
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EXYNOS_CAMERA_H_
#define EXYNOS_CAMERA_H_

#include <media/exynos_mc.h>

enum cam_bus_type {
	CAM_TYPE_ITU = 1,
	CAM_TYPE_MIPI,
};

enum cam_port {
	CAM_PORT_A,
	CAM_PORT_B,
	CAM_PORT_C,
};

#define MAX_CAM_NUM		3
#define CAM_CLK_INV_PCLK	(1 << 0)
#define CAM_CLK_INV_VSYNC	(1 << 1)
#define CAM_CLK_INV_HREF	(1 << 2)
#define CAM_CLK_INV_HSYNC	(1 << 3)

struct i2c_board_info;

/**
 * struct exynos_isp_info - image sensor information required for host
 *			      interface configuration.
 *
 * @board_info: pointer to I2C subdevice's board info
 * @clk_frequency: frequency of the clock the host interface provides to sensor
 * @bus_type: determines bus type, MIPI, ITU-R BT.601 etc.
 * @csi_data_align: MIPI-CSI interface data alignment in bits
 * @i2c_bus_num: i2c control bus id the sensor is attached to
 * @mux_id: FIMC camera interface multiplexer index (separate for MIPI and ITU)
 * @flags: flags defining bus signals polarity inversion (High by default)
 * @use_cam: a means of used by GSCALER
 */
struct exynos_isp_info {
	struct i2c_board_info *board_info;
	unsigned long clk_frequency;
	const char *cam_srclk_name;
	const char *cam_clk_name;
	const char *cam_clk_src_name;
	const char *camif_clk_name;
	enum cam_bus_type bus_type;
	u16 csi_data_align;
	u16 i2c_bus_num;
	enum cam_port cam_port;
	u16 flags;
};
#endif /* EXYNOS_CAMERA_H_ */
