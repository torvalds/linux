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

#define to_led_card_dev(_dev) \
	container_of(_dev, struct snd_ctl_led_card, dev)

enum snd_ctl_led_mode {
	 MODE_FOLLOW_MUTE = 0,
	 MODE_FOLLOW_ROUTE,
	 MODE_OFF,
	 MODE_ON,
};

struct snd_ctl_led_card {
	struct device dev;
	int number;
	struct snd_ctl_led *led;
};

struct snd_ctl_led {
	struct device dev;
	struct list_head controls;
	const char *name;
	unsigned int group;
	enum led_audio trigger_type;
	enum snd_ctl_led_mode mode;
	struct snd_ctl_led_card *cards[SNDRV_CARDS];
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

static void snd_ctl_led_sysfs_add(struct snd_card *card);
static void snd_ctl_led_sysfs_remove(struct snd_card *card);

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

/*
 * A note for callers:
 *   The two static variables info and value are protected using snd_ctl_led_mutex.
 */
static int snd_ctl_led_get(struct snd_ctl_led_ctl *lctl)
{
	static struct snd_ctl_elem_info info;
	static struct snd_ctl_elem_value value;
	struct snd_kcontrol *kctl = lctl->kctl;
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

static int snd_ctl_led_set_id(int card_number, struct snd_ctl_elem_id *id,
			      unsigned int group, bool set)
{
	struct snd_card *card;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;
	unsigned int ioff, access, new_access;
	int err = 0;

	card = snd_card_ref(card_number);
	if (card) {
		down_write(&card->controls_rwsem);
		kctl = snd_ctl_find_id(card, id);
		if (kctl) {
			ioff = snd_ctl_get_ioff(kctl, id);
			vd = &kctl->vd[ioff];
			access = vd->access & SNDRV_CTL_ELEM_ACCESS_LED_MASK;
			if (access != 0 && access != group_to_access(group)) {
				err = -EXDEV;
				goto unlock;
			}
			new_access = vd->access & ~SNDRV_CTL_ELEM_ACCESS_LED_MASK;
			if (set)
				new_access |= group_to_access(group);
			if (new_access != vd->access) {
				vd->access = new_access;
				snd_ctl_led_notify(card, SNDRV_CTL_EVENT_MASK_INFO, kctl, ioff);
			}
		} else {
			err = -ENOENT;
		}
unlock:
		up_write(&card->controls_rwsem);
		snd_card_unref(card);
	} else {
		err = -ENXIO;
	}
	return err;
}

static void snd_ctl_led_refresh(void)
{
	unsigned int group;

	for (group = 0; group < MAX_LED; group++)
		snd_ctl_led_set_state(NULL, group_to_access(group), NULL, 0);
}

static void snd_ctl_led_ctl_destroy(struct snd_ctl_led_ctl *lctl)
{
	list_del(&lctl->list);
	kfree(lctl);
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
				snd_ctl_led_ctl_destroy(lctl);
				goto repeat;
			}
	}
}

static int snd_ctl_led_reset(int card_number, unsigned int group)
{
	struct snd_card *card;
	struct snd_ctl_led *led;
	struct snd_ctl_led_ctl *lctl;
	struct snd_kcontrol_volatile *vd;
	bool change = false;

	card = snd_card_ref(card_number);
	if (!card)
		return -ENXIO;

	mutex_lock(&snd_ctl_led_mutex);
	if (!snd_ctl_led_card_valid[card_number]) {
		mutex_unlock(&snd_ctl_led_mutex);
		snd_card_unref(card);
		return -ENXIO;
	}
	led = &snd_ctl_leds[group];
repeat:
	list_for_each_entry(lctl, &led->controls, list)
		if (lctl->card == card) {
			vd = &lctl->kctl->vd[lctl->index_offset];
			vd->access &= ~group_to_access(group);
			snd_ctl_led_ctl_destroy(lctl);
			change = true;
			goto repeat;
		}
	mutex_unlock(&snd_ctl_led_mutex);
	if (change)
		snd_ctl_led_set_state(NULL, group_to_access(group), NULL, 0);
	snd_card_unref(card);
	return 0;
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
	snd_ctl_led_sysfs_add(card);
}

static void snd_ctl_led_disconnect(struct snd_card *card)
{
	snd_ctl_led_sysfs_remove(card);
	mutex_lock(&snd_ctl_led_mutex);
	snd_ctl_led_card_valid[card->number] = false;
	snd_ctl_led_clean(card);
	mutex_unlock(&snd_ctl_led_mutex);
	snd_ctl_led_refresh();
}

static void snd_ctl_led_card_release(struct device *dev)
{
	struct snd_ctl_led_card *led_card = to_led_card_dev(dev);

	kfree(led_card);
}

static void snd_ctl_led_release(struct device *dev)
{
}

static void snd_ctl_led_dev_release(struct device *dev)
{
}

/*
 * sysfs
 */

static ssize_t mode_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct snd_ctl_led *led = container_of(dev, struct snd_ctl_led, dev);
	const char *str = NULL;

	switch (led->mode) {
	case MODE_FOLLOW_MUTE:	str = "follow-mute"; break;
	case MODE_FOLLOW_ROUTE:	str = "follow-route"; break;
	case MODE_ON:		str = "on"; break;
	case MODE_OFF:		str = "off"; break;
	}
	return sysfs_emit(buf, "%s\n", str);
}

static ssize_t mode_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct snd_ctl_led *led = container_of(dev, struct snd_ctl_led, dev);
	char _buf[16];
	size_t l = min(count, sizeof(_buf) - 1);
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

static ssize_t brightness_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct snd_ctl_led *led = container_of(dev, struct snd_ctl_led, dev);

	return sysfs_emit(buf, "%u\n", ledtrig_audio_get(led->trigger_type));
}

static DEVICE_ATTR_RW(mode);
static DEVICE_ATTR_RO(brightness);

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

static char *find_eos(char *s)
{
	while (*s && *s != ',')
		s++;
	if (*s)
		s++;
	return s;
}

static char *parse_uint(char *s, unsigned int *val)
{
	unsigned long long res;
	if (kstrtoull(s, 10, &res))
		res = 0;
	*val = res;
	return find_eos(s);
}

static char *parse_string(char *s, char *val, size_t val_size)
{
	if (*s == '"' || *s == '\'') {
		char c = *s;
		s++;
		while (*s && *s != c) {
			if (val_size > 1) {
				*val++ = *s;
				val_size--;
			}
			s++;
		}
	} else {
		while (*s && *s != ',') {
			if (val_size > 1) {
				*val++ = *s;
				val_size--;
			}
			s++;
		}
	}
	*val = '\0';
	if (*s)
		s++;
	return s;
}

static char *parse_iface(char *s, snd_ctl_elem_iface_t *val)
{
	if (!strncasecmp(s, "card", 4))
		*val = SNDRV_CTL_ELEM_IFACE_CARD;
	else if (!strncasecmp(s, "mixer", 5))
		*val = SNDRV_CTL_ELEM_IFACE_MIXER;
	return find_eos(s);
}

/*
 * These types of input strings are accepted:
 *
 *   unsigned integer - numid (equivaled to numid=UINT)
 *   string - basic mixer name (equivalent to iface=MIXER,name=STR)
 *   numid=UINT
 *   [iface=MIXER,][device=UINT,][subdevice=UINT,]name=STR[,index=UINT]
 */
static ssize_t set_led_id(struct snd_ctl_led_card *led_card, const char *buf, size_t count,
			  bool attach)
{
	char buf2[256], *s, *os;
	struct snd_ctl_elem_id id;
	int err;

	if (strscpy(buf2, buf, sizeof(buf2)) < 0)
		return -E2BIG;
	memset(&id, 0, sizeof(id));
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	s = buf2;
	while (*s) {
		os = s;
		if (!strncasecmp(s, "numid=", 6)) {
			s = parse_uint(s + 6, &id.numid);
		} else if (!strncasecmp(s, "iface=", 6)) {
			s = parse_iface(s + 6, &id.iface);
		} else if (!strncasecmp(s, "device=", 7)) {
			s = parse_uint(s + 7, &id.device);
		} else if (!strncasecmp(s, "subdevice=", 10)) {
			s = parse_uint(s + 10, &id.subdevice);
		} else if (!strncasecmp(s, "name=", 5)) {
			s = parse_string(s + 5, id.name, sizeof(id.name));
		} else if (!strncasecmp(s, "index=", 6)) {
			s = parse_uint(s + 6, &id.index);
		} else if (s == buf2) {
			while (*s) {
				if (*s < '0' || *s > '9')
					break;
				s++;
			}
			if (*s == '\0')
				parse_uint(buf2, &id.numid);
			else {
				for (; *s >= ' '; s++);
				*s = '\0';
				strscpy(id.name, buf2, sizeof(id.name));
			}
			break;
		}
		if (*s == ',')
			s++;
		if (s == os)
			break;
	}

	err = snd_ctl_led_set_id(led_card->number, &id, led_card->led->group, attach);
	if (err < 0)
		return err;

	return count;
}

static ssize_t attach_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct snd_ctl_led_card *led_card = container_of(dev, struct snd_ctl_led_card, dev);
	return set_led_id(led_card, buf, count, true);
}

static ssize_t detach_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct snd_ctl_led_card *led_card = container_of(dev, struct snd_ctl_led_card, dev);
	return set_led_id(led_card, buf, count, false);
}

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct snd_ctl_led_card *led_card = container_of(dev, struct snd_ctl_led_card, dev);
	int err;

	if (count > 0 && buf[0] == '1') {
		err = snd_ctl_led_reset(led_card->number, led_card->led->group);
		if (err < 0)
			return err;
	}
	return count;
}

static ssize_t list_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct snd_ctl_led_card *led_card = container_of(dev, struct snd_ctl_led_card, dev);
	struct snd_card *card;
	struct snd_ctl_led_ctl *lctl;
	size_t l = 0;

	card = snd_card_ref(led_card->number);
	if (!card)
		return -ENXIO;
	down_read(&card->controls_rwsem);
	mutex_lock(&snd_ctl_led_mutex);
	if (snd_ctl_led_card_valid[led_card->number]) {
		list_for_each_entry(lctl, &led_card->led->controls, list) {
			if (lctl->card != card)
				continue;
			if (l)
				l += sysfs_emit_at(buf, l, " ");
			l += sysfs_emit_at(buf, l, "%u",
					   lctl->kctl->id.numid + lctl->index_offset);
		}
	}
	mutex_unlock(&snd_ctl_led_mutex);
	up_read(&card->controls_rwsem);
	snd_card_unref(card);
	return l;
}

static DEVICE_ATTR_WO(attach);
static DEVICE_ATTR_WO(detach);
static DEVICE_ATTR_WO(reset);
static DEVICE_ATTR_RO(list);

static struct attribute *snd_ctl_led_card_attrs[] = {
	&dev_attr_attach.attr,
	&dev_attr_detach.attr,
	&dev_attr_reset.attr,
	&dev_attr_list.attr,
	NULL,
};

static const struct attribute_group snd_ctl_led_card_attr_group = {
	.attrs = snd_ctl_led_card_attrs,
};

static const struct attribute_group *snd_ctl_led_card_attr_groups[] = {
	&snd_ctl_led_card_attr_group,
	NULL,
};

static struct device snd_ctl_led_dev;

static void snd_ctl_led_sysfs_add(struct snd_card *card)
{
	unsigned int group;
	struct snd_ctl_led_card *led_card;
	struct snd_ctl_led *led;
	char link_name[32];

	for (group = 0; group < MAX_LED; group++) {
		led = &snd_ctl_leds[group];
		led_card = kzalloc(sizeof(*led_card), GFP_KERNEL);
		if (!led_card)
			goto cerr2;
		led_card->number = card->number;
		led_card->led = led;
		device_initialize(&led_card->dev);
		led_card->dev.release = snd_ctl_led_card_release;
		if (dev_set_name(&led_card->dev, "card%d", card->number) < 0)
			goto cerr;
		led_card->dev.parent = &led->dev;
		led_card->dev.groups = snd_ctl_led_card_attr_groups;
		if (device_add(&led_card->dev))
			goto cerr;
		led->cards[card->number] = led_card;
		snprintf(link_name, sizeof(link_name), "led-%s", led->name);
		WARN(sysfs_create_link(&card->ctl_dev.kobj, &led_card->dev.kobj, link_name),
			"can't create symlink to controlC%i device\n", card->number);
		WARN(sysfs_create_link(&led_card->dev.kobj, &card->card_dev.kobj, "card"),
			"can't create symlink to card%i\n", card->number);

		continue;
cerr:
		put_device(&led_card->dev);
cerr2:
		printk(KERN_ERR "snd_ctl_led: unable to add card%d", card->number);
	}
}

static void snd_ctl_led_sysfs_remove(struct snd_card *card)
{
	unsigned int group;
	struct snd_ctl_led_card *led_card;
	struct snd_ctl_led *led;
	char link_name[32];

	for (group = 0; group < MAX_LED; group++) {
		led = &snd_ctl_leds[group];
		led_card = led->cards[card->number];
		if (!led_card)
			continue;
		snprintf(link_name, sizeof(link_name), "led-%s", led->name);
		sysfs_remove_link(&card->ctl_dev.kobj, link_name);
		sysfs_remove_link(&led_card->dev.kobj, "card");
		device_unregister(&led_card->dev);
		led->cards[card->number] = NULL;
	}
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
	struct snd_ctl_led *led;
	unsigned int group;

	device_initialize(&snd_ctl_led_dev);
	snd_ctl_led_dev.class = sound_class;
	snd_ctl_led_dev.release = snd_ctl_led_dev_release;
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
		led->dev.release = snd_ctl_led_release;
		led->dev.groups = snd_ctl_led_dev_attr_groups;
		dev_set_name(&led->dev, led->name);
		if (device_add(&led->dev)) {
			put_device(&led->dev);
			for (; group > 0; group--) {
				led = &snd_ctl_leds[group - 1];
				device_unregister(&led->dev);
			}
			device_unregister(&snd_ctl_led_dev);
			return -ENOMEM;
		}
	}
	snd_ctl_register_layer(&snd_ctl_led_lops);
	return 0;
}

static void __exit snd_ctl_led_exit(void)
{
	struct snd_ctl_led *led;
	struct snd_card *card;
	unsigned int group, card_number;

	snd_ctl_disconnect_layer(&snd_ctl_led_lops);
	for (card_number = 0; card_number < SNDRV_CARDS; card_number++) {
		if (!snd_ctl_led_card_valid[card_number])
			continue;
		card = snd_card_ref(card_number);
		if (card) {
			snd_ctl_led_sysfs_remove(card);
			snd_card_unref(card);
		}
	}
	for (group = 0; group < MAX_LED; group++) {
		led = &snd_ctl_leds[group];
		device_unregister(&led->dev);
	}
	device_unregister(&snd_ctl_led_dev);
	snd_ctl_led_clean(NULL);
}

module_init(snd_ctl_led_init)
module_exit(snd_ctl_led_exit)
