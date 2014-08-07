/*
 * SiRF Audio port driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

struct sirf_audio_port {
	struct regmap *regmap;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
};


static int sirf_audio_port_dai_probe(struct snd_soc_dai *dai)
{
	struct sirf_audio_port *port = snd_soc_dai_get_drvdata(dai);
	snd_soc_dai_init_dma_data(dai, &port->playback_dma_data,
			&port->capture_dma_data);
	return 0;
}

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
};

static const struct snd_soc_component_driver sirf_audio_port_component = {
	.name       = "sirf-audio-port",
};

static int sirf_audio_port_probe(struct platform_device *pdev)
{
	int ret;
	struct sirf_audio_port *port;

	port = devm_kzalloc(&pdev->dev,
			sizeof(struct sirf_audio_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

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
