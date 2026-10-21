/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Copyright (c) 2026 Baylibre SAS.
 * Author: Valerio Setti <vsetti@baylibre.com>
 */

#ifndef _MESON_GX_FORMATTER_H
#define _MESON_GX_FORMATTER_H

#include "gx-interface.h"

struct platform_device;
struct regmap;
struct snd_soc_dapm_widget;
struct snd_kcontrol;

struct gx_formatter_hw {
	unsigned int skew_offset;
};

struct gx_formatter_ops {
	struct gx_stream *(*get_stream)(struct snd_soc_dapm_widget *w);
	void (*enable)(struct regmap *map);
	void (*disable)(struct regmap *map);
	int (*prepare)(struct regmap *map,
		       const struct gx_formatter_hw *quirks,
		       struct gx_stream *ts);
};

struct gx_formatter_driver {
	const struct snd_soc_component_driver *component_drv;
	const struct regmap_config *regmap_cfg;
	const struct gx_formatter_ops *ops;
	const struct gx_formatter_hw *quirks;
};

int gx_formatter_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *control,
		       int event);
int gx_formatter_probe(struct platform_device *pdev);

int gx_formatter_create(struct device *dev,
			struct snd_soc_dapm_widget *w,
			const struct gx_formatter_driver *drv,
			struct regmap *regmap);

/*
 * Formatter data is already freed when the associated device is removed,
 * so we just need to remove the pointer from the widget.
 */
static inline void gx_formatter_free(struct snd_soc_dapm_widget *w)
{
	w->priv = NULL;
}

#endif /* _MESON_GX_FORMATTER_H */
