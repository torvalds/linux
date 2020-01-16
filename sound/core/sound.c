// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Advanced Linux Sound Architecture
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/device.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/miyesrs.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <linux/kmod.h>
#include <linux/mutex.h>

static int major = CONFIG_SND_MAJOR;
int snd_major;
EXPORT_SYMBOL(snd_major);

static int cards_limit = 1;

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Advanced Linux Sound Architecture driver for soundcards.");
MODULE_LICENSE("GPL");
module_param(major, int, 0444);
MODULE_PARM_DESC(major, "Major # for sound driver.");
module_param(cards_limit, int, 0444);
MODULE_PARM_DESC(cards_limit, "Count of auto-loadable soundcards.");
MODULE_ALIAS_CHARDEV_MAJOR(CONFIG_SND_MAJOR);

/* this one holds the actual max. card number currently available.
 * as default, it's identical with cards_limit option.  when more
 * modules are loaded manually, this limit number increases, too.
 */
int snd_ecards_limit;
EXPORT_SYMBOL(snd_ecards_limit);

static struct snd_miyesr *snd_miyesrs[SNDRV_OS_MINORS];
static DEFINE_MUTEX(sound_mutex);

#ifdef CONFIG_MODULES

/**
 * snd_request_card - try to load the card module
 * @card: the card number
 *
 * Tries to load the module "snd-card-X" for the given card number
 * via request_module.  Returns immediately if already loaded.
 */
void snd_request_card(int card)
{
	if (snd_card_locked(card))
		return;
	if (card < 0 || card >= cards_limit)
		return;
	request_module("snd-card-%i", card);
}
EXPORT_SYMBOL(snd_request_card);

static void snd_request_other(int miyesr)
{
	char *str;

	switch (miyesr) {
	case SNDRV_MINOR_SEQUENCER:	str = "snd-seq";	break;
	case SNDRV_MINOR_TIMER:		str = "snd-timer";	break;
	default:			return;
	}
	request_module(str);
}

#endif	/* modular kernel */

/**
 * snd_lookup_miyesr_data - get user data of a registered device
 * @miyesr: the miyesr number
 * @type: device type (SNDRV_DEVICE_TYPE_XXX)
 *
 * Checks that a miyesr device with the specified type is registered, and returns
 * its user data pointer.
 *
 * This function increments the reference counter of the card instance
 * if an associated instance with the given miyesr number and type is found.
 * The caller must call snd_card_unref() appropriately later.
 *
 * Return: The user data pointer if the specified device is found. %NULL
 * otherwise.
 */
void *snd_lookup_miyesr_data(unsigned int miyesr, int type)
{
	struct snd_miyesr *mreg;
	void *private_data;

	if (miyesr >= ARRAY_SIZE(snd_miyesrs))
		return NULL;
	mutex_lock(&sound_mutex);
	mreg = snd_miyesrs[miyesr];
	if (mreg && mreg->type == type) {
		private_data = mreg->private_data;
		if (private_data && mreg->card_ptr)
			get_device(&mreg->card_ptr->card_dev);
	} else
		private_data = NULL;
	mutex_unlock(&sound_mutex);
	return private_data;
}
EXPORT_SYMBOL(snd_lookup_miyesr_data);

#ifdef CONFIG_MODULES
static struct snd_miyesr *autoload_device(unsigned int miyesr)
{
	int dev;
	mutex_unlock(&sound_mutex); /* release lock temporarily */
	dev = SNDRV_MINOR_DEVICE(miyesr);
	if (dev == SNDRV_MINOR_CONTROL) {
		/* /dev/aloadC? */
		int card = SNDRV_MINOR_CARD(miyesr);
		struct snd_card *ref = snd_card_ref(card);
		if (!ref)
			snd_request_card(card);
		else
			snd_card_unref(ref);
	} else if (dev == SNDRV_MINOR_GLOBAL) {
		/* /dev/aloadSEQ */
		snd_request_other(miyesr);
	}
	mutex_lock(&sound_mutex); /* reacuire lock */
	return snd_miyesrs[miyesr];
}
#else /* !CONFIG_MODULES */
#define autoload_device(miyesr)	NULL
#endif /* CONFIG_MODULES */

static int snd_open(struct iyesde *iyesde, struct file *file)
{
	unsigned int miyesr = imiyesr(iyesde);
	struct snd_miyesr *mptr = NULL;
	const struct file_operations *new_fops;
	int err = 0;

	if (miyesr >= ARRAY_SIZE(snd_miyesrs))
		return -ENODEV;
	mutex_lock(&sound_mutex);
	mptr = snd_miyesrs[miyesr];
	if (mptr == NULL) {
		mptr = autoload_device(miyesr);
		if (!mptr) {
			mutex_unlock(&sound_mutex);
			return -ENODEV;
		}
	}
	new_fops = fops_get(mptr->f_ops);
	mutex_unlock(&sound_mutex);
	if (!new_fops)
		return -ENODEV;
	replace_fops(file, new_fops);

	if (file->f_op->open)
		err = file->f_op->open(iyesde, file);
	return err;
}

static const struct file_operations snd_fops =
{
	.owner =	THIS_MODULE,
	.open =		snd_open,
	.llseek =	yesop_llseek,
};

#ifdef CONFIG_SND_DYNAMIC_MINORS
static int snd_find_free_miyesr(int type, struct snd_card *card, int dev)
{
	int miyesr;

	/* static miyesrs for module auto loading */
	if (type == SNDRV_DEVICE_TYPE_SEQUENCER)
		return SNDRV_MINOR_SEQUENCER;
	if (type == SNDRV_DEVICE_TYPE_TIMER)
		return SNDRV_MINOR_TIMER;

	for (miyesr = 0; miyesr < ARRAY_SIZE(snd_miyesrs); ++miyesr) {
		/* skip static miyesrs still used for module auto loading */
		if (SNDRV_MINOR_DEVICE(miyesr) == SNDRV_MINOR_CONTROL)
			continue;
		if (miyesr == SNDRV_MINOR_SEQUENCER ||
		    miyesr == SNDRV_MINOR_TIMER)
			continue;
		if (!snd_miyesrs[miyesr])
			return miyesr;
	}
	return -EBUSY;
}
#else
static int snd_find_free_miyesr(int type, struct snd_card *card, int dev)
{
	int miyesr;

	switch (type) {
	case SNDRV_DEVICE_TYPE_SEQUENCER:
	case SNDRV_DEVICE_TYPE_TIMER:
		miyesr = type;
		break;
	case SNDRV_DEVICE_TYPE_CONTROL:
		if (snd_BUG_ON(!card))
			return -EINVAL;
		miyesr = SNDRV_MINOR(card->number, type);
		break;
	case SNDRV_DEVICE_TYPE_HWDEP:
	case SNDRV_DEVICE_TYPE_RAWMIDI:
	case SNDRV_DEVICE_TYPE_PCM_PLAYBACK:
	case SNDRV_DEVICE_TYPE_PCM_CAPTURE:
	case SNDRV_DEVICE_TYPE_COMPRESS:
		if (snd_BUG_ON(!card))
			return -EINVAL;
		miyesr = SNDRV_MINOR(card->number, type + dev);
		break;
	default:
		return -EINVAL;
	}
	if (snd_BUG_ON(miyesr < 0 || miyesr >= SNDRV_OS_MINORS))
		return -EINVAL;
	if (snd_miyesrs[miyesr])
		return -EBUSY;
	return miyesr;
}
#endif

/**
 * snd_register_device - Register the ALSA device file for the card
 * @type: the device type, SNDRV_DEVICE_TYPE_XXX
 * @card: the card instance
 * @dev: the device index
 * @f_ops: the file operations
 * @private_data: user pointer for f_ops->open()
 * @device: the device to register
 *
 * Registers an ALSA device file for the given card.
 * The operators have to be set in reg parameter.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_register_device(int type, struct snd_card *card, int dev,
			const struct file_operations *f_ops,
			void *private_data, struct device *device)
{
	int miyesr;
	int err = 0;
	struct snd_miyesr *preg;

	if (snd_BUG_ON(!device))
		return -EINVAL;

	preg = kmalloc(sizeof *preg, GFP_KERNEL);
	if (preg == NULL)
		return -ENOMEM;
	preg->type = type;
	preg->card = card ? card->number : -1;
	preg->device = dev;
	preg->f_ops = f_ops;
	preg->private_data = private_data;
	preg->card_ptr = card;
	mutex_lock(&sound_mutex);
	miyesr = snd_find_free_miyesr(type, card, dev);
	if (miyesr < 0) {
		err = miyesr;
		goto error;
	}

	preg->dev = device;
	device->devt = MKDEV(major, miyesr);
	err = device_add(device);
	if (err < 0)
		goto error;

	snd_miyesrs[miyesr] = preg;
 error:
	mutex_unlock(&sound_mutex);
	if (err < 0)
		kfree(preg);
	return err;
}
EXPORT_SYMBOL(snd_register_device);

/**
 * snd_unregister_device - unregister the device on the given card
 * @dev: the device instance
 *
 * Unregisters the device file already registered via
 * snd_register_device().
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_unregister_device(struct device *dev)
{
	int miyesr;
	struct snd_miyesr *preg;

	mutex_lock(&sound_mutex);
	for (miyesr = 0; miyesr < ARRAY_SIZE(snd_miyesrs); ++miyesr) {
		preg = snd_miyesrs[miyesr];
		if (preg && preg->dev == dev) {
			snd_miyesrs[miyesr] = NULL;
			device_del(dev);
			kfree(preg);
			break;
		}
	}
	mutex_unlock(&sound_mutex);
	if (miyesr >= ARRAY_SIZE(snd_miyesrs))
		return -ENOENT;
	return 0;
}
EXPORT_SYMBOL(snd_unregister_device);

#ifdef CONFIG_SND_PROC_FS
/*
 *  INFO PART
 */
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

static void snd_miyesr_info_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	int miyesr;
	struct snd_miyesr *mptr;

	mutex_lock(&sound_mutex);
	for (miyesr = 0; miyesr < SNDRV_OS_MINORS; ++miyesr) {
		if (!(mptr = snd_miyesrs[miyesr]))
			continue;
		if (mptr->card >= 0) {
			if (mptr->device >= 0)
				snd_iprintf(buffer, "%3i: [%2i-%2i]: %s\n",
					    miyesr, mptr->card, mptr->device,
					    snd_device_type_name(mptr->type));
			else
				snd_iprintf(buffer, "%3i: [%2i]   : %s\n",
					    miyesr, mptr->card,
					    snd_device_type_name(mptr->type));
		} else
			snd_iprintf(buffer, "%3i:        : %s\n", miyesr,
				    snd_device_type_name(mptr->type));
	}
	mutex_unlock(&sound_mutex);
}

int __init snd_miyesr_info_init(void)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "devices", NULL);
	if (!entry)
		return -ENOMEM;
	entry->c.text.read = snd_miyesr_info_read;
	return snd_info_register(entry); /* freed in error path */
}
#endif /* CONFIG_SND_PROC_FS */

/*
 *  INIT PART
 */

static int __init alsa_sound_init(void)
{
	snd_major = major;
	snd_ecards_limit = cards_limit;
	if (register_chrdev(major, "alsa", &snd_fops)) {
		pr_err("ALSA core: unable to register native major device number %d\n", major);
		return -EIO;
	}
	if (snd_info_init() < 0) {
		unregister_chrdev(major, "alsa");
		return -ENOMEM;
	}
#ifndef MODULE
	pr_info("Advanced Linux Sound Architecture Driver Initialized.\n");
#endif
	return 0;
}

static void __exit alsa_sound_exit(void)
{
	snd_info_done();
	unregister_chrdev(major, "alsa");
}

subsys_initcall(alsa_sound_init);
module_exit(alsa_sound_exit);
