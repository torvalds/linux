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
	SND_SOC_CARD_CLASS_ROOT		= 0,
	SND_SOC_CARD_CLASS_RUNTIME	= 1,
};

static inline void snd_soc_card_mutex_lock_root(struct snd_soc_card *card)
{
	mutex_lock_nested(&card->mutex, SND_SOC_CARD_CLASS_ROOT);
}

static inline void snd_soc_card_mutex_lock(struct snd_soc_card *card)
{
	mutex_lock_nested(&card->mutex, SND_SOC_CARD_CLASS_RUNTIME);
}

static inline void snd_soc_card_mutex_unlock(struct snd_soc_card *card)
{
	mutex_unlock(&card->mutex);
}

struct snd_kcontrol *snd_soc_card_get_kcontrol(struct snd_soc_card *soc_card,
					       const char *name);
struct snd_kcontrol *snd_soc_card_get_kcontrol_locked(struct snd_soc_card *soc_card,
						      const char *name);
int snd_soc_card_jack_new(struct snd_soc_card *card, const char *id, int type,
			  struct snd_soc_jack *jack);
int snd_soc_card_jack_new_pins(struct snd_soc_card *card, const char *id,
			       int type, struct snd_soc_jack *jack,
			       struct snd_soc_jack_pin *pins,
			       unsigned int num_pins);

int snd_soc_card_suspend_pre(struct snd_soc_card *card);
int snd_soc_card_suspend_post(struct snd_soc_card *card);
int snd_soc_card_resume_pre(struct snd_soc_card *card);
int snd_soc_card_resume_post(struct snd_soc_card *card);

int snd_soc_card_probe(struct snd_soc_card *card);
int snd_soc_card_late_probe(struct snd_soc_card *card);
void snd_soc_card_fixup_controls(struct snd_soc_card *card);
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

#ifdef CONFIG_PCI
static inline void snd_soc_card_set_pci_ssid(struct snd_soc_card *card,
					     unsigned short vendor,
					     unsigned short device)
{
	card->pci_subsystem_vendor = vendor;
	card->pci_subsystem_device = device;
	card->pci_subsystem_set = true;
}

static inline int snd_soc_card_get_pci_ssid(struct snd_soc_card *card,
					    unsigned short *vendor,
					    unsigned short *device)
{
	if (!card->pci_subsystem_set)
		return -ENOENT;

	*vendor = card->pci_subsystem_vendor;
	*device = card->pci_subsystem_device;

	return 0;
}
#else /* !CONFIG_PCI */
static inline void snd_soc_card_set_pci_ssid(struct snd_soc_card *card,
					     unsigned short vendor,
					     unsigned short device)
{
}

static inline int snd_soc_card_get_pci_ssid(struct snd_soc_card *card,
					    unsigned short *vendor,
					    unsigned short *device)
{
	return -ENOENT;
}
#endif /* CONFIG_PCI */

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
		if (!strcmp(snd_soc_rtd_to_codec(rtd, 0)->name, dai_name))
			return snd_soc_rtd_to_codec(rtd, 0);
	}

	return NULL;
}

#endif /* __SOC_CARD_H */
