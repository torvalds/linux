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

enum snd_ctl_led_mode {
	 MODE_FOLLOW_MUTE = 0,
	 MODE_FOLLOW_ROUTE,
	 MODE_OFF,
	 MODE_ON,
};

struct snd_ctl_led {
	struct device dev;
	struct list_head controls;
	const char *name;
	unsigned int group;
	enum led_audio trigger_type;
	enum snd_ctl_led_mode mode;
};

struct snd_ctl_led_ctl {
	struct list_head list;
	struct snd_card *card;
	unsigned int access;
	struct snd_kcontrol *kctl;
	unsigned int index_offset;
};

static DEFINE_MUTEX(snd_ctl_led_mutex);
static bool snd_ctl_led_card_valid[SNDRV_CARDS];
static struct snd_ctl_led snd_ctl_leds[MAX_LED] = {
	{
		.name = "speaker",
		.group = (SNDRV_CTL_ELEM_ACCESS_SPK_LED >> SNDRV_CTL_ELEM_ACCESS_LED_SHIFT) - 1,
		.trigger_type = LED_AUDIO_MUTE,
		.mode = MODE_FOLLOW_MUTE,
	},
	{
		.name = "mic",
		.group = (SNDRV_CTL_ELEM_ACCESS_MIC_LED >> SNDRV_CTL_ELEM_ACCESS_LED_SHIFT) - 1,
		.trigger_type = LED_AUDIO_MICMUTE,
		.mode = MODE_FOLLOW_MUTE,
	},
};

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

static struct snd_ctl_led *snd_ctl_led_get_by_access(unsigned int access)
{
	unsigned int group = access_to_group(access);
	if (group >= MAX_LED)
		return NULL;
	return &snd_ctl_leds[group];
}

static int snd_ctl_led_get(struct snd_ctl_led_ctl *lctl)
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
	struct snd_ctl_led *led;
	struct snd_ctl_led_ctl *lctl;
	int route;
	bool found;

	led = snd_ctl_led_get_by_access(access);
	if (!led)
		return;
	route = -1;
	found = false;
	mutex_lock(&snd_ctl_led_mutex);
	/* the card may not be registered (active) at this point */
	if (card && !snd_ctl_led_card_valid[card->number]) {
		mutex_unlock(&snd_ctl_led_mutex);
		return;
	}
	list_for_each_entry(lctl, &led->controls, list) {
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
			list_add(&lctl->list, &led->controls);
			UPDATE_ROUTE(route, snd_ctl_led_get(lctl));
		}
	}
	mutex_unlock(&snd_ctl_led_mutex);
	switch (led->mode) {
	case MODE_OFF:		route = 1; break;
	case MODE_ON:		route = 0; break;
	case MODE_FOLLOW_ROUTE:	if (route >= 0) route ^= 1; break;
	case MODE_FOLLOW_MUTE:	/* noop */ break;
	}
	if (route >= 0)
		ledtrig_audio_set(led->trigger_type, route ? LED_OFF : LED_ON);
}

static struct snd_ctl_led_ctl *snd_ctl_led_find(struct snd_kcontrol *kctl, unsigned int ioff)
{
	struct list_head *controls;
	struct snd_ctl_led_ctl *lctl;
	unsigned int group;

	for (group = 0; group < MAX_LED; group++) {
		controls = &snd_ctl_leds[group].controls;
		list_for_each_entry(lctl, controls, list)
			if (lctl->kctl == kctl && lctl->index_offset == ioff)
				return lctl;
	}
	return NULL;
}

static unsigned int snd_ctl_led_remove(struct snd_kcontrol *kctl, unsigned int ioff,
				       unsigned int access)
{
	struct snd_ctl_led_ctl *lctl;
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
	struct snd_ctl_led *led;
	struct snd_ctl_led_ctl *lctl;

	for (group = 0; group < MAX_LED; group++) {
		led = &snd_ctl_leds[group];
repeat:
		list_for_each_entry(lctl, &led->controls, list)
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
 * sysfs
 */

static ssize_t show_mode(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct snd_ctl_led *led = container_of(dev, struct snd_ctl_led, dev);
	const char *str;

	switch (led->mode) {
	case MODE_FOLLOW_MUTE:	str = "follow-mute"; break;
	case MODE_FOLLOW_ROUTE:	str = "follow-route"; break;
	case MODE_ON:		str = "on"; break;
	case MODE_OFF:		str = "off"; break;
	}
	return sprintf(buf, "%s\n", str);
}

static ssize_t store_mode(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct snd_ctl_led *led = container_of(dev, struct snd_ctl_led, dev);
	char _buf[16];
	size_t l = min(count, sizeof(_buf) - 1) + 1;
	enum snd_ctl_led_mode mode;

	memcpy(_buf, buf, l);
	_buf[l] = '\0';
	if (strstr(_buf, "mute"))
		mode = MODE_FOLLOW_MUTE;
	else if (strstr(_buf, "route"))
		mode = MODE_FOLLOW_ROUTE;
	else if (strncmp(_buf, "off", 3) == 0 || strncmp(_buf, "0", 1) == 0)
		mode = MODE_OFF;
	else if (strncmp(_buf, "on", 2) == 0 || strncmp(_buf, "1", 1) == 0)
		mode = MODE_ON;
	else
		return count;

	mutex_lock(&snd_ctl_led_mutex);
	led->mode = mode;
	mutex_unlock(&snd_ctl_led_mutex);

	snd_ctl_led_set_state(NULL, group_to_access(led->group), NULL, 0);
	return count;
}

static ssize_t show_brightness(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct snd_ctl_led *led = container_of(dev, struct snd_ctl_led, dev);

	return sprintf(buf, "%u\n", ledtrig_audio_get(led->trigger_type));
}

static DEVICE_ATTR(mode, 0644, show_mode, store_mode);
static DEVICE_ATTR(brightness, 0444, show_brightness, NULL);

static struct attribute *snd_ctl_led_dev_attrs[] = {
	&dev_attr_mode.attr,
	&dev_attr_brightness.attr,
	NULL,
};

static const struct attribute_group snd_ctl_led_dev_attr_group = {
	.attrs = snd_ctl_led_dev_attrs,
};

static const struct attribute_group *snd_ctl_led_dev_attr_groups[] = {
	&snd_ctl_led_dev_attr_group,
	NULL,
};

static struct device snd_ctl_led_dev;

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
	struct snd_ctl_led *led;
	unsigned int group;

	device_initialize(&snd_ctl_led_dev);
	snd_ctl_led_dev.class = sound_class;
	dev_set_name(&snd_ctl_led_dev, "ctl-led");
	if (device_add(&snd_ctl_led_dev)) {
		put_device(&snd_ctl_led_dev);
		return -ENOMEM;
	}
	for (group = 0; group < MAX_LED; group++) {
		led = &snd_ctl_leds[group];
		INIT_LIST_HEAD(&led->controls);
		device_initialize(&led->dev);
		led->dev.parent = &snd_ctl_led_dev;
		led->dev.groups = snd_ctl_led_dev_attr_groups;
		dev_set_name(&led->dev, led->name);
		if (device_add(&led->dev)) {
			put_device(&led->dev);
			for (; group > 0; group--) {
				led = &snd_ctl_leds[group];
				device_del(&led->dev);
			}
			device_del(&snd_ctl_led_dev);
			return -ENOMEM;
		}
	}
	snd_ctl_register_layer(&snd_ctl_led_lops);
	return 0;
}

static void __exit snd_ctl_led_exit(void)
{
	struct snd_ctl_led *led;
	unsigned int group;

	for (group = 0; group < MAX_LED; group++) {
		led = &snd_ctl_leds[group];
		device_del(&led->dev);
	}
	device_del(&snd_ctl_led_dev);
	snd_ctl_disconnect_layer(&snd_ctl_led_lops);
	snd_ctl_led_clean(NULL);
}

module_init(snd_ctl_led_init)
module_exit(snd_ctl_led_exit)
