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

#include <sound/driver.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/emu8000.h>
#include <sound/emu8000_reg.h>

/* emu8000_patch.c */
int snd_emu8000_sample_new(snd_emux_t *rec, snd_sf_sample_t *sp, snd_util_memhdr_t *hdr, const void __user *data, long count);
int snd_emu8000_sample_free(snd_emux_t *rec, snd_sf_sample_t *sp, snd_util_memhdr_t *hdr);
void snd_emu8000_sample_reset(snd_emux_t *rec);

/* emu8000_callback.c */
void snd_emu8000_ops_setup(emu8000_t *emu);

/* emu8000_pcm.c */
int snd_emu8000_pcm_new(snd_card_t *card, emu8000_t *emu, int index);

#endif	/* __EMU8000_LOCAL_H */
