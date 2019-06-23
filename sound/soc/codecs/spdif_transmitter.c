// SPDX-License-Identifier: GPL-2.0-only
/*
 * ALSA SoC SPDIF DIT driver
 *
 *  This driver is used by controllers which can operate in DIT (SPDI/F) where
 *  no codec is needed.  This file provides stub codec that can be used
 *  in these configurations. TI DaVinci Audio controller uses this driver.
 *
 * Author:      Steve Chen,  <schen@mvista.com>
 * Copyright:   (C) 2009 MontaVista Software, Inc., <source@mvista.com>
 * Copyright:   (C) 2009  Texas Instruments, India
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <linux/of.h>

#define DRV_NAME "spdif-dit"

#define STUB_RATES	SNDRV_PCM_RATE_8000_192000
#define STUB_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE  | \
			SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dapm_widget dit_widgets[] = {
	SND_SOC_DAPM_OUTPUT("spdif-out"),
};

static const struct snd_soc_dapm_route dit_routes[] = {
	{ "spdif-out", NULL, "Playback" },
};

static struct snd_soc_component_driver soc_codec_spdif_dit = {
	.dapm_widgets		= dit_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(dit_widgets),
	.dapm_routes		= dit_routes,
	.num_dapm_routes	= ARRAY_SIZE(dit_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static struct snd_soc_dai_driver dit_stub_dai = {
	.name		= "dit-hifi",
	.playback 	= {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 384,
		.rates		= STUB_RATES,
		.formats	= STUB_FORMATS,
	},
};

static int spdif_dit_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
			&soc_codec_spdif_dit,
			&dit_stub_dai, 1);
}

#ifdef CONFIG_OF
static const struct of_device_id spdif_dit_dt_ids[] = {
	{ .compatible = "linux,spdif-dit", },
	{ }
};
MODULE_DEVICE_TABLE(of, spdif_dit_dt_ids);
#endif

static struct platform_driver spdif_dit_driver = {
	.probe		= spdif_dit_probe,
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = of_match_ptr(spdif_dit_dt_ids),
	},
};

module_platform_driver(spdif_dit_driver);

MODULE_AUTHOR("Steve Chen <schen@mvista.com>");
MODULE_DESCRIPTION("SPDIF dummy codec driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
