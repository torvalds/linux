/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * hdmi-audio.c -- OMAP4+ DSS HDMI audio support library
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - https://www.ti.com
 *
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#ifndef __OMAP_HDMI_AUDIO_H__
#define __OMAP_HDMI_AUDIO_H__

#include <linux/platform_data/omapdss.h>

struct omap_dss_audio {
	struct snd_aes_iec958 *iec;
	struct snd_cea_861_aud_if *cea;
};

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
	unsigned int version;
	phys_addr_t audio_dma_addr;

	const struct omap_hdmi_audio_ops *ops;
};

#endif /* __OMAP_HDMI_AUDIO_H__ */
