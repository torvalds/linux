#ifndef __EMU8000_LOCAL_H
#define __EMU8000_LOCAL_H
/*
 *  Local defininitons for the emu8000 (AWE32/64)
 *
 *  Copyright (C) 1999 Steve Ratcliffe
 *  Copyright (C) 1999-2000 Takashi Iwai <tiwai@suse.de>
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
#include <linux/sched.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/emu8000.h>
#include <sound/emu8000_reg.h>

/* emu8000_patch.c */
int snd_emu8000_sample_new(struct snd_emux *rec, struct snd_sf_sample *sp,
			   struct snd_util_memhdr *hdr,
			   const void __user *data, long count);
int snd_emu8000_sample_free(struct snd_emux *rec, struct snd_sf_sample *sp,
			    struct snd_util_memhdr *hdr);
void snd_emu8000_sample_reset(struct snd_emux *rec);

/* emu8000_callback.c */
void snd_emu8000_ops_setup(struct snd_emu8000 *emu);

/* emu8000_pcm.c */
int snd_emu8000_pcm_new(struct snd_card *card, struct snd_emu8000 *emu, int index);

#endif	/* __EMU8000_LOCAL_H */
