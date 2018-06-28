// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, Linaro Limited

#include <linux/soc/qcom/apr.h>
#include <linux/module.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>

static int apq8096_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				      struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	return 0;
}

static int apq8096_sbc_parse_of(struct snd_soc_card *card)
{
	struct device_node *np;
	struct device_node *codec = NULL;
	struct device_node *platform = NULL;
	struct device_node *cpu = NULL;
	struct device *dev = card->dev;
	struct snd_soc_dai_link *link;
	int ret, num_links;

	ret = snd_soc_of_parse_card_name(card, "qcom,model");
	if (ret) {
		dev_err(dev, "Error parsing card name: %d\n", ret);
		return ret;
	}

	/* DAPM routes */
	if (of_property_read_bool(dev->of_node, "qcom,audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card,
					"qcom,audio-routing");
		if (ret)
			return ret;
	}

	/* Populate links */
	num_links = of_get_child_count(dev->of_node);

	/* Allocate the DAI link array */
	card->dai_link = kcalloc(num_links, sizeof(*link), GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	card->num_links	= num_links;
	link = card->dai_link;

	for_each_child_of_node(dev->of_node, np) {
		cpu = of_get_child_by_name(np, "cpu");
		if (!cpu) {
			dev_err(dev, "Can't find cpu DT node\n");
			ret = -EINVAL;
			goto err;
		}

		link->cpu_of_node = of_parse_phandle(cpu, "sound-dai", 0);
		if (!link->cpu_of_node) {
			dev_err(card->dev, "error getting cpu phandle\n");
			ret = -EINVAL;
			goto err;
		}

		ret = snd_soc_of_get_dai_name(cpu, &link->cpu_dai_name);
		if (ret) {
			dev_err(card->dev, "error getting cpu dai name\n");
			goto err;
		}

		platform = of_get_child_by_name(np, "platform");
		codec = of_get_child_by_name(np, "codec");
		if (codec && platform) {
			link->platform_of_node = of_parse_phandle(platform,
								  "sound-dai",
								   0);
			if (!link->platform_of_node) {
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
			link->be_hw_params_fixup = apq8096_be_hw_params_fixup;
		} else {
			link->platform_of_node = link->cpu_of_node;
			link->codec_dai_name = "snd-soc-dummy-dai";
			link->codec_name = "snd-soc-dummy";
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
	}

	return 0;
err:
	of_node_put(cpu);
	of_node_put(codec);
	of_node_put(platform);
	kfree(card->dai_link);
	return ret;
}

static int apq8096_bind(struct device *dev)
{
	struct snd_soc_card *card;
	int ret;

	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	component_bind_all(dev, card);
	card->dev = dev;
	ret = apq8096_sbc_parse_of(card);
	if (ret) {
		dev_err(dev, "Error parsing OF data\n");
		goto err;
	}

	ret = snd_soc_register_card(card);
	if (ret)
		goto err;

	return 0;

err:
	component_unbind_all(dev, card);
	kfree(card);
	return ret;
}

static void apq8096_unbind(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);

	snd_soc_unregister_card(card);
	component_unbind_all(dev, card);
	kfree(card->dai_link);
	kfree(card);
}

static const struct component_master_ops apq8096_ops = {
	.bind = apq8096_bind,
	.unbind = apq8096_unbind,
};

static int apq8016_compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static void apq8016_release_of(struct device *dev, void *data)
{
	of_node_put(data);
}

static int add_audio_components(struct device *dev,
				struct component_match **matchptr)
{
	struct device_node *np, *platform, *cpu, *node, *dai_node;

	node = dev->of_node;

	for_each_child_of_node(node, np) {
		cpu = of_get_child_by_name(np, "cpu");
		if (cpu) {
			dai_node = of_parse_phandle(cpu, "sound-dai", 0);
			of_node_get(dai_node);
			component_match_add_release(dev, matchptr,
						    apq8016_release_of,
						    apq8016_compare_of,
						    dai_node);
		}

		platform = of_get_child_by_name(np, "platform");
		if (platform) {
			dai_node = of_parse_phandle(platform, "sound-dai", 0);
			component_match_add_release(dev, matchptr,
						    apq8016_release_of,
						    apq8016_compare_of,
						    dai_node);
		}
	}

	return 0;
}

static int apq8096_platform_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	int ret;

	ret = add_audio_components(&pdev->dev, &match);
	if (ret)
		return ret;

	return component_master_add_with_match(&pdev->dev, &apq8096_ops, match);
}

static int apq8096_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &apq8096_ops);

	return 0;
}

static const struct of_device_id msm_snd_apq8096_dt_match[] = {
	{.compatible = "qcom,apq8096-sndcard"},
	{}
};

MODULE_DEVICE_TABLE(of, msm_snd_apq8096_dt_match);

static struct platform_driver msm_snd_apq8096_driver = {
	.probe  = apq8096_platform_probe,
	.remove = apq8096_platform_remove,
	.driver = {
		.name = "msm-snd-apq8096",
		.owner = THIS_MODULE,
		.of_match_table = msm_snd_apq8096_dt_match,
	},
};
module_platform_driver(msm_snd_apq8096_driver);
MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org");
MODULE_DESCRIPTION("APQ8096 ASoC Machine Driver");
MODULE_LICENSE("GPL v2");
