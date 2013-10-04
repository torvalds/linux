/*
 * vsp1.h  --  R-Car VSP1 Platform Data
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __PLATFORM_VSP1_H__
#define __PLATFORM_VSP1_H__

#define VSP1_HAS_LIF		(1 << 0)

struct vsp1_platform_data {
	unsigned int features;
	unsigned int rpf_count;
	unsigned int uds_count;
	unsigned int wpf_count;
};

#endif /* __PLATFORM_VSP1_H__ */
