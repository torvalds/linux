/* SPDX-License-Identifier: GPL-2.0
 *
 * soc-card.h
 *
 * Copyright (C) 2019 Renesas Electronics Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */
#ifndef __SOC_CARD_H
#define __SOC_CARD_H

enum snd_soc_card_subclass {
	SND_SOC_CARD_CLASS_INIT		= 0,
	SND_SOC_CARD_CLASS_RUNTIME	= 1,
};

struct snd_kcontrol *snd_soc_card_get_kcontrol(struct snd_soc_card *soc_card,
					       const char *name);
int snd_soc_card_jack_new(struct snd_soc_card *card, const char *id, int type,
			  struct snd_soc_jack *jack,
			  struct snd_soc_jack_pin *pins, unsigned int num_pins);

int snd_soc_card_suspend_pre(struct snd_soc_card *card);
int snd_soc_card_suspend_post(struct snd_soc_card *card);
int snd_soc_card_resume_pre(struct snd_soc_card *card);
int snd_soc_card_resume_post(struct snd_soc_card *card);

int snd_soc_card_probe(struct snd_soc_card *card);
int snd_soc_card_late_probe(struct snd_soc_card *card);
int snd_soc_card_remove(struct snd_soc_card *card);

int snd_soc_card_set_bias_level(struct snd_soc_card *card,
				struct snd_soc_dapm_context *dapm,
				enum snd_soc_bias_level level);
int snd_soc_card_set_bias_level_post(struct snd_soc_card *card,
				     struct snd_soc_dapm_context *dapm,
				     enum snd_soc_bias_level level);

int snd_soc_card_add_dai_link(struct snd_soc_card *card,
			      struct snd_soc_dai_link *dai_link);
void snd_soc_card_remove_dai_link(struct snd_soc_card *card,
				  struct snd_soc_dai_link *dai_link);

/* device driver data */
static inline void snd_soc_card_set_drvdata(struct snd_soc_card *card,
					    void *data)
{
	card->drvdata = data;
}

static inline void *snd_soc_card_get_drvdata(struct snd_soc_card *card)
{
	return card->drvdata;
}

static inline
struct snd_soc_dai *snd_soc_card_get_codec_dai(struct snd_soc_card *card,
					       const char *dai_name)
{
	struct snd_soc_pcm_runtime *rtd;

	for_each_card_rtds(card, rtd) {
		if (!strcmp(asoc_rtd_to_codec(rtd, 0)->name, dai_name))
			return asoc_rtd_to_codec(rtd, 0);
	}

	return NULL;
}

#endif /* __SOC_CARD_H */
