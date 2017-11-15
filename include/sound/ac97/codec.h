/*
 *  Copyright (C) 2016 Robert Jarzmik <robert.jarzmik@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __SOUND_AC97_CODEC2_H
#define __SOUND_AC97_CODEC2_H

#include <linux/device.h>

#define AC97_ID(vendor_id1, vendor_id2) \
	((((vendor_id1) & 0xffff) << 16) | ((vendor_id2) & 0xffff))
#define AC97_DRIVER_ID(vendor_id1, vendor_id2, mask_id1, mask_id2, _data) \
	{ .id = (((vendor_id1) & 0xffff) << 16) | ((vendor_id2) & 0xffff), \
	  .mask = (((mask_id1) & 0xffff) << 16) | ((mask_id2) & 0xffff), \
	  .data = (_data) }

struct ac97_controller;
struct clk;

/**
 * struct ac97_id - matches a codec device and driver on an ac97 bus
 * @id: The significant bits if the codec vendor ID1 and ID2
 * @mask: Bitmask specifying which bits of the id field are significant when
 *	  matching. A driver binds to a device when :
 *        ((vendorID1 << 8 | vendorID2) & (mask_id1 << 8 | mask_id2)) == id.
 * @data: Private data used by the driver.
 */
struct ac97_id {
	unsigned int		id;
	unsigned int		mask;
	void			*data;
};

/**
 * ac97_codec_device - a ac97 codec
 * @dev: the core device
 * @vendor_id: the vendor_id of the codec, as sensed on the AC-link
 * @num: the codec number, 0 is primary, 1 is first slave, etc ...
 * @clk: the clock BIT_CLK provided by the codec
 * @ac97_ctrl: ac97 digital controller on the same AC-link
 *
 * This is the device instantiated for each codec living on a AC-link. There are
 * normally 0 to 4 codec devices per AC-link, and all of them are controlled by
 * an AC97 digital controller.
 */
struct ac97_codec_device {
	struct device		dev;
	unsigned int		vendor_id;
	unsigned int		num;
	struct clk		*clk;
	struct ac97_controller	*ac97_ctrl;
};

/**
 * ac97_codec_driver - a ac97 codec driver
 * @driver: the device driver structure
 * @probe: the function called when a ac97_codec_device is matched
 * @remove: the function called when the device is unbound/removed
 * @shutdown: shutdown function (might be NULL)
 * @id_table: ac97 vendor_id match table, { } member terminated
 */
struct ac97_codec_driver {
	struct device_driver	driver;
	int			(*probe)(struct ac97_codec_device *);
	int			(*remove)(struct ac97_codec_device *);
	void			(*shutdown)(struct ac97_codec_device *);
	const struct ac97_id	*id_table;
};

static inline struct ac97_codec_device *to_ac97_device(struct device *d)
{
	return container_of(d, struct ac97_codec_device, dev);
}

static inline struct ac97_codec_driver *to_ac97_driver(struct device_driver *d)
{
	return container_of(d, struct ac97_codec_driver, driver);
}

#if IS_ENABLED(CONFIG_AC97_BUS_NEW)
int snd_ac97_codec_driver_register(struct ac97_codec_driver *drv);
void snd_ac97_codec_driver_unregister(struct ac97_codec_driver *drv);
#else
static inline int
snd_ac97_codec_driver_register(struct ac97_codec_driver *drv)
{
	return 0;
}
static inline void
snd_ac97_codec_driver_unregister(struct ac97_codec_driver *drv)
{
}
#endif


static inline struct device *
ac97_codec_dev2dev(struct ac97_codec_device *adev)
{
	return &adev->dev;
}

static inline void *ac97_get_drvdata(struct ac97_codec_device *adev)
{
	return dev_get_drvdata(ac97_codec_dev2dev(adev));
}

static inline void ac97_set_drvdata(struct ac97_codec_device *adev,
				    void *data)
{
	dev_set_drvdata(ac97_codec_dev2dev(adev), data);
}

void *snd_ac97_codec_get_platdata(const struct ac97_codec_device *adev);

#endif
