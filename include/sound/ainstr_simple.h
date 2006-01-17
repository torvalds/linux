/*
 *  Advanced Linux Sound Architecture
 *
 *  Simple (MOD player) Instrument Format
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

#ifndef __SOUND_AINSTR_SIMPLE_H
#define __SOUND_AINSTR_SIMPLE_H

#ifndef __KERNEL__
#include <asm/types.h>
#include <asm/byteorder.h>
#endif

/*
 *  share types (share ID 1)
 */

#define SIMPLE_SHARE_FILE		0

/*
 *  wave formats
 */

#define SIMPLE_WAVE_16BIT		0x0001  /* 16-bit wave */
#define SIMPLE_WAVE_UNSIGNED		0x0002  /* unsigned wave */
#define SIMPLE_WAVE_INVERT		0x0002  /* same as unsigned wave */
#define SIMPLE_WAVE_BACKWARD		0x0004  /* backward mode (maybe used for reverb or ping-ping loop) */
#define SIMPLE_WAVE_LOOP		0x0008  /* loop mode */
#define SIMPLE_WAVE_BIDIR		0x0010  /* bidirectional mode */
#define SIMPLE_WAVE_STEREO		0x0100	/* stereo wave */
#define SIMPLE_WAVE_ULAW		0x0200	/* uLaw compression mode */

/*
 *  instrument effects
 */

#define SIMPLE_EFFECT_NONE		0
#define SIMPLE_EFFECT_REVERB		1
#define SIMPLE_EFFECT_CHORUS		2
#define SIMPLE_EFFECT_ECHO		3

/*
 *  instrument info
 */

struct simple_instrument_info {
	unsigned int format;		/* supported format bits */
	unsigned int effects;		/* supported effects (1 << SIMPLE_EFFECT_*) */
	unsigned int max8_len;		/* maximum 8-bit wave length */
	unsigned int max16_len;		/* maximum 16-bit wave length */
};

/*
 *  Instrument
 */

struct simple_instrument {
	unsigned int share_id[4];	/* share id - zero = no sharing */
	unsigned int format;		/* wave format */

	struct {
		unsigned int number;	/* some other ID for this instrument */
		unsigned int memory;	/* begin of waveform in onboard memory */
		unsigned char *ptr;	/* pointer to waveform in system memory */
	} address;

	unsigned int size;		/* size of waveform in samples */
	unsigned int start;		/* start offset in samples * 16 (lowest 4 bits - fraction) */
	unsigned int loop_start;	/* loop start offset in samples * 16 (lowest 4 bits - fraction) */
	unsigned int loop_end;		/* loop end offset in samples * 16 (lowest 4 bits - fraction) */
	unsigned short loop_repeat;	/* loop repeat - 0 = forever */

	unsigned char effect1;		/* effect 1 */
	unsigned char effect1_depth;	/* 0-127 */
	unsigned char effect2;		/* effect 2 */
	unsigned char effect2_depth;	/* 0-127 */
};

/*
 *
 *    Kernel <-> user space
 *    Hardware (CPU) independent section
 *
 *    * = zero or more
 *    + = one or more
 *
 *    simple_xinstrument	SIMPLE_STRU_INSTR
 *
 */

#define SIMPLE_STRU_INSTR	__cpu_to_be32(('I'<<24)|('N'<<16)|('S'<<8)|'T')

/*
 *  Instrument
 */

struct simple_xinstrument {
	__u32 stype;

	__u32 share_id[4];		/* share id - zero = no sharing */
	__u32 format;			/* wave format */

	__u32 size;			/* size of waveform in samples */
	__u32 start;			/* start offset in samples * 16 (lowest 4 bits - fraction) */
	__u32 loop_start;		/* bits loop start offset in samples * 16 (lowest 4 bits - fraction) */
	__u32 loop_end;			/* loop start offset in samples * 16 (lowest 4 bits - fraction) */
	__u16 loop_repeat;		/* loop repeat - 0 = forever */
	
	__u8 effect1;			/* effect 1 */
	__u8 effect1_depth;		/* 0-127 */
	__u8 effect2;			/* effect 2 */
	__u8 effect2_depth;		/* 0-127 */
};

#ifdef __KERNEL__

#include "seq_instr.h"

struct snd_simple_ops {
	void *private_data;
	int (*info)(void *private_data, struct simple_instrument_info *info);
	int (*put_sample)(void *private_data, struct simple_instrument *instr,
	                  char __user *data, long len, int atomic);
	int (*get_sample)(void *private_data, struct simple_instrument *instr,
			  char __user *data, long len, int atomic);
	int (*remove_sample)(void *private_data, struct simple_instrument *instr,
			     int atomic);
	void (*notify)(void *private_data, struct snd_seq_kinstr *instr, int what);
	struct snd_seq_kinstr_ops kops;
};

int snd_seq_simple_init(struct snd_simple_ops *ops,
			void *private_data,
			struct snd_seq_kinstr_ops *next);

#endif

/* typedefs for compatibility to user-space */
typedef struct simple_xinstrument simple_xinstrument_t;

#endif /* __SOUND_AINSTR_SIMPLE_H */
