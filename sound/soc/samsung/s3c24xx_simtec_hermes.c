/* sound/soc/samsung/s3c24xx_simtec_hermes.c
 *
 * Copyright 2009 Simtec Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <sound/soc.h>

#include "s3c24xx_simtec.h"

static const struct snd_soc_dapm_widget dapm_widgets[] = {
	SND_SOC_DAPM_LINE("GSM Out", NULL),
	SND_SOC_DAPM_LINE("GSM In", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_LINE("ZV", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
};

static const struct snd_soc_dapm_route base_map[] = {
	/* Headphone connected to HP{L,R}OUT and HP{L,R}COM */

	{ "Headphone Jack", NULL, "HPLOUT" },
	{ "Headphone Jack", NULL, "HPLCOM" },
	{ "Headphone Jack", NULL, "HPROUT" },
	{ "Headphone Jack", NULL, "HPRCOM" },

	/* ZV connected to Line1 */

	{ "LINE1L", NULL, "ZV" },
	{ "LINE1R", NULL, "ZV" },

	/* Line In connected to Line2 */

	{ "LINE2L", NULL, "Line In" },
	{ "LINE2R", NULL, "Line In" },

	/* Microphone connected to MIC3R and MIC_BIAS */

	{ "MIC3L", NULL, "Mic Jack" },

	/* GSM connected to MONO_LOUT and MIC3L (in) */

	{ "GSM Out", NULL, "MONO_LOUT" },
	{ "MIC3L", NULL, "GSM In" },

	/* Speaker is connected to LINEOUT{LN,LP,RN,RP}, however we are
	 * not using the DAPM to power it up and down as there it makes
	 * a click when powering up. */
};

/**
 * simtec_hermes_init - initialise and add controls
 * @codec; The codec instance to attach to.
 *
 * Attach our controls and configure the necessary codec
 * mappings for our sound card instance.
*/
static int simtec_hermes_init(struct snd_soc_pcm_runtime *rtd)
{
	simtec_audio_init(rtd);

	return 0;
}

static struct snd_soc_dai_link simtec_dai_aic33 = {
	.name		= "tlv320aic33",
	.stream_name	= "TLV320AIC33",
	.codec_name	= "tlv320aic3x-codec.0-001a",
	.cpu_dai_name	= "s3c24xx-iis",
	.codec_dai_name = "tlv320aic3x-hifi",
	.platform_name	= "s3c24xx-iis",
	.init		= simtec_hermes_init,
};

/* simtec audio machine driver */
static struct snd_soc_card snd_soc_machine_simtec_aic33 = {
	.name		= "Simtec-Hermes",
	.owner		= THIS_MODULE,
	.dai_link	= &simtec_dai_aic33,
	.num_links	= 1,

	.dapm_widgets	= dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(dapm_widgets),
	.dapm_routes	= base_map,
	.num_dapm_routes = ARRAY_SIZE(base_map),
};

static int simtec_audio_hermes_probe(struct platform_device *pd)
{
	dev_info(&pd->dev, "probing....\n");
	return simtec_audio_core_probe(pd, &snd_soc_machine_simtec_aic33);
}

static struct platform_driver simtec_audio_hermes_platdrv = {
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "s3c24xx-simtec-hermes-snd",
		.pm	= simtec_audio_pm,
	},
	.probe	= simtec_audio_hermes_probe,
	.remove	= simtec_audio_remove,
};

module_platform_driver(simtec_audio_hermes_platdrv);

MODULE_ALIAS("platform:s3c24xx-simtec-hermes-snd");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("ALSA SoC Simtec Audio support");
MODULE_LICENSE("GPL");
