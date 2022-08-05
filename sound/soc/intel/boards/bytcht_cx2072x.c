// SPDX-License-Identifier: GPL-2.0-only
//
// ASoC DPCM Machine driver for Baytrail / Cherrytrail platforms with
// CX2072X codec
//

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../codecs/cx2072x.h"
#include "../atom/sst-atom-controls.h"

static const struct snd_soc_dapm_widget byt_cht_cx2072x_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_route byt_cht_cx2072x_audio_map[] = {
	/* External Speakers: HFL, HFR */
	{"Headphone", NULL, "PORTA"},
	{"Ext Spk", NULL, "PORTG"},
	{"PORTC", NULL, "Int Mic"},
	{"PORTD", NULL, "Headset Mic"},

	{"Playback", NULL, "ssp2 Tx"},
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},
	{"ssp2 Rx", NULL, "Capture"},
};

static const struct snd_kcontrol_new byt_cht_cx2072x_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static struct snd_soc_jack byt_cht_cx2072x_headset;

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin byt_cht_cx2072x_headset_pins[] = {
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	},
};

static const struct acpi_gpio_params byt_cht_cx2072x_headset_gpios;
static const struct acpi_gpio_mapping byt_cht_cx2072x_acpi_gpios[] = {
	{ "headset-gpios", &byt_cht_cx2072x_headset_gpios, 1 },
	{},
};

static int byt_cht_cx2072x_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_component *codec = asoc_rtd_to_codec(rtd, 0)->component;
	int ret;

	if (devm_acpi_dev_add_driver_gpios(codec->dev,
					   byt_cht_cx2072x_acpi_gpios))
		dev_warn(rtd->dev, "Unable to add GPIO mapping table\n");

	card->dapm.idle_bias_off = true;

	/* set the default PLL rate, the clock is handled by the codec driver */
	ret = snd_soc_dai_set_sysclk(asoc_rtd_to_codec(rtd, 0), CX2072X_MCLK_EXTERNAL_PLL,
				     19200000, SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(rtd->dev, "Could not set sysclk\n");
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(card, "Headset",
					 SND_JACK_HEADSET | SND_JACK_BTN_0,
					 &byt_cht_cx2072x_headset,
					 byt_cht_cx2072x_headset_pins,
					 ARRAY_SIZE(byt_cht_cx2072x_headset_pins));
	if (ret)
		return ret;

	snd_soc_component_set_jack(codec, &byt_cht_cx2072x_headset, NULL);

	snd_soc_dai_set_bclk_ratio(asoc_rtd_to_codec(rtd, 0), 50);

	return 0;
}

static int byt_cht_cx2072x_fixup(struct snd_soc_pcm_runtime *rtd,
				 struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	int ret;

	/* The DSP will covert the FE rate to 48k, stereo, 24bits */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP2 to 24-bit */
	params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);

	/*
	 * Default mode for SSP configuration is TDM 4 slot, override config
	 * with explicit setting to I2S 2ch 24-bit. The word length is set with
	 * dai_set_tdm_slot() since there is no other API exposed
	 */
	ret = snd_soc_dai_set_fmt(asoc_rtd_to_cpu(rtd, 0),
				SND_SOC_DAIFMT_I2S     |
				SND_SOC_DAIFMT_NB_NF   |
				SND_SOC_DAIFMT_CBC_CFC);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set format to I2S, err %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(asoc_rtd_to_cpu(rtd, 0), 0x3, 0x3, 2, 24);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set I2S config, err %d\n", ret);
		return ret;
	}

	return 0;
}

static int byt_cht_cx2072x_aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_single(substream->runtime,
					    SNDRV_PCM_HW_PARAM_RATE, 48000);
}

static const struct snd_soc_ops byt_cht_cx2072x_aif1_ops = {
	.startup = byt_cht_cx2072x_aif1_startup,
};

SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(media,
	DAILINK_COMP_ARRAY(COMP_CPU("media-cpu-dai")));

SND_SOC_DAILINK_DEF(deepbuffer,
	DAILINK_COMP_ARRAY(COMP_CPU("deepbuffer-cpu-dai")));

SND_SOC_DAILINK_DEF(ssp2,
	DAILINK_COMP_ARRAY(COMP_CPU("ssp2-port")));

SND_SOC_DAILINK_DEF(cx2072x,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-14F10720:00", "cx2072x-hifi")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("sst-mfld-platform")));

static struct snd_soc_dai_link byt_cht_cx2072x_dais[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Audio Port",
		.stream_name = "Audio",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &byt_cht_cx2072x_aif1_ops,
		SND_SOC_DAILINK_REG(media, dummy, platform),
	},
	[MERR_DPCM_DEEP_BUFFER] = {
		.name = "Deep-Buffer Audio Port",
		.stream_name = "Deep-Buffer Audio",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &byt_cht_cx2072x_aif1_ops,
		SND_SOC_DAILINK_REG(deepbuffer, dummy, platform),
	},
	/* back ends */
	{
		.name = "SSP2-Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
					      | SND_SOC_DAIFMT_CBC_CFC,
		.init = byt_cht_cx2072x_init,
		.be_hw_params_fixup = byt_cht_cx2072x_fixup,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp2, cx2072x, platform),
	},
};

/* use space before codec name to simplify card ID, and simplify driver name */
#define SOF_CARD_NAME "bytcht cx2072x" /* card name will be 'sof-bytcht cx2072x' */
#define SOF_DRIVER_NAME "SOF"

#define CARD_NAME "bytcht-cx2072x"
#define DRIVER_NAME NULL /* card name will be used for driver name */

/* SoC card */
static struct snd_soc_card byt_cht_cx2072x_card = {
	.name = CARD_NAME,
	.driver_name = DRIVER_NAME,
	.owner = THIS_MODULE,
	.dai_link = byt_cht_cx2072x_dais,
	.num_links = ARRAY_SIZE(byt_cht_cx2072x_dais),
	.dapm_widgets = byt_cht_cx2072x_widgets,
	.num_dapm_widgets = ARRAY_SIZE(byt_cht_cx2072x_widgets),
	.dapm_routes = byt_cht_cx2072x_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_cht_cx2072x_audio_map),
	.controls = byt_cht_cx2072x_controls,
	.num_controls = ARRAY_SIZE(byt_cht_cx2072x_controls),
};

static char codec_name[SND_ACPI_I2C_ID_LEN];

static int snd_byt_cht_cx2072x_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach;
	struct acpi_device *adev;
	int dai_index = 0;
	bool sof_parent;
	int i, ret;

	byt_cht_cx2072x_card.dev = &pdev->dev;
	mach = dev_get_platdata(&pdev->dev);

	/* fix index of codec dai */
	for (i = 0; i < ARRAY_SIZE(byt_cht_cx2072x_dais); i++) {
		if (!strcmp(byt_cht_cx2072x_dais[i].codecs->name,
			    "i2c-14F10720:00")) {
			dai_index = i;
			break;
		}
	}

	/* fixup codec name based on HID */
	adev = acpi_dev_get_first_match_dev(mach->id, NULL, -1);
	if (adev) {
		snprintf(codec_name, sizeof(codec_name), "i2c-%s",
			 acpi_dev_name(adev));
		put_device(&adev->dev);
		byt_cht_cx2072x_dais[dai_index].codecs->name = codec_name;
	}

	/* override platform name, if required */
	ret = snd_soc_fixup_dai_links_platform_name(&byt_cht_cx2072x_card,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	sof_parent = snd_soc_acpi_sof_parent(&pdev->dev);

	/* set card and driver name */
	if (sof_parent) {
		byt_cht_cx2072x_card.name = SOF_CARD_NAME;
		byt_cht_cx2072x_card.driver_name = SOF_DRIVER_NAME;
	} else {
		byt_cht_cx2072x_card.name = CARD_NAME;
		byt_cht_cx2072x_card.driver_name = DRIVER_NAME;
	}

	/* set pm ops */
	if (sof_parent)
		pdev->dev.driver->pm = &snd_soc_pm_ops;

	return devm_snd_soc_register_card(&pdev->dev, &byt_cht_cx2072x_card);
}

static struct platform_driver snd_byt_cht_cx2072x_driver = {
	.driver = {
		.name = "bytcht_cx2072x",
	},
	.probe = snd_byt_cht_cx2072x_probe,
};
module_platform_driver(snd_byt_cht_cx2072x_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail/Cherrytrail Machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bytcht_cx2072x");
