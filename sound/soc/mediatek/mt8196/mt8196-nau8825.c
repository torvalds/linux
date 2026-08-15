// SPDX-License-Identifier: GPL-2.0
/*
 *  mt8196-nau8825.c  --  mt8196 nau8825 ALSA SoC machine driver
 *
 *  Copyright (c) 2025 MediaTek Inc.
 *  Author: Darren Ye <darren.ye@mediatek.com>
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>

#include "mt8196-afe-common.h"

#include "../../codecs/nau8825.h"
#include "../../codecs/rt5682s.h"

#include "../common/mtk-soc-card.h"
#include "../common/mtk-dsp-sof-common.h"
#include "../common/mtk-soundcard-driver.h"
#include "../common/mtk-afe-platform-driver.h"

#define NAU8825_HS_PRESENT	BIT(0)
#define RT5682S_HS_PRESENT	BIT(1)
#define RT5650_HS_PRESENT	BIT(2)

/*
 * Nau88l25
 */
#define NAU8825_CODEC_DAI  "nau8825-hifi"

/*
 * Rt5682s
 */
#define RT5682S_CODEC_DAI     "rt5682s-aif1"

/*
 * Rt5650
 */
#define RT5650_CODEC_DAI     "rt5645-aif1"

#define SOF_DMA_DL1 "SOF_DMA_DL1"
#define SOF_DMA_DL_24CH "SOF_DMA_DL_24CH"
#define SOF_DMA_UL0 "SOF_DMA_UL0"
#define SOF_DMA_UL1 "SOF_DMA_UL1"
#define SOF_DMA_UL2 "SOF_DMA_UL2"

enum mt8196_jacks {
	MT8196_JACK_HEADSET,
	MT8196_JACK_DP,
	MT8196_JACK_HDMI,
	MT8196_JACK_MAX,
};

static struct snd_soc_jack_pin mt8196_dp_jack_pins[] = {
	{
		.pin = "DP",
		.mask = SND_JACK_AVOUT,
	},
};

static struct snd_soc_jack_pin mt8196_hdmi_jack_pins[] = {
	{
		.pin = "HDMI",
		.mask = SND_JACK_AVOUT,
	},
};

static struct snd_soc_jack_pin nau8825_jack_pins[] = {
	{
		.pin    = "Headphone Jack",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "Headset Mic",
		.mask   = SND_JACK_MICROPHONE,
	},
};

static const struct snd_kcontrol_new mt8196_dumb_spk_controls[] = {
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static const struct snd_soc_dapm_widget mt8196_dumb_spk_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_widget mt8196_nau8825_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_SINK("DP"),
};

static const struct snd_kcontrol_new mt8196_nau8825_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

#define EXT_SPK_AMP_W_NAME "Ext_Speaker_Amp"

static struct snd_soc_card mt8196_nau8825_soc_card;

static const struct snd_soc_dapm_widget mt8196_nau8825_card_widgets[] = {
	/* SOF Uplink */
	SND_SOC_DAPM_MIXER("SOF_DMA_UL0", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SOF_DMA_UL1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SOF_DMA_UL2", SND_SOC_NOPM, 0, 0, NULL, 0),

	/*
	 * SOF Downlink
	 * the widgets on the machine driver cannot use the parameter with kcontrol
	 * because the widget domain is its platform driver. so sof downlink route
	 * is written in the i2s dai driver.
	 */
};

static const struct snd_soc_dapm_route mt8196_nau8825_card_routes[] = {
	/* SOF Uplink */
	{"SOF_DMA_UL0", NULL, "UL0_CH1"},
	{"SOF_DMA_UL0", NULL, "UL0_CH2"},
	/* SOF Uplink */
	{"SOF_DMA_UL1", NULL, "UL1_CH1"},
	{"SOF_DMA_UL1", NULL, "UL1_CH2"},
	/* SOF Uplink */
	{"SOF_DMA_UL2", NULL, "UL2_CH1"},
	{"SOF_DMA_UL2", NULL, "UL2_CH2"},
};

static const struct snd_kcontrol_new mt8196_nau8825_card_controls[] = {
	SOC_DAPM_PIN_SWITCH(EXT_SPK_AMP_W_NAME),
};

/*
 * define mtk_spk_i2s_mck node in dts when need mclk,
 * BE i2s need assign snd_soc_ops = mt8196_nau8825_i2s_ops
 */
static int mt8196_nau8825_i2s_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs_ratio = 128;
	unsigned int mclk_fs = rate * mclk_fs_ratio;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);

	return snd_soc_dai_set_sysclk(cpu_dai,
				      0, mclk_fs, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8196_nau8825_i2s_ops = {
	.hw_params = mt8196_nau8825_i2s_hw_params,
};

static int mt8196_dptx_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs_ratio = 256;
	unsigned int mclk_fs = rate * mclk_fs_ratio;
	struct snd_soc_dai *dai = snd_soc_rtd_to_cpu(rtd, 0);

	return snd_soc_dai_set_sysclk(dai, 0, mclk_fs, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8196_dptx_ops = {
	.hw_params = mt8196_dptx_hw_params,
};

static int mt8196_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params)
{
	dev_info(rtd->dev, "fix format to 32bit\n");

	/* fix BE i2s format to 32bit, clean param mask first */
	snd_mask_reset_range(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
			     0, (__force unsigned int)SNDRV_PCM_FORMAT_LAST);

	params_set_format(params, SNDRV_PCM_FORMAT_S32_LE);
	return 0;
}

static int mt8196_sof_be_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *cmpnt_afe = NULL;
	struct snd_soc_pcm_runtime *runtime;

	/* find afe component */
	for_each_card_rtds(rtd->card, runtime) {
		cmpnt_afe = snd_soc_rtdcom_lookup(runtime, AFE_PCM_NAME);
		if (cmpnt_afe) {
			dev_info(rtd->dev, "component->name: %s\n", cmpnt_afe->name);
			break;
		}
	}

	if (cmpnt_afe && !pm_runtime_active(cmpnt_afe->dev)) {
		dev_err(rtd->dev, "afe pm runtime is not active!!\n");
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_ops mt8196_sof_be_ops = {
	.hw_params = mt8196_sof_be_hw_params,
};

static const struct sof_conn_stream g_sof_conn_streams[] = {
	{
		.sof_link = "AFE_SOF_DL1",
		.sof_dma = SOF_DMA_DL1,
		.stream_dir = SNDRV_PCM_STREAM_PLAYBACK
	},
	{
		.sof_link = "AFE_SOF_DL_24CH",
		.sof_dma = SOF_DMA_DL_24CH,
		.stream_dir = SNDRV_PCM_STREAM_PLAYBACK
	},
	{
		.sof_link = "AFE_SOF_UL0",
		.sof_dma = SOF_DMA_UL0,
		.stream_dir = SNDRV_PCM_STREAM_CAPTURE
	},
	{
		.sof_link = "AFE_SOF_UL1",
		.sof_dma = SOF_DMA_UL1,
		.stream_dir = SNDRV_PCM_STREAM_CAPTURE
	},
	{
		.sof_link = "AFE_SOF_UL2",
		.sof_dma = SOF_DMA_UL2,
		.stream_dir = SNDRV_PCM_STREAM_CAPTURE
	},
};

/* FE */
SND_SOC_DAILINK_DEFS(playback1,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(playback_24ch,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL_24CH")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(capture0,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL0")),
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
SND_SOC_DAILINK_DEFS(playback_hdmi,
		     DAILINK_COMP_ARRAY(COMP_CPU("HDMI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(playback2,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(capture_cm0,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL_CM0")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
/* BE */
SND_SOC_DAILINK_DEFS(ap_dmic,
		     DAILINK_COMP_ARRAY(COMP_CPU("AP_DMIC")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(ap_dmic_ch34,
		     DAILINK_COMP_ARRAY(COMP_CPU("AP_DMIC_CH34")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(ap_dmic_multich,
		     DAILINK_COMP_ARRAY(COMP_CPU("AP_DMIC_MULTICH")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2sin6,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2SIN6")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2sout3,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2SOUT3")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2sout4,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2SOUT4")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2sout6,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2SOUT6")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(tdm_dptx,
		     DAILINK_COMP_ARRAY(COMP_CPU("TDM_DPTX")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(AFE_SOF_DL_24CH,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_DL_24CH")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(AFE_SOF_DL1,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_DL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(AFE_SOF_UL0,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_UL0")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(AFE_SOF_UL1,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_UL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(AFE_SOF_UL2,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_UL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link mt8196_nau8825_dai_links[] = {
	/*
	 * The SOF topology expects PCM streams 0~4 to be available
	 * for the SOF PCM streams. Put the SOF BE definitions here
	 * so that the PCM device numbers are skipped over.
	 * (BE dailinks do not have PCM devices created.)
	 */
	{
		.name = "AFE_SOF_DL_24CH",
		.no_pcm = 1,
		.playback_only = 1,
		.ops = &mt8196_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_DL_24CH),
	},
	{
		.name = "AFE_SOF_DL1",
		.no_pcm = 1,
		.playback_only = 1,
		.ops = &mt8196_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_DL1),
	},
	{
		.name = "AFE_SOF_UL0",
		.no_pcm = 1,
		.capture_only = 1,
		.ops = &mt8196_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_UL0),
	},
	{
		.name = "AFE_SOF_UL1",
		.no_pcm = 1,
		.capture_only = 1,
		.ops = &mt8196_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_UL1),
	},
	{
		.name = "AFE_SOF_UL2",
		.no_pcm = 1,
		.capture_only = 1,
		.ops = &mt8196_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_UL2),
	},
	/* Front End DAI links */
	{
		.name = "HDMI_FE",
		.stream_name = "HDMI Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback_hdmi),
	},
	{
		.name = "DL2_FE",
		.stream_name = "DL2 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback2),
	},
	{
		.name = "UL_CM0_FE",
		.stream_name = "UL_CM0 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture_cm0),
	},
	{
		.name = "DL_24CH_FE",
		.stream_name = "DL_24CH Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
				SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback_24ch),
	},
	{
		.name = "DL1_FE",
		.stream_name = "DL1 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
				SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback1),
	},
	{
		.name = "UL0_FE",
		.stream_name = "UL0 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
				SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture0),
	},
	{
		.name = "UL1_FE",
		.stream_name = "UL1 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
				SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture1),
	},
	{
		.name = "UL2_FE",
		.stream_name = "UL2 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
				SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture2),
	},
	/* Back End DAI links */
	{
		.name = "I2SIN6_BE",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC
			| SND_SOC_DAIFMT_GATED,
		.ops = &mt8196_nau8825_i2s_ops,
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8196_hw_params_fixup,
		SND_SOC_DAILINK_REG(i2sin6),
	},
	{
		.name = "I2SOUT4_BE",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC
			| SND_SOC_DAIFMT_GATED,
		.ops = &mt8196_nau8825_i2s_ops,
		.no_pcm = 1,
		.playback_only = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = mt8196_hw_params_fixup,
		SND_SOC_DAILINK_REG(i2sout4),
	},
	{
		.name = "I2SOUT6_BE",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC
			| SND_SOC_DAIFMT_GATED,
		.ops = &mt8196_nau8825_i2s_ops,
		.no_pcm = 1,
		.playback_only = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8196_hw_params_fixup,
		SND_SOC_DAILINK_REG(i2sout6),
	},
	{
		.name = "AP_DMIC_BE",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(ap_dmic),
	},
	{
		.name = "AP_DMIC_CH34_BE",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(ap_dmic_ch34),
	},
	{
		.name = "AP_DMIC_MULTICH_BE",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(ap_dmic_multich),
	},
	{
		.name = "TDM_DPTX_BE",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC
			| SND_SOC_DAIFMT_GATED,
		.ops = &mt8196_dptx_ops,
		.be_hw_params_fixup = mt8196_hw_params_fixup,
		.no_pcm = 1,
		.playback_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(tdm_dptx),
	},
	{
		.name = "I2SOUT3_BE",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC
			| SND_SOC_DAIFMT_GATED,
		.ops = &mt8196_nau8825_i2s_ops,
		.no_pcm = 1,
		.playback_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(i2sout3),
	},
};

static int mt8196_dumb_amp_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dapm_context *dapm = snd_soc_card_to_dapm(card);
	int ret = 0;

	ret = snd_soc_dapm_new_controls(dapm, mt8196_dumb_spk_widgets,
					ARRAY_SIZE(mt8196_dumb_spk_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add Dumb Speaker dapm, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, mt8196_dumb_spk_controls,
					ARRAY_SIZE(mt8196_dumb_spk_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add Dumb card controls, ret %d\n", ret);
		return ret;
	}

	return 0;
}

static int mt8196_dptx_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_jack *jack = &soc_card_data->card_data->jacks[MT8196_JACK_DP];
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	int ret = 0;

	ret = snd_soc_card_jack_new_pins(rtd->card, "DP Jack", SND_JACK_AVOUT,
					 jack, mt8196_dp_jack_pins,
					 ARRAY_SIZE(mt8196_dp_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "new jack failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "set jack failed on %s (ret=%d)\n",
			component->name, ret);
		return ret;
	}

	return 0;
}

static int mt8196_hdmi_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_jack *jack = &soc_card_data->card_data->jacks[MT8196_JACK_HDMI];
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	int ret = 0;

	ret = snd_soc_card_jack_new_pins(rtd->card, "HDMI Jack", SND_JACK_AVOUT,
					 jack, mt8196_hdmi_jack_pins,
					 ARRAY_SIZE(mt8196_hdmi_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "new jack failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "set jack failed on %s (ret=%d)\n",
			component->name, ret);
		return ret;
	}

	return 0;
}

static int mt8196_headset_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dapm_context *dapm = snd_soc_card_to_dapm(card);
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(card);
	struct snd_soc_jack *jack = &soc_card_data->card_data->jacks[MT8196_JACK_HEADSET];
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	int ret;
	int type;

	ret = snd_soc_dapm_new_controls(dapm, mt8196_nau8825_widgets,
					ARRAY_SIZE(mt8196_nau8825_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add nau8825 card widget, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, mt8196_nau8825_controls,
					ARRAY_SIZE(mt8196_nau8825_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add nau8825 card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0 |
					 SND_JACK_BTN_1 | SND_JACK_BTN_2 |
					 SND_JACK_BTN_3,
					 jack,
					 nau8825_jack_pins,
					 ARRAY_SIZE(nau8825_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	type = SND_JACK_HEADSET | SND_JACK_BTN_0 | SND_JACK_BTN_1 | SND_JACK_BTN_2 | SND_JACK_BTN_3;
	ret = snd_soc_component_set_jack(component, jack, (void *)&type);

	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return 0;
};

static void mt8196_headset_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int mt8196_nau8825_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rate = params_rate(params);
	unsigned int bit_width = params_width(params);
	int clk_freq, ret;

	clk_freq = rate * 2 * bit_width;

	/* Configure clock for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, NAU8825_CLK_FLL_BLK, 0,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set BCLK clock %d\n", ret);
		return ret;
	}

	/* Configure pll for codec */
	ret = snd_soc_dai_set_pll(codec_dai, 0, 0, clk_freq,
				  params_rate(params) * 256);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set BCLK: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops mt8196_nau8825_ops = {
	.hw_params = mt8196_nau8825_hw_params,
};

static int mt8196_rt5682s_i2s_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rate = params_rate(params);
	int bitwidth;
	int ret;

	bitwidth = snd_pcm_format_width(params_format(params));
	if (bitwidth < 0) {
		dev_err(card->dev, "invalid bit width: %d\n", bitwidth);
		return bitwidth;
	}

	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x00, 0x0, 0x2, bitwidth);
	if (ret) {
		dev_err(card->dev, "failed to set tdm slot\n");
		return ret;
	}

	ret = snd_soc_dai_set_pll(codec_dai, RT5682S_PLL1, RT5682S_PLL_S_BCLK1,
				  rate * 32, rate * 512);
	if (ret) {
		dev_err(card->dev, "failed to set pll\n");
		return ret;
	}

	dev_info(card->dev, "%s set mclk rate: %d\n", __func__, rate * 512);

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5682S_SCLK_S_MCLK,
				     rate * 512, SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(card->dev, "failed to set sysclk\n");
		return ret;
	}

	return snd_soc_dai_set_sysclk(cpu_dai, 0, rate * 512,
				      SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8196_rt5682s_i2s_ops = {
	.hw_params = mt8196_rt5682s_i2s_hw_params,
};

static int mt8196_nau8825_soc_card_probe(struct mtk_soc_card_data *soc_card_data, bool legacy)
{
	struct snd_soc_card *card = soc_card_data->card_data->card;
	struct snd_soc_dai_link *dai_link;
	bool init_nau8825 = false;
	bool init_rt5682s = false;
	bool init_rt5650 = false;
	bool init_dumb = false;
	int i;

	dev_info(card->dev, "legacy: %d\n", legacy);

	for_each_card_prelinks(card, i, dai_link) {
		if (strcmp(dai_link->name, "TDM_DPTX_BE") == 0) {
			if (dai_link->num_codecs &&
			    strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai"))
				dai_link->init = mt8196_dptx_codec_init;
		} else if (strcmp(dai_link->name, "I2SOUT3_BE") == 0) {
			if (dai_link->num_codecs &&
			    strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai"))
				dai_link->init = mt8196_hdmi_codec_init;
		} else if (strcmp(dai_link->name, "I2SOUT6_BE") == 0 ||
			   strcmp(dai_link->name, "I2SIN6_BE") == 0) {
			if (!strcmp(dai_link->codecs->dai_name, NAU8825_CODEC_DAI)) {
				dai_link->ops = &mt8196_nau8825_ops;
				if (!init_nau8825) {
					dai_link->init = mt8196_headset_codec_init;
					dai_link->exit = mt8196_headset_codec_exit;
					init_nau8825 = true;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, RT5682S_CODEC_DAI)) {
				dai_link->ops = &mt8196_rt5682s_i2s_ops;
				if (!init_rt5682s) {
					dai_link->init = mt8196_headset_codec_init;
					dai_link->exit = mt8196_headset_codec_exit;
					init_rt5682s = true;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, RT5650_CODEC_DAI)) {
				dai_link->ops = &mt8196_rt5682s_i2s_ops;
				if (!init_rt5650) {
					dai_link->init = mt8196_headset_codec_init;
					dai_link->exit = mt8196_headset_codec_exit;
					init_rt5650 = true;
				}
			} else {
				if (strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai")) {
					if (!init_dumb) {
						dai_link->init = mt8196_dumb_amp_init;
						init_dumb = true;
					}
				}
			}
		}
	}

	return 0;
}

static const struct mtk_sof_priv mt8196_sof_priv = {
	.conn_streams = g_sof_conn_streams,
	.num_streams = ARRAY_SIZE(g_sof_conn_streams),
};

static struct snd_soc_card mt8196_nau8825_soc_card = {
	.owner = THIS_MODULE,
	.dai_link = mt8196_nau8825_dai_links,
	.num_links = ARRAY_SIZE(mt8196_nau8825_dai_links),
	.dapm_widgets = mt8196_nau8825_card_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8196_nau8825_card_widgets),
	.dapm_routes = mt8196_nau8825_card_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8196_nau8825_card_routes),
	.controls = mt8196_nau8825_card_controls,
	.num_controls = ARRAY_SIZE(mt8196_nau8825_card_controls),
};

static const struct mtk_soundcard_pdata mt8196_nau8825_card = {
	.card_name = "mt8196_nau8825",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8196_nau8825_soc_card,
		.num_jacks = MT8196_JACK_MAX,
		.flags = NAU8825_HS_PRESENT
	},
	.sof_priv = &mt8196_sof_priv,
	.soc_probe = mt8196_nau8825_soc_card_probe,
};

static const struct mtk_soundcard_pdata mt8196_rt5682s_card = {
	.card_name = "mt8196_rt5682s",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8196_nau8825_soc_card,
		.num_jacks = MT8196_JACK_MAX,
		.flags = RT5682S_HS_PRESENT
	},
	.sof_priv = &mt8196_sof_priv,
	.soc_probe = mt8196_nau8825_soc_card_probe,
};

static const struct mtk_soundcard_pdata mt8196_rt5650_card = {
	.card_name = "mt8196_rt5650",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8196_nau8825_soc_card,
		.num_jacks = MT8196_JACK_MAX,
		.flags = RT5650_HS_PRESENT
	},
	.sof_priv = &mt8196_sof_priv,
	.soc_probe = mt8196_nau8825_soc_card_probe,
};

static const struct of_device_id mt8196_nau8825_dt_match[] = {
	{.compatible = "mediatek,mt8196-nau8825-sound", .data = &mt8196_nau8825_card,},
	{.compatible = "mediatek,mt8196-rt5682s-sound", .data = &mt8196_rt5682s_card,},
	{.compatible = "mediatek,mt8196-rt5650-sound", .data = &mt8196_rt5650_card,},
	{}
};
MODULE_DEVICE_TABLE(of, mt8196_nau8825_dt_match);

static struct platform_driver mt8196_nau8825_driver = {
	.driver = {
		.name = "mt8196-nau8825",
		.of_match_table = mt8196_nau8825_dt_match,
		.pm = &snd_soc_pm_ops,
	},
	.probe = mtk_soundcard_common_probe,
};
module_platform_driver(mt8196_nau8825_driver);

/* Module information */
MODULE_DESCRIPTION("MT8196 nau8825 ALSA SoC machine driver");
MODULE_AUTHOR("Darren Ye <darren.ye@mediatek.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("mt8196 nau8825 soc card");

