/*
 *  Advanced Linux Sound Architecture
 *
 *  FM (OPL2/3) Instrument Format
 *  Copyright (c) 2000 Uros Bizjak <uros@kss-loka.si>
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

#ifndef __SOUND_AINSTR_FM_H
#define __SOUND_AINSTR_FM_H

#ifndef __KERNEL__
#include <asm/types.h>
#include <asm/byteorder.h>
#endif

/*
 *  share types (share ID 1)
 */

#define FM_SHARE_FILE		0

/*
 * FM operator
 */

struct fm_operator {
	unsigned char am_vib;
	unsigned char ksl_level;
	unsigned char attack_decay;
	unsigned char sustain_release;
	unsigned char wave_select;
};

/*
 *  Instrument
 */

#define FM_PATCH_OPL2	0x01		/* OPL2 2 operators FM instrument */
#define FM_PATCH_OPL3	0x02		/* OPL3 4 operators FM instrument */

struct fm_instrument {
	unsigned int share_id[4];	/* share id - zero = no sharing */
	unsigned char type;		/* instrument type */

	struct fm_operator op[4];
	unsigned char feedback_connection[2];

	unsigned char echo_delay;
	unsigned char echo_atten;
	unsigned char chorus_spread;
	unsigned char trnsps;
	unsigned char fix_dur;
	unsigned char modes;
	unsigned char fix_key;
};

/*
 *
 *    Kernel <-> user space
 *    Hardware (CPU) independent section
 *
 *    * = zero or more
 *    + = one or more
 *
 *    fm_xinstrument	FM_STRU_INSTR
 *
 */

#define FM_STRU_INSTR	__cpu_to_be32(('I'<<24)|('N'<<16)|('S'<<8)|'T')

/*
 * FM operator
 */

struct fm_xoperator {
	__u8 am_vib;
	__u8 ksl_level;
	__u8 attack_decay;
	__u8 sustain_release;
	__u8 wave_select;
};

/*
 *  Instrument
 */

struct fm_xinstrument {
	__u32 stype;			/* structure type */

	__u32 share_id[4];		/* share id - zero = no sharing */
	__u8 type;			/* instrument type */

	struct fm_xoperator op[4];		/* fm operators */
	__u8 feedback_connection[2];

	__u8 echo_delay;
	__u8 echo_atten;
	__u8 chorus_spread;
	__u8 trnsps;
	__u8 fix_dur;
	__u8 modes;
	__u8 fix_key;
};

#ifdef __KERNEL__

#include "seq_instr.h"

int snd_seq_fm_init(struct snd_seq_kinstr_ops * ops,
		    struct snd_seq_kinstr_ops * next);

#endif

/* typedefs for compatibility to user-space */
typedef struct fm_xoperator fm_xoperator_t;
typedef struct fm_xinstrument fm_xinstrument_t;

#endif	/* __SOUND_AINSTR_FM_H */
