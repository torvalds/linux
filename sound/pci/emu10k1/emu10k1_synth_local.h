#ifndef __EMU10K1_SYNTH_LOCAL_H
#define __EMU10K1_SYNTH_LOCAL_H
/*
 *  Local defininitons for Emu10k1 wavetable
 *
 *  Copyright (C) 2000 Takashi Iwai <tiwai@suse.de>
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

#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/emu10k1_synth.h>

/* emu10k1_patch.c */
int snd_emu10k1_sample_new(snd_emux_t *private_data, snd_sf_sample_t *sp, snd_util_memhdr_t *hdr, const void __user *_data, long count);
int snd_emu10k1_sample_free(snd_emux_t *private_data, snd_sf_sample_t *sp, snd_util_memhdr_t *hdr);
int snd_emu10k1_memhdr_init(snd_emux_t *emu);

/* emu10k1_callback.c */
void snd_emu10k1_ops_setup(snd_emux_t *emu);
int snd_emu10k1_synth_get_voice(emu10k1_t *hw);


#endif	/* __EMU10K1_SYNTH_LOCAL_H */
