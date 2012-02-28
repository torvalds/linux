/*
 * Helper functions for jack-detection kcontrols
 *
 * Copyright (c) 2011 Takashi Iwai <tiwai@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <sound/core.h>
#include <sound/control.h>

#define jack_detect_kctl_info	snd_ctl_boolean_mono_info

static int jack_detect_kctl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kcontrol->private_value;
	return 0;
}

static struct snd_kcontrol_new jack_detect_kctl = {
	/* name is filled later */
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = jack_detect_kctl_info,
	.get = jack_detect_kctl_get,
};

struct snd_kcontrol *
snd_kctl_jack_new(const char *name, int idx, void *private_data)
{
	struct snd_kcontrol *kctl;
	kctl = snd_ctl_new1(&jack_detect_kctl, private_data);
	if (!kctl)
		return NULL;
	snprintf(kctl->id.name, sizeof(kctl->id.name), "%s Jack", name);
	kctl->id.index = idx;
	kctl->private_value = 0;
	return kctl;
}
EXPORT_SYMBOL_GPL(snd_kctl_jack_new);

void snd_kctl_jack_report(struct snd_card *card,
			  struct snd_kcontrol *kctl, bool status)
{
	if (kctl->private_value == status)
		return;
	kctl->private_value = status;
	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE, &kctl->id);
}
EXPORT_SYMBOL_GPL(snd_kctl_jack_report);
