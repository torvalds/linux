/*
 *  Advanced Linux Sound Architecture
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
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

#include <sound/driver.h>

#ifdef CONFIG_SND_OSSEMUL

#if !defined(CONFIG_SOUND) && !(defined(MODULE) && defined(CONFIG_SOUND_MODULE))
#error "Enable the OSS soundcore multiplexer (CONFIG_SOUND) in the kernel."
#endif

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <linux/sound.h>

#define SNDRV_OS_MINORS		256

static struct list_head snd_oss_minors_hash[SNDRV_CARDS];

static DECLARE_MUTEX(sound_oss_mutex);

static snd_minor_t *snd_oss_minor_search(int minor)
{
	struct list_head *list;
	snd_minor_t *mptr;

	list_for_each(list, &snd_oss_minors_hash[SNDRV_MINOR_OSS_CARD(minor)]) {
		mptr = list_entry(list, snd_minor_t, list);
		if (mptr->number == minor)
			return mptr;
	}
	return NULL;
}

static int snd_oss_kernel_minor(int type, snd_card_t * card, int dev)
{
	int minor;

	switch (type) {
	case SNDRV_OSS_DEVICE_TYPE_MIXER:
		snd_assert(card != NULL && dev <= 1, return -EINVAL);
		minor = SNDRV_MINOR_OSS(card->number, (dev ? SNDRV_MINOR_OSS_MIXER1 : SNDRV_MINOR_OSS_MIXER));
		break;
	case SNDRV_OSS_DEVICE_TYPE_SEQUENCER:
		minor = SNDRV_MINOR_OSS_SEQUENCER;
		break;
	case SNDRV_OSS_DEVICE_TYPE_MUSIC:
		minor = SNDRV_MINOR_OSS_MUSIC;
		break;
	case SNDRV_OSS_DEVICE_TYPE_PCM:
		snd_assert(card != NULL && dev <= 1, return -EINVAL);
		minor = SNDRV_MINOR_OSS(card->number, (dev ? SNDRV_MINOR_OSS_PCM1 : SNDRV_MINOR_OSS_PCM));
		break;
	case SNDRV_OSS_DEVICE_TYPE_MIDI:
		snd_assert(card != NULL && dev <= 1, return -EINVAL);
		minor = SNDRV_MINOR_OSS(card->number, (dev ? SNDRV_MINOR_OSS_MIDI1 : SNDRV_MINOR_OSS_MIDI));
		break;
	case SNDRV_OSS_DEVICE_TYPE_DMFM:
		minor = SNDRV_MINOR_OSS(card->number, SNDRV_MINOR_OSS_DMFM);
		break;
	case SNDRV_OSS_DEVICE_TYPE_SNDSTAT:
		minor = SNDRV_MINOR_OSS_SNDSTAT;
		break;
	default:
		return -EINVAL;
	}
	snd_assert(minor >= 0 && minor < SNDRV_OS_MINORS, return -EINVAL);
	return minor;
}

int snd_register_oss_device(int type, snd_card_t * card, int dev, snd_minor_t * reg, const char *name)
{
	int minor = snd_oss_kernel_minor(type, card, dev);
	int minor_unit;
	snd_minor_t *preg;
	int cidx = SNDRV_MINOR_OSS_CARD(minor);
	int track2 = -1;
	int register1 = -1, register2 = -1;
	struct device *carddev = NULL;

	if (minor < 0)
		return minor;
	preg = (snd_minor_t *)kmalloc(sizeof(snd_minor_t), GFP_KERNEL);
	if (preg == NULL)
		return -ENOMEM;
	*preg = *reg;
	preg->number = minor;
	preg->device = dev;
	down(&sound_oss_mutex);
	list_add_tail(&preg->list, &snd_oss_minors_hash[cidx]);
	minor_unit = SNDRV_MINOR_OSS_DEVICE(minor);
	switch (minor_unit) {
	case SNDRV_MINOR_OSS_PCM:
		track2 = SNDRV_MINOR_OSS(cidx, SNDRV_MINOR_OSS_AUDIO);
		break;
	case SNDRV_MINOR_OSS_MIDI:
		track2 = SNDRV_MINOR_OSS(cidx, SNDRV_MINOR_OSS_DMMIDI);
		break;
	case SNDRV_MINOR_OSS_MIDI1:
		track2 = SNDRV_MINOR_OSS(cidx, SNDRV_MINOR_OSS_DMMIDI1);
		break;
	}
	if (card)
		carddev = card->dev;
	register1 = register_sound_special_device(reg->f_ops, minor, carddev);
	if (register1 != minor)
		goto __end;
	if (track2 >= 0) {
		register2 = register_sound_special_device(reg->f_ops, track2, carddev);
		if (register2 != track2)
			goto __end;
	}
	up(&sound_oss_mutex);
	return 0;

      __end:
      	if (register2 >= 0)
      		unregister_sound_special(register2);
      	if (register1 >= 0)
      		unregister_sound_special(register1);
      	list_del(&preg->list);
	up(&sound_oss_mutex);
	kfree(preg);
      	return -EBUSY;
}

int snd_unregister_oss_device(int type, snd_card_t * card, int dev)
{
	int minor = snd_oss_kernel_minor(type, card, dev);
	int cidx = SNDRV_MINOR_OSS_CARD(minor);
	int track2 = -1;
	snd_minor_t *mptr;

	if (minor < 0)
		return minor;
	down(&sound_oss_mutex);
	mptr = snd_oss_minor_search(minor);
	if (mptr == NULL) {
		up(&sound_oss_mutex);
		return -ENOENT;
	}
	unregister_sound_special(minor);
	switch (SNDRV_MINOR_OSS_DEVICE(minor)) {
	case SNDRV_MINOR_OSS_PCM:
		track2 = SNDRV_MINOR_OSS(cidx, SNDRV_MINOR_OSS_AUDIO);
		break;
	case SNDRV_MINOR_OSS_MIDI:
		track2 = SNDRV_MINOR_OSS(cidx, SNDRV_MINOR_OSS_DMMIDI);
		break;
	case SNDRV_MINOR_OSS_MIDI1:
		track2 = SNDRV_MINOR_OSS(cidx, SNDRV_MINOR_OSS_DMMIDI1);
		break;
	}
	if (track2 >= 0)
		unregister_sound_special(track2);
	list_del(&mptr->list);
	up(&sound_oss_mutex);
	kfree(mptr);
	return 0;
}

/*
 *  INFO PART
 */

#ifdef CONFIG_PROC_FS

static snd_info_entry_t *snd_minor_info_oss_entry = NULL;

static void snd_minor_info_oss_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	int card, dev;
	struct list_head *list;
	snd_minor_t *mptr;

	down(&sound_oss_mutex);
	for (card = 0; card < SNDRV_CARDS; card++) {
		list_for_each(list, &snd_oss_minors_hash[card]) {
			mptr = list_entry(list, snd_minor_t, list);
			dev = SNDRV_MINOR_OSS_DEVICE(mptr->number);
		        if (dev != SNDRV_MINOR_OSS_SNDSTAT &&
			    dev != SNDRV_MINOR_OSS_SEQUENCER &&
			    dev != SNDRV_MINOR_OSS_MUSIC)
				snd_iprintf(buffer, "%3i: [%i-%2i]: %s\n", mptr->number, card, dev, mptr->comment);
			else
				snd_iprintf(buffer, "%3i:       : %s\n", mptr->number, mptr->comment);
		}
	}
	up(&sound_oss_mutex);
}

#endif /* CONFIG_PROC_FS */

int __init snd_minor_info_oss_init(void)
{
#ifdef CONFIG_PROC_FS
	snd_info_entry_t *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "devices", snd_oss_root);
	if (entry) {
		entry->c.text.read_size = PAGE_SIZE;
		entry->c.text.read = snd_minor_info_oss_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	snd_minor_info_oss_entry = entry;
#endif
	return 0;
}

int __exit snd_minor_info_oss_done(void)
{
#ifdef CONFIG_PROC_FS
	if (snd_minor_info_oss_entry)
		snd_info_unregister(snd_minor_info_oss_entry);
#endif
	return 0;
}

int __init snd_oss_init_module(void)
{
	int card;
	
	for (card = 0; card < SNDRV_CARDS; card++)
		INIT_LIST_HEAD(&snd_oss_minors_hash[card]);
	return 0;
}

#endif /* CONFIG_SND_OSSEMUL */
