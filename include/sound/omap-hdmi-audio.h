/*
 * hdmi-audio.c -- OMAP4+ DSS HDMI audio support library
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Jyri Sarha <jsarha@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <video/omapdss.h>

#ifndef __OMAP_HDMI_AUDIO_H__
#define __OMAP_HDMI_AUDIO_H__

struct omap_hdmi_audio_ops {
	int (*audio_startup)(struct device *dev,
			     void (*abort_cb)(struct device *dev));
	int (*audio_shutdown)(struct device *dev);
	int (*audio_start)(struct device *dev);
	void (*audio_stop)(struct device *dev);
	int (*audio_config)(struct device *dev,
			    struct omap_dss_audio *dss_audio);
};

/* HDMI audio initalization data */
struct omap_hdmi_audio_pdata {
	struct device *dev;
	enum omapdss_version dss_version;
	phys_addr_t audio_dma_addr;

	const struct omap_hdmi_audio_ops *ops;
};

#endif /* __OMAP_HDMI_AUDIO_H__ */
