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

#ifndef S5P_FIMC_H_
#define S5P_FIMC_H_

#include <media/media-entity.h>

enum cam_bus_type {
	FIMC_ITU_601 = 1,
	FIMC_ITU_656,
	FIMC_MIPI_CSI2,
	FIMC_LCD_WB, /* FIFO link from LCD mixer */
};

struct i2c_board_info;

/**
 * struct s5p_fimc_isp_info - image sensor information required for host
 *			      interace configuration.
 *
 * @board_info: pointer to I2C subdevice's board info
 * @clk_frequency: frequency of the clock the host interface provides to sensor
 * @bus_type: determines bus type, MIPI, ITU-R BT.601 etc.
 * @csi_data_align: MIPI-CSI interface data alignment in bits
 * @i2c_bus_num: i2c control bus id the sensor is attached to
 * @mux_id: FIMC camera interface multiplexer index (separate for MIPI and ITU)
 * @clk_id: index of the SoC peripheral clock for sensors
 * @flags: the parallel bus flags defining signals polarity (V4L2_MBUS_*)
 */
struct s5p_fimc_isp_info {
	struct i2c_board_info *board_info;
	unsigned long clk_frequency;
	enum cam_bus_type bus_type;
	u16 csi_data_align;
	u16 i2c_bus_num;
	u16 mux_id;
	u16 flags;
	u8 clk_id;
};

/**
 * struct s5p_platform_fimc - camera host interface platform data
 *
 * @isp_info: properties of camera sensor required for host interface setup
 * @num_clients: the number of attached image sensors
 */
struct s5p_platform_fimc {
	struct s5p_fimc_isp_info *isp_info;
	int num_clients;
};

/*
 * v4l2_device notification id. This is only for internal use in the kernel.
 * Sensor subdevs should issue S5P_FIMC_TX_END_NOTIFY notification in single
 * frame capture mode when there is only one VSYNC pulse issued by the sensor
 * at begining of the frame transmission.
 */
#define S5P_FIMC_TX_END_NOTIFY _IO('e', 0)

enum fimc_subdev_index {
	IDX_SENSOR,
	IDX_CSIS,
	IDX_FLITE,
	IDX_FIMC,
	IDX_MAX,
};

struct media_pipeline;
struct v4l2_subdev;

struct fimc_pipeline {
	struct v4l2_subdev *subdevs[IDX_MAX];
	struct media_pipeline *m_pipeline;
};

/*
 * Media pipeline operations to be called from within the fimc(-lite)
 * video node when it is the last entity of the pipeline. Implemented
 * by corresponding media device driver.
 */
struct fimc_pipeline_ops {
	int (*open)(struct fimc_pipeline *p, struct media_entity *me,
			  bool resume);
	int (*close)(struct fimc_pipeline *p);
	int (*set_stream)(struct fimc_pipeline *p, bool state);
};

#define fimc_pipeline_call(f, op, p, args...)				\
	(!(f) ? -ENODEV : (((f)->pipeline_ops && (f)->pipeline_ops->op) ? \
			    (f)->pipeline_ops->op((p), ##args) : -ENOIOCTLCMD))

#endif /* S5P_FIMC_H_ */
