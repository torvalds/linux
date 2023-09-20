// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Creative Labs, Inc.
 *                   Lee Revell <rlrevell@joe-job.com>
 *  Routines for control of EMU10K1 chips - voice manager
 *
 *  Rewrote voice allocator for multichannel support - rlrevell 12/2004
 * 
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
 */

#include <linux/time.h>
#include <linux/export.h>
#include <sound/core.h>
#include <sound/emu10k1.h>

/* Previously the voice allocator started at 0 every time.  The new voice 
 * allocator uses a round robin scheme.  The next free voice is tracked in 
 * the card record and each allocation begins where the last left off.  The 
 * hardware requires stereo interleaved voices be aligned to an even/odd 
 * boundary.
 *							--rlrevell
 */

static int voice_alloc(struct snd_emu10k1 *emu, int type, int number,
		       struct snd_emu10k1_pcm *epcm, struct snd_emu10k1_voice **rvoice)
{
	struct snd_emu10k1_voice *voice;
	int i, j, k, skip;

	for (i = emu->next_free_voice, j = 0; j < NUM_G; i = (i + skip) % NUM_G, j += skip) {
		/*
		dev_dbg(emu->card->dev, "i %d j %d next free %d!\n",
		       i, j, emu->next_free_voice);
		*/

		/* stereo voices must be even/odd */
		if ((number > 1) && (i % 2)) {
			skip = 1;
			continue;
		}

		for (k = 0; k < number; k++) {
			voice = &emu->voices[i + k];
			if (voice->use) {
				skip = k + 1;
				goto next;
			}
		}

		for (k = 0; k < number; k++) {
			voice = &emu->voices[i + k];
			voice->use = type;
			voice->epcm = epcm;
			/* dev_dbg(emu->card->dev, "allocated voice %d\n", i + k); */
		}
		voice->last = 1;

		*rvoice = &emu->voices[i];
		emu->next_free_voice = (i + number) % NUM_G;
		return 0;

	next: ;
	}
	return -ENOMEM;  // -EBUSY would have been better
}

static void voice_free(struct snd_emu10k1 *emu,
		       struct snd_emu10k1_voice *pvoice)
{
	if (pvoice->dirty)
		snd_emu10k1_voice_init(emu, pvoice->number);
	pvoice->interrupt = NULL;
	pvoice->use = pvoice->dirty = pvoice->last = 0;
	pvoice->epcm = NULL;
}

int snd_emu10k1_voice_alloc(struct snd_emu10k1 *emu, int type, int count, int channels,
			    struct snd_emu10k1_pcm *epcm, struct snd_emu10k1_voice **rvoice)
{
	unsigned long flags;
	int result;

	if (snd_BUG_ON(!rvoice))
		return -EINVAL;
	if (snd_BUG_ON(!count))
		return -EINVAL;
	if (snd_BUG_ON(!channels))
		return -EINVAL;

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (int got = 0; got < channels; ) {
		result = voice_alloc(emu, type, count, epcm, &rvoice[got]);
		if (result == 0) {
			got++;
			/*
			dev_dbg(emu->card->dev, "voice alloc - %i, %i of %i\n",
			        rvoice[got - 1]->number, got, want);
			*/
			continue;
		}
		if (type != EMU10K1_SYNTH && emu->get_synth_voice) {
			/* free a voice from synth */
			result = emu->get_synth_voice(emu);
			if (result >= 0) {
				voice_free(emu, &emu->voices[result]);
				continue;
			}
		}
		for (int i = 0; i < got; i++) {
			for (int j = 0; j < count; j++)
				voice_free(emu, rvoice[i] + j);
			rvoice[i] = NULL;
		}
		break;
	}
	spin_unlock_irqrestore(&emu->voice_lock, flags);

	return result;
}

EXPORT_SYMBOL(snd_emu10k1_voice_alloc);

int snd_emu10k1_voice_free(struct snd_emu10k1 *emu,
			   struct snd_emu10k1_voice *pvoice)
{
	unsigned long flags;
	int last;

	if (snd_BUG_ON(!pvoice))
		return -EINVAL;
	spin_lock_irqsave(&emu->voice_lock, flags);
	do {
		last = pvoice->last;
		voice_free(emu, pvoice++);
	} while (!last);
	spin_unlock_irqrestore(&emu->voice_lock, flags);
	return 0;
}

EXPORT_SYMBOL(snd_emu10k1_voice_free);
