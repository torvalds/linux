/*
 *  Jack abstraction layer
 *
 *  Copyright 2008 Wolfson Microelectronics
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

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/jack.h>
#include <sound/core.h>

static int jack_switch_types[SND_JACK_SWITCH_TYPES] = {
	SW_HEADPHONE_INSERT,
	SW_MICROPHONE_INSERT,
	SW_LINEOUT_INSERT,
	SW_JACK_PHYSICAL_INSERT,
	SW_VIDEOOUT_INSERT,
	SW_LINEIN_INSERT,
};

static int snd_jack_dev_free(struct snd_device *device)
{
	struct snd_jack *jack = device->device_data;

	if (jack->private_free)
		jack->private_free(jack);

	/* If the input device is registered with the input subsystem
	 * then we need to use a different deallocator. */
	if (jack->registered)
		input_unregister_device(jack->input_dev);
	else
		input_free_device(jack->input_dev);

	kfree(jack->id);
	kfree(jack);

	return 0;
}

static int snd_jack_dev_register(struct snd_device *device)
{
	struct snd_jack *jack = device->device_data;
	struct snd_card *card = device->card;
	int err, i;

	snprintf(jack->name, sizeof(jack->name), "%s %s",
		 card->shortname, jack->id);
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

/**
 * snd_jack_new - Create a new jack
 * @card:  the card instance
 * @id:    an identifying string for this jack
 * @type:  a bitmask of enum snd_jack_type values that can be detected by
 *         this jack
 * @jjack: Used to provide the allocated jack object to the caller.
 *
 * Creates a new jack object.
 *
 * Return: Zero if successful, or a negative error code on failure.
 * On success @jjack will be initialised.
 */
int snd_jack_new(struct snd_card *card, const char *id, int type,
		 struct snd_jack **jjack)
{
	struct snd_jack *jack;
	int err;
	int i;
	static struct snd_device_ops ops = {
		.dev_free = snd_jack_dev_free,
		.dev_register = snd_jack_dev_register,
	};

	jack = kzalloc(sizeof(struct snd_jack), GFP_KERNEL);
	if (jack == NULL)
		return -ENOMEM;

	jack->id = kstrdup(id, GFP_KERNEL);

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

	err = snd_device_new(card, SNDRV_DEV_JACK, jack, &ops);
	if (err < 0)
		goto fail_input;

	*jjack = jack;

	return 0;

fail_input:
	input_free_device(jack->input_dev);
	kfree(jack->id);
	kfree(jack);
	return err;
}
EXPORT_SYMBOL(snd_jack_new);

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
 * Map a SND_JACK_BTN_ button type to an input layer key, allowing
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

/**
 * snd_jack_report - Report the current status of a jack
 *
 * @jack:   The jack to report status for
 * @status: The current status of the jack
 */
void snd_jack_report(struct snd_jack *jack, int status)
{
	int i;

	if (!jack)
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
}
EXPORT_SYMBOL(snd_jack_report);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("Jack detection support for ALSA");
MODULE_LICENSE("GPL");
