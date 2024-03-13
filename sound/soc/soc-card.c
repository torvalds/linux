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

static int jack_new(struct snd_soc_card *card, const char *id, int type,
		    struct snd_soc_jack *jack, bool initial_kctl)
{
	mutex_init(&jack->mutex);
	jack->card = card;
	INIT_LIST_HEAD(&jack->pins);
	INIT_LIST_HEAD(&jack->jack_zones);
	BLOCKING_INIT_NOTIFIER_HEAD(&jack->notifier);

	return snd_jack_new(card->snd_card, id, type, &jack->jack, initial_kctl, false);
}

/**
 * snd_soc_card_jack_new - Create a new jack without pins
 * @card:  ASoC card
 * @id:    an identifying string for this jack
 * @type:  a bitmask of enum snd_jack_type values that can be detected by
 *         this jack
 * @jack:  structure to use for the jack
 *
 * Creates a new jack object without pins. If adding pins later,
 * snd_soc_card_jack_new_pins() should be used instead with 0 as num_pins
 * argument.
 *
 * Returns zero if successful, or a negative error code on failure.
 * On success jack will be initialised.
 */
int snd_soc_card_jack_new(struct snd_soc_card *card, const char *id, int type,
			  struct snd_soc_jack *jack)
{
	return soc_card_ret(card, jack_new(card, id, type, jack, true));
}
EXPORT_SYMBOL_GPL(snd_soc_card_jack_new);

/**
 * snd_soc_card_jack_new_pins - Create a new jack with pins
 * @card:  ASoC card
 * @id:    an identifying string for this jack
 * @type:  a bitmask of enum snd_jack_type values that can be detected by
 *         this jack
 * @jack:  structure to use for the jack
 * @pins:  Array of jack pins to be added to the jack or NULL
 * @num_pins: Number of elements in the @pins array
 *
 * Creates a new jack object with pins. If not adding pins,
 * snd_soc_card_jack_new() should be used instead.
 *
 * Returns zero if successful, or a negative error code on failure.
 * On success jack will be initialised.
 */
int snd_soc_card_jack_new_pins(struct snd_soc_card *card, const char *id,
			       int type, struct snd_soc_jack *jack,
			       struct snd_soc_jack_pin *pins,
			       unsigned int num_pins)
{
	int ret;

	ret = jack_new(card, id, type, jack, false);
	if (ret)
		goto end;

	if (num_pins)
		ret = snd_soc_jack_add_pins(jack, num_pins, pins);
end:
	return soc_card_ret(card, ret);
}
EXPORT_SYMBOL_GPL(snd_soc_card_jack_new_pins);

int snd_soc_card_suspend_pre(struct snd_soc_card *card)
{
	int ret = 0;

	if (card->suspend_pre)
		ret = card->suspend_pre(card);

	return soc_card_ret(card, ret);
}

int snd_soc_card_suspend_post(struct snd_soc_card *card)
{
	int ret = 0;

	if (card->suspend_post)
		ret = card->suspend_post(card);

	return soc_card_ret(card, ret);
}

int snd_soc_card_resume_pre(struct snd_soc_card *card)
{
	int ret = 0;

	if (card->resume_pre)
		ret = card->resume_pre(card);

	return soc_card_ret(card, ret);
}

int snd_soc_card_resume_post(struct snd_soc_card *card)
{
	int ret = 0;

	if (card->resume_post)
		ret = card->resume_post(card);

	return soc_card_ret(card, ret);
}

int snd_soc_card_probe(struct snd_soc_card *card)
{
	if (card->probe) {
		int ret = card->probe(card);

		if (ret < 0)
			return soc_card_ret(card, ret);

		/*
		 * It has "card->probe" and "card->late_probe" callbacks.
		 * So, set "probed" flag here, because it needs to care
		 * about "late_probe".
		 *
		 * see
		 *	snd_soc_bind_card()
		 *	snd_soc_card_late_probe()
		 */
		card->probed = 1;
	}

	return 0;
}

int snd_soc_card_late_probe(struct snd_soc_card *card)
{
	if (card->late_probe) {
		int ret = card->late_probe(card);

		if (ret < 0)
			return soc_card_ret(card, ret);
	}

	/*
	 * It has "card->probe" and "card->late_probe" callbacks,
	 * and "late_probe" callback is called after "probe".
	 * This means, we can set "card->probed" flag afer "late_probe"
	 * for all cases.
	 *
	 * see
	 *	snd_soc_bind_card()
	 *	snd_soc_card_probe()
	 */
	card->probed = 1;

	return 0;
}

void snd_soc_card_fixup_controls(struct snd_soc_card *card)
{
	if (card->fixup_controls)
		card->fixup_controls(card);
}

int snd_soc_card_remove(struct snd_soc_card *card)
{
	int ret = 0;

	if (card->probed &&
	    card->remove)
		ret = card->remove(card);

	card->probed = 0;

	return soc_card_ret(card, ret);
}

int snd_soc_card_set_bias_level(struct snd_soc_card *card,
				struct snd_soc_dapm_context *dapm,
				enum snd_soc_bias_level level)
{
	int ret = 0;

	if (card && card->set_bias_level)
		ret = card->set_bias_level(card, dapm, level);

	return soc_card_ret(card, ret);
}

int snd_soc_card_set_bias_level_post(struct snd_soc_card *card,
				     struct snd_soc_dapm_context *dapm,
				     enum snd_soc_bias_level level)
{
	int ret = 0;

	if (card && card->set_bias_level_post)
		ret = card->set_bias_level_post(card, dapm, level);

	return soc_card_ret(card, ret);
}

int snd_soc_card_add_dai_link(struct snd_soc_card *card,
			      struct snd_soc_dai_link *dai_link)
{
	int ret = 0;

	if (card->add_dai_link)
		ret = card->add_dai_link(card, dai_link);

	return soc_card_ret(card, ret);
}
EXPORT_SYMBOL_GPL(snd_soc_card_add_dai_link);

void snd_soc_card_remove_dai_link(struct snd_soc_card *card,
				  struct snd_soc_dai_link *dai_link)
{
	if (card->remove_dai_link)
		card->remove_dai_link(card, dai_link);
}
EXPORT_SYMBOL_GPL(snd_soc_card_remove_dai_link);
