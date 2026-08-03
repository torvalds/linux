// SPDX-License-Identifier: GPL-2.0
//
// Loongson ASoC Audio Machine driver
//
// Copyright (C) 2023-2026 Loongson Technology Corporation Limited
// Author: Yingkun Meng <mengyingkun@loongson.cn>
//         Binbin Zhou <zhoubinbin@loongson.cn>
//

#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>

static char codec_name[SND_ACPI_I2C_ID_LEN];

struct loongson_card_data {
	struct snd_soc_card snd_card;
	unsigned int mclk_fs;
	struct gpio_desc *gpiod_hp_det;
	struct gpio_desc *gpiod_hp_ctl;
	struct gpio_desc *gpiod_spkr_en;
	const struct loongson_card_config *cfg;
};

struct loongson_card_config {
	unsigned int fmt;
	bool add_hp_jack;
	bool add_dapm_widgets;
	bool add_dapm_routes;
};

static const struct loongson_card_config ls2k1000_card_config = {
	.fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_CBC_CFC,
	.add_hp_jack = false,
	.add_dapm_widgets = false,
	.add_dapm_routes = false,
};

static const struct loongson_card_config ls2k0300_forever_pi_card_config = {
	.fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC,
	.add_hp_jack = false,
	.add_dapm_widgets = false,
	.add_dapm_routes = false,
};

static const struct loongson_card_config ls2k0300_dl2k0300b_card_config = {
	.fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC,
	.add_hp_jack = true,
	.add_dapm_widgets = true,
	.add_dapm_routes = true,
};

static int tegra_machine_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *k, int event)
{
	struct snd_soc_card *card = snd_soc_dapm_to_card(w->dapm);
	struct loongson_card_data *priv = snd_soc_card_get_drvdata(card);

	if (!snd_soc_dapm_widget_name_cmp(w, "Speaker"))
		gpiod_set_value_cansleep(priv->gpiod_spkr_en,
					 SND_SOC_DAPM_EVENT_ON(event));

	if (!snd_soc_dapm_widget_name_cmp(w, "Headphone"))
		gpiod_set_value_cansleep(priv->gpiod_hp_ctl,
					 SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static const struct snd_soc_dapm_widget loongson_aosc_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", tegra_machine_event),
	SND_SOC_DAPM_SPK("Speaker", tegra_machine_event),
};

/* Headphones Jack */

static struct snd_soc_jack loongson_asoc_hp_jack;

static struct snd_soc_jack_pin loongson_asoc_hp_jack_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE
	},
	{
		.pin = "Speaker",
		.mask = SND_JACK_HEADPHONE,
		.invert = 1
	},
};

static struct snd_soc_jack_gpio loongson_asoc_hp_jack_gpio = {
	.name = "Headphones detection",
	.report = SND_JACK_HEADPHONE,
	.debounce_time = 150,
};

static int loongson_asoc_machine_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct loongson_card_data *ls_priv = snd_soc_card_get_drvdata(card);
	int ret = 0;

	if (!ls_priv->cfg->add_hp_jack || !ls_priv->gpiod_hp_det)
		return 0;

	ret = snd_soc_card_jack_new_pins(card, "Headphones Jack",
					 SND_JACK_HEADPHONE,
					 &loongson_asoc_hp_jack,
					 loongson_asoc_hp_jack_pins,
					 ARRAY_SIZE(loongson_asoc_hp_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headphones Jack creation failed: %d\n", ret);
		return ret;
	}

	loongson_asoc_hp_jack_gpio.desc = ls_priv->gpiod_hp_det;

	ret = snd_soc_jack_add_gpios(&loongson_asoc_hp_jack, 1, &loongson_asoc_hp_jack_gpio);
	if (ret)
		dev_err(rtd->dev, "Headphone GPIO not added: %d\n", ret);

	return ret;
}

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

	return snd_soc_runtime_set_dai_fmt(rtd, ls_card->cfg->fmt);
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
		.init = loongson_asoc_machine_init,
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
	int i, ret;

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

	ret = device_property_read_string(card->dev, "codec-dai-name", &codec_dai_name);
	if (ret)
		return ret;

	for (i = 0; i < card->num_links; i++) {
		loongson_dai_links[i].platforms->name = dev_name(phy_dev);
		loongson_dai_links[i].codecs->name = codec_name;
		loongson_dai_links[i].codecs->dai_name = codec_dai_name;
	}

	return 0;
}

static int loongson_card_parse_of(struct loongson_card_data *data)
{
	struct snd_soc_card *card = &data->snd_card;
	struct device_node *cpu, *codec;
	struct device *dev = card->dev;
	int ret, i;

	data->gpiod_hp_det = devm_gpiod_get_optional(dev, "hp-det", GPIOD_IN);
	if (IS_ERR(data->gpiod_hp_det))
		return PTR_ERR(data->gpiod_hp_det);

	data->gpiod_hp_ctl = devm_gpiod_get_optional(dev, "hp-ctl", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_hp_ctl))
		return PTR_ERR(data->gpiod_hp_ctl);

	data->gpiod_spkr_en = devm_gpiod_get_optional(dev, "spkr-en", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_spkr_en))
		return PTR_ERR(data->gpiod_spkr_en);

	if (data->cfg->add_dapm_routes) {
		ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
		if (ret)
			return ret;
	}

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

	ls_priv->cfg = (const struct loongson_card_config *)device_get_match_data(dev);
	if (!ls_priv->cfg)
		return -EINVAL;

	card = &ls_priv->snd_card;

	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dai_link = loongson_dai_links;
	card->num_links = ARRAY_SIZE(loongson_dai_links);

	if (ls_priv->cfg->add_dapm_widgets) {
		card->dapm_widgets = loongson_aosc_dapm_widgets;
		card->num_dapm_widgets = ARRAY_SIZE(loongson_aosc_dapm_widgets);
	}

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
	/* Loongson-2K1000/Loongson-2K2000/LS7A */
	{
		.compatible = "loongson,ls-audio-card",
		.data = &ls2k1000_card_config
	},
	{
		.compatible = "loongson,ls2k0300-forever-pi-audio-card",
		.data = &ls2k0300_forever_pi_card_config
	},
	{
		.compatible = "loongson,ls2k0300-dl2k0300b-audio-card",
		.data = &ls2k0300_dl2k0300b_card_config
	},
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
