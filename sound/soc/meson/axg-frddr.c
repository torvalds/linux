// SPDX-License-Identifier: (GPL-2.0 OR MIT)
//
// Copyright (c) 2018 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

/* This driver implements the frontend playback DAI of AXG based SoCs */

#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "axg-fifo.h"

#define CTRL0_FRDDR_PP_MODE	BIT(30)

static int axg_frddr_dai_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct axg_fifo *fifo = snd_soc_dai_get_drvdata(dai);
	unsigned int fifo_depth, fifo_threshold;
	int ret;

	/* Enable pclk to access registers and clock the fifo ip */
	ret = clk_prepare_enable(fifo->pclk);
	if (ret)
		return ret;

	/* Apply single buffer mode to the interface */
	regmap_update_bits(fifo->map, FIFO_CTRL0, CTRL0_FRDDR_PP_MODE, 0);

	/*
	 * TODO: We could adapt the fifo depth and the fifo threshold
	 * depending on the expected memory throughput and lantencies
	 * For now, we'll just use the same values as the vendor kernel
	 * Depth and threshold are zero based.
	 */
	fifo_depth = AXG_FIFO_MIN_CNT - 1;
	fifo_threshold = (AXG_FIFO_MIN_CNT / 2) - 1;
	regmap_update_bits(fifo->map, FIFO_CTRL1,
			   CTRL1_FRDDR_DEPTH_MASK | CTRL1_THRESHOLD_MASK,
			   CTRL1_FRDDR_DEPTH(fifo_depth) |
			   CTRL1_THRESHOLD(fifo_threshold));

	return 0;
}

static void axg_frddr_dai_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct axg_fifo *fifo = snd_soc_dai_get_drvdata(dai);

	clk_disable_unprepare(fifo->pclk);
}

static int axg_frddr_pcm_new(struct snd_soc_pcm_runtime *rtd,
			     struct snd_soc_dai *dai)
{
	return axg_fifo_pcm_new(rtd, SNDRV_PCM_STREAM_PLAYBACK);
}

static const struct snd_soc_dai_ops axg_frddr_ops = {
	.startup	= axg_frddr_dai_startup,
	.shutdown	= axg_frddr_dai_shutdown,
};

static struct snd_soc_dai_driver axg_frddr_dai_drv = {
	.name = "FRDDR",
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= AXG_FIFO_CH_MAX,
		.rates		= AXG_FIFO_RATES,
		.formats	= AXG_FIFO_FORMATS,
	},
	.ops		= &axg_frddr_ops,
	.pcm_new	= axg_frddr_pcm_new,
};

static const char * const axg_frddr_sel_texts[] = {
	"OUT 0", "OUT 1", "OUT 2", "OUT 3"
};

static SOC_ENUM_SINGLE_DECL(axg_frddr_sel_enum, FIFO_CTRL0, CTRL0_SEL_SHIFT,
			    axg_frddr_sel_texts);

static const struct snd_kcontrol_new axg_frddr_out_demux =
	SOC_DAPM_ENUM("Output Sink", axg_frddr_sel_enum);

static const struct snd_soc_dapm_widget axg_frddr_dapm_widgets[] = {
	SND_SOC_DAPM_DEMUX("SINK SEL", SND_SOC_NOPM, 0, 0,
			   &axg_frddr_out_demux),
	SND_SOC_DAPM_AIF_OUT("OUT 0", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 2", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 3", NULL, 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route axg_frddr_dapm_routes[] = {
	{ "SINK SEL", NULL, "Playback" },
	{ "OUT 0", "OUT 0",  "SINK SEL" },
	{ "OUT 1", "OUT 1",  "SINK SEL" },
	{ "OUT 2", "OUT 2",  "SINK SEL" },
	{ "OUT 3", "OUT 3",  "SINK SEL" },
};

static const struct snd_soc_component_driver axg_frddr_component_drv = {
	.dapm_widgets		= axg_frddr_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(axg_frddr_dapm_widgets),
	.dapm_routes		= axg_frddr_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(axg_frddr_dapm_routes),
	.ops			= &axg_fifo_pcm_ops
};

static const struct axg_fifo_match_data axg_frddr_match_data = {
	.component_drv	= &axg_frddr_component_drv,
	.dai_drv	= &axg_frddr_dai_drv
};

static const struct of_device_id axg_frddr_of_match[] = {
	{
		.compatible = "amlogic,axg-frddr",
		.data = &axg_frddr_match_data,
	}, {}
};
MODULE_DEVICE_TABLE(of, axg_frddr_of_match);

static struct platform_driver axg_frddr_pdrv = {
	.probe = axg_fifo_probe,
	.driver = {
		.name = "axg-frddr",
		.of_match_table = axg_frddr_of_match,
	},
};
module_platform_driver(axg_frddr_pdrv);

MODULE_DESCRIPTION("Amlogic AXG playback fifo driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
