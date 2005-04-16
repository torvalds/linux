#ifndef __SOUND_EMUX_SYNTH_H
#define __SOUND_EMUX_SYNTH_H

/*
 *  Defines for the Emu-series WaveTable chip
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

#include "seq_kernel.h"
#include "seq_device.h"
#include "soundfont.h"
#include "seq_midi_emul.h"
#ifdef CONFIG_SND_SEQUENCER_OSS
#include "seq_oss.h"
#endif
#include "emux_legacy.h"
#include "seq_virmidi.h"

/*
 * compile flags
 */
#define SNDRV_EMUX_USE_RAW_EFFECT


/*
 * typedefs
 */
typedef struct snd_emux_effect_table snd_emux_effect_table_t;
typedef struct snd_emux_port snd_emux_port_t;
typedef struct snd_emux_voice snd_emux_voice_t;
typedef struct snd_emux snd_emux_t;


/*
 * operators
 */
typedef struct snd_emux_operators {
	struct module *owner;
	snd_emux_voice_t *(*get_voice)(snd_emux_t *emu, snd_emux_port_t *port);
	int (*prepare)(snd_emux_voice_t *vp);
	void (*trigger)(snd_emux_voice_t *vp);
	void (*release)(snd_emux_voice_t *vp);
	void (*update)(snd_emux_voice_t *vp, int update);
	void (*terminate)(snd_emux_voice_t *vp);
	void (*free_voice)(snd_emux_voice_t *vp);
	void (*reset)(snd_emux_t *emu, int ch);
	/* the first parameters are snd_emux_t */
	int (*sample_new)(snd_emux_t *emu, snd_sf_sample_t *sp, snd_util_memhdr_t *hdr, const void __user *data, long count);
	int (*sample_free)(snd_emux_t *emu, snd_sf_sample_t *sp, snd_util_memhdr_t *hdr);
	void (*sample_reset)(snd_emux_t *emu);
	int (*load_fx)(snd_emux_t *emu, int type, int arg, const void __user *data, long count);
	void (*sysex)(snd_emux_t *emu, char *buf, int len, int parsed, snd_midi_channel_set_t *chset);
#ifdef CONFIG_SND_SEQUENCER_OSS
	int (*oss_ioctl)(snd_emux_t *emu, int cmd, int p1, int p2);
#endif
} snd_emux_operators_t;


/*
 * constant values
 */
#define SNDRV_EMUX_MAX_PORTS		32	/* max # of sequencer ports */
#define SNDRV_EMUX_MAX_VOICES		64	/* max # of voices */
#define SNDRV_EMUX_MAX_MULTI_VOICES	16	/* max # of playable voices
						 * simultineously
						 */

/*
 * flags
 */
#define SNDRV_EMUX_ACCEPT_ROM		(1<<0)

/*
 * emuX wavetable
 */
struct snd_emux {

	snd_card_t *card;	/* assigned card */

	/* following should be initialized before registration */
	int max_voices;		/* Number of voices */
	int mem_size;		/* memory size (in byte) */
	int num_ports;		/* number of ports to be created */
	int pitch_shift;	/* pitch shift value (for Emu10k1) */
	snd_emux_operators_t ops;	/* operators */
	void *hw;		/* hardware */
	unsigned long flags;	/* other conditions */
	int midi_ports;		/* number of virtual midi devices */
	int midi_devidx;	/* device offset of virtual midi */
	unsigned int linear_panning: 1; /* panning is linear (sbawe = 1, emu10k1 = 0) */
	int hwdep_idx;		/* hwdep device index */
	snd_hwdep_t *hwdep;	/* hwdep device */

	/* private */
	int num_voices;		/* current number of voices */
	snd_sf_list_t *sflist;	/* root of SoundFont list */
	snd_emux_voice_t *voices;	/* Voices (EMU 'channel') */
	int use_time;	/* allocation counter */
	spinlock_t voice_lock;	/* Lock for voice access */
	struct semaphore register_mutex;
	int client;		/* For the sequencer client */
	int ports[SNDRV_EMUX_MAX_PORTS];	/* The ports for this device */
	snd_emux_port_t *portptrs[SNDRV_EMUX_MAX_PORTS];
	int used;	/* use counter */
	char *name;	/* name of the device (internal) */
	snd_rawmidi_t **vmidi;
	struct timer_list tlist;	/* for pending note-offs */
	int timer_active;

	snd_util_memhdr_t *memhdr;	/* memory chunk information */

#ifdef CONFIG_PROC_FS
	snd_info_entry_t *proc;
#endif

#ifdef CONFIG_SND_SEQUENCER_OSS
	snd_seq_device_t *oss_synth;
#endif
};


/*
 * sequencer port information
 */
struct snd_emux_port {

	snd_midi_channel_set_t chset;
	snd_emux_t *emu;

	char port_mode;			/* operation mode */
	int volume_atten;		/* emuX raw attenuation */
	unsigned long drum_flags;	/* drum bitmaps */
	int ctrls[EMUX_MD_END];		/* control parameters */
#ifdef SNDRV_EMUX_USE_RAW_EFFECT
	snd_emux_effect_table_t *effect;
#endif
#ifdef CONFIG_SND_SEQUENCER_OSS
	snd_seq_oss_arg_t *oss_arg;
#endif
};

/* port_mode */
#define SNDRV_EMUX_PORT_MODE_MIDI		0	/* normal MIDI port */
#define SNDRV_EMUX_PORT_MODE_OSS_SYNTH	1	/* OSS synth port */
#define SNDRV_EMUX_PORT_MODE_OSS_MIDI	2	/* OSS multi channel synth port */

/*
 * A structure to keep track of each hardware voice
 */
struct snd_emux_voice {
	int  ch;		/* Hardware channel number */

	int  state;		/* status */
#define SNDRV_EMUX_ST_OFF		0x00	/* Not playing, and inactive */
#define SNDRV_EMUX_ST_ON		0x01	/* Note on */
#define SNDRV_EMUX_ST_RELEASED 	(0x02|SNDRV_EMUX_ST_ON)	/* Note released */
#define SNDRV_EMUX_ST_SUSTAINED	(0x04|SNDRV_EMUX_ST_ON)	/* Note sustained */
#define SNDRV_EMUX_ST_STANDBY	(0x08|SNDRV_EMUX_ST_ON)	/* Waiting to be triggered */
#define SNDRV_EMUX_ST_PENDING 	(0x10|SNDRV_EMUX_ST_ON)	/* Note will be released */
#define SNDRV_EMUX_ST_LOCKED		0x100	/* Not accessible */

	unsigned int  time;	/* An allocation time */
	unsigned char note;	/* Note currently assigned to this voice */
	unsigned char key;
	unsigned char velocity;	/* Velocity of current note */

	snd_sf_zone_t *zone;	/* Zone assigned to this note */
	void *block;		/* sample block pointer (optional) */
	snd_midi_channel_t *chan;	/* Midi channel for this note */
	snd_emux_port_t *port;	/* associated port */
	snd_emux_t *emu;	/* assigned root info */
	void *hw;		/* hardware pointer (emu8000_t or emu10k1_t) */
	unsigned long ontime;	/* jiffies at note triggered */
	
	/* Emu8k/Emu10k1 registers */
	soundfont_voice_info_t reg;

	/* additional registers */
	int avol;		/* volume attenuation */
	int acutoff;		/* cutoff target */
	int apitch;		/* pitch offset */
	int apan;		/* pan/aux pair */
	int aaux;
	int ptarget;		/* pitch target */
	int vtarget;		/* volume target */
	int ftarget;		/* filter target */

};

/*
 * update flags (can be combined)
 */
#define SNDRV_EMUX_UPDATE_VOLUME		(1<<0)
#define SNDRV_EMUX_UPDATE_PITCH		(1<<1)
#define SNDRV_EMUX_UPDATE_PAN		(1<<2)
#define SNDRV_EMUX_UPDATE_FMMOD		(1<<3)
#define SNDRV_EMUX_UPDATE_TREMFREQ	(1<<4)
#define SNDRV_EMUX_UPDATE_FM2FRQ2		(1<<5)
#define SNDRV_EMUX_UPDATE_Q		(1<<6)


#ifdef SNDRV_EMUX_USE_RAW_EFFECT
/*
 * effect table
 */
struct snd_emux_effect_table {
	/* Emu8000 specific effects */
	short val[EMUX_NUM_EFFECTS];
	unsigned char flag[EMUX_NUM_EFFECTS];
};
#endif /* SNDRV_EMUX_USE_RAW_EFFECT */


/*
 * prototypes - interface to Emu10k1 and Emu8k routines
 */
int snd_emux_new(snd_emux_t **remu);
int snd_emux_register(snd_emux_t *emu, snd_card_t *card, int index, char *name);
int snd_emux_free(snd_emux_t *emu);

/*
 * exported functions
 */
void snd_emux_terminate_all(snd_emux_t *emu);
void snd_emux_lock_voice(snd_emux_t *emu, int voice);
void snd_emux_unlock_voice(snd_emux_t *emu, int voice);

#endif /* __SOUND_EMUX_SYNTH_H */
