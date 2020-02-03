// SPDX-License-Identifier: GPL-2.0-only
/*
 * ASoC machine driver for Intel Broadwell platforms with RT5677 codec
 *
 * Copyright (c) 2014, The Chromium OS Authors.  All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc-acpi.h>

#include "../common/sst-dsp.h"
#include "../haswell/sst-haswell-ipc.h"

#include "../../codecs/rt5677.h"

struct bdw_rt5677_priv {
	struct gpio_desc *gpio_hp_en;
	struct snd_soc_component *component;
};

static int bdw_rt5677_event_hp(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *k, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct bdw_rt5677_priv *bdw_rt5677 = snd_soc_card_get_drvdata(card);

	if (SND_SOC_DAPM_EVENT_ON(event))
		msleep(70);

	gpiod_set_value_cansleep(bdw_rt5677->gpio_hp_en,
		SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static const struct snd_soc_dapm_widget bdw_rt5677_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", bdw_rt5677_event_hp),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Local DMICs", NULL),
	SND_SOC_DAPM_MIC("Remote DMICs", NULL),
};

static const struct snd_soc_dapm_route bdw_rt5677_map[] = {
	/* Speakers */
	{"Speaker", NULL, "PDM1L"},
	{"Speaker", NULL, "PDM1R"},

	/* Headset jack connectors */
	{"Headphone", NULL, "LOUT1"},
	{"Headphone", NULL, "LOUT2"},
	{"IN1P", NULL, "Headset Mic"},
	{"IN1N", NULL, "Headset Mic"},

	/* Digital MICs
	 * Local DMICs: the two DMICs on the mainboard
	 * Remote DMICs: the two DMICs on the camera module
	 */
	{"DMIC L1", NULL, "Remote DMICs"},
	{"DMIC R1", NULL, "Remote DMICs"},
	{"DMIC L2", NULL, "Local DMICs"},
	{"DMIC R2", NULL, "Local DMICs"},

	/* CODEC BE connections */
	{"SSP0 CODEC IN", NULL, "AIF1 Capture"},
	{"AIF1 Playback", NULL, "SSP0 CODEC OUT"},
	{"DSP Capture", NULL, "DSP Buffer"},

	/* DSP Clock Connections */
	{ "DSP Buffer", NULL, "SSP0 CODEC IN" },
	{ "SSP0 CODEC IN", NULL, "DSPTX" },
};

static const struct snd_kcontrol_new bdw_rt5677_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Local DMICs"),
	SOC_DAPM_PIN_SWITCH("Remote DMICs"),
};


static struct snd_soc_jack headphone_jack;
static struct snd_soc_jack mic_jack;

static struct snd_soc_jack_pin headphone_jack_pin = {
	.pin	= "Headphone",
	.mask	= SND_JACK_HEADPHONE,
};

static struct snd_soc_jack_pin mic_jack_pin = {
	.pin	= "Headset Mic",
	.mask	= SND_JACK_MICROPHONE,
};

static struct snd_soc_jack_gpio headphone_jack_gpio = {
	.name			= "plug-det",
	.report			= SND_JACK_HEADPHONE,
	.debounce_time		= 200,
};

static struct snd_soc_jack_gpio mic_jack_gpio = {
	.name			= "mic-present",
	.report			= SND_JACK_MICROPHONE,
	.debounce_time		= 200,
	.invert			= 1,
};

/* GPIO indexes defined by ACPI */
enum {
	RT5677_GPIO_PLUG_DET		= 0,
	RT5677_GPIO_MIC_PRESENT_L	= 1,
	RT5677_GPIO_HOTWORD_DET_L	= 2,
	RT5677_GPIO_DSP_INT		= 3,
	RT5677_GPIO_HP_AMP_SHDN_L	= 4,
};

static const struct acpi_gpio_params plug_det_gpio = { RT5677_GPIO_PLUG_DET, 0, false };
static const struct acpi_gpio_params mic_present_gpio = { RT5677_GPIO_MIC_PRESENT_L, 0, false };
static const struct acpi_gpio_params headphone_enable_gpio = { RT5677_GPIO_HP_AMP_SHDN_L, 0, false };

static const struct acpi_gpio_mapping bdw_rt5677_gpios[] = {
	{ "plug-det-gpios", &plug_det_gpio, 1 },
	{ "mic-present-gpios", &mic_present_gpio, 1 },
	{ "headphone-enable-gpios", &headphone_enable_gpio, 1 },
	{ NULL },
};

static int broadwell_ssp0_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);

	/* The ADSP will covert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP0 to 16 bit */
	params_set_format(params, SNDRV_PCM_FORMAT_S16_LE);
	return 0;
}

static int bdw_rt5677_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5677_SCLK_S_MCLK, 24576000,
		SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk configuration\n");
		return ret;
	}

	return ret;
}

static int bdw_rt5677_dsp_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5677_SCLK_S_PLL1, 24576000,
		SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk configuration\n");
		return ret;
	}
	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5677_PLL1_S_MCLK,
		24000000, 24576000);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec pll configuration\n");
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops bdw_rt5677_ops = {
	.hw_params = bdw_rt5677_hw_params,
};

static const struct snd_soc_ops bdw_rt5677_dsp_ops = {
	.hw_params = bdw_rt5677_dsp_hw_params,
};

#if !IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
static int bdw_rt5677_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct sst_pdata *pdata = dev_get_platdata(component->dev);
	struct sst_hsw *broadwell = pdata->dsp;
	int ret;

	/* Set ADSP SSP port settings */
	ret = sst_hsw_device_set_config(broadwell, SST_HSW_DEVICE_SSP_0,
		SST_HSW_DEVICE_MCLK_FREQ_24_MHZ,
		SST_HSW_DEVICE_CLOCK_MASTER, 9);
	if (ret < 0) {
		dev_err(rtd->dev, "error: failed to set device config\n");
		return ret;
	}

	return 0;
}
#endif

static int bdw_rt5677_init(struct snd_soc_pcm_runtime *rtd)
{
	struct bdw_rt5677_priv *bdw_rt5677 =
			snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = rtd->codec_dai->component;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int ret;

	ret = devm_acpi_dev_add_driver_gpios(component->dev, bdw_rt5677_gpios);
	if (ret)
		dev_warn(component->dev, "Failed to add driver gpios\n");

	/* Enable codec ASRC function for Stereo DAC/Stereo1 ADC/DMIC/I2S1.
	 * The ASRC clock source is clk_i2s1_asrc.
	 */
	rt5677_sel_asrc_clk_src(component, RT5677_DA_STEREO_FILTER |
			RT5677_AD_STEREO1_FILTER | RT5677_I2S1_SOURCE,
			RT5677_CLK_SEL_I2S1_ASRC);
	/* Enable codec ASRC function for Mono ADC L.
	 * The ASRC clock source is clk_sys2_asrc.
	 */
	rt5677_sel_asrc_clk_src(component, RT5677_AD_MONO_L_FILTER,
			RT5677_CLK_SEL_SYS2);

	/* Request rt5677 GPIO for headphone amp control */
	bdw_rt5677->gpio_hp_en = devm_gpiod_get(component->dev, "headphone-enable",
						GPIOD_OUT_LOW);
	if (IS_ERR(bdw_rt5677->gpio_hp_en)) {
		dev_err(component->dev, "Can't find HP_AMP_SHDN_L gpio\n");
		return PTR_ERR(bdw_rt5677->gpio_hp_en);
	}

	/* Create and initialize headphone jack */
	if (!snd_soc_card_jack_new(rtd->card, "Headphone Jack",
			SND_JACK_HEADPHONE, &headphone_jack,
			&headphone_jack_pin, 1)) {
		headphone_jack_gpio.gpiod_dev = component->dev;
		if (snd_soc_jack_add_gpios(&headphone_jack, 1,
				&headphone_jack_gpio))
			dev_err(component->dev, "Can't add headphone jack gpio\n");
	} else {
		dev_err(component->dev, "Can't create headphone jack\n");
	}

	/* Create and initialize mic jack */
	if (!snd_soc_card_jack_new(rtd->card, "Mic Jack",
			SND_JACK_MICROPHONE, &mic_jack,
			&mic_jack_pin, 1)) {
		mic_jack_gpio.gpiod_dev = component->dev;
		if (snd_soc_jack_add_gpios(&mic_jack, 1, &mic_jack_gpio))
			dev_err(component->dev, "Can't add mic jack gpio\n");
	} else {
		dev_err(component->dev, "Can't create mic jack\n");
	}
	bdw_rt5677->component = component;

	snd_soc_dapm_force_enable_pin(dapm, "MICBIAS1");
	return 0;
}

/* broadwell digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(fe,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("haswell-pcm-audio")));

SND_SOC_DAILINK_DEF(be,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-RT5677CE:00", "rt5677-aif1")));

#if IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
SND_SOC_DAILINK_DEF(ssp0_port,
	    DAILINK_COMP_ARRAY(COMP_CPU("ssp0-port")));
#else
SND_SOC_DAILINK_DEF(ssp0_port,
	    DAILINK_COMP_ARRAY(COMP_DUMMY()));
#endif

/* Wake on voice interface */
SND_SOC_DAILINK_DEFS(dsp,
	DAILINK_COMP_ARRAY(COMP_CPU("spi-RT5677AA:00")),
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-RT5677CE:00", "rt5677-dspbuffer")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("spi-RT5677AA:00")));

static struct snd_soc_dai_link bdw_rt5677_dais[] = {
	/* Front End DAI links */
	{
		.name = "System PCM",
		.stream_name = "System Playback/Capture",
		.dynamic = 1,
#if !IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
		.init = bdw_rt5677_rtd_init,
#endif
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dpcm_capture = 1,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(fe, dummy, platform),
	},

	/* Non-DPCM links */
	{
		.name = "Codec DSP",
		.stream_name = "Wake on Voice",
		.ops = &bdw_rt5677_dsp_ops,
		SND_SOC_DAILINK_REG(dsp),
	},

	/* Back End DAI links */
	{
		/* SSP0 - Codec */
		.name = "Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = broadwell_ssp0_fixup,
		.ops = &bdw_rt5677_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = bdw_rt5677_init,
		SND_SOC_DAILINK_REG(ssp0_port, be, platform),
	},
};

static int bdw_rt5677_suspend_pre(struct snd_soc_card *card)
{
	struct bdw_rt5677_priv *bdw_rt5677 = snd_soc_card_get_drvdata(card);
	struct snd_soc_dapm_context *dapm;

	if (bdw_rt5677->component) {
		dapm = snd_soc_component_get_dapm(bdw_rt5677->component);
		snd_soc_dapm_disable_pin(dapm, "MICBIAS1");
	}
	return 0;
}

static int bdw_rt5677_resume_post(struct snd_soc_card *card)
{
	struct bdw_rt5677_priv *bdw_rt5677 = snd_soc_card_get_drvdata(card);
	struct snd_soc_dapm_context *dapm;

	if (bdw_rt5677->component) {
		dapm = snd_soc_component_get_dapm(bdw_rt5677->component);
		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS1");
	}
	return 0;
}

/* ASoC machine driver for Broadwell DSP + RT5677 */
static struct snd_soc_card bdw_rt5677_card = {
	.name = "bdw-rt5677",
	.owner = THIS_MODULE,
	.dai_link = bdw_rt5677_dais,
	.num_links = ARRAY_SIZE(bdw_rt5677_dais),
	.dapm_widgets = bdw_rt5677_widgets,
	.num_dapm_widgets = ARRAY_SIZE(bdw_rt5677_widgets),
	.dapm_routes = bdw_rt5677_map,
	.num_dapm_routes = ARRAY_SIZE(bdw_rt5677_map),
	.controls = bdw_rt5677_controls,
	.num_controls = ARRAY_SIZE(bdw_rt5677_controls),
	.fully_routed = true,
	.suspend_pre = bdw_rt5677_suspend_pre,
	.resume_post = bdw_rt5677_resume_post,
};

static int bdw_rt5677_probe(struct platform_device *pdev)
{
	struct bdw_rt5677_priv *bdw_rt5677;
	struct snd_soc_acpi_mach *mach;
	int ret;

	bdw_rt5677_card.dev = &pdev->dev;

	/* Allocate driver private struct */
	bdw_rt5677 = devm_kzalloc(&pdev->dev, sizeof(struct bdw_rt5677_priv),
		GFP_KERNEL);
	if (!bdw_rt5677) {
		dev_err(&pdev->dev, "Can't allocate bdw_rt5677\n");
		return -ENOMEM;
	}

	/* override plaform name, if required */
	mach = (&pdev->dev)->platform_data;
	ret = snd_soc_fixup_dai_links_platform_name(&bdw_rt5677_card,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(&bdw_rt5677_card, bdw_rt5677);

	return devm_snd_soc_register_card(&pdev->dev, &bdw_rt5677_card);
}

static struct platform_driver bdw_rt5677_audio = {
	.probe = bdw_rt5677_probe,
	.driver = {
		.name = "bdw-rt5677",
	},
};

module_platform_driver(bdw_rt5677_audio)

/* Module information */
MODULE_AUTHOR("Ben Zhang");
MODULE_DESCRIPTION("Intel Broadwell RT5677 machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bdw-rt5677");
