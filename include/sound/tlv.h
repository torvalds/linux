#ifndef __SOUND_TLV_H
#define __SOUND_TLV_H

/*
 *  Advanced Linux Sound Architecture - ALSA - Driver
 *  Copyright (c) 2006 by Jaroslav Kysela <perex@suse.cz>
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

#define SNDRV_CTL_TLVT_CONTAINER 0	/* one level down - group of TLVs */
#define SNDRV_CTL_TLVT_DB_SCALE	1       /* dB scale */
#define SNDRV_CTL_TLVT_DB_LINEAR 2	/* linear volume */

#define DECLARE_TLV_DB_SCALE(name, min, step, mute) \
unsigned int name[] = { \
        SNDRV_CTL_TLVT_DB_SCALE, 2 * sizeof(unsigned int), \
        (min), ((step) & 0xffff) | ((mute) ? 0x10000 : 0) \
}

/* linear volume between min_dB and max_dB (.01dB unit) */
#define DECLARE_TLV_DB_LINEAR(name, min_dB, max_dB)	\
unsigned int name[] = { \
        SNDRV_CTL_TLVT_DB_LINEAR, 2 * sizeof(unsigned int), \
        (min_dB), (max_dB)				\
}

#define TLV_DB_GAIN_MUTE	-9999999

#endif /* __SOUND_TLV_H */
