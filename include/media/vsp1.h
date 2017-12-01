/*
 * vsp1.h  --  R-Car VSP1 API
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __MEDIA_VSP1_H__
#define __MEDIA_VSP1_H__

#include <linux/scatterlist.h>
#include <linux/types.h>
#include <linux/videodev2.h>

struct device;

int vsp1_du_init(struct device *dev);

/**
 * struct vsp1_du_lif_config - VSP LIF configuration
 * @width: output frame width
 * @height: output frame height
 * @callback: frame completion callback function (optional). When a callback
 *	      is provided, the VSP driver guarantees that it will be called once
 *	      and only once for each vsp1_du_atomic_flush() call.
 * @callback_data: data to be passed to the frame completion callback
 */
struct vsp1_du_lif_config {
	unsigned int width;
	unsigned int height;

	void (*callback)(void *data, bool completed, u32 crc);
	void *callback_data;
};

int vsp1_du_setup_lif(struct device *dev, unsigned int pipe_index,
		      const struct vsp1_du_lif_config *cfg);

/**
 * struct vsp1_du_atomic_config - VSP atomic configuration parameters
 * @pixelformat: plane pixel format (V4L2 4CC)
 * @pitch: line pitch in bytes, for all planes
 * @mem: DMA memory address for each plane of the frame buffer
 * @src: source rectangle in the frame buffer (integer coordinates)
 * @dst: destination rectangle on the display (integer coordinates)
 * @alpha: alpha value (0: fully transparent, 255: fully opaque)
 * @zpos: Z position of the plane (from 0 to number of planes minus 1)
 */
struct vsp1_du_atomic_config {
	u32 pixelformat;
	unsigned int pitch;
	dma_addr_t mem[3];
	struct v4l2_rect src;
	struct v4l2_rect dst;
	unsigned int alpha;
	unsigned int zpos;
};

/**
 * enum vsp1_du_crc_source - Source used for CRC calculation
 * @VSP1_DU_CRC_NONE: CRC calculation disabled
 * @VSP1_DU_CRC_PLANE: Perform CRC calculation on an input plane
 * @VSP1_DU_CRC_OUTPUT: Perform CRC calculation on the composed output
 */
enum vsp1_du_crc_source {
	VSP1_DU_CRC_NONE,
	VSP1_DU_CRC_PLANE,
	VSP1_DU_CRC_OUTPUT,
};

/**
 * struct vsp1_du_crc_config - VSP CRC computation configuration parameters
 * @source: source for CRC calculation
 * @index: index of the CRC source plane (when source is set to plane)
 */
struct vsp1_du_crc_config {
	enum vsp1_du_crc_source source;
	unsigned int index;
};

/**
 * struct vsp1_du_atomic_pipe_config - VSP atomic pipe configuration parameters
 * @crc: CRC computation configuration
 */
struct vsp1_du_atomic_pipe_config {
	struct vsp1_du_crc_config crc;
};

void vsp1_du_atomic_begin(struct device *dev, unsigned int pipe_index);
int vsp1_du_atomic_update(struct device *dev, unsigned int pipe_index,
			  unsigned int rpf,
			  const struct vsp1_du_atomic_config *cfg);
void vsp1_du_atomic_flush(struct device *dev, unsigned int pipe_index,
			  const struct vsp1_du_atomic_pipe_config *cfg);
int vsp1_du_map_sg(struct device *dev, struct sg_table *sgt);
void vsp1_du_unmap_sg(struct device *dev, struct sg_table *sgt);

#endif /* __MEDIA_VSP1_H__ */
