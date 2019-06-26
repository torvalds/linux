/* SPDX-License-Identifier: (GPL-2.0 OR MIT)
 *
 * Copyright (c) 2018 Baylibre SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef _MESON_AXG_TDM_FORMATTER_H
#define _MESON_AXG_TDM_FORMATTER_H

#include "axg-tdm.h"

struct platform_device;
struct regmap;
struct snd_soc_dapm_widget;
struct snd_kcontrol;

struct axg_tdm_formatter_ops {
	struct axg_tdm_stream *(*get_stream)(struct snd_soc_dapm_widget *w);
	void (*enable)(struct regmap *map);
	void (*disable)(struct regmap *map);
	int (*prepare)(struct regmap *map, struct axg_tdm_stream *ts);
};

struct axg_tdm_formatter_driver {
	const struct snd_soc_component_driver *component_drv;
	const struct regmap_config *regmap_cfg;
	const struct axg_tdm_formatter_ops *ops;
	bool invert_sclk;
};

int axg_tdm_formatter_set_channel_masks(struct regmap *map,
					struct axg_tdm_stream *ts,
					unsigned int offset);
int axg_tdm_formatter_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *control,
			    int event);
int axg_tdm_formatter_probe(struct platform_device *pdev);

#endif /* _MESON_AXG_TDM_FORMATTER_H */
