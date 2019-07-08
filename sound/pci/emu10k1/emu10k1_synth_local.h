/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __EMU10K1_SYNTH_LOCAL_H
#define __EMU10K1_SYNTH_LOCAL_H
/*
 *  Local defininitons for Emu10k1 wavetable
 *
 *  Copyright (C) 2000 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/time.h>
#include <sound/core.h>
#include <sound/emu10k1_synth.h>

/* emu10k1_patch.c */
int snd_emu10k1_sample_new(struct snd_emux *private_data,
			   struct snd_sf_sample *sp,
			   struct snd_util_memhdr *hdr,
			   const void __user *_data, long count);
int snd_emu10k1_sample_free(struct snd_emux *private_data,
			    struct snd_sf_sample *sp,
			    struct snd_util_memhdr *hdr);
int snd_emu10k1_memhdr_init(struct snd_emux *emu);

/* emu10k1_callback.c */
void snd_emu10k1_ops_setup(struct snd_emux *emu);
int snd_emu10k1_synth_get_voice(struct snd_emu10k1 *hw);


#endif	/* __EMU10K1_SYNTH_LOCAL_H */
