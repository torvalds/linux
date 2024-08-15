// SPDX-License-Identifier: (GPL-2.0 OR MIT)
//
// Copyright (c) 2018 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

/*
 * This driver implements the frontend playback DAI of AXG and G12A based SoCs
 */

#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "axg-fifo.h"

#define CTRL0_FRDDR_PP_MODE		BIT(30)
#define CTRL0_SEL1_EN_SHIFT		3
#define CTRL0_SEL2_SHIFT		4
#define CTRL0_SEL2_EN_SHIFT		7
#define CTRL0_SEL3_SHIFT		8
#define CTRL0_SEL3_EN_SHIFT		11
#define CTRL1_FRDDR_FORCE_FINISH	BIT(12)
#define CTRL2_SEL1_SHIFT		0
#define CTRL2_SEL1_EN_SHIFT		4
#define CTRL2_SEL2_SHIFT		8
#define CTRL2_SEL2_EN_SHIFT		12
#define CTRL2_SEL3_SHIFT		16
#define CTRL2_SEL3_EN_SHIFT		20

static int g12a_frddr_dai_prepare(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct axg_fifo *fifo = snd_soc_dai_get_drvdata(dai);

	/* Reset the read pointer to the FIFO_INIT_ADDR */
	regmap_update_bits(fifo->map, FIFO_CTRL1,
			   CTRL1_FRDDR_FORCE_FINISH, 0);
	regmap_update_bits(fifo->map, FIFO_CTRL1,
			   CTRL1_FRDDR_FORCE_FINISH, CTRL1_FRDDR_FORCE_FINISH);
	regmap_update_bits(fifo->map, FIFO_CTRL1,
			   CTRL1_FRDDR_FORCE_FINISH, 0);

	return 0;
}

static int axg_frddr_dai_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct axg_fifo *fifo = snd_soc_dai_get_drvdata(dai);
	unsigned int period, depth, val;

	period = params_period_bytes(params);

	/* Trim the FIFO depth if the period is small to improve latency */
	depth = min(period, fifo->depth);
	val = (depth / AXG_FIFO_BURST) - 1;
	regmap_update_bits(fifo->map, FIFO_CTRL1, CTRL1_FRDDR_DEPTH_MASK,
			   CTRL1_FRDDR_DEPTH(val));

	return 0;
}

static int axg_frddr_dai_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct axg_fifo *fifo = snd_soc_dai_get_drvdata(dai);
	int ret;

	/* Enable pclk to access registers and clock the fifo ip */
	ret = clk_prepare_enable(fifo->pclk);
	if (ret)
		return ret;

	/* Apply single buffer mode to the interface */
	regmap_update_bits(fifo->map, FIFO_CTRL0, CTRL0_FRDDR_PP_MODE, 0);

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
	.hw_params	= axg_frddr_dai_hw_params,
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
	"OUT 0", "OUT 1", "OUT 2", "OUT 3", "OUT 4", "OUT 5", "OUT 6", "OUT 7",
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
	SND_SOC_DAPM_AIF_OUT("OUT 4", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 5", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 6", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 7", NULL, 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route axg_frddr_dapm_routes[] = {
	{ "SINK SEL", NULL, "Playback" },
	{ "OUT 0", "OUT 0",  "SINK SEL" },
	{ "OUT 1", "OUT 1",  "SINK SEL" },
	{ "OUT 2", "OUT 2",  "SINK SEL" },
	{ "OUT 3", "OUT 3",  "SINK SEL" },
	{ "OUT 4", "OUT 4",  "SINK SEL" },
	{ "OUT 5", "OUT 5",  "SINK SEL" },
	{ "OUT 6", "OUT 6",  "SINK SEL" },
	{ "OUT 7", "OUT 7",  "SINK SEL" },
};

static const struct snd_soc_component_driver axg_frddr_component_drv = {
	.dapm_widgets		= axg_frddr_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(axg_frddr_dapm_widgets),
	.dapm_routes		= axg_frddr_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(axg_frddr_dapm_routes),
	.open			= axg_fifo_pcm_open,
	.close			= axg_fifo_pcm_close,
	.hw_params		= axg_fifo_pcm_hw_params,
	.hw_free		= axg_fifo_pcm_hw_free,
	.pointer		= axg_fifo_pcm_pointer,
	.trigger		= axg_fifo_pcm_trigger,
	.legacy_dai_naming	= 1,
};

static const struct axg_fifo_match_data axg_frddr_match_data = {
	.field_threshold	= REG_FIELD(FIFO_CTRL1, 16, 23),
	.component_drv		= &axg_frddr_component_drv,
	.dai_drv		= &axg_frddr_dai_drv
};

static const struct snd_soc_dai_ops g12a_frddr_ops = {
	.prepare	= g12a_frddr_dai_prepare,
	.hw_params	= axg_frddr_dai_hw_params,
	.startup	= axg_frddr_dai_startup,
	.shutdown	= axg_frddr_dai_shutdown,
};

static struct snd_soc_dai_driver g12a_frddr_dai_drv = {
	.name = "FRDDR",
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= AXG_FIFO_CH_MAX,
		.rates		= AXG_FIFO_RATES,
		.formats	= AXG_FIFO_FORMATS,
	},
	.ops		= &g12a_frddr_ops,
	.pcm_new	= axg_frddr_pcm_new,
};

static SOC_ENUM_SINGLE_DECL(g12a_frddr_sel1_enum, FIFO_CTRL0, CTRL0_SEL_SHIFT,
			    axg_frddr_sel_texts);
static SOC_ENUM_SINGLE_DECL(g12a_frddr_sel2_enum, FIFO_CTRL0, CTRL0_SEL2_SHIFT,
			    axg_frddr_sel_texts);
static SOC_ENUM_SINGLE_DECL(g12a_frddr_sel3_enum, FIFO_CTRL0, CTRL0_SEL3_SHIFT,
			    axg_frddr_sel_texts);

static const struct snd_kcontrol_new g12a_frddr_out1_demux =
	SOC_DAPM_ENUM("Output Src 1", g12a_frddr_sel1_enum);
static const struct snd_kcontrol_new g12a_frddr_out2_demux =
	SOC_DAPM_ENUM("Output Src 2", g12a_frddr_sel2_enum);
static const struct snd_kcontrol_new g12a_frddr_out3_demux =
	SOC_DAPM_ENUM("Output Src 3", g12a_frddr_sel3_enum);

static const struct snd_kcontrol_new g12a_frddr_out1_enable =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", FIFO_CTRL0,
				    CTRL0_SEL1_EN_SHIFT, 1, 0);
static const struct snd_kcontrol_new g12a_frddr_out2_enable =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", FIFO_CTRL0,
				    CTRL0_SEL2_EN_SHIFT, 1, 0);
static const struct snd_kcontrol_new g12a_frddr_out3_enable =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", FIFO_CTRL0,
				    CTRL0_SEL3_EN_SHIFT, 1, 0);

static const struct snd_soc_dapm_widget g12a_frddr_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_OUT("SRC 1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SRC 2", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SRC 3", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SWITCH("SRC 1 EN", SND_SOC_NOPM, 0, 0,
			    &g12a_frddr_out1_enable),
	SND_SOC_DAPM_SWITCH("SRC 2 EN", SND_SOC_NOPM, 0, 0,
			    &g12a_frddr_out2_enable),
	SND_SOC_DAPM_SWITCH("SRC 3 EN", SND_SOC_NOPM, 0, 0,
			    &g12a_frddr_out3_enable),
	SND_SOC_DAPM_DEMUX("SINK 1 SEL", SND_SOC_NOPM, 0, 0,
			   &g12a_frddr_out1_demux),
	SND_SOC_DAPM_DEMUX("SINK 2 SEL", SND_SOC_NOPM, 0, 0,
			   &g12a_frddr_out2_demux),
	SND_SOC_DAPM_DEMUX("SINK 3 SEL", SND_SOC_NOPM, 0, 0,
			   &g12a_frddr_out3_demux),
	SND_SOC_DAPM_AIF_OUT("OUT 0", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 2", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 3", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 4", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 5", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 6", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 7", NULL, 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route g12a_frddr_dapm_routes[] = {
	{ "SRC 1", NULL, "Playback" },
	{ "SRC 2", NULL, "Playback" },
	{ "SRC 3", NULL, "Playback" },
	{ "SRC 1 EN", "Switch", "SRC 1" },
	{ "SRC 2 EN", "Switch", "SRC 2" },
	{ "SRC 3 EN", "Switch", "SRC 3" },
	{ "SINK 1 SEL", NULL, "SRC 1 EN" },
	{ "SINK 2 SEL", NULL, "SRC 2 EN" },
	{ "SINK 3 SEL", NULL, "SRC 3 EN" },
	{ "OUT 0", "OUT 0", "SINK 1 SEL" },
	{ "OUT 1", "OUT 1", "SINK 1 SEL" },
	{ "OUT 2", "OUT 2", "SINK 1 SEL" },
	{ "OUT 3", "OUT 3", "SINK 1 SEL" },
	{ "OUT 4", "OUT 4", "SINK 1 SEL" },
	{ "OUT 5", "OUT 5", "SINK 1 SEL" },
	{ "OUT 6", "OUT 6", "SINK 1 SEL" },
	{ "OUT 7", "OUT 7", "SINK 1 SEL" },
	{ "OUT 0", "OUT 0", "SINK 2 SEL" },
	{ "OUT 1", "OUT 1", "SINK 2 SEL" },
	{ "OUT 2", "OUT 2", "SINK 2 SEL" },
	{ "OUT 3", "OUT 3", "SINK 2 SEL" },
	{ "OUT 4", "OUT 4", "SINK 2 SEL" },
	{ "OUT 5", "OUT 5", "SINK 2 SEL" },
	{ "OUT 6", "OUT 6", "SINK 2 SEL" },
	{ "OUT 7", "OUT 7", "SINK 2 SEL" },
	{ "OUT 0", "OUT 0", "SINK 3 SEL" },
	{ "OUT 1", "OUT 1", "SINK 3 SEL" },
	{ "OUT 2", "OUT 2", "SINK 3 SEL" },
	{ "OUT 3", "OUT 3", "SINK 3 SEL" },
	{ "OUT 4", "OUT 4", "SINK 3 SEL" },
	{ "OUT 5", "OUT 5", "SINK 3 SEL" },
	{ "OUT 6", "OUT 6", "SINK 3 SEL" },
	{ "OUT 7", "OUT 7", "SINK 3 SEL" },
};

static const struct snd_soc_component_driver g12a_frddr_component_drv = {
	.dapm_widgets		= g12a_frddr_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(g12a_frddr_dapm_widgets),
	.dapm_routes		= g12a_frddr_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(g12a_frddr_dapm_routes),
	.open			= axg_fifo_pcm_open,
	.close			= axg_fifo_pcm_close,
	.hw_params		= g12a_fifo_pcm_hw_params,
	.hw_free		= axg_fifo_pcm_hw_free,
	.pointer		= axg_fifo_pcm_pointer,
	.trigger		= axg_fifo_pcm_trigger,
	.legacy_dai_naming	= 1,
};

static const struct axg_fifo_match_data g12a_frddr_match_data = {
	.field_threshold	= REG_FIELD(FIFO_CTRL1, 16, 23),
	.component_drv		= &g12a_frddr_component_drv,
	.dai_drv		= &g12a_frddr_dai_drv
};

/* On SM1, the output selection in on CTRL2 */
static const struct snd_kcontrol_new sm1_frddr_out1_enable =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", FIFO_CTRL2,
				    CTRL2_SEL1_EN_SHIFT, 1, 0);
static const struct snd_kcontrol_new sm1_frddr_out2_enable =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", FIFO_CTRL2,
				    CTRL2_SEL2_EN_SHIFT, 1, 0);
static const struct snd_kcontrol_new sm1_frddr_out3_enable =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", FIFO_CTRL2,
				    CTRL2_SEL3_EN_SHIFT, 1, 0);

static SOC_ENUM_SINGLE_DECL(sm1_frddr_sel1_enum, FIFO_CTRL2, CTRL2_SEL1_SHIFT,
			    axg_frddr_sel_texts);
static SOC_ENUM_SINGLE_DECL(sm1_frddr_sel2_enum, FIFO_CTRL2, CTRL2_SEL2_SHIFT,
			    axg_frddr_sel_texts);
static SOC_ENUM_SINGLE_DECL(sm1_frddr_sel3_enum, FIFO_CTRL2, CTRL2_SEL3_SHIFT,
			    axg_frddr_sel_texts);

static const struct snd_kcontrol_new sm1_frddr_out1_demux =
	SOC_DAPM_ENUM("Output Src 1", sm1_frddr_sel1_enum);
static const struct snd_kcontrol_new sm1_frddr_out2_demux =
	SOC_DAPM_ENUM("Output Src 2", sm1_frddr_sel2_enum);
static const struct snd_kcontrol_new sm1_frddr_out3_demux =
	SOC_DAPM_ENUM("Output Src 3", sm1_frddr_sel3_enum);

static const struct snd_soc_dapm_widget sm1_frddr_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_OUT("SRC 1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SRC 2", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SRC 3", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SWITCH("SRC 1 EN", SND_SOC_NOPM, 0, 0,
			    &sm1_frddr_out1_enable),
	SND_SOC_DAPM_SWITCH("SRC 2 EN", SND_SOC_NOPM, 0, 0,
			    &sm1_frddr_out2_enable),
	SND_SOC_DAPM_SWITCH("SRC 3 EN", SND_SOC_NOPM, 0, 0,
			    &sm1_frddr_out3_enable),
	SND_SOC_DAPM_DEMUX("SINK 1 SEL", SND_SOC_NOPM, 0, 0,
			   &sm1_frddr_out1_demux),
	SND_SOC_DAPM_DEMUX("SINK 2 SEL", SND_SOC_NOPM, 0, 0,
			   &sm1_frddr_out2_demux),
	SND_SOC_DAPM_DEMUX("SINK 3 SEL", SND_SOC_NOPM, 0, 0,
			   &sm1_frddr_out3_demux),
	SND_SOC_DAPM_AIF_OUT("OUT 0", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 2", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 3", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 4", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 5", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 6", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("OUT 7", NULL, 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_component_driver sm1_frddr_component_drv = {
	.dapm_widgets		= sm1_frddr_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sm1_frddr_dapm_widgets),
	.dapm_routes		= g12a_frddr_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(g12a_frddr_dapm_routes),
	.open			= axg_fifo_pcm_open,
	.close			= axg_fifo_pcm_close,
	.hw_params		= g12a_fifo_pcm_hw_params,
	.hw_free		= axg_fifo_pcm_hw_free,
	.pointer		= axg_fifo_pcm_pointer,
	.trigger		= axg_fifo_pcm_trigger,
	.legacy_dai_naming	= 1,
};

static const struct axg_fifo_match_data sm1_frddr_match_data = {
	.field_threshold	= REG_FIELD(FIFO_CTRL1, 16, 23),
	.component_drv		= &sm1_frddr_component_drv,
	.dai_drv		= &g12a_frddr_dai_drv
};

static const struct of_device_id axg_frddr_of_match[] = {
	{
		.compatible = "amlogic,axg-frddr",
		.data = &axg_frddr_match_data,
	}, {
		.compatible = "amlogic,g12a-frddr",
		.data = &g12a_frddr_match_data,
	}, {
		.compatible = "amlogic,sm1-frddr",
		.data = &sm1_frddr_match_data,
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

MODULE_DESCRIPTION("Amlogic AXG/G12A playback fifo driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
