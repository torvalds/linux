// SPDX-License-Identifier: GPL-2.0
//
// mt8183-mt6358.c  --
//	MT8183-MT6358-TS3A227-MAX98357 ALSA SoC machine driver
//
// Copyright (c) 2018 MediaTek Inc.
// Author: Shunli Wang <shunli.wang@mediatek.com>

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../../codecs/rt1015.h"
#include "../../codecs/ts3a227e.h"
#include "mt8183-afe-common.h"

#define RT1015_CODEC_DAI "rt1015-aif"
#define RT1015_DEV0_NAME "rt1015.6-0028"
#define RT1015_DEV1_NAME "rt1015.6-0029"

enum PINCTRL_PIN_STATE {
	PIN_STATE_DEFAULT = 0,
	PIN_TDM_OUT_ON,
	PIN_TDM_OUT_OFF,
	PIN_WOV,
	PIN_STATE_MAX
};

static const char * const mt8183_pin_str[PIN_STATE_MAX] = {
	"default", "aud_tdm_out_on", "aud_tdm_out_off", "wov",
};

struct mt8183_mt6358_ts3a227_max98357_priv {
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_states[PIN_STATE_MAX];
	struct snd_soc_jack headset_jack, hdmi_jack;
};

static int mt8183_mt6358_i2s_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs_ratio = 128;
	unsigned int mclk_fs = rate * mclk_fs_ratio;

	return snd_soc_dai_set_sysclk(asoc_rtd_to_cpu(rtd, 0),
				      0, mclk_fs, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8183_mt6358_i2s_ops = {
	.hw_params = mt8183_mt6358_i2s_hw_params,
};

static int
mt8183_mt6358_rt1015_i2s_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs_ratio = 128;
	unsigned int mclk_fs = rate * mclk_fs_ratio;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai;
	int ret, i;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_pll(codec_dai, 0, RT1015_PLL_S_BCLK,
				rate * 64, rate * 256);
		if (ret < 0) {
			dev_err(card->dev, "failed to set pll\n");
			return ret;
		}

		ret = snd_soc_dai_set_sysclk(codec_dai, RT1015_SCLK_S_PLL,
				rate * 256, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(card->dev, "failed to set sysclk\n");
			return ret;
		}
	}

	return snd_soc_dai_set_sysclk(asoc_rtd_to_cpu(rtd, 0),
				      0, mclk_fs, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8183_mt6358_rt1015_i2s_ops = {
	.hw_params = mt8183_mt6358_rt1015_i2s_hw_params,
};

static int mt8183_i2s_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				      struct snd_pcm_hw_params *params)
{
	dev_dbg(rtd->dev, "%s(), fix format to S32_LE\n", __func__);

	/* fix BE i2s format to S32_LE, clean param mask first */
	snd_mask_reset_range(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
			     0, (__force unsigned int)SNDRV_PCM_FORMAT_LAST);

	params_set_format(params, SNDRV_PCM_FORMAT_S32_LE);
	return 0;
}

static int mt8183_rt1015_i2s_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					     struct snd_pcm_hw_params *params)
{
	dev_dbg(rtd->dev, "%s(), fix format to S24_LE\n", __func__);

	/* fix BE i2s format to S24_LE, clean param mask first */
	snd_mask_reset_range(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
			     0, (__force unsigned int)SNDRV_PCM_FORMAT_LAST);

	params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);
	return 0;
}

static int
mt8183_mt6358_startup(struct snd_pcm_substream *substream)
{
	static const unsigned int rates[] = {
		48000,
	};
	static const struct snd_pcm_hw_constraint_list constraints_rates = {
		.count = ARRAY_SIZE(rates),
		.list  = rates,
		.mask = 0,
	};
	static const unsigned int channels[] = {
		2,
	};
	static const struct snd_pcm_hw_constraint_list constraints_channels = {
		.count = ARRAY_SIZE(channels),
		.list = channels,
		.mask = 0,
	};

	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_hw_constraint_list(runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);
	runtime->hw.channels_max = 2;
	snd_pcm_hw_constraint_list(runtime, 0,
				   SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);

	runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
	snd_pcm_hw_constraint_msbits(runtime, 0, 16, 16);

	return 0;
}

static const struct snd_soc_ops mt8183_mt6358_ops = {
	.startup = mt8183_mt6358_startup,
};

static int
mt8183_mt6358_ts3a227_max98357_bt_sco_startup(
	struct snd_pcm_substream *substream)
{
	static const unsigned int rates[] = {
		8000, 16000
	};
	static const struct snd_pcm_hw_constraint_list constraints_rates = {
		.count = ARRAY_SIZE(rates),
		.list  = rates,
		.mask = 0,
	};
	static const unsigned int channels[] = {
		1,
	};
	static const struct snd_pcm_hw_constraint_list constraints_channels = {
		.count = ARRAY_SIZE(channels),
		.list = channels,
		.mask = 0,
	};

	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_hw_constraint_list(runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);
	runtime->hw.channels_max = 1;
	snd_pcm_hw_constraint_list(runtime, 0,
			SNDRV_PCM_HW_PARAM_CHANNELS,
			&constraints_channels);

	runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
	snd_pcm_hw_constraint_msbits(runtime, 0, 16, 16);

	return 0;
}

static const struct snd_soc_ops mt8183_mt6358_ts3a227_max98357_bt_sco_ops = {
	.startup = mt8183_mt6358_ts3a227_max98357_bt_sco_startup,
};

/* FE */
SND_SOC_DAILINK_DEFS(playback1,
	DAILINK_COMP_ARRAY(COMP_CPU("DL1")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback2,
	DAILINK_COMP_ARRAY(COMP_CPU("DL2")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback3,
	DAILINK_COMP_ARRAY(COMP_CPU("DL3")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture1,
	DAILINK_COMP_ARRAY(COMP_CPU("UL1")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture2,
	DAILINK_COMP_ARRAY(COMP_CPU("UL2")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture3,
	DAILINK_COMP_ARRAY(COMP_CPU("UL3")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture_mono,
	DAILINK_COMP_ARRAY(COMP_CPU("UL_MONO_1")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback_hdmi,
	DAILINK_COMP_ARRAY(COMP_CPU("HDMI")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(wake_on_voice,
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* BE */
SND_SOC_DAILINK_DEFS(primary_codec,
	DAILINK_COMP_ARRAY(COMP_CPU("ADDA")),
	DAILINK_COMP_ARRAY(COMP_CODEC("mt6358-sound", "mt6358-snd-codec-aif1")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(pcm1,
	DAILINK_COMP_ARRAY(COMP_CPU("PCM 1")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(pcm2,
	DAILINK_COMP_ARRAY(COMP_CPU("PCM 2")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(i2s0,
	DAILINK_COMP_ARRAY(COMP_CPU("I2S0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("bt-sco", "bt-sco-pcm")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(i2s1,
	DAILINK_COMP_ARRAY(COMP_CPU("I2S1")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(i2s2,
	DAILINK_COMP_ARRAY(COMP_CPU("I2S2")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(i2s3_max98357a,
	DAILINK_COMP_ARRAY(COMP_CPU("I2S3")),
	DAILINK_COMP_ARRAY(COMP_CODEC("max98357a", "HiFi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(i2s3_rt1015,
	DAILINK_COMP_ARRAY(COMP_CPU("I2S3")),
	DAILINK_COMP_ARRAY(COMP_CODEC(RT1015_DEV0_NAME, RT1015_CODEC_DAI),
			   COMP_CODEC(RT1015_DEV1_NAME, RT1015_CODEC_DAI)),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(i2s3_rt1015p,
	DAILINK_COMP_ARRAY(COMP_CPU("I2S3")),
	DAILINK_COMP_ARRAY(COMP_CODEC("rt1015p", "HiFi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(i2s5,
	DAILINK_COMP_ARRAY(COMP_CPU("I2S5")),
	DAILINK_COMP_ARRAY(COMP_CODEC("bt-sco", "bt-sco-pcm")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(tdm,
	DAILINK_COMP_ARRAY(COMP_CPU("TDM")),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "i2s-hifi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static int mt8183_mt6358_tdm_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct mt8183_mt6358_ts3a227_max98357_priv *priv =
		snd_soc_card_get_drvdata(rtd->card);
	int ret;

	if (IS_ERR(priv->pin_states[PIN_TDM_OUT_ON]))
		return PTR_ERR(priv->pin_states[PIN_TDM_OUT_ON]);

	ret = pinctrl_select_state(priv->pinctrl,
				   priv->pin_states[PIN_TDM_OUT_ON]);
	if (ret)
		dev_err(rtd->card->dev, "%s failed to select state %d\n",
			__func__, ret);

	return ret;
}

static void mt8183_mt6358_tdm_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct mt8183_mt6358_ts3a227_max98357_priv *priv =
		snd_soc_card_get_drvdata(rtd->card);
	int ret;

	if (IS_ERR(priv->pin_states[PIN_TDM_OUT_OFF]))
		return;

	ret = pinctrl_select_state(priv->pinctrl,
				   priv->pin_states[PIN_TDM_OUT_OFF]);
	if (ret)
		dev_err(rtd->card->dev, "%s failed to select state %d\n",
			__func__, ret);
}

static const struct snd_soc_ops mt8183_mt6358_tdm_ops = {
	.startup = mt8183_mt6358_tdm_startup,
	.shutdown = mt8183_mt6358_tdm_shutdown,
};

static int
mt8183_mt6358_ts3a227_max98357_wov_startup(
	struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct mt8183_mt6358_ts3a227_max98357_priv *priv =
			snd_soc_card_get_drvdata(card);

	return pinctrl_select_state(priv->pinctrl,
				    priv->pin_states[PIN_WOV]);
}

static void
mt8183_mt6358_ts3a227_max98357_wov_shutdown(
	struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct mt8183_mt6358_ts3a227_max98357_priv *priv =
			snd_soc_card_get_drvdata(card);
	int ret;

	ret = pinctrl_select_state(priv->pinctrl,
				   priv->pin_states[PIN_STATE_DEFAULT]);
	if (ret)
		dev_err(card->dev, "%s failed to select state %d\n",
			__func__, ret);
}

static const struct snd_soc_ops mt8183_mt6358_ts3a227_max98357_wov_ops = {
	.startup = mt8183_mt6358_ts3a227_max98357_wov_startup,
	.shutdown = mt8183_mt6358_ts3a227_max98357_wov_shutdown,
};

static int
mt8183_mt6358_ts3a227_max98357_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mt8183_mt6358_ts3a227_max98357_priv *priv =
		snd_soc_card_get_drvdata(rtd->card);
	int ret;

	ret = snd_soc_card_jack_new(rtd->card, "HDMI Jack", SND_JACK_LINEOUT,
				    &priv->hdmi_jack, NULL, 0);
	if (ret)
		return ret;

	return snd_soc_component_set_jack(asoc_rtd_to_codec(rtd, 0)->component,
					  &priv->hdmi_jack, NULL);
}

static struct snd_soc_dai_link mt8183_mt6358_ts3a227_dai_links[] = {
	/* FE */
	{
		.name = "Playback_1",
		.stream_name = "Playback_1",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &mt8183_mt6358_ops,
		SND_SOC_DAILINK_REG(playback1),
	},
	{
		.name = "Playback_2",
		.stream_name = "Playback_2",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &mt8183_mt6358_ts3a227_max98357_bt_sco_ops,
		SND_SOC_DAILINK_REG(playback2),
	},
	{
		.name = "Playback_3",
		.stream_name = "Playback_3",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(playback3),
	},
	{
		.name = "Capture_1",
		.stream_name = "Capture_1",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_capture = 1,
		.ops = &mt8183_mt6358_ts3a227_max98357_bt_sco_ops,
		SND_SOC_DAILINK_REG(capture1),
	},
	{
		.name = "Capture_2",
		.stream_name = "Capture_2",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(capture2),
	},
	{
		.name = "Capture_3",
		.stream_name = "Capture_3",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_capture = 1,
		.ops = &mt8183_mt6358_ops,
		SND_SOC_DAILINK_REG(capture3),
	},
	{
		.name = "Capture_Mono_1",
		.stream_name = "Capture_Mono_1",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(capture_mono),
	},
	{
		.name = "Playback_HDMI",
		.stream_name = "Playback_HDMI",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(playback_hdmi),
	},
	{
		.name = "Wake on Voice",
		.stream_name = "Wake on Voice",
		.ignore_suspend = 1,
		.ignore = 1,
		SND_SOC_DAILINK_REG(wake_on_voice),
		.ops = &mt8183_mt6358_ts3a227_max98357_wov_ops,
	},

	/* BE */
	{
		.name = "Primary Codec",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(primary_codec),
	},
	{
		.name = "PCM 1",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(pcm1),
	},
	{
		.name = "PCM 2",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(pcm2),
	},
	{
		.name = "I2S0",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8183_i2s_hw_params_fixup,
		.ops = &mt8183_mt6358_i2s_ops,
		SND_SOC_DAILINK_REG(i2s0),
	},
	{
		.name = "I2S1",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8183_i2s_hw_params_fixup,
		.ops = &mt8183_mt6358_i2s_ops,
		SND_SOC_DAILINK_REG(i2s1),
	},
	{
		.name = "I2S2",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8183_i2s_hw_params_fixup,
		.ops = &mt8183_mt6358_i2s_ops,
		SND_SOC_DAILINK_REG(i2s2),
	},
	{
		.name = "I2S3",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "I2S5",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8183_i2s_hw_params_fixup,
		.ops = &mt8183_mt6358_i2s_ops,
		SND_SOC_DAILINK_REG(i2s5),
	},
	{
		.name = "TDM",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			   SND_SOC_DAIFMT_IB_IF |
			   SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_playback = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8183_i2s_hw_params_fixup,
		.ops = &mt8183_mt6358_tdm_ops,
		.ignore = 1,
		.init = mt8183_mt6358_ts3a227_max98357_hdmi_init,
		SND_SOC_DAILINK_REG(tdm),
	},
};

static struct snd_soc_card mt8183_mt6358_ts3a227_max98357_card = {
	.name = "mt8183_mt6358_ts3a227_max98357",
	.owner = THIS_MODULE,
	.dai_link = mt8183_mt6358_ts3a227_dai_links,
	.num_links = ARRAY_SIZE(mt8183_mt6358_ts3a227_dai_links),
};

static struct snd_soc_card mt8183_mt6358_ts3a227_max98357b_card = {
	.name = "mt8183_mt6358_ts3a227_max98357b",
	.owner = THIS_MODULE,
	.dai_link = mt8183_mt6358_ts3a227_dai_links,
	.num_links = ARRAY_SIZE(mt8183_mt6358_ts3a227_dai_links),
};

static struct snd_soc_codec_conf mt8183_mt6358_ts3a227_rt1015_amp_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(RT1015_DEV0_NAME),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF(RT1015_DEV1_NAME),
		.name_prefix = "Right",
	},
};

static struct snd_soc_card mt8183_mt6358_ts3a227_rt1015_card = {
	.name = "mt8183_mt6358_ts3a227_rt1015",
	.owner = THIS_MODULE,
	.dai_link = mt8183_mt6358_ts3a227_dai_links,
	.num_links = ARRAY_SIZE(mt8183_mt6358_ts3a227_dai_links),
	.codec_conf = mt8183_mt6358_ts3a227_rt1015_amp_conf,
	.num_configs = ARRAY_SIZE(mt8183_mt6358_ts3a227_rt1015_amp_conf),
};

static struct snd_soc_card mt8183_mt6358_ts3a227_rt1015p_card = {
	.name = "mt8183_mt6358_ts3a227_rt1015p",
	.owner = THIS_MODULE,
	.dai_link = mt8183_mt6358_ts3a227_dai_links,
	.num_links = ARRAY_SIZE(mt8183_mt6358_ts3a227_dai_links),
};

static int
mt8183_mt6358_ts3a227_max98357_headset_init(struct snd_soc_component *component)
{
	int ret;
	struct mt8183_mt6358_ts3a227_max98357_priv *priv =
			snd_soc_card_get_drvdata(component->card);

	/* Enable Headset and 4 Buttons Jack detection */
	ret = snd_soc_card_jack_new(component->card,
				    "Headset Jack",
				    SND_JACK_HEADSET |
				    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				    SND_JACK_BTN_2 | SND_JACK_BTN_3,
				    &priv->headset_jack,
				    NULL, 0);
	if (ret)
		return ret;

	ret = ts3a227e_enable_jack_detect(component, &priv->headset_jack);

	return ret;
}

static struct snd_soc_aux_dev mt8183_mt6358_ts3a227_max98357_headset_dev = {
	.dlc = COMP_EMPTY(),
	.init = mt8183_mt6358_ts3a227_max98357_headset_init,
};

static int
mt8183_mt6358_ts3a227_max98357_dev_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct device_node *platform_node, *ec_codec, *hdmi_codec;
	struct snd_soc_dai_link *dai_link;
	struct mt8183_mt6358_ts3a227_max98357_priv *priv;
	int ret, i;

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}

	card = (struct snd_soc_card *)of_device_get_match_data(&pdev->dev);
	if (!card)
		return -EINVAL;
	card->dev = &pdev->dev;

	ec_codec = of_parse_phandle(pdev->dev.of_node, "mediatek,ec-codec", 0);
	hdmi_codec = of_parse_phandle(pdev->dev.of_node,
				      "mediatek,hdmi-codec", 0);

	for_each_card_prelinks(card, i, dai_link) {
		if (ec_codec && strcmp(dai_link->name, "Wake on Voice") == 0) {
			dai_link->cpus[0].name = NULL;
			dai_link->cpus[0].of_node = ec_codec;
			dai_link->cpus[0].dai_name = NULL;
			dai_link->codecs[0].name = NULL;
			dai_link->codecs[0].of_node = ec_codec;
			dai_link->codecs[0].dai_name = "Wake on Voice";
			dai_link->platforms[0].of_node = ec_codec;
			dai_link->ignore = 0;
		}

		if (strcmp(dai_link->name, "I2S3") == 0) {
			if (card == &mt8183_mt6358_ts3a227_max98357_card ||
			    card == &mt8183_mt6358_ts3a227_max98357b_card) {
				dai_link->be_hw_params_fixup =
					mt8183_i2s_hw_params_fixup;
				dai_link->ops = &mt8183_mt6358_i2s_ops;
				dai_link->cpus = i2s3_max98357a_cpus;
				dai_link->num_cpus =
					ARRAY_SIZE(i2s3_max98357a_cpus);
				dai_link->codecs = i2s3_max98357a_codecs;
				dai_link->num_codecs =
					ARRAY_SIZE(i2s3_max98357a_codecs);
				dai_link->platforms = i2s3_max98357a_platforms;
				dai_link->num_platforms =
					ARRAY_SIZE(i2s3_max98357a_platforms);
			} else if (card == &mt8183_mt6358_ts3a227_rt1015_card) {
				dai_link->be_hw_params_fixup =
					mt8183_rt1015_i2s_hw_params_fixup;
				dai_link->ops = &mt8183_mt6358_rt1015_i2s_ops;
				dai_link->cpus = i2s3_rt1015_cpus;
				dai_link->num_cpus =
					ARRAY_SIZE(i2s3_rt1015_cpus);
				dai_link->codecs = i2s3_rt1015_codecs;
				dai_link->num_codecs =
					ARRAY_SIZE(i2s3_rt1015_codecs);
				dai_link->platforms = i2s3_rt1015_platforms;
				dai_link->num_platforms =
					ARRAY_SIZE(i2s3_rt1015_platforms);
			} else if (card == &mt8183_mt6358_ts3a227_rt1015p_card) {
				dai_link->be_hw_params_fixup =
					mt8183_rt1015_i2s_hw_params_fixup;
				dai_link->ops = &mt8183_mt6358_i2s_ops;
				dai_link->cpus = i2s3_rt1015p_cpus;
				dai_link->num_cpus =
					ARRAY_SIZE(i2s3_rt1015p_cpus);
				dai_link->codecs = i2s3_rt1015p_codecs;
				dai_link->num_codecs =
					ARRAY_SIZE(i2s3_rt1015p_codecs);
				dai_link->platforms = i2s3_rt1015p_platforms;
				dai_link->num_platforms =
					ARRAY_SIZE(i2s3_rt1015p_platforms);
			}
		}

		if (card == &mt8183_mt6358_ts3a227_max98357b_card) {
			if (strcmp(dai_link->name, "I2S2") == 0 ||
			    strcmp(dai_link->name, "I2S3") == 0)
				dai_link->dai_fmt = SND_SOC_DAIFMT_LEFT_J |
						    SND_SOC_DAIFMT_NB_NF |
						    SND_SOC_DAIFMT_CBM_CFM;
		}

		if (hdmi_codec && strcmp(dai_link->name, "TDM") == 0) {
			dai_link->codecs->of_node = hdmi_codec;
			dai_link->ignore = 0;
		}

		if (!dai_link->platforms->name)
			dai_link->platforms->of_node = platform_node;
	}

	mt8183_mt6358_ts3a227_max98357_headset_dev.dlc.of_node =
		of_parse_phandle(pdev->dev.of_node,
				 "mediatek,headset-codec", 0);
	if (mt8183_mt6358_ts3a227_max98357_headset_dev.dlc.of_node) {
		card->aux_dev = &mt8183_mt6358_ts3a227_max98357_headset_dev;
		card->num_aux_devs = 1;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	snd_soc_card_set_drvdata(card, priv);

	priv->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(priv->pinctrl)) {
		dev_err(&pdev->dev, "%s devm_pinctrl_get failed\n",
			__func__);
		return PTR_ERR(priv->pinctrl);
	}

	for (i = 0; i < PIN_STATE_MAX; i++) {
		priv->pin_states[i] = pinctrl_lookup_state(priv->pinctrl,
							   mt8183_pin_str[i]);
		if (IS_ERR(priv->pin_states[i])) {
			ret = PTR_ERR(priv->pin_states[i]);
			dev_info(&pdev->dev, "%s Can't find pin state %s %d\n",
				 __func__, mt8183_pin_str[i], ret);
		}
	}

	if (!IS_ERR(priv->pin_states[PIN_TDM_OUT_OFF])) {
		ret = pinctrl_select_state(priv->pinctrl,
					   priv->pin_states[PIN_TDM_OUT_OFF]);
		if (ret)
			dev_info(&pdev->dev,
				 "%s failed to select state %d\n",
				 __func__, ret);
	}

	if (!IS_ERR(priv->pin_states[PIN_STATE_DEFAULT])) {
		ret = pinctrl_select_state(priv->pinctrl,
					   priv->pin_states[PIN_STATE_DEFAULT]);
		if (ret)
			dev_info(&pdev->dev,
				 "%s failed to select state %d\n",
				 __func__, ret);
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);

	of_node_put(platform_node);
	of_node_put(ec_codec);
	of_node_put(hdmi_codec);
	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id mt8183_mt6358_ts3a227_max98357_dt_match[] = {
	{
		.compatible = "mediatek,mt8183_mt6358_ts3a227_max98357",
		.data = &mt8183_mt6358_ts3a227_max98357_card,
	},
	{
		.compatible = "mediatek,mt8183_mt6358_ts3a227_max98357b",
		.data = &mt8183_mt6358_ts3a227_max98357b_card,
	},
	{
		.compatible = "mediatek,mt8183_mt6358_ts3a227_rt1015",
		.data = &mt8183_mt6358_ts3a227_rt1015_card,
	},
	{
		.compatible = "mediatek,mt8183_mt6358_ts3a227_rt1015p",
		.data = &mt8183_mt6358_ts3a227_rt1015p_card,
	},
	{}
};
#endif

static struct platform_driver mt8183_mt6358_ts3a227_max98357_driver = {
	.driver = {
		.name = "mt8183_mt6358_ts3a227",
#ifdef CONFIG_OF
		.of_match_table = mt8183_mt6358_ts3a227_max98357_dt_match,
#endif
		.pm = &snd_soc_pm_ops,
	},
	.probe = mt8183_mt6358_ts3a227_max98357_dev_probe,
};

module_platform_driver(mt8183_mt6358_ts3a227_max98357_driver);

/* Module information */
MODULE_DESCRIPTION("MT8183-MT6358-TS3A227-MAX98357 ALSA SoC machine driver");
MODULE_AUTHOR("Shunli Wang <shunli.wang@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("mt8183_mt6358_ts3a227_max98357 soc card");
