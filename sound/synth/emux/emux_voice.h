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

#include <sound/driver.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <sound/core.h>
#include <sound/emux_synth.h>

/* Prototypes for emux_seq.c */
int snd_emux_init_seq(snd_emux_t *emu, snd_card_t *card, int index);
void snd_emux_detach_seq(snd_emux_t *emu);
snd_emux_port_t *snd_emux_create_port(snd_emux_t *emu, char *name, int max_channels, int type, snd_seq_port_callback_t *callback);
void snd_emux_reset_port(snd_emux_port_t *port);
int snd_emux_event_input(snd_seq_event_t *ev, int direct, void *private, int atomic, int hop);
int snd_emux_inc_count(snd_emux_t *emu);
void snd_emux_dec_count(snd_emux_t *emu);
int snd_emux_init_virmidi(snd_emux_t *emu, snd_card_t *card);
int snd_emux_delete_virmidi(snd_emux_t *emu);

/* Prototypes for emux_synth.c */
void snd_emux_init_voices(snd_emux_t *emu);

void snd_emux_note_on(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_emux_note_off(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_emux_key_press(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_emux_terminate_note(void *p, int note, snd_midi_channel_t *chan);
void snd_emux_control(void *p, int type, struct snd_midi_channel *chan);

void snd_emux_sounds_off_all(snd_emux_port_t *port);
void snd_emux_update_channel(snd_emux_port_t *port, snd_midi_channel_t *chan, int update);
void snd_emux_update_port(snd_emux_port_t *port, int update);

void snd_emux_timer_callback(unsigned long data);

/* emux_effect.c */
#ifdef SNDRV_EMUX_USE_RAW_EFFECT
void snd_emux_create_effect(snd_emux_port_t *p);
void snd_emux_delete_effect(snd_emux_port_t *p);
void snd_emux_clear_effect(snd_emux_port_t *p);
void snd_emux_setup_effect(snd_emux_voice_t *vp);
void snd_emux_send_effect_oss(snd_emux_port_t *port, snd_midi_channel_t *chan, int type, int val);
void snd_emux_send_effect(snd_emux_port_t *port, snd_midi_channel_t *chan, int type, int val, int mode);
#endif

/* emux_nrpn.c */
void snd_emux_sysex(void *private_data, unsigned char *buf, int len, int parsed, snd_midi_channel_set_t *chset);
int snd_emux_xg_control(snd_emux_port_t *port, snd_midi_channel_t *chan, int param);
void snd_emux_nrpn(void *private_data, snd_midi_channel_t *chan, snd_midi_channel_set_t *chset);

/* emux_oss.c */
void snd_emux_init_seq_oss(snd_emux_t *emu);
void snd_emux_detach_seq_oss(snd_emux_t *emu);

/* emux_proc.c */
#ifdef CONFIG_PROC_FS
void snd_emux_proc_init(snd_emux_t *emu, snd_card_t *card, int device);
void snd_emux_proc_free(snd_emux_t *emu);
#endif

#define STATE_IS_PLAYING(s) ((s) & SNDRV_EMUX_ST_ON)

/* emux_hwdep.c */
int snd_emux_init_hwdep(snd_emux_t *emu);
void snd_emux_delete_hwdep(snd_emux_t *emu);

#endif
