// SPDX-License-Identifier: GPL-2.0
//
// Socionext UniPhier AIO ALSA driver for PXs2.
//
// Copyright (c) 2018 Socionext Inc.

#include <linux/module.h>

#include "aio.h"

static const struct uniphier_aio_spec uniphier_aio_pxs2[] = {
	/* for Line PCM In, Pin:AI1Dx */
	{
		.name = AUD_NAME_PCMIN1,
		.gname = AUD_GNAME_LINE,
		.swm = {
			.type  = PORT_TYPE_I2S,
			.dir   = PORT_DIR_INPUT,
			.rb    = { 16, 11, },
			.ch    = { 16, 11, },
			.iif   = { 0, 0, },
			.iport = { 0, AUD_HW_PCMIN1, },
		},
	},

	/* for Speaker/Headphone/Mic PCM In, Pin:AI2Dx */
	{
		.name = AUD_NAME_PCMIN2,
		.gname = AUD_GNAME_AUX,
		.swm = {
			.type  = PORT_TYPE_I2S,
			.dir   = PORT_DIR_INPUT,
			.rb    = { 17, 12, },
			.ch    = { 17, 12, },
			.iif   = { 1, 1, },
			.iport = { 1, AUD_HW_PCMIN2, },
		},
	},

	/* for HDMI PCM Out, Pin:AO1Dx (inner) */
	{
		.name = AUD_NAME_HPCMOUT1,
		.gname = AUD_GNAME_HDMI,
		.swm = {
			.type  = PORT_TYPE_I2S,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 0, 0, },
			.ch    = { 0, 0, },
			.oif   = { 0, 0, },
			.oport = { 3, AUD_HW_HPCMOUT1, },
		},
	},

	/* for Line PCM Out, Pin:AO2Dx */
	{
		.name = AUD_NAME_PCMOUT1,
		.gname = AUD_GNAME_LINE,
		.swm = {
			.type  = PORT_TYPE_I2S,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 1, 1, },
			.ch    = { 1, 1, },
			.oif   = { 1, 1, },
			.oport = { 0, AUD_HW_PCMOUT1, },
		},
	},

	/* for Speaker/Headphone/Mic PCM Out, Pin:AO3Dx */
	{
		.name = AUD_NAME_PCMOUT2,
		.gname = AUD_GNAME_AUX,
		.swm = {
			.type  = PORT_TYPE_I2S,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 2, 2, },
			.ch    = { 2, 2, },
			.oif   = { 2, 2, },
			.oport = { 1, AUD_HW_PCMOUT2, },
		},
	},

	/* for HDMI Out, Pin:AO1IEC */
	{
		.name = AUD_NAME_HIECOUT1,
		.swm = {
			.type  = PORT_TYPE_SPDIF,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 6, 4, },
			.ch    = { 6, 4, },
			.oif   = { 6, 4, },
			.oport = { 12, AUD_HW_HIECOUT1, },
		},
	},

	/* for HDMI Out, Pin:AO1IEC, Compress */
	{
		.name = AUD_NAME_HIECCOMPOUT1,
		.swm = {
			.type  = PORT_TYPE_SPDIF,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 6, 4, },
			.ch    = { 6, 4, },
			.oif   = { 6, 4, },
			.oport = { 12, AUD_HW_HIECOUT1, },
		},
	},

	/* for S/PDIF Out, Pin:AO2IEC */
	{
		.name = AUD_NAME_IECOUT1,
		.swm = {
			.type  = PORT_TYPE_SPDIF,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 7, 5, },
			.ch    = { 7, 5, },
			.oif   = { 7, 5, },
			.oport = { 13, AUD_HW_IECOUT1, },
		},
	},

	/* for S/PDIF Out, Pin:AO2IEC */
	{
		.name = AUD_NAME_IECCOMPOUT1,
		.swm = {
			.type  = PORT_TYPE_SPDIF,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 7, 5, },
			.ch    = { 7, 5, },
			.oif   = { 7, 5, },
			.oport = { 13, AUD_HW_IECOUT1, },
		},
	},
};

static const struct uniphier_aio_pll uniphier_aio_pll_pxs2[] = {
	[AUD_PLL_A1]   = { .enable = true, },
	[AUD_PLL_F1]   = { .enable = true, },
	[AUD_PLL_A2]   = { .enable = true, },
	[AUD_PLL_F2]   = { .enable = true, },
	[AUD_PLL_APLL] = { .enable = true, },
	[AUD_PLL_HSC0] = { .enable = true, },
};

static struct snd_soc_dai_driver uniphier_aio_dai_pxs2[] = {
	{
		.name    = AUD_GNAME_HDMI,
		.playback = {
			.stream_name = AUD_NAME_HPCMOUT1,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_i2s_pxs2_ops,
	},
	{
		.name    = AUD_GNAME_LINE,
		.playback = {
			.stream_name = AUD_NAME_PCMOUT1,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.capture = {
			.stream_name = AUD_NAME_PCMIN1,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_i2s_pxs2_ops,
	},
	{
		.name    = AUD_GNAME_AUX,
		.playback = {
			.stream_name = AUD_NAME_PCMOUT2,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.capture = {
			.stream_name = AUD_NAME_PCMIN2,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_i2s_pxs2_ops,
	},
	{
		.name    = AUD_NAME_HIECOUT1,
		.playback = {
			.stream_name = AUD_NAME_HIECOUT1,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_spdif_pxs2_ops,
	},
	{
		.name    = AUD_NAME_IECOUT1,
		.playback = {
			.stream_name = AUD_NAME_IECOUT1,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_spdif_pxs2_ops,
	},
	{
		.name    = AUD_NAME_HIECCOMPOUT1,
		.playback = {
			.stream_name = AUD_NAME_HIECCOMPOUT1,
			.channels_min = 1,
			.channels_max = 1,
		},
		.ops = &uniphier_aio_spdif_pxs2_ops2,
	},
	{
		.name    = AUD_NAME_IECCOMPOUT1,
		.playback = {
			.stream_name = AUD_NAME_IECCOMPOUT1,
			.channels_min = 1,
			.channels_max = 1,
		},
		.ops = &uniphier_aio_spdif_pxs2_ops2,
	},
};

static const struct uniphier_aio_chip_spec uniphier_aio_pxs2_spec = {
	.specs     = uniphier_aio_pxs2,
	.num_specs = ARRAY_SIZE(uniphier_aio_pxs2),
	.dais      = uniphier_aio_dai_pxs2,
	.num_dais  = ARRAY_SIZE(uniphier_aio_dai_pxs2),
	.plls      = uniphier_aio_pll_pxs2,
	.num_plls  = ARRAY_SIZE(uniphier_aio_pll_pxs2),
	.addr_ext  = 0,
};

static const struct of_device_id uniphier_aio_of_match[] __maybe_unused = {
	{
		.compatible = "socionext,uniphier-pxs2-aio",
		.data = &uniphier_aio_pxs2_spec,
	},
	{},
};
MODULE_DEVICE_TABLE(of, uniphier_aio_of_match);

static struct platform_driver uniphier_aio_driver = {
	.driver = {
		.name = "snd-uniphier-aio-pxs2",
		.of_match_table = of_match_ptr(uniphier_aio_of_match),
	},
	.probe    = uniphier_aio_probe,
	.remove_new = uniphier_aio_remove,
};
module_platform_driver(uniphier_aio_driver);

MODULE_AUTHOR("Katsuhiro Suzuki <suzuki.katsuhiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier PXs2 AIO driver.");
MODULE_LICENSE("GPL v2");
