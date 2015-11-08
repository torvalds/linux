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

#endif
