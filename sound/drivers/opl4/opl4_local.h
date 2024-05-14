/*
 * Local definitions for the OPL4 driver
 *
 * Copyright (c) 2003 by Clemens Ladisch <clemens@ladisch.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed and/or modified under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __OPL4_LOCAL_H
#define __OPL4_LOCAL_H

#include <sound/opl4.h>

/*
 * Register numbers
 */

#define OPL4_REG_TEST0			0x00
#define OPL4_REG_TEST1			0x01

#define OPL4_REG_MEMORY_CONFIGURATION	0x02
#define   OPL4_MODE_BIT			0x01
#define   OPL4_MTYPE_BIT		0x02
#define   OPL4_TONE_HEADER_MASK		0x1c
#define   OPL4_DEVICE_ID_MASK		0xe0

#define OPL4_REG_MEMORY_ADDRESS_HIGH	0x03
#define OPL4_REG_MEMORY_ADDRESS_MID	0x04
#define OPL4_REG_MEMORY_ADDRESS_LOW	0x05
#define OPL4_REG_MEMORY_DATA		0x06

/*
 * Offsets to the register banks for voices. To get the
 * register number just add the voice number to the bank offset.
 *
 * Wave Table Number low bits (0x08 to 0x1F)
 */
#define OPL4_REG_TONE_NUMBER		0x08

/* Wave Table Number high bit, F-Number low bits (0x20 to 0x37) */
#define OPL4_REG_F_NUMBER		0x20
#define   OPL4_TONE_NUMBER_BIT8		0x01
#define   OPL4_F_NUMBER_LOW_MASK	0xfe

/* F-Number high bits, Octave, Pseudo-Reverb (0x38 to 0x4F) */
#define OPL4_REG_OCTAVE			0x38
#define   OPL4_F_NUMBER_HIGH_MASK	0x07
#define   OPL4_BLOCK_MASK		0xf0
#define   OPL4_PSEUDO_REVERB_BIT	0x08

/* Total Level, Level Direct (0x50 to 0x67) */
#define OPL4_REG_LEVEL			0x50
#define   OPL4_TOTAL_LEVEL_MASK		0xfe
#define   OPL4_LEVEL_DIRECT_BIT		0x01

/* Key On, Damp, LFO RST, CH, Panpot (0x68 to 0x7F) */
#define OPL4_REG_MISC			0x68
#define   OPL4_KEY_ON_BIT		0x80
#define   OPL4_DAMP_BIT			0x40
#define   OPL4_LFO_RESET_BIT		0x20
#define   OPL4_OUTPUT_CHANNEL_BIT	0x10
#define   OPL4_PAN_POT_MASK		0x0f

/* LFO, VIB (0x80 to 0x97) */
#define OPL4_REG_LFO_VIBRATO		0x80
#define   OPL4_LFO_FREQUENCY_MASK	0x38
#define   OPL4_VIBRATO_DEPTH_MASK	0x07
#define   OPL4_CHORUS_SEND_MASK		0xc0 /* ML only */

/* Attack / Decay 1 rate (0x98 to 0xAF) */
#define OPL4_REG_ATTACK_DECAY1		0x98
#define   OPL4_ATTACK_RATE_MASK		0xf0
#define   OPL4_DECAY1_RATE_MASK		0x0f

/* Decay level / 2 rate (0xB0 to 0xC7) */
#define OPL4_REG_LEVEL_DECAY2		0xb0
#define   OPL4_DECAY_LEVEL_MASK		0xf0
#define   OPL4_DECAY2_RATE_MASK		0x0f

/* Release rate / Rate correction (0xC8 to 0xDF) */
#define OPL4_REG_RELEASE_CORRECTION	0xc8
#define   OPL4_RELEASE_RATE_MASK	0x0f
#define   OPL4_RATE_INTERPOLATION_MASK	0xf0

/* AM (0xE0 to 0xF7) */
#define OPL4_REG_TREMOLO		0xe0
#define   OPL4_TREMOLO_DEPTH_MASK	0x07
#define   OPL4_REVERB_SEND_MASK		0xe0 /* ML only */

/* Mixer */
#define OPL4_REG_MIX_CONTROL_FM		0xf8
#define OPL4_REG_MIX_CONTROL_PCM	0xf9
#define   OPL4_MIX_LEFT_MASK		0x07
#define   OPL4_MIX_RIGHT_MASK		0x38

#define OPL4_REG_ATC			0xfa
#define   OPL4_ATC_BIT			0x01 /* ???, ML only */

/* bits in the OPL3 Status register */
#define OPL4_STATUS_BUSY		0x01
#define OPL4_STATUS_LOAD		0x02


#define OPL4_MAX_VOICES 24

#define SNDRV_SEQ_DEV_ID_OPL4 "opl4-synth"


struct opl4_sound {
	u16 tone;
	s16 pitch_offset;
	u8 key_scaling;
	s8 panpot;
	u8 vibrato;
	u8 tone_attenuate;
	u8 volume_factor;
	u8 reg_lfo_vibrato;
	u8 reg_attack_decay1;
	u8 reg_level_decay2;
	u8 reg_release_correction;
	u8 reg_tremolo;
};

struct opl4_region {
	u8 key_min, key_max;
	struct opl4_sound sound;
};

struct opl4_region_ptr {
	int count;
	const struct opl4_region *regions;
};

struct opl4_voice {
	struct list_head list;
	int number;
	struct snd_midi_channel *chan;
	int note;
	int velocity;
	const struct opl4_sound *sound;
	u8 level_direct;
	u8 reg_f_number;
	u8 reg_misc;
	u8 reg_lfo_vibrato;
};

struct snd_opl4 {
	unsigned long fm_port;
	unsigned long pcm_port;
	struct resource *res_fm_port;
	struct resource *res_pcm_port;
	unsigned short hardware;
	spinlock_t reg_lock;
	struct snd_card *card;

#ifdef CONFIG_SND_PROC_FS
	struct snd_info_entry *proc_entry;
	int memory_access;
#endif
	struct mutex access_mutex;

#if IS_ENABLED(CONFIG_SND_SEQUENCER)
	int used;

	int seq_dev_num;
	int seq_client;
	struct snd_seq_device *seq_dev;

	struct snd_midi_channel_set *chset;
	struct opl4_voice voices[OPL4_MAX_VOICES];
	struct list_head off_voices;
	struct list_head on_voices;
#endif
};

/* opl4_lib.c */
void snd_opl4_write(struct snd_opl4 *opl4, u8 reg, u8 value);
u8 snd_opl4_read(struct snd_opl4 *opl4, u8 reg);
void snd_opl4_read_memory(struct snd_opl4 *opl4, char *buf, int offset, int size);
void snd_opl4_write_memory(struct snd_opl4 *opl4, const char *buf, int offset, int size);

/* opl4_mixer.c */
int snd_opl4_create_mixer(struct snd_opl4 *opl4);

#ifdef CONFIG_SND_PROC_FS
/* opl4_proc.c */
int snd_opl4_create_proc(struct snd_opl4 *opl4);
void snd_opl4_free_proc(struct snd_opl4 *opl4);
#else
static inline int snd_opl4_create_proc(struct snd_opl4 *opl4) { return 0; }
static inline void snd_opl4_free_proc(struct snd_opl4 *opl4) {}
#endif

/* opl4_seq.c */
extern int volume_boost;

/* opl4_synth.c */
void snd_opl4_synth_reset(struct snd_opl4 *opl4);
void snd_opl4_synth_shutdown(struct snd_opl4 *opl4);
void snd_opl4_note_on(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_opl4_note_off(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_opl4_terminate_note(void *p, int note, struct snd_midi_channel *chan);
void snd_opl4_control(void *p, int type, struct snd_midi_channel *chan);
void snd_opl4_sysex(void *p, unsigned char *buf, int len, int parsed, struct snd_midi_channel_set *chset);

/* yrw801.c */
int snd_yrw801_detect(struct snd_opl4 *opl4);
extern const struct opl4_region_ptr snd_yrw801_regions[];

#endif /* __OPL4_LOCAL_H */
