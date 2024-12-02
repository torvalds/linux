// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux driver model AC97 bus interface
 *
 * Author:	Nicolas Pitre
 * Created:	Jan 14, 2005
 * Copyright:	(C) MontaVista Software Inc.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/string.h>
#include <sound/ac97_codec.h>

/*
 * snd_ac97_check_id() - Reads and checks the vendor ID of the device
 * @ac97: The AC97 device to check
 * @id: The ID to compare to
 * @id_mask: Mask that is applied to the device ID before comparing to @id
 *
 * If @id is 0 this function returns true if the read device vendor ID is
 * a valid ID. If @id is non 0 this functions returns true if @id
 * matches the read vendor ID. Otherwise the function returns false.
 */
static bool snd_ac97_check_id(struct snd_ac97 *ac97, unsigned int id,
	unsigned int id_mask)
{
	ac97->id = ac97->bus->ops->read(ac97, AC97_VENDOR_ID1) << 16;
	ac97->id |= ac97->bus->ops->read(ac97, AC97_VENDOR_ID2);

	if (ac97->id == 0x0 || ac97->id == 0xffffffff)
		return false;

	if (id != 0 && id != (ac97->id & id_mask))
		return false;

	return true;
}

/**
 * snd_ac97_reset() - Reset AC'97 device
 * @ac97: The AC'97 device to reset
 * @try_warm: Try a warm reset first
 * @id: Expected device vendor ID
 * @id_mask: Mask that is applied to the device ID before comparing to @id
 *
 * This function resets the AC'97 device. If @try_warm is true the function
 * first performs a warm reset. If the warm reset is successful the function
 * returns 1. Otherwise or if @try_warm is false the function issues cold reset
 * followed by a warm reset. If this is successful the function returns 0,
 * otherwise a negative error code. If @id is 0 any valid device ID will be
 * accepted, otherwise only the ID that matches @id and @id_mask is accepted.
 */
int snd_ac97_reset(struct snd_ac97 *ac97, bool try_warm, unsigned int id,
	unsigned int id_mask)
{
	const struct snd_ac97_bus_ops *ops = ac97->bus->ops;

	if (try_warm && ops->warm_reset) {
		ops->warm_reset(ac97);
		if (snd_ac97_check_id(ac97, id, id_mask))
			return 1;
	}

	if (ops->reset)
		ops->reset(ac97);
	if (ops->warm_reset)
		ops->warm_reset(ac97);

	if (snd_ac97_check_id(ac97, id, id_mask))
		return 0;

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(snd_ac97_reset);

/*
 * Let drivers decide whether they want to support given codec from their
 * probe method. Drivers have direct access to the struct snd_ac97
 * structure and may  decide based on the id field amongst other things.
 */
static int ac97_bus_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

struct bus_type ac97_bus_type = {
	.name		= "ac97",
	.match		= ac97_bus_match,
};

static int __init ac97_bus_init(void)
{
	return bus_register(&ac97_bus_type);
}

subsys_initcall(ac97_bus_init);

static void __exit ac97_bus_exit(void)
{
	bus_unregister(&ac97_bus_type);
}

module_exit(ac97_bus_exit);

EXPORT_SYMBOL(ac97_bus_type);

MODULE_LICENSE("GPL");
