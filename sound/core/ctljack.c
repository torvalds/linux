// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Helper functions for jack-detection kcontrols
 *
 * Copyright (c) 2011 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/control.h>

#define jack_detect_kctl_info	snd_ctl_boolean_mono_info

static int jack_detect_kctl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kcontrol->private_value;
	return 0;
}

static const struct snd_kcontrol_new jack_detect_kctl = {
	/* name is filled later */
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = jack_detect_kctl_info,
	.get = jack_detect_kctl_get,
};

static int get_available_index(struct snd_card *card, const char *name)
{
	struct snd_ctl_elem_id sid;

	memset(&sid, 0, sizeof(sid));

	sid.index = 0;
	sid.iface = SNDRV_CTL_ELEM_IFACE_CARD;
	strscpy(sid.name, name, sizeof(sid.name));

	while (snd_ctl_find_id(card, &sid)) {
		sid.index++;
		/* reset numid; otherwise snd_ctl_find_id() hits this again */
		sid.numid = 0;
	}

	return sid.index;
}

static void jack_kctl_name_gen(char *name, const char *src_name, size_t size)
{
	size_t count = strlen(src_name);
	const char *suf = " Jack";
	size_t suf_len = strlen(suf);
	bool append_suf = true;

	if (count >= suf_len)
		append_suf = strncmp(&src_name[count - suf_len], suf, suf_len) != 0;

	if (append_suf)
		snprintf(name, size, "%s%s", src_name, suf);
	else
		strscpy(name, src_name, size);
}

struct snd_kcontrol *
snd_kctl_jack_new(const char *name, struct snd_card *card)
{
	struct snd_kcontrol *kctl;

	kctl = snd_ctl_new1(&jack_detect_kctl, NULL);
	if (!kctl)
		return NULL;

	jack_kctl_name_gen(kctl->id.name, name, sizeof(kctl->id.name));
	kctl->id.index = get_available_index(card, kctl->id.name);
	kctl->private_value = 0;
	return kctl;
}

void snd_kctl_jack_report(struct snd_card *card,
			  struct snd_kcontrol *kctl, bool status)
{
	if (kctl->private_value == status)
		return;
	kctl->private_value = status;
	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE, &kctl->id);
}
