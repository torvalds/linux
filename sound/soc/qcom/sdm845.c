// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <linux/soundwire/sdw.h>
#include <uapi/linux/input-event-codes.h>
#include "common.h"
#include "qdsp6/q6afe.h"
#include "../codecs/rt5663.h"

#define DRIVER_NAME	"sdm845"
#define DEFAULT_SAMPLE_RATE_48K		48000
#define DEFAULT_MCLK_RATE		24576000
#define TDM_BCLK_RATE		6144000
#define MI2S_BCLK_RATE		1536000
#define LEFT_SPK_TDM_TX_MASK    0x30
#define RIGHT_SPK_TDM_TX_MASK   0xC0
#define SPK_TDM_RX_MASK         0x03
#define NUM_TDM_SLOTS           8
#define SLIM_MAX_TX_PORTS 16
#define SLIM_MAX_RX_PORTS 16
#define WCD934X_DEFAULT_MCLK_RATE	9600000

struct sdm845_snd_data {
	struct snd_soc_jack jack;
	bool jack_setup;
	bool stream_prepared[SLIM_MAX_RX_PORTS];
	struct snd_soc_card *card;
	uint32_t pri_mi2s_clk_count;
	uint32_t sec_mi2s_clk_count;
	uint32_t quat_tdm_clk_count;
	struct sdw_stream_runtime *sruntime[SLIM_MAX_RX_PORTS];
};

static unsigned int tdm_slot_offset[8] = {0, 4, 8, 12, 16, 20, 24, 28};

static int sdm845_slim_snd_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai;
	struct sdm845_snd_data *pdata = snd_soc_card_get_drvdata(rtd->card);
	u32 rx_ch[SLIM_MAX_RX_PORTS], tx_ch[SLIM_MAX_TX_PORTS];
	struct sdw_stream_runtime *sruntime;
	u32 rx_ch_cnt = 0, tx_ch_cnt = 0;
	int ret = 0, i;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		sruntime = snd_soc_dai_get_sdw_stream(codec_dai,
						      substream->stream);
		if (sruntime != ERR_PTR(-ENOTSUPP))
			pdata->sruntime[cpu_dai->id] = sruntime;

		ret = snd_soc_dai_get_channel_map(codec_dai,
				&tx_ch_cnt, tx_ch, &rx_ch_cnt, rx_ch);

		if (ret != 0 && ret != -ENOTSUPP) {
			pr_err("failed to get codec chan map, err:%d\n", ret);
			return ret;
		} else if (ret == -ENOTSUPP) {
			/* Ignore unsupported */
			continue;
		}

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			ret = snd_soc_dai_set_channel_map(cpu_dai, 0, NULL,
							  rx_ch_cnt, rx_ch);
		else
			ret = snd_soc_dai_set_channel_map(cpu_dai, tx_ch_cnt,
							  tx_ch, 0, NULL);
	}

	return 0;
}

static int sdm845_tdm_snd_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai;
	int ret = 0, j;
	int channels, slot_width;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		slot_width = 16;
		break;
	default:
		dev_err(rtd->dev, "%s: invalid param format 0x%x\n",
				__func__, params_format(params));
		return -EINVAL;
	}

	channels = params_channels(params);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0, 0x3,
				8, slot_width);
		if (ret < 0) {
			dev_err(rtd->dev, "%s: failed to set tdm slot, err:%d\n",
					__func__, ret);
			goto end;
		}

		ret = snd_soc_dai_set_channel_map(cpu_dai, 0, NULL,
				channels, tdm_slot_offset);
		if (ret < 0) {
			dev_err(rtd->dev, "%s: failed to set channel map, err:%d\n",
					__func__, ret);
			goto end;
		}
	} else {
		ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0xf, 0,
				8, slot_width);
		if (ret < 0) {
			dev_err(rtd->dev, "%s: failed to set tdm slot, err:%d\n",
					__func__, ret);
			goto end;
		}

		ret = snd_soc_dai_set_channel_map(cpu_dai, channels,
				tdm_slot_offset, 0, NULL);
		if (ret < 0) {
			dev_err(rtd->dev, "%s: failed to set channel map, err:%d\n",
					__func__, ret);
			goto end;
		}
	}

	for_each_rtd_codec_dais(rtd, j, codec_dai) {

		if (!strcmp(codec_dai->component->name_prefix, "Left")) {
			ret = snd_soc_dai_set_tdm_slot(
					codec_dai, LEFT_SPK_TDM_TX_MASK,
					SPK_TDM_RX_MASK, NUM_TDM_SLOTS,
					slot_width);
			if (ret < 0) {
				dev_err(rtd->dev,
					"DEV0 TDM slot err:%d\n", ret);
				return ret;
			}
		}

		if (!strcmp(codec_dai->component->name_prefix, "Right")) {
			ret = snd_soc_dai_set_tdm_slot(
					codec_dai, RIGHT_SPK_TDM_TX_MASK,
					SPK_TDM_RX_MASK, NUM_TDM_SLOTS,
					slot_width);
			if (ret < 0) {
				dev_err(rtd->dev,
					"DEV1 TDM slot err:%d\n", ret);
				return ret;
			}
		}
	}

end:
	return ret;
}

static int sdm845_snd_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int ret = 0;

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX:
	case PRIMARY_MI2S_TX:
		/*
		 * Use ASRC for internal clocks, as PLL rate isn't multiple
		 * of BCLK.
		 */
		rt5663_sel_asrc_clk_src(
			codec_dai->component,
			RT5663_DA_STEREO_FILTER | RT5663_AD_STEREO_FILTER,
			RT5663_CLK_SEL_I2S1_ASRC);
		ret = snd_soc_dai_set_sysclk(
			codec_dai, RT5663_SCLK_S_MCLK, DEFAULT_MCLK_RATE,
			SND_SOC_CLOCK_IN);
		if (ret < 0)
			dev_err(rtd->dev,
				"snd_soc_dai_set_sysclk err = %d\n", ret);
		break;
	case QUATERNARY_TDM_RX_0:
	case QUATERNARY_TDM_TX_0:
		ret = sdm845_tdm_snd_hw_params(substream, params);
		break;
	case SLIMBUS_0_RX...SLIMBUS_6_TX:
		ret = sdm845_slim_snd_hw_params(substream, params);
		break;
	case QUATERNARY_MI2S_RX:
		break;
	default:
		pr_err("%s: invalid dai id 0x%x\n", __func__, cpu_dai->id);
		break;
	}
	return ret;
}

static void sdm845_jack_free(struct snd_jack *jack)
{
	struct snd_soc_component *component = jack->private_data;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sdm845_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sdm845_snd_data *pdata = snd_soc_card_get_drvdata(card);
	struct snd_jack *jack;
	/*
	 * Codec SLIMBUS configuration
	 * RX1, RX2, RX3, RX4, RX5, RX6, RX7, RX8, RX9, RX10, RX11, RX12, RX13
	 * TX1, TX2, TX3, TX4, TX5, TX6, TX7, TX8, TX9, TX10, TX11, TX12, TX13
	 * TX14, TX15, TX16
	 */
	unsigned int rx_ch[SLIM_MAX_RX_PORTS] = {144, 145, 146, 147, 148, 149,
					150, 151, 152, 153, 154, 155, 156};
	unsigned int tx_ch[SLIM_MAX_TX_PORTS] = {128, 129, 130, 131, 132, 133,
					    134, 135, 136, 137, 138, 139,
					    140, 141, 142, 143};
	int rval, i;


	if (!pdata->jack_setup) {
		rval = snd_soc_card_jack_new(card, "Headset Jack",
				SND_JACK_HEADSET |
				SND_JACK_HEADPHONE |
				SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				SND_JACK_BTN_2 | SND_JACK_BTN_3,
				&pdata->jack, NULL, 0);

		if (rval < 0) {
			dev_err(card->dev, "Unable to add Headphone Jack\n");
			return rval;
		}

		jack = pdata->jack.jack;

		snd_jack_set_key(jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
		snd_jack_set_key(jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
		snd_jack_set_key(jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
		snd_jack_set_key(jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);
		pdata->jack_setup = true;
	}

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX:
		jack  = pdata->jack.jack;
		component = codec_dai->component;

		jack->private_data = component;
		jack->private_free = sdm845_jack_free;
		rval = snd_soc_component_set_jack(component,
						  &pdata->jack, NULL);
		if (rval != 0 && rval != -ENOTSUPP) {
			dev_warn(card->dev, "Failed to set jack: %d\n", rval);
			return rval;
		}
		break;
	case SLIMBUS_0_RX...SLIMBUS_6_TX:
		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			rval = snd_soc_dai_set_channel_map(codec_dai,
							  ARRAY_SIZE(tx_ch),
							  tx_ch,
							  ARRAY_SIZE(rx_ch),
							  rx_ch);
			if (rval != 0 && rval != -ENOTSUPP)
				return rval;

			snd_soc_dai_set_sysclk(codec_dai, 0,
					       WCD934X_DEFAULT_MCLK_RATE,
					       SNDRV_PCM_STREAM_PLAYBACK);
		}
		break;
	default:
		break;
	}

	return 0;
}


static int sdm845_snd_startup(struct snd_pcm_substream *substream)
{
	unsigned int fmt = SND_SOC_DAIFMT_CBS_CFS;
	unsigned int codec_dai_fmt = SND_SOC_DAIFMT_CBS_CFS;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct sdm845_snd_data *data = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int j;
	int ret;

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX:
	case PRIMARY_MI2S_TX:
		codec_dai_fmt |= SND_SOC_DAIFMT_NB_NF;
		if (++(data->pri_mi2s_clk_count) == 1) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_MCLK_1,
				DEFAULT_MCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_PRI_MI2S_IBIT,
				MI2S_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		}
		snd_soc_dai_set_fmt(cpu_dai, fmt);
		snd_soc_dai_set_fmt(codec_dai, codec_dai_fmt);
		break;

	case SECONDARY_MI2S_TX:
		codec_dai_fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_I2S;
		if (++(data->sec_mi2s_clk_count) == 1) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_SEC_MI2S_IBIT,
				MI2S_BCLK_RATE,	SNDRV_PCM_STREAM_CAPTURE);
		}
		snd_soc_dai_set_fmt(cpu_dai, fmt);
		snd_soc_dai_set_fmt(codec_dai, codec_dai_fmt);
		break;
	case QUATERNARY_MI2S_RX:
		snd_soc_dai_set_sysclk(cpu_dai,
			Q6AFE_LPASS_CLK_ID_QUAD_MI2S_IBIT,
			MI2S_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBS_CFS);


		break;

	case QUATERNARY_TDM_RX_0:
	case QUATERNARY_TDM_TX_0:
		if (++(data->quat_tdm_clk_count) == 1) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_QUAD_TDM_IBIT,
				TDM_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		}

		codec_dai_fmt |= SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_DSP_B;

		for_each_rtd_codec_dais(rtd, j, codec_dai) {

			if (!strcmp(codec_dai->component->name_prefix,
				    "Left")) {
				ret = snd_soc_dai_set_fmt(
						codec_dai, codec_dai_fmt);
				if (ret < 0) {
					dev_err(rtd->dev,
						"Left TDM fmt err:%d\n", ret);
					return ret;
				}
			}

			if (!strcmp(codec_dai->component->name_prefix,
				    "Right")) {
				ret = snd_soc_dai_set_fmt(
						codec_dai, codec_dai_fmt);
				if (ret < 0) {
					dev_err(rtd->dev,
						"Right TDM slot err:%d\n", ret);
					return ret;
				}
			}
		}
		break;
	case SLIMBUS_0_RX...SLIMBUS_6_TX:
		break;

	default:
		pr_err("%s: invalid dai id 0x%x\n", __func__, cpu_dai->id);
		break;
	}
	return 0;
}

static void  sdm845_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct sdm845_snd_data *data = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX:
	case PRIMARY_MI2S_TX:
		if (--(data->pri_mi2s_clk_count) == 0) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_MCLK_1,
				0, SNDRV_PCM_STREAM_PLAYBACK);
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_PRI_MI2S_IBIT,
				0, SNDRV_PCM_STREAM_PLAYBACK);
		}
		break;

	case SECONDARY_MI2S_TX:
		if (--(data->sec_mi2s_clk_count) == 0) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_SEC_MI2S_IBIT,
				0, SNDRV_PCM_STREAM_CAPTURE);
		}
		break;

	case QUATERNARY_TDM_RX_0:
	case QUATERNARY_TDM_TX_0:
		if (--(data->quat_tdm_clk_count) == 0) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_QUAD_TDM_IBIT,
				0, SNDRV_PCM_STREAM_PLAYBACK);
		}
		break;
	case SLIMBUS_0_RX...SLIMBUS_6_TX:
	case QUATERNARY_MI2S_RX:
		break;

	default:
		pr_err("%s: invalid dai id 0x%x\n", __func__, cpu_dai->id);
		break;
	}
}

static int sdm845_snd_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sdm845_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];
	int ret;

	if (!sruntime)
		return 0;

	if (data->stream_prepared[cpu_dai->id]) {
		sdw_disable_stream(sruntime);
		sdw_deprepare_stream(sruntime);
		data->stream_prepared[cpu_dai->id] = false;
	}

	ret = sdw_prepare_stream(sruntime);
	if (ret)
		return ret;

	/**
	 * NOTE: there is a strict hw requirement about the ordering of port
	 * enables and actual WSA881x PA enable. PA enable should only happen
	 * after soundwire ports are enabled if not DC on the line is
	 * accumulated resulting in Click/Pop Noise
	 * PA enable/mute are handled as part of codec DAPM and digital mute.
	 */

	ret = sdw_enable_stream(sruntime);
	if (ret) {
		sdw_deprepare_stream(sruntime);
		return ret;
	}
	data->stream_prepared[cpu_dai->id] = true;

	return ret;
}

static int sdm845_snd_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sdm845_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	if (sruntime && data->stream_prepared[cpu_dai->id]) {
		sdw_disable_stream(sruntime);
		sdw_deprepare_stream(sruntime);
		data->stream_prepared[cpu_dai->id] = false;
	}

	return 0;
}

static const struct snd_soc_ops sdm845_be_ops = {
	.hw_params = sdm845_snd_hw_params,
	.hw_free = sdm845_snd_hw_free,
	.prepare = sdm845_snd_prepare,
	.startup = sdm845_snd_startup,
	.shutdown = sdm845_snd_shutdown,
};

static int sdm845_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	rate->min = rate->max = DEFAULT_SAMPLE_RATE_48K;
	channels->min = channels->max = 2;
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);

	return 0;
}

static const struct snd_soc_dapm_widget sdm845_snd_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
};

static void sdm845_add_ops(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;

	for_each_card_prelinks(card, i, link) {
		if (link->no_pcm == 1) {
			link->ops = &sdm845_be_ops;
			link->be_hw_params_fixup = sdm845_be_hw_params_fixup;
		}
		link->init = sdm845_dai_init;
	}
}

static int sdm845_snd_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct sdm845_snd_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	/* Allocate the private data */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card->driver_name = DRIVER_NAME;
	card->dapm_widgets = sdm845_snd_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(sdm845_snd_widgets);
	card->dev = dev;
	card->owner = THIS_MODULE;
	dev_set_drvdata(dev, card);
	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	data->card = card;
	snd_soc_card_set_drvdata(card, data);

	sdm845_add_ops(card);
	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id sdm845_snd_device_id[]  = {
	{ .compatible = "qcom,sdm845-sndcard" },
	{ .compatible = "qcom,db845c-sndcard" },
	{ .compatible = "lenovo,yoga-c630-sndcard" },
	{},
};
MODULE_DEVICE_TABLE(of, sdm845_snd_device_id);

static struct platform_driver sdm845_snd_driver = {
	.probe = sdm845_snd_platform_probe,
	.driver = {
		.name = "msm-snd-sdm845",
		.of_match_table = sdm845_snd_device_id,
	},
};
module_platform_driver(sdm845_snd_driver);

MODULE_DESCRIPTION("sdm845 ASoC Machine Driver");
MODULE_LICENSE("GPL v2");
