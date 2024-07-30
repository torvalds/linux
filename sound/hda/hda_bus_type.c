// SPDX-License-Identifier: GPL-2.0-only
/*
 * HD-audio bus
 */
#include <linux/init.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/export.h>
#include <sound/hdaudio.h>

MODULE_DESCRIPTION("HD-audio bus");
MODULE_LICENSE("GPL");

/**
 * hdac_get_device_id - gets the hdac device id entry
 * @hdev: HD-audio core device
 * @drv: HD-audio codec driver
 *
 * Compares the hdac device vendor_id and revision_id to the hdac_device
 * driver id_table and returns the matching device id entry.
 */
const struct hda_device_id *
hdac_get_device_id(struct hdac_device *hdev, struct hdac_driver *drv)
{
	if (drv->id_table) {
		const struct hda_device_id *id  = drv->id_table;

		while (id->vendor_id) {
			if (hdev->vendor_id == id->vendor_id &&
				(!id->rev_id || id->rev_id == hdev->revision_id))
				return id;
			id++;
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(hdac_get_device_id);

static int hdac_codec_match(struct hdac_device *dev, struct hdac_driver *drv)
{
	if (hdac_get_device_id(dev, drv))
		return 1;
	else
		return 0;
}

static int hda_bus_match(struct device *dev, const struct device_driver *drv)
{
	struct hdac_device *hdev = dev_to_hdac_dev(dev);
	struct hdac_driver *hdrv = drv_to_hdac_driver(drv);

	if (hdev->type != hdrv->type)
		return 0;

	/*
	 * if driver provided a match function use that otherwise we will
	 * use hdac_codec_match function
	 */
	if (hdrv->match)
		return hdrv->match(hdev, hdrv);
	else
		return hdac_codec_match(hdev, hdrv);
	return 1;
}

static int hda_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	char modalias[32];

	snd_hdac_codec_modalias(dev_to_hdac_dev(dev), modalias,
				sizeof(modalias));
	if (add_uevent_var(env, "MODALIAS=%s", modalias))
		return -ENOMEM;
	return 0;
}

const struct bus_type snd_hda_bus_type = {
	.name = "hdaudio",
	.match = hda_bus_match,
	.uevent = hda_uevent,
};
EXPORT_SYMBOL_GPL(snd_hda_bus_type);

static int __init hda_bus_init(void)
{
	return bus_register(&snd_hda_bus_type);
}

static void __exit hda_bus_exit(void)
{
	bus_unregister(&snd_hda_bus_type);
}

subsys_initcall(hda_bus_init);
module_exit(hda_bus_exit);
