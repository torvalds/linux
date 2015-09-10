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

#include <linux/types.h>

struct device;
struct v4l2_rect;

int vsp1_du_init(struct device *dev);

int vsp1_du_setup_lif(struct device *dev, unsigned int width,
		      unsigned int height);

int vsp1_du_atomic_begin(struct device *dev);
int vsp1_du_atomic_update(struct device *dev, unsigned int rpf, u32 pixelformat,
			  unsigned int pitch, dma_addr_t mem[2],
			  const struct v4l2_rect *src,
			  const struct v4l2_rect *dst);
int vsp1_du_atomic_flush(struct device *dev);

#endif /* __MEDIA_VSP1_H__ */
