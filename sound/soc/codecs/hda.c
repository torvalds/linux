// SPDX-License-Identifier: GPL-2.0
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <sound/hdaudio_ext.h>
#include <sound/hda_i915.h>
#include <sound/hda_codec.h>
#include "hda.h"

static int hda_codec_create_dais(struct hda_codec *codec, int pcm_count,
				 struct snd_soc_dai_driver **drivers)
{
	struct device *dev = &codec->core.dev;
	struct snd_soc_dai_driver *drvs;
	struct hda_pcm *pcm;
	int i;

	drvs = devm_kcalloc(dev, pcm_count, sizeof(*drvs), GFP_KERNEL);
	if (!drvs)
		return -ENOMEM;

	pcm = list_first_entry(&codec->pcm_list_head, struct hda_pcm, list);

	for (i = 0; i < pcm_count; i++, pcm = list_next_entry(pcm, list)) {
		struct snd_soc_pcm_stream *stream;
		int dir;

		dev_info(dev, "creating for %s %d\n", pcm->name, i);
		drvs[i].id = i;
		drvs[i].name = pcm->name;
		drvs[i].ops = &snd_soc_hda_codec_dai_ops;

		dir = SNDRV_PCM_STREAM_PLAYBACK;
		stream = &drvs[i].playback;
		if (!pcm->stream[dir].substreams) {
			dev_info(dev, "skipping playback dai for %s\n", pcm->name);
			goto capture_dais;
		}

		stream->stream_name =
			devm_kasprintf(dev, GFP_KERNEL, "%s %s", pcm->name,
				       snd_pcm_direction_name(dir));
		if (!stream->stream_name)
			return -ENOMEM;
		stream->channels_min = pcm->stream[dir].channels_min;
		stream->channels_max = pcm->stream[dir].channels_max;
		stream->rates = pcm->stream[dir].rates;
		stream->formats = pcm->stream[dir].formats;
		stream->subformats = pcm->stream[dir].subformats;
		stream->sig_bits = pcm->stream[dir].maxbps;

capture_dais:
		dir = SNDRV_PCM_STREAM_CAPTURE;
		stream = &drvs[i].capture;
		if (!pcm->stream[dir].substreams) {
			dev_info(dev, "skipping capture dai for %s\n", pcm->name);
			continue;
		}

		stream->stream_name =
			devm_kasprintf(dev, GFP_KERNEL, "%s %s", pcm->name,
				       snd_pcm_direction_name(dir));
		if (!stream->stream_name)
			return -ENOMEM;
		stream->channels_min = pcm->stream[dir].channels_min;
		stream->channels_max = pcm->stream[dir].channels_max;
		stream->rates = pcm->stream[dir].rates;
		stream->formats = pcm->stream[dir].formats;
		stream->subformats = pcm->stream[dir].subformats;
		stream->sig_bits = pcm->stream[dir].maxbps;
	}

	*drivers = drvs;
	return 0;
}

static int hda_codec_register_dais(struct hda_codec *codec, struct snd_soc_component *component)
{
	struct snd_soc_dai_driver *drvs = NULL;
	struct snd_soc_dapm_context *dapm;
	struct hda_pcm *pcm;
	int ret, pcm_count = 0;

	if (list_empty(&codec->pcm_list_head))
		return -EINVAL;
	list_for_each_entry(pcm, &codec->pcm_list_head, list)
		pcm_count++;

	ret = hda_codec_create_dais(codec, pcm_count, &drvs);
	if (ret < 0)
		return ret;

	dapm = snd_soc_component_get_dapm(component);

	list_for_each_entry(pcm, &codec->pcm_list_head, list) {
		struct snd_soc_dai *dai;

		dai = snd_soc_register_dai(component, drvs, false);
		if (!dai) {
			dev_err(component->dev, "register dai for %s failed\n", pcm->name);
			return -EINVAL;
		}

		ret = snd_soc_dapm_new_dai_widgets(dapm, dai);
		if (ret < 0) {
			dev_err(component->dev, "create widgets failed: %d\n", ret);
			snd_soc_unregister_dai(dai);
			return ret;
		}

		snd_soc_dai_init_dma_data(dai, &pcm->stream[0], &pcm->stream[1]);
		drvs++;
	}

	return 0;
}

static void hda_codec_unregister_dais(struct hda_codec *codec,
				      struct snd_soc_component *component)
{
	struct snd_soc_dai *dai, *save;
	struct hda_pcm *pcm;

	for_each_component_dais_safe(component, dai, save) {
		int stream;

		list_for_each_entry(pcm, &codec->pcm_list_head, list) {
			if (strcmp(dai->driver->name, pcm->name))
				continue;

			for_each_pcm_streams(stream)
				snd_soc_dapm_free_widget(snd_soc_dai_get_widget(dai, stream));

			snd_soc_unregister_dai(dai);
			break;
		}
	}
}

int hda_codec_probe_complete(struct hda_codec *codec)
{
	struct hdac_device *hdev = &codec->core;
	struct hdac_bus *bus = hdev->bus;
	int ret;

	ret = snd_hda_codec_build_controls(codec);
	if (ret < 0) {
		dev_err(&hdev->dev, "unable to create controls %d\n", ret);
		goto out;
	}

	/* Bus suspended codecs as it does not manage their pm */
	pm_runtime_set_active(&hdev->dev);
	/* rpm was forbidden in snd_hda_codec_device_new() */
	snd_hda_codec_set_power_save(codec, 2000);
	snd_hda_codec_register(codec);
out:
	/* Complement pm_runtime_get_sync(bus) in probe */
	pm_runtime_mark_last_busy(bus->dev);
	pm_runtime_put_autosuspend(bus->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(hda_codec_probe_complete);

/* Expects codec with usage_count=1 and status=suspended */
static int hda_codec_probe(struct snd_soc_component *component)
{
	struct hda_codec *codec = dev_to_hda_codec(component->dev);
	struct hdac_device *hdev = &codec->core;
	struct hdac_bus *bus = hdev->bus;
	struct hdac_ext_link *hlink;
	hda_codec_patch_t patch;
	int ret;

#ifdef CONFIG_PM
	WARN_ON(atomic_read(&hdev->dev.power.usage_count) != 1 ||
		!pm_runtime_status_suspended(&hdev->dev));
#endif

	hlink = snd_hdac_ext_bus_get_hlink_by_addr(bus, hdev->addr);
	if (!hlink) {
		dev_err(&hdev->dev, "hdac link not found\n");
		return -EIO;
	}

	pm_runtime_get_sync(bus->dev);
	if (hda_codec_is_display(codec))
		snd_hdac_display_power(bus, hdev->addr, true);
	snd_hdac_ext_bus_link_get(bus, hlink);

	ret = snd_hda_codec_device_new(codec->bus, component->card->snd_card, hdev->addr, codec,
				       false);
	if (ret < 0) {
		dev_err(&hdev->dev, "codec create failed: %d\n", ret);
		goto device_new_err;
	}

	ret = snd_hda_codec_set_name(codec, codec->preset->name);
	if (ret < 0) {
		dev_err(&hdev->dev, "set name: %s failed: %d\n", codec->preset->name, ret);
		goto err;
	}

	ret = snd_hdac_regmap_init(&codec->core);
	if (ret < 0) {
		dev_err(&hdev->dev, "regmap init failed: %d\n", ret);
		goto err;
	}

	patch = (hda_codec_patch_t)codec->preset->driver_data;
	if (!patch) {
		dev_err(&hdev->dev, "no patch specified\n");
		ret = -EINVAL;
		goto err;
	}

	ret = patch(codec);
	if (ret < 0) {
		dev_err(&hdev->dev, "codec init failed: %d\n", ret);
		goto err;
	}

	ret = snd_hda_codec_parse_pcms(codec);
	if (ret < 0) {
		dev_err(&hdev->dev, "unable to map pcms to dai: %d\n", ret);
		goto parse_pcms_err;
	}

	ret = hda_codec_register_dais(codec, component);
	if (ret < 0) {
		dev_err(&hdev->dev, "update dais failed: %d\n", ret);
		goto parse_pcms_err;
	}

	if (!hda_codec_is_display(codec)) {
		ret = hda_codec_probe_complete(codec);
		if (ret < 0)
			goto complete_err;
	}

	codec->core.lazy_cache = true;

	return 0;

complete_err:
	hda_codec_unregister_dais(codec, component);
parse_pcms_err:
	if (codec->patch_ops.free)
		codec->patch_ops.free(codec);
err:
	snd_hda_codec_cleanup_for_unbind(codec);
device_new_err:
	if (hda_codec_is_display(codec))
		snd_hdac_display_power(bus, hdev->addr, false);

	snd_hdac_ext_bus_link_put(bus, hlink);

	pm_runtime_mark_last_busy(bus->dev);
	pm_runtime_put_autosuspend(bus->dev);
	return ret;
}

/* Leaves codec with usage_count=1 and status=suspended */
static void hda_codec_remove(struct snd_soc_component *component)
{
	struct hda_codec *codec = dev_to_hda_codec(component->dev);
	struct hdac_device *hdev = &codec->core;
	struct hdac_bus *bus = hdev->bus;
	struct hdac_ext_link *hlink;
	bool was_registered = codec->core.registered;

	/* Don't allow any more runtime suspends */
	pm_runtime_forbid(&hdev->dev);

	hda_codec_unregister_dais(codec, component);

	if (codec->patch_ops.free)
		codec->patch_ops.free(codec);

	snd_hda_codec_cleanup_for_unbind(codec);
	pm_runtime_put_noidle(&hdev->dev);
	/* snd_hdac_device_exit() is only called on bus remove */
	pm_runtime_set_suspended(&hdev->dev);

	if (hda_codec_is_display(codec))
		snd_hdac_display_power(bus, hdev->addr, false);

	hlink = snd_hdac_ext_bus_get_hlink_by_addr(bus, hdev->addr);
	if (hlink)
		snd_hdac_ext_bus_link_put(bus, hlink);
	/*
	 * HDMI card's hda_codec_probe_complete() (see late_probe()) may
	 * not be called due to early error, leaving bus uc unbalanced
	 */
	if (!was_registered) {
		pm_runtime_mark_last_busy(bus->dev);
		pm_runtime_put_autosuspend(bus->dev);
	}

#ifdef CONFIG_PM
	WARN_ON(atomic_read(&hdev->dev.power.usage_count) != 1 ||
		!pm_runtime_status_suspended(&hdev->dev));
#endif
}

static const struct snd_soc_dapm_route hda_dapm_routes[] = {
	{"AIF1TX", NULL, "Codec Input Pin1"},
	{"AIF2TX", NULL, "Codec Input Pin2"},
	{"AIF3TX", NULL, "Codec Input Pin3"},

	{"Codec Output Pin1", NULL, "AIF1RX"},
	{"Codec Output Pin2", NULL, "AIF2RX"},
	{"Codec Output Pin3", NULL, "AIF3RX"},
};

static const struct snd_soc_dapm_widget hda_dapm_widgets[] = {
	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "Analog Codec Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "Digital Codec Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF3RX", "Alt Analog Codec Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "Analog Codec Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "Digital Codec Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF3TX", "Alt Analog Codec Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Input Pins */
	SND_SOC_DAPM_INPUT("Codec Input Pin1"),
	SND_SOC_DAPM_INPUT("Codec Input Pin2"),
	SND_SOC_DAPM_INPUT("Codec Input Pin3"),

	/* Output Pins */
	SND_SOC_DAPM_OUTPUT("Codec Output Pin1"),
	SND_SOC_DAPM_OUTPUT("Codec Output Pin2"),
	SND_SOC_DAPM_OUTPUT("Codec Output Pin3"),
};

static struct snd_soc_dai_driver card_binder_dai = {
	.id = -1,
	.name = "codec-probing-DAI",
};

static int hda_hdev_attach(struct hdac_device *hdev)
{
	struct hda_codec *codec = dev_to_hda_codec(&hdev->dev);
	struct snd_soc_component_driver *comp_drv;

	if (hda_codec_is_display(codec) && !hdev->bus->audio_component) {
		dev_dbg(&hdev->dev, "no i915, skip registration for 0x%08x\n", hdev->vendor_id);
		return -ENODEV;
	}

	comp_drv = devm_kzalloc(&hdev->dev, sizeof(*comp_drv), GFP_KERNEL);
	if (!comp_drv)
		return -ENOMEM;

	/*
	 * It's save to rely on dev_name() rather than a copy as component
	 * driver's lifetime is directly tied to hda codec one
	 */
	comp_drv->name = dev_name(&hdev->dev);
	comp_drv->probe = hda_codec_probe;
	comp_drv->remove = hda_codec_remove;
	comp_drv->idle_bias_on = false;
	if (!hda_codec_is_display(codec)) {
		comp_drv->dapm_widgets = hda_dapm_widgets;
		comp_drv->num_dapm_widgets = ARRAY_SIZE(hda_dapm_widgets);
		comp_drv->dapm_routes = hda_dapm_routes;
		comp_drv->num_dapm_routes = ARRAY_SIZE(hda_dapm_routes);
	}

	return snd_soc_register_component(&hdev->dev, comp_drv, &card_binder_dai, 1);
}

static int hda_hdev_detach(struct hdac_device *hdev)
{
	struct hda_codec *codec = dev_to_hda_codec(&hdev->dev);

	if (codec->core.registered)
		cancel_delayed_work_sync(&codec->jackpoll_work);

	snd_soc_unregister_component(&hdev->dev);

	return 0;
}

const struct hdac_ext_bus_ops soc_hda_ext_bus_ops = {
	.hdev_attach = hda_hdev_attach,
	.hdev_detach = hda_hdev_detach,
};
EXPORT_SYMBOL_GPL(soc_hda_ext_bus_ops);

MODULE_DESCRIPTION("HD-Audio codec driver");
MODULE_AUTHOR("Cezary Rojewski <cezary.rojewski@intel.com>");
MODULE_LICENSE("GPL");
