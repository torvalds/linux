// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022, Linaro Limited

#include <dt-bindings/sound/qcom,q6afe.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <linux/soundwire/sdw.h>
#include <sound/jack.h>
#include <linux/input-event-codes.h>
#include "qdsp6/q6afe.h"
#include "common.h"
#include "sdw.h"

struct sc8280xp_snd_data {
	bool stream_prepared[AFE_PORT_MAX];
	struct snd_soc_card *card;
	struct sdw_stream_runtime *sruntime[AFE_PORT_MAX];
	struct snd_soc_jack jack;
	bool jack_setup;
};

static int sc8280xp_snd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sc8280xp_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_card *card = rtd->card;

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
	default:
		break;
	}

	return qcom_snd_wcd_jack_setup(rtd, &data->jack, &data->jack_setup);
}

static void sc8280xp_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct sc8280xp_snd_data *pdata = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime = pdata->sruntime[cpu_dai->id];

	pdata->sruntime[cpu_dai->id] = NULL;
	sdw_release_stream(sruntime);
}

static int sc8280xp_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = 48000;
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
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct sc8280xp_snd_data *pdata = snd_soc_card_get_drvdata(rtd->card);

	return qcom_snd_sdw_hw_params(substream, params, &pdata->sruntime[cpu_dai->id]);
}

static int sc8280xp_snd_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct sc8280xp_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	return qcom_snd_sdw_prepare(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static int sc8280xp_snd_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sc8280xp_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	return qcom_snd_sdw_hw_free(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static const struct snd_soc_ops sc8280xp_be_ops = {
	.startup = qcom_snd_sdw_startup,
	.shutdown = sc8280xp_snd_shutdown,
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
	card->owner = THIS_MODULE;
	/* Allocate the private data */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card->dev = dev;
	dev_set_drvdata(dev, card);
	snd_soc_card_set_drvdata(card, data);
	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	card->driver_name = of_device_get_match_data(dev);
	sc8280xp_add_be_ops(card);
	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id snd_sc8280xp_dt_match[] = {
	{.compatible = "qcom,sc8280xp-sndcard", "sc8280xp"},
	{.compatible = "qcom,sm8450-sndcard", "sm8450"},
	{.compatible = "qcom,sm8550-sndcard", "sm8550"},
	{.compatible = "qcom,sm8650-sndcard", "sm8650"},
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
