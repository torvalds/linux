/*
 * HD-audio bus
 */
#include <linux/init.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/export.h>
#include <sound/hdaudio.h>

MODULE_DESCRIPTION("HD-audio bus");
MODULE_LICENSE("GPL");

static int hda_bus_match(struct device *dev, struct device_driver *drv)
{
	struct hdac_device *hdev = dev_to_hdac_dev(dev);
	struct hdac_driver *hdrv = drv_to_hdac_driver(drv);

	if (hdev->type != hdrv->type)
		return 0;
	if (hdrv->match)
		return hdrv->match(hdev, hdrv);
	return 1;
}

struct bus_type snd_hda_bus_type = {
	.name = "hdaudio",
	.match = hda_bus_match,
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
