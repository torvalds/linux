// SPDX-License-Identifier: MIT
//
// Machine driver for AMD ACP Audio engine using DA7219, RT5682 & MAX98357 codec
//
//Copyright 2017-2021 Advanced Micro Devices, Inc.

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/acpi.h>

#include "acp.h"
#include "../codecs/da7219.h"
#include "../codecs/da7219-aad.h"
#include "../codecs/rt5682.h"

#define CZ_PLAT_CLK 48000000
#define DUAL_CHANNEL		2
#define RT5682_PLL_FREQ (48000 * 512)

static struct snd_soc_jack cz_jack;
static struct clk *da7219_dai_wclk;
static struct clk *da7219_dai_bclk;
static struct clk *rt5682_dai_wclk;
static struct clk *rt5682_dai_bclk;

void *acp_soc_is_rltk_max(struct device *dev);

static int cz_da7219_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
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

	da7219_dai_wclk = devm_clk_get(component->dev, "da7219-dai-wclk");
	if (IS_ERR(da7219_dai_wclk))
		return PTR_ERR(da7219_dai_wclk);

	da7219_dai_bclk = devm_clk_get(component->dev, "da7219-dai-bclk");
	if (IS_ERR(da7219_dai_bclk))
		return PTR_ERR(da7219_dai_bclk);

	ret = snd_soc_card_jack_new(card, "Headset Jack",
				SND_JACK_HEADSET | SND_JACK_LINEOUT |
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
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);

	/*
	 * Set wclk to 48000 because the rate constraint of this driver is
	 * 48000. ADAU7002 spec: "The ADAU7002 requires a BCLK rate that is
	 * minimum of 64x the LRCLK sample rate." DA7219 is the only clk
	 * source so for all codecs we have to limit bclk to 64X lrclk.
	 */
	clk_set_rate(da7219_dai_wclk, 48000);
	clk_set_rate(da7219_dai_bclk, 48000 * 64);
	ret = clk_prepare_enable(da7219_dai_bclk);
	if (ret < 0) {
		dev_err(rtd->dev, "can't enable master clock %d\n", ret);
		return ret;
	}

	return ret;
}

static void da7219_clk_disable(void)
{
	clk_disable_unprepare(da7219_dai_bclk);
}

static int cz_rt5682_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;

	dev_info(codec_dai->dev, "codec dai name = %s\n", codec_dai->name);

	/* Set codec sysclk */
	ret = snd_soc_dai_set_sysclk(codec_dai, RT5682_SCLK_S_PLL2,
				     RT5682_PLL_FREQ, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev,
			"Failed to set rt5682 SYSCLK: %d\n", ret);
		return ret;
	}
	/* set codec PLL */
	ret = snd_soc_dai_set_pll(codec_dai, RT5682_PLL2, RT5682_PLL2_S_MCLK,
				  CZ_PLAT_CLK, RT5682_PLL_FREQ);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set rt5682 PLL: %d\n", ret);
		return ret;
	}

	rt5682_dai_wclk = devm_clk_get(component->dev, "rt5682-dai-wclk");
	if (IS_ERR(rt5682_dai_wclk))
		return PTR_ERR(rt5682_dai_wclk);

	rt5682_dai_bclk = devm_clk_get(component->dev, "rt5682-dai-bclk");
	if (IS_ERR(rt5682_dai_bclk))
		return PTR_ERR(rt5682_dai_bclk);

	ret = snd_soc_card_jack_new(card, "Headset Jack",
				    SND_JACK_HEADSET | SND_JACK_LINEOUT |
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

	ret = snd_soc_component_set_jack(component, &cz_jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static int rt5682_clk_enable(struct snd_pcm_substream *substream)
{
	int ret;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);

	/*
	 * Set wclk to 48000 because the rate constraint of this driver is
	 * 48000. ADAU7002 spec: "The ADAU7002 requires a BCLK rate that is
	 * minimum of 64x the LRCLK sample rate." RT5682 is the only clk
	 * source so for all codecs we have to limit bclk to 64X lrclk.
	 */
	ret = clk_set_rate(rt5682_dai_wclk, 48000);
	if (ret) {
		dev_err(rtd->dev, "Error setting wclk rate: %d\n", ret);
		return ret;
	}
	ret = clk_set_rate(rt5682_dai_bclk, 48000 * 64);
	if (ret) {
		dev_err(rtd->dev, "Error setting bclk rate: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(rt5682_dai_wclk);
	if (ret < 0) {
		dev_err(rtd->dev, "can't enable wclk %d\n", ret);
		return ret;
	}
	return ret;
}

static void rt5682_clk_disable(void)
{
	clk_disable_unprepare(rt5682_dai_wclk);
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

static int cz_da7219_play_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
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

	machine->play_i2s_instance = I2S_SP_INSTANCE;
	return da7219_clk_enable(substream);
}

static int cz_da7219_cap_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
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

	machine->cap_i2s_instance = I2S_SP_INSTANCE;
	machine->capture_channel = CAP_CHANNEL1;
	return da7219_clk_enable(substream);
}

static int cz_max_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
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

	machine->play_i2s_instance = I2S_BT_INSTANCE;
	return da7219_clk_enable(substream);
}

static int cz_dmic0_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
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

	machine->cap_i2s_instance = I2S_BT_INSTANCE;
	return da7219_clk_enable(substream);
}

static int cz_dmic1_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
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

	machine->cap_i2s_instance = I2S_SP_INSTANCE;
	machine->capture_channel = CAP_CHANNEL0;
	return da7219_clk_enable(substream);
}

static void cz_da7219_shutdown(struct snd_pcm_substream *substream)
{
	da7219_clk_disable();
}

static int cz_rt5682_play_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
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

	machine->play_i2s_instance = I2S_SP_INSTANCE;
	return rt5682_clk_enable(substream);
}

static int cz_rt5682_cap_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
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

	machine->cap_i2s_instance = I2S_SP_INSTANCE;
	machine->capture_channel = CAP_CHANNEL1;
	return rt5682_clk_enable(substream);
}

static int cz_rt5682_max_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
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

	machine->play_i2s_instance = I2S_BT_INSTANCE;
	return rt5682_clk_enable(substream);
}

static int cz_rt5682_dmic0_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
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

	machine->cap_i2s_instance = I2S_BT_INSTANCE;
	return rt5682_clk_enable(substream);
}

static int cz_rt5682_dmic1_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
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

	machine->cap_i2s_instance = I2S_SP_INSTANCE;
	machine->capture_channel = CAP_CHANNEL0;
	return rt5682_clk_enable(substream);
}

static void cz_rt5682_shutdown(struct snd_pcm_substream *substream)
{
	rt5682_clk_disable();
}

static const struct snd_soc_ops cz_da7219_play_ops = {
	.startup = cz_da7219_play_startup,
	.shutdown = cz_da7219_shutdown,
};

static const struct snd_soc_ops cz_da7219_cap_ops = {
	.startup = cz_da7219_cap_startup,
	.shutdown = cz_da7219_shutdown,
};

static const struct snd_soc_ops cz_max_play_ops = {
	.startup = cz_max_startup,
	.shutdown = cz_da7219_shutdown,
};

static const struct snd_soc_ops cz_dmic0_cap_ops = {
	.startup = cz_dmic0_startup,
	.shutdown = cz_da7219_shutdown,
};

static const struct snd_soc_ops cz_dmic1_cap_ops = {
	.startup = cz_dmic1_startup,
	.shutdown = cz_da7219_shutdown,
};

static const struct snd_soc_ops cz_rt5682_play_ops = {
	.startup = cz_rt5682_play_startup,
	.shutdown = cz_rt5682_shutdown,
};

static const struct snd_soc_ops cz_rt5682_cap_ops = {
	.startup = cz_rt5682_cap_startup,
	.shutdown = cz_rt5682_shutdown,
};

static const struct snd_soc_ops cz_rt5682_max_play_ops = {
	.startup = cz_rt5682_max_startup,
	.shutdown = cz_rt5682_shutdown,
};

static const struct snd_soc_ops cz_rt5682_dmic0_cap_ops = {
	.startup = cz_rt5682_dmic0_startup,
	.shutdown = cz_rt5682_shutdown,
};

static const struct snd_soc_ops cz_rt5682_dmic1_cap_ops = {
	.startup = cz_rt5682_dmic1_startup,
	.shutdown = cz_rt5682_shutdown,
};

SND_SOC_DAILINK_DEF(designware1,
	DAILINK_COMP_ARRAY(COMP_CPU("designware-i2s.1.auto")));
SND_SOC_DAILINK_DEF(designware2,
	DAILINK_COMP_ARRAY(COMP_CPU("designware-i2s.2.auto")));
SND_SOC_DAILINK_DEF(designware3,
	DAILINK_COMP_ARRAY(COMP_CPU("designware-i2s.3.auto")));

SND_SOC_DAILINK_DEF(dlgs,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-DLGS7219:00", "da7219-hifi")));
SND_SOC_DAILINK_DEF(rt5682,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC5682:00", "rt5682-aif1")));
SND_SOC_DAILINK_DEF(mx,
	DAILINK_COMP_ARRAY(COMP_CODEC("MX98357A:00", "HiFi")));
SND_SOC_DAILINK_DEF(adau,
	DAILINK_COMP_ARRAY(COMP_CODEC("ADAU7002:00", "adau7002-hifi")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("acp_audio_dma.0.auto")));

static struct snd_soc_dai_link cz_dai_7219_98357[] = {
	{
		.name = "amd-da7219-play",
		.stream_name = "Playback",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.init = cz_da7219_init,
		.dpcm_playback = 1,
		.stop_dma_first = 1,
		.ops = &cz_da7219_play_ops,
		SND_SOC_DAILINK_REG(designware1, dlgs, platform),
	},
	{
		.name = "amd-da7219-cap",
		.stream_name = "Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_capture = 1,
		.stop_dma_first = 1,
		.ops = &cz_da7219_cap_ops,
		SND_SOC_DAILINK_REG(designware2, dlgs, platform),
	},
	{
		.name = "amd-max98357-play",
		.stream_name = "HiFi Playback",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_playback = 1,
		.stop_dma_first = 1,
		.ops = &cz_max_play_ops,
		SND_SOC_DAILINK_REG(designware3, mx, platform),
	},
	{
		/* C panel DMIC */
		.name = "dmic0",
		.stream_name = "DMIC0 Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_capture = 1,
		.stop_dma_first = 1,
		.ops = &cz_dmic0_cap_ops,
		SND_SOC_DAILINK_REG(designware3, adau, platform),
	},
	{
		/* A/B panel DMIC */
		.name = "dmic1",
		.stream_name = "DMIC1 Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_capture = 1,
		.stop_dma_first = 1,
		.ops = &cz_dmic1_cap_ops,
		SND_SOC_DAILINK_REG(designware2, adau, platform),
	},
};

static struct snd_soc_dai_link cz_dai_5682_98357[] = {
	{
		.name = "amd-rt5682-play",
		.stream_name = "Playback",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.init = cz_rt5682_init,
		.dpcm_playback = 1,
		.stop_dma_first = 1,
		.ops = &cz_rt5682_play_ops,
		SND_SOC_DAILINK_REG(designware1, rt5682, platform),
	},
	{
		.name = "amd-rt5682-cap",
		.stream_name = "Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_capture = 1,
		.stop_dma_first = 1,
		.ops = &cz_rt5682_cap_ops,
		SND_SOC_DAILINK_REG(designware2, rt5682, platform),
	},
	{
		.name = "amd-max98357-play",
		.stream_name = "HiFi Playback",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_playback = 1,
		.stop_dma_first = 1,
		.ops = &cz_rt5682_max_play_ops,
		SND_SOC_DAILINK_REG(designware3, mx, platform),
	},
	{
		/* C panel DMIC */
		.name = "dmic0",
		.stream_name = "DMIC0 Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_capture = 1,
		.stop_dma_first = 1,
		.ops = &cz_rt5682_dmic0_cap_ops,
		SND_SOC_DAILINK_REG(designware3, adau, platform),
	},
	{
		/* A/B panel DMIC */
		.name = "dmic1",
		.stream_name = "DMIC1 Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_capture = 1,
		.stop_dma_first = 1,
		.ops = &cz_rt5682_dmic1_cap_ops,
		SND_SOC_DAILINK_REG(designware2, adau, platform),
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

static const struct snd_soc_dapm_route cz_rt5682_audio_route[] = {
	{"Headphones", NULL, "HPOL"},
	{"Headphones", NULL, "HPOR"},
	{"IN1P", NULL, "Headset Mic"},
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

static struct snd_soc_card cz_rt5682_card = {
	.name = "acpr5682m98357",
	.owner = THIS_MODULE,
	.dai_link = cz_dai_5682_98357,
	.num_links = ARRAY_SIZE(cz_dai_5682_98357),
	.dapm_widgets = cz_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cz_widgets),
	.dapm_routes = cz_rt5682_audio_route,
	.controls = cz_mc_controls,
	.num_controls = ARRAY_SIZE(cz_mc_controls),
};

void *acp_soc_is_rltk_max(struct device *dev)
{
	const struct acpi_device_id *match;

	match = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!match)
		return NULL;
	return (void *)match->driver_data;
}

static struct regulator_consumer_supply acp_da7219_supplies[] = {
	REGULATOR_SUPPLY("VDD", "i2c-DLGS7219:00"),
	REGULATOR_SUPPLY("VDDMIC", "i2c-DLGS7219:00"),
	REGULATOR_SUPPLY("VDDIO", "i2c-DLGS7219:00"),
	REGULATOR_SUPPLY("IOVDD", "ADAU7002:00"),
};

static struct regulator_init_data acp_da7219_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(acp_da7219_supplies),
	.consumer_supplies = acp_da7219_supplies,
};

static struct regulator_config acp_da7219_cfg = {
	.init_data = &acp_da7219_data,
};

static struct regulator_ops acp_da7219_ops = {
};

static const struct regulator_desc acp_da7219_desc = {
	.name = "reg-fixed-1.8V",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &acp_da7219_ops,
	.fixed_uV = 1800000, /* 1.8V */
	.n_voltages = 1,
};

static int cz_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card;
	struct acp_platform_info *machine;
	struct regulator_dev *rdev;
	struct device *dev = &pdev->dev;

	card = (struct snd_soc_card *)acp_soc_is_rltk_max(dev);
	if (!card)
		return -ENODEV;
	if (!strcmp(card->name, "acpd7219m98357")) {
		acp_da7219_cfg.dev = &pdev->dev;
		rdev = devm_regulator_register(&pdev->dev, &acp_da7219_desc,
					       &acp_da7219_cfg);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "Failed to register regulator: %d\n",
				(int)PTR_ERR(rdev));
			return -EINVAL;
		}
	}

	machine = devm_kzalloc(&pdev->dev, sizeof(struct acp_platform_info),
			       GFP_KERNEL);
	if (!machine)
		return -ENOMEM;
	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);
	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				"devm_snd_soc_register_card(%s) failed\n",
				card->name);
	}
	acp_bt_uart_enable = !device_property_read_bool(&pdev->dev,
							"bt-pad-enable");
	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id cz_audio_acpi_match[] = {
	{ "AMD7219", (unsigned long)&cz_card },
	{ "AMDI5682", (unsigned long)&cz_rt5682_card},
	{},
};
MODULE_DEVICE_TABLE(acpi, cz_audio_acpi_match);
#endif

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
MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("DA7219, RT5682 & MAX98357A audio support");
MODULE_LICENSE("GPL v2");
