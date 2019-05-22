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
#include <linux/io.h>
#include <sound/hdaudio_ext.h>

MODULE_DESCRIPTION("HDA extended core");
MODULE_LICENSE("GPL v2");

static void hdac_ext_writel(u32 value, u32 __iomem *addr)
{
	writel(value, addr);
}

static u32 hdac_ext_readl(u32 __iomem *addr)
{
	return readl(addr);
}

static void hdac_ext_writew(u16 value, u16 __iomem *addr)
{
	writew(value, addr);
}

static u16 hdac_ext_readw(u16 __iomem *addr)
{
	return readw(addr);
}

static void hdac_ext_writeb(u8 value, u8 __iomem *addr)
{
	writeb(value, addr);
}

static u8 hdac_ext_readb(u8 __iomem *addr)
{
	return readb(addr);
}

static int hdac_ext_dma_alloc_pages(struct hdac_bus *bus, int type,
			   size_t size, struct snd_dma_buffer *buf)
{
	return snd_dma_alloc_pages(type, bus->dev, size, buf);
}

static void hdac_ext_dma_free_pages(struct hdac_bus *bus, struct snd_dma_buffer *buf)
{
	snd_dma_free_pages(buf);
}

static const struct hdac_io_ops hdac_ext_default_io = {
	.reg_writel = hdac_ext_writel,
	.reg_readl = hdac_ext_readl,
	.reg_writew = hdac_ext_writew,
	.reg_readw = hdac_ext_readw,
	.reg_writeb = hdac_ext_writeb,
	.reg_readb = hdac_ext_readb,
	.dma_alloc_pages = hdac_ext_dma_alloc_pages,
	.dma_free_pages = hdac_ext_dma_free_pages,
};

/**
 * snd_hdac_ext_bus_init - initialize a HD-audio extended bus
 * @ebus: the pointer to extended bus object
 * @dev: device pointer
 * @ops: bus verb operators
 * @io_ops: lowlevel I/O operators, can be NULL. If NULL core will use
 * default ops
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hdac_ext_bus_init(struct hdac_bus *bus, struct device *dev,
			const struct hdac_bus_ops *ops,
			const struct hdac_io_ops *io_ops,
			const struct hdac_ext_bus_ops *ext_ops)
{
	int ret;
	static int idx;

	/* check if io ops are provided, if not load the defaults */
	if (io_ops == NULL)
		io_ops = &hdac_ext_default_io;

	ret = snd_hdac_bus_init(bus, dev, ops, io_ops);
	if (ret < 0)
		return ret;

	bus->ext_ops = ext_ops;
	INIT_LIST_HEAD(&bus->hlink_list);
	bus->idx = idx++;

	mutex_init(&bus->lock);
	bus->cmd_dma_state = true;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_init);

/**
 * snd_hdac_ext_bus_exit - clean up a HD-audio extended bus
 * @ebus: the pointer to extended bus object
 */
void snd_hdac_ext_bus_exit(struct hdac_bus *bus)
{
	snd_hdac_bus_exit(bus);
	WARN_ON(!list_empty(&bus->hlink_list));
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_exit);

static void default_release(struct device *dev)
{
	snd_hdac_ext_bus_device_exit(container_of(dev, struct hdac_device, dev));
}

/**
 * snd_hdac_ext_bus_device_init - initialize the HDA extended codec base device
 * @ebus: hdac extended bus to attach to
 * @addr: codec address
 *
 * Returns zero for success or a negative error code.
 */
int snd_hdac_ext_bus_device_init(struct hdac_bus *bus, int addr,
					struct hdac_device *hdev)
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
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_device_exit);

/**
 * snd_hdac_ext_bus_device_remove - remove HD-audio extended codec base devices
 *
 * @ebus: HD-audio extended bus
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
