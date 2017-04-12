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

#include <uapi/sound/tlv.h>

/* For historical reasons, these macros are aliases to the ones in UAPI. */
#define TLV_ITEM			SNDRV_CTL_TLVD_ITEM
#define TLV_LENGTH			SNDRV_CTL_TLVD_LENGTH

#define TLV_CONTAINER_ITEM		SNDRV_CTL_TLVD_CONTAINER_ITEM
#define DECLARE_TLV_CONTAINER		SNDRV_CTL_TLVD_DECLARE_CONTAINER

#define TLV_DB_SCALE_MASK		SNDRV_CTL_TLVD_DB_SCALE_MASK
#define TLV_DB_SCALE_MUTE		SNDRV_CTL_TLVD_DB_SCALE_MUTE
#define TLV_DB_SCALE_ITEM		SNDRV_CTL_TLVD_DB_SCALE_ITEM
#define DECLARE_TLV_DB_SCALE		SNDRV_CTL_TLVD_DECLARE_DB_SCALE

#define TLV_DB_MINMAX_ITEM		SNDRV_CTL_TLVD_DB_MINMAX_ITEM
#define TLV_DB_MINMAX_MUTE_ITEM		SNDRV_CTL_TLVD_DB_MINMAX_MUTE_ITEM
#define DECLARE_TLV_DB_MINMAX		SNDRV_CTL_TLVD_DECLARE_DB_MINMAX
#define DECLARE_TLV_DB_MINMAX_MUTE	SNDRV_CTL_TLVD_DECLARE_DB_MINMAX_MUTE

#define TLV_DB_LINEAR_ITEM		SNDRV_CTL_TLVD_DB_LINEAR_ITEM
#define DECLARE_TLV_DB_LINEAR		SNDRV_CTL_TLVD_DECLARE_DB_LINEAR

#define TLV_DB_RANGE_ITEM		SNDRV_CTL_TLVD_DB_RANGE_ITEM
#define DECLARE_TLV_DB_RANGE		SNDRV_CTL_TLVD_DECLARE_DB_RANGE

#define TLV_DB_GAIN_MUTE		SNDRV_CTL_TLVD_DB_GAIN_MUTE

/*
 * The below assumes that each item TLV is 4 words like DB_SCALE or LINEAR.
 * This is an old fasion and obsoleted by commit bf1d1c9b6179("ALSA: tlv: add
 * DECLARE_TLV_DB_RANGE()").
 */
#define TLV_DB_RANGE_HEAD(num) \
	SNDRV_CTL_TLVT_DB_RANGE, 6 * (num) * sizeof(unsigned int)

#endif /* __SOUND_TLV_H */
