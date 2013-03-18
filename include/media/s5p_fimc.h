/*
 * Samsung S5P/Exynos4 SoC series camera interface driver header
 *
 * Copyright (C) 2010 - 2013 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef S5P_FIMC_H_
#define S5P_FIMC_H_

#include <media/media-entity.h>

/*
 * Enumeration of the FIMC data bus types.
 */
enum fimc_bus_type {
	/* Camera parallel bus */
	FIMC_BUS_TYPE_ITU_601 = 1,
	/* Camera parallel bus with embedded synchronization */
	FIMC_BUS_TYPE_ITU_656,
	/* Camera MIPI-CSI2 serial bus */
	FIMC_BUS_TYPE_MIPI_CSI2,
	/* FIFO link from LCD controller (WriteBack A) */
	FIMC_BUS_TYPE_LCD_WRITEBACK_A,
	/* FIFO link from LCD controller (WriteBack B) */
	FIMC_BUS_TYPE_LCD_WRITEBACK_B,
	/* FIFO link from FIMC-IS */
	FIMC_BUS_TYPE_ISP_WRITEBACK = FIMC_BUS_TYPE_LCD_WRITEBACK_B,
};

struct i2c_board_info;

/**
 * struct fimc_source_info - video source description required for the host
 *			     interface configuration
 *
 * @board_info: pointer to I2C subdevice's board info
 * @clk_frequency: frequency of the clock the host interface provides to sensor
 * @fimc_bus_type: FIMC camera input type
 * @sensor_bus_type: image sensor bus type, MIPI, ITU-R BT.601 etc.
 * @flags: the parallel sensor bus flags defining signals polarity (V4L2_MBUS_*)
 * @i2c_bus_num: i2c control bus id the sensor is attached to
 * @mux_id: FIMC camera interface multiplexer index (separate for MIPI and ITU)
 * @clk_id: index of the SoC peripheral clock for sensors
 */
struct fimc_source_info {
	struct i2c_board_info *board_info;
	unsigned long clk_frequency;
	enum fimc_bus_type fimc_bus_type;
	enum fimc_bus_type sensor_bus_type;
	u16 flags;
	u16 i2c_bus_num;
	u16 mux_id;
	u8 clk_id;
};

/**
 * struct s5p_platform_fimc - camera host interface platform data
 *
 * @source_info: properties of an image source for the host interface setup
 * @num_clients: the number of attached image sources
 */
struct s5p_platform_fimc {
	struct fimc_source_info *source_info;
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
