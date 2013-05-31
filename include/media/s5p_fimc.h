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
#include <media/v4l2-dev.h>
#include <media/v4l2-mediabus.h>

/*
 * Enumeration of data inputs to the camera subsystem.
 */
enum fimc_input {
	FIMC_INPUT_PARALLEL_0	= 1,
	FIMC_INPUT_PARALLEL_1,
	FIMC_INPUT_MIPI_CSI2_0	= 3,
	FIMC_INPUT_MIPI_CSI2_1,
	FIMC_INPUT_WRITEBACK_A	= 5,
	FIMC_INPUT_WRITEBACK_B,
	FIMC_INPUT_WRITEBACK_ISP = 5,
};

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

#define fimc_input_is_parallel(x) ((x) == 1 || (x) == 2)
#define fimc_input_is_mipi_csi(x) ((x) == 3 || (x) == 4)

/*
 * The subdevices' group IDs.
 */
#define GRP_ID_SENSOR		(1 << 8)
#define GRP_ID_FIMC_IS_SENSOR	(1 << 9)
#define GRP_ID_WRITEBACK	(1 << 10)
#define GRP_ID_CSIS		(1 << 11)
#define GRP_ID_FIMC		(1 << 12)
#define GRP_ID_FLITE		(1 << 13)
#define GRP_ID_FIMC_IS		(1 << 14)

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

#define FIMC_MAX_PLANES	3

/**
 * struct fimc_fmt - color format data structure
 * @mbus_code: media bus pixel code, -1 if not applicable
 * @name: format description
 * @fourcc: fourcc code for this format, 0 if not applicable
 * @color: the driver's private color format id
 * @memplanes: number of physically non-contiguous data planes
 * @colplanes: number of physically contiguous data planes
 * @depth: per plane driver's private 'number of bits per pixel'
 * @mdataplanes: bitmask indicating meta data plane(s), (1 << plane_no)
 * @flags: flags indicating which operation mode format applies to
 */
struct fimc_fmt {
	enum v4l2_mbus_pixelcode mbus_code;
	char	*name;
	u32	fourcc;
	u32	color;
	u16	memplanes;
	u16	colplanes;
	u8	depth[FIMC_MAX_PLANES];
	u16	mdataplanes;
	u16	flags;
#define FMT_FLAGS_CAM		(1 << 0)
#define FMT_FLAGS_M2M_IN	(1 << 1)
#define FMT_FLAGS_M2M_OUT	(1 << 2)
#define FMT_FLAGS_M2M		(1 << 1 | 1 << 2)
#define FMT_HAS_ALPHA		(1 << 3)
#define FMT_FLAGS_COMPRESSED	(1 << 4)
#define FMT_FLAGS_WRITEBACK	(1 << 5)
#define FMT_FLAGS_RAW_BAYER	(1 << 6)
#define FMT_FLAGS_YUV		(1 << 7)
};

enum fimc_subdev_index {
	IDX_SENSOR,
	IDX_CSIS,
	IDX_FLITE,
	IDX_IS_ISP,
	IDX_FIMC,
	IDX_MAX,
};

struct media_pipeline;
struct v4l2_subdev;

struct fimc_pipeline {
	struct v4l2_subdev *subdevs[IDX_MAX];
	struct media_pipeline *m_pipeline;
};

struct exynos_video_entity {
	struct video_device vdev;
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
