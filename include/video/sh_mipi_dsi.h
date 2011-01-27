/*
 * Public SH-mobile MIPI DSI header
 *
 * Copyright (C) 2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef VIDEO_SH_MIPI_DSI_H
#define VIDEO_SH_MIPI_DSI_H

enum sh_mipi_dsi_data_fmt {
	MIPI_RGB888,
	MIPI_RGB565,
	MIPI_RGB666_LP,
	MIPI_RGB666,
	MIPI_BGR888,
	MIPI_BGR565,
	MIPI_BGR666_LP,
	MIPI_BGR666,
	MIPI_YUYV,
	MIPI_UYVY,
	MIPI_YUV420_L,
	MIPI_YUV420,
};

struct sh_mobile_lcdc_chan_cfg;

#define SH_MIPI_DSI_HSABM	(1 << 0)
#define SH_MIPI_DSI_HSPBM	(1 << 1)

struct sh_mipi_dsi_info {
	enum sh_mipi_dsi_data_fmt	data_format;
	struct sh_mobile_lcdc_chan_cfg	*lcd_chan;
	unsigned long			flags;
	u32				clksrc;
	unsigned int			vsynw_offset;
};

#endif
