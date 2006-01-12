/*
 *  Advanced Linux Sound Architecture
 *
 *  GF1 (GUS) Patch Instrument Format
 *  Copyright (c) 1994-99 by Jaroslav Kysela <perex@suse.cz>
 *
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

#ifndef __SOUND_AINSTR_GF1_H
#define __SOUND_AINSTR_GF1_H

#ifndef __KERNEL__
#include <asm/types.h>
#include <asm/byteorder.h>
#endif

/*
 *  share types (share ID 1)
 */

#define GF1_SHARE_FILE			0

/*
 *  wave formats
 */

#define GF1_WAVE_16BIT			0x0001  /* 16-bit wave */
#define GF1_WAVE_UNSIGNED		0x0002  /* unsigned wave */
#define GF1_WAVE_INVERT			0x0002  /* same as unsigned wave */
#define GF1_WAVE_BACKWARD		0x0004  /* backward mode (maybe used for reverb or ping-ping loop) */
#define GF1_WAVE_LOOP			0x0008  /* loop mode */
#define GF1_WAVE_BIDIR			0x0010  /* bidirectional mode */
#define GF1_WAVE_STEREO			0x0100	/* stereo mode */
#define GF1_WAVE_ULAW			0x0200	/* uLaw compression mode */

/*
 *  Wavetable definitions
 */

struct gf1_wave {
	unsigned int share_id[4];	/* share id - zero = no sharing */
	unsigned int format;		/* wave format */

	struct {
		unsigned int number;	/* some other ID for this instrument */
		unsigned int memory;	/* begin of waveform in onboard memory */
		unsigned char *ptr;	/* pointer to waveform in system memory */
	} address;

	unsigned int size;		/* size of waveform in samples */
	unsigned int start;		/* start offset in samples * 16 (lowest 4 bits - fraction) */
	unsigned int loop_start;	/* bits loop start offset in samples * 16 (lowest 4 bits - fraction) */
	unsigned int loop_end;		/* loop start offset in samples * 16 (lowest 4 bits - fraction) */
	unsigned short loop_repeat;	/* loop repeat - 0 = forever */

	unsigned char flags;		/* GF1 patch flags */
	unsigned char pad;
	unsigned int sample_rate;	/* sample rate in Hz */
	unsigned int low_frequency;	/* low frequency range */
	unsigned int high_frequency;	/* high frequency range */
	unsigned int root_frequency;	/* root frequency range */
	signed short tune;
	unsigned char balance;
	unsigned char envelope_rate[6];
	unsigned char envelope_offset[6];
	unsigned char tremolo_sweep;
	unsigned char tremolo_rate;
	unsigned char tremolo_depth;
	unsigned char vibrato_sweep;
	unsigned char vibrato_rate;
	unsigned char vibrato_depth;
	unsigned short scale_frequency;
	unsigned short scale_factor;	/* 0-2048 or 0-2 */
  
	struct gf1_wave *next;
};

/*
 *  Instrument
 */

#define IWFFFF_EXCLUDE_NONE		0x0000	/* exclusion mode - none */
#define IWFFFF_EXCLUDE_SINGLE		0x0001	/* exclude single - single note from the instrument group */
#define IWFFFF_EXCLUDE_MULTIPLE		0x0002	/* exclude multiple - stop only same note from this instrument */

#define IWFFFF_EFFECT_NONE		0
#define IWFFFF_EFFECT_REVERB		1
#define IWFFFF_EFFECT_CHORUS		2
#define IWFFFF_EFFECT_ECHO		3

struct gf1_instrument {
	unsigned short exclusion;
	unsigned short exclusion_group;	/* 0 - none, 1-65535 */

	unsigned char effect1;		/* effect 1 */
	unsigned char effect1_depth;	/* 0-127 */
	unsigned char effect2;		/* effect 2 */
	unsigned char effect2_depth;	/* 0-127 */

	struct gf1_wave *wave;		/* first waveform */
};

/*
 *
 *    Kernel <-> user space
 *    Hardware (CPU) independent section
 *
 *    * = zero or more
 *    + = one or more
 *
 *    gf1_xinstrument		IWFFFF_STRU_INSTR
 *      +gf1_xwave		IWFFFF_STRU_WAVE
 *
 */

#define GF1_STRU_WAVE		__cpu_to_be32(('W'<<24)|('A'<<16)|('V'<<8)|'E')
#define GF1_STRU_INSTR		__cpu_to_be32(('I'<<24)|('N'<<16)|('S'<<8)|'T')

/*
 *  Wavetable definitions
 */

struct gf1_xwave {
	__u32 stype;			/* structure type */

	__u32 share_id[4];		/* share id - zero = no sharing */
	__u32 format;			/* wave format */

	__u32 size;			/* size of waveform in samples */
	__u32 start;			/* start offset in samples * 16 (lowest 4 bits - fraction) */
	__u32 loop_start;		/* bits loop start offset in samples * 16 (lowest 4 bits - fraction) */
	__u32 loop_end;			/* loop start offset in samples * 16 (lowest 4 bits - fraction) */
	__u16 loop_repeat;		/* loop repeat - 0 = forever */

	__u8 flags;			/* GF1 patch flags */
	__u8 pad;
	__u32 sample_rate;		/* sample rate in Hz */
	__u32 low_frequency;		/* low frequency range */
	__u32 high_frequency;		/* high frequency range */
	__u32 root_frequency;		/* root frequency range */
	__s16 tune;
	__u8 balance;
	__u8 envelope_rate[6];
	__u8 envelope_offset[6];
	__u8 tremolo_sweep;
	__u8 tremolo_rate;
	__u8 tremolo_depth;
	__u8 vibrato_sweep;
	__u8 vibrato_rate;
	__u8 vibrato_depth;
	__u16 scale_frequency;
	__u16 scale_factor;		/* 0-2048 or 0-2 */  
};

/*
 *  Instrument
 */

struct gf1_xinstrument {
	__u32 stype;
	
	__u16 exclusion;
	__u16 exclusion_group;		/* 0 - none, 1-65535 */

	__u8 effect1;			/* effect 1 */
	__u8 effect1_depth;		/* 0-127 */
	__u8 effect2;			/* effect 2 */
	__u8 effect2_depth;		/* 0-127 */
};

/*
 *  Instrument info
 */

#define GF1_INFO_ENVELOPE		(1<<0)
#define GF1_INFO_TREMOLO		(1<<1)
#define GF1_INFO_VIBRATO		(1<<2)

struct gf1_info {
	unsigned char flags;		/* supported wave flags */
	unsigned char pad[3];
	unsigned int features;		/* supported features */
	unsigned int max8_len;		/* maximum 8-bit wave length */
	unsigned int max16_len;		/* maximum 16-bit wave length */
};

#ifdef __KERNEL__

#include "seq_instr.h"

struct snd_gf1_ops {
	void *private_data;
	int (*info)(void *private_data, struct gf1_info *info);
	int (*put_sample)(void *private_data, struct gf1_wave *wave,
	                  char __user *data, long len, int atomic);
	int (*get_sample)(void *private_data, struct gf1_wave *wave,
			  char __user *data, long len, int atomic);
	int (*remove_sample)(void *private_data, struct gf1_wave *wave,
			     int atomic);
	void (*notify)(void *private_data, struct snd_seq_kinstr *instr, int what);
	struct snd_seq_kinstr_ops kops;
};

int snd_seq_gf1_init(struct snd_gf1_ops *ops,
		     void *private_data,
		     struct snd_seq_kinstr_ops *next);

#endif

/* typedefs for compatibility to user-space */
typedef struct gf1_xwave gf1_xwave_t;
typedef struct gf1_xinstrument gf1_xinstrument_t;

#endif /* __SOUND_AINSTR_GF1_H */
