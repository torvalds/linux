/*
 *  ALSA sequencer device management
 *  Copyright (c) 1999 by Takashi Iwai <tiwai@suse.de>
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
 *
 */

#include <sound/driver.h>
#include <linux/init.h>
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

/* driver state */
#define DRIVER_EMPTY		0
#define DRIVER_LOADED		(1<<0)
#define DRIVER_REQUESTED	(1<<1)
#define DRIVER_LOCKED		(1<<2)

struct ops_list {
	char id[ID_LEN];	/* driver id */
	int driver;		/* driver state */
	int used;		/* reference counter */
	int argsize;		/* argument size */

	/* operators */
	struct snd_seq_dev_ops ops;

	/* registred devices */
	struct list_head dev_list;	/* list of devices */
	int num_devices;	/* number of associated devices */
	int num_init_devices;	/* number of initialized devices */
	struct mutex reg_mutex;

	struct list_head list;	/* next driver */
};


static LIST_HEAD(opslist);
static int num_ops;
static DEFINE_MUTEX(ops_mutex);
#ifdef CONFIG_PROC_FS
static struct snd_info_entry *info_entry = NULL;
#endif

/*
 * prototypes
 */
static int snd_seq_device_free(struct snd_seq_device *dev);
static int snd_seq_device_dev_free(struct snd_device *device);
static int snd_seq_device_dev_register(struct snd_device *device);
static int snd_seq_device_dev_disconnect(struct snd_device *device);
static int snd_seq_device_dev_unregister(struct snd_device *device);

static int init_device(struct snd_seq_device *dev, struct ops_list *ops);
static int free_device(struct snd_seq_device *dev, struct ops_list *ops);
static struct ops_list *find_driver(char *id, int create_if_empty);
static struct ops_list *create_driver(char *id);
static void unlock_driver(struct ops_list *ops);
static void remove_drivers(void);

/*
 * show all drivers and their status
 */

#ifdef CONFIG_PROC_FS
static void snd_seq_device_info(struct snd_info_entry *entry,
				struct snd_info_buffer *buffer)
{
	struct list_head *head;

	mutex_lock(&ops_mutex);
	list_for_each(head, &opslist) {
		struct ops_list *ops = list_entry(head, struct ops_list, list);
		snd_iprintf(buffer, "snd-%s%s%s%s,%d\n",
				ops->id,
				ops->driver & DRIVER_LOADED ? ",loaded" : (ops->driver == DRIVER_EMPTY ? ",empty" : ""),
				ops->driver & DRIVER_REQUESTED ? ",requested" : "",
				ops->driver & DRIVER_LOCKED ? ",locked" : "",
				ops->num_devices);
	}
	mutex_unlock(&ops_mutex);
}
#endif
 
/*
 * load all registered drivers (called from seq_clientmgr.c)
 */

#ifdef CONFIG_KMOD
/* avoid auto-loading during module_init() */
static int snd_seq_in_init;
void snd_seq_autoload_lock(void)
{
	snd_seq_in_init++;
}

void snd_seq_autoload_unlock(void)
{
	snd_seq_in_init--;
}
#endif

void snd_seq_device_load_drivers(void)
{
#ifdef CONFIG_KMOD
	struct list_head *head;

	/* Calling request_module during module_init()
	 * may cause blocking.
	 */
	if (snd_seq_in_init)
		return;

	if (! current->fs->root)
		return;

	mutex_lock(&ops_mutex);
	list_for_each(head, &opslist) {
		struct ops_list *ops = list_entry(head, struct ops_list, list);
		if (! (ops->driver & DRIVER_LOADED) &&
		    ! (ops->driver & DRIVER_REQUESTED)) {
			ops->used++;
			mutex_unlock(&ops_mutex);
			ops->driver |= DRIVER_REQUESTED;
			request_module("snd-%s", ops->id);
			mutex_lock(&ops_mutex);
			ops->used--;
		}
	}
	mutex_unlock(&ops_mutex);
#endif
}

/*
 * register a sequencer device
 * card = card info (NULL allowed)
 * device = device number (if any)
 * id = id of driver
 * result = return pointer (NULL allowed if unnecessary)
 */
int snd_seq_device_new(struct snd_card *card, int device, char *id, int argsize,
		       struct snd_seq_device **result)
{
	struct snd_seq_device *dev;
	struct ops_list *ops;
	int err;
	static struct snd_device_ops dops = {
		.dev_free = snd_seq_device_dev_free,
		.dev_register = snd_seq_device_dev_register,
		.dev_disconnect = snd_seq_device_dev_disconnect,
		.dev_unregister = snd_seq_device_dev_unregister
	};

	if (result)
		*result = NULL;

	snd_assert(id != NULL, return -EINVAL);

	ops = find_driver(id, 1);
	if (ops == NULL)
		return -ENOMEM;

	dev = kzalloc(sizeof(*dev)*2 + argsize, GFP_KERNEL);
	if (dev == NULL) {
		unlock_driver(ops);
		return -ENOMEM;
	}

	/* set up device info */
	dev->card = card;
	dev->device = device;
	strlcpy(dev->id, id, sizeof(dev->id));
	dev->argsize = argsize;
	dev->status = SNDRV_SEQ_DEVICE_FREE;

	/* add this device to the list */
	mutex_lock(&ops->reg_mutex);
	list_add_tail(&dev->list, &ops->dev_list);
	ops->num_devices++;
	mutex_unlock(&ops->reg_mutex);

	unlock_driver(ops);
	
	if ((err = snd_device_new(card, SNDRV_DEV_SEQUENCER, dev, &dops)) < 0) {
		snd_seq_device_free(dev);
		return err;
	}
	
	if (result)
		*result = dev;

	return 0;
}

/*
 * free the existing device
 */
static int snd_seq_device_free(struct snd_seq_device *dev)
{
	struct ops_list *ops;

	snd_assert(dev != NULL, return -EINVAL);

	ops = find_driver(dev->id, 0);
	if (ops == NULL)
		return -ENXIO;

	/* remove the device from the list */
	mutex_lock(&ops->reg_mutex);
	list_del(&dev->list);
	ops->num_devices--;
	mutex_unlock(&ops->reg_mutex);

	free_device(dev, ops);
	if (dev->private_free)
		dev->private_free(dev);
	kfree(dev);

	unlock_driver(ops);

	return 0;
}

static int snd_seq_device_dev_free(struct snd_device *device)
{
	struct snd_seq_device *dev = device->device_data;
	return snd_seq_device_free(dev);
}

/*
 * register the device
 */
static int snd_seq_device_dev_register(struct snd_device *device)
{
	struct snd_seq_device *dev = device->device_data;
	struct ops_list *ops;

	ops = find_driver(dev->id, 0);
	if (ops == NULL)
		return -ENOENT;

	/* initialize this device if the corresponding driver was
	 * already loaded
	 */
	if (ops->driver & DRIVER_LOADED)
		init_device(dev, ops);

	unlock_driver(ops);
	return 0;
}

/*
 * disconnect the device
 */
static int snd_seq_device_dev_disconnect(struct snd_device *device)
{
	struct snd_seq_device *dev = device->device_data;
	struct ops_list *ops;

	ops = find_driver(dev->id, 0);
	if (ops == NULL)
		return -ENOENT;

	free_device(dev, ops);

	unlock_driver(ops);
	return 0;
}

/*
 * unregister the existing device
 */
static int snd_seq_device_dev_unregister(struct snd_device *device)
{
	struct snd_seq_device *dev = device->device_data;
	return snd_seq_device_free(dev);
}

/*
 * register device driver
 * id = driver id
 * entry = driver operators - duplicated to each instance
 */
int snd_seq_device_register_driver(char *id, struct snd_seq_dev_ops *entry,
				   int argsize)
{
	struct list_head *head;
	struct ops_list *ops;

	if (id == NULL || entry == NULL ||
	    entry->init_device == NULL || entry->free_device == NULL)
		return -EINVAL;

	snd_seq_autoload_lock();
	ops = find_driver(id, 1);
	if (ops == NULL) {
		snd_seq_autoload_unlock();
		return -ENOMEM;
	}
	if (ops->driver & DRIVER_LOADED) {
		snd_printk(KERN_WARNING "driver_register: driver '%s' already exists\n", id);
		unlock_driver(ops);
		snd_seq_autoload_unlock();
		return -EBUSY;
	}

	mutex_lock(&ops->reg_mutex);
	/* copy driver operators */
	ops->ops = *entry;
	ops->driver |= DRIVER_LOADED;
	ops->argsize = argsize;

	/* initialize existing devices if necessary */
	list_for_each(head, &ops->dev_list) {
		struct snd_seq_device *dev = list_entry(head, struct snd_seq_device, list);
		init_device(dev, ops);
	}
	mutex_unlock(&ops->reg_mutex);

	unlock_driver(ops);
	snd_seq_autoload_unlock();

	return 0;
}


/*
 * create driver record
 */
static struct ops_list * create_driver(char *id)
{
	struct ops_list *ops;

	ops = kmalloc(sizeof(*ops), GFP_KERNEL);
	if (ops == NULL)
		return ops;
	memset(ops, 0, sizeof(*ops));

	/* set up driver entry */
	strlcpy(ops->id, id, sizeof(ops->id));
	mutex_init(&ops->reg_mutex);
	ops->driver = DRIVER_EMPTY;
	INIT_LIST_HEAD(&ops->dev_list);
	/* lock this instance */
	ops->used = 1;

	/* register driver entry */
	mutex_lock(&ops_mutex);
	list_add_tail(&ops->list, &opslist);
	num_ops++;
	mutex_unlock(&ops_mutex);

	return ops;
}


/*
 * unregister the specified driver
 */
int snd_seq_device_unregister_driver(char *id)
{
	struct list_head *head;
	struct ops_list *ops;

	ops = find_driver(id, 0);
	if (ops == NULL)
		return -ENXIO;
	if (! (ops->driver & DRIVER_LOADED) ||
	    (ops->driver & DRIVER_LOCKED)) {
		snd_printk(KERN_ERR "driver_unregister: cannot unload driver '%s': status=%x\n",
			   id, ops->driver);
		unlock_driver(ops);
		return -EBUSY;
	}

	/* close and release all devices associated with this driver */
	mutex_lock(&ops->reg_mutex);
	ops->driver |= DRIVER_LOCKED; /* do not remove this driver recursively */
	list_for_each(head, &ops->dev_list) {
		struct snd_seq_device *dev = list_entry(head, struct snd_seq_device, list);
		free_device(dev, ops);
	}

	ops->driver = 0;
	if (ops->num_init_devices > 0)
		snd_printk(KERN_ERR "free_driver: init_devices > 0!! (%d)\n",
			   ops->num_init_devices);
	mutex_unlock(&ops->reg_mutex);

	unlock_driver(ops);

	/* remove empty driver entries */
	remove_drivers();

	return 0;
}


/*
 * remove empty driver entries
 */
static void remove_drivers(void)
{
	struct list_head *head;

	mutex_lock(&ops_mutex);
	head = opslist.next;
	while (head != &opslist) {
		struct ops_list *ops = list_entry(head, struct ops_list, list);
		if (! (ops->driver & DRIVER_LOADED) &&
		    ops->used == 0 && ops->num_devices == 0) {
			head = head->next;
			list_del(&ops->list);
			kfree(ops);
			num_ops--;
		} else
			head = head->next;
	}
	mutex_unlock(&ops_mutex);
}

/*
 * initialize the device - call init_device operator
 */
static int init_device(struct snd_seq_device *dev, struct ops_list *ops)
{
	if (! (ops->driver & DRIVER_LOADED))
		return 0; /* driver is not loaded yet */
	if (dev->status != SNDRV_SEQ_DEVICE_FREE)
		return 0; /* already initialized */
	if (ops->argsize != dev->argsize) {
		snd_printk(KERN_ERR "incompatible device '%s' for plug-in '%s' (%d %d)\n",
			   dev->name, ops->id, ops->argsize, dev->argsize);
		return -EINVAL;
	}
	if (ops->ops.init_device(dev) >= 0) {
		dev->status = SNDRV_SEQ_DEVICE_REGISTERED;
		ops->num_init_devices++;
	} else {
		snd_printk(KERN_ERR "init_device failed: %s: %s\n",
			   dev->name, dev->id);
	}

	return 0;
}

/*
 * release the device - call free_device operator
 */
static int free_device(struct snd_seq_device *dev, struct ops_list *ops)
{
	int result;

	if (! (ops->driver & DRIVER_LOADED))
		return 0; /* driver is not loaded yet */
	if (dev->status != SNDRV_SEQ_DEVICE_REGISTERED)
		return 0; /* not registered */
	if (ops->argsize != dev->argsize) {
		snd_printk(KERN_ERR "incompatible device '%s' for plug-in '%s' (%d %d)\n",
			   dev->name, ops->id, ops->argsize, dev->argsize);
		return -EINVAL;
	}
	if ((result = ops->ops.free_device(dev)) >= 0 || result == -ENXIO) {
		dev->status = SNDRV_SEQ_DEVICE_FREE;
		dev->driver_data = NULL;
		ops->num_init_devices--;
	} else {
		snd_printk(KERN_ERR "free_device failed: %s: %s\n",
			   dev->name, dev->id);
	}

	return 0;
}

/*
 * find the matching driver with given id
 */
static struct ops_list * find_driver(char *id, int create_if_empty)
{
	struct list_head *head;

	mutex_lock(&ops_mutex);
	list_for_each(head, &opslist) {
		struct ops_list *ops = list_entry(head, struct ops_list, list);
		if (strcmp(ops->id, id) == 0) {
			ops->used++;
			mutex_unlock(&ops_mutex);
			return ops;
		}
	}
	mutex_unlock(&ops_mutex);
	if (create_if_empty)
		return create_driver(id);
	return NULL;
}

static void unlock_driver(struct ops_list *ops)
{
	mutex_lock(&ops_mutex);
	ops->used--;
	mutex_unlock(&ops_mutex);
}


/*
 * module part
 */

static int __init alsa_seq_device_init(void)
{
#ifdef CONFIG_PROC_FS
	info_entry = snd_info_create_module_entry(THIS_MODULE, "drivers",
						  snd_seq_root);
	if (info_entry == NULL)
		return -ENOMEM;
	info_entry->content = SNDRV_INFO_CONTENT_TEXT;
	info_entry->c.text.read_size = 2048;
	info_entry->c.text.read = snd_seq_device_info;
	if (snd_info_register(info_entry) < 0) {
		snd_info_free_entry(info_entry);
		return -ENOMEM;
	}
#endif
	return 0;
}

static void __exit alsa_seq_device_exit(void)
{
	remove_drivers();
#ifdef CONFIG_PROC_FS
	snd_info_unregister(info_entry);
#endif
	if (num_ops)
		snd_printk(KERN_ERR "drivers not released (%d)\n", num_ops);
}

module_init(alsa_seq_device_init)
module_exit(alsa_seq_device_exit)

EXPORT_SYMBOL(snd_seq_device_load_drivers);
EXPORT_SYMBOL(snd_seq_device_new);
EXPORT_SYMBOL(snd_seq_device_register_driver);
EXPORT_SYMBOL(snd_seq_device_unregister_driver);
#ifdef CONFIG_KMOD
EXPORT_SYMBOL(snd_seq_autoload_lock);
EXPORT_SYMBOL(snd_seq_autoload_unlock);
#endif
