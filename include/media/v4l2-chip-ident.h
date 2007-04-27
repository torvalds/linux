/*
    v4l2 chip identifiers header

    This header provides a list of chip identifiers that can be returned
    through the VIDIOC_G_CHIP_IDENT ioctl.

    Copyright (C) 2007 Hans Verkuil <hverkuil@xs4all.nl>

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef V4L2_CHIP_IDENT_H_
#define V4L2_CHIP_IDENT_H_

/* VIDIOC_G_CHIP_IDENT: identifies the actual chip installed on the board */
enum {
	/* general idents: reserved range 0-49 */
	V4L2_IDENT_NONE      = 0,       /* No chip matched */
	V4L2_IDENT_AMBIGUOUS = 1,       /* Match too general, multiple chips matched */
	V4L2_IDENT_UNKNOWN   = 2,       /* Chip found, but cannot identify */

	/* module saa7110: just ident= 100 */
	V4L2_IDENT_SAA7110 = 100,

	/* module saa7111: just ident= 101 */
	V4L2_IDENT_SAA7111 = 101,

	/* module saa7115: reserved range 102-149 */
	V4L2_IDENT_SAA7113 = 103,
	V4L2_IDENT_SAA7114 = 104,
	V4L2_IDENT_SAA7115 = 105,
	V4L2_IDENT_SAA7118 = 108,

	/* module saa7127: reserved range 150-199 */
	V4L2_IDENT_SAA7127 = 157,
	V4L2_IDENT_SAA7129 = 159,

	/* module cx25840: reserved range 200-249 */
	V4L2_IDENT_CX25836 = 236,
	V4L2_IDENT_CX25837 = 237,
	V4L2_IDENT_CX25840 = 240,
	V4L2_IDENT_CX25841 = 241,
	V4L2_IDENT_CX25842 = 242,
	V4L2_IDENT_CX25843 = 243,

	/* OmniVision sensors - range 250-299 */
	V4L2_IDENT_OV7670 = 250,
};

#endif
