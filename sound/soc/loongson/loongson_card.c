// SPDX-License-Identifier: GPL-2.0
//
// Loongson ASoC Audio Machine driver
//
// Copyright (C) 2023 Loongson Technology Corporation Limited
// Author: Yingkun Meng <mengyingkun@loongson.cn>
//

#include <linux/module.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <sound/pcm_params.h>

static char codec_name[SND_ACPI_I2C_ID_LEN];

struct loongson_card_data {
	struct snd_soc_card snd_card;
	unsigned int mclk_fs;
};

static int loongson_card_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct loongson_card_data *ls_card = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int ret, mclk;

	if (!ls_card->mclk_fs)
		return 0;

	mclk = ls_card->mclk_fs * params_rate(params);
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(codec_dai->dev, "cpu_dai clock not set\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "codec_dai clock not set\n");
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops loongson_ops = {
	.hw_params = loongson_card_hw_params,
};

SND_SOC_DAILINK_DEFS(analog,
	DAILINK_COMP_ARRAY(COMP_CPU("loongson-i2s")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link loongson_dai_links[] = {
	{
		.name = "Loongson Audio Port",
		.stream_name = "Loongson Audio",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_IB_NF
			| SND_SOC_DAIFMT_CBC_CFC,
		SND_SOC_DAILINK_REG(analog),
		.ops = &loongson_ops,
	},
};

static struct acpi_device *loongson_card_acpi_find_device(struct snd_soc_card *card,
							  const char *name)
{
	struct fwnode_handle *fwnode = card->dev->fwnode;
	struct fwnode_reference_args args;
	int status;

	memset(&args, 0, sizeof(args));
	status = acpi_node_get_property_reference(fwnode, name, 0, &args);
	if (status || !is_acpi_device_node(args.fwnode)) {
		dev_err(card->dev, "No matching phy in ACPI table\n");
		return NULL;
	}

	return to_acpi_device_node(args.fwnode);
}

static int loongson_card_parse_acpi(struct loongson_card_data *data)
{
	struct snd_soc_card *card = &data->snd_card;
	const char *codec_dai_name;
	struct acpi_device *adev;
	struct device *phy_dev;
	int i;

	/* fixup platform name based on reference node */
	adev = loongson_card_acpi_find_device(card, "cpu");
	if (!adev)
		return -ENOENT;

	phy_dev = acpi_get_first_physical_node(adev);
	if (!phy_dev)
		return -EPROBE_DEFER;

	/* fixup codec name based on reference node */
	adev = loongson_card_acpi_find_device(card, "codec");
	if (!adev)
		return -ENOENT;
	snprintf(codec_name, sizeof(codec_name), "i2c-%s", acpi_dev_name(adev));

	device_property_read_string(card->dev, "codec-dai-name", &codec_dai_name);

	for (i = 0; i < card->num_links; i++) {
		loongson_dai_links[i].platforms->name = dev_name(phy_dev);
		loongson_dai_links[i].codecs->name = codec_name;
		loongson_dai_links[i].codecs->dai_name = codec_dai_name;
	}

	return 0;
}

static int loongson_card_parse_of(struct loongson_card_data *data)
{
	struct device_node *cpu, *codec;
	struct snd_soc_card *card = &data->snd_card;
	struct device *dev = card->dev;
	int ret, i;

	cpu = of_get_child_by_name(dev->of_node, "cpu");
	if (!cpu) {
		dev_err(dev, "platform property missing or invalid\n");
		return -EINVAL;
	}
	codec = of_get_child_by_name(dev->of_node, "codec");
	if (!codec) {
		dev_err(dev, "audio-codec property missing or invalid\n");
		of_node_put(cpu);
		return -EINVAL;
	}

	for (i = 0; i < card->num_links; i++) {
		ret = snd_soc_of_get_dlc(cpu, NULL, loongson_dai_links[i].cpus, 0);
		if (ret < 0) {
			dev_err(dev, "getting cpu dlc error (%d)\n", ret);
			goto err;
		}
		loongson_dai_links[i].platforms->of_node = loongson_dai_links[i].cpus->of_node;

		ret = snd_soc_of_get_dlc(codec, NULL, loongson_dai_links[i].codecs, 0);
		if (ret < 0) {
			dev_err(dev, "getting codec dlc error (%d)\n", ret);
			goto err;
		}
	}

	of_node_put(cpu);
	of_node_put(codec);

	return 0;

err:
	of_node_put(cpu);
	of_node_put(codec);
	return ret;
}

static int loongson_asoc_card_probe(struct platform_device *pdev)
{
	struct loongson_card_data *ls_priv;
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	int ret;

	ls_priv = devm_kzalloc(dev, sizeof(*ls_priv), GFP_KERNEL);
	if (!ls_priv)
		return -ENOMEM;

	card = &ls_priv->snd_card;

	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dai_link = loongson_dai_links;
	card->num_links = ARRAY_SIZE(loongson_dai_links);
	snd_soc_card_set_drvdata(card, ls_priv);

	ret = device_property_read_string(dev, "model", &card->name);
	if (ret)
		return dev_err_probe(dev, ret, "Error parsing card name\n");

	ret = device_property_read_u32(dev, "mclk-fs", &ls_priv->mclk_fs);
	if (ret)
		return dev_err_probe(dev, ret, "Error parsing mclk-fs\n");

	ret = has_acpi_companion(dev) ? loongson_card_parse_acpi(ls_priv)
				      : loongson_card_parse_of(ls_priv);
	if (ret)
		return dev_err_probe(dev, ret, "Error parsing acpi/of properties\n");

	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id loongson_asoc_dt_ids[] = {
	{ .compatible = "loongson,ls-audio-card" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, loongson_asoc_dt_ids);

static struct platform_driver loongson_audio_driver = {
	.probe = loongson_asoc_card_probe,
	.driver = {
		.name = "loongson-asoc-card",
		.pm = &snd_soc_pm_ops,
		.of_match_table = loongson_asoc_dt_ids,
	},
};
module_platform_driver(loongson_audio_driver);

MODULE_DESCRIPTION("Loongson ASoc Sound Card driver");
MODULE_AUTHOR("Loongson Technology Corporation Limited");
MODULE_LICENSE("GPL");
