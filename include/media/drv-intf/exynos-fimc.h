/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Samsung S5P/Exynos4 SoC series camera interface driver header
 *
 * Copyright (C) 2010 - 2013 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
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

/**
 * struct fimc_source_info - video source description required for the host
 *			     interface configuration
 *
 * @fimc_bus_type: FIMC camera input type
 * @sensor_bus_type: image sensor bus type, MIPI, ITU-R BT.601 etc.
 * @flags: the parallel sensor bus flags defining signals polarity (V4L2_MBUS_*)
 * @mux_id: FIMC camera interface multiplexer index (separate for MIPI and ITU)
 */
struct fimc_source_info {
	enum fimc_bus_type fimc_bus_type;
	enum fimc_bus_type sensor_bus_type;
	u16 flags;
	u16 mux_id;
};

/*
 * v4l2_device notification id. This is only for internal use in the kernel.
 * Sensor subdevs should issue S5P_FIMC_TX_END_NOTIFY notification in single
 * frame capture mode when there is only one VSYNC pulse issued by the sensor
 * at beginning of the frame transmission.
 */
#define S5P_FIMC_TX_END_NOTIFY _IO('e', 0)

#define FIMC_MAX_PLANES	3

/**
 * struct fimc_fmt - color format data structure
 * @mbus_code: media bus pixel code, -1 if not applicable
 * @fourcc: fourcc code for this format, 0 if not applicable
 * @color: the driver's private color format id
 * @memplanes: number of physically non-contiguous data planes
 * @colplanes: number of physically contiguous data planes
 * @colorspace: v4l2 colorspace (V4L2_COLORSPACE_*)
 * @depth: per plane driver's private 'number of bits per pixel'
 * @mdataplanes: bitmask indicating meta data plane(s), (1 << plane_no)
 * @flags: flags indicating which operation mode format applies to
 */
struct fimc_fmt {
	u32 mbus_code;
	u32	fourcc;
	u32	color;
	u16	memplanes;
	u16	colplanes;
	u8	colorspace;
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

struct exynos_media_pipeline;

/*
 * Media pipeline operations to be called from within a video node,  i.e. the
 * last entity within the pipeline. Implemented by related media device driver.
 */
struct exynos_media_pipeline_ops {
	int (*prepare)(struct exynos_media_pipeline *p,
						struct media_entity *me);
	int (*unprepare)(struct exynos_media_pipeline *p);
	int (*open)(struct exynos_media_pipeline *p, struct media_entity *me,
							bool resume);
	int (*close)(struct exynos_media_pipeline *p);
	int (*set_stream)(struct exynos_media_pipeline *p, bool state);
};

struct exynos_video_entity {
	struct video_device vdev;
	struct exynos_media_pipeline *pipe;
};

struct exynos_media_pipeline {
	struct media_pipeline mp;
	const struct exynos_media_pipeline_ops *ops;
};

static inline struct exynos_video_entity *vdev_to_exynos_video_entity(
					struct video_device *vdev)
{
	return container_of(vdev, struct exynos_video_entity, vdev);
}

#define fimc_pipeline_call(ent, op, args...)				  \
	((!(ent) || !(ent)->pipe) ? -ENOENT : \
	(((ent)->pipe->ops && (ent)->pipe->ops->op) ? \
	(ent)->pipe->ops->op(((ent)->pipe), ##args) : -ENOIOCTLCMD))	  \

#endif /* S5P_FIMC_H_ */
