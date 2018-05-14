/*
    m52790.h - definition for m52790 inputs and outputs

    Copyright (C) 2007 Hans Verkuil (hverkuil@xs4all.nl)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _M52790_H_
#define _M52790_H_

/* Input routing switch 1 */

#define M52790_SW1_IN_MASK	0x0003
#define M52790_SW1_IN_TUNER	0x0000
#define M52790_SW1_IN_V2	0x0001
#define M52790_SW1_IN_V3	0x0002
#define M52790_SW1_IN_V4	0x0003

/* Selects component input instead of composite */
#define M52790_SW1_YCMIX	0x0004


/* Input routing switch 2 */

#define M52790_SW2_IN_MASK	0x0300
#define M52790_SW2_IN_TUNER	0x0000
#define M52790_SW2_IN_V2	0x0100
#define M52790_SW2_IN_V3	0x0200
#define M52790_SW2_IN_V4	0x0300

/* Selects component input instead of composite */
#define M52790_SW2_YCMIX	0x0400


/* Output routing switch 1 */

/* Enable 6dB amplifier for composite out */
#define M52790_SW1_V_AMP	0x0008

/* Enable 6dB amplifier for component out */
#define M52790_SW1_YC_AMP	0x0010

/* Audio output mode */
#define M52790_SW1_AUDIO_MASK	0x00c0
#define M52790_SW1_AUDIO_MUTE	0x0000
#define M52790_SW1_AUDIO_R	0x0040
#define M52790_SW1_AUDIO_L	0x0080
#define M52790_SW1_AUDIO_STEREO 0x00c0


/* Output routing switch 2 */

/* Enable 6dB amplifier for composite out */
#define M52790_SW2_V_AMP	0x0800

/* Enable 6dB amplifier for component out */
#define M52790_SW2_YC_AMP	0x1000

/* Audio output mode */
#define M52790_SW2_AUDIO_MASK	0xc000
#define M52790_SW2_AUDIO_MUTE	0x0000
#define M52790_SW2_AUDIO_R	0x4000
#define M52790_SW2_AUDIO_L	0x8000
#define M52790_SW2_AUDIO_STEREO 0xc000


/* Common values */
#define M52790_IN_TUNER (M52790_SW1_IN_TUNER | M52790_SW2_IN_TUNER)
#define M52790_IN_V2    (M52790_SW1_IN_V2 | M52790_SW2_IN_V2)
#define M52790_IN_V3    (M52790_SW1_IN_V3 | M52790_SW2_IN_V3)
#define M52790_IN_V4    (M52790_SW1_IN_V4 | M52790_SW2_IN_V4)

#define M52790_OUT_STEREO	(M52790_SW1_AUDIO_STEREO | \
				 M52790_SW2_AUDIO_STEREO)
#define M52790_OUT_AMP_STEREO	(M52790_SW1_AUDIO_STEREO | \
				 M52790_SW1_V_AMP | \
				 M52790_SW2_AUDIO_STEREO | \
				 M52790_SW2_V_AMP)

#endif
