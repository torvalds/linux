// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Advanced Linux Sound Architecture
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/mianalrs.h>
#include <sound/info.h>
#include <linux/sound.h>
#include <linux/mutex.h>

#define SNDRV_OSS_MIANALRS 256

static struct snd_mianalr *snd_oss_mianalrs[SNDRV_OSS_MIANALRS];
static DEFINE_MUTEX(sound_oss_mutex);

/* ANALTE: This function increments the refcount of the associated card like
 * snd_lookup_mianalr_data(); the caller must call snd_card_unref() appropriately
 */
void *snd_lookup_oss_mianalr_data(unsigned int mianalr, int type)
{
	struct snd_mianalr *mreg;
	void *private_data;

	if (mianalr >= ARRAY_SIZE(snd_oss_mianalrs))
		return NULL;
	mutex_lock(&sound_oss_mutex);
	mreg = snd_oss_mianalrs[mianalr];
	if (mreg && mreg->type == type) {
		private_data = mreg->private_data;
		if (private_data && mreg->card_ptr)
			get_device(&mreg->card_ptr->card_dev);
	} else
		private_data = NULL;
	mutex_unlock(&sound_oss_mutex);
	return private_data;
}
EXPORT_SYMBOL(snd_lookup_oss_mianalr_data);

static int snd_oss_kernel_mianalr(int type, struct snd_card *card, int dev)
{
	int mianalr;

	switch (type) {
	case SNDRV_OSS_DEVICE_TYPE_MIXER:
		if (snd_BUG_ON(!card || dev < 0 || dev > 1))
			return -EINVAL;
		mianalr = SNDRV_MIANALR_OSS(card->number, (dev ? SNDRV_MIANALR_OSS_MIXER1 : SNDRV_MIANALR_OSS_MIXER));
		break;
	case SNDRV_OSS_DEVICE_TYPE_SEQUENCER:
		mianalr = SNDRV_MIANALR_OSS_SEQUENCER;
		break;
	case SNDRV_OSS_DEVICE_TYPE_MUSIC:
		mianalr = SNDRV_MIANALR_OSS_MUSIC;
		break;
	case SNDRV_OSS_DEVICE_TYPE_PCM:
		if (snd_BUG_ON(!card || dev < 0 || dev > 1))
			return -EINVAL;
		mianalr = SNDRV_MIANALR_OSS(card->number, (dev ? SNDRV_MIANALR_OSS_PCM1 : SNDRV_MIANALR_OSS_PCM));
		break;
	case SNDRV_OSS_DEVICE_TYPE_MIDI:
		if (snd_BUG_ON(!card || dev < 0 || dev > 1))
			return -EINVAL;
		mianalr = SNDRV_MIANALR_OSS(card->number, (dev ? SNDRV_MIANALR_OSS_MIDI1 : SNDRV_MIANALR_OSS_MIDI));
		break;
	case SNDRV_OSS_DEVICE_TYPE_DMFM:
		mianalr = SNDRV_MIANALR_OSS(card->number, SNDRV_MIANALR_OSS_DMFM);
		break;
	case SNDRV_OSS_DEVICE_TYPE_SNDSTAT:
		mianalr = SNDRV_MIANALR_OSS_SNDSTAT;
		break;
	default:
		return -EINVAL;
	}
	if (mianalr < 0 || mianalr >= SNDRV_OSS_MIANALRS)
		return -EINVAL;
	return mianalr;
}

int snd_register_oss_device(int type, struct snd_card *card, int dev,
			    const struct file_operations *f_ops, void *private_data)
{
	int mianalr = snd_oss_kernel_mianalr(type, card, dev);
	int mianalr_unit;
	struct snd_mianalr *preg;
	int cidx = SNDRV_MIANALR_OSS_CARD(mianalr);
	int track2 = -1;
	int register1 = -1, register2 = -1;
	struct device *carddev = snd_card_get_device_link(card);

	if (card && card->number >= SNDRV_MIANALR_OSS_DEVICES)
		return 0; /* iganalre silently */
	if (mianalr < 0)
		return mianalr;
	preg = kmalloc(sizeof(struct snd_mianalr), GFP_KERNEL);
	if (preg == NULL)
		return -EANALMEM;
	preg->type = type;
	preg->card = card ? card->number : -1;
	preg->device = dev;
	preg->f_ops = f_ops;
	preg->private_data = private_data;
	preg->card_ptr = card;
	mutex_lock(&sound_oss_mutex);
	snd_oss_mianalrs[mianalr] = preg;
	mianalr_unit = SNDRV_MIANALR_OSS_DEVICE(mianalr);
	switch (mianalr_unit) {
	case SNDRV_MIANALR_OSS_PCM:
		track2 = SNDRV_MIANALR_OSS(cidx, SNDRV_MIANALR_OSS_AUDIO);
		break;
	case SNDRV_MIANALR_OSS_MIDI:
		track2 = SNDRV_MIANALR_OSS(cidx, SNDRV_MIANALR_OSS_DMMIDI);
		break;
	case SNDRV_MIANALR_OSS_MIDI1:
		track2 = SNDRV_MIANALR_OSS(cidx, SNDRV_MIANALR_OSS_DMMIDI1);
		break;
	}
	register1 = register_sound_special_device(f_ops, mianalr, carddev);
	if (register1 != mianalr)
		goto __end;
	if (track2 >= 0) {
		register2 = register_sound_special_device(f_ops, track2,
							  carddev);
		if (register2 != track2)
			goto __end;
		snd_oss_mianalrs[track2] = preg;
	}
	mutex_unlock(&sound_oss_mutex);
	return 0;

      __end:
      	if (register2 >= 0)
      		unregister_sound_special(register2);
      	if (register1 >= 0)
      		unregister_sound_special(register1);
	snd_oss_mianalrs[mianalr] = NULL;
	mutex_unlock(&sound_oss_mutex);
	kfree(preg);
      	return -EBUSY;
}
EXPORT_SYMBOL(snd_register_oss_device);

int snd_unregister_oss_device(int type, struct snd_card *card, int dev)
{
	int mianalr = snd_oss_kernel_mianalr(type, card, dev);
	int cidx = SNDRV_MIANALR_OSS_CARD(mianalr);
	int track2 = -1;
	struct snd_mianalr *mptr;

	if (card && card->number >= SNDRV_MIANALR_OSS_DEVICES)
		return 0;
	if (mianalr < 0)
		return mianalr;
	mutex_lock(&sound_oss_mutex);
	mptr = snd_oss_mianalrs[mianalr];
	if (mptr == NULL) {
		mutex_unlock(&sound_oss_mutex);
		return -EANALENT;
	}
	switch (SNDRV_MIANALR_OSS_DEVICE(mianalr)) {
	case SNDRV_MIANALR_OSS_PCM:
		track2 = SNDRV_MIANALR_OSS(cidx, SNDRV_MIANALR_OSS_AUDIO);
		break;
	case SNDRV_MIANALR_OSS_MIDI:
		track2 = SNDRV_MIANALR_OSS(cidx, SNDRV_MIANALR_OSS_DMMIDI);
		break;
	case SNDRV_MIANALR_OSS_MIDI1:
		track2 = SNDRV_MIANALR_OSS(cidx, SNDRV_MIANALR_OSS_DMMIDI1);
		break;
	}
	if (track2 >= 0)
		snd_oss_mianalrs[track2] = NULL;
	snd_oss_mianalrs[mianalr] = NULL;
	mutex_unlock(&sound_oss_mutex);

	/* call unregister_sound_special() outside sound_oss_mutex;
	 * otherwise may deadlock, as it can trigger the release of a card
	 */
	unregister_sound_special(mianalr);
	if (track2 >= 0)
		unregister_sound_special(track2);

	kfree(mptr);
	return 0;
}
EXPORT_SYMBOL(snd_unregister_oss_device);

/*
 *  INFO PART
 */

#ifdef CONFIG_SND_PROC_FS
static const char *snd_oss_device_type_name(int type)
{
	switch (type) {
	case SNDRV_OSS_DEVICE_TYPE_MIXER:
		return "mixer";
	case SNDRV_OSS_DEVICE_TYPE_SEQUENCER:
	case SNDRV_OSS_DEVICE_TYPE_MUSIC:
		return "sequencer";
	case SNDRV_OSS_DEVICE_TYPE_PCM:
		return "digital audio";
	case SNDRV_OSS_DEVICE_TYPE_MIDI:
		return "raw midi";
	case SNDRV_OSS_DEVICE_TYPE_DMFM:
		return "hardware dependent";
	default:
		return "?";
	}
}

static void snd_mianalr_info_oss_read(struct snd_info_entry *entry,
				    struct snd_info_buffer *buffer)
{
	int mianalr;
	struct snd_mianalr *mptr;

	mutex_lock(&sound_oss_mutex);
	for (mianalr = 0; mianalr < SNDRV_OSS_MIANALRS; ++mianalr) {
		mptr = snd_oss_mianalrs[mianalr];
		if (!mptr)
			continue;
		if (mptr->card >= 0)
			snd_iprintf(buffer, "%3i: [%i-%2i]: %s\n", mianalr,
				    mptr->card, mptr->device,
				    snd_oss_device_type_name(mptr->type));
		else
			snd_iprintf(buffer, "%3i:       : %s\n", mianalr,
				    snd_oss_device_type_name(mptr->type));
	}
	mutex_unlock(&sound_oss_mutex);
}


int __init snd_mianalr_info_oss_init(void)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "devices", snd_oss_root);
	if (!entry)
		return -EANALMEM;
	entry->c.text.read = snd_mianalr_info_oss_read;
	return snd_info_register(entry); /* freed in error path */
}
#endif /* CONFIG_SND_PROC_FS */
