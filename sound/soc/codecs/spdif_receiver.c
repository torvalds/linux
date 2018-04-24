/*
 * ALSA SoC SPDIF DIR (Digital Interface Reciever) driver
 *
 * Based on ALSA SoC SPDIF DIT driver
 *
 *  This driver is used by controllers which can operate in DIR (SPDI/F) where
 *  no codec is needed.  This file provides stub codec that can be used
 *  in these configurations. SPEAr SPDIF IN Audio controller uses this driver.
 *
 * Author:      Vipin Kumar,  <vipin.kumar@st.com>
 * Copyright:   (C) 2012  ST Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <linux/of.h>

static const struct snd_soc_dapm_widget dir_widgets[] = {
	SND_SOC_DAPM_INPUT("spdif-in"),
};

static const struct snd_soc_dapm_route dir_routes[] = {
	{ "Capture", NULL, "spdif-in" },
};

#define STUB_RATES	SNDRV_PCM_RATE_8000_192000
#define STUB_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE  | \
			SNDRV_PCM_FMTBIT_S32_LE | \
			SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE)

static struct snd_soc_component_driver soc_codec_spdif_dir = {
	.dapm_widgets		= dir_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(dir_widgets),
	.dapm_routes		= dir_routes,
	.num_dapm_routes	= ARRAY_SIZE(dir_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static struct snd_soc_dai_driver dir_stub_dai = {
	.name		= "dir-hifi",
	.capture	= {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= 384,
		.rates		= STUB_RATES,
		.formats	= STUB_FORMATS,
	},
};

static int spdif_dir_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
			&soc_codec_spdif_dir,
			&dir_stub_dai, 1);
}

#ifdef CONFIG_OF
static const struct of_device_id spdif_dir_dt_ids[] = {
	{ .compatible = "linux,spdif-dir", },
	{ }
};
MODULE_DEVICE_TABLE(of, spdif_dir_dt_ids);
#endif

static struct platform_driver spdif_dir_driver = {
	.probe		= spdif_dir_probe,
	.driver		= {
		.name	= "spdif-dir",
		.of_match_table = of_match_ptr(spdif_dir_dt_ids),
	},
};

module_platform_driver(spdif_dir_driver);

MODULE_DESCRIPTION("ASoC SPDIF DIR driver");
MODULE_AUTHOR("Vipin Kumar <vipin.kumar@st.com>");
MODULE_LICENSE("GPL");
