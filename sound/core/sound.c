/*
 *  Advanced Linux Sound Architecture
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <sound/version.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <linux/kmod.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/mutex.h>

#define SNDRV_OS_MINORS 256

static int major = CONFIG_SND_MAJOR;
int snd_major;
EXPORT_SYMBOL(snd_major);

static int cards_limit = 1;
static int device_mode = S_IFCHR | S_IRUGO | S_IWUGO;

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Advanced Linux Sound Architecture driver for soundcards.");
MODULE_LICENSE("GPL");
module_param(major, int, 0444);
MODULE_PARM_DESC(major, "Major # for sound driver.");
module_param(cards_limit, int, 0444);
MODULE_PARM_DESC(cards_limit, "Count of auto-loadable soundcards.");
#ifdef CONFIG_DEVFS_FS
module_param(device_mode, int, 0444);
MODULE_PARM_DESC(device_mode, "Device file permission mask for devfs.");
#endif
MODULE_ALIAS_CHARDEV_MAJOR(CONFIG_SND_MAJOR);

/* this one holds the actual max. card number currently available.
 * as default, it's identical with cards_limit option.  when more
 * modules are loaded manually, this limit number increases, too.
 */
int snd_ecards_limit;
EXPORT_SYMBOL(snd_ecards_limit);

static struct snd_minor *snd_minors[SNDRV_OS_MINORS];
static DEFINE_MUTEX(sound_mutex);

extern struct class *sound_class;


#ifdef CONFIG_KMOD

/**
 * snd_request_card - try to load the card module
 * @card: the card number
 *
 * Tries to load the module "snd-card-X" for the given card number
 * via KMOD.  Returns immediately if already loaded.
 */
void snd_request_card(int card)
{
	if (! current->fs->root)
		return;
	if (snd_card_locked(card))
		return;
	if (card < 0 || card >= cards_limit)
		return;
	request_module("snd-card-%i", card);
}

EXPORT_SYMBOL(snd_request_card);

static void snd_request_other(int minor)
{
	char *str;

	if (! current->fs->root)
		return;
	switch (minor) {
	case SNDRV_MINOR_SEQUENCER:	str = "snd-seq";	break;
	case SNDRV_MINOR_TIMER:		str = "snd-timer";	break;
	default:			return;
	}
	request_module(str);
}

#endif				/* request_module support */

/**
 * snd_lookup_minor_data - get user data of a registered device
 * @minor: the minor number
 * @type: device type (SNDRV_DEVICE_TYPE_XXX)
 *
 * Checks that a minor device with the specified type is registered, and returns
 * its user data pointer.
 */
void *snd_lookup_minor_data(unsigned int minor, int type)
{
	struct snd_minor *mreg;
	void *private_data;

	if (minor >= ARRAY_SIZE(snd_minors))
		return NULL;
	mutex_lock(&sound_mutex);
	mreg = snd_minors[minor];
	if (mreg && mreg->type == type)
		private_data = mreg->private_data;
	else
		private_data = NULL;
	mutex_unlock(&sound_mutex);
	return private_data;
}

EXPORT_SYMBOL(snd_lookup_minor_data);

static int snd_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct snd_minor *mptr = NULL;
	const struct file_operations *old_fops;
	int err = 0;

	if (minor >= ARRAY_SIZE(snd_minors))
		return -ENODEV;
	mptr = snd_minors[minor];
	if (mptr == NULL) {
#ifdef CONFIG_KMOD
		int dev = SNDRV_MINOR_DEVICE(minor);
		if (dev == SNDRV_MINOR_CONTROL) {
			/* /dev/aloadC? */
			int card = SNDRV_MINOR_CARD(minor);
			if (snd_cards[card] == NULL)
				snd_request_card(card);
		} else if (dev == SNDRV_MINOR_GLOBAL) {
			/* /dev/aloadSEQ */
			snd_request_other(minor);
		}
#ifndef CONFIG_SND_DYNAMIC_MINORS
		/* /dev/snd/{controlC?,seq} */
		mptr = snd_minors[minor];
		if (mptr == NULL)
#endif
#endif
			return -ENODEV;
	}
	old_fops = file->f_op;
	file->f_op = fops_get(mptr->f_ops);
	if (file->f_op->open)
		err = file->f_op->open(inode, file);
	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
	return err;
}

static struct file_operations snd_fops =
{
	.owner =	THIS_MODULE,
	.open =		snd_open
};

#ifdef CONFIG_SND_DYNAMIC_MINORS
static int snd_find_free_minor(void)
{
	int minor;

	for (minor = 0; minor < ARRAY_SIZE(snd_minors); ++minor) {
		/* skip minors still used statically for autoloading devices */
		if (SNDRV_MINOR_DEVICE(minor) == SNDRV_MINOR_CONTROL ||
		    minor == SNDRV_MINOR_SEQUENCER)
			continue;
		if (!snd_minors[minor])
			return minor;
	}
	return -EBUSY;
}
#else
static int snd_kernel_minor(int type, struct snd_card *card, int dev)
{
	int minor;

	switch (type) {
	case SNDRV_DEVICE_TYPE_SEQUENCER:
	case SNDRV_DEVICE_TYPE_TIMER:
		minor = type;
		break;
	case SNDRV_DEVICE_TYPE_CONTROL:
		snd_assert(card != NULL, return -EINVAL);
		minor = SNDRV_MINOR(card->number, type);
		break;
	case SNDRV_DEVICE_TYPE_HWDEP:
	case SNDRV_DEVICE_TYPE_RAWMIDI:
	case SNDRV_DEVICE_TYPE_PCM_PLAYBACK:
	case SNDRV_DEVICE_TYPE_PCM_CAPTURE:
		snd_assert(card != NULL, return -EINVAL);
		minor = SNDRV_MINOR(card->number, type + dev);
		break;
	default:
		return -EINVAL;
	}
	snd_assert(minor >= 0 && minor < SNDRV_OS_MINORS, return -EINVAL);
	return minor;
}
#endif

/**
 * snd_register_device - Register the ALSA device file for the card
 * @type: the device type, SNDRV_DEVICE_TYPE_XXX
 * @card: the card instance
 * @dev: the device index
 * @f_ops: the file operations
 * @private_data: user pointer for f_ops->open()
 * @name: the device file name
 *
 * Registers an ALSA device file for the given card.
 * The operators have to be set in reg parameter.
 *
 * Retrurns zero if successful, or a negative error code on failure.
 */
int snd_register_device(int type, struct snd_card *card, int dev,
			const struct file_operations *f_ops, void *private_data,
			const char *name)
{
	int minor;
	struct snd_minor *preg;
	struct device *device = NULL;

	snd_assert(name, return -EINVAL);
	preg = kmalloc(sizeof(struct snd_minor) + strlen(name) + 1, GFP_KERNEL);
	if (preg == NULL)
		return -ENOMEM;
	preg->type = type;
	preg->card = card ? card->number : -1;
	preg->device = dev;
	preg->f_ops = f_ops;
	preg->private_data = private_data;
	strcpy(preg->name, name);
	mutex_lock(&sound_mutex);
#ifdef CONFIG_SND_DYNAMIC_MINORS
	minor = snd_find_free_minor();
#else
	minor = snd_kernel_minor(type, card, dev);
	if (minor >= 0 && snd_minors[minor])
		minor = -EBUSY;
#endif
	if (minor < 0) {
		mutex_unlock(&sound_mutex);
		kfree(preg);
		return minor;
	}
	snd_minors[minor] = preg;
	if (type != SNDRV_DEVICE_TYPE_CONTROL || preg->card >= cards_limit)
		devfs_mk_cdev(MKDEV(major, minor), S_IFCHR | device_mode, "snd/%s", name);
	if (card)
		device = card->dev;
	class_device_create(sound_class, NULL, MKDEV(major, minor), device, "%s", name);

	mutex_unlock(&sound_mutex);
	return 0;
}

EXPORT_SYMBOL(snd_register_device);

/**
 * snd_unregister_device - unregister the device on the given card
 * @type: the device type, SNDRV_DEVICE_TYPE_XXX
 * @card: the card instance
 * @dev: the device index
 *
 * Unregisters the device file already registered via
 * snd_register_device().
 *
 * Returns zero if sucecessful, or a negative error code on failure
 */
int snd_unregister_device(int type, struct snd_card *card, int dev)
{
	int cardnum, minor;
	struct snd_minor *mptr;

	cardnum = card ? card->number : -1;
	mutex_lock(&sound_mutex);
	for (minor = 0; minor < ARRAY_SIZE(snd_minors); ++minor)
		if ((mptr = snd_minors[minor]) != NULL &&
		    mptr->type == type &&
		    mptr->card == cardnum &&
		    mptr->device == dev)
			break;
	if (minor == ARRAY_SIZE(snd_minors)) {
		mutex_unlock(&sound_mutex);
		return -EINVAL;
	}

	if (mptr->type != SNDRV_DEVICE_TYPE_CONTROL ||
	    mptr->card >= cards_limit)			/* created in sound.c */
		devfs_remove("snd/%s", mptr->name);
	class_device_destroy(sound_class, MKDEV(major, minor));

	snd_minors[minor] = NULL;
	mutex_unlock(&sound_mutex);
	kfree(mptr);
	return 0;
}

EXPORT_SYMBOL(snd_unregister_device);

#ifdef CONFIG_PROC_FS
/*
 *  INFO PART
 */

static struct snd_info_entry *snd_minor_info_entry;

static const char *snd_device_type_name(int type)
{
	switch (type) {
	case SNDRV_DEVICE_TYPE_CONTROL:
		return "control";
	case SNDRV_DEVICE_TYPE_HWDEP:
		return "hardware dependent";
	case SNDRV_DEVICE_TYPE_RAWMIDI:
		return "raw midi";
	case SNDRV_DEVICE_TYPE_PCM_PLAYBACK:
		return "digital audio playback";
	case SNDRV_DEVICE_TYPE_PCM_CAPTURE:
		return "digital audio capture";
	case SNDRV_DEVICE_TYPE_SEQUENCER:
		return "sequencer";
	case SNDRV_DEVICE_TYPE_TIMER:
		return "timer";
	default:
		return "?";
	}
}

static void snd_minor_info_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	int minor;
	struct snd_minor *mptr;

	mutex_lock(&sound_mutex);
	for (minor = 0; minor < SNDRV_OS_MINORS; ++minor) {
		if (!(mptr = snd_minors[minor]))
			continue;
		if (mptr->card >= 0) {
			if (mptr->device >= 0)
				snd_iprintf(buffer, "%3i: [%2i-%2i]: %s\n",
					    minor, mptr->card, mptr->device,
					    snd_device_type_name(mptr->type));
			else
				snd_iprintf(buffer, "%3i: [%2i]   : %s\n",
					    minor, mptr->card,
					    snd_device_type_name(mptr->type));
		} else
			snd_iprintf(buffer, "%3i:        : %s\n", minor,
				    snd_device_type_name(mptr->type));
	}
	mutex_unlock(&sound_mutex);
}

int __init snd_minor_info_init(void)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "devices", NULL);
	if (entry) {
		entry->c.text.read = snd_minor_info_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	snd_minor_info_entry = entry;
	return 0;
}

int __exit snd_minor_info_done(void)
{
	if (snd_minor_info_entry)
		snd_info_unregister(snd_minor_info_entry);
	return 0;
}
#endif /* CONFIG_PROC_FS */

/*
 *  INIT PART
 */

static int __init alsa_sound_init(void)
{
	short controlnum;

	snd_major = major;
	snd_ecards_limit = cards_limit;
	devfs_mk_dir("snd");
	if (register_chrdev(major, "alsa", &snd_fops)) {
		snd_printk(KERN_ERR "unable to register native major device number %d\n", major);
		devfs_remove("snd");
		return -EIO;
	}
	if (snd_info_init() < 0) {
		unregister_chrdev(major, "alsa");
		devfs_remove("snd");
		return -ENOMEM;
	}
	snd_info_minor_register();
	for (controlnum = 0; controlnum < cards_limit; controlnum++)
		devfs_mk_cdev(MKDEV(major, controlnum<<5), S_IFCHR | device_mode, "snd/controlC%d", controlnum);
#ifndef MODULE
	printk(KERN_INFO "Advanced Linux Sound Architecture Driver Version " CONFIG_SND_VERSION CONFIG_SND_DATE ".\n");
#endif
	return 0;
}

static void __exit alsa_sound_exit(void)
{
	short controlnum;

	for (controlnum = 0; controlnum < cards_limit; controlnum++)
		devfs_remove("snd/controlC%d", controlnum);

	snd_info_minor_unregister();
	snd_info_done();
	if (unregister_chrdev(major, "alsa") != 0)
		snd_printk(KERN_ERR "unable to unregister major device number %d\n", major);
	devfs_remove("snd");
}

module_init(alsa_sound_init)
module_exit(alsa_sound_exit)
