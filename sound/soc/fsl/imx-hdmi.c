// SPDX-License-Identifier: GPL-2.0
// Copyright 2017-2020 NXP

#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/hdmi-codec.h>
#include "fsl_sai.h"

/**
 * struct cpu_priv - CPU private data
 * @sysclk_id: SYSCLK ids for set_sysclk()
 * @slot_width: Slot width of each frame
 *
 * Note: [1] for tx and [0] for rx
 */
struct cpu_priv {
	u32 sysclk_id[2];
	u32 slot_width;
};

struct imx_hdmi_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
	struct snd_soc_jack hdmi_jack;
	struct snd_soc_jack_pin hdmi_jack_pin;
	struct cpu_priv cpu_priv;
	u32 dai_fmt;
};

static int imx_hdmi_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_hdmi_data *data = snd_soc_card_get_drvdata(rtd->card);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	struct device *dev = card->dev;
	u32 slot_width = data->cpu_priv.slot_width;
	int ret;

	/* MCLK always is (256 or 192) * rate. */
	ret = snd_soc_dai_set_sysclk(cpu_dai, data->cpu_priv.sysclk_id[tx],
				     8 * slot_width * params_rate(params),
				     tx ? SND_SOC_CLOCK_OUT : SND_SOC_CLOCK_IN);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dev, "failed to set cpu sysclk: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0, 0, 2, slot_width);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dev, "failed to set cpu dai tdm slot: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops imx_hdmi_ops = {
	.hw_params = imx_hdmi_hw_params,
};

static const struct snd_soc_dapm_widget imx_hdmi_widgets[] = {
	SND_SOC_DAPM_LINE("HDMI Jack", NULL),
};

static int imx_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	struct imx_hdmi_data *data = snd_soc_card_get_drvdata(card);
	int ret;

	data->hdmi_jack_pin.pin = "HDMI Jack";
	data->hdmi_jack_pin.mask = SND_JACK_LINEOUT;
	/* enable jack detection */
	ret = snd_soc_card_jack_new(card, "HDMI Jack", SND_JACK_LINEOUT,
				    &data->hdmi_jack, &data->hdmi_jack_pin, 1);
	if (ret) {
		dev_err(card->dev, "Can't new HDMI Jack %d\n", ret);
		return ret;
	}

	ret = snd_soc_component_set_jack(component, &data->hdmi_jack, NULL);
	if (ret && ret != -ENOTSUPP) {
		dev_err(card->dev, "Can't set HDMI Jack %d\n", ret);
		return ret;
	}

	return 0;
};

static int imx_hdmi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	bool hdmi_out = of_property_read_bool(np, "hdmi-out");
	bool hdmi_in = of_property_read_bool(np, "hdmi-in");
	struct snd_soc_dai_link_component *dlc;
	struct platform_device *cpu_pdev;
	struct device_node *cpu_np;
	struct imx_hdmi_data *data;
	int ret;

	dlc = devm_kzalloc(&pdev->dev, 3 * sizeof(*dlc), GFP_KERNEL);
	if (!dlc)
		return -ENOMEM;

	cpu_np = of_parse_phandle(np, "audio-cpu", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "cpu dai phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev = of_find_device_by_node(cpu_np);
	if (!cpu_pdev) {
		dev_err(&pdev->dev, "failed to find SAI platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}

	data->dai.cpus = &dlc[0];
	data->dai.num_cpus = 1;
	data->dai.platforms = &dlc[1];
	data->dai.num_platforms = 1;
	data->dai.codecs = &dlc[2];
	data->dai.num_codecs = 1;

	data->dai.name = "i.MX HDMI";
	data->dai.stream_name = "i.MX HDMI";
	data->dai.cpus->dai_name = dev_name(&cpu_pdev->dev);
	data->dai.platforms->of_node = cpu_np;
	data->dai.ops = &imx_hdmi_ops;
	data->dai.playback_only = true;
	data->dai.capture_only = false;
	data->dai.init = imx_hdmi_init;

	put_device(&cpu_pdev->dev);

	if (of_node_name_eq(cpu_np, "sai")) {
		data->cpu_priv.sysclk_id[1] = FSL_SAI_CLK_MAST1;
		data->cpu_priv.sysclk_id[0] = FSL_SAI_CLK_MAST1;
	}

	if (of_device_is_compatible(np, "fsl,imx-audio-sii902x")) {
		data->dai_fmt = SND_SOC_DAIFMT_LEFT_J;
		data->cpu_priv.slot_width = 24;
	} else {
		data->dai_fmt = SND_SOC_DAIFMT_I2S;
		data->cpu_priv.slot_width = 32;
	}

	if ((hdmi_out && hdmi_in) || (!hdmi_out && !hdmi_in)) {
		dev_err(&pdev->dev, "Invalid HDMI DAI link\n");
		ret = -EINVAL;
		goto fail;
	}

	if (hdmi_out) {
		data->dai.playback_only = true;
		data->dai.capture_only = false;
		data->dai.codecs->dai_name = "i2s-hifi";
		data->dai.codecs->name = "hdmi-audio-codec.1";
		data->dai.dai_fmt = data->dai_fmt |
				    SND_SOC_DAIFMT_NB_NF |
				    SND_SOC_DAIFMT_CBC_CFC;
	}

	if (hdmi_in) {
		data->dai.playback_only = false;
		data->dai.capture_only = true;
		data->dai.codecs->dai_name = "i2s-hifi";
		data->dai.codecs->name = "hdmi-audio-codec.2";
		data->dai.dai_fmt = data->dai_fmt |
				    SND_SOC_DAIFMT_NB_NF |
				    SND_SOC_DAIFMT_CBP_CFP;
	}

	data->card.dapm_widgets = imx_hdmi_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(imx_hdmi_widgets);
	data->card.dev = &pdev->dev;
	data->card.owner = THIS_MODULE;
	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret)
		goto fail;

	data->card.num_links = 1;
	data->card.dai_link = &data->dai;

	snd_soc_card_set_drvdata(&data->card, data);
	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		goto fail;
	}

fail:
	if (cpu_np)
		of_node_put(cpu_np);

	return ret;
}

static const struct of_device_id imx_hdmi_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-hdmi", },
	{ .compatible = "fsl,imx-audio-sii902x", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_hdmi_dt_ids);

static struct platform_driver imx_hdmi_driver = {
	.driver = {
		.name = "imx-hdmi",
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_hdmi_dt_ids,
	},
	.probe = imx_hdmi_probe,
};
module_platform_driver(imx_hdmi_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Freescale i.MX hdmi audio ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-hdmi");
