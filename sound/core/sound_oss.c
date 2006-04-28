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
#include <linux/mutex.h>

#define SNDRV_OSS_MINORS 128

static struct snd_minor *snd_oss_minors[SNDRV_OSS_MINORS];
static DEFINE_MUTEX(sound_oss_mutex);

void *snd_lookup_oss_minor_data(unsigned int minor, int type)
{
	struct snd_minor *mreg;
	void *private_data;

	if (minor >= ARRAY_SIZE(snd_oss_minors))
		return NULL;
	mutex_lock(&sound_oss_mutex);
	mreg = snd_oss_minors[minor];
	if (mreg && mreg->type == type)
		private_data = mreg->private_data;
	else
		private_data = NULL;
	mutex_unlock(&sound_oss_mutex);
	return private_data;
}

EXPORT_SYMBOL(snd_lookup_oss_minor_data);

static int snd_oss_kernel_minor(int type, struct snd_card *card, int dev)
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
	snd_assert(minor >= 0 && minor < SNDRV_OSS_MINORS, return -EINVAL);
	return minor;
}

int snd_register_oss_device(int type, struct snd_card *card, int dev,
			    const struct file_operations *f_ops, void *private_data,
			    const char *name)
{
	int minor = snd_oss_kernel_minor(type, card, dev);
	int minor_unit;
	struct snd_minor *preg;
	int cidx = SNDRV_MINOR_OSS_CARD(minor);
	int track2 = -1;
	int register1 = -1, register2 = -1;
	struct device *carddev = NULL;

	if (card && card->number >= 8)
		return 0; /* ignore silently */
	if (minor < 0)
		return minor;
	preg = kmalloc(sizeof(struct snd_minor), GFP_KERNEL);
	if (preg == NULL)
		return -ENOMEM;
	preg->type = type;
	preg->card = card ? card->number : -1;
	preg->device = dev;
	preg->f_ops = f_ops;
	preg->private_data = private_data;
	mutex_lock(&sound_oss_mutex);
	snd_oss_minors[minor] = preg;
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
	register1 = register_sound_special_device(f_ops, minor, carddev);
	if (register1 != minor)
		goto __end;
	if (track2 >= 0) {
		register2 = register_sound_special_device(f_ops, track2,
							  carddev);
		if (register2 != track2)
			goto __end;
		snd_oss_minors[track2] = preg;
	}
	mutex_unlock(&sound_oss_mutex);
	return 0;

      __end:
      	if (register2 >= 0)
      		unregister_sound_special(register2);
      	if (register1 >= 0)
      		unregister_sound_special(register1);
	snd_oss_minors[minor] = NULL;
	mutex_unlock(&sound_oss_mutex);
	kfree(preg);
      	return -EBUSY;
}

EXPORT_SYMBOL(snd_register_oss_device);

int snd_unregister_oss_device(int type, struct snd_card *card, int dev)
{
	int minor = snd_oss_kernel_minor(type, card, dev);
	int cidx = SNDRV_MINOR_OSS_CARD(minor);
	int track2 = -1;
	struct snd_minor *mptr;

	if (card && card->number >= 8)
		return 0;
	if (minor < 0)
		return minor;
	mutex_lock(&sound_oss_mutex);
	mptr = snd_oss_minors[minor];
	if (mptr == NULL) {
		mutex_unlock(&sound_oss_mutex);
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
	if (track2 >= 0) {
		unregister_sound_special(track2);
		snd_oss_minors[track2] = NULL;
	}
	snd_oss_minors[minor] = NULL;
	mutex_unlock(&sound_oss_mutex);
	kfree(mptr);
	return 0;
}

EXPORT_SYMBOL(snd_unregister_oss_device);

/*
 *  INFO PART
 */

#ifdef CONFIG_PROC_FS

static struct snd_info_entry *snd_minor_info_oss_entry = NULL;

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

static void snd_minor_info_oss_read(struct snd_info_entry *entry,
				    struct snd_info_buffer *buffer)
{
	int minor;
	struct snd_minor *mptr;

	mutex_lock(&sound_oss_mutex);
	for (minor = 0; minor < SNDRV_OSS_MINORS; ++minor) {
		if (!(mptr = snd_oss_minors[minor]))
			continue;
		if (mptr->card >= 0)
			snd_iprintf(buffer, "%3i: [%i-%2i]: %s\n", minor,
				    mptr->card, mptr->device,
				    snd_oss_device_type_name(mptr->type));
		else
			snd_iprintf(buffer, "%3i:       : %s\n", minor,
				    snd_oss_device_type_name(mptr->type));
	}
	mutex_unlock(&sound_oss_mutex);
}


int __init snd_minor_info_oss_init(void)
{
	struct snd_info_entry *entry;

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
	return 0;
}

int __exit snd_minor_info_oss_done(void)
{
	if (snd_minor_info_oss_entry)
		snd_info_unregister(snd_minor_info_oss_entry);
	return 0;
}
#endif /* CONFIG_PROC_FS */

#endif /* CONFIG_SND_OSSEMUL */
