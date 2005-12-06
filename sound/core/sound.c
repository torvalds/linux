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
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <sound/version.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <linux/kmod.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/platform_device.h>

#define SNDRV_OS_MINORS 256

static int major = CONFIG_SND_MAJOR;
int snd_major;
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

static struct list_head snd_minors_hash[SNDRV_CARDS];

static DECLARE_MUTEX(sound_mutex);

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
	int locked;

	if (! current->fs->root)
		return;
	read_lock(&snd_card_rwlock);
	locked = snd_cards_lock & (1 << card);
	read_unlock(&snd_card_rwlock);
	if (locked)
		return;
	if (card < 0 || card >= cards_limit)
		return;
	request_module("snd-card-%i", card);
}

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

static snd_minor_t *snd_minor_search(int minor)
{
	struct list_head *list;
	snd_minor_t *mptr;

	list_for_each(list, &snd_minors_hash[SNDRV_MINOR_CARD(minor)]) {
		mptr = list_entry(list, snd_minor_t, list);
		if (mptr->number == minor)
			return mptr;
	}
	return NULL;
}

static int snd_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	int card = SNDRV_MINOR_CARD(minor);
	int dev = SNDRV_MINOR_DEVICE(minor);
	snd_minor_t *mptr = NULL;
	struct file_operations *old_fops;
	int err = 0;

	if (dev != SNDRV_MINOR_GLOBAL) {
		if (snd_cards[card] == NULL) {
#ifdef CONFIG_KMOD
			snd_request_card(card);
			if (snd_cards[card] == NULL)
#endif
				return -ENODEV;
		}
	} else {
#ifdef CONFIG_KMOD
		if ((mptr = snd_minor_search(minor)) == NULL)
			snd_request_other(minor);
#endif
	}
	if (mptr == NULL && (mptr = snd_minor_search(minor)) == NULL)
		return -ENODEV;
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

static int snd_kernel_minor(int type, snd_card_t * card, int dev)
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

/**
 * snd_register_device - Register the ALSA device file for the card
 * @type: the device type, SNDRV_DEVICE_TYPE_XXX
 * @card: the card instance
 * @dev: the device index
 * @reg: the snd_minor_t record
 * @name: the device file name
 *
 * Registers an ALSA device file for the given card.
 * The operators have to be set in reg parameter.
 *
 * Retrurns zero if successful, or a negative error code on failure.
 */
int snd_register_device(int type, snd_card_t * card, int dev, snd_minor_t * reg, const char *name)
{
	int minor = snd_kernel_minor(type, card, dev);
	snd_minor_t *preg;
	struct device *device = NULL;

	if (minor < 0)
		return minor;
	snd_assert(name, return -EINVAL);
	preg = (snd_minor_t *)kmalloc(sizeof(snd_minor_t) + strlen(name) + 1, GFP_KERNEL);
	if (preg == NULL)
		return -ENOMEM;
	*preg = *reg;
	preg->number = minor;
	preg->device = dev;
	strcpy(preg->name, name);
	down(&sound_mutex);
	if (snd_minor_search(minor)) {
		up(&sound_mutex);
		kfree(preg);
		return -EBUSY;
	}
	list_add_tail(&preg->list, &snd_minors_hash[SNDRV_MINOR_CARD(minor)]);
	if (strncmp(name, "controlC", 8) || card->number >= cards_limit)
		devfs_mk_cdev(MKDEV(major, minor), S_IFCHR | device_mode, "snd/%s", name);
	if (card)
		device = card->dev;
	class_device_create(sound_class, NULL, MKDEV(major, minor), device, "%s", name);

	up(&sound_mutex);
	return 0;
}

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
int snd_unregister_device(int type, snd_card_t * card, int dev)
{
	int minor = snd_kernel_minor(type, card, dev);
	snd_minor_t *mptr;

	if (minor < 0)
		return minor;
	down(&sound_mutex);
	if ((mptr = snd_minor_search(minor)) == NULL) {
		up(&sound_mutex);
		return -EINVAL;
	}

	if (strncmp(mptr->name, "controlC", 8) || card->number >= cards_limit) /* created in sound.c */
		devfs_remove("snd/%s", mptr->name);
	class_device_destroy(sound_class, MKDEV(major, minor));

	list_del(&mptr->list);
	up(&sound_mutex);
	kfree(mptr);
	return 0;
}

/*
 *  INFO PART
 */

static snd_info_entry_t *snd_minor_info_entry = NULL;

static void snd_minor_info_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	int card, device;
	struct list_head *list;
	snd_minor_t *mptr;

	down(&sound_mutex);
	for (card = 0; card < SNDRV_CARDS; card++) {
		list_for_each(list, &snd_minors_hash[card]) {
			mptr = list_entry(list, snd_minor_t, list);
			if (SNDRV_MINOR_DEVICE(mptr->number) != SNDRV_MINOR_GLOBAL) {
				if ((device = mptr->device) >= 0)
					snd_iprintf(buffer, "%3i: [%i-%2i]: %s\n", mptr->number, card, device, mptr->comment);
				else
					snd_iprintf(buffer, "%3i: [%i]   : %s\n", mptr->number, card, mptr->comment);
			} else {
				snd_iprintf(buffer, "%3i:       : %s\n", mptr->number, mptr->comment);
			}
		}
	}
	up(&sound_mutex);
}

int __init snd_minor_info_init(void)
{
	snd_info_entry_t *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "devices", NULL);
	if (entry) {
		entry->c.text.read_size = PAGE_SIZE;
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

/*
 *  INIT PART
 */

#ifdef CONFIG_SND_GENERIC_DRIVER
extern struct platform_driver snd_generic_driver;
#endif

static int __init alsa_sound_init(void)
{
	short controlnum;
	int err;
	int card;

	snd_major = major;
	snd_ecards_limit = cards_limit;
	for (card = 0; card < SNDRV_CARDS; card++)
		INIT_LIST_HEAD(&snd_minors_hash[card]);
	if ((err = snd_oss_init_module()) < 0)
		return err;
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
#ifdef CONFIG_SND_GENERIC_DRIVER
	platform_driver_register(&snd_generic_driver);
#endif
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

#ifdef CONFIG_SND_GENERIC_DRIVER
	platform_driver_unregister(&snd_generic_driver);
#endif
	snd_info_minor_unregister();
	snd_info_done();
	if (unregister_chrdev(major, "alsa") != 0)
		snd_printk(KERN_ERR "unable to unregister major device number %d\n", major);
	devfs_remove("snd");
}

module_init(alsa_sound_init)
module_exit(alsa_sound_exit)

  /* sound.c */
EXPORT_SYMBOL(snd_major);
EXPORT_SYMBOL(snd_ecards_limit);
#if defined(CONFIG_KMOD)
EXPORT_SYMBOL(snd_request_card);
#endif
EXPORT_SYMBOL(snd_register_device);
EXPORT_SYMBOL(snd_unregister_device);
#if defined(CONFIG_SND_OSSEMUL)
EXPORT_SYMBOL(snd_register_oss_device);
EXPORT_SYMBOL(snd_unregister_oss_device);
#endif
  /* memory.c */
EXPORT_SYMBOL(copy_to_user_fromio);
EXPORT_SYMBOL(copy_from_user_toio);
  /* init.c */
EXPORT_SYMBOL(snd_cards);
#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
EXPORT_SYMBOL(snd_mixer_oss_notify_callback);
#endif
EXPORT_SYMBOL(snd_card_new);
EXPORT_SYMBOL(snd_card_disconnect);
EXPORT_SYMBOL(snd_card_free);
EXPORT_SYMBOL(snd_card_free_in_thread);
EXPORT_SYMBOL(snd_card_register);
EXPORT_SYMBOL(snd_component_add);
EXPORT_SYMBOL(snd_card_file_add);
EXPORT_SYMBOL(snd_card_file_remove);
#ifdef CONFIG_SND_GENERIC_DRIVER
EXPORT_SYMBOL(snd_card_set_generic_dev);
#endif
#ifdef CONFIG_PM
EXPORT_SYMBOL(snd_power_wait);
EXPORT_SYMBOL(snd_card_set_pm_callback);
#ifdef CONFIG_SND_GENERIC_DRIVER
EXPORT_SYMBOL(snd_card_set_generic_pm_callback);
#endif
#ifdef CONFIG_PCI
EXPORT_SYMBOL(snd_card_pci_suspend);
EXPORT_SYMBOL(snd_card_pci_resume);
#endif
#endif
  /* device.c */
EXPORT_SYMBOL(snd_device_new);
EXPORT_SYMBOL(snd_device_register);
EXPORT_SYMBOL(snd_device_free);
  /* isadma.c */
#ifdef CONFIG_ISA_DMA_API
EXPORT_SYMBOL(snd_dma_program);
EXPORT_SYMBOL(snd_dma_disable);
EXPORT_SYMBOL(snd_dma_pointer);
#endif
  /* info.c */
#ifdef CONFIG_PROC_FS
EXPORT_SYMBOL(snd_seq_root);
EXPORT_SYMBOL(snd_iprintf);
EXPORT_SYMBOL(snd_info_get_line);
EXPORT_SYMBOL(snd_info_get_str);
EXPORT_SYMBOL(snd_info_create_module_entry);
EXPORT_SYMBOL(snd_info_create_card_entry);
EXPORT_SYMBOL(snd_info_free_entry);
EXPORT_SYMBOL(snd_info_register);
EXPORT_SYMBOL(snd_info_unregister);
EXPORT_SYMBOL(snd_card_proc_new);
#endif
  /* info_oss.c */
#if defined(CONFIG_SND_OSSEMUL) && defined(CONFIG_PROC_FS)
EXPORT_SYMBOL(snd_oss_info_register);
#endif
  /* control.c */
EXPORT_SYMBOL(snd_ctl_new);
EXPORT_SYMBOL(snd_ctl_new1);
EXPORT_SYMBOL(snd_ctl_free_one);
EXPORT_SYMBOL(snd_ctl_add);
EXPORT_SYMBOL(snd_ctl_remove);
EXPORT_SYMBOL(snd_ctl_remove_id);
EXPORT_SYMBOL(snd_ctl_rename_id);
EXPORT_SYMBOL(snd_ctl_find_numid);
EXPORT_SYMBOL(snd_ctl_find_id);
EXPORT_SYMBOL(snd_ctl_notify);
EXPORT_SYMBOL(snd_ctl_register_ioctl);
EXPORT_SYMBOL(snd_ctl_unregister_ioctl);
#ifdef CONFIG_COMPAT
EXPORT_SYMBOL(snd_ctl_register_ioctl_compat);
EXPORT_SYMBOL(snd_ctl_unregister_ioctl_compat);
#endif
EXPORT_SYMBOL(snd_ctl_elem_read);
EXPORT_SYMBOL(snd_ctl_elem_write);
  /* misc.c */
EXPORT_SYMBOL(release_and_free_resource);
#ifdef CONFIG_SND_VERBOSE_PRINTK
EXPORT_SYMBOL(snd_verbose_printk);
#endif
#if defined(CONFIG_SND_DEBUG) && defined(CONFIG_SND_VERBOSE_PRINTK)
EXPORT_SYMBOL(snd_verbose_printd);
#endif
