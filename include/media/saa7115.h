/*
    saa7115.h - definition for saa7111/3/4/5 inputs and frequency flags

    Copyright (C) 2006 Hans Verkuil (hverkuil@xs4all.nl)

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

#ifndef _SAA7115_H_
#define _SAA7115_H_

/* s_routing inputs, outputs, and config */

/* SAA7111/3/4/5 HW inputs */
#define SAA7115_COMPOSITE0 0
#define SAA7115_COMPOSITE1 1
#define SAA7115_COMPOSITE2 2
#define SAA7115_COMPOSITE3 3
#define SAA7115_COMPOSITE4 4 /* not available for the saa7111/3 */
#define SAA7115_COMPOSITE5 5 /* not available for the saa7111/3 */
#define SAA7115_SVIDEO0    6
#define SAA7115_SVIDEO1    7
#define SAA7115_SVIDEO2    8
#define SAA7115_SVIDEO3    9

/* outputs */
#define SAA7115_IPORT_ON    	1
#define SAA7115_IPORT_OFF   	0

/* SAA7111 specific outputs. */
#define SAA7111_VBI_BYPASS 	2
#define SAA7111_FMT_YUV422      0x00
#define SAA7111_FMT_RGB 	0x40
#define SAA7111_FMT_CCIR 	0x80
#define SAA7111_FMT_YUV411 	0xc0

/* config flags */
/* Register 0x85 should set bit 0 to 0 (it's 1 by default). This bit
 * controls the IDQ signal polarity which is set to 'inverted' if the bit
 * it 1 and to 'default' if it is 0. */
#define SAA7115_IDQ_IS_DEFAULT  (1 << 0)

/* s_crystal_freq values and flags */

/* SAA7115 v4l2_crystal_freq frequency values */
#define SAA7115_FREQ_32_11_MHZ  32110000   /* 32.11 MHz crystal, SAA7114/5 only */
#define SAA7115_FREQ_24_576_MHZ 24576000   /* 24.576 MHz crystal */

/* SAA7115 v4l2_crystal_freq audio clock control flags */
#define SAA7115_FREQ_FL_UCGC         (1 << 0) /* SA 3A[7], UCGC, SAA7115 only */
#define SAA7115_FREQ_FL_CGCDIV       (1 << 1) /* SA 3A[6], CGCDIV, SAA7115 only */
#define SAA7115_FREQ_FL_APLL         (1 << 2) /* SA 3A[3], APLL, SAA7114/5 only */
#define SAA7115_FREQ_FL_DOUBLE_ASCLK (1 << 3) /* SA 39, LRDIV, SAA7114/5 only */

#endif

