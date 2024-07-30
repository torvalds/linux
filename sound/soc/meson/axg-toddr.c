// SPDX-License-Identifier: (GPL-2.0 OR MIT)
//
// Copyright (c) 2018 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

/* This driver implements the frontend capture DAI of AXG based SoCs */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "axg-fifo.h"

#define CTRL0_TODDR_SEL_RESAMPLE	BIT(30)
#define CTRL0_TODDR_EXT_SIGNED		BIT(29)
#define CTRL0_TODDR_PP_MODE		BIT(28)
#define CTRL0_TODDR_SYNC_CH		BIT(27)
#define CTRL0_TODDR_TYPE		GENMASK(15, 13)
#define CTRL0_TODDR_MSB_POS		GENMASK(12, 8)
#define CTRL0_TODDR_LSB_POS		GENMASK(7, 3)
#define CTRL1_TODDR_FORCE_FINISH	BIT(25)
#define CTRL1_SEL_SHIFT			28

#define TODDR_MSB_POS	31

static int axg_toddr_pcm_new(struct snd_soc_pcm_runtime *rtd,
			     struct snd_soc_dai *dai)
{
	return axg_fifo_pcm_new(rtd, SNDRV_PCM_STREAM_CAPTURE);
}

static int g12a_toddr_dai_prepare(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct axg_fifo *fifo = snd_soc_dai_get_drvdata(dai);

	/* Reset the write pointer to the FIFO_INIT_ADDR */
	regmap_update_bits(fifo->map, FIFO_CTRL1,
			   CTRL1_TODDR_FORCE_FINISH, 0);
	regmap_update_bits(fifo->map, FIFO_CTRL1,
			   CTRL1_TODDR_FORCE_FINISH, CTRL1_TODDR_FORCE_FINISH);
	regmap_update_bits(fifo->map, FIFO_CTRL1,
			   CTRL1_TODDR_FORCE_FINISH, 0);

	return 0;
}

static int axg_toddr_dai_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct axg_fifo *fifo = snd_soc_dai_get_drvdata(dai);
	unsigned int type, width;

	switch (params_physical_width(params)) {
	case 8:
		type = 0; /* 8 samples of 8 bits */
		break;
	case 16:
		type = 2; /* 4 samples of 16 bits - right justified */
		break;
	case 32:
		type = 4; /* 2 samples of 32 bits - right justified */
		break;
	default:
		return -EINVAL;
	}

	width = params_width(params);

	regmap_update_bits(fifo->map, FIFO_CTRL0,
			   CTRL0_TODDR_TYPE |
			   CTRL0_TODDR_MSB_POS |
			   CTRL0_TODDR_LSB_POS,
			   FIELD_PREP(CTRL0_TODDR_TYPE, type) |
			   FIELD_PREP(CTRL0_TODDR_MSB_POS, TODDR_MSB_POS) |
			   FIELD_PREP(CTRL0_TODDR_LSB_POS, TODDR_MSB_POS - (width - 1)));

	return 0;
}

static int axg_toddr_dai_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct axg_fifo *fifo = snd_soc_dai_get_drvdata(dai);
	int ret;

	/* Enable pclk to access registers and clock the fifo ip */
	ret = clk_prepare_enable(fifo->pclk);
	if (ret)
		return ret;

	/* Select orginal data - resampling not supported ATM */
	regmap_update_bits(fifo->map, FIFO_CTRL0, CTRL0_TODDR_SEL_RESAMPLE, 0);

	/* Only signed format are supported ATM */
	regmap_update_bits(fifo->map, FIFO_CTRL0, CTRL0_TODDR_EXT_SIGNED,
			   CTRL0_TODDR_EXT_SIGNED);

	/* Apply single buffer mode to the interface */
	regmap_update_bits(fifo->map, FIFO_CTRL0, CTRL0_TODDR_PP_MODE, 0);

	return 0;
}

static void axg_toddr_dai_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct axg_fifo *fifo = snd_soc_dai_get_drvdata(dai);

	clk_disable_unprepare(fifo->pclk);
}

static const struct snd_soc_dai_ops axg_toddr_ops = {
	.hw_params	= axg_toddr_dai_hw_params,
	.startup	= axg_toddr_dai_startup,
	.shutdown	= axg_toddr_dai_shutdown,
	.pcm_new	= axg_toddr_pcm_new,
};

static struct snd_soc_dai_driver axg_toddr_dai_drv = {
	.name = "TODDR",
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= AXG_FIFO_CH_MAX,
		.rates		= SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min	= 5515,
		.rate_max	= 768000,
		.formats	= AXG_FIFO_FORMATS,
	},
	.ops		= &axg_toddr_ops,
};

static const char * const axg_toddr_sel_texts[] = {
	"IN 0", "IN 1", "IN 2", "IN 3", "IN 4", "IN 5", "IN 6", "IN 7"
};

static SOC_ENUM_SINGLE_DECL(axg_toddr_sel_enum, FIFO_CTRL0, CTRL0_SEL_SHIFT,
			    axg_toddr_sel_texts);

static const struct snd_kcontrol_new axg_toddr_in_mux =
	SOC_DAPM_ENUM("Input Source", axg_toddr_sel_enum);

static const struct snd_soc_dapm_widget axg_toddr_dapm_widgets[] = {
	SND_SOC_DAPM_MUX("SRC SEL", SND_SOC_NOPM, 0, 0, &axg_toddr_in_mux),
	SND_SOC_DAPM_AIF_IN("IN 0", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 2", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 3", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 4", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 5", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 6", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("IN 7", NULL, 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route axg_toddr_dapm_routes[] = {
	{ "Capture", NULL, "SRC SEL" },
	{ "SRC SEL", "IN 0", "IN 0" },
	{ "SRC SEL", "IN 1", "IN 1" },
	{ "SRC SEL", "IN 2", "IN 2" },
	{ "SRC SEL", "IN 3", "IN 3" },
	{ "SRC SEL", "IN 4", "IN 4" },
	{ "SRC SEL", "IN 5", "IN 5" },
	{ "SRC SEL", "IN 6", "IN 6" },
	{ "SRC SEL", "IN 7", "IN 7" },
};

static const struct snd_soc_component_driver axg_toddr_component_drv = {
	.dapm_widgets		= axg_toddr_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(axg_toddr_dapm_widgets),
	.dapm_routes		= axg_toddr_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(axg_toddr_dapm_routes),
	.open			= axg_fifo_pcm_open,
	.close			= axg_fifo_pcm_close,
	.hw_params		= axg_fifo_pcm_hw_params,
	.hw_free		= axg_fifo_pcm_hw_free,
	.pointer		= axg_fifo_pcm_pointer,
	.trigger		= axg_fifo_pcm_trigger,
	.legacy_dai_naming	= 1,
};

static const struct axg_fifo_match_data axg_toddr_match_data = {
	.field_threshold	= REG_FIELD(FIFO_CTRL1, 16, 23),
	.component_drv		= &axg_toddr_component_drv,
	.dai_drv		= &axg_toddr_dai_drv
};

static int g12a_toddr_dai_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct axg_fifo *fifo = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = axg_toddr_dai_startup(substream, dai);
	if (ret)
		return ret;

	/*
	 * Make sure the first channel ends up in the at beginning of the output
	 * As weird as it looks, without this the first channel may be misplaced
	 * in memory, with a random shift of 2 channels.
	 */
	regmap_update_bits(fifo->map, FIFO_CTRL0, CTRL0_TODDR_SYNC_CH,
			   CTRL0_TODDR_SYNC_CH);

	return 0;
}

static const struct snd_soc_dai_ops g12a_toddr_ops = {
	.prepare	= g12a_toddr_dai_prepare,
	.hw_params	= axg_toddr_dai_hw_params,
	.startup	= g12a_toddr_dai_startup,
	.shutdown	= axg_toddr_dai_shutdown,
	.pcm_new	= axg_toddr_pcm_new,
};

static struct snd_soc_dai_driver g12a_toddr_dai_drv = {
	.name = "TODDR",
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= AXG_FIFO_CH_MAX,
		.rates		= SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min	= 5515,
		.rate_max	= 768000,
		.formats	= AXG_FIFO_FORMATS,
	},
	.ops		= &g12a_toddr_ops,
};

static const struct snd_soc_component_driver g12a_toddr_component_drv = {
	.dapm_widgets		= axg_toddr_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(axg_toddr_dapm_widgets),
	.dapm_routes		= axg_toddr_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(axg_toddr_dapm_routes),
	.open			= axg_fifo_pcm_open,
	.close			= axg_fifo_pcm_close,
	.hw_params		= g12a_fifo_pcm_hw_params,
	.hw_free		= axg_fifo_pcm_hw_free,
	.pointer		= axg_fifo_pcm_pointer,
	.trigger		= axg_fifo_pcm_trigger,
	.legacy_dai_naming	= 1,
};

static const struct axg_fifo_match_data g12a_toddr_match_data = {
	.field_threshold	= REG_FIELD(FIFO_CTRL1, 16, 23),
	.component_drv		= &g12a_toddr_component_drv,
	.dai_drv		= &g12a_toddr_dai_drv
};

static const char * const sm1_toddr_sel_texts[] = {
	"IN 0", "IN 1", "IN 2",  "IN 3",  "IN 4",  "IN 5",  "IN 6",  "IN 7",
	"IN 8", "IN 9", "IN 10", "IN 11", "IN 12", "IN 13", "IN 14", "IN 15"
};

static SOC_ENUM_SINGLE_DECL(sm1_toddr_sel_enum, FIFO_CTRL1, CTRL1_SEL_SHIFT,
			    sm1_toddr_sel_texts);

static const struct snd_kcontrol_new sm1_toddr_in_mux =
	SOC_DAPM_ENUM("Input Source", sm1_toddr_sel_enum);

static const struct snd_soc_dapm_widget sm1_toddr_dapm_widgets[] = {
	SND_SOC_DAPM_MUX("SRC SEL", SND_SOC_NOPM, 0, 0, &sm1_toddr_in_mux),
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
};

static const struct snd_soc_dapm_route sm1_toddr_dapm_routes[] = {
	{ "Capture", NULL, "SRC SEL" },
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
};

static const struct snd_soc_component_driver sm1_toddr_component_drv = {
	.dapm_widgets		= sm1_toddr_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sm1_toddr_dapm_widgets),
	.dapm_routes		= sm1_toddr_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(sm1_toddr_dapm_routes),
	.open			= axg_fifo_pcm_open,
	.close			= axg_fifo_pcm_close,
	.hw_params		= g12a_fifo_pcm_hw_params,
	.hw_free		= axg_fifo_pcm_hw_free,
	.pointer		= axg_fifo_pcm_pointer,
	.trigger		= axg_fifo_pcm_trigger,
	.legacy_dai_naming	= 1,
};

static const struct axg_fifo_match_data sm1_toddr_match_data = {
	.field_threshold	= REG_FIELD(FIFO_CTRL1, 12, 23),
	.component_drv		= &sm1_toddr_component_drv,
	.dai_drv		= &g12a_toddr_dai_drv
};

static const struct of_device_id axg_toddr_of_match[] = {
	{
		.compatible = "amlogic,axg-toddr",
		.data = &axg_toddr_match_data,
	}, {
		.compatible = "amlogic,g12a-toddr",
		.data = &g12a_toddr_match_data,
	}, {
		.compatible = "amlogic,sm1-toddr",
		.data = &sm1_toddr_match_data,
	}, {}
};
MODULE_DEVICE_TABLE(of, axg_toddr_of_match);

static struct platform_driver axg_toddr_pdrv = {
	.probe = axg_fifo_probe,
	.driver = {
		.name = "axg-toddr",
		.of_match_table = axg_toddr_of_match,
	},
};
module_platform_driver(axg_toddr_pdrv);

MODULE_DESCRIPTION("Amlogic AXG capture fifo driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
