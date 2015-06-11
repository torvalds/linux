/*
 *  hdac-ext-bus.c - HD-audio extended core bus functions.
 *
 *  Copyright (C) 2014-2015 Intel Corp
 *  Author: Jeeja KP <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <sound/hdaudio_ext.h>

MODULE_DESCRIPTION("HDA extended core");
MODULE_LICENSE("GPL v2");

/**
 * snd_hdac_ext_bus_init - initialize a HD-audio extended bus
 * @ebus: the pointer to extended bus object
 * @dev: device pointer
 * @ops: bus verb operators
 * @io_ops: lowlevel I/O operators
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hdac_ext_bus_init(struct hdac_ext_bus *ebus, struct device *dev,
			const struct hdac_bus_ops *ops,
			const struct hdac_io_ops *io_ops)
{
	int ret;
	static int idx;

	ret = snd_hdac_bus_init(&ebus->bus, dev, ops, io_ops);
	if (ret < 0)
		return ret;

	INIT_LIST_HEAD(&ebus->hlink_list);
	ebus->idx = idx++;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_init);

/**
 * snd_hdac_ext_bus_exit - clean up a HD-audio extended bus
 * @ebus: the pointer to extended bus object
 */
void snd_hdac_ext_bus_exit(struct hdac_ext_bus *ebus)
{
	snd_hdac_bus_exit(&ebus->bus);
	WARN_ON(!list_empty(&ebus->hlink_list));
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_exit);

static void default_release(struct device *dev)
{
	snd_hdac_ext_bus_device_exit(container_of(dev, struct hdac_device, dev));
}

/**
 * snd_hdac_ext_device_init - initialize the HDA extended codec base device
 * @ebus: hdac extended bus to attach to
 * @addr: codec address
 *
 * Returns zero for success or a negative error code.
 */
int snd_hdac_ext_bus_device_init(struct hdac_ext_bus *ebus, int addr)
{
	struct hdac_device *hdev = NULL;
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	char name[15];
	int ret;

	hdev = kzalloc(sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;

	snprintf(name, sizeof(name), "ehdaudio%dD%d", ebus->idx, addr);

	ret  = snd_hdac_device_init(hdev, bus, name, addr);
	if (ret < 0) {
		dev_err(bus->dev, "device init failed for hdac device\n");
		return ret;
	}
	hdev->type = HDA_DEV_ASOC;
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
	kfree(hdev);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_device_exit);
