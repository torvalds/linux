// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, Linaro Limited.
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#include <linux/module.h>
#include <sound/jack.h>
#include <linux/input-event-codes.h>
#include "qdsp6/q6afe.h"
#include "common.h"

int qcom_snd_parse_of(struct snd_soc_card *card)
{
	struct device_node *np;
	struct device_node *codec = NULL;
	struct device_node *platform = NULL;
	struct device_node *cpu = NULL;
	struct device *dev = card->dev;
	struct snd_soc_dai_link *link;
	struct of_phandle_args args;
	struct snd_soc_dai_link_component *dlc;
	int ret, num_links;

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret == 0 && !card->name)
		/* Deprecated, only for compatibility with old device trees */
		ret = snd_soc_of_parse_card_name(card, "qcom,model");
	if (ret) {
		dev_err(dev, "Error parsing card name: %d\n", ret);
		return ret;
	}

	if (of_property_read_bool(dev->of_node, "widgets")) {
		ret = snd_soc_of_parse_audio_simple_widgets(card, "widgets");
		if (ret)
			return ret;
	}

	/* DAPM routes */
	if (of_property_read_bool(dev->of_node, "audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
		if (ret)
			return ret;
	}
	/* Deprecated, only for compatibility with old device trees */
	if (of_property_read_bool(dev->of_node, "qcom,audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card, "qcom,audio-routing");
		if (ret)
			return ret;
	}

	ret = snd_soc_of_parse_pin_switches(card, "pin-switches");
	if (ret)
		return ret;

	ret = snd_soc_of_parse_aux_devs(card, "aux-devs");
	if (ret)
		return ret;

	/* Populate links */
	num_links = of_get_available_child_count(dev->of_node);

	/* Allocate the DAI link array */
	card->dai_link = devm_kcalloc(dev, num_links, sizeof(*link), GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	card->num_links = num_links;
	link = card->dai_link;

	for_each_available_child_of_node(dev->of_node, np) {
		dlc = devm_kzalloc(dev, 2 * sizeof(*dlc), GFP_KERNEL);
		if (!dlc) {
			ret = -ENOMEM;
			goto err_put_np;
		}

		link->cpus	= &dlc[0];
		link->platforms	= &dlc[1];

		link->num_cpus		= 1;
		link->num_platforms	= 1;

		ret = of_property_read_string(np, "link-name", &link->name);
		if (ret) {
			dev_err(card->dev, "error getting codec dai_link name\n");
			goto err_put_np;
		}

		cpu = of_get_child_by_name(np, "cpu");
		platform = of_get_child_by_name(np, "platform");
		codec = of_get_child_by_name(np, "codec");

		if (!cpu) {
			dev_err(dev, "%s: Can't find cpu DT node\n", link->name);
			ret = -EINVAL;
			goto err;
		}

		ret = of_parse_phandle_with_args(cpu, "sound-dai",
					"#sound-dai-cells", 0, &args);
		if (ret) {
			dev_err(card->dev, "%s: error getting cpu phandle\n", link->name);
			goto err;
		}
		link->cpus->of_node = args.np;
		link->id = args.args[0];

		ret = snd_soc_of_get_dai_name(cpu, &link->cpus->dai_name);
		if (ret) {
			dev_err_probe(card->dev, ret,
				      "%s: error getting cpu dai name\n", link->name);
			goto err;
		}

		if (platform) {
			link->platforms->of_node = of_parse_phandle(platform,
					"sound-dai",
					0);
			if (!link->platforms->of_node) {
				dev_err(card->dev, "%s: platform dai not found\n", link->name);
				ret = -EINVAL;
				goto err;
			}
		} else {
			link->platforms->of_node = link->cpus->of_node;
		}

		if (codec) {
			ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);
			if (ret < 0) {
				dev_err_probe(card->dev, ret,
					      "%s: codec dai not found\n", link->name);
				goto err;
			}

			if (platform) {
				/* DPCM backend */
				link->no_pcm = 1;
				link->ignore_pmdown_time = 1;
			}
		} else {
			/* DPCM frontend */
			dlc = devm_kzalloc(dev, sizeof(*dlc), GFP_KERNEL);
			if (!dlc) {
				ret = -ENOMEM;
				goto err;
			}

			link->codecs	 = dlc;
			link->num_codecs = 1;

			link->codecs->dai_name = "snd-soc-dummy-dai";
			link->codecs->name = "snd-soc-dummy";
			link->dynamic = 1;
		}

		if (platform || !codec) {
			/* DPCM */
			snd_soc_dai_link_set_capabilities(link);
			link->ignore_suspend = 1;
			link->nonatomic = 1;
		}

		link->stream_name = link->name;
		link++;

		of_node_put(cpu);
		of_node_put(codec);
		of_node_put(platform);
	}

	return 0;
err:
	of_node_put(cpu);
	of_node_put(codec);
	of_node_put(platform);
err_put_np:
	of_node_put(np);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_snd_parse_of);

static struct snd_soc_jack_pin qcom_headset_jack_pins[] = {
	/* Headset */
	{
		.pin = "Mic Jack",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
};

int qcom_snd_wcd_jack_setup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_soc_jack *jack, bool *jack_setup)
{
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	int rval, i;

	if (!*jack_setup) {
		rval = snd_soc_card_jack_new_pins(card, "Headset Jack",
					     SND_JACK_HEADSET | SND_JACK_LINEOUT |
					     SND_JACK_MECHANICAL |
					     SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					     SND_JACK_BTN_2 | SND_JACK_BTN_3 |
					     SND_JACK_BTN_4 | SND_JACK_BTN_5,
					     jack, qcom_headset_jack_pins,
					     ARRAY_SIZE(qcom_headset_jack_pins));

		if (rval < 0) {
			dev_err(card->dev, "Unable to add Headphone Jack\n");
			return rval;
		}

		snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_MEDIA);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);
		*jack_setup = true;
	}

	switch (cpu_dai->id) {
	case TX_CODEC_DMA_TX_0:
	case TX_CODEC_DMA_TX_1:
	case TX_CODEC_DMA_TX_2:
	case TX_CODEC_DMA_TX_3:
		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			rval = snd_soc_component_set_jack(codec_dai->component,
							  jack, NULL);
			if (rval != 0 && rval != -ENOTSUPP) {
				dev_warn(card->dev, "Failed to set jack: %d\n", rval);
				return rval;
			}
		}

		break;
	default:
		break;
	}


	return 0;
}
EXPORT_SYMBOL_GPL(qcom_snd_wcd_jack_setup);
MODULE_LICENSE("GPL v2");
