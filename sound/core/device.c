/*
 *  Device management routines
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <sound/core.h>

/**
 * snd_device_new - create an ALSA device component
 * @card: the card instance
 * @type: the device type, SNDRV_DEV_XXX
 * @device_data: the data pointer of this device
 * @ops: the operator table
 *
 * Creates a new device component for the given data pointer.
 * The device will be assigned to the card and managed together
 * by the card.
 *
 * The data pointer plays a role as the identifier, too, so the
 * pointer address must be unique and unchanged.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_device_new(struct snd_card *card, snd_device_type_t type,
		   void *device_data, struct snd_device_ops *ops)
{
	struct snd_device *dev;

	snd_assert(card != NULL, return -ENXIO);
	snd_assert(device_data != NULL, return -ENXIO);
	snd_assert(ops != NULL, return -ENXIO);
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		snd_printk(KERN_ERR "Cannot allocate device\n");
		return -ENOMEM;
	}
	dev->card = card;
	dev->type = type;
	dev->state = SNDRV_DEV_BUILD;
	dev->device_data = device_data;
	dev->ops = ops;
	list_add(&dev->list, &card->devices);	/* add to the head of list */
	return 0;
}

EXPORT_SYMBOL(snd_device_new);

/**
 * snd_device_free - release the device from the card
 * @card: the card instance
 * @device_data: the data pointer to release
 *
 * Removes the device from the list on the card and invokes the
 * callbacks, dev_disconnect and dev_free, corresponding to the state.
 * Then release the device.
 *
 * Returns zero if successful, or a negative error code on failure or if the
 * device not found.
 */
int snd_device_free(struct snd_card *card, void *device_data)
{
	struct snd_device *dev;
	
	snd_assert(card != NULL, return -ENXIO);
	snd_assert(device_data != NULL, return -ENXIO);
	list_for_each_entry(dev, &card->devices, list) {
		if (dev->device_data != device_data)
			continue;
		/* unlink */
		list_del(&dev->list);
		if (dev->state == SNDRV_DEV_REGISTERED &&
		    dev->ops->dev_disconnect)
			if (dev->ops->dev_disconnect(dev))
				snd_printk(KERN_ERR
					   "device disconnect failure\n");
		if (dev->ops->dev_free) {
			if (dev->ops->dev_free(dev))
				snd_printk(KERN_ERR "device free failure\n");
		}
		kfree(dev);
		return 0;
	}
	snd_printd("device free %p (from %p), not found\n", device_data,
		   __builtin_return_address(0));
	return -ENXIO;
}

EXPORT_SYMBOL(snd_device_free);

/**
 * snd_device_disconnect - disconnect the device
 * @card: the card instance
 * @device_data: the data pointer to disconnect
 *
 * Turns the device into the disconnection state, invoking
 * dev_disconnect callback, if the device was already registered.
 *
 * Usually called from snd_card_disconnect().
 *
 * Returns zero if successful, or a negative error code on failure or if the
 * device not found.
 */
int snd_device_disconnect(struct snd_card *card, void *device_data)
{
	struct snd_device *dev;

	snd_assert(card != NULL, return -ENXIO);
	snd_assert(device_data != NULL, return -ENXIO);
	list_for_each_entry(dev, &card->devices, list) {
		if (dev->device_data != device_data)
			continue;
		if (dev->state == SNDRV_DEV_REGISTERED &&
		    dev->ops->dev_disconnect) {
			if (dev->ops->dev_disconnect(dev))
				snd_printk(KERN_ERR "device disconnect failure\n");
			dev->state = SNDRV_DEV_DISCONNECTED;
		}
		return 0;
	}
	snd_printd("device disconnect %p (from %p), not found\n", device_data,
		   __builtin_return_address(0));
	return -ENXIO;
}

/**
 * snd_device_register - register the device
 * @card: the card instance
 * @device_data: the data pointer to register
 *
 * Registers the device which was already created via
 * snd_device_new().  Usually this is called from snd_card_register(),
 * but it can be called later if any new devices are created after
 * invocation of snd_card_register().
 *
 * Returns zero if successful, or a negative error code on failure or if the
 * device not found.
 */
int snd_device_register(struct snd_card *card, void *device_data)
{
	struct snd_device *dev;
	int err;

	snd_assert(card != NULL, return -ENXIO);
	snd_assert(device_data != NULL, return -ENXIO);
	list_for_each_entry(dev, &card->devices, list) {
		if (dev->device_data != device_data)
			continue;
		if (dev->state == SNDRV_DEV_BUILD && dev->ops->dev_register) {
			if ((err = dev->ops->dev_register(dev)) < 0)
				return err;
			dev->state = SNDRV_DEV_REGISTERED;
			return 0;
		}
		snd_printd("snd_device_register busy\n");
		return -EBUSY;
	}
	snd_BUG();
	return -ENXIO;
}

EXPORT_SYMBOL(snd_device_register);

/*
 * register all the devices on the card.
 * called from init.c
 */
int snd_device_register_all(struct snd_card *card)
{
	struct snd_device *dev;
	int err;
	
	snd_assert(card != NULL, return -ENXIO);
	list_for_each_entry(dev, &card->devices, list) {
		if (dev->state == SNDRV_DEV_BUILD && dev->ops->dev_register) {
			if ((err = dev->ops->dev_register(dev)) < 0)
				return err;
			dev->state = SNDRV_DEV_REGISTERED;
		}
	}
	return 0;
}

/*
 * disconnect all the devices on the card.
 * called from init.c
 */
int snd_device_disconnect_all(struct snd_card *card)
{
	struct snd_device *dev;
	int err = 0;

	snd_assert(card != NULL, return -ENXIO);
	list_for_each_entry(dev, &card->devices, list) {
		if (snd_device_disconnect(card, dev->device_data) < 0)
			err = -ENXIO;
	}
	return err;
}

/*
 * release all the devices on the card.
 * called from init.c
 */
int snd_device_free_all(struct snd_card *card, snd_device_cmd_t cmd)
{
	struct snd_device *dev;
	int err;
	unsigned int range_low, range_high;

	snd_assert(card != NULL, return -ENXIO);
	range_low = cmd * SNDRV_DEV_TYPE_RANGE_SIZE;
	range_high = range_low + SNDRV_DEV_TYPE_RANGE_SIZE - 1;
      __again:
	list_for_each_entry(dev, &card->devices, list) {
		if (dev->type >= range_low && dev->type <= range_high) {
			if ((err = snd_device_free(card, dev->device_data)) < 0)
				return err;
			goto __again;
		}
	}
	return 0;
}
