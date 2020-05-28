// SPDX-License-Identifier: GPL-2.0
//
// soc-card.c
//
// Copyright (C) 2019 Renesas Electronics Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
#include <sound/soc.h>
#include <sound/jack.h>

#define soc_card_ret(dai, ret) _soc_card_ret(dai, __func__, ret)
static inline int _soc_card_ret(struct snd_soc_card *card,
				const char *func, int ret)
{
	switch (ret) {
	case -EPROBE_DEFER:
	case -ENOTSUPP:
	case 0:
		break;
	default:
		dev_err(card->dev,
			"ASoC: error at %s on %s: %d\n",
			func, card->name, ret);
	}

	return ret;
}

struct snd_kcontrol *snd_soc_card_get_kcontrol(struct snd_soc_card *soc_card,
					       const char *name)
{
	struct snd_card *card = soc_card->snd_card;
	struct snd_kcontrol *kctl;

	if (unlikely(!name))
		return NULL;

	list_for_each_entry(kctl, &card->controls, list)
		if (!strncmp(kctl->id.name, name, sizeof(kctl->id.name)))
			return kctl;
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_card_get_kcontrol);

/**
 * snd_soc_card_jack_new - Create a new jack
 * @card:  ASoC card
 * @id:    an identifying string for this jack
 * @type:  a bitmask of enum snd_jack_type values that can be detected by
 *         this jack
 * @jack:  structure to use for the jack
 * @pins:  Array of jack pins to be added to the jack or NULL
 * @num_pins: Number of elements in the @pins array
 *
 * Creates a new jack object.
 *
 * Returns zero if successful, or a negative error code on failure.
 * On success jack will be initialised.
 */
int snd_soc_card_jack_new(struct snd_soc_card *card, const char *id, int type,
			  struct snd_soc_jack *jack,
			  struct snd_soc_jack_pin *pins, unsigned int num_pins)
{
	int ret;

	mutex_init(&jack->mutex);
	jack->card = card;
	INIT_LIST_HEAD(&jack->pins);
	INIT_LIST_HEAD(&jack->jack_zones);
	BLOCKING_INIT_NOTIFIER_HEAD(&jack->notifier);

	ret = snd_jack_new(card->snd_card, id, type, &jack->jack, false, false);
	if (ret)
		goto end;

	if (num_pins)
		ret = snd_soc_jack_add_pins(jack, num_pins, pins);
end:
	return soc_card_ret(card, ret);
}
EXPORT_SYMBOL_GPL(snd_soc_card_jack_new);

int snd_soc_card_suspend_pre(struct snd_soc_card *card)
{
	int ret = 0;

	if (card->suspend_pre)
		ret = card->suspend_pre(card);

	return soc_card_ret(card, ret);
}
