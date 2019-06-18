// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ad1980.c  --  ALSA Soc AD1980 codec support
 *
 * Copyright:	Analog Device Inc.
 * Author:	Roy Huang <roy.huang@analog.com>
 * 		Cliff Cai <cliff.cai@analog.com>
 */

/*
 * WARNING:
 *
 * Because Analog Devices Inc. discontinued the ad1980 sound chip since
 * Sep. 2009, this ad1980 driver is not maintained, tested and supported
 * by ADI now.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/soc.h>

static const struct reg_default ad1980_reg_defaults[] = {
	{ 0x02, 0x8000 },
	{ 0x04, 0x8000 },
	{ 0x06, 0x8000 },
	{ 0x0c, 0x8008 },
	{ 0x0e, 0x8008 },
	{ 0x10, 0x8808 },
	{ 0x12, 0x8808 },
	{ 0x16, 0x8808 },
	{ 0x18, 0x8808 },
	{ 0x1a, 0x0000 },
	{ 0x1c, 0x8000 },
	{ 0x20, 0x0000 },
	{ 0x28, 0x03c7 },
	{ 0x2c, 0xbb80 },
	{ 0x2e, 0xbb80 },
	{ 0x30, 0xbb80 },
	{ 0x32, 0xbb80 },
	{ 0x36, 0x8080 },
	{ 0x38, 0x8080 },
	{ 0x3a, 0x2000 },
	{ 0x60, 0x0000 },
	{ 0x62, 0x0000 },
	{ 0x72, 0x0000 },
	{ 0x74, 0x1001 },
	{ 0x76, 0x0000 },
};

static bool ad1980_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AC97_RESET ... AC97_MASTER_MONO:
	case AC97_PHONE ... AC97_CD:
	case AC97_AUX ... AC97_GENERAL_PURPOSE:
	case AC97_POWERDOWN ... AC97_PCM_LR_ADC_RATE:
	case AC97_SPDIF:
	case AC97_CODEC_CLASS_REV:
	case AC97_PCI_SVID:
	case AC97_AD_CODEC_CFG:
	case AC97_AD_JACK_SPDIF:
	case AC97_AD_SERIAL_CFG:
	case AC97_VENDOR_ID1:
	case AC97_VENDOR_ID2:
		return true;
	default:
		return false;
	}
}

static bool ad1980_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AC97_VENDOR_ID1:
	case AC97_VENDOR_ID2:
		return false;
	default:
		return ad1980_readable_reg(dev, reg);
	}
}

static const struct regmap_config ad1980_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.max_register = 0x7e,
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = regmap_ac97_default_volatile,
	.readable_reg = ad1980_readable_reg,
	.writeable_reg = ad1980_writeable_reg,

	.reg_defaults = ad1980_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ad1980_reg_defaults),
};

static const char *ad1980_rec_sel[] = {"Mic", "CD", "NC", "AUX", "Line",
		"Stereo Mix", "Mono Mix", "Phone"};

static SOC_ENUM_DOUBLE_DECL(ad1980_cap_src,
			    AC97_REC_SEL, 8, 0, ad1980_rec_sel);

static const struct snd_kcontrol_new ad1980_snd_ac97_controls[] = {
SOC_DOUBLE("Master Playback Volume", AC97_MASTER, 8, 0, 31, 1),
SOC_SINGLE("Master Playback Switch", AC97_MASTER, 15, 1, 1),

SOC_DOUBLE("Headphone Playback Volume", AC97_HEADPHONE, 8, 0, 31, 1),
SOC_SINGLE("Headphone Playback Switch", AC97_HEADPHONE, 15, 1, 1),

SOC_DOUBLE("PCM Playback Volume", AC97_PCM, 8, 0, 31, 1),
SOC_SINGLE("PCM Playback Switch", AC97_PCM, 15, 1, 1),

SOC_DOUBLE("PCM Capture Volume", AC97_REC_GAIN, 8, 0, 31, 0),
SOC_SINGLE("PCM Capture Switch", AC97_REC_GAIN, 15, 1, 1),

SOC_SINGLE("Mono Playback Volume", AC97_MASTER_MONO, 0, 31, 1),
SOC_SINGLE("Mono Playback Switch", AC97_MASTER_MONO, 15, 1, 1),

SOC_SINGLE("Phone Capture Volume", AC97_PHONE, 0, 31, 1),
SOC_SINGLE("Phone Capture Switch", AC97_PHONE, 15, 1, 1),

SOC_SINGLE("Mic Volume", AC97_MIC, 0, 31, 1),
SOC_SINGLE("Mic Switch", AC97_MIC, 15, 1, 1),

SOC_SINGLE("Stereo Mic Switch", AC97_AD_MISC, 6, 1, 0),
SOC_DOUBLE("Line HP Swap Switch", AC97_AD_MISC, 10, 5, 1, 0),

SOC_DOUBLE("Surround Playback Volume", AC97_SURROUND_MASTER, 8, 0, 31, 1),
SOC_DOUBLE("Surround Playback Switch", AC97_SURROUND_MASTER, 15, 7, 1, 1),

SOC_DOUBLE("Center/LFE Playback Volume", AC97_CENTER_LFE_MASTER, 8, 0, 31, 1),
SOC_DOUBLE("Center/LFE Playback Switch", AC97_CENTER_LFE_MASTER, 15, 7, 1, 1),

SOC_ENUM("Capture Source", ad1980_cap_src),

SOC_SINGLE("Mic Boost Switch", AC97_MIC, 6, 1, 0),
};

static const struct snd_soc_dapm_widget ad1980_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("MIC1"),
SND_SOC_DAPM_INPUT("MIC2"),
SND_SOC_DAPM_INPUT("CD_L"),
SND_SOC_DAPM_INPUT("CD_R"),
SND_SOC_DAPM_INPUT("AUX_L"),
SND_SOC_DAPM_INPUT("AUX_R"),
SND_SOC_DAPM_INPUT("LINE_IN_L"),
SND_SOC_DAPM_INPUT("LINE_IN_R"),

SND_SOC_DAPM_OUTPUT("LFE_OUT"),
SND_SOC_DAPM_OUTPUT("CENTER_OUT"),
SND_SOC_DAPM_OUTPUT("LINE_OUT_L"),
SND_SOC_DAPM_OUTPUT("LINE_OUT_R"),
SND_SOC_DAPM_OUTPUT("MONO_OUT"),
SND_SOC_DAPM_OUTPUT("HP_OUT_L"),
SND_SOC_DAPM_OUTPUT("HP_OUT_R"),
};

static const struct snd_soc_dapm_route ad1980_dapm_routes[] = {
	{ "Capture", NULL, "MIC1" },
	{ "Capture", NULL, "MIC2" },
	{ "Capture", NULL, "CD_L" },
	{ "Capture", NULL, "CD_R" },
	{ "Capture", NULL, "AUX_L" },
	{ "Capture", NULL, "AUX_R" },
	{ "Capture", NULL, "LINE_IN_L" },
	{ "Capture", NULL, "LINE_IN_R" },

	{ "LFE_OUT", NULL, "Playback" },
	{ "CENTER_OUT", NULL, "Playback" },
	{ "LINE_OUT_L", NULL, "Playback" },
	{ "LINE_OUT_R", NULL, "Playback" },
	{ "MONO_OUT", NULL, "Playback" },
	{ "HP_OUT_L", NULL, "Playback" },
	{ "HP_OUT_R", NULL, "Playback" },
};

static struct snd_soc_dai_driver ad1980_dai = {
	.name = "ad1980-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 6,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SND_SOC_STD_AC97_FMTS, },
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SND_SOC_STD_AC97_FMTS, },
};

#define AD1980_VENDOR_ID 0x41445300
#define AD1980_VENDOR_MASK 0xffffff00

static int ad1980_reset(struct snd_soc_component *component, int try_warm)
{
	struct snd_ac97 *ac97 = snd_soc_component_get_drvdata(component);
	unsigned int retry_cnt = 0;
	int ret;

	do {
		ret = snd_ac97_reset(ac97, true, AD1980_VENDOR_ID,
			AD1980_VENDOR_MASK);
		if (ret >= 0)
			return 0;

		/*
		 * Set bit 16slot in register 74h, then every slot will has only
		 * 16 bits. This command is sent out in 20bit mode, in which
		 * case the first nibble of data is eaten by the addr. (Tag is
		 * always 16 bit)
		 */
		snd_soc_component_write(component, AC97_AD_SERIAL_CFG, 0x9900);

	} while (retry_cnt++ < 10);

	dev_err(component->dev, "Failed to reset: AC97 link error\n");

	return -EIO;
}

static int ad1980_soc_probe(struct snd_soc_component *component)
{
	struct snd_ac97 *ac97;
	struct regmap *regmap;
	int ret;
	u16 vendor_id2;
	u16 ext_status;

	ac97 = snd_soc_new_ac97_component(component, 0, 0);
	if (IS_ERR(ac97)) {
		ret = PTR_ERR(ac97);
		dev_err(component->dev, "Failed to register AC97 component: %d\n", ret);
		return ret;
	}

	regmap = regmap_init_ac97(ac97, &ad1980_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		goto err_free_ac97;
	}

	snd_soc_component_init_regmap(component, regmap);
	snd_soc_component_set_drvdata(component, ac97);

	ret = ad1980_reset(component, 0);
	if (ret < 0)
		goto reset_err;

	vendor_id2 = snd_soc_component_read32(component, AC97_VENDOR_ID2);
	if (vendor_id2 == 0x5374) {
		dev_warn(component->dev,
			"Found AD1981 - only 2/2 IN/OUT Channels supported\n");
	}

	/* unmute captures and playbacks volume */
	snd_soc_component_write(component, AC97_MASTER, 0x0000);
	snd_soc_component_write(component, AC97_PCM, 0x0000);
	snd_soc_component_write(component, AC97_REC_GAIN, 0x0000);
	snd_soc_component_write(component, AC97_CENTER_LFE_MASTER, 0x0000);
	snd_soc_component_write(component, AC97_SURROUND_MASTER, 0x0000);

	/*power on LFE/CENTER/Surround DACs*/
	ext_status = snd_soc_component_read32(component, AC97_EXTENDED_STATUS);
	snd_soc_component_write(component, AC97_EXTENDED_STATUS, ext_status&~0x3800);

	return 0;

reset_err:
	snd_soc_component_exit_regmap(component);
err_free_ac97:
	snd_soc_free_ac97_component(ac97);
	return ret;
}

static void ad1980_soc_remove(struct snd_soc_component *component)
{
	struct snd_ac97 *ac97 = snd_soc_component_get_drvdata(component);

	snd_soc_component_exit_regmap(component);
	snd_soc_free_ac97_component(ac97);
}

static const struct snd_soc_component_driver soc_component_dev_ad1980 = {
	.probe			= ad1980_soc_probe,
	.remove			= ad1980_soc_remove,
	.controls		= ad1980_snd_ac97_controls,
	.num_controls		= ARRAY_SIZE(ad1980_snd_ac97_controls),
	.dapm_widgets		= ad1980_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ad1980_dapm_widgets),
	.dapm_routes		= ad1980_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(ad1980_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int ad1980_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
			&soc_component_dev_ad1980, &ad1980_dai, 1);
}

static struct platform_driver ad1980_codec_driver = {
	.driver = {
			.name = "ad1980",
	},

	.probe = ad1980_probe,
};

module_platform_driver(ad1980_codec_driver);

MODULE_DESCRIPTION("ASoC ad1980 driver (Obsolete)");
MODULE_AUTHOR("Roy Huang, Cliff Cai");
MODULE_LICENSE("GPL");
