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
#include "hda_codec.h"
#include "hda_local.h"

/* codec vendor labels */
struct hda_vendor_id {
	unsigned int id;
	const char *name;
};

static struct hda_vendor_id hda_vendor_ids[] = {
	{ 0x1002, "ATI" },
	{ 0x1013, "Cirrus Logic" },
	{ 0x1057, "Motorola" },
	{ 0x1095, "Silicon Image" },
	{ 0x10de, "Nvidia" },
	{ 0x10ec, "Realtek" },
	{ 0x1102, "Creative" },
	{ 0x1106, "VIA" },
	{ 0x111d, "IDT" },
	{ 0x11c1, "LSI" },
	{ 0x11d4, "Analog Devices" },
	{ 0x13f6, "C-Media" },
	{ 0x14f1, "Conexant" },
	{ 0x17e8, "Chrontel" },
	{ 0x1854, "LG" },
	{ 0x1aec, "Wolfson Microelectronics" },
	{ 0x1af4, "QEMU" },
	{ 0x434d, "C-Media" },
	{ 0x8086, "Intel" },
	{ 0x8384, "SigmaTel" },
	{} /* terminator */
};

/*
 * find a matching codec preset
 */
static int hda_bus_match(struct device *dev, struct device_driver *drv)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, dev);
	struct hda_codec_driver *driver =
		container_of(drv, struct hda_codec_driver, driver);
	const struct hda_codec_preset *preset;
	/* check probe_id instead of vendor_id if set */
	u32 id = codec->probe_id ? codec->probe_id : codec->vendor_id;

	for (preset = driver->preset; preset->id; preset++) {
		u32 mask = preset->mask;

		if (preset->afg && preset->afg != codec->afg)
			continue;
		if (preset->mfg && preset->mfg != codec->mfg)
			continue;
		if (!mask)
			mask = ~0;
		if (preset->id == (id & mask) &&
		    (!preset->rev || preset->rev == codec->revision_id)) {
			codec->preset = preset;
			return 1;
		}
	}
	return 0;
}

/* reset the codec name from the preset */
static int codec_refresh_name(struct hda_codec *codec, const char *name)
{
	char tmp[16];

	kfree(codec->chip_name);
	if (!name) {
		sprintf(tmp, "ID %x", codec->vendor_id & 0xffff);
		name = tmp;
	}
	codec->chip_name = kstrdup(name, GFP_KERNEL);
	return codec->chip_name ? 0 : -ENOMEM;
}

static int hda_codec_driver_probe(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);
	struct module *owner = dev->driver->owner;
	int err;

	if (WARN_ON(!codec->preset))
		return -EINVAL;

	err = codec_refresh_name(codec, codec->preset->name);
	if (err < 0)
		goto error;

	if (!try_module_get(owner)) {
		err = -EINVAL;
		goto error;
	}

	err = codec->preset->patch(codec);
	if (err < 0)
		goto error_module;

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
	}

	return 0;

 error_module:
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
	drv->driver.name = name;
	drv->driver.owner = owner;
	drv->driver.bus = &snd_hda_bus_type;
	drv->driver.probe = hda_codec_driver_probe;
	drv->driver.remove = hda_codec_driver_remove;
	drv->driver.shutdown = hda_codec_driver_shutdown;
	drv->driver.pm = &hda_codec_driver_pm;
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__hda_codec_driver_register);

void hda_codec_driver_unregister(struct hda_codec_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(hda_codec_driver_unregister);

static inline bool codec_probed(struct hda_codec *codec)
{
	return device_attach(hda_codec_dev(codec)) > 0 && codec->preset;
}

/* try to auto-load and bind the codec module */
static void codec_bind_module(struct hda_codec *codec)
{
#ifdef MODULE
	request_module("snd-hda-codec-id:%08x", codec->vendor_id);
	if (codec_probed(codec))
		return;
	request_module("snd-hda-codec-id:%04x*",
		       (codec->vendor_id >> 16) & 0xffff);
	if (codec_probed(codec))
		return;
#endif
}

/* store the codec vendor name */
static int get_codec_vendor_name(struct hda_codec *codec)
{
	const struct hda_vendor_id *c;
	const char *vendor = NULL;
	u16 vendor_id = codec->vendor_id >> 16;
	char tmp[16];

	for (c = hda_vendor_ids; c->id; c++) {
		if (c->id == vendor_id) {
			vendor = c->name;
			break;
		}
	}
	if (!vendor) {
		sprintf(tmp, "Generic %04x", vendor_id);
		vendor = tmp;
	}
	codec->vendor_name = kstrdup(vendor, GFP_KERNEL);
	if (!codec->vendor_name)
		return -ENOMEM;
	return 0;
}

#if IS_ENABLED(CONFIG_SND_HDA_CODEC_HDMI)
/* if all audio out widgets are digital, let's assume the codec as a HDMI/DP */
static bool is_likely_hdmi_codec(struct hda_codec *codec)
{
	hda_nid_t nid = codec->start_nid;
	int i;

	for (i = 0; i < codec->num_nodes; i++, nid++) {
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
#if IS_MODULE(CONFIG_SND_HDA_CODEC_HDMI)
		request_module("snd-hda-codec-hdmi");
#endif
		if (codec_probed(codec))
			return 0;
	}

	codec->probe_id = HDA_CODEC_ID_GENERIC;
#if IS_MODULE(CONFIG_SND_HDA_GENERIC)
	request_module("snd-hda-codec-generic");
#endif
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

	if (!codec->vendor_name) {
		err = get_codec_vendor_name(codec);
		if (err < 0)
			return err;
	}

	if (is_generic_config(codec))
		codec->probe_id = HDA_CODEC_ID_GENERIC;
	else
		codec->probe_id = 0;

	err = device_add(hda_codec_dev(codec));
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

	/* audio codec should override the mixer name */
	if (codec->afg || !*codec->card->mixername)
		snprintf(codec->card->mixername,
			 sizeof(codec->card->mixername),
			 "%s %s", codec->vendor_name, codec->chip_name);
	return 0;

 error:
	device_del(hda_codec_dev(codec));
	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_configure);

/*
 * bus registration
 */
struct bus_type snd_hda_bus_type = {
	.name = "hdaudio",
	.match = hda_bus_match,
};

static int __init hda_codec_init(void)
{
	return bus_register(&snd_hda_bus_type);
}

static void __exit hda_codec_exit(void)
{
	bus_unregister(&snd_hda_bus_type);
}

module_init(hda_codec_init);
module_exit(hda_codec_exit);
