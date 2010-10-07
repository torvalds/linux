/*
 * Samsung S5P SoC camera interface driver header
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd
 * Author: Sylwester Nawrocki, <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef S3C_FIMC_H_
#define S3C_FIMC_H_

enum cam_bus_type {
	FIMC_ITU_601 = 1,
	FIMC_ITU_656,
	FIMC_MIPI_CSI2,
	FIMC_LCD_WB, /* FIFO link from LCD mixer */
};

#define FIMC_CLK_INV_PCLK	(1 << 0)
#define FIMC_CLK_INV_VSYNC	(1 << 1)
#define FIMC_CLK_INV_HREF	(1 << 2)
#define FIMC_CLK_INV_HSYNC	(1 << 3)

struct i2c_board_info;

/**
 * struct s3c_fimc_isp_info - image sensor information required for host
 *			      interace configuration.
 *
 * @board_info: pointer to I2C subdevice's board info
 * @bus_type: determines bus type, MIPI, ITU-R BT.601 etc.
 * @i2c_bus_num: i2c control bus id the sensor is attached to
 * @mux_id: FIMC camera interface multiplexer index (separate for MIPI and ITU)
 * @bus_width: camera data bus width in bits
 * @flags: flags defining bus signals polarity inversion (High by default)
 */
struct s3c_fimc_isp_info {
	struct i2c_board_info *board_info;
	enum cam_bus_type bus_type;
	u16 i2c_bus_num;
	u16 mux_id;
	u16 bus_width;
	u16 flags;
};


#define FIMC_MAX_CAMIF_CLIENTS	2

/**
 * struct s3c_platform_fimc - camera host interface platform data
 *
 * @isp_info: properties of camera sensor required for host interface setup
 */
struct s3c_platform_fimc {
	struct s3c_fimc_isp_info *isp_info[FIMC_MAX_CAMIF_CLIENTS];
};
#endif /* S3C_FIMC_H_ */
