// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, Linaro Limited

#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include "q6dsp-lpass-ports.h"
#include "audioreach.h"
#include "q6apm.h"

#define AUDIOREACH_BE_PCM_BASE	16

struct q6apm_lpass_dai_data {
	struct q6apm_graph *graph[APM_PORT_MAX];
	bool is_port_started[APM_PORT_MAX];
	struct audioreach_module_config module_config[APM_PORT_MAX];
};

static int q6dma_set_channel_map(struct snd_soc_dai *dai,
				 unsigned int tx_num, unsigned int *tx_ch_mask,
				 unsigned int rx_num, unsigned int *rx_ch_mask)
{

	struct q6apm_lpass_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct audioreach_module_config *cfg = &dai_data->module_config[dai->id];
	int ch_mask;

	switch (dai->id) {
	case WSA_CODEC_DMA_TX_0:
	case WSA_CODEC_DMA_TX_1:
	case WSA_CODEC_DMA_TX_2:
	case VA_CODEC_DMA_TX_0:
	case VA_CODEC_DMA_TX_1:
	case VA_CODEC_DMA_TX_2:
	case TX_CODEC_DMA_TX_0:
	case TX_CODEC_DMA_TX_1:
	case TX_CODEC_DMA_TX_2:
	case TX_CODEC_DMA_TX_3:
	case TX_CODEC_DMA_TX_4:
	case TX_CODEC_DMA_TX_5:
		if (!tx_ch_mask) {
			dev_err(dai->dev, "tx slot not found\n");
			return -EINVAL;
		}

		if (tx_num > AR_PCM_MAX_NUM_CHANNEL) {
			dev_err(dai->dev, "invalid tx num %d\n",
				tx_num);
			return -EINVAL;
		}
		ch_mask = *tx_ch_mask;

		break;
	case WSA_CODEC_DMA_RX_0:
	case WSA_CODEC_DMA_RX_1:
	case RX_CODEC_DMA_RX_0:
	case RX_CODEC_DMA_RX_1:
	case RX_CODEC_DMA_RX_2:
	case RX_CODEC_DMA_RX_3:
	case RX_CODEC_DMA_RX_4:
	case RX_CODEC_DMA_RX_5:
	case RX_CODEC_DMA_RX_6:
	case RX_CODEC_DMA_RX_7:
		/* rx */
		if (!rx_ch_mask) {
			dev_err(dai->dev, "rx slot not found\n");
			return -EINVAL;
		}
		if (rx_num > APM_PORT_MAX_AUDIO_CHAN_CNT) {
			dev_err(dai->dev, "invalid rx num %d\n",
				rx_num);
			return -EINVAL;
		}
		ch_mask = *rx_ch_mask;

		break;
	default:
		dev_err(dai->dev, "%s: invalid dai id 0x%x\n",
			__func__, dai->id);
		return -EINVAL;
	}

	cfg->active_channels_mask = ch_mask;

	return 0;
}

static int q6dma_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct q6apm_lpass_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct audioreach_module_config *cfg = &dai_data->module_config[dai->id];

	cfg->bit_width = params_width(params);
	cfg->sample_rate = params_rate(params);
	cfg->num_channels = params_channels(params);

	return 0;
}

static void q6apm_lpass_dai_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct q6apm_lpass_dai_data *dai_data = dev_get_drvdata(dai->dev);
	int rc;

	if (!dai_data->is_port_started[dai->id])
		return;
	rc = q6apm_graph_stop(dai_data->graph[dai->id]);
	if (rc < 0)
		dev_err(dai->dev, "fail to close APM port (%d)\n", rc);

	q6apm_graph_close(dai_data->graph[dai->id]);
	dai_data->is_port_started[dai->id] = false;
}

static int q6apm_lpass_dai_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct q6apm_lpass_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct audioreach_module_config *cfg = &dai_data->module_config[dai->id];
	struct q6apm_graph *graph;
	int graph_id = dai->id;
	int rc;

	if (dai_data->is_port_started[dai->id]) {
		q6apm_graph_stop(dai_data->graph[dai->id]);
		dai_data->is_port_started[dai->id] = false;

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			q6apm_graph_close(dai_data->graph[dai->id]);
	}

	/**
	 * It is recommend to load DSP with source graph first and then sink
	 * graph, so sequence for playback and capture will be different
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		graph = q6apm_graph_open(dai->dev, NULL, dai->dev, graph_id);
		if (IS_ERR(graph)) {
			dev_err(dai->dev, "Failed to open graph (%d)\n", graph_id);
			rc = PTR_ERR(graph);
			return rc;
		}
		dai_data->graph[graph_id] = graph;
	}

	cfg->direction = substream->stream;
	rc = q6apm_graph_media_format_pcm(dai_data->graph[dai->id], cfg);

	if (rc) {
		dev_err(dai->dev, "Failed to set media format %d\n", rc);
		return rc;
	}

	rc = q6apm_graph_prepare(dai_data->graph[dai->id]);
	if (rc) {
		dev_err(dai->dev, "Failed to prepare Graph %d\n", rc);
		return rc;
	}

	rc = q6apm_graph_start(dai_data->graph[dai->id]);
	if (rc < 0) {
		dev_err(dai->dev, "fail to start APM port %x\n", dai->id);
		return rc;
	}
	dai_data->is_port_started[dai->id] = true;

	return 0;
}

static int q6apm_lpass_dai_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct q6apm_lpass_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6apm_graph *graph;
	int graph_id = dai->id;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		graph = q6apm_graph_open(dai->dev, NULL, dai->dev, graph_id);
		if (IS_ERR(graph)) {
			dev_err(dai->dev, "Failed to open graph (%d)\n", graph_id);
			return PTR_ERR(graph);
		}
		dai_data->graph[graph_id] = graph;
	}

	return 0;
}

static int q6i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct q6apm_lpass_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct audioreach_module_config *cfg = &dai_data->module_config[dai->id];

	cfg->fmt = fmt;

	return 0;
}

static const struct snd_soc_dai_ops q6dma_ops = {
	.prepare	= q6apm_lpass_dai_prepare,
	.startup	= q6apm_lpass_dai_startup,
	.shutdown	= q6apm_lpass_dai_shutdown,
	.set_channel_map  = q6dma_set_channel_map,
	.hw_params        = q6dma_hw_params,
};

static const struct snd_soc_dai_ops q6i2s_ops = {
	.prepare	= q6apm_lpass_dai_prepare,
	.startup	= q6apm_lpass_dai_startup,
	.shutdown	= q6apm_lpass_dai_shutdown,
	.set_channel_map  = q6dma_set_channel_map,
	.hw_params        = q6dma_hw_params,
	.set_fmt	= q6i2s_set_fmt,
};

static const struct snd_soc_component_driver q6apm_lpass_dai_component = {
	.name = "q6apm-be-dai-component",
	.of_xlate_dai_name = q6dsp_audio_ports_of_xlate_dai_name,
	.be_pcm_base = AUDIOREACH_BE_PCM_BASE,
	.use_dai_pcm_id = true,
};

static int q6apm_lpass_dai_dev_probe(struct platform_device *pdev)
{
	struct q6dsp_audio_port_dai_driver_config cfg;
	struct q6apm_lpass_dai_data *dai_data;
	struct snd_soc_dai_driver *dais;
	struct device *dev = &pdev->dev;
	int num_dais;

	dai_data = devm_kzalloc(dev, sizeof(*dai_data), GFP_KERNEL);
	if (!dai_data)
		return -ENOMEM;

	dev_set_drvdata(dev, dai_data);

	memset(&cfg, 0, sizeof(cfg));
	cfg.q6i2s_ops = &q6i2s_ops;
	cfg.q6dma_ops = &q6dma_ops;
	dais = q6dsp_audio_ports_set_config(dev, &cfg, &num_dais);

	return devm_snd_soc_register_component(dev, &q6apm_lpass_dai_component, dais, num_dais);
}

#ifdef CONFIG_OF
static const struct of_device_id q6apm_lpass_dai_device_id[] = {
	{ .compatible = "qcom,q6apm-lpass-dais" },
	{},
};
MODULE_DEVICE_TABLE(of, q6apm_lpass_dai_device_id);
#endif

static struct platform_driver q6apm_lpass_dai_platform_driver = {
	.driver = {
		.name = "q6apm-lpass-dais",
		.of_match_table = of_match_ptr(q6apm_lpass_dai_device_id),
	},
	.probe = q6apm_lpass_dai_dev_probe,
};
module_platform_driver(q6apm_lpass_dai_platform_driver);

MODULE_DESCRIPTION("AUDIOREACH APM LPASS dai driver");
MODULE_LICENSE("GPL");
