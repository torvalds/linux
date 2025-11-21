// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2024 Advanced Micro Devices, Inc.

/*
 *  acp-sdw-legacy-mach - ASoC legacy Machine driver for AMD SoundWire platforms
 */

#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "soc_amd_sdw_common.h"
#include "../../codecs/rt711.h"

static unsigned long soc_sdw_quirk = RT711_JD1;
static int quirk_override = -1;
module_param_named(quirk, quirk_override, int, 0444);
MODULE_PARM_DESC(quirk, "Board-specific quirk override");

static void log_quirks(struct device *dev)
{
	if (SOC_JACK_JDSRC(soc_sdw_quirk))
		dev_dbg(dev, "quirk realtek,jack-detect-source %ld\n",
			SOC_JACK_JDSRC(soc_sdw_quirk));
	if (soc_sdw_quirk & ASOC_SDW_ACP_DMIC)
		dev_dbg(dev, "quirk SOC_SDW_ACP_DMIC enabled\n");
	if (soc_sdw_quirk & ASOC_SDW_CODEC_SPKR)
		dev_dbg(dev, "quirk ASOC_SDW_CODEC_SPKR enabled\n");
}

static int soc_sdw_quirk_cb(const struct dmi_system_id *id)
{
	soc_sdw_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id soc_sdw_quirk_table[] = {
	{
		.callback = soc_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AMD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Birman-PHX"),
		},
		.driver_data = (void *)RT711_JD2,
	},
	{
		.callback = soc_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0D80"),
		},
		.driver_data = (void *)(ASOC_SDW_CODEC_SPKR),
	},
	{
		.callback = soc_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0D81"),
		},
		.driver_data = (void *)(ASOC_SDW_CODEC_SPKR),
	},
	{
		.callback = soc_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0D82"),
		},
		.driver_data = (void *)(ASOC_SDW_CODEC_SPKR),
	},
	{
		.callback = soc_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0D83"),
		},
		.driver_data = (void *)(ASOC_SDW_CODEC_SPKR),
	},
	{
		.callback = soc_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0DD3"),
		},
		.driver_data = (void *)(ASOC_SDW_CODEC_SPKR),
	},
	{
		.callback = soc_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0DD4"),
		},
		.driver_data = (void *)(ASOC_SDW_CODEC_SPKR),
	},
	{}
};

static const struct snd_soc_ops sdw_ops = {
	.startup = asoc_sdw_startup,
	.prepare = asoc_sdw_prepare,
	.trigger = asoc_sdw_trigger,
	.hw_params = asoc_sdw_hw_params,
	.hw_free = asoc_sdw_hw_free,
	.shutdown = asoc_sdw_shutdown,
};

static const char * const type_strings[] = {"SimpleJack", "SmartAmp", "SmartMic"};

static int create_sdw_dailink(struct snd_soc_card *card,
			      struct asoc_sdw_dailink *soc_dai,
			      struct snd_soc_dai_link **dai_links,
			      int *be_id, struct snd_soc_codec_conf **codec_conf,
			      struct snd_soc_dai_link_component *sdw_platform_component)
{
	struct device *dev = card->dev;
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct amd_mc_ctx *amd_ctx = (struct amd_mc_ctx *)ctx->private;
	struct asoc_sdw_endpoint *soc_end;
	int cpu_pin_id;
	int stream;
	int ret;

	list_for_each_entry(soc_end, &soc_dai->endpoints, list) {
		if (soc_end->name_prefix) {
			(*codec_conf)->dlc.name = soc_end->codec_name;
			(*codec_conf)->name_prefix = soc_end->name_prefix;
			(*codec_conf)++;
		}

		if (soc_end->include_sidecar) {
			ret = soc_end->codec_info->add_sidecar(card, dai_links, codec_conf);
			if (ret)
				return ret;
		}
	}

	for_each_pcm_streams(stream) {
		static const char * const sdw_stream_name[] = {
			"SDW%d-PIN%d-PLAYBACK",
			"SDW%d-PIN%d-CAPTURE",
			"SDW%d-PIN%d-PLAYBACK-%s",
			"SDW%d-PIN%d-CAPTURE-%s",
		};
		struct snd_soc_dai_link_ch_map *codec_maps;
		struct snd_soc_dai_link_component *codecs;
		struct snd_soc_dai_link_component *cpus;
		int num_cpus = hweight32(soc_dai->link_mask[stream]);
		int num_codecs = soc_dai->num_devs[stream];
		int playback, capture;
		int j = 0;
		char *name;

		if (!soc_dai->num_devs[stream])
			continue;

		soc_end = list_first_entry(&soc_dai->endpoints,
					   struct asoc_sdw_endpoint, list);

		*be_id = soc_end->dai_info->dailink[stream];
		if (*be_id < 0) {
			dev_err(dev, "Invalid dailink id %d\n", *be_id);
			return -EINVAL;
		}

		switch (amd_ctx->acp_rev) {
		case ACP63_PCI_REV:
			ret = get_acp63_cpu_pin_id(ffs(soc_end->link_mask - 1),
						   *be_id, &cpu_pin_id, dev);
			if (ret)
				return ret;
			break;
		case ACP70_PCI_REV:
		case ACP71_PCI_REV:
		case ACP72_PCI_REV:
			ret = get_acp70_cpu_pin_id(ffs(soc_end->link_mask - 1),
						   *be_id, &cpu_pin_id, dev);
			if (ret)
				return ret;
			break;
		default:
			return -EINVAL;
		}
		/* create stream name according to first link id */
		if (ctx->append_dai_type) {
			name = devm_kasprintf(dev, GFP_KERNEL,
					      sdw_stream_name[stream + 2],
					      ffs(soc_end->link_mask) - 1,
					      cpu_pin_id,
					      type_strings[soc_end->dai_info->dai_type]);
		} else {
			name = devm_kasprintf(dev, GFP_KERNEL,
					      sdw_stream_name[stream],
					      ffs(soc_end->link_mask) - 1,
					      cpu_pin_id);
		}
		if (!name)
			return -ENOMEM;

		cpus = devm_kcalloc(dev, num_cpus, sizeof(*cpus), GFP_KERNEL);
		if (!cpus)
			return -ENOMEM;

		codecs = devm_kcalloc(dev, num_codecs, sizeof(*codecs), GFP_KERNEL);
		if (!codecs)
			return -ENOMEM;

		codec_maps = devm_kcalloc(dev, num_codecs, sizeof(*codec_maps), GFP_KERNEL);
		if (!codec_maps)
			return -ENOMEM;

		list_for_each_entry(soc_end, &soc_dai->endpoints, list) {
			if (!soc_end->dai_info->direction[stream])
				continue;

			int link_num = ffs(soc_end->link_mask) - 1;

			cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
							"SDW%d Pin%d",
							link_num, cpu_pin_id);
			dev_dbg(dev, "cpu->dai_name:%s\n", cpus->dai_name);
			if (!cpus->dai_name)
				return -ENOMEM;

			codec_maps[j].cpu = 0;
			codec_maps[j].codec = j;

			codecs[j].name = soc_end->codec_name;
			codecs[j].dai_name = soc_end->dai_info->dai_name;
			j++;
		}

		WARN_ON(j != num_codecs);

		playback = (stream == SNDRV_PCM_STREAM_PLAYBACK);
		capture = (stream == SNDRV_PCM_STREAM_CAPTURE);

		asoc_sdw_init_dai_link(dev, *dai_links, be_id, name, playback, capture,
				       cpus, num_cpus, sdw_platform_component,
				       1, codecs, num_codecs,
				       0, asoc_sdw_rtd_init, &sdw_ops);
		/*
		 * SoundWire DAILINKs use 'stream' functions and Bank Switch operations
		 * based on wait_for_completion(), tag them as 'nonatomic'.
		 */
		(*dai_links)->nonatomic = true;
		(*dai_links)->ch_maps = codec_maps;

		list_for_each_entry(soc_end, &soc_dai->endpoints, list) {
			if (soc_end->dai_info->init)
				soc_end->dai_info->init(card, *dai_links,
							soc_end->codec_info,
							playback);
		}

		(*dai_links)++;
	}

	return 0;
}

static int create_sdw_dailinks(struct snd_soc_card *card,
			       struct snd_soc_dai_link **dai_links, int *be_id,
			       struct asoc_sdw_dailink *soc_dais,
			       struct snd_soc_codec_conf **codec_conf)
{
	struct device *dev = card->dev;
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct amd_mc_ctx *amd_ctx = (struct amd_mc_ctx *)ctx->private;
	struct snd_soc_dai_link_component *sdw_platform_component;
	int ret;

	sdw_platform_component = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link_component),
					      GFP_KERNEL);
	if (!sdw_platform_component)
		return -ENOMEM;

	switch (amd_ctx->acp_rev) {
	case ACP63_PCI_REV:
	case ACP70_PCI_REV:
	case ACP71_PCI_REV:
	case ACP72_PCI_REV:
		sdw_platform_component->name = "amd_ps_sdw_dma.0";
		break;
	default:
		return -EINVAL;
	}

	/* generate DAI links by each sdw link */
	while (soc_dais->initialised) {
		int current_be_id = 0;

		ret = create_sdw_dailink(card, soc_dais, dai_links,
					 &current_be_id, codec_conf, sdw_platform_component);
		if (ret)
			return ret;

		/* Update the be_id to match the highest ID used for SDW link */
		if (*be_id < current_be_id)
			*be_id = current_be_id;

		soc_dais++;
	}

	return 0;
}

static int create_dmic_dailinks(struct snd_soc_card *card,
				struct snd_soc_dai_link **dai_links, int *be_id, int no_pcm)
{
	struct device *dev = card->dev;
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct amd_mc_ctx *amd_ctx = (struct amd_mc_ctx *)ctx->private;
	struct snd_soc_dai_link_component *pdm_cpu;
	struct snd_soc_dai_link_component *pdm_platform;
	int ret;

	pdm_cpu = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
	if (!pdm_cpu)
		return -ENOMEM;

	pdm_platform = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
	if (!pdm_platform)
		return -ENOMEM;

	switch (amd_ctx->acp_rev) {
	case ACP63_PCI_REV:
	case ACP70_PCI_REV:
	case ACP71_PCI_REV:
	case ACP72_PCI_REV:
		pdm_cpu->name = "acp_ps_pdm_dma.0";
		pdm_platform->name = "acp_ps_pdm_dma.0";
		break;
	default:
		return -EINVAL;
	}

	*be_id = ACP_DMIC_BE_ID;
	ret = asoc_sdw_init_simple_dai_link(dev, *dai_links, be_id, "acp-dmic-codec",
					    0, 1, // DMIC only supports capture
					    pdm_cpu->name, pdm_platform->name,
					    "dmic-codec.0", "dmic-hifi", no_pcm,
					    asoc_sdw_dmic_init, NULL);
	if (ret)
		return ret;

	(*dai_links)++;

	return 0;
}

static int soc_card_dai_links_create(struct snd_soc_card *card)
{
	struct device *dev = card->dev;
	struct snd_soc_acpi_mach *mach = dev_get_platdata(card->dev);
	int sdw_be_num = 0, dmic_num = 0;
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_acpi_mach_params *mach_params = &mach->mach_params;
	struct asoc_sdw_endpoint *soc_ends __free(kfree) = NULL;
	struct asoc_sdw_dailink *soc_dais __free(kfree) = NULL;
	struct snd_soc_codec_conf *codec_conf;
	struct snd_soc_dai_link *dai_links;
	int num_devs = 0;
	int num_ends = 0;
	int num_links;
	int be_id = 0;
	int ret;

	ret = asoc_sdw_count_sdw_endpoints(card, &num_devs, &num_ends);
	if (ret < 0) {
		dev_err(dev, "failed to count devices/endpoints: %d\n", ret);
		return ret;
	}

	/* One per DAI link, worst case is a DAI link for every endpoint */
	soc_dais = kcalloc(num_ends, sizeof(*soc_dais), GFP_KERNEL);
	if (!soc_dais)
		return -ENOMEM;

	/* One per endpoint, ie. each DAI on each codec/amp */
	soc_ends = kcalloc(num_ends, sizeof(*soc_ends), GFP_KERNEL);
	if (!soc_ends)
		return -ENOMEM;

	ret = asoc_sdw_parse_sdw_endpoints(card, soc_dais, soc_ends, &num_devs);
	if (ret < 0)
		return ret;

	sdw_be_num = ret;

	/* enable dmic */
	if (soc_sdw_quirk & ASOC_SDW_ACP_DMIC || mach_params->dmic_num)
		dmic_num = 1;

	dev_dbg(dev, "sdw %d, dmic %d", sdw_be_num, dmic_num);

	codec_conf = devm_kcalloc(dev, num_devs, sizeof(*codec_conf), GFP_KERNEL);
	if (!codec_conf)
		return -ENOMEM;

	/* allocate BE dailinks */
	num_links = sdw_be_num + dmic_num;
	dai_links = devm_kcalloc(dev, num_links, sizeof(*dai_links), GFP_KERNEL);
	if (!dai_links)
		return -ENOMEM;

	card->codec_conf = codec_conf;
	card->num_configs = num_devs;
	card->dai_link = dai_links;
	card->num_links = num_links;

	/* SDW */
	if (sdw_be_num) {
		ret = create_sdw_dailinks(card, &dai_links, &be_id,
					  soc_dais, &codec_conf);
		if (ret)
			return ret;
	}

	/* dmic */
	if (dmic_num > 0) {
		if (ctx->ignore_internal_dmic) {
			dev_warn(dev, "Ignoring ACP DMIC\n");
		} else {
			ret = create_dmic_dailinks(card, &dai_links, &be_id, 0);
			if (ret)
				return ret;
		}
	}

	WARN_ON(codec_conf != card->codec_conf + card->num_configs);
	WARN_ON(dai_links != card->dai_link + card->num_links);

	return ret;
}

static int mc_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach = dev_get_platdata(&pdev->dev);
	struct snd_soc_card *card;
	struct amd_mc_ctx *amd_ctx;
	struct asoc_sdw_mc_private *ctx;
	int amp_num = 0, i;
	int ret;

	amd_ctx = devm_kzalloc(&pdev->dev, sizeof(*amd_ctx), GFP_KERNEL);
	if (!amd_ctx)
		return -ENOMEM;

	amd_ctx->acp_rev = mach->mach_params.subsystem_rev;
	amd_ctx->max_sdw_links = ACP63_SDW_MAX_LINKS;
	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->codec_info_list_count = asoc_sdw_get_codec_info_list_count();
	ctx->private = amd_ctx;
	card = &ctx->card;
	card->dev = &pdev->dev;
	card->name = "amd-soundwire";
	card->owner = THIS_MODULE;
	card->late_probe = asoc_sdw_card_late_probe;

	snd_soc_card_set_drvdata(card, ctx);

	dmi_check_system(soc_sdw_quirk_table);

	if (quirk_override != -1) {
		dev_info(card->dev, "Overriding quirk 0x%lx => 0x%x\n",
			 soc_sdw_quirk, quirk_override);
		soc_sdw_quirk = quirk_override;
	}

	log_quirks(card->dev);

	ctx->mc_quirk = soc_sdw_quirk;
	dev_dbg(card->dev, "legacy quirk 0x%lx\n", ctx->mc_quirk);
	/* reset amp_num to ensure amp_num++ starts from 0 in each probe */
	for (i = 0; i < ctx->codec_info_list_count; i++)
		codec_info_list[i].amp_num = 0;

	ret = soc_card_dai_links_create(card);
	if (ret < 0)
		return ret;

	/*
	 * the default amp_num is zero for each codec and
	 * amp_num will only be increased for active amp
	 * codecs on used platform
	 */
	for (i = 0; i < ctx->codec_info_list_count; i++)
		amp_num += codec_info_list[i].amp_num;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  " cfg-amp:%d", amp_num);
	if (!card->components)
		return -ENOMEM;
	if (mach->mach_params.dmic_num) {
		card->components = devm_kasprintf(card->dev, GFP_KERNEL,
						  "%s mic:dmic cfg-mics:%d",
						  card->components,
						  mach->mach_params.dmic_num);
		if (!card->components)
			return -ENOMEM;
	}

	/* Register the card */
	ret = devm_snd_soc_register_card(card->dev, card);
	if (ret) {
		dev_err_probe(card->dev, ret, "snd_soc_register_card failed %d\n", ret);
		asoc_sdw_mc_dailink_exit_loop(card);
		return ret;
	}

	platform_set_drvdata(pdev, card);

	return ret;
}

static void mc_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	asoc_sdw_mc_dailink_exit_loop(card);
}

static const struct platform_device_id mc_id_table[] = {
	{ "amd_sdw", },
	{}
};
MODULE_DEVICE_TABLE(platform, mc_id_table);

static struct platform_driver soc_sdw_driver = {
	.driver = {
		.name = "amd_sdw",
		.pm = &snd_soc_pm_ops,
	},
	.probe = mc_probe,
	.remove = mc_remove,
	.id_table = mc_id_table,
};

module_platform_driver(soc_sdw_driver);

MODULE_DESCRIPTION("ASoC AMD SoundWire Legacy Generic Machine driver");
MODULE_AUTHOR("Vijendar Mukunda <Vijendar.Mukunda@amd.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("SND_SOC_SDW_UTILS");
MODULE_IMPORT_NS("SND_SOC_AMD_SDW_MACH");
