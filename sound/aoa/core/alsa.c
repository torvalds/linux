// SPDX-License-Identifier: GPL-2.0-only
/*
 * Apple Onboard Audio Alsa helpers
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 */
#include <linux/module.h>
#include "alsa.h"

static int index = -1;
module_param(index, int, 0444);
MODULE_PARM_DESC(index, "index for AOA sound card.");

static struct aoa_card *aoa_card;

int aoa_alsa_init(char *name, struct module *mod, struct device *dev)
{
	struct snd_card *alsa_card;
	int err;

	if (aoa_card)
		/* cannot be EEXIST due to usage in aoa_fabric_register */
		return -EBUSY;

	err = snd_card_new(dev, index, name, mod, sizeof(struct aoa_card),
			   &alsa_card);
	if (err < 0)
		return err;
	aoa_card = alsa_card->private_data;
	aoa_card->alsa_card = alsa_card;
	strlcpy(alsa_card->driver, "AppleOnbdAudio", sizeof(alsa_card->driver));
	strlcpy(alsa_card->shortname, name, sizeof(alsa_card->shortname));
	strlcpy(alsa_card->longname, name, sizeof(alsa_card->longname));
	strlcpy(alsa_card->mixername, name, sizeof(alsa_card->mixername));
	err = snd_card_register(aoa_card->alsa_card);
	if (err < 0) {
		printk(KERN_ERR "snd-aoa: couldn't register alsa card\n");
		snd_card_free(aoa_card->alsa_card);
		aoa_card = NULL;
		return err;
	}
	return 0;
}

struct snd_card *aoa_get_card(void)
{
	if (aoa_card)
		return aoa_card->alsa_card;
	return NULL;
}
EXPORT_SYMBOL_GPL(aoa_get_card);

void aoa_alsa_cleanup(void)
{
	if (aoa_card) {
		snd_card_free(aoa_card->alsa_card);
		aoa_card = NULL;
	}
}

int aoa_snd_device_new(enum snd_device_type type,
		       void *device_data, const struct snd_device_ops *ops)
{
	struct snd_card *card = aoa_get_card();
	int err;

	if (!card) return -ENOMEM;

	err = snd_device_new(card, type, device_data, ops);
	if (err) {
		printk(KERN_ERR "snd-aoa: failed to create snd device (%d)\n", err);
		return err;
	}
	err = snd_device_register(card, device_data);
	if (err) {
		printk(KERN_ERR "snd-aoa: failed to register "
				"snd device (%d)\n", err);
		printk(KERN_ERR "snd-aoa: have you forgotten the "
				"dev_register callback?\n");
		snd_device_free(card, device_data);
	}
	return err;
}
EXPORT_SYMBOL_GPL(aoa_snd_device_new);

int aoa_snd_ctl_add(struct snd_kcontrol* control)
{
	int err;

	if (!aoa_card) return -ENODEV;

	err = snd_ctl_add(aoa_card->alsa_card, control);
	if (err)
		printk(KERN_ERR "snd-aoa: failed to add alsa control (%d)\n",
		       err);
	return err;
}
EXPORT_SYMBOL_GPL(aoa_snd_ctl_add);
