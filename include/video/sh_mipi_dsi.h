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
#define SH_MIPI_DSI_HBPBM	(1 << 1)
#define SH_MIPI_DSI_HFPBM	(1 << 2)
#define SH_MIPI_DSI_BL2E	(1 << 3)
#define SH_MIPI_DSI_VSEE	(1 << 4)
#define SH_MIPI_DSI_HSEE	(1 << 5)
#define SH_MIPI_DSI_HSAE	(1 << 6)

#define SH_MIPI_DSI_HSbyteCLK	(1 << 24)
#define SH_MIPI_DSI_HS6divCLK	(1 << 25)
#define SH_MIPI_DSI_HS4divCLK	(1 << 26)

#define SH_MIPI_DSI_SYNC_PULSES_MODE	(SH_MIPI_DSI_VSEE | \
					 SH_MIPI_DSI_HSEE | \
					 SH_MIPI_DSI_HSAE)
#define SH_MIPI_DSI_SYNC_EVENTS_MODE	(0)
#define SH_MIPI_DSI_SYNC_BURST_MODE	(SH_MIPI_DSI_BL2E)

struct sh_mipi_dsi_info {
	enum sh_mipi_dsi_data_fmt	data_format;
	struct sh_mobile_lcdc_chan_cfg	*lcd_chan;
	int				lane;
	unsigned long			flags;
	u32				clksrc;
	unsigned int			vsynw_offset;
	int	(*set_dot_clock)(struct platform_device *pdev,
				 void __iomem *base,
				 int enable);
};

#endif
