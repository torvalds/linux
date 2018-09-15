/*
 *  Copyright (C) 2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Routines for control of EMU WaveTable chip
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
 */

#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/emux_synth.h>
#include <linux/init.h>
#include <linux/module.h>
#include "emux_voice.h"

MODULE_AUTHOR("Takashi Iwai");
MODULE_DESCRIPTION("Routines for control of EMU WaveTable chip");
MODULE_LICENSE("GPL");

/*
 * create a new hardware dependent device for Emu8000/Emu10k1
 */
int snd_emux_new(struct snd_emux **remu)
{
	struct snd_emux *emu;

	*remu = NULL;
	emu = kzalloc(sizeof(*emu), GFP_KERNEL);
	if (emu == NULL)
		return -ENOMEM;

	spin_lock_init(&emu->voice_lock);
	mutex_init(&emu->register_mutex);

	emu->client = -1;
#if IS_ENABLED(CONFIG_SND_SEQUENCER_OSS)
	emu->oss_synth = NULL;
#endif
	emu->max_voices = 0;
	emu->use_time = 0;

	timer_setup(&emu->tlist, snd_emux_timer_callback, 0);
	emu->timer_active = 0;

	*remu = emu;
	return 0;
}

EXPORT_SYMBOL(snd_emux_new);

/*
 */
static int sf_sample_new(void *private_data, struct snd_sf_sample *sp,
				  struct snd_util_memhdr *hdr,
				  const void __user *buf, long count)
{
	struct snd_emux *emu = private_data;
	return emu->ops.sample_new(emu, sp, hdr, buf, count);
	
}

static int sf_sample_free(void *private_data, struct snd_sf_sample *sp,
				   struct snd_util_memhdr *hdr)
{
	struct snd_emux *emu = private_data;
	return emu->ops.sample_free(emu, sp, hdr);
	
}

static void sf_sample_reset(void *private_data)
{
	struct snd_emux *emu = private_data;
	emu->ops.sample_reset(emu);
}

int snd_emux_register(struct snd_emux *emu, struct snd_card *card, int index, char *name)
{
	int err;
	struct snd_sf_callback sf_cb;

	if (snd_BUG_ON(!emu->hw || emu->max_voices <= 0))
		return -EINVAL;
	if (snd_BUG_ON(!card || !name))
		return -EINVAL;

	emu->card = card;
	emu->name = kstrdup(name, GFP_KERNEL);
	emu->voices = kcalloc(emu->max_voices, sizeof(struct snd_emux_voice),
			      GFP_KERNEL);
	if (emu->voices == NULL)
		return -ENOMEM;

	/* create soundfont list */
	memset(&sf_cb, 0, sizeof(sf_cb));
	sf_cb.private_data = emu;
	if (emu->ops.sample_new)
		sf_cb.sample_new = sf_sample_new;
	if (emu->ops.sample_free)
		sf_cb.sample_free = sf_sample_free;
	if (emu->ops.sample_reset)
		sf_cb.sample_reset = sf_sample_reset;
	emu->sflist = snd_sf_new(&sf_cb, emu->memhdr);
	if (emu->sflist == NULL)
		return -ENOMEM;

	if ((err = snd_emux_init_hwdep(emu)) < 0)
		return err;

	snd_emux_init_voices(emu);

	snd_emux_init_seq(emu, card, index);
#if IS_ENABLED(CONFIG_SND_SEQUENCER_OSS)
	snd_emux_init_seq_oss(emu);
#endif
	snd_emux_init_virmidi(emu, card);

	snd_emux_proc_init(emu, card, index);
	return 0;
}

EXPORT_SYMBOL(snd_emux_register);

/*
 */
int snd_emux_free(struct snd_emux *emu)
{
	unsigned long flags;

	if (! emu)
		return -EINVAL;

	spin_lock_irqsave(&emu->voice_lock, flags);
	if (emu->timer_active)
		del_timer(&emu->tlist);
	spin_unlock_irqrestore(&emu->voice_lock, flags);

	snd_emux_proc_free(emu);
	snd_emux_delete_virmidi(emu);
#if IS_ENABLED(CONFIG_SND_SEQUENCER_OSS)
	snd_emux_detach_seq_oss(emu);
#endif
	snd_emux_detach_seq(emu);
	snd_emux_delete_hwdep(emu);
	snd_sf_free(emu->sflist);
	kfree(emu->voices);
	kfree(emu->name);
	kfree(emu);
	return 0;
}

EXPORT_SYMBOL(snd_emux_free);
