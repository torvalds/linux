/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Copyright (c) 2026 Baylibre SAS.
 * Author: Valerio Setti <vsetti@baylibre.com>
 */

#ifndef _MESON_GX_INTERFACE_H
#define _MESON_GX_INTERFACE_H

#include <linux/clk.h>
#include <linux/regmap.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

struct gx_iface {
	struct clk *mclk;
	unsigned long mclk_rate;

	/* format is common to all the DAIs of the iface */
	unsigned int fmt;
};

struct gx_stream {
	struct gx_iface *iface;
	struct list_head formatter_list;
	struct mutex lock;
	unsigned int channels;
	unsigned int width;
	unsigned int physical_width;
	bool ready;

	/* For continuous clock tracking */
	bool clk_enabled;
};

struct gx_stream *gx_stream_alloc(struct gx_iface *iface);
void gx_stream_free(struct gx_stream *ts);
int gx_stream_start(struct gx_stream *ts);
void gx_stream_stop(struct gx_stream *ts);

#endif /* _MESON_GX_INTERFACE_H */
