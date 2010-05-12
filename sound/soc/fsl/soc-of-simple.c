/*
 * OF helpers for ALSA SoC Layer
 *
 * Copyright (C) 2008, Secret Lab Technologies Ltd.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-of-simple.h>
#include <sound/initval.h>

MODULE_AUTHOR("Grant Likely <grant.likely@secretlab.ca>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ALSA SoC OpenFirmware bindings");

static DEFINE_MUTEX(of_snd_soc_mutex);
static LIST_HEAD(of_snd_soc_device_list);
static int of_snd_soc_next_index;

struct of_snd_soc_device {
	int id;
	struct list_head list;
	struct snd_soc_device device;
	struct snd_soc_card card;
	struct snd_soc_dai_link dai_link;
	struct platform_device *pdev;
	struct device_node *platform_node;
	struct device_node *codec_node;
};

static struct snd_soc_ops of_snd_soc_ops = {
};

static struct of_snd_soc_device *
of_snd_soc_get_device(struct device_node *codec_node)
{
	struct of_snd_soc_device *of_soc;

	list_for_each_entry(of_soc, &of_snd_soc_device_list, list) {
		if (of_soc->codec_node == codec_node)
			return of_soc;
	}

	of_soc = kzalloc(sizeof(struct of_snd_soc_device), GFP_KERNEL);
	if (!of_soc)
		return NULL;

	/* Initialize the structure and add it to the global list */
	of_soc->codec_node = codec_node;
	of_soc->id = of_snd_soc_next_index++;
	of_soc->card.dai_link = &of_soc->dai_link;
	of_soc->card.num_links = 1;
	of_soc->device.card = &of_soc->card;
	of_soc->dai_link.ops = &of_snd_soc_ops;
	list_add(&of_soc->list, &of_snd_soc_device_list);

	return of_soc;
}

static void of_snd_soc_register_device(struct of_snd_soc_device *of_soc)
{
	struct platform_device *pdev;
	int rc;

	/* Only register the device if both the codec and platform have
	 * been registered */
	if ((!of_soc->device.codec_data) || (!of_soc->platform_node))
		return;

	pr_info("platform<-->codec match achieved; registering machine\n");

	pdev = platform_device_alloc("soc-audio", of_soc->id);
	if (!pdev) {
		pr_err("of_soc: platform_device_alloc() failed\n");
		return;
	}

	pdev->dev.platform_data = of_soc;
	platform_set_drvdata(pdev, &of_soc->device);
	of_soc->device.dev = &pdev->dev;

	/* The ASoC device is complete; register it */
	rc = platform_device_add(pdev);
	if (rc) {
		pr_err("of_soc: platform_device_add() failed\n");
		return;
	}

}

int of_snd_soc_register_codec(struct snd_soc_codec_device *codec_dev,
			      void *codec_data, struct snd_soc_dai *dai,
			      struct device_node *node)
{
	struct of_snd_soc_device *of_soc;
	int rc = 0;

	pr_info("registering ASoC codec driver: %s\n", node->full_name);

	mutex_lock(&of_snd_soc_mutex);
	of_soc = of_snd_soc_get_device(node);
	if (!of_soc) {
		rc = -ENOMEM;
		goto out;
	}

	/* Store the codec data */
	of_soc->device.codec_data = codec_data;
	of_soc->device.codec_dev = codec_dev;
	of_soc->dai_link.name = (char *)node->name;
	of_soc->dai_link.stream_name = (char *)node->name;
	of_soc->dai_link.codec_dai = dai;

	/* Now try to register the SoC device */
	of_snd_soc_register_device(of_soc);

 out:
	mutex_unlock(&of_snd_soc_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(of_snd_soc_register_codec);

int of_snd_soc_register_platform(struct snd_soc_platform *platform,
				 struct device_node *node,
				 struct snd_soc_dai *cpu_dai)
{
	struct of_snd_soc_device *of_soc;
	struct device_node *codec_node;
	const phandle *handle;
	int len, rc = 0;

	pr_info("registering ASoC platform driver: %s\n", node->full_name);

	handle = of_get_property(node, "codec-handle", &len);
	if (!handle || len < sizeof(handle))
		return -ENODEV;
	codec_node = of_find_node_by_phandle(*handle);
	if (!codec_node)
		return -ENODEV;
	pr_info("looking for codec: %s\n", codec_node->full_name);

	mutex_lock(&of_snd_soc_mutex);
	of_soc = of_snd_soc_get_device(codec_node);
	if (!of_soc) {
		rc = -ENOMEM;
		goto out;
	}

	of_soc->platform_node = node;
	of_soc->dai_link.cpu_dai = cpu_dai;
	of_soc->card.platform = platform;
	of_soc->card.name = of_soc->dai_link.cpu_dai->name;

	/* Now try to register the SoC device */
	of_snd_soc_register_device(of_soc);

 out:
	mutex_unlock(&of_snd_soc_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(of_snd_soc_register_platform);
