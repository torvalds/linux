/* sound/soc/s3c24xx/s3c24xx_simtec_tlv320aic23.c
 *
 * Copyright 2009 Simtec Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <plat/audio-simtec.h>

#include "s3c-dma.h"
#include "s3c24xx-i2s.h"
#include "s3c24xx_simtec.h"

#include "../codecs/tlv320aic23.h"

/* supported machines:
 *
 * Machine	Connections		AMP
 * -------	-----------		---
 * BAST		MIC, HPOUT, LOUT, LIN	TPA2001D1 (HPOUTL,R) (gain hardwired)
 * VR1000	HPOUT, LIN		None
 * VR2000	LIN, LOUT, MIC, HP	LM4871 (HPOUTL,R)
 * DePicture	LIN, LOUT, MIC, HP	LM4871 (HPOUTL,R)
 * Anubis	LIN, LOUT, MIC, HP	TPA2001D1 (HPOUTL,R)
 */

static const struct snd_soc_dapm_widget dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
};

static const struct snd_soc_dapm_route base_map[] = {
	{ "Headphone Jack", NULL, "LHPOUT"},
	{ "Headphone Jack", NULL, "RHPOUT"},

	{ "Line Out", NULL, "LOUT" },
	{ "Line Out", NULL, "ROUT" },

	{ "LLINEIN", NULL, "Line In"},
	{ "RLINEIN", NULL, "Line In"},

	{ "MICIN", NULL, "Mic Jack"},
};

/**
 * simtec_tlv320aic23_init - initialise and add controls
 * @codec; The codec instance to attach to.
 *
 * Attach our controls and configure the necessary codec
 * mappings for our sound card instance.
*/
static int simtec_tlv320aic23_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;

	snd_soc_dapm_new_controls(codec, dapm_widgets,
				  ARRAY_SIZE(dapm_widgets));

	snd_soc_dapm_add_routes(codec, base_map, ARRAY_SIZE(base_map));

	snd_soc_dapm_enable_pin(codec, "Headphone Jack");
	snd_soc_dapm_enable_pin(codec, "Line In");
	snd_soc_dapm_enable_pin(codec, "Line Out");
	snd_soc_dapm_enable_pin(codec, "Mic Jack");

	simtec_audio_init(rtd);
	snd_soc_dapm_sync(codec);

	return 0;
}

static struct snd_soc_dai_link simtec_dai_aic23 = {
	.name		= "tlv320aic23",
	.stream_name	= "TLV320AIC23",
	.codec_name	= "tlv320aic3x-codec.0-0x1a",
	.cpu_dai_name	= "s3c24xx-i2s",
	.codec_dai_name = "tlv320aic3x-hifi",
	.platform_name	= "s3c24xx-pcm-audio",
	.init		= simtec_tlv320aic23_init,
};

/* simtec audio machine driver */
static struct snd_soc_card snd_soc_machine_simtec_aic23 = {
	.name		= "Simtec",
	.dai_link	= &simtec_dai_aic23,
	.num_links	= 1,
};

static int __devinit simtec_audio_tlv320aic23_probe(struct platform_device *pd)
{
	return simtec_audio_core_probe(pd, &snd_soc_machine_simtec_aic23);
}

static struct platform_driver simtec_audio_tlv320aic23_platdrv = {
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "s3c24xx-simtec-tlv320aic23",
		.pm	= simtec_audio_pm,
	},
	.probe	= simtec_audio_tlv320aic23_probe,
	.remove	= __devexit_p(simtec_audio_remove),
};

MODULE_ALIAS("platform:s3c24xx-simtec-tlv320aic23");

static int __init simtec_tlv320aic23_modinit(void)
{
	return platform_driver_register(&simtec_audio_tlv320aic23_platdrv);
}

static void __exit simtec_tlv320aic23_modexit(void)
{
	platform_driver_unregister(&simtec_audio_tlv320aic23_platdrv);
}

module_init(simtec_tlv320aic23_modinit);
module_exit(simtec_tlv320aic23_modexit);

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("ALSA SoC Simtec Audio support");
MODULE_LICENSE("GPL");
