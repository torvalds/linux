// SPDX-License-Identifier: GPL-2.0
//
// Socionext UniPhier AIO ALSA driver for LD11/LD20.
//
// Copyright (c) 2016-2018 Socionext Inc.

#include <linux/module.h>

#include "aio.h"

static const struct uniphier_aio_spec uniphier_aio_ld11[] = {
	/* for HDMI PCM In, Pin:AI1Dx */
	{
		.name = AUD_NAME_PCMIN1,
		.gname = AUD_GNAME_HDMI,
		.swm = {
			.type  = PORT_TYPE_I2S,
			.dir   = PORT_DIR_INPUT,
			.rb    = { 21, 14, },
			.ch    = { 21, 14, },
			.iif   = { 5, 3, },
			.iport = { 0, AUD_HW_PCMIN1, },
		},
	},

	/* for SIF In, Pin:AI2Dx */
	{
		.name = AUD_NAME_PCMIN2,
		.swm = {
			.type  = PORT_TYPE_I2S,
			.dir   = PORT_DIR_INPUT,
			.rb    = { 22, 15, },
			.ch    = { 22, 15, },
			.iif   = { 6, 4, },
			.iport = { 1, AUD_HW_PCMIN2, },
		},
	},

	/* for Line In, Pin:AI3Dx */
	{
		.name = AUD_NAME_PCMIN3,
		.gname = AUD_GNAME_LINE,
		.swm = {
			.type  = PORT_TYPE_EVE,
			.dir   = PORT_DIR_INPUT,
			.rb    = { 23, 16, },
			.ch    = { 23, 16, },
			.iif   = { 7, 5, },
			.iport = { 2, AUD_HW_PCMIN3, },
		},
	},

	/* for S/PDIF In, Pin:AI1IEC */
	{
		.name = AUD_NAME_IECIN1,
		.gname = AUD_GNAME_IEC,
		.swm = {
			.type  = PORT_TYPE_SPDIF,
			.dir   = PORT_DIR_INPUT,
			.rb    = { 26, 17, },
			.ch    = { 26, 17, },
			.iif   = { 10, 6, },
			.iport = { 3, AUD_HW_IECIN1, },
		},
	},

	/* for Speaker, Pin:AO1Dx */
	{
		.name = AUD_NAME_HPCMOUT1,
		.swm = {
			.type  = PORT_TYPE_I2S,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 0, 0, },
			.ch    = { 0, 0, },
			.oif   = { 0, 0, },
			.oport = { 0, AUD_HW_HPCMOUT1, },
		},
	},

	/* for HDMI PCM, Pin:AO2Dx */
	{
		.name = AUD_NAME_PCMOUT1,
		.gname = AUD_GNAME_HDMI,
		.swm = {
			.type  = PORT_TYPE_I2S,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 0, 0, },
			.ch    = { 0, 0, },
			.oif   = { 0, 0, },
			.oport = { 3, AUD_HW_PCMOUT1, },
		},
	},

	/* for Line Out, Pin:LO2_x */
	{
		.name = AUD_NAME_PCMOUT2,
		.gname = AUD_GNAME_LINE,
		.swm = {
			.type  = PORT_TYPE_EVE,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 2, 2, },
			.ch    = { 2, 2, },
			.oif   = { 2, 2, },
			.oport = { 1, AUD_HW_PCMOUT2, },
		},
	},

	/* for Headphone, Pin:HP1_x */
	{
		.name = AUD_NAME_PCMOUT3,
		.swm = {
			.type  = PORT_TYPE_EVE,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 3, 3, },
			.ch    = { 3, 3, },
			.oif   = { 3, 3, },
			.oport = { 2, AUD_HW_PCMOUT3, },
		},
	},

	/* for HW Sampling Rate Converter */
	{
		.name = AUD_NAME_EPCMOUT2,
		.swm = {
			.type  = PORT_TYPE_CONV,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 7, 5, },
			.ch    = { 7, 5, },
			.oif   = { 7, 5, },
			.oport = { 6, AUD_HW_EPCMOUT2, },
			.och   = { 17, 12, },
			.iif   = { 1, 1, },
		},
	},

	/* for HW Sampling Rate Converter 2 */
	{
		.name = AUD_NAME_EPCMOUT3,
		.swm = {
			.type  = PORT_TYPE_CONV,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 8, 6, },
			.ch    = { 8, 6, },
			.oif   = { 8, 6, },
			.oport = { 7, AUD_HW_EPCMOUT3, },
			.och   = { 18, 13, },
			.iif   = { 2, 2, },
		},
	},

	/* for S/PDIF Out, Pin:AO1IEC */
	{
		.name = AUD_NAME_HIECOUT1,
		.gname = AUD_GNAME_IEC,
		.swm = {
			.type  = PORT_TYPE_SPDIF,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 1, 1, },
			.ch    = { 1, 1, },
			.oif   = { 1, 1, },
			.oport = { 12, AUD_HW_HIECOUT1, },
		},
	},

	/* for S/PDIF Out, Pin:AO1IEC, Compress */
	{
		.name = AUD_NAME_HIECCOMPOUT1,
		.gname = AUD_GNAME_IEC,
		.swm = {
			.type  = PORT_TYPE_SPDIF,
			.dir   = PORT_DIR_OUTPUT,
			.rb    = { 1, 1, },
			.ch    = { 1, 1, },
			.oif   = { 1, 1, },
			.oport = { 12, AUD_HW_HIECOUT1, },
		},
	},
};

static const struct uniphier_aio_pll uniphier_aio_pll_ld11[] = {
	[AUD_PLL_A1]   = { .enable = true, },
	[AUD_PLL_F1]   = { .enable = true, },
	[AUD_PLL_A2]   = { .enable = true, },
	[AUD_PLL_F2]   = { .enable = true, },
	[AUD_PLL_APLL] = { .enable = true, },
	[AUD_PLL_RX0]  = { .enable = true, },
	[AUD_PLL_USB0] = { .enable = true, },
	[AUD_PLL_HSC0] = { .enable = true, },
};

static int uniphier_aio_ld11_probe(struct snd_soc_dai *dai)
{
	int ret;

	ret = uniphier_aio_dai_probe(dai);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_pll(dai, AUD_PLL_A1, 0, 0, 36864000);
	if (ret < 0)
		return ret;
	ret = snd_soc_dai_set_pll(dai, AUD_PLL_F1, 0, 0, 36864000);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_pll(dai, AUD_PLL_A2, 0, 0, 33868800);
	if (ret < 0)
		return ret;
	ret = snd_soc_dai_set_pll(dai, AUD_PLL_F2, 0, 0, 33868800);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_dai_driver uniphier_aio_dai_ld11[] = {
	{
		.name    = AUD_GNAME_HDMI,
		.probe   = uniphier_aio_ld11_probe,
		.remove  = uniphier_aio_dai_remove,
		.suspend = uniphier_aio_dai_suspend,
		.resume  = uniphier_aio_dai_resume,
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
			.rates       = SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_32000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_i2s_ops,
	},
	{
		.name    = AUD_NAME_PCMIN2,
		.probe   = uniphier_aio_ld11_probe,
		.remove  = uniphier_aio_dai_remove,
		.suspend = uniphier_aio_dai_suspend,
		.resume  = uniphier_aio_dai_resume,
		.capture = {
			.stream_name = AUD_NAME_PCMIN2,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_i2s_ops,
	},
	{
		.name    = AUD_GNAME_LINE,
		.probe   = uniphier_aio_ld11_probe,
		.remove  = uniphier_aio_dai_remove,
		.suspend = uniphier_aio_dai_suspend,
		.resume  = uniphier_aio_dai_resume,
		.playback = {
			.stream_name = AUD_NAME_PCMOUT2,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.capture = {
			.stream_name = AUD_NAME_PCMIN3,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_i2s_ops,
	},
	{
		.name    = AUD_NAME_HPCMOUT1,
		.probe   = uniphier_aio_ld11_probe,
		.remove  = uniphier_aio_dai_remove,
		.suspend = uniphier_aio_dai_suspend,
		.resume  = uniphier_aio_dai_resume,
		.playback = {
			.stream_name = AUD_NAME_HPCMOUT1,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_i2s_ops,
	},
	{
		.name    = AUD_NAME_PCMOUT3,
		.probe   = uniphier_aio_ld11_probe,
		.remove  = uniphier_aio_dai_remove,
		.suspend = uniphier_aio_dai_suspend,
		.resume  = uniphier_aio_dai_resume,
		.playback = {
			.stream_name = AUD_NAME_PCMOUT3,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_i2s_ops,
	},
	{
		.name    = AUD_NAME_HIECOUT1,
		.probe   = uniphier_aio_ld11_probe,
		.remove  = uniphier_aio_dai_remove,
		.suspend = uniphier_aio_dai_suspend,
		.resume  = uniphier_aio_dai_resume,
		.playback = {
			.stream_name = AUD_NAME_HIECOUT1,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_spdif_ops,
	},
	{
		.name    = AUD_NAME_EPCMOUT2,
		.probe   = uniphier_aio_ld11_probe,
		.remove  = uniphier_aio_dai_remove,
		.suspend = uniphier_aio_dai_suspend,
		.resume  = uniphier_aio_dai_resume,
		.playback = {
			.stream_name = AUD_NAME_EPCMOUT2,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_32000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_i2s_ops,
	},
	{
		.name    = AUD_NAME_EPCMOUT3,
		.probe   = uniphier_aio_ld11_probe,
		.remove  = uniphier_aio_dai_remove,
		.suspend = uniphier_aio_dai_suspend,
		.resume  = uniphier_aio_dai_resume,
		.playback = {
			.stream_name = AUD_NAME_EPCMOUT3,
			.formats     = SNDRV_PCM_FMTBIT_S32_LE,
			.rates       = SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_32000,
			.channels_min = 2,
			.channels_max = 2,
		},
		.ops = &uniphier_aio_i2s_ops,
	},
	{
		.name    = AUD_NAME_HIECCOMPOUT1,
		.probe   = uniphier_aio_ld11_probe,
		.remove  = uniphier_aio_dai_remove,
		.suspend = uniphier_aio_dai_suspend,
		.resume  = uniphier_aio_dai_resume,
		.compress_new = snd_soc_new_compress,
		.playback = {
			.stream_name = AUD_NAME_HIECCOMPOUT1,
			.channels_min = 1,
			.channels_max = 1,
		},
		.ops = &uniphier_aio_spdif_ops,
	},
};

static const struct uniphier_aio_chip_spec uniphier_aio_ld11_spec = {
	.specs     = uniphier_aio_ld11,
	.num_specs = ARRAY_SIZE(uniphier_aio_ld11),
	.dais      = uniphier_aio_dai_ld11,
	.num_dais  = ARRAY_SIZE(uniphier_aio_dai_ld11),
	.plls      = uniphier_aio_pll_ld11,
	.num_plls  = ARRAY_SIZE(uniphier_aio_pll_ld11),
	.addr_ext  = 0,
};

static const struct uniphier_aio_chip_spec uniphier_aio_ld20_spec = {
	.specs     = uniphier_aio_ld11,
	.num_specs = ARRAY_SIZE(uniphier_aio_ld11),
	.dais      = uniphier_aio_dai_ld11,
	.num_dais  = ARRAY_SIZE(uniphier_aio_dai_ld11),
	.plls      = uniphier_aio_pll_ld11,
	.num_plls  = ARRAY_SIZE(uniphier_aio_pll_ld11),
	.addr_ext  = 1,
};

static const struct of_device_id uniphier_aio_of_match[] = {
	{
		.compatible = "socionext,uniphier-ld11-aio",
		.data = &uniphier_aio_ld11_spec,
	},
	{
		.compatible = "socionext,uniphier-ld20-aio",
		.data = &uniphier_aio_ld20_spec,
	},
	{},
};
MODULE_DEVICE_TABLE(of, uniphier_aio_of_match);

static struct platform_driver uniphier_aio_driver = {
	.driver = {
		.name = "snd-uniphier-aio-ld11",
		.of_match_table = of_match_ptr(uniphier_aio_of_match),
	},
	.probe    = uniphier_aio_probe,
	.remove   = uniphier_aio_remove,
};
module_platform_driver(uniphier_aio_driver);

MODULE_AUTHOR("Katsuhiro Suzuki <suzuki.katsuhiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier LD11/LD20 AIO driver.");
MODULE_LICENSE("GPL v2");
