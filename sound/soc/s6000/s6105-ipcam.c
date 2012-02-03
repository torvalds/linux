/*
 * ASoC driver for Stretch s6105 IP camera platform
 *
 * Author:      Daniel Gloeckner, <dg@emlix.com>
 * Copyright:   (C) 2009 emlix GmbH <info@emlix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <variant/dmac.h>

#include "s6000-pcm.h"
#include "s6000-i2s.h"

#define S6105_CAM_CODEC_CLOCK 12288000

static int s6105_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret = 0;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
					     SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBM_CFM |
					   SND_SOC_DAIFMT_NB_NF);
	if (ret < 0)
		return ret;

	/* set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, S6105_CAM_CODEC_CLOCK,
					    SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops s6105_ops = {
	.hw_params = s6105_hw_params,
};

/* s6105 machine dapm widgets */
static const struct snd_soc_dapm_widget aic3x_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Audio Out Differential", NULL),
	SND_SOC_DAPM_LINE("Audio Out Stereo", NULL),
	SND_SOC_DAPM_LINE("Audio In", NULL),
};

/* s6105 machine audio_mapnections to the codec pins */
static const struct snd_soc_dapm_route audio_map[] = {
	/* Audio Out connected to HPLOUT, HPLCOM, HPROUT */
	{"Audio Out Differential", NULL, "HPLOUT"},
	{"Audio Out Differential", NULL, "HPLCOM"},
	{"Audio Out Stereo", NULL, "HPLOUT"},
	{"Audio Out Stereo", NULL, "HPROUT"},

	/* Audio In connected to LINE1L, LINE1R */
	{"LINE1L", NULL, "Audio In"},
	{"LINE1R", NULL, "Audio In"},
};

static int output_type_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item) {
		uinfo->value.enumerated.item = 1;
		strcpy(uinfo->value.enumerated.name, "HPLOUT/HPROUT");
	} else {
		strcpy(uinfo->value.enumerated.name, "HPLOUT/HPLCOM");
	}
	return 0;
}

static int output_type_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = kcontrol->private_value;
	return 0;
}

static int output_type_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = kcontrol->private_data;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	unsigned int val = (ucontrol->value.enumerated.item[0] != 0);
	char *differential = "Audio Out Differential";
	char *stereo = "Audio Out Stereo";

	if (kcontrol->private_value == val)
		return 0;
	kcontrol->private_value = val;
	snd_soc_dapm_disable_pin(dapm, val ? differential : stereo);
	snd_soc_dapm_sync(dapm);
	snd_soc_dapm_enable_pin(dapm, val ? stereo : differential);
	snd_soc_dapm_sync(dapm);

	return 1;
}

static const struct snd_kcontrol_new audio_out_mux = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Master Output Mux",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = output_type_info,
	.get = output_type_get,
	.put = output_type_put,
	.private_value = 1 /* default to stereo */
};

/* Logic for a aic3x as connected on the s6105 ip camera ref design */
static int s6105_aic3x_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	/* Add s6105 specific widgets */
	snd_soc_dapm_new_controls(dapm, aic3x_dapm_widgets,
				  ARRAY_SIZE(aic3x_dapm_widgets));

	/* Set up s6105 specific audio path audio_map */
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	/* not present */
	snd_soc_dapm_nc_pin(dapm, "MONO_LOUT");
	snd_soc_dapm_nc_pin(dapm, "LINE2L");
	snd_soc_dapm_nc_pin(dapm, "LINE2R");

	/* not connected */
	snd_soc_dapm_nc_pin(dapm, "MIC3L"); /* LINE2L on this chip */
	snd_soc_dapm_nc_pin(dapm, "MIC3R"); /* LINE2R on this chip */
	snd_soc_dapm_nc_pin(dapm, "LLOUT");
	snd_soc_dapm_nc_pin(dapm, "RLOUT");
	snd_soc_dapm_nc_pin(dapm, "HPRCOM");

	/* always connected */
	snd_soc_dapm_enable_pin(dapm, "Audio In");

	/* must correspond to audio_out_mux.private_value initializer */
	snd_soc_dapm_disable_pin(dapm, "Audio Out Differential");
	snd_soc_dapm_sync(dapm);
	snd_soc_dapm_enable_pin(dapm, "Audio Out Stereo");

	snd_soc_dapm_sync(dapm);

	snd_ctl_add(codec->card->snd_card, snd_ctl_new1(&audio_out_mux, codec));

	return 0;
}

/* s6105 digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link s6105_dai = {
	.name = "TLV320AIC31",
	.stream_name = "AIC31",
	.cpu_dai_name = "s6000-i2s",
	.codec_dai_name = "tlv320aic3x-hifi",
	.platform_name = "s6000-pcm-audio",
	.codec_name = "tlv320aic3x-codec.0-001a",
	.init = s6105_aic3x_init,
	.ops = &s6105_ops,
};

/* s6105 audio machine driver */
static struct snd_soc_card snd_soc_card_s6105 = {
	.name = "Stretch IP Camera",
	.owner = THIS_MODULE,
	.dai_link = &s6105_dai,
	.num_links = 1,
};

static struct s6000_snd_platform_data __initdata s6105_snd_data = {
	.wide		= 0,
	.channel_in	= 0,
	.channel_out	= 1,
	.lines_in	= 1,
	.lines_out	= 1,
	.same_rate	= 1,
};

static struct platform_device *s6105_snd_device;

/* temporary i2c device creation until this can be moved into the machine
 * support file.
*/
static struct i2c_board_info i2c_device[] = {
	{ I2C_BOARD_INFO("tlv320aic33", 0x18), }
};

static int __init s6105_init(void)
{
	int ret;

	i2c_register_board_info(0, i2c_device, ARRAY_SIZE(i2c_device));

	s6105_snd_device = platform_device_alloc("soc-audio", -1);
	if (!s6105_snd_device)
		return -ENOMEM;

	platform_set_drvdata(s6105_snd_device, &snd_soc_card_s6105);
	platform_device_add_data(s6105_snd_device, &s6105_snd_data,
				 sizeof(s6105_snd_data));

	ret = platform_device_add(s6105_snd_device);
	if (ret)
		platform_device_put(s6105_snd_device);

	return ret;
}

static void __exit s6105_exit(void)
{
	platform_device_unregister(s6105_snd_device);
}

module_init(s6105_init);
module_exit(s6105_exit);

MODULE_AUTHOR("Daniel Gloeckner");
MODULE_DESCRIPTION("Stretch s6105 IP camera ASoC driver");
MODULE_LICENSE("GPL");
