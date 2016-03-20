#ifndef __SOUND_TLV_H
#define __SOUND_TLV_H

/*
 *  Advanced Linux Sound Architecture - ALSA - Driver
 *  Copyright (c) 2006 by Jaroslav Kysela <perex@perex.cz>
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

/*
 * TLV structure is right behind the struct snd_ctl_tlv:
 *   unsigned int type  	- see SNDRV_CTL_TLVT_*
 *   unsigned int length
 *   .... data aligned to sizeof(unsigned int), use
 *        block_length = (length + (sizeof(unsigned int) - 1)) &
 *                       ~(sizeof(unsigned int) - 1)) ....
 */

#include <uapi/sound/tlv.h>

#define TLV_ITEM(type, ...) \
	(type), TLV_LENGTH(__VA_ARGS__), __VA_ARGS__
#define TLV_LENGTH(...) \
	((unsigned int)sizeof((const unsigned int[]) { __VA_ARGS__ }))

#define TLV_CONTAINER_ITEM(...) \
	TLV_ITEM(SNDRV_CTL_TLVT_CONTAINER, __VA_ARGS__)
#define DECLARE_TLV_CONTAINER(name, ...) \
	unsigned int name[] = { TLV_CONTAINER_ITEM(__VA_ARGS__) }

#define TLV_DB_SCALE_MASK	0xffff
#define TLV_DB_SCALE_MUTE	0x10000
#define TLV_DB_SCALE_ITEM(min, step, mute)			\
	TLV_ITEM(SNDRV_CTL_TLVT_DB_SCALE,			\
		 (min),					\
		 ((step) & TLV_DB_SCALE_MASK) |		\
			((mute) ? TLV_DB_SCALE_MUTE : 0))
#define DECLARE_TLV_DB_SCALE(name, min, step, mute) \
	unsigned int name[] = { TLV_DB_SCALE_ITEM(min, step, mute) }

/* dB scale specified with min/max values instead of step */
#define TLV_DB_MINMAX_ITEM(min_dB, max_dB)			\
	TLV_ITEM(SNDRV_CTL_TLVT_DB_MINMAX, (min_dB), (max_dB))
#define TLV_DB_MINMAX_MUTE_ITEM(min_dB, max_dB)			\
	TLV_ITEM(SNDRV_CTL_TLVT_DB_MINMAX_MUTE, (min_dB), (max_dB))
#define DECLARE_TLV_DB_MINMAX(name, min_dB, max_dB) \
	unsigned int name[] = { TLV_DB_MINMAX_ITEM(min_dB, max_dB) }
#define DECLARE_TLV_DB_MINMAX_MUTE(name, min_dB, max_dB) \
	unsigned int name[] = { TLV_DB_MINMAX_MUTE_ITEM(min_dB, max_dB) }

/* linear volume between min_dB and max_dB (.01dB unit) */
#define TLV_DB_LINEAR_ITEM(min_dB, max_dB)		    \
	TLV_ITEM(SNDRV_CTL_TLVT_DB_LINEAR, (min_dB), (max_dB))
#define DECLARE_TLV_DB_LINEAR(name, min_dB, max_dB)	\
	unsigned int name[] = { TLV_DB_LINEAR_ITEM(min_dB, max_dB) }

/* dB range container:
 * Items in dB range container must be ordered by their values and by their
 * dB values. This implies that larger values must correspond with larger
 * dB values (which is also required for all other mixer controls).
 */
/* Each item is: <min> <max> <TLV> */
#define TLV_DB_RANGE_ITEM(...) \
	TLV_ITEM(SNDRV_CTL_TLVT_DB_RANGE, __VA_ARGS__)
#define DECLARE_TLV_DB_RANGE(name, ...) \
	unsigned int name[] = { TLV_DB_RANGE_ITEM(__VA_ARGS__) }
/* The below assumes that each item TLV is 4 words like DB_SCALE or LINEAR */
#define TLV_DB_RANGE_HEAD(num)			\
	SNDRV_CTL_TLVT_DB_RANGE, 6 * (num) * sizeof(unsigned int)

#define TLV_DB_GAIN_MUTE	-9999999

#endif /* __SOUND_TLV_H */
