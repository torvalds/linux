// SPDX-License-Identifier: (GPL-2.0 OR MIT)
//
// Copyright (c) 2018 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "axg-tdm-formatter.h"

#define TDMIN_CTRL			0x00
#define  TDMIN_CTRL_ENABLE		BIT(31)
#define  TDMIN_CTRL_I2S_MODE		BIT(30)
#define  TDMIN_CTRL_RST_OUT		BIT(29)
#define  TDMIN_CTRL_RST_IN		BIT(28)
#define  TDMIN_CTRL_WS_INV		BIT(25)
#define  TDMIN_CTRL_SEL_SHIFT		20
#define  TDMIN_CTRL_IN_BIT_SKEW_MASK	GENMASK(18, 16)
#define  TDMIN_CTRL_IN_BIT_SKEW(x)	((x) << 16)
#define  TDMIN_CTRL_LSB_FIRST		BIT(5)
#define  TDMIN_CTRL_BITNUM_MASK	GENMASK(4, 0)
#define  TDMIN_CTRL_BITNUM(x)		((x) << 0)
#define TDMIN_SWAP			0x04
#define TDMIN_MASK0			0x08
#define TDMIN_MASK1			0x0c
#define TDMIN_MASK2			0x10
#define TDMIN_MASK3			0x14
#define TDMIN_STAT			0x18
#define TDMIN_MUTE_VAL			0x1c
#define TDMIN_MUTE0			0x20
#define TDMIN_MUTE1			0x24
#define TDMIN_MUTE2			0x28
#define TDMIN_MUTE3			0x2c

static const struct regmap_config axg_tdmin_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= TDMIN_MUTE3,
};

static const char * const axg_tdmin_sel_texts[] = {
	"IN 0", "IN 1", "IN 2",  "IN 3",  "IN 4",  "IN 5",  "IN 6",  "IN 7",
	"IN 8", "IN 9", "IN 10", "IN 11", "IN 12", "IN 13", "IN 14", "IN 15",
};

/* Change to special mux control to reset dapm */
static SOC_ENUM_SINGLE_DECL(axg_tdmin_sel_enum, TDMIN_CTRL,
			    TDMIN_CTRL_SEL_SHIFT, axg_tdmin_sel_texts);

static const struct snd_kcontrol_new axg_tdmin_in_mux =
	SOC_DAPM_ENUM("Input Source", axg_tdmin_sel_enum);

static struct snd_soc_dai *
axg_tdmin_get_be(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p = NULL;
	struct snd_soc_dai *be;

	snd_soc_dapm_widget_for_each_source_path(w, p) {
		if (!p->connect)
			continue;

		if (p->source->id == snd_soc_dapm_dai_out)
			return (struct snd_soc_dai *)p->source->priv;

		be = axg_tdmin_get_be(p->source);
		if (be)
			return be;
	}

	return NULL;
}

static struct axg_tdm_stream *
axg_tdmin_get_tdm_stream(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dai *be = axg_tdmin_get_be(w);

	if (!be)
		return NULL;

	return be->capture_dma_data;
}

static void axg_tdmin_enable(struct regmap *map)
{
	/* Apply both reset */
	regmap_update_bits(map, TDMIN_CTRL,
			   TDMIN_CTRL_RST_OUT | TDMIN_CTRL_RST_IN, 0);

	/* Clear out reset before in reset */
	regmap_update_bits(map, TDMIN_CTRL,
			   TDMIN_CTRL_RST_OUT, TDMIN_CTRL_RST_OUT);
	regmap_update_bits(map, TDMIN_CTRL,
			   TDMIN_CTRL_RST_IN,  TDMIN_CTRL_RST_IN);

	/* Actually enable tdmin */
	regmap_update_bits(map, TDMIN_CTRL,
			   TDMIN_CTRL_ENABLE, TDMIN_CTRL_ENABLE);
}

static void axg_tdmin_disable(struct regmap *map)
{
	regmap_update_bits(map, TDMIN_CTRL, TDMIN_CTRL_ENABLE, 0);
}

static int axg_tdmin_prepare(struct regmap *map,
			     const struct axg_tdm_formatter_hw *quirks,
			     struct axg_tdm_stream *ts)
{
	unsigned int val, skew = quirks->skew_offset;

	/* Set stream skew */
	switch (ts->iface->fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_DSP_A:
		skew += 1;
		break;

	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_DSP_B:
		break;

	default:
		pr_err("Unsupported format: %u\n",
		       ts->iface->fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	val = TDMIN_CTRL_IN_BIT_SKEW(skew);

	/* Set stream format mode */
	switch (ts->iface->fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
		val |= TDMIN_CTRL_I2S_MODE;
		break;
	}

	/* If the sample clock is inverted, invert it back for the formatter */
	if (axg_tdm_lrclk_invert(ts->iface->fmt))
		val |= TDMIN_CTRL_WS_INV;

	/* Set the slot width */
	val |= TDMIN_CTRL_BITNUM(ts->iface->slot_width - 1);

	/*
	 * The following also reset LSB_FIRST which result in the formatter
	 * placing the first bit received at bit 31
	 */
	regmap_update_bits(map, TDMIN_CTRL,
			   (TDMIN_CTRL_IN_BIT_SKEW_MASK | TDMIN_CTRL_WS_INV |
			    TDMIN_CTRL_I2S_MODE | TDMIN_CTRL_LSB_FIRST |
			    TDMIN_CTRL_BITNUM_MASK), val);

	/* Set static swap mask configuration */
	regmap_write(map, TDMIN_SWAP, 0x76543210);

	return axg_tdm_formatter_set_channel_masks(map, ts, TDMIN_MASK0);
}

static const struct snd_soc_dapm_widget axg_tdmin_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("IN 0",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 1",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 2",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 3",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 4",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 5",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 6",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 7",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 8",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 9",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 10", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 11", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 12", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 13", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 14", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 15", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_MUX("SRC SEL", SND_SOC_NOPM, 0, 0, &axg_tdmin_in_mux),
	SND_SOC_DAPM_PGA_E("DEC", SND_SOC_NOPM, 0, 0, NULL, 0,
			   axg_tdm_formatter_event,
			   (SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD)),
	SND_SOC_DAPM_AIF_OUT("OUT", NULL, 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route axg_tdmin_dapm_routes[] = {
	{ "SRC SEL", "IN 0",  "IN 0" },
	{ "SRC SEL", "IN 1",  "IN 1" },
	{ "SRC SEL", "IN 2",  "IN 2" },
	{ "SRC SEL", "IN 3",  "IN 3" },
	{ "SRC SEL", "IN 4",  "IN 4" },
	{ "SRC SEL", "IN 5",  "IN 5" },
	{ "SRC SEL", "IN 6",  "IN 6" },
	{ "SRC SEL", "IN 7",  "IN 7" },
	{ "SRC SEL", "IN 8",  "IN 8" },
	{ "SRC SEL", "IN 9",  "IN 9" },
	{ "SRC SEL", "IN 10", "IN 10" },
	{ "SRC SEL", "IN 11", "IN 11" },
	{ "SRC SEL", "IN 12", "IN 12" },
	{ "SRC SEL", "IN 13", "IN 13" },
	{ "SRC SEL", "IN 14", "IN 14" },
	{ "SRC SEL", "IN 15", "IN 15" },
	{ "DEC", NULL, "SRC SEL" },
	{ "OUT", NULL, "DEC" },
};

static const struct snd_soc_component_driver axg_tdmin_component_drv = {
	.dapm_widgets		= axg_tdmin_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(axg_tdmin_dapm_widgets),
	.dapm_routes		= axg_tdmin_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(axg_tdmin_dapm_routes),
};

static const struct axg_tdm_formatter_ops axg_tdmin_ops = {
	.get_stream	= axg_tdmin_get_tdm_stream,
	.prepare	= axg_tdmin_prepare,
	.enable		= axg_tdmin_enable,
	.disable	= axg_tdmin_disable,
};

static const struct axg_tdm_formatter_driver axg_tdmin_drv = {
	.component_drv	= &axg_tdmin_component_drv,
	.regmap_cfg	= &axg_tdmin_regmap_cfg,
	.ops		= &axg_tdmin_ops,
	.quirks		= &(const struct axg_tdm_formatter_hw) {
		.invert_sclk	= false,
		.skew_offset	= 2,
	},
};

static const struct of_device_id axg_tdmin_of_match[] = {
	{
		.compatible = "amlogic,axg-tdmin",
		.data = &axg_tdmin_drv,
	}, {}
};
MODULE_DEVICE_TABLE(of, axg_tdmin_of_match);

static struct platform_driver axg_tdmin_pdrv = {
	.probe = axg_tdm_formatter_probe,
	.driver = {
		.name = "axg-tdmin",
		.of_match_table = axg_tdmin_of_match,
	},
};
module_platform_driver(axg_tdmin_pdrv);

MODULE_DESCRIPTION("Amlogic AXG TDM input formatter driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
