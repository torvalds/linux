/*
 * SH-Mobile High-Definition Multimedia Interface (HDMI)
 *
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SH_MOBILE_HDMI_H
#define SH_MOBILE_HDMI_H

struct sh_mobile_lcdc_chan_cfg;
struct device;

struct sh_mobile_hdmi_info {
	struct sh_mobile_lcdc_chan_cfg	*lcd_chan;
	struct device			*lcd_dev;
};

#endif
