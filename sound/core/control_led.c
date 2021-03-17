// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  LED state routines for driver control interface
 *  Copyright (c) 2021 by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <sound/core.h>
#include <sound/control.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("ALSA control interface to LED trigger code.");
MODULE_LICENSE("GPL");

#define MAX_LED (((SNDRV_CTL_ELEM_ACCESS_MIC_LED - SNDRV_CTL_ELEM_ACCESS_SPK_LED) \
			>> SNDRV_CTL_ELEM_ACCESS_LED_SHIFT) + 1)

struct snd_ctl_led {
	struct list_head list;
	struct snd_card *card;
	unsigned int access;
	struct snd_kcontrol *kctl;
	unsigned int index_offset;
};

static DEFINE_MUTEX(snd_ctl_led_mutex);
static struct list_head snd_ctl_led_controls[MAX_LED];
static bool snd_ctl_led_card_valid[SNDRV_CARDS];

#define UPDATE_ROUTE(route, cb) \
	do { \
		int route2 = (cb); \
		if (route2 >= 0) \
			route = route < 0 ? route2 : (route | route2); \
	} while (0)

static inline unsigned int access_to_group(unsigned int access)
{
	return ((access & SNDRV_CTL_ELEM_ACCESS_LED_MASK) >>
				SNDRV_CTL_ELEM_ACCESS_LED_SHIFT) - 1;
}

static inline unsigned int group_to_access(unsigned int group)
{
	return (group + 1) << SNDRV_CTL_ELEM_ACCESS_LED_SHIFT;
}

static struct list_head *snd_ctl_led_controls_by_access(unsigned int access)
{
	unsigned int group = access_to_group(access);
	if (group >= MAX_LED)
		return NULL;
	return &snd_ctl_led_controls[group];
}

static int snd_ctl_led_get(struct snd_ctl_led *lctl)
{
	struct snd_kcontrol *kctl = lctl->kctl;
	struct snd_ctl_elem_info info;
	struct snd_ctl_elem_value value;
	unsigned int i;
	int result;

	memset(&info, 0, sizeof(info));
	info.id = kctl->id;
	info.id.index += lctl->index_offset;
	info.id.numid += lctl->index_offset;
	result = kctl->info(kctl, &info);
	if (result < 0)
		return -1;
	memset(&value, 0, sizeof(value));
	value.id = info.id;
	result = kctl->get(kctl, &value);
	if (result < 0)
		return -1;
	if (info.type == SNDRV_CTL_ELEM_TYPE_BOOLEAN ||
	    info.type == SNDRV_CTL_ELEM_TYPE_INTEGER) {
		for (i = 0; i < info.count; i++)
			if (value.value.integer.value[i] != info.value.integer.min)
				return 1;
	} else if (info.type == SNDRV_CTL_ELEM_TYPE_INTEGER64) {
		for (i = 0; i < info.count; i++)
			if (value.value.integer64.value[i] != info.value.integer64.min)
				return 1;
	}
	return 0;
}

static void snd_ctl_led_set_state(struct snd_card *card, unsigned int access,
				  struct snd_kcontrol *kctl, unsigned int ioff)
{
	struct list_head *controls;
	struct snd_ctl_led *lctl;
	enum led_audio led_trigger_type;
	int route;
	bool found;

	controls = snd_ctl_led_controls_by_access(access);
	if (!controls)
		return;
	if (access == SNDRV_CTL_ELEM_ACCESS_SPK_LED) {
		led_trigger_type = LED_AUDIO_MUTE;
	} else if (access == SNDRV_CTL_ELEM_ACCESS_MIC_LED) {
		led_trigger_type = LED_AUDIO_MICMUTE;
	} else {
		return;
	}
	route = -1;
	found = false;
	mutex_lock(&snd_ctl_led_mutex);
	/* the card may not be registered (active) at this point */
	if (card && !snd_ctl_led_card_valid[card->number]) {
		mutex_unlock(&snd_ctl_led_mutex);
		return;
	}
	list_for_each_entry(lctl, controls, list) {
		if (lctl->kctl == kctl && lctl->index_offset == ioff)
			found = true;
		UPDATE_ROUTE(route, snd_ctl_led_get(lctl));
	}
	if (!found && kctl && card) {
		lctl = kzalloc(sizeof(*lctl), GFP_KERNEL);
		if (lctl) {
			lctl->card = card;
			lctl->access = access;
			lctl->kctl = kctl;
			lctl->index_offset = ioff;
			list_add(&lctl->list, controls);
			UPDATE_ROUTE(route, snd_ctl_led_get(lctl));
		}
	}
	mutex_unlock(&snd_ctl_led_mutex);
	if (route >= 0)
		ledtrig_audio_set(led_trigger_type, route ? LED_OFF : LED_ON);
}

static struct snd_ctl_led *snd_ctl_led_find(struct snd_kcontrol *kctl, unsigned int ioff)
{
	struct list_head *controls;
	struct snd_ctl_led *lctl;
	unsigned int group;

	for (group = 0; group < MAX_LED; group++) {
		controls = &snd_ctl_led_controls[group];
		list_for_each_entry(lctl, controls, list)
			if (lctl->kctl == kctl && lctl->index_offset == ioff)
				return lctl;
	}
	return NULL;
}

static unsigned int snd_ctl_led_remove(struct snd_kcontrol *kctl, unsigned int ioff,
				       unsigned int access)
{
	struct snd_ctl_led *lctl;
	unsigned int ret = 0;

	mutex_lock(&snd_ctl_led_mutex);
	lctl = snd_ctl_led_find(kctl, ioff);
	if (lctl && (access == 0 || access != lctl->access)) {
		ret = lctl->access;
		list_del(&lctl->list);
		kfree(lctl);
	}
	mutex_unlock(&snd_ctl_led_mutex);
	return ret;
}

static void snd_ctl_led_notify(struct snd_card *card, unsigned int mask,
			       struct snd_kcontrol *kctl, unsigned int ioff)
{
	struct snd_kcontrol_volatile *vd;
	unsigned int access, access2;

	if (mask == SNDRV_CTL_EVENT_MASK_REMOVE) {
		access = snd_ctl_led_remove(kctl, ioff, 0);
		if (access)
			snd_ctl_led_set_state(card, access, NULL, 0);
	} else if (mask & SNDRV_CTL_EVENT_MASK_INFO) {
		vd = &kctl->vd[ioff];
		access = vd->access & SNDRV_CTL_ELEM_ACCESS_LED_MASK;
		access2 = snd_ctl_led_remove(kctl, ioff, access);
		if (access2)
			snd_ctl_led_set_state(card, access2, NULL, 0);
		if (access)
			snd_ctl_led_set_state(card, access, kctl, ioff);
	} else if ((mask & (SNDRV_CTL_EVENT_MASK_ADD |
			    SNDRV_CTL_EVENT_MASK_VALUE)) != 0) {
		vd = &kctl->vd[ioff];
		access = vd->access & SNDRV_CTL_ELEM_ACCESS_LED_MASK;
		if (access)
			snd_ctl_led_set_state(card, access, kctl, ioff);
	}
}

static void snd_ctl_led_refresh(void)
{
	unsigned int group;

	for (group = 0; group < MAX_LED; group++)
		snd_ctl_led_set_state(NULL, group_to_access(group), NULL, 0);
}

static void snd_ctl_led_clean(struct snd_card *card)
{
	unsigned int group;
	struct list_head *controls;
	struct snd_ctl_led *lctl;

	for (group = 0; group < MAX_LED; group++) {
		controls = &snd_ctl_led_controls[group];
repeat:
		list_for_each_entry(lctl, controls, list)
			if (!card || lctl->card == card) {
				list_del(&lctl->list);
				kfree(lctl);
				goto repeat;
			}
	}
}

static void snd_ctl_led_register(struct snd_card *card)
{
	struct snd_kcontrol *kctl;
	unsigned int ioff;

	if (snd_BUG_ON(card->number < 0 ||
		       card->number >= ARRAY_SIZE(snd_ctl_led_card_valid)))
		return;
	mutex_lock(&snd_ctl_led_mutex);
	snd_ctl_led_card_valid[card->number] = true;
	mutex_unlock(&snd_ctl_led_mutex);
	/* the register callback is already called with held card->controls_rwsem */
	list_for_each_entry(kctl, &card->controls, list)
		for (ioff = 0; ioff < kctl->count; ioff++)
			snd_ctl_led_notify(card, SNDRV_CTL_EVENT_MASK_VALUE, kctl, ioff);
	snd_ctl_led_refresh();
}

static void snd_ctl_led_disconnect(struct snd_card *card)
{
	mutex_lock(&snd_ctl_led_mutex);
	snd_ctl_led_card_valid[card->number] = false;
	snd_ctl_led_clean(card);
	mutex_unlock(&snd_ctl_led_mutex);
	snd_ctl_led_refresh();
}

/*
 * Control layer registration
 */
static struct snd_ctl_layer_ops snd_ctl_led_lops = {
	.module_name = SND_CTL_LAYER_MODULE_LED,
	.lregister = snd_ctl_led_register,
	.ldisconnect = snd_ctl_led_disconnect,
	.lnotify = snd_ctl_led_notify,
};

static int __init snd_ctl_led_init(void)
{
	unsigned int group;

	for (group = 0; group < MAX_LED; group++)
		INIT_LIST_HEAD(&snd_ctl_led_controls[group]);
	snd_ctl_register_layer(&snd_ctl_led_lops);
	return 0;
}

static void __exit snd_ctl_led_exit(void)
{
	snd_ctl_disconnect_layer(&snd_ctl_led_lops);
	snd_ctl_led_clean(NULL);
}

module_init(snd_ctl_led_init)
module_exit(snd_ctl_led_exit)
