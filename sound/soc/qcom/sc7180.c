// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2020, The Linux Foundation. All rights reserved.
//
// sc7180.c -- ALSA SoC Machine driver for SC7180

#include <dt-bindings/sound/sc7180-lpass.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <uapi/linux/input-event-codes.h>

#include "../codecs/rt5682.h"
#include "common.h"
#include "lpass.h"

#define DEFAULT_MCLK_RATE		19200000
#define RT5682_PLL1_FREQ (48000 * 512)

#define DRIVER_NAME "SC7180"

struct sc7180_snd_data {
	struct snd_soc_card card;
	u32 pri_mi2s_clk_count;
	struct snd_soc_jack hs_jack;
	struct snd_soc_jack hdmi_jack;
	struct gpio_desc *dmic_sel;
	int dmic_switch;
};

static void sc7180_jack_free(struct snd_jack *jack)
{
	struct snd_soc_component *component = jack->private_data;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sc7180_headset_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct sc7180_snd_data *pdata = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	struct snd_jack *jack;
	int rval;

	rval = snd_soc_card_jack_new(
			card, "Headset Jack",
			SND_JACK_HEADSET |
			SND_JACK_HEADPHONE |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3,
			&pdata->hs_jack, NULL, 0);

	if (rval < 0) {
		dev_err(card->dev, "Unable to add Headset Jack\n");
		return rval;
	}

	jack = pdata->hs_jack.jack;

	snd_jack_set_key(jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	jack->private_data = component;
	jack->private_free = sc7180_jack_free;

	return snd_soc_component_set_jack(component, &pdata->hs_jack, NULL);
}

static int sc7180_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct sc7180_snd_data *pdata = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	struct snd_jack *jack;
	int rval;

	rval = snd_soc_card_jack_new(
			card, "HDMI Jack",
			SND_JACK_LINEOUT,
			&pdata->hdmi_jack, NULL, 0);

	if (rval < 0) {
		dev_err(card->dev, "Unable to add HDMI Jack\n");
		return rval;
	}

	jack = pdata->hdmi_jack.jack;
	jack->private_data = component;
	jack->private_free = sc7180_jack_free;

	return snd_soc_component_set_jack(component, &pdata->hdmi_jack, NULL);
}

static int sc7180_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);

	switch (cpu_dai->id) {
	case MI2S_PRIMARY:
		return sc7180_headset_init(rtd);
	case MI2S_SECONDARY:
		return 0;
	case LPASS_DP_RX:
		return sc7180_hdmi_init(rtd);
	default:
		dev_err(rtd->dev, "%s: invalid dai id 0x%x\n", __func__,
			cpu_dai->id);
		return -EINVAL;
	}
	return 0;
}

static int sc7180_snd_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct sc7180_snd_data *data = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int ret;

	switch (cpu_dai->id) {
	case MI2S_PRIMARY:
		if (++data->pri_mi2s_clk_count == 1) {
			snd_soc_dai_set_sysclk(cpu_dai,
					       LPASS_MCLK0,
					       DEFAULT_MCLK_RATE,
					       SNDRV_PCM_STREAM_PLAYBACK);
		}

		snd_soc_dai_set_fmt(codec_dai,
				    SND_SOC_DAIFMT_CBS_CFS |
				    SND_SOC_DAIFMT_NB_NF |
				    SND_SOC_DAIFMT_I2S);

		/* Configure PLL1 for codec */
		ret = snd_soc_dai_set_pll(codec_dai, 0, RT5682_PLL1_S_MCLK,
					  DEFAULT_MCLK_RATE, RT5682_PLL1_FREQ);
		if (ret) {
			dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
			return ret;
		}

		/* Configure sysclk for codec */
		ret = snd_soc_dai_set_sysclk(codec_dai, RT5682_SCLK_S_PLL1,
					     RT5682_PLL1_FREQ,
					     SND_SOC_CLOCK_IN);
		if (ret)
			dev_err(rtd->dev, "snd_soc_dai_set_sysclk err = %d\n",
				ret);

		break;
	case MI2S_SECONDARY:
		break;
	case LPASS_DP_RX:
		break;
	default:
		dev_err(rtd->dev, "%s: invalid dai id 0x%x\n", __func__,
			cpu_dai->id);
		return -EINVAL;
	}
	return 0;
}

static int dmic_get(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct sc7180_snd_data *data = snd_soc_card_get_drvdata(dapm->card);

	ucontrol->value.integer.value[0] = data->dmic_switch;
	return 0;
}

static int dmic_set(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct sc7180_snd_data *data = snd_soc_card_get_drvdata(dapm->card);

	data->dmic_switch = ucontrol->value.integer.value[0];
	gpiod_set_value(data->dmic_sel, data->dmic_switch);
	return 0;
}

static void sc7180_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct sc7180_snd_data *data = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);

	switch (cpu_dai->id) {
	case MI2S_PRIMARY:
		if (--data->pri_mi2s_clk_count == 0) {
			snd_soc_dai_set_sysclk(cpu_dai,
					       LPASS_MCLK0,
					       0,
					       SNDRV_PCM_STREAM_PLAYBACK);
		}
		break;
	case MI2S_SECONDARY:
		break;
	case LPASS_DP_RX:
		break;
	default:
		dev_err(rtd->dev, "%s: invalid dai id 0x%x\n", __func__,
			cpu_dai->id);
		break;
	}
}

static int sc7180_adau7002_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);

	switch (cpu_dai->id) {
	case MI2S_PRIMARY:
		return 0;
	case MI2S_SECONDARY:
		return 0;
	case LPASS_DP_RX:
		return sc7180_hdmi_init(rtd);
	default:
		dev_err(rtd->dev, "%s: invalid dai id 0x%x\n", __func__,
			cpu_dai->id);
		return -EINVAL;
	}
	return 0;
}

static int sc7180_adau7002_snd_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_pcm_runtime *runtime = substream->runtime;

	switch (cpu_dai->id) {
	case MI2S_PRIMARY:
		snd_soc_dai_set_fmt(codec_dai,
				    SND_SOC_DAIFMT_CBS_CFS |
				    SND_SOC_DAIFMT_NB_NF |
				    SND_SOC_DAIFMT_I2S);
		runtime->hw.formats = SNDRV_PCM_FMTBIT_S32_LE;
		snd_pcm_hw_constraint_msbits(runtime, 0, 32, 32);

		break;
	case MI2S_SECONDARY:
		break;
	case LPASS_DP_RX:
		break;
	default:
		dev_err(rtd->dev, "%s: invalid dai id 0x%x\n", __func__,
			cpu_dai->id);
		return -EINVAL;
	}
	return 0;
}

static const struct snd_soc_ops sc7180_ops = {
	.startup = sc7180_snd_startup,
	.shutdown = sc7180_snd_shutdown,
};

static const struct snd_soc_ops sc7180_adau7002_ops = {
	.startup = sc7180_adau7002_snd_startup,
};

static const struct snd_soc_dapm_widget sc7180_snd_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_widget sc7180_adau7002_snd_widgets[] = {
	SND_SOC_DAPM_MIC("DMIC", NULL),
};

static const char * const dmic_mux_text[] = {
	"Front Mic",
	"Rear Mic",
};

static SOC_ENUM_SINGLE_DECL(sc7180_dmic_enum,
			    SND_SOC_NOPM, 0, dmic_mux_text);

static const struct snd_kcontrol_new sc7180_dmic_mux_control =
	SOC_DAPM_ENUM_EXT("DMIC Select Mux", sc7180_dmic_enum,
			  dmic_get, dmic_set);

static const struct snd_soc_dapm_widget sc7180_snd_dual_mic_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("DMIC", NULL),
	SND_SOC_DAPM_MUX("Dmic Mux", SND_SOC_NOPM, 0, 0, &sc7180_dmic_mux_control),
};

static const struct snd_soc_dapm_route sc7180_snd_dual_mic_audio_route[] = {
	{"Dmic Mux", "Front Mic", "DMIC"},
	{"Dmic Mux", "Rear Mic", "DMIC"},
};

static int sc7180_snd_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct sc7180_snd_data *data;
	struct device *dev = &pdev->dev;
	struct snd_soc_dai_link *link;
	int ret;
	int i;
	bool no_headphone = false;

	/* Allocate the private data */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card = &data->card;
	snd_soc_card_set_drvdata(card, data);

	card->owner = THIS_MODULE;
	card->driver_name = DRIVER_NAME;
	card->dev = dev;
	card->dapm_widgets = sc7180_snd_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(sc7180_snd_widgets);

	if (of_property_read_bool(dev->of_node, "dmic-gpios")) {
		card->dapm_widgets = sc7180_snd_dual_mic_widgets,
		card->num_dapm_widgets = ARRAY_SIZE(sc7180_snd_dual_mic_widgets),
		card->dapm_routes = sc7180_snd_dual_mic_audio_route,
		card->num_dapm_routes = ARRAY_SIZE(sc7180_snd_dual_mic_audio_route),
		data->dmic_sel = devm_gpiod_get(&pdev->dev, "dmic", GPIOD_OUT_LOW);
		if (IS_ERR(data->dmic_sel)) {
			dev_err(&pdev->dev, "DMIC gpio failed err=%ld\n", PTR_ERR(data->dmic_sel));
			return PTR_ERR(data->dmic_sel);
		}
	}

	if (of_device_is_compatible(dev->of_node, "google,sc7180-coachz")) {
		no_headphone = true;
		card->dapm_widgets = sc7180_adau7002_snd_widgets;
		card->num_dapm_widgets = ARRAY_SIZE(sc7180_adau7002_snd_widgets);
	}

	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	for_each_card_prelinks(card, i, link) {
		if (no_headphone) {
			link->ops = &sc7180_adau7002_ops;
			link->init = sc7180_adau7002_init;
		} else {
			link->ops = &sc7180_ops;
			link->init = sc7180_init;
		}
	}

	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id sc7180_snd_device_id[]  = {
	{.compatible = "google,sc7180-trogdor"},
	{.compatible = "google,sc7180-coachz"},
	{},
};
MODULE_DEVICE_TABLE(of, sc7180_snd_device_id);

static struct platform_driver sc7180_snd_driver = {
	.probe = sc7180_snd_platform_probe,
	.driver = {
		.name = "msm-snd-sc7180",
		.of_match_table = sc7180_snd_device_id,
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(sc7180_snd_driver);

MODULE_DESCRIPTION("sc7180 ASoC Machine Driver");
MODULE_LICENSE("GPL v2");
