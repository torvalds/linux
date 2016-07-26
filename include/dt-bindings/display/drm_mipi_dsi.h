/*
 * MIPI DSI Bus
 *
 * Copyright (c) Fuzhou Rockchip Electronics Co.Ltd
 * Authors:
 *       Mark Yao <yzq@rock-chips.com>
 *
 * based on include/drm/drm_mipi_dsi.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _DRM_MIPI_DSI_H__
#define _DRM_MIPI_DSI_H__

/* DSI mode flags */

/* video mode */
#define MIPI_DSI_MODE_VIDEO		(1 << 0)
/* video burst mode */
#define MIPI_DSI_MODE_VIDEO_BURST	(1 << 1)
/* video pulse mode */
#define MIPI_DSI_MODE_VIDEO_SYNC_PULSE	(1 << 2)
/* enable auto vertical count mode */
#define MIPI_DSI_MODE_VIDEO_AUTO_VERT	(1 << 3)
/* enable hsync-end packets in vsync-pulse and v-porch area */
#define MIPI_DSI_MODE_VIDEO_HSE		(1 << 4)
/* disable hfront-porch area */
#define MIPI_DSI_MODE_VIDEO_HFP		(1 << 5)
/* disable hback-porch area */
#define MIPI_DSI_MODE_VIDEO_HBP		(1 << 6)
/* disable hsync-active area */
#define MIPI_DSI_MODE_VIDEO_HSA		(1 << 7)
/* flush display FIFO on vsync pulse */
#define MIPI_DSI_MODE_VSYNC_FLUSH	(1 << 8)
/* disable EoT packets in HS mode */
#define MIPI_DSI_MODE_EOT_PACKET	(1 << 9)
/* device supports non-continuous clock behavior (DSI spec 5.6.1) */
#define MIPI_DSI_CLOCK_NON_CONTINUOUS	(1 << 10)
/* transmit data in low power */
#define MIPI_DSI_MODE_LPM		(1 << 11)

#define MIPI_DSI_FMT_RGB888		0
#define MIPI_DSI_FMT_RGB666		1
#define MIPI_DSI_FMT_RGB666_PACKED	2
#define MIPI_DSI_FMT_RGB565		3

#endif /* __DRM_MIPI_DSI__ */
