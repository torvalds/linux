// SPDX-License-Identifier: GPL-2.0-only
/*
 *  hdac-ext-bus.c - HD-audio extended core bus functions.
 *
 *  Copyright (C) 2014-2015 Intel Corp
 *  Author: Jeeja KP <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <sound/hdaudio_ext.h>

MODULE_DESCRIPTION("HDA extended core");
MODULE_LICENSE("GPL v2");

/**
 * snd_hdac_ext_bus_init - initialize a HD-audio extended bus
 * @bus: the pointer to HDAC bus object
 * @dev: device pointer
 * @ops: bus verb operators
 * @ext_ops: operators used for ASoC HDA codec drivers
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hdac_ext_bus_init(struct hdac_bus *bus, struct device *dev,
			const struct hdac_bus_ops *ops,
			const struct hdac_ext_bus_ops *ext_ops)
{
	int ret;

	ret = snd_hdac_bus_init(bus, dev, ops);
	if (ret < 0)
		return ret;

	bus->ext_ops = ext_ops;
	/* FIXME:
	 * Currently only one bus is supported, if there is device with more
	 * buses, bus->idx should be greater than 0, but there needs to be a
	 * reliable way to always assign same number.
	 */
	bus->idx = 0;
	bus->cmd_dma_state = true;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_init);

/**
 * snd_hdac_ext_bus_exit - clean up a HD-audio extended bus
 * @bus: the pointer to HDAC bus object
 */
void snd_hdac_ext_bus_exit(struct hdac_bus *bus)
{
	snd_hdac_bus_exit(bus);
	WARN_ON(!list_empty(&bus->hlink_list));
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_exit);

static void default_release(struct device *dev)
{
	snd_hdac_ext_bus_device_exit(dev_to_hdac_dev(dev));
}

/**
 * snd_hdac_ext_bus_device_init - initialize the HDA extended codec base device
 * @bus: hdac bus to attach to
 * @addr: codec address
 * @hdev: hdac device to init
 * @type: codec type (HDAC_DEV_*) to use for this device
 *
 * Returns zero for success or a negative error code.
 */
int snd_hdac_ext_bus_device_init(struct hdac_bus *bus, int addr,
				 struct hdac_device *hdev, int type)
{
	char name[15];
	int ret;

	hdev->bus = bus;

	snprintf(name, sizeof(name), "ehdaudio%dD%d", bus->idx, addr);

	ret  = snd_hdac_device_init(hdev, bus, name, addr);
	if (ret < 0) {
		dev_err(bus->dev, "device init failed for hdac device\n");
		return ret;
	}
	hdev->type = type;
	hdev->dev.release = default_release;

	ret = snd_hdac_device_register(hdev);
	if (ret) {
		dev_err(bus->dev, "failed to register hdac device\n");
		snd_hdac_ext_bus_device_exit(hdev);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_device_init);

/**
 * snd_hdac_ext_bus_device_exit - clean up a HD-audio extended codec base device
 * @hdev: hdac device to clean up
 */
void snd_hdac_ext_bus_device_exit(struct hdac_device *hdev)
{
	snd_hdac_device_exit(hdev);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_device_exit);

/**
 * snd_hdac_ext_bus_device_remove - remove HD-audio extended codec base devices
 *
 * @bus: the pointer to HDAC bus object
 */
void snd_hdac_ext_bus_device_remove(struct hdac_bus *bus)
{
	struct hdac_device *codec, *__codec;
	/*
	 * we need to remove all the codec devices objects created in the
	 * snd_hdac_ext_bus_device_init
	 */
	list_for_each_entry_safe(codec, __codec, &bus->codec_list, list) {
		snd_hdac_device_unregister(codec);
		put_device(&codec->dev);
	}
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_device_remove);
#define dev_to_hdac(dev) (container_of((dev), \
			struct hdac_device, dev))

static inline struct hdac_driver *get_hdrv(struct device *dev)
{
	struct hdac_driver *hdrv = drv_to_hdac_driver(dev->driver);
	return hdrv;
}

static inline struct hdac_device *get_hdev(struct device *dev)
{
	struct hdac_device *hdev = dev_to_hdac_dev(dev);
	return hdev;
}

static int hda_ext_drv_probe(struct device *dev)
{
	return (get_hdrv(dev))->probe(get_hdev(dev));
}

static int hdac_ext_drv_remove(struct device *dev)
{
	return (get_hdrv(dev))->remove(get_hdev(dev));
}

static void hdac_ext_drv_shutdown(struct device *dev)
{
	return (get_hdrv(dev))->shutdown(get_hdev(dev));
}

/**
 * snd_hda_ext_driver_register - register a driver for ext hda devices
 *
 * @drv: ext hda driver structure
 */
int snd_hda_ext_driver_register(struct hdac_driver *drv)
{
	drv->type = HDA_DEV_ASOC;
	drv->driver.bus = &snd_hda_bus_type;
	/* we use default match */

	if (drv->probe)
		drv->driver.probe = hda_ext_drv_probe;
	if (drv->remove)
		drv->driver.remove = hdac_ext_drv_remove;
	if (drv->shutdown)
		drv->driver.shutdown = hdac_ext_drv_shutdown;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(snd_hda_ext_driver_register);

/**
 * snd_hda_ext_driver_unregister - unregister a driver for ext hda devices
 *
 * @drv: ext hda driver structure
 */
void snd_hda_ext_driver_unregister(struct hdac_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(snd_hda_ext_driver_unregister);
