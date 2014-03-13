/*
 * SiRF Audio port driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "sirf-audio-port.h"

struct sirf_audio_port {
	struct regmap *regmap;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
};

static void sirf_audio_port_tx_enable(struct sirf_audio_port *port)
{
	regmap_update_bits(port->regmap, AUDIO_PORT_IC_TXFIFO_OP,
		AUDIO_FIFO_RESET, AUDIO_FIFO_RESET);
	regmap_write(port->regmap, AUDIO_PORT_IC_TXFIFO_INT_MSK, 0);
	regmap_write(port->regmap, AUDIO_PORT_IC_TXFIFO_OP, 0);
	regmap_update_bits(port->regmap, AUDIO_PORT_IC_TXFIFO_OP,
		AUDIO_FIFO_START, AUDIO_FIFO_START);
	regmap_update_bits(port->regmap, AUDIO_PORT_IC_CODEC_TX_CTRL,
		IC_TX_ENABLE, IC_TX_ENABLE);
}

static void sirf_audio_port_tx_disable(struct sirf_audio_port *port)
{
	regmap_write(port->regmap, AUDIO_PORT_IC_TXFIFO_OP, 0);
	regmap_update_bits(port->regmap, AUDIO_PORT_IC_CODEC_TX_CTRL,
		IC_TX_ENABLE, ~IC_TX_ENABLE);
}

static void sirf_audio_port_rx_enable(struct sirf_audio_port *port,
	int channels)
{
	regmap_update_bits(port->regmap, AUDIO_PORT_IC_RXFIFO_OP,
		AUDIO_FIFO_RESET, AUDIO_FIFO_RESET);
	regmap_write(port->regmap, AUDIO_PORT_IC_RXFIFO_INT_MSK, 0);
	regmap_write(port->regmap, AUDIO_PORT_IC_RXFIFO_OP, 0);
	regmap_update_bits(port->regmap, AUDIO_PORT_IC_RXFIFO_OP,
		AUDIO_FIFO_START, AUDIO_FIFO_START);
	if (channels == 1)
		regmap_update_bits(port->regmap, AUDIO_PORT_IC_CODEC_RX_CTRL,
			IC_RX_ENABLE_MONO, IC_RX_ENABLE_MONO);
	else
		regmap_update_bits(port->regmap, AUDIO_PORT_IC_CODEC_RX_CTRL,
			IC_RX_ENABLE_STEREO, IC_RX_ENABLE_STEREO);
}

static void sirf_audio_port_rx_disable(struct sirf_audio_port *port)
{
	regmap_update_bits(port->regmap, AUDIO_PORT_IC_CODEC_RX_CTRL,
			IC_RX_ENABLE_STEREO, ~IC_RX_ENABLE_STEREO);
}

static int sirf_audio_port_dai_probe(struct snd_soc_dai *dai)
{
	struct sirf_audio_port *port = snd_soc_dai_get_drvdata(dai);
	snd_soc_dai_init_dma_data(dai, &port->playback_dma_data,
			&port->capture_dma_data);
	return 0;
}

static int sirf_audio_port_trigger(struct snd_pcm_substream *substream, int cmd,
	struct snd_soc_dai *dai)
{
	struct sirf_audio_port *port = snd_soc_dai_get_drvdata(dai);
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (playback)
			sirf_audio_port_tx_disable(port);
		else
			sirf_audio_port_rx_disable(port);
		break;
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (playback)
			sirf_audio_port_tx_enable(port);
		else
			sirf_audio_port_rx_enable(port,
				substream->runtime->channels);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops sirf_audio_port_dai_ops = {
	.trigger = sirf_audio_port_trigger,
};

static struct snd_soc_dai_driver sirf_audio_port_dai = {
	.probe = sirf_audio_port_dai_probe,
	.name = "sirf-audio-port",
	.id = 0,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &sirf_audio_port_dai_ops,
};

static const struct snd_soc_component_driver sirf_audio_port_component = {
	.name       = "sirf-audio-port",
};

static const struct regmap_config sirf_audio_port_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AUDIO_PORT_IC_RXFIFO_INT_MSK,
	.cache_type = REGCACHE_NONE,
};

static int sirf_audio_port_probe(struct platform_device *pdev)
{
	int ret;
	struct sirf_audio_port *port;
	void __iomem *base;
	struct resource *mem_res;

	port = devm_kzalloc(&pdev->dev,
			sizeof(struct sirf_audio_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}

	base = devm_ioremap(&pdev->dev, mem_res->start,
			resource_size(mem_res));
	if (base == NULL)
		return -ENOMEM;

	port->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &sirf_audio_port_regmap_config);
	if (IS_ERR(port->regmap))
		return PTR_ERR(port->regmap);

	ret = devm_snd_soc_register_component(&pdev->dev,
			&sirf_audio_port_component, &sirf_audio_port_dai, 1);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, port);
	return devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
}

static const struct of_device_id sirf_audio_port_of_match[] = {
	{ .compatible = "sirf,audio-port", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_audio_port_of_match);

static struct platform_driver sirf_audio_port_driver = {
	.driver = {
		.name = "sirf-audio-port",
		.owner = THIS_MODULE,
		.of_match_table = sirf_audio_port_of_match,
	},
	.probe = sirf_audio_port_probe,
};

module_platform_driver(sirf_audio_port_driver);

MODULE_DESCRIPTION("SiRF Audio Port driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
