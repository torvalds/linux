// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, Linaro Limited.
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#include <linux/module.h>
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
	if (ret) {
		dev_err(dev, "Error parsing card name: %d\n", ret);
		return ret;
	}

	/* DAPM routes */
	if (of_property_read_bool(dev->of_node, "audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card,
				"audio-routing");
		if (ret)
			return ret;
	}

	/* Populate links */
	num_links = of_get_child_count(dev->of_node);

	/* Allocate the DAI link array */
	card->dai_link = kcalloc(num_links, sizeof(*link), GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	card->num_links = num_links;
	link = card->dai_link;

	for_each_child_of_node(dev->of_node, np) {
		dlc = devm_kzalloc(dev, 2 * sizeof(*dlc), GFP_KERNEL);
		if (!dlc)
			return -ENOMEM;

		link->cpus	= &dlc[0];
		link->platforms	= &dlc[1];

		link->num_cpus		= 1;
		link->num_platforms	= 1;

		cpu = of_get_child_by_name(np, "cpu");
		platform = of_get_child_by_name(np, "platform");
		codec = of_get_child_by_name(np, "codec");

		if (!cpu) {
			dev_err(dev, "Can't find cpu DT node\n");
			ret = -EINVAL;
			goto err;
		}

		ret = of_parse_phandle_with_args(cpu, "sound-dai",
					"#sound-dai-cells", 0, &args);
		if (ret) {
			dev_err(card->dev, "error getting cpu phandle\n");
			goto err;
		}
		link->cpus->of_node = args.np;
		link->id = args.args[0];

		ret = snd_soc_of_get_dai_name(cpu, &link->cpus->dai_name);
		if (ret) {
			dev_err(card->dev, "error getting cpu dai name\n");
			goto err;
		}

		if (codec && platform) {
			link->platforms->of_node = of_parse_phandle(platform,
					"sound-dai",
					0);
			if (!link->platforms->of_node) {
				dev_err(card->dev, "platform dai not found\n");
				ret = -EINVAL;
				goto err;
			}

			ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);
			if (ret < 0) {
				dev_err(card->dev, "codec dai not found\n");
				goto err;
			}
			link->no_pcm = 1;
			link->ignore_pmdown_time = 1;
		} else {
			dlc = devm_kzalloc(dev, sizeof(*dlc), GFP_KERNEL);
			if (!dlc)
				return -ENOMEM;

			link->codecs	 = dlc;
			link->num_codecs = 1;

			link->platforms->of_node = link->cpus->of_node;
			link->codecs->dai_name = "snd-soc-dummy-dai";
			link->codecs->name = "snd-soc-dummy";
			link->dynamic = 1;
		}

		link->ignore_suspend = 1;
		ret = of_property_read_string(np, "link-name", &link->name);
		if (ret) {
			dev_err(card->dev, "error getting codec dai_link name\n");
			goto err;
		}

		link->dpcm_playback = 1;
		link->dpcm_capture = 1;
		link->stream_name = link->name;
		link++;

		of_node_put(cpu);
		of_node_put(codec);
		of_node_put(platform);
	}

	return 0;
err:
	of_node_put(np);
	of_node_put(cpu);
	of_node_put(codec);
	of_node_put(platform);
	kfree(card->dai_link);
	return ret;
}
EXPORT_SYMBOL(qcom_snd_parse_of);

MODULE_LICENSE("GPL v2");
