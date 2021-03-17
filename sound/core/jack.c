// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Jack abstraction layer
 *
 *  Copyright 2008 Wolfson Microelectronics
 */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/jack.h>
#include <sound/core.h>
#include <sound/control.h>

struct snd_jack_kctl {
	struct snd_kcontrol *kctl;
	struct list_head list;  /* list of controls belong to the same jack */
	unsigned int mask_bits; /* only masked status bits are reported via kctl */
};

#ifdef CONFIG_SND_JACK_INPUT_DEV
static const int jack_switch_types[SND_JACK_SWITCH_TYPES] = {
	SW_HEADPHONE_INSERT,
	SW_MICROPHONE_INSERT,
	SW_LINEOUT_INSERT,
	SW_JACK_PHYSICAL_INSERT,
	SW_VIDEOOUT_INSERT,
	SW_LINEIN_INSERT,
};
#endif /* CONFIG_SND_JACK_INPUT_DEV */

static int snd_jack_dev_disconnect(struct snd_device *device)
{
#ifdef CONFIG_SND_JACK_INPUT_DEV
	struct snd_jack *jack = device->device_data;

	if (!jack->input_dev)
		return 0;

	/* If the input device is registered with the input subsystem
	 * then we need to use a different deallocator. */
	if (jack->registered)
		input_unregister_device(jack->input_dev);
	else
		input_free_device(jack->input_dev);
	jack->input_dev = NULL;
#endif /* CONFIG_SND_JACK_INPUT_DEV */
	return 0;
}

static int snd_jack_dev_free(struct snd_device *device)
{
	struct snd_jack *jack = device->device_data;
	struct snd_card *card = device->card;
	struct snd_jack_kctl *jack_kctl, *tmp_jack_kctl;

	list_for_each_entry_safe(jack_kctl, tmp_jack_kctl, &jack->kctl_list, list) {
		list_del_init(&jack_kctl->list);
		snd_ctl_remove(card, jack_kctl->kctl);
	}
	if (jack->private_free)
		jack->private_free(jack);

	snd_jack_dev_disconnect(device);

	kfree(jack->id);
	kfree(jack);

	return 0;
}

#ifdef CONFIG_SND_JACK_INPUT_DEV
static int snd_jack_dev_register(struct snd_device *device)
{
	struct snd_jack *jack = device->device_data;
	struct snd_card *card = device->card;
	int err, i;

	snprintf(jack->name, sizeof(jack->name), "%s %s",
		 card->shortname, jack->id);

	if (!jack->input_dev)
		return 0;

	jack->input_dev->name = jack->name;

	/* Default to the sound card device. */
	if (!jack->input_dev->dev.parent)
		jack->input_dev->dev.parent = snd_card_get_device_link(card);

	/* Add capabilities for any keys that are enabled */
	for (i = 0; i < ARRAY_SIZE(jack->key); i++) {
		int testbit = SND_JACK_BTN_0 >> i;

		if (!(jack->type & testbit))
			continue;

		if (!jack->key[i])
			jack->key[i] = BTN_0 + i;

		input_set_capability(jack->input_dev, EV_KEY, jack->key[i]);
	}

	err = input_register_device(jack->input_dev);
	if (err == 0)
		jack->registered = 1;

	return err;
}
#endif /* CONFIG_SND_JACK_INPUT_DEV */

static void snd_jack_kctl_private_free(struct snd_kcontrol *kctl)
{
	struct snd_jack_kctl *jack_kctl;

	jack_kctl = kctl->private_data;
	if (jack_kctl) {
		list_del(&jack_kctl->list);
		kfree(jack_kctl);
	}
}

static void snd_jack_kctl_add(struct snd_jack *jack, struct snd_jack_kctl *jack_kctl)
{
	list_add_tail(&jack_kctl->list, &jack->kctl_list);
}

static struct snd_jack_kctl * snd_jack_kctl_new(struct snd_card *card, const char *name, unsigned int mask)
{
	struct snd_kcontrol *kctl;
	struct snd_jack_kctl *jack_kctl;
	int err;

	kctl = snd_kctl_jack_new(name, card);
	if (!kctl)
		return NULL;

	err = snd_ctl_add(card, kctl);
	if (err < 0)
		return NULL;

	jack_kctl = kzalloc(sizeof(*jack_kctl), GFP_KERNEL);

	if (!jack_kctl)
		goto error;

	jack_kctl->kctl = kctl;
	jack_kctl->mask_bits = mask;

	kctl->private_data = jack_kctl;
	kctl->private_free = snd_jack_kctl_private_free;

	return jack_kctl;
error:
	snd_ctl_free_one(kctl);
	return NULL;
}

/**
 * snd_jack_add_new_kctl - Create a new snd_jack_kctl and add it to jack
 * @jack:  the jack instance which the kctl will attaching to
 * @name:  the name for the snd_kcontrol object
 * @mask:  a bitmask of enum snd_jack_type values that can be detected
 *         by this snd_jack_kctl object.
 *
 * Creates a new snd_kcontrol object and adds it to the jack kctl_list.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_jack_add_new_kctl(struct snd_jack *jack, const char * name, int mask)
{
	struct snd_jack_kctl *jack_kctl;

	jack_kctl = snd_jack_kctl_new(jack->card, name, mask);
	if (!jack_kctl)
		return -ENOMEM;

	snd_jack_kctl_add(jack, jack_kctl);
	return 0;
}
EXPORT_SYMBOL(snd_jack_add_new_kctl);

/**
 * snd_jack_new - Create a new jack
 * @card:  the card instance
 * @id:    an identifying string for this jack
 * @type:  a bitmask of enum snd_jack_type values that can be detected by
 *         this jack
 * @jjack: Used to provide the allocated jack object to the caller.
 * @initial_kctl: if true, create a kcontrol and add it to the jack list.
 * @phantom_jack: Don't create a input device for phantom jacks.
 *
 * Creates a new jack object.
 *
 * Return: Zero if successful, or a negative error code on failure.
 * On success @jjack will be initialised.
 */
int snd_jack_new(struct snd_card *card, const char *id, int type,
		 struct snd_jack **jjack, bool initial_kctl, bool phantom_jack)
{
	struct snd_jack *jack;
	struct snd_jack_kctl *jack_kctl = NULL;
	int err;
	static const struct snd_device_ops ops = {
		.dev_free = snd_jack_dev_free,
#ifdef CONFIG_SND_JACK_INPUT_DEV
		.dev_register = snd_jack_dev_register,
		.dev_disconnect = snd_jack_dev_disconnect,
#endif /* CONFIG_SND_JACK_INPUT_DEV */
	};

	if (initial_kctl) {
		jack_kctl = snd_jack_kctl_new(card, id, type);
		if (!jack_kctl)
			return -ENOMEM;
	}

	jack = kzalloc(sizeof(struct snd_jack), GFP_KERNEL);
	if (jack == NULL)
		return -ENOMEM;

	jack->id = kstrdup(id, GFP_KERNEL);

	/* don't creat input device for phantom jack */
	if (!phantom_jack) {
#ifdef CONFIG_SND_JACK_INPUT_DEV
		int i;

		jack->input_dev = input_allocate_device();
		if (jack->input_dev == NULL) {
			err = -ENOMEM;
			goto fail_input;
		}

		jack->input_dev->phys = "ALSA";

		jack->type = type;

		for (i = 0; i < SND_JACK_SWITCH_TYPES; i++)
			if (type & (1 << i))
				input_set_capability(jack->input_dev, EV_SW,
						     jack_switch_types[i]);

#endif /* CONFIG_SND_JACK_INPUT_DEV */
	}

	err = snd_device_new(card, SNDRV_DEV_JACK, jack, &ops);
	if (err < 0)
		goto fail_input;

	jack->card = card;
	INIT_LIST_HEAD(&jack->kctl_list);

	if (initial_kctl)
		snd_jack_kctl_add(jack, jack_kctl);

	*jjack = jack;

	return 0;

fail_input:
#ifdef CONFIG_SND_JACK_INPUT_DEV
	input_free_device(jack->input_dev);
#endif
	kfree(jack->id);
	kfree(jack);
	return err;
}
EXPORT_SYMBOL(snd_jack_new);

#ifdef CONFIG_SND_JACK_INPUT_DEV
/**
 * snd_jack_set_parent - Set the parent device for a jack
 *
 * @jack:   The jack to configure
 * @parent: The device to set as parent for the jack.
 *
 * Set the parent for the jack devices in the device tree.  This
 * function is only valid prior to registration of the jack.  If no
 * parent is configured then the parent device will be the sound card.
 */
void snd_jack_set_parent(struct snd_jack *jack, struct device *parent)
{
	WARN_ON(jack->registered);
	if (!jack->input_dev)
		return;

	jack->input_dev->dev.parent = parent;
}
EXPORT_SYMBOL(snd_jack_set_parent);

/**
 * snd_jack_set_key - Set a key mapping on a jack
 *
 * @jack:    The jack to configure
 * @type:    Jack report type for this key
 * @keytype: Input layer key type to be reported
 *
 * Map a SND_JACK_BTN_* button type to an input layer key, allowing
 * reporting of keys on accessories via the jack abstraction.  If no
 * mapping is provided but keys are enabled in the jack type then
 * BTN_n numeric buttons will be reported.
 *
 * If jacks are not reporting via the input API this call will have no
 * effect.
 *
 * Note that this is intended to be use by simple devices with small
 * numbers of keys that can be reported.  It is also possible to
 * access the input device directly - devices with complex input
 * capabilities on accessories should consider doing this rather than
 * using this abstraction.
 *
 * This function may only be called prior to registration of the jack.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_jack_set_key(struct snd_jack *jack, enum snd_jack_types type,
		     int keytype)
{
	int key = fls(SND_JACK_BTN_0) - fls(type);

	WARN_ON(jack->registered);

	if (!keytype || key >= ARRAY_SIZE(jack->key))
		return -EINVAL;

	jack->type |= type;
	jack->key[key] = keytype;
	return 0;
}
EXPORT_SYMBOL(snd_jack_set_key);
#endif /* CONFIG_SND_JACK_INPUT_DEV */

/**
 * snd_jack_report - Report the current status of a jack
 *
 * @jack:   The jack to report status for
 * @status: The current status of the jack
 */
void snd_jack_report(struct snd_jack *jack, int status)
{
	struct snd_jack_kctl *jack_kctl;
#ifdef CONFIG_SND_JACK_INPUT_DEV
	int i;
#endif

	if (!jack)
		return;

	list_for_each_entry(jack_kctl, &jack->kctl_list, list)
		snd_kctl_jack_report(jack->card, jack_kctl->kctl,
					    status & jack_kctl->mask_bits);

#ifdef CONFIG_SND_JACK_INPUT_DEV
	if (!jack->input_dev)
		return;

	for (i = 0; i < ARRAY_SIZE(jack->key); i++) {
		int testbit = SND_JACK_BTN_0 >> i;

		if (jack->type & testbit)
			input_report_key(jack->input_dev, jack->key[i],
					 status & testbit);
	}

	for (i = 0; i < ARRAY_SIZE(jack_switch_types); i++) {
		int testbit = 1 << i;
		if (jack->type & testbit)
			input_report_switch(jack->input_dev,
					    jack_switch_types[i],
					    status & testbit);
	}

	input_sync(jack->input_dev);
#endif /* CONFIG_SND_JACK_INPUT_DEV */
}
EXPORT_SYMBOL(snd_jack_report);
