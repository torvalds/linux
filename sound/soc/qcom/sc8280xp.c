// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022, Linaro Limited

#include <dt-bindings/sound/qcom,q6afe.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/soundwire/sdw.h>
#include <sound/jack.h>
#include <linux/input-event-codes.h>
#include "qdsp6/q6afe.h"
#include "qdsp6/q6apm.h"
#include "qdsp6/q6prm.h"
#include "qdsp6/q6dsp-common.h"
#include "common.h"
#include "sdw.h"

#define I2S_MCLKFS 256

#define I2S_MCLK_RATE(rate) \
	((rate) * (I2S_MCLKFS))
#define I2S_BIT_RATE(rate, channels, format) \
	((rate) * (channels) * (format))

static struct snd_soc_dapm_widget sc8280xp_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("DP0 Jack", NULL),
	SND_SOC_DAPM_SPK("DP1 Jack", NULL),
	SND_SOC_DAPM_SPK("DP2 Jack", NULL),
	SND_SOC_DAPM_SPK("DP3 Jack", NULL),
	SND_SOC_DAPM_SPK("DP4 Jack", NULL),
	SND_SOC_DAPM_SPK("DP5 Jack", NULL),
	SND_SOC_DAPM_SPK("DP6 Jack", NULL),
	SND_SOC_DAPM_SPK("DP7 Jack", NULL),
};

static const struct snd_kcontrol_new max98090_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headset Mic12"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic56"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Receiver"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
};

static const struct snd_soc_dapm_widget max98090_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic12", NULL),
	SND_SOC_DAPM_MIC("Headset Mic56", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Receiver", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

struct qcom_snd_soc_common {
	const char *driver_name;
	const struct snd_soc_dapm_widget *dapm_widgets;
	int num_dapm_widgets;
	const struct snd_soc_dapm_route *dapm_routes;
	int num_dapm_routes;
	const struct snd_kcontrol_new *controls;
	int num_controls;
	unsigned int codec_dai_fmt;
	bool codec_sysclk_set;
	bool mi2s_mclk_enable;
	bool mi2s_bclk_enable;
	bool wcd_jack;
	int (*snd_prepare)(struct snd_pcm_substream *substream);
};

struct sc8280xp_snd_data {
	bool stream_prepared[AFE_PORT_MAX];
	struct snd_soc_card *card;
	struct snd_soc_jack jack;
	struct snd_soc_jack dp_jack[8];
	const struct qcom_snd_soc_common *priv;
	bool jack_setup;
};

static inline int sc8280xp_get_mclk_freq(struct snd_pcm_hw_params *params)
{
	int rate = params_rate(params);

	switch (rate) {
	case 11025:
	case 44100:
	case 88200:
		return I2S_MCLK_RATE(44100);
	default:
		break;
	}

	return I2S_MCLK_RATE(rate);
}

static inline int sc8280xp_get_bclk_freq(struct snd_pcm_hw_params *params)
{
	return I2S_BIT_RATE(params_rate(params),
			    params_channels(params),
			    snd_pcm_format_width(params_format(params)));
}

static int sc8280xp_tdm_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sc8280xp_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai;
	struct qcom_snd_tdm_slot_cfg cpu_cfg;
	struct qcom_snd_tdm_slot_cfg codec_cfg;
	int bclk_freq;
	int ret;
	int i;

	ret = qcom_snd_get_dai_tdm_slots(rtd, &cpu_cfg, &codec_cfg);
	if (ret)
		return ret == -ENOENT ? 0 : ret;

	if (!cpu_cfg.slots)
		return 0;

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_BP_FP);
	if (ret && ret != -ENOTSUPP)
		return ret;

	if (data->priv->codec_dai_fmt) {
		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			ret = snd_soc_dai_set_fmt(codec_dai,
						  data->priv->codec_dai_fmt);
			if (ret && ret != -ENOTSUPP)
				return ret;
		}
	}

	ret = qcom_snd_apply_dai_tdm_slots_cfg(rtd, &cpu_cfg, &codec_cfg);
	if (ret)
		return ret;

	bclk_freq = snd_soc_tdm_params_to_bclk(params, cpu_cfg.slot_width, cpu_cfg.slots, 1);
	if (bclk_freq <= 0)
		return -EINVAL;

	if (data->priv->mi2s_bclk_enable) {
		ret = snd_soc_dai_set_sysclk(cpu_dai, LPAIF_MI2S_BCLK, bclk_freq,
					     SND_SOC_CLOCK_IN);
		if (ret && ret != -ENOTSUPP) {
			dev_err(rtd->dev, "%s: failed to set cpu sysclk: %d\n",
				__func__, ret);
			return ret;
		}
	}

	if (data->priv->codec_sysclk_set) {
		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			ret = snd_soc_dai_set_sysclk(codec_dai, 0, bclk_freq,
						     SND_SOC_CLOCK_IN);
			if (ret && ret != -ENOTSUPP) {
				dev_err(rtd->dev, "%s: failed to set codec sysclk on %s: %d\n",
					__func__, codec_dai->name, ret);
				return ret;
			}
		}
	}

	return 0;
}

static int sc8280xp_snd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sc8280xp_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_jack *dp_jack  = NULL;
	int dp_pcm_id = 0;

	switch (cpu_dai->id) {
	case WSA_CODEC_DMA_RX_0:
	case WSA_CODEC_DMA_RX_1:
		/*
		 * Set limit of -3 dB on Digital Volume and 0 dB on PA Volume
		 * to reduce the risk of speaker damage until we have active
		 * speaker protection in place.
		 */
		snd_soc_limit_volume(card, "WSA_RX0 Digital Volume", 81);
		snd_soc_limit_volume(card, "WSA_RX1 Digital Volume", 81);
		snd_soc_limit_volume(card, "SpkrLeft PA Volume", 17);
		snd_soc_limit_volume(card, "SpkrRight PA Volume", 17);
		break;
	case DISPLAY_PORT_RX_0:
		/* DISPLAY_PORT dai ids are not contiguous */
		dp_pcm_id = 0;
		dp_jack = &data->dp_jack[dp_pcm_id];
		break;
	case DISPLAY_PORT_RX_1 ... DISPLAY_PORT_RX_7:
		dp_pcm_id = cpu_dai->id - DISPLAY_PORT_RX_1 + 1;
		dp_jack = &data->dp_jack[dp_pcm_id];
		break;
	default:
		break;
	}

	if (dp_jack)
		return qcom_snd_dp_jack_setup(rtd, dp_jack, dp_pcm_id);

	if (data->priv->wcd_jack)
		return qcom_snd_wcd_jack_setup(rtd, &data->jack, &data->jack_setup);

	return 0;
}

static int sc8280xp_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	rate->min = rate->max = 48000;
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);
	channels->min = 2;
	channels->max = 2;
	switch (cpu_dai->id) {
	case TX_CODEC_DMA_TX_0:
	case TX_CODEC_DMA_TX_1:
	case TX_CODEC_DMA_TX_2:
	case TX_CODEC_DMA_TX_3:
		channels->min = 1;
		break;
	default:
		break;
	}


	return 0;
}

static int sc8280xp_snd_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct sc8280xp_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	int mclk_freq = sc8280xp_get_mclk_freq(params);
	int bclk_freq = sc8280xp_get_bclk_freq(params);
	int ret;

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX ... QUATERNARY_MI2S_TX:
	case QUINARY_MI2S_RX ... QUINARY_MI2S_TX:
	case SENARY_MI2S_RX ... SENARY_MI2S_TX:
	case LPI_MI2S_RX_0 ... LPI_MI2S_TX_4:
		ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_BP_FP);
		if (ret && ret != -ENOTSUPP)
			return ret;

		if (data->priv->codec_dai_fmt) {
			ret = snd_soc_dai_set_fmt(codec_dai,
						  data->priv->codec_dai_fmt);
			if (ret && ret != -ENOTSUPP)
				return ret;
		}

		if (data->priv->mi2s_mclk_enable) {
			ret = snd_soc_dai_set_sysclk(cpu_dai,
						     LPAIF_MI2S_MCLK, mclk_freq,
						     SND_SOC_CLOCK_OUT);
			if (ret)
				return ret;
		}

		if (data->priv->mi2s_bclk_enable) {
			ret = snd_soc_dai_set_sysclk(cpu_dai,
						     LPAIF_MI2S_BCLK, bclk_freq,
						     SND_SOC_CLOCK_OUT);
			if (ret)
				return ret;
		}

		if (data->priv->codec_sysclk_set) {
			ret = snd_soc_dai_set_sysclk(codec_dai,
						     0, mclk_freq,
						     SND_SOC_CLOCK_IN);
			if (ret && ret != -ENOTSUPP)
				return ret;
		}
		break;
	case PRIMARY_TDM_RX_0 ... QUINARY_TDM_TX_7:
		return sc8280xp_tdm_hw_params(substream, params);
	default:
		break;
	}

	return 0;
}

/*
 * WSA and WSA2 are handled as a single interface with the
 * following channels mask:
 *  __________________________________________________
 *  | Bits  |     3    |     2    |   1     |     0   |
 *  ---------------------------------------------------
 *  | Line  | WSA2 Ch2 | WSA2 Ch1 | WSA Ch2 | WSA Ch1 |
 *  ---------------------------------------------------
 *
 * The Ayaneo Pocket S2 speakers are connected only to
 * the WSA2 interface and the WSA interface is not enabled.
 *
 * Set the channel mapping on the WSA2 channels only.
 */
static const unsigned int ayaneo_ps2_channels_mapping[] = {
	0,			/* WSA Ch1 */
	0,			/* WSA Ch2 */
	PCM_CHANNEL_FL,		/* WSA2 Ch1 */
	PCM_CHANNEL_FR		/* WSA2 Ch2 */
};

static int ayaneo_ps2_snd_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	unsigned int channels = substream->runtime->channels;

	if (cpu_dai->id != WSA_CODEC_DMA_RX_0)
		return 0;

	if (channels != 2)
		return -EINVAL;

	return snd_soc_dai_set_channel_map(cpu_dai, 0, NULL,
					   ARRAY_SIZE(ayaneo_ps2_channels_mapping),
					   ayaneo_ps2_channels_mapping);
}

static int sc8280xp_snd_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct sc8280xp_snd_data *data = snd_soc_card_get_drvdata(rtd->card);

	if (data->priv->snd_prepare) {
		int ret;

		ret = data->priv->snd_prepare(substream);
		if (ret)
			return ret;
	}

	return qcom_snd_sdw_prepare(substream, &data->stream_prepared[cpu_dai->id]);
}

static int sc8280xp_snd_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sc8280xp_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);

	return qcom_snd_sdw_hw_free(substream, &data->stream_prepared[cpu_dai->id]);
}

static const struct snd_soc_ops sc8280xp_be_ops = {
	.startup = qcom_snd_sdw_startup,
	.shutdown = qcom_snd_sdw_shutdown,
	.hw_params = sc8280xp_snd_hw_params,
	.hw_free = sc8280xp_snd_hw_free,
	.prepare = sc8280xp_snd_prepare,
};

static void sc8280xp_add_be_ops(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;

	for_each_card_prelinks(card, i, link) {
		if (link->no_pcm == 1) {
			link->init = sc8280xp_snd_init;
			link->be_hw_params_fixup = sc8280xp_be_hw_params_fixup;
			link->ops = &sc8280xp_be_ops;
		}
	}
}

static int sc8280xp_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct sc8280xp_snd_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	/* Allocate the private data */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->priv = of_device_get_match_data(dev);
	if (!data->priv)
		return -ENODEV;

	card->owner = THIS_MODULE;
	card->dev = dev;
	dev_set_drvdata(dev, card);
	snd_soc_card_set_drvdata(card, data);
	card->dapm_widgets = data->priv->dapm_widgets;
	card->num_dapm_widgets = data->priv->num_dapm_widgets;
	card->dapm_routes = data->priv->dapm_routes;
	card->num_dapm_routes = data->priv->num_dapm_routes;
	card->controls = data->priv->controls;
	card->num_controls = data->priv->num_controls;

	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	card->driver_name = data->priv->driver_name;
	sc8280xp_add_be_ops(card);
	return devm_snd_soc_register_card(dev, card);
}

static struct qcom_snd_soc_common ayaneo_ps2_priv_data = {
	.driver_name = "ayaneo-ps2",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.snd_prepare = ayaneo_ps2_snd_prepare,
	.wcd_jack = true,
};

static const struct qcom_snd_soc_common eliza_priv_data = {
	.driver_name = "eliza",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.wcd_jack = true,
};

static const struct qcom_snd_soc_common hawi_priv_data = {
	.driver_name = "hawi",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.codec_sysclk_set = true,
	.mi2s_bclk_enable = true,
	.wcd_jack = true,
};

static const struct qcom_snd_soc_common kaanapali_priv_data = {
	.driver_name = "kaanapali",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.wcd_jack = true,
};

static const struct qcom_snd_soc_common qcs9100_priv_data = {
	.driver_name = "sa8775p",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
};

static const struct qcom_snd_soc_common qcs615_priv_data = {
	.driver_name = "qcs615",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.codec_sysclk_set = true,
};

static const struct qcom_snd_soc_common qcm6490_priv_data = {
	.driver_name = "qcm6490",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.wcd_jack = true,
};

static const struct qcom_snd_soc_common qcs6490_priv_data = {
	.driver_name = "qcs6490",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.wcd_jack = true,
};

static const struct qcom_snd_soc_common qcs8275_priv_data = {
	.driver_name = "qcs8300",
	.dapm_widgets = max98090_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(max98090_dapm_widgets),
	.controls = max98090_controls,
	.num_controls = ARRAY_SIZE(max98090_controls),
	.codec_sysclk_set = true,
	.codec_dai_fmt = SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_BC_FC,
};

static const struct qcom_snd_soc_common sc8280xp_priv_data = {
	.driver_name = "sc8280xp",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.wcd_jack = true,
};

static const struct qcom_snd_soc_common sm8450_priv_data = {
	.driver_name = "sm8450",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.wcd_jack = true,
	/* I2S Connected to HDMI */
	.mi2s_mclk_enable = true,
	.mi2s_bclk_enable = true,
	.codec_dai_fmt = SND_SOC_DAIFMT_BC_FC |
			 SND_SOC_DAIFMT_NB_NF |
			 SND_SOC_DAIFMT_I2S,
};

static const struct qcom_snd_soc_common sm8475_priv_data = {
	.driver_name = "sm8475",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.wcd_jack = true,
};

static const struct qcom_snd_soc_common sm8550_priv_data = {
	.driver_name = "sm8550",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.wcd_jack = true,
	/* I2S Connected to HDMI */
	.mi2s_mclk_enable = true,
	.mi2s_bclk_enable = true,
	.codec_dai_fmt = SND_SOC_DAIFMT_BC_FC |
			 SND_SOC_DAIFMT_NB_NF |
			 SND_SOC_DAIFMT_I2S,
};

static const struct qcom_snd_soc_common sm8650_priv_data = {
	.driver_name = "sm8650",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.wcd_jack = true,
	/* I2S Connected to HDMI */
	.mi2s_mclk_enable = true,
	.mi2s_bclk_enable = true,
	.codec_dai_fmt = SND_SOC_DAIFMT_BC_FC |
			 SND_SOC_DAIFMT_NB_NF |
			 SND_SOC_DAIFMT_I2S,
};

static const struct qcom_snd_soc_common sm8750_priv_data = {
	.driver_name = "sm8750",
	.dapm_widgets = sc8280xp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sc8280xp_dapm_widgets),
	.wcd_jack = true,
};

static const struct of_device_id snd_sc8280xp_dt_match[] = {
	{ .compatible = "ayaneo,pocket-s2-sndcard", .data = &ayaneo_ps2_priv_data },
	{ .compatible = "qcom,eliza-sndcard", .data = &eliza_priv_data },
	{ .compatible = "qcom,hawi-sndcard", .data = &hawi_priv_data },
	{ .compatible = "qcom,kaanapali-sndcard", .data = &kaanapali_priv_data },
	{ .compatible = "qcom,maili-sndcard", .data = &hawi_priv_data },
	{ .compatible = "qcom,qcm6490-idp-sndcard", .data = &qcm6490_priv_data },
	{ .compatible = "qcom,qcs615-sndcard", .data = &qcs615_priv_data },
	{ .compatible = "qcom,qcs6490-rb3gen2-sndcard", .data = &qcs6490_priv_data },
	{ .compatible = "qcom,qcs8275-sndcard", .data = &qcs8275_priv_data },
	{ .compatible = "qcom,qcs9075-sndcard", .data = &qcs9100_priv_data },
	{ .compatible = "qcom,qcs9100-sndcard", .data = &qcs9100_priv_data },
	{ .compatible = "qcom,sc8280xp-sndcard", .data = &sc8280xp_priv_data },
	{ .compatible = "qcom,sm8450-sndcard", .data = &sm8450_priv_data },
	{ .compatible = "qcom,sm8475-sndcard", .data = &sm8475_priv_data },
	{ .compatible = "qcom,sm8550-sndcard", .data = &sm8550_priv_data },
	{ .compatible = "qcom,sm8650-sndcard", .data = &sm8650_priv_data },
	{ .compatible = "qcom,sm8750-sndcard", .data = &sm8750_priv_data },
	{}
};

MODULE_DEVICE_TABLE(of, snd_sc8280xp_dt_match);

static struct platform_driver snd_sc8280xp_driver = {
	.probe  = sc8280xp_platform_probe,
	.driver = {
		.name = "snd-sc8280xp",
		.of_match_table = snd_sc8280xp_dt_match,
	},
};
module_platform_driver(snd_sc8280xp_driver);
MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org");
MODULE_DESCRIPTION("SC8280XP ASoC Machine Driver");
MODULE_LICENSE("GPL");
