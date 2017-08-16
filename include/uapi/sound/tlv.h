/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#ifndef __UAPI_SOUND_TLV_H
#define __UAPI_SOUND_TLV_H

#define SNDRV_CTL_TLVT_CONTAINER 0	/* one level down - group of TLVs */
#define SNDRV_CTL_TLVT_DB_SCALE	1       /* dB scale */
#define SNDRV_CTL_TLVT_DB_LINEAR 2	/* linear volume */
#define SNDRV_CTL_TLVT_DB_RANGE 3	/* dB range container */
#define SNDRV_CTL_TLVT_DB_MINMAX 4	/* dB scale with min/max */
#define SNDRV_CTL_TLVT_DB_MINMAX_MUTE 5	/* dB scale with min/max with mute */

/*
 * channel-mapping TLV items
 *  TLV length must match with num_channels
 */
#define SNDRV_CTL_TLVT_CHMAP_FIXED	0x101	/* fixed channel position */
#define SNDRV_CTL_TLVT_CHMAP_VAR	0x102	/* channels freely swappable */
#define SNDRV_CTL_TLVT_CHMAP_PAIRED	0x103	/* pair-wise swappable */

/*
 * TLV structure is right behind the struct snd_ctl_tlv:
 *   unsigned int type  	- see SNDRV_CTL_TLVT_*
 *   unsigned int length
 *   .... data aligned to sizeof(unsigned int), use
 *        block_length = (length + (sizeof(unsigned int) - 1)) &
 *                       ~(sizeof(unsigned int) - 1)) ....
 */
#define SNDRV_CTL_TLVD_ITEM(type, ...) \
	(type), SNDRV_CTL_TLVD_LENGTH(__VA_ARGS__), __VA_ARGS__
#define SNDRV_CTL_TLVD_LENGTH(...) \
	((unsigned int)sizeof((const unsigned int[]) { __VA_ARGS__ }))

#define SNDRV_CTL_TLVD_CONTAINER_ITEM(...) \
	SNDRV_CTL_TLVD_ITEM(SNDRV_CTL_TLVT_CONTAINER, __VA_ARGS__)
#define SNDRV_CTL_TLVD_DECLARE_CONTAINER(name, ...) \
	unsigned int name[] = { \
		SNDRV_CTL_TLVD_CONTAINER_ITEM(__VA_ARGS__) \
	}

#define SNDRV_CTL_TLVD_DB_SCALE_MASK	0xffff
#define SNDRV_CTL_TLVD_DB_SCALE_MUTE	0x10000
#define SNDRV_CTL_TLVD_DB_SCALE_ITEM(min, step, mute) \
	SNDRV_CTL_TLVD_ITEM(SNDRV_CTL_TLVT_DB_SCALE, \
			    (min), \
			    ((step) & SNDRV_CTL_TLVD_DB_SCALE_MASK) | \
			     ((mute) ? SNDRV_CTL_TLVD_DB_SCALE_MUTE : 0))
#define SNDRV_CTL_TLVD_DECLARE_DB_SCALE(name, min, step, mute) \
	unsigned int name[] = { \
		SNDRV_CTL_TLVD_DB_SCALE_ITEM(min, step, mute) \
	}

/* dB scale specified with min/max values instead of step */
#define SNDRV_CTL_TLVD_DB_MINMAX_ITEM(min_dB, max_dB) \
	SNDRV_CTL_TLVD_ITEM(SNDRV_CTL_TLVT_DB_MINMAX, (min_dB), (max_dB))
#define SNDRV_CTL_TLVD_DB_MINMAX_MUTE_ITEM(min_dB, max_dB) \
	SNDRV_CTL_TLVD_ITEM(SNDRV_CTL_TLVT_DB_MINMAX_MUTE, (min_dB), (max_dB))
#define SNDRV_CTL_TLVD_DECLARE_DB_MINMAX(name, min_dB, max_dB) \
	unsigned int name[] = { \
		SNDRV_CTL_TLVD_DB_MINMAX_ITEM(min_dB, max_dB) \
	}
#define SNDRV_CTL_TLVD_DECLARE_DB_MINMAX_MUTE(name, min_dB, max_dB) \
	unsigned int name[] = { \
		SNDRV_CTL_TLVD_DB_MINMAX_MUTE_ITEM(min_dB, max_dB) \
	}

/* linear volume between min_dB and max_dB (.01dB unit) */
#define SNDRV_CTL_TLVD_DB_LINEAR_ITEM(min_dB, max_dB) \
	SNDRV_CTL_TLVD_ITEM(SNDRV_CTL_TLVT_DB_LINEAR, (min_dB), (max_dB))
#define SNDRV_CTL_TLVD_DECLARE_DB_LINEAR(name, min_dB, max_dB) \
	unsigned int name[] = { \
		SNDRV_CTL_TLVD_DB_LINEAR_ITEM(min_dB, max_dB) \
	}

/* dB range container:
 * Items in dB range container must be ordered by their values and by their
 * dB values. This implies that larger values must correspond with larger
 * dB values (which is also required for all other mixer controls).
 */
/* Each item is: <min> <max> <TLV> */
#define SNDRV_CTL_TLVD_DB_RANGE_ITEM(...) \
	SNDRV_CTL_TLVD_ITEM(SNDRV_CTL_TLVT_DB_RANGE, __VA_ARGS__)
#define SNDRV_CTL_TLVD_DECLARE_DB_RANGE(name, ...) \
	unsigned int name[] = { \
		SNDRV_CTL_TLVD_DB_RANGE_ITEM(__VA_ARGS__) \
	}

#define SNDRV_CTL_TLVD_DB_GAIN_MUTE	-9999999

#endif
