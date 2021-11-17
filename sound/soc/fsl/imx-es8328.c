// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2012 Freescale Semiconductor, Inc.
// Copyright 2012 Linaro Ltd.

#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include "imx-audmux.h"

#define DAI_NAME_SIZE	32
#define MUX_PORT_MAX	7

struct imx_es8328_data {
	struct device *dev;
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
	char codec_dai_name[DAI_NAME_SIZE];
	char platform_name[DAI_NAME_SIZE];
	int jack_gpio;
};

static struct snd_soc_jack_gpio headset_jack_gpios[] = {
	{
		.gpio = -1,
		.name = "headset-gpio",
		.report = SND_JACK_HEADSET,
		.invert = 0,
		.debounce_time = 200,
	},
};

static struct snd_soc_jack headset_jack;

static int imx_es8328_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct imx_es8328_data *data = container_of(rtd->card,
					struct imx_es8328_data, card);
	int ret = 0;

	/* Headphone jack detection */
	if (gpio_is_valid(data->jack_gpio)) {
		ret = snd_soc_card_jack_new(rtd->card, "Headphone",
					    SND_JACK_HEADPHONE | SND_JACK_BTN_0,
					    &headset_jack, NULL, 0);
		if (ret)
			return ret;

		headset_jack_gpios[0].gpio = data->jack_gpio;
		ret = snd_soc_jack_add_gpios(&headset_jack,
					     ARRAY_SIZE(headset_jack_gpios),
					     headset_jack_gpios);
	}

	return ret;
}

static const struct snd_soc_dapm_widget imx_es8328_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_REGULATOR_SUPPLY("audio-amp", 1, 0),
};

static int imx_es8328_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *ssi_np = NULL, *codec_np = NULL;
	struct platform_device *ssi_pdev;
	struct imx_es8328_data *data;
	struct snd_soc_dai_link_component *comp;
	u32 int_port, ext_port;
	int ret;
	struct device *dev = &pdev->dev;

	ret = of_property_read_u32(np, "mux-int-port", &int_port);
	if (ret) {
		dev_err(dev, "mux-int-port missing or invalid\n");
		goto fail;
	}
	if (int_port > MUX_PORT_MAX || int_port == 0) {
		dev_err(dev, "mux-int-port: hardware only has %d mux ports\n",
			MUX_PORT_MAX);
		goto fail;
	}

	ret = of_property_read_u32(np, "mux-ext-port", &ext_port);
	if (ret) {
		dev_err(dev, "mux-ext-port missing or invalid\n");
		goto fail;
	}
	if (ext_port > MUX_PORT_MAX || ext_port == 0) {
		dev_err(dev, "mux-ext-port: hardware only has %d mux ports\n",
			MUX_PORT_MAX);
		ret = -EINVAL;
		goto fail;
	}

	/*
	 * The port numbering in the hardware manual starts at 1, while
	 * the audmux API expects it starts at 0.
	 */
	int_port--;
	ext_port--;
	ret = imx_audmux_v2_configure_port(int_port,
			IMX_AUDMUX_V2_PTCR_SYN |
			IMX_AUDMUX_V2_PTCR_TFSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TCSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TFSDIR |
			IMX_AUDMUX_V2_PTCR_TCLKDIR,
			IMX_AUDMUX_V2_PDCR_RXDSEL(ext_port));
	if (ret) {
		dev_err(dev, "audmux internal port setup failed\n");
		return ret;
	}
	ret = imx_audmux_v2_configure_port(ext_port,
			IMX_AUDMUX_V2_PTCR_SYN,
			IMX_AUDMUX_V2_PDCR_RXDSEL(int_port));
	if (ret) {
		dev_err(dev, "audmux external port setup failed\n");
		return ret;
	}

	ssi_np = of_parse_phandle(pdev->dev.of_node, "ssi-controller", 0);
	codec_np = of_parse_phandle(pdev->dev.of_node, "audio-codec", 0);
	if (!ssi_np || !codec_np) {
		dev_err(dev, "phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	ssi_pdev = of_find_device_by_node(ssi_np);
	if (!ssi_pdev) {
		dev_err(dev, "failed to find SSI platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto put_device;
	}

	comp = devm_kzalloc(dev, 3 * sizeof(*comp), GFP_KERNEL);
	if (!comp) {
		ret = -ENOMEM;
		goto put_device;
	}

	data->dev = dev;

	data->jack_gpio = of_get_named_gpio(pdev->dev.of_node, "jack-gpio", 0);

	data->dai.cpus		= &comp[0];
	data->dai.codecs	= &comp[1];
	data->dai.platforms	= &comp[2];

	data->dai.num_cpus	= 1;
	data->dai.num_codecs	= 1;
	data->dai.num_platforms	= 1;

	data->dai.name = "hifi";
	data->dai.stream_name = "hifi";
	data->dai.codecs->dai_name = "es8328-hifi-analog";
	data->dai.codecs->of_node = codec_np;
	data->dai.cpus->of_node = ssi_np;
	data->dai.platforms->of_node = ssi_np;
	data->dai.init = &imx_es8328_dai_init;
	data->dai.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			    SND_SOC_DAIFMT_CBM_CFM;

	data->card.dev = dev;
	data->card.dapm_widgets = imx_es8328_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(imx_es8328_dapm_widgets);
	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret) {
		dev_err(dev, "Unable to parse card name\n");
		goto put_device;
	}
	ret = snd_soc_of_parse_audio_routing(&data->card, "audio-routing");
	if (ret) {
		dev_err(dev, "Unable to parse routing: %d\n", ret);
		goto put_device;
	}
	data->card.num_links = 1;
	data->card.owner = THIS_MODULE;
	data->card.dai_link = &data->dai;

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		dev_err(dev, "Unable to register: %d\n", ret);
		goto put_device;
	}

	platform_set_drvdata(pdev, data);
put_device:
	put_device(&ssi_pdev->dev);
fail:
	of_node_put(ssi_np);
	of_node_put(codec_np);

	return ret;
}

static const struct of_device_id imx_es8328_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-es8328", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_es8328_dt_ids);

static struct platform_driver imx_es8328_driver = {
	.driver = {
		.name = "imx-es8328",
		.of_match_table = imx_es8328_dt_ids,
	},
	.probe = imx_es8328_probe,
};
module_platform_driver(imx_es8328_driver);

MODULE_AUTHOR("Sean Cross <xobs@kosagi.com>");
MODULE_DESCRIPTION("Kosagi i.MX6 ES8328 ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-audio-es8328");
