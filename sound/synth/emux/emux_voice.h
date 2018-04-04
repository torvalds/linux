#ifndef __EMUX_VOICE_H
#define __EMUX_VOICE_H

/*
 * A structure to keep track of each hardware voice
 *
 *  Copyright (C) 1999 Steve Ratcliffe
 *  Copyright (c) 1999-2000 Takashi Iwai <tiwai@suse.de>
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
#include <sound/core.h>
#include <sound/emux_synth.h>

/* Prototypes for emux_seq.c */
int snd_emux_init_seq(struct snd_emux *emu, struct snd_card *card, int index);
void snd_emux_detach_seq(struct snd_emux *emu);
struct snd_emux_port *snd_emux_create_port(struct snd_emux *emu, char *name,
					   int max_channels, int type,
					   struct snd_seq_port_callback *callback);
void snd_emux_reset_port(struct snd_emux_port *port);
int snd_emux_event_input(struct snd_seq_event *ev, int direct, void *private,
			 int atomic, int hop);
int snd_emux_inc_count(struct snd_emux *emu);
void snd_emux_dec_count(struct snd_emux *emu);
int snd_emux_init_virmidi(struct snd_emux *emu, struct snd_card *card);
int snd_emux_delete_virmidi(struct snd_emux *emu);

/* Prototypes for emux_synth.c */
void snd_emux_init_voices(struct snd_emux *emu);

void snd_emux_note_on(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_emux_note_off(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_emux_key_press(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_emux_terminate_note(void *p, int note, struct snd_midi_channel *chan);
void snd_emux_control(void *p, int type, struct snd_midi_channel *chan);

void snd_emux_sounds_off_all(struct snd_emux_port *port);
void snd_emux_update_channel(struct snd_emux_port *port,
			     struct snd_midi_channel *chan, int update);
void snd_emux_update_port(struct snd_emux_port *port, int update);

void snd_emux_timer_callback(struct timer_list *t);

/* emux_effect.c */
#ifdef SNDRV_EMUX_USE_RAW_EFFECT
void snd_emux_create_effect(struct snd_emux_port *p);
void snd_emux_delete_effect(struct snd_emux_port *p);
void snd_emux_clear_effect(struct snd_emux_port *p);
void snd_emux_setup_effect(struct snd_emux_voice *vp);
void snd_emux_send_effect_oss(struct snd_emux_port *port,
			      struct snd_midi_channel *chan, int type, int val);
void snd_emux_send_effect(struct snd_emux_port *port,
			  struct snd_midi_channel *chan, int type, int val, int mode);
#endif

/* emux_nrpn.c */
void snd_emux_sysex(void *private_data, unsigned char *buf, int len,
		    int parsed, struct snd_midi_channel_set *chset);
int snd_emux_xg_control(struct snd_emux_port *port,
			struct snd_midi_channel *chan, int param);
void snd_emux_nrpn(void *private_data, struct snd_midi_channel *chan,
		   struct snd_midi_channel_set *chset);

/* emux_oss.c */
void snd_emux_init_seq_oss(struct snd_emux *emu);
void snd_emux_detach_seq_oss(struct snd_emux *emu);

/* emux_proc.c */
#ifdef CONFIG_SND_PROC_FS
void snd_emux_proc_init(struct snd_emux *emu, struct snd_card *card, int device);
void snd_emux_proc_free(struct snd_emux *emu);
#else
static inline void snd_emux_proc_init(struct snd_emux *emu,
				      struct snd_card *card, int device) {}
static inline void snd_emux_proc_free(struct snd_emux *emu) {}
#endif

#define STATE_IS_PLAYING(s) ((s) & SNDRV_EMUX_ST_ON)

/* emux_hwdep.c */
int snd_emux_init_hwdep(struct snd_emux *emu);
void snd_emux_delete_hwdep(struct snd_emux *emu);

#endif
