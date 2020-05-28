// SPDX-License-Identifier: GPL-2.0
//
// soc-card.c
//
// Copyright (C) 2019 Renesas Electronics Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
#include <sound/soc.h>

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
