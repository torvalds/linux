/*
 * HD-audio codec driver binding
 * Copyright (c) Takashi Iwai <tiwai@suse.de>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <sound/core.h>
#include <sound/hda_codec.h>
#include "hda_local.h"

/*
 * find a matching codec id
 */
static int hda_codec_match(struct hdac_device *dev, struct hdac_driver *drv)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);
	struct hda_codec_driver *driver =
		container_of(drv, struct hda_codec_driver, core);
	const struct hda_device_id *list;
	/* check probe_id instead of vendor_id if set */
	u32 id = codec->probe_id ? codec->probe_id : codec->core.vendor_id;
	u32 rev_id = codec->core.revision_id;

	for (list = driver->id; list->vendor_id; list++) {
		if (list->vendor_id == id &&
		    (!list->rev_id || list->rev_id == rev_id)) {
			codec->preset = list;
			return 1;
		}
	}
	return 0;
}

/* process an unsolicited event */
static void hda_codec_unsol_event(struct hdac_device *dev, unsigned int ev)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);

	if (codec->patch_ops.unsol_event)
		codec->patch_ops.unsol_event(codec, ev);
}

/**
 * snd_hda_codec_set_name - set the codec name
 * @codec: the HDA codec
 * @name: name string to set
 */
int snd_hda_codec_set_name(struct hda_codec *codec, const char *name)
{
	int err;

	if (!name)
		return 0;
	err = snd_hdac_device_set_chip_name(&codec->core, name);
	if (err < 0)
		return err;

	/* update the mixer name */
	if (!*codec->card->mixername ||
	    codec->bus->mixer_assigned >= codec->core.addr) {
		snprintf(codec->card->mixername,
			 sizeof(codec->card->mixername), "%s %s",
			 codec->core.vendor_name, codec->core.chip_name);
		codec->bus->mixer_assigned = codec->core.addr;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_set_name);

static int hda_codec_driver_probe(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);
	struct module *owner = dev->driver->owner;
	hda_codec_patch_t patch;
	int err;

	if (WARN_ON(!codec->preset))
		return -EINVAL;

	err = snd_hda_codec_set_name(codec, codec->preset->name);
	if (err < 0)
		goto error;
	err = snd_hdac_regmap_init(&codec->core);
	if (err < 0)
		goto error;

	if (!try_module_get(owner)) {
		err = -EINVAL;
		goto error;
	}

	patch = (hda_codec_patch_t)codec->preset->driver_data;
	if (patch) {
		err = patch(codec);
		if (err < 0)
			goto error_module_put;
	}

	err = snd_hda_codec_build_pcms(codec);
	if (err < 0)
		goto error_module;
	err = snd_hda_codec_build_controls(codec);
	if (err < 0)
		goto error_module;
	if (codec->card->registered) {
		err = snd_card_register(codec->card);
		if (err < 0)
			goto error_module;
		snd_hda_codec_register(codec);
	}

	codec->core.lazy_cache = true;
	return 0;

 error_module:
	if (codec->patch_ops.free)
		codec->patch_ops.free(codec);
 error_module_put:
	module_put(owner);

 error:
	snd_hda_codec_cleanup_for_unbind(codec);
	return err;
}

static int hda_codec_driver_remove(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);

	if (codec->patch_ops.free)
		codec->patch_ops.free(codec);
	snd_hda_codec_cleanup_for_unbind(codec);
	module_put(dev->driver->owner);
	return 0;
}

static void hda_codec_driver_shutdown(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);

	if (!pm_runtime_suspended(dev) && codec->patch_ops.reboot_notify)
		codec->patch_ops.reboot_notify(codec);
}

int __hda_codec_driver_register(struct hda_codec_driver *drv, const char *name,
			       struct module *owner)
{
	drv->core.driver.name = name;
	drv->core.driver.owner = owner;
	drv->core.driver.bus = &snd_hda_bus_type;
	drv->core.driver.probe = hda_codec_driver_probe;
	drv->core.driver.remove = hda_codec_driver_remove;
	drv->core.driver.shutdown = hda_codec_driver_shutdown;
	drv->core.driver.pm = &hda_codec_driver_pm;
	drv->core.type = HDA_DEV_LEGACY;
	drv->core.match = hda_codec_match;
	drv->core.unsol_event = hda_codec_unsol_event;
	return driver_register(&drv->core.driver);
}
EXPORT_SYMBOL_GPL(__hda_codec_driver_register);

void hda_codec_driver_unregister(struct hda_codec_driver *drv)
{
	driver_unregister(&drv->core.driver);
}
EXPORT_SYMBOL_GPL(hda_codec_driver_unregister);

static inline bool codec_probed(struct hda_codec *codec)
{
	return device_attach(hda_codec_dev(codec)) > 0 && codec->preset;
}

/* try to auto-load codec module */
static void request_codec_module(struct hda_codec *codec)
{
#ifdef MODULE
	char modalias[32];
	const char *mod = NULL;

	switch (codec->probe_id) {
	case HDA_CODEC_ID_GENERIC_HDMI:
#if IS_MODULE(CONFIG_SND_HDA_CODEC_HDMI)
		mod = "snd-hda-codec-hdmi";
#endif
		break;
	case HDA_CODEC_ID_GENERIC:
#if IS_MODULE(CONFIG_SND_HDA_GENERIC)
		mod = "snd-hda-codec-generic";
#endif
		break;
	default:
		snd_hdac_codec_modalias(&codec->core, modalias, sizeof(modalias));
		mod = modalias;
		break;
	}

	if (mod)
		request_module(mod);
#endif /* MODULE */
}

/* try to auto-load and bind the codec module */
static void codec_bind_module(struct hda_codec *codec)
{
#ifdef MODULE
	request_codec_module(codec);
	if (codec_probed(codec))
		return;
#endif
}

#if IS_ENABLED(CONFIG_SND_HDA_CODEC_HDMI)
/* if all audio out widgets are digital, let's assume the codec as a HDMI/DP */
static bool is_likely_hdmi_codec(struct hda_codec *codec)
{
	hda_nid_t nid;

	for_each_hda_codec_node(nid, codec) {
		unsigned int wcaps = get_wcaps(codec, nid);
		switch (get_wcaps_type(wcaps)) {
		case AC_WID_AUD_IN:
			return false; /* HDMI parser supports only HDMI out */
		case AC_WID_AUD_OUT:
			if (!(wcaps & AC_WCAP_DIGITAL))
				return false;
			break;
		}
	}
	return true;
}
#else
/* no HDMI codec parser support */
#define is_likely_hdmi_codec(codec)	false
#endif /* CONFIG_SND_HDA_CODEC_HDMI */

static int codec_bind_generic(struct hda_codec *codec)
{
	if (codec->probe_id)
		return -ENODEV;

	if (is_likely_hdmi_codec(codec)) {
		codec->probe_id = HDA_CODEC_ID_GENERIC_HDMI;
		request_codec_module(codec);
		if (codec_probed(codec))
			return 0;
	}

	codec->probe_id = HDA_CODEC_ID_GENERIC;
	request_codec_module(codec);
	if (codec_probed(codec))
		return 0;
	return -ENODEV;
}

#if IS_ENABLED(CONFIG_SND_HDA_GENERIC)
#define is_generic_config(codec) \
	(codec->modelname && !strcmp(codec->modelname, "generic"))
#else
#define is_generic_config(codec)	0
#endif

/**
 * snd_hda_codec_configure - (Re-)configure the HD-audio codec
 * @codec: the HDA codec
 *
 * Start parsing of the given codec tree and (re-)initialize the whole
 * patch instance.
 *
 * Returns 0 if successful or a negative error code.
 */
int snd_hda_codec_configure(struct hda_codec *codec)
{
	int err;

	if (is_generic_config(codec))
		codec->probe_id = HDA_CODEC_ID_GENERIC;
	else
		codec->probe_id = 0;

	err = snd_hdac_device_register(&codec->core);
	if (err < 0)
		return err;

	if (!codec->preset)
		codec_bind_module(codec);
	if (!codec->preset) {
		err = codec_bind_generic(codec);
		if (err < 0) {
			codec_err(codec, "Unable to bind the codec\n");
			goto error;
		}
	}

	return 0;

 error:
	snd_hdac_device_unregister(&codec->core);
	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_configure);
