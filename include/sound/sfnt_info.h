#ifndef __SOUND_SFNT_INFO_H
#define __SOUND_SFNT_INFO_H

/*
 *  Patch record compatible with AWE driver on OSS
 *
 *  Copyright (C) 1999-2000 Takashi Iwai
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
 *
 */

#include <sound/asound.h>

/*
 * patch information record
 */

#ifdef SNDRV_BIG_ENDIAN
#define SNDRV_OSS_PATCHKEY(id) (0xfd00|id)
#else
#define SNDRV_OSS_PATCHKEY(id) ((id<<8)|0xfd)
#endif

/* patch interface header: 16 bytes */
typedef struct soundfont_patch_info_t {
	unsigned short key;		/* use the key below */
#define SNDRV_OSS_SOUNDFONT_PATCH		SNDRV_OSS_PATCHKEY(0x07)

	short device_no;		/* synthesizer number */
	unsigned short sf_id;		/* file id (should be zero) */
	short optarg;			/* optional argument */
	int len;			/* data length (without this header) */

	short type;			/* patch operation type */
#define SNDRV_SFNT_LOAD_INFO		0	/* awe_voice_rec */
#define SNDRV_SFNT_LOAD_DATA		1	/* awe_sample_info */
#define SNDRV_SFNT_OPEN_PATCH	2	/* awe_open_parm */
#define SNDRV_SFNT_CLOSE_PATCH	3	/* none */
	/* 4 is obsolete */
#define SNDRV_SFNT_REPLACE_DATA	5	/* awe_sample_info (optarg=#channels)*/
#define SNDRV_SFNT_MAP_PRESET	6	/* awe_voice_map */
	/* 7 is not used */
#define SNDRV_SFNT_PROBE_DATA		8	/* optarg=sample */
#define SNDRV_SFNT_REMOVE_INFO		9	/* optarg=(bank<<8)|instr */

	short reserved;			/* word alignment data */

	/* the actual patch data begins after this */
} soundfont_patch_info_t;


/*
 * open patch
 */

#define SNDRV_SFNT_PATCH_NAME_LEN	32

typedef struct soundfont_open_parm_t {
	unsigned short type;		/* sample type */
#define SNDRV_SFNT_PAT_TYPE_MISC	0
#define SNDRV_SFNT_PAT_TYPE_GUS	6
#define SNDRV_SFNT_PAT_TYPE_MAP	7
#define SNDRV_SFNT_PAT_LOCKED	0x100	/* lock the samples */
#define SNDRV_SFNT_PAT_SHARED	0x200	/* sample is shared */

	short reserved;
	char name[SNDRV_SFNT_PATCH_NAME_LEN];
} soundfont_open_parm_t;


/*
 * raw voice information record
 */

/* wave table envelope & effect parameters to control EMU8000 */
typedef struct soundfont_voice_parm_t {
	unsigned short moddelay;	/* modulation delay (0x8000) */
	unsigned short modatkhld;	/* modulation attack & hold time (0x7f7f) */
	unsigned short moddcysus;	/* modulation decay & sustain (0x7f7f) */
	unsigned short modrelease;	/* modulation release time (0x807f) */
	short modkeyhold, modkeydecay;	/* envelope change per key (not used) */
	unsigned short voldelay;	/* volume delay (0x8000) */
	unsigned short volatkhld;	/* volume attack & hold time (0x7f7f) */
	unsigned short voldcysus;	/* volume decay & sustain (0x7f7f) */
	unsigned short volrelease;	/* volume release time (0x807f) */
	short volkeyhold, volkeydecay;	/* envelope change per key (not used) */
	unsigned short lfo1delay;	/* LFO1 delay (0x8000) */
	unsigned short lfo2delay;	/* LFO2 delay (0x8000) */
	unsigned short pefe;		/* modulation pitch & cutoff (0x0000) */
	unsigned short fmmod;		/* LFO1 pitch & cutoff (0x0000) */
	unsigned short tremfrq;		/* LFO1 volume & freq (0x0000) */
	unsigned short fm2frq2;		/* LFO2 pitch & freq (0x0000) */
	unsigned char cutoff;		/* initial cutoff (0xff) */
	unsigned char filterQ;		/* initial filter Q [0-15] (0x0) */
	unsigned char chorus;		/* chorus send (0x00) */
	unsigned char reverb;		/* reverb send (0x00) */
	unsigned short reserved[4];	/* not used */
} soundfont_voice_parm_t;


/* wave table parameters: 92 bytes */
typedef struct soundfont_voice_info_t {
	unsigned short sf_id;		/* file id (should be zero) */
	unsigned short sample;		/* sample id */
	int start, end;			/* sample offset correction */
	int loopstart, loopend;		/* loop offset correction */
	short rate_offset;		/* sample rate pitch offset */
	unsigned short mode;		/* sample mode */
#define SNDRV_SFNT_MODE_ROMSOUND		0x8000
#define SNDRV_SFNT_MODE_STEREO		1
#define SNDRV_SFNT_MODE_LOOPING		2
#define SNDRV_SFNT_MODE_NORELEASE		4	/* obsolete */
#define SNDRV_SFNT_MODE_INIT_PARM		8

	short root;			/* midi root key */
	short tune;			/* pitch tuning (in cents) */
	unsigned char low, high;	/* key note range */
	unsigned char vellow, velhigh;	/* velocity range */
	signed char fixkey, fixvel;	/* fixed key, velocity */
	signed char pan, fixpan;	/* panning, fixed panning */
	short exclusiveClass;		/* exclusive class (0 = none) */
	unsigned char amplitude;	/* sample volume (127 max) */
	unsigned char attenuation;	/* attenuation (0.375dB) */
	short scaleTuning;		/* pitch scale tuning(%), normally 100 */
	soundfont_voice_parm_t parm;	/* voice envelope parameters */
	unsigned short sample_mode;	/* sample mode_flag (set by driver) */
} soundfont_voice_info_t;


/* instrument info header: 4 bytes */
typedef struct soundfont_voice_rec_hdr_t {
	unsigned char bank;		/* midi bank number */
	unsigned char instr;		/* midi preset number */
	char nvoices;			/* number of voices */
	char write_mode;		/* write mode; normally 0 */
#define SNDRV_SFNT_WR_APPEND		0	/* append anyway */
#define SNDRV_SFNT_WR_EXCLUSIVE		1	/* skip if already exists */
#define SNDRV_SFNT_WR_REPLACE		2	/* replace if already exists */
} soundfont_voice_rec_hdr_t;


/*
 * sample wave information
 */

/* wave table sample header: 32 bytes */
typedef struct soundfont_sample_info_t {
	unsigned short sf_id;		/* file id (should be zero) */
	unsigned short sample;		/* sample id */
	int start, end;			/* start & end offset */
	int loopstart, loopend;		/* loop start & end offset */
	int size;			/* size (0 = ROM) */
	short dummy;			/* not used */
	unsigned short mode_flags;	/* mode flags */
#define SNDRV_SFNT_SAMPLE_8BITS		1	/* wave data is 8bits */
#define SNDRV_SFNT_SAMPLE_UNSIGNED	2	/* wave data is unsigned */
#define SNDRV_SFNT_SAMPLE_NO_BLANK	4	/* no blank loop is attached */
#define SNDRV_SFNT_SAMPLE_SINGLESHOT	8	/* single-shot w/o loop */
#define SNDRV_SFNT_SAMPLE_BIDIR_LOOP	16	/* bidirectional looping */
#define SNDRV_SFNT_SAMPLE_STEREO_LEFT	32	/* stereo left sound */
#define SNDRV_SFNT_SAMPLE_STEREO_RIGHT	64	/* stereo right sound */
#define SNDRV_SFNT_SAMPLE_REVERSE_LOOP	128	/* reverse looping */
	unsigned int truesize;		/* used memory size (set by driver) */
} soundfont_sample_info_t;


/*
 * voice preset mapping (aliasing)
 */

typedef struct soundfont_voice_map_t {
	int map_bank, map_instr, map_key;	/* key = -1 means all keys */
	int src_bank, src_instr, src_key;
} soundfont_voice_map_t;


/*
 * ioctls for hwdep
 */

#define SNDRV_EMUX_HWDEP_NAME	"Emux WaveTable"

#define SNDRV_EMUX_VERSION	((1 << 16) | (0 << 8) | 0)	/* 1.0.0 */

struct sndrv_emux_misc_mode {
	int port;	/* -1 = all */
	int mode;
	int value;
	int value2;	/* reserved */
};

enum {
	SNDRV_EMUX_IOCTL_VERSION = _IOR('H', 0x80, unsigned int),
	SNDRV_EMUX_IOCTL_LOAD_PATCH = _IOWR('H', 0x81, soundfont_patch_info_t),
	SNDRV_EMUX_IOCTL_RESET_SAMPLES = _IO('H', 0x82),
	SNDRV_EMUX_IOCTL_REMOVE_LAST_SAMPLES = _IO('H', 0x83),
	SNDRV_EMUX_IOCTL_MEM_AVAIL = _IOW('H', 0x84, int),
	SNDRV_EMUX_IOCTL_MISC_MODE = _IOWR('H', 0x84, struct sndrv_emux_misc_mode),
};

#endif /* __SOUND_SFNT_INFO_H */
