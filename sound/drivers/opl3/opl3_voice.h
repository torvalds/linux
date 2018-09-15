#ifndef __OPL3_VOICE_H
#define __OPL3_VOICE_H

/*
 *  Copyright (c) 2000 Uros Bizjak <uros@kss-loka.si>
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

#include <sound/opl3.h>

/* Prototypes for opl3_seq.c */
int snd_opl3_synth_use_inc(struct snd_opl3 * opl3);
void snd_opl3_synth_use_dec(struct snd_opl3 * opl3);
int snd_opl3_synth_setup(struct snd_opl3 * opl3);
void snd_opl3_synth_cleanup(struct snd_opl3 * opl3);

/* Prototypes for opl3_midi.c */
void snd_opl3_note_on(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_opl3_note_off(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_opl3_key_press(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_opl3_terminate_note(void *p, int note, struct snd_midi_channel *chan);
void snd_opl3_control(void *p, int type, struct snd_midi_channel *chan);
void snd_opl3_nrpn(void *p, struct snd_midi_channel *chan, struct snd_midi_channel_set *chset);
void snd_opl3_sysex(void *p, unsigned char *buf, int len, int parsed, struct snd_midi_channel_set *chset);

void snd_opl3_calc_volume(unsigned char *reg, int vel, struct snd_midi_channel *chan);
void snd_opl3_timer_func(struct timer_list *t);

/* Prototypes for opl3_drums.c */
void snd_opl3_load_drums(struct snd_opl3 *opl3);
void snd_opl3_drum_switch(struct snd_opl3 *opl3, int note, int on_off, int vel, struct snd_midi_channel *chan);

/* Prototypes for opl3_oss.c */
#if IS_ENABLED(CONFIG_SND_SEQUENCER_OSS)
void snd_opl3_init_seq_oss(struct snd_opl3 *opl3, char *name);
void snd_opl3_free_seq_oss(struct snd_opl3 *opl3);
#else
#define snd_opl3_init_seq_oss(opl3, name) /* NOP */
#define snd_opl3_free_seq_oss(opl3) /* NOP */
#endif

extern char snd_opl3_regmap[MAX_OPL2_VOICES][4];
extern bool use_internal_drums;
extern struct snd_midi_op opl3_ops;

#endif
