// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  ALSA sequencer device management
 *  Copyright (c) 1999 by Takashi Iwai <tiwai@suse.de>
 *
 *----------------------------------------------------------------
 *
 * This device handler separates the card driver module from sequencer
 * stuff (sequencer core, synth drivers, etc), so that user can avoid
 * to spend unnecessary resources e.g. if he needs only listening to
 * MP3s.
 *
 * The card (or lowlevel) driver creates a sequencer device entry
 * via snd_seq_device_new().  This is an entry pointer to communicate
 * with the sequencer device "driver", which is involved with the
 * actual part to communicate with the sequencer core.
 * Each sequencer device entry has an id string and the corresponding
 * driver with the same id is loaded when required.  For example,
 * lowlevel codes to access emu8000 chip on sbawe card are included in
 * emu8000-synth module.  To activate this module, the hardware
 * resources like i/o port are passed via snd_seq_device argument.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/seq_device.h>
#include <sound/seq_kernel.h>
#include <sound/initval.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/mutex.h>

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("ALSA sequencer device management");
MODULE_LICENSE("GPL");

/*
 * bus definition
 */
static int snd_seq_bus_match(struct device *dev, struct device_driver *drv)
{
	struct snd_seq_device *sdev = to_seq_dev(dev);
	struct snd_seq_driver *sdrv = to_seq_drv(drv);

	return strcmp(sdrv->id, sdev->id) == 0 &&
		sdrv->argsize == sdev->argsize;
}

static struct bus_type snd_seq_bus_type = {
	.name = "snd_seq",
	.match = snd_seq_bus_match,
};

/*
 * proc interface -- just for compatibility
 */
#ifdef CONFIG_SND_PROC_FS
static struct snd_info_entry *info_entry;

static int print_dev_info(struct device *dev, void *data)
{
	struct snd_seq_device *sdev = to_seq_dev(dev);
	struct snd_info_buffer *buffer = data;

	snd_iprintf(buffer, "snd-%s,%s,%d\n", sdev->id,
		    dev->driver ? "loaded" : "empty",
		    dev->driver ? 1 : 0);
	return 0;
}

static void snd_seq_device_info(struct snd_info_entry *entry,
				struct snd_info_buffer *buffer)
{
	bus_for_each_dev(&snd_seq_bus_type, NULL, buffer, print_dev_info);
}
#endif

/*
 * load all registered drivers (called from seq_clientmgr.c)
 */

#ifdef CONFIG_MODULES
/* flag to block auto-loading */
static atomic_t snd_seq_in_init = ATOMIC_INIT(1); /* blocked as default */

static int request_seq_drv(struct device *dev, void *data)
{
	struct snd_seq_device *sdev = to_seq_dev(dev);

	if (!dev->driver)
		request_module("snd-%s", sdev->id);
	return 0;
}

static void autoload_drivers(struct work_struct *work)
{
	/* avoid reentrance */
	if (atomic_inc_return(&snd_seq_in_init) == 1)
		bus_for_each_dev(&snd_seq_bus_type, NULL, NULL,
				 request_seq_drv);
	atomic_dec(&snd_seq_in_init);
}

static DECLARE_WORK(autoload_work, autoload_drivers);

static void queue_autoload_drivers(void)
{
	schedule_work(&autoload_work);
}

void snd_seq_autoload_init(void)
{
	atomic_dec(&snd_seq_in_init);
#ifdef CONFIG_SND_SEQUENCER_MODULE
	/* initial autoload only when snd-seq is a module */
	queue_autoload_drivers();
#endif
}
EXPORT_SYMBOL(snd_seq_autoload_init);

void snd_seq_autoload_exit(void)
{
	atomic_inc(&snd_seq_in_init);
}
EXPORT_SYMBOL(snd_seq_autoload_exit);

void snd_seq_device_load_drivers(void)
{
	queue_autoload_drivers();
	flush_work(&autoload_work);
}
EXPORT_SYMBOL(snd_seq_device_load_drivers);
#define cancel_autoload_drivers()	cancel_work_sync(&autoload_work)
#else
#define queue_autoload_drivers() /* NOP */
#define cancel_autoload_drivers() /* NOP */
#endif

/*
 * device management
 */
static int snd_seq_device_dev_free(struct snd_device *device)
{
	struct snd_seq_device *dev = device->device_data;

	cancel_autoload_drivers();
	put_device(&dev->dev);
	return 0;
}

static int snd_seq_device_dev_register(struct snd_device *device)
{
	struct snd_seq_device *dev = device->device_data;
	int err;

	err = device_add(&dev->dev);
	if (err < 0)
		return err;
	if (!dev->dev.driver)
		queue_autoload_drivers();
	return 0;
}

static int snd_seq_device_dev_disconnect(struct snd_device *device)
{
	struct snd_seq_device *dev = device->device_data;

	device_del(&dev->dev);
	return 0;
}

static void snd_seq_dev_release(struct device *dev)
{
	struct snd_seq_device *sdev = to_seq_dev(dev);

	if (sdev->private_free)
		sdev->private_free(sdev);
	kfree(sdev);
}

/*
 * register a sequencer device
 * card = card info
 * device = device number (if any)
 * id = id of driver
 * result = return pointer (NULL allowed if unnecessary)
 */
int snd_seq_device_new(struct snd_card *card, int device, const char *id,
		       int argsize, struct snd_seq_device **result)
{
	struct snd_seq_device *dev;
	int err;
	static const struct snd_device_ops dops = {
		.dev_free = snd_seq_device_dev_free,
		.dev_register = snd_seq_device_dev_register,
		.dev_disconnect = snd_seq_device_dev_disconnect,
	};

	if (result)
		*result = NULL;

	if (snd_BUG_ON(!id))
		return -EINVAL;

	dev = kzalloc(sizeof(*dev) + argsize, GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	/* set up device info */
	dev->card = card;
	dev->device = device;
	dev->id = id;
	dev->argsize = argsize;

	device_initialize(&dev->dev);
	dev->dev.parent = &card->card_dev;
	dev->dev.bus = &snd_seq_bus_type;
	dev->dev.release = snd_seq_dev_release;
	dev_set_name(&dev->dev, "%s-%d-%d", dev->id, card->number, device);

	/* add this device to the list */
	err = snd_device_new(card, SNDRV_DEV_SEQUENCER, dev, &dops);
	if (err < 0) {
		put_device(&dev->dev);
		return err;
	}
	
	if (result)
		*result = dev;

	return 0;
}
EXPORT_SYMBOL(snd_seq_device_new);

/*
 * driver registration
 */
int __snd_seq_driver_register(struct snd_seq_driver *drv, struct module *mod)
{
	if (WARN_ON(!drv->driver.name || !drv->id))
		return -EINVAL;
	drv->driver.bus = &snd_seq_bus_type;
	drv->driver.owner = mod;
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__snd_seq_driver_register);

void snd_seq_driver_unregister(struct snd_seq_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(snd_seq_driver_unregister);

/*
 * module part
 */

static int __init seq_dev_proc_init(void)
{
#ifdef CONFIG_SND_PROC_FS
	info_entry = snd_info_create_module_entry(THIS_MODULE, "drivers",
						  snd_seq_root);
	if (info_entry == NULL)
		return -ENOMEM;
	info_entry->content = SNDRV_INFO_CONTENT_TEXT;
	info_entry->c.text.read = snd_seq_device_info;
	if (snd_info_register(info_entry) < 0) {
		snd_info_free_entry(info_entry);
		return -ENOMEM;
	}
#endif
	return 0;
}

static int __init alsa_seq_device_init(void)
{
	int err;

	err = bus_register(&snd_seq_bus_type);
	if (err < 0)
		return err;
	err = seq_dev_proc_init();
	if (err < 0)
		bus_unregister(&snd_seq_bus_type);
	return err;
}

static void __exit alsa_seq_device_exit(void)
{
#ifdef CONFIG_MODULES
	cancel_work_sync(&autoload_work);
#endif
#ifdef CONFIG_SND_PROC_FS
	snd_info_free_entry(info_entry);
#endif
	bus_unregister(&snd_seq_bus_type);
}

subsys_initcall(alsa_seq_device_init)
module_exit(alsa_seq_device_exit)
