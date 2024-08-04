// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2018-19 Canonical Corporation.

/*
 * Intel Kabylake I2S Machine Driver with RT5660 Codec
 *
 * Modified from:
 *   Intel Kabylake I2S Machine driver supporting MAXIM98357a and
 *   DA7219 codecs
 * Also referred to:
 *   Intel Broadwell I2S Machine driver supporting RT5677 codec
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/acpi.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../../codecs/hdac_hdmi.h"
#include "../../codecs/rt5660.h"

#define KBL_RT5660_CODEC_DAI "rt5660-aif1"
#define DUAL_CHANNEL 2

static struct snd_soc_card *kabylake_audio_card;
static struct snd_soc_jack skylake_hdmi[3];
static struct snd_soc_jack lineout_jack;
static struct snd_soc_jack mic_jack;

struct kbl_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

struct kbl_codec_private {
	struct gpio_desc *gpio_lo_mute;
	struct list_head hdmi_pcm_list;
};

enum {
	KBL_DPCM_AUDIO_PB = 0,
	KBL_DPCM_AUDIO_CP,
	KBL_DPCM_AUDIO_HDMI1_PB,
	KBL_DPCM_AUDIO_HDMI2_PB,
	KBL_DPCM_AUDIO_HDMI3_PB,
};

#define GPIO_LINEOUT_MUTE_INDEX 0
#define GPIO_LINEOUT_DET_INDEX 3
#define GPIO_LINEIN_DET_INDEX 4

static const struct acpi_gpio_params lineout_mute_gpio = { GPIO_LINEOUT_MUTE_INDEX, 0, true };
static const struct acpi_gpio_params lineout_det_gpio = { GPIO_LINEOUT_DET_INDEX, 0, false };
static const struct acpi_gpio_params mic_det_gpio = { GPIO_LINEIN_DET_INDEX, 0, false };


static const struct acpi_gpio_mapping acpi_rt5660_gpios[] = {
	{ "lineout-mute-gpios", &lineout_mute_gpio, 1 },
	{ "lineout-det-gpios", &lineout_det_gpio, 1 },
	{ "mic-det-gpios", &mic_det_gpio, 1 },
	{ NULL },
};

static struct snd_soc_jack_pin lineout_jack_pin = {
	.pin	= "Line Out",
	.mask	= SND_JACK_LINEOUT,
};

static struct snd_soc_jack_pin mic_jack_pin = {
	.pin	= "Line In",
	.mask	= SND_JACK_MICROPHONE,
};

static struct snd_soc_jack_gpio lineout_jack_gpio = {
	.name			= "lineout-det",
	.report			= SND_JACK_LINEOUT,
	.debounce_time		= 200,
};

static struct snd_soc_jack_gpio mic_jack_gpio = {
	.name			= "mic-det",
	.report			= SND_JACK_MICROPHONE,
	.debounce_time		= 200,
};

static int kabylake_5660_event_lineout(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *k, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct kbl_codec_private *priv = snd_soc_card_get_drvdata(dapm->card);

	gpiod_set_value_cansleep(priv->gpio_lo_mute,
			!(SND_SOC_DAPM_EVENT_ON(event)));

	return 0;
}

static const struct snd_kcontrol_new kabylake_rt5660_controls[] = {
	SOC_DAPM_PIN_SWITCH("Line In"),
	SOC_DAPM_PIN_SWITCH("Line Out"),
};

static const struct snd_soc_dapm_widget kabylake_rt5660_widgets[] = {
	SND_SOC_DAPM_MIC("Line In", NULL),
	SND_SOC_DAPM_LINE("Line Out", kabylake_5660_event_lineout),
};

static const struct snd_soc_dapm_route kabylake_rt5660_map[] = {
	/* other jacks */
	{"IN1P", NULL, "Line In"},
	{"IN2P", NULL, "Line In"},
	{"Line Out", NULL, "LOUTR"},
	{"Line Out", NULL, "LOUTL"},

	/* CODEC BE connections */
	{ "AIF1 Playback", NULL, "ssp0 Tx"},
	{ "ssp0 Tx", NULL, "codec0_out"},

	{ "codec0_in", NULL, "ssp0 Rx" },
	{ "ssp0 Rx", NULL, "AIF1 Capture" },

	{ "hifi1", NULL, "iDisp1 Tx"},
	{ "iDisp1 Tx", NULL, "iDisp1_out"},
	{ "hifi2", NULL, "iDisp2 Tx"},
	{ "iDisp2 Tx", NULL, "iDisp2_out"},
	{ "hifi3", NULL, "iDisp3 Tx"},
	{ "iDisp3 Tx", NULL, "iDisp3_out"},
};

static int kabylake_ssp0_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *chan = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The ADSP will convert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	chan->min = chan->max = DUAL_CHANNEL;

	/* set SSP0 to 24 bit */
	snd_mask_none(fmt);
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

static int kabylake_rt5660_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct kbl_codec_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	ret = devm_acpi_dev_add_driver_gpios(component->dev, acpi_rt5660_gpios);
	if (ret)
		dev_warn(component->dev, "Failed to add driver gpios\n");

	/* Request rt5660 GPIO for lineout mute control, return if fails */
	ctx->gpio_lo_mute = gpiod_get(component->dev, "lineout-mute",
				      GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->gpio_lo_mute)) {
		dev_err(component->dev, "Can't find GPIO_MUTE# gpio\n");
		return PTR_ERR(ctx->gpio_lo_mute);
	}

	/* Create and initialize headphone jack, this jack is not mandatory, don't return if fails */
	ret = snd_soc_card_jack_new_pins(rtd->card, "Lineout Jack",
					 SND_JACK_LINEOUT, &lineout_jack,
					 &lineout_jack_pin, 1);
	if (ret)
		dev_warn(component->dev, "Can't create Lineout jack\n");
	else {
		lineout_jack_gpio.gpiod_dev = component->dev;
		ret = snd_soc_jack_add_gpios(&lineout_jack, 1,
					     &lineout_jack_gpio);
		if (ret)
			dev_warn(component->dev, "Can't add Lineout jack gpio\n");
	}

	/* Create and initialize mic jack, this jack is not mandatory, don't return if fails */
	ret = snd_soc_card_jack_new_pins(rtd->card, "Mic Jack",
					 SND_JACK_MICROPHONE, &mic_jack,
					 &mic_jack_pin, 1);
	if (ret)
		dev_warn(component->dev, "Can't create mic jack\n");
	else {
		mic_jack_gpio.gpiod_dev = component->dev;
		ret = snd_soc_jack_add_gpios(&mic_jack, 1, &mic_jack_gpio);
		if (ret)
			dev_warn(component->dev, "Can't add mic jack gpio\n");
	}

	/* Here we enable some dapms in advance to reduce the pop noise for recording via line-in */
	snd_soc_dapm_force_enable_pin(dapm, "MICBIAS1");
	snd_soc_dapm_force_enable_pin(dapm, "BST1");
	snd_soc_dapm_force_enable_pin(dapm, "BST2");

	return 0;
}

static void kabylake_rt5660_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct kbl_codec_private *ctx = snd_soc_card_get_drvdata(rtd->card);

	/*
	 * The .exit() can be reached without going through the .init()
	 * so explicitly test if the gpiod is valid
	 */
	if (!IS_ERR_OR_NULL(ctx->gpio_lo_mute))
		gpiod_put(ctx->gpio_lo_mute);
}

static int kabylake_hdmi_init(struct snd_soc_pcm_runtime *rtd, int device)
{
	struct kbl_codec_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = snd_soc_rtd_to_codec(rtd, 0);
	struct kbl_hdmi_pcm *pcm;

	pcm = devm_kzalloc(rtd->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	pcm->device = device;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

static int kabylake_hdmi1_init(struct snd_soc_pcm_runtime *rtd)
{
	return kabylake_hdmi_init(rtd, KBL_DPCM_AUDIO_HDMI1_PB);
}

static int kabylake_hdmi2_init(struct snd_soc_pcm_runtime *rtd)
{
	return kabylake_hdmi_init(rtd, KBL_DPCM_AUDIO_HDMI2_PB);
}

static int kabylake_hdmi3_init(struct snd_soc_pcm_runtime *rtd)
{
	return kabylake_hdmi_init(rtd, KBL_DPCM_AUDIO_HDMI3_PB);
}

static int kabylake_rt5660_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai,
				     RT5660_SCLK_S_PLL1, params_rate(params) * 512,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "snd_soc_dai_set_sysclk err = %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_pll(codec_dai, 0,
				  RT5660_PLL1_S_BCLK,
				  params_rate(params) * 50,
				  params_rate(params) * 512);
	if (ret < 0)
		dev_err(codec_dai->dev, "can't set codec pll: %d\n", ret);

	return ret;
}

static const struct snd_soc_ops kabylake_rt5660_ops = {
	.hw_params = kabylake_rt5660_hw_params,
};

static const unsigned int rates[] = {
	48000,
};

static const struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list  = rates,
	.mask = 0,
};

static const unsigned int channels[] = {
	DUAL_CHANNEL,
};

static const struct snd_pcm_hw_constraint_list constraints_channels = {
	.count = ARRAY_SIZE(channels),
	.list = channels,
	.mask = 0,
};

static int kbl_fe_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	/*
	 * On this platform for PCM device we support,
	 * 48Khz
	 * stereo
	 * 16 bit audio
	 */

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
					   &constraints_channels);

	runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
	snd_pcm_hw_constraint_msbits(runtime, 0, 16, 16);

	snd_pcm_hw_constraint_list(runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);

	return 0;
}

static const struct snd_soc_ops kabylake_rt5660_fe_ops = {
	.startup = kbl_fe_startup,
};

SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(system,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin")));

SND_SOC_DAILINK_DEF(hdmi1,
	DAILINK_COMP_ARRAY(COMP_CPU("HDMI1 Pin")));

SND_SOC_DAILINK_DEF(hdmi2,
	DAILINK_COMP_ARRAY(COMP_CPU("HDMI2 Pin")));

SND_SOC_DAILINK_DEF(hdmi3,
	DAILINK_COMP_ARRAY(COMP_CPU("HDMI3 Pin")));

SND_SOC_DAILINK_DEF(ssp0_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP0 Pin")));
SND_SOC_DAILINK_DEF(ssp0_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC3277:00", KBL_RT5660_CODEC_DAI)));

SND_SOC_DAILINK_DEF(idisp1_pin,
		    DAILINK_COMP_ARRAY(COMP_CPU("iDisp1 Pin")));
SND_SOC_DAILINK_DEF(idisp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi1")));

SND_SOC_DAILINK_DEF(idisp2_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp2 Pin")));
SND_SOC_DAILINK_DEF(idisp2_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi2")));

SND_SOC_DAILINK_DEF(idisp3_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp3 Pin")));
SND_SOC_DAILINK_DEF(idisp3_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi3")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:1f.3")));

/* kabylake digital audio interface glue - connects rt5660 codec <--> CPU */
static struct snd_soc_dai_link kabylake_rt5660_dais[] = {
	/* Front End DAI links */
	[KBL_DPCM_AUDIO_PB] = {
		.name = "Kbl Audio Port",
		.stream_name = "Audio",
		.dynamic = 1,
		.nonatomic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.ops = &kabylake_rt5660_fe_ops,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	[KBL_DPCM_AUDIO_CP] = {
		.name = "Kbl Audio Capture Port",
		.stream_name = "Audio Record",
		.dynamic = 1,
		.nonatomic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
		.ops = &kabylake_rt5660_fe_ops,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	[KBL_DPCM_AUDIO_HDMI1_PB] = {
		.name = "Kbl HDMI Port1",
		.stream_name = "Hdmi1",
		.dpcm_playback = 1,
		.init = NULL,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi1, dummy, platform),
	},
	[KBL_DPCM_AUDIO_HDMI2_PB] = {
		.name = "Kbl HDMI Port2",
		.stream_name = "Hdmi2",
		.dpcm_playback = 1,
		.init = NULL,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi2, dummy, platform),
	},
	[KBL_DPCM_AUDIO_HDMI3_PB] = {
		.name = "Kbl HDMI Port3",
		.stream_name = "Hdmi3",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi3, dummy, platform),
	},

	/* Back End DAI links */
	{
		/* SSP0 - Codec */
		.name = "SSP0-Codec",
		.id = 0,
		.no_pcm = 1,
		.init = kabylake_rt5660_codec_init,
		.exit = kabylake_rt5660_codec_exit,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBC_CFC,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = kabylake_ssp0_fixup,
		.ops = &kabylake_rt5660_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp0_pin, ssp0_codec, platform),
	},
	{
		.name = "iDisp1",
		.id = 1,
		.dpcm_playback = 1,
		.init = kabylake_hdmi1_init,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp1_pin, idisp1_codec, platform),
	},
	{
		.name = "iDisp2",
		.id = 2,
		.init = kabylake_hdmi2_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp2_pin, idisp2_codec, platform),
	},
	{
		.name = "iDisp3",
		.id = 3,
		.init = kabylake_hdmi3_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp3_pin, idisp3_codec, platform),
	},
};


#define NAME_SIZE	32
static int kabylake_card_late_probe(struct snd_soc_card *card)
{
	struct kbl_codec_private *ctx = snd_soc_card_get_drvdata(card);
	struct kbl_hdmi_pcm *pcm;
	struct snd_soc_component *component = NULL;
	int err, i = 0;
	char jack_name[NAME_SIZE];

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			"HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					SND_JACK_AVOUT, &skylake_hdmi[i]);

		if (err)
			return err;

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
				&skylake_hdmi[i]);
		if (err < 0)
			return err;

		i++;

	}

	if (!component)
		return -EINVAL;

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}

/* kabylake audio machine driver for rt5660 */
static struct snd_soc_card kabylake_audio_card_rt5660 = {
	.name = "kblrt5660",
	.owner = THIS_MODULE,
	.dai_link = kabylake_rt5660_dais,
	.num_links = ARRAY_SIZE(kabylake_rt5660_dais),
	.controls = kabylake_rt5660_controls,
	.num_controls = ARRAY_SIZE(kabylake_rt5660_controls),
	.dapm_widgets = kabylake_rt5660_widgets,
	.num_dapm_widgets = ARRAY_SIZE(kabylake_rt5660_widgets),
	.dapm_routes = kabylake_rt5660_map,
	.num_dapm_routes = ARRAY_SIZE(kabylake_rt5660_map),
	.fully_routed = true,
	.disable_route_checks = true,
	.late_probe = kabylake_card_late_probe,
};

static int kabylake_audio_probe(struct platform_device *pdev)
{
	struct kbl_codec_private *ctx;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	kabylake_audio_card =
		(struct snd_soc_card *)pdev->id_entry->driver_data;

	kabylake_audio_card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(kabylake_audio_card, ctx);
	return devm_snd_soc_register_card(&pdev->dev, kabylake_audio_card);
}

static const struct platform_device_id kbl_board_ids[] = {
	{
		.name = "kbl_rt5660",
		.driver_data =
			(kernel_ulong_t)&kabylake_audio_card_rt5660,
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, kbl_board_ids);

static struct platform_driver kabylake_audio = {
	.probe = kabylake_audio_probe,
	.driver = {
		.name = "kbl_rt5660",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = kbl_board_ids,
};

module_platform_driver(kabylake_audio)

/* Module information */
MODULE_DESCRIPTION("Audio Machine driver-RT5660 in I2S mode");
MODULE_AUTHOR("Hui Wang <hui.wang@canonical.com>");
MODULE_LICENSE("GPL v2");
