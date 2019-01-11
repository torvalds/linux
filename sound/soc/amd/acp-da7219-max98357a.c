/*
 * Machine driver for AMD ACP Audio engine using DA7219 & MAX98357 codec
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/acpi.h>

#include "acp.h"
#include "../codecs/da7219.h"
#include "../codecs/da7219-aad.h"

#define CZ_PLAT_CLK 25000000
#define DUAL_CHANNEL		2

static struct snd_soc_jack cz_jack;
static struct clk *da7219_dai_clk;
extern int bt_uart_enable;

static int cz_da7219_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_component *component = codec_dai->component;

	dev_info(rtd->dev, "codec dai name = %s\n", codec_dai->name);

	ret = snd_soc_dai_set_sysclk(codec_dai, DA7219_CLKSRC_MCLK,
				     CZ_PLAT_CLK, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_pll(codec_dai, 0, DA7219_SYSCLK_PLL,
				  CZ_PLAT_CLK, DA7219_PLL_FREQ_OUT_98304);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}

	da7219_dai_clk = clk_get(component->dev, "da7219-dai-clks");

	ret = snd_soc_card_jack_new(card, "Headset Jack",
				SND_JACK_HEADPHONE | SND_JACK_MICROPHONE |
				SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				SND_JACK_BTN_2 | SND_JACK_BTN_3,
				&cz_jack, NULL, 0);
	if (ret) {
		dev_err(card->dev, "HP jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(cz_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(cz_jack.jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(cz_jack.jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
	snd_jack_set_key(cz_jack.jack, SND_JACK_BTN_3, KEY_VOICECOMMAND);

	da7219_aad_jack_det(component, &cz_jack);

	return 0;
}

static int da7219_clk_enable(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	ret = clk_prepare_enable(da7219_dai_clk);
	if (ret < 0) {
		dev_err(rtd->dev, "can't enable master clock %d\n", ret);
		return ret;
	}

	return ret;
}

static void da7219_clk_disable(void)
{
	clk_disable_unprepare(da7219_dai_clk);
}

static const unsigned int channels[] = {
	DUAL_CHANNEL,
};

static const unsigned int rates[] = {
	48000,
};

static const struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list  = rates,
	.mask = 0,
};

static const struct snd_pcm_hw_constraint_list constraints_channels = {
	.count = ARRAY_SIZE(channels),
	.list = channels,
	.mask = 0,
};

static int cz_da7219_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct acp_platform_info *machine = snd_soc_card_get_drvdata(card);

	/*
	 * On this platform for PCM device we support stereo
	 */

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &constraints_rates);

	machine->i2s_instance = I2S_BT_INSTANCE;
	return da7219_clk_enable(substream);
}

static void cz_da7219_shutdown(struct snd_pcm_substream *substream)
{
	da7219_clk_disable();
}

static int cz_max_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct acp_platform_info *machine = snd_soc_card_get_drvdata(card);

	machine->i2s_instance = I2S_SP_INSTANCE;
	return da7219_clk_enable(substream);
}

static void cz_max_shutdown(struct snd_pcm_substream *substream)
{
	da7219_clk_disable();
}

static int cz_dmic_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct acp_platform_info *machine = snd_soc_card_get_drvdata(card);

	machine->i2s_instance = I2S_SP_INSTANCE;
	return da7219_clk_enable(substream);
}

static void cz_dmic_shutdown(struct snd_pcm_substream *substream)
{
	da7219_clk_disable();
}

static const struct snd_soc_ops cz_da7219_cap_ops = {
	.startup = cz_da7219_startup,
	.shutdown = cz_da7219_shutdown,
};

static const struct snd_soc_ops cz_max_play_ops = {
	.startup = cz_max_startup,
	.shutdown = cz_max_shutdown,
};

static const struct snd_soc_ops cz_dmic_cap_ops = {
	.startup = cz_dmic_startup,
	.shutdown = cz_dmic_shutdown,
};

static struct snd_soc_dai_link cz_dai_7219_98357[] = {
	{
		.name = "amd-da7219-play-cap",
		.stream_name = "Playback and Capture",
		.platform_name = "acp_audio_dma.0.auto",
		.cpu_dai_name = "designware-i2s.3.auto",
		.codec_dai_name = "da7219-hifi",
		.codec_name = "i2c-DLGS7219:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.init = cz_da7219_init,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &cz_da7219_cap_ops,
	},
	{
		.name = "amd-max98357-play",
		.stream_name = "HiFi Playback",
		.platform_name = "acp_audio_dma.0.auto",
		.cpu_dai_name = "designware-i2s.1.auto",
		.codec_dai_name = "HiFi",
		.codec_name = "MX98357A:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_playback = 1,
		.ops = &cz_max_play_ops,
	},
	{
		.name = "dmic",
		.stream_name = "DMIC Capture",
		.platform_name = "acp_audio_dma.0.auto",
		.cpu_dai_name = "designware-i2s.2.auto",
		.codec_dai_name = "adau7002-hifi",
		.codec_name = "ADAU7002:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_capture = 1,
		.ops = &cz_dmic_cap_ops,
	},
};

static const struct snd_soc_dapm_widget cz_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_SPK("Speakers", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
};

static const struct snd_soc_dapm_route cz_audio_route[] = {
	{"Headphones", NULL, "HPL"},
	{"Headphones", NULL, "HPR"},
	{"MIC", NULL, "Headset Mic"},
	{"Speakers", NULL, "Speaker"},
	{"PDM_DAT", NULL, "Int Mic"},
};

static const struct snd_kcontrol_new cz_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphones"),
	SOC_DAPM_PIN_SWITCH("Speakers"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
};

static struct snd_soc_card cz_card = {
	.name = "acpd7219m98357",
	.owner = THIS_MODULE,
	.dai_link = cz_dai_7219_98357,
	.num_links = ARRAY_SIZE(cz_dai_7219_98357),
	.dapm_widgets = cz_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cz_widgets),
	.dapm_routes = cz_audio_route,
	.num_dapm_routes = ARRAY_SIZE(cz_audio_route),
	.controls = cz_mc_controls,
	.num_controls = ARRAY_SIZE(cz_mc_controls),
};

static int cz_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card;
	struct acp_platform_info *machine;

	machine = devm_kzalloc(&pdev->dev, sizeof(struct acp_platform_info),
			       GFP_KERNEL);
	if (!machine)
		return -ENOMEM;
	card = &cz_card;
	cz_card.dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);
	ret = devm_snd_soc_register_card(&pdev->dev, &cz_card);
	if (ret) {
		dev_err(&pdev->dev,
				"devm_snd_soc_register_card(%s) failed: %d\n",
				cz_card.name, ret);
		return ret;
	}
	bt_uart_enable = !device_property_read_bool(&pdev->dev,
						    "bt-pad-enable");
	return 0;
}

static const struct acpi_device_id cz_audio_acpi_match[] = {
	{ "AMD7219", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, cz_audio_acpi_match);

static struct platform_driver cz_pcm_driver = {
	.driver = {
		.name = "cz-da7219-max98357a",
		.acpi_match_table = ACPI_PTR(cz_audio_acpi_match),
		.pm = &snd_soc_pm_ops,
	},
	.probe = cz_probe,
};

module_platform_driver(cz_pcm_driver);

MODULE_AUTHOR("akshu.agrawal@amd.com");
MODULE_DESCRIPTION("DA7219 & MAX98357A audio support");
MODULE_LICENSE("GPL v2");
