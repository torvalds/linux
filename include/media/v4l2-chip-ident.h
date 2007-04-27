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

	/* module tvaudio: reserved range 50-99 */
	V4L2_IDENT_TVAUDIO = 50,	/* A tvaudio chip, unknown which it is exactly */

	/* module saa7110: just ident 100 */
	V4L2_IDENT_SAA7110 = 100,

	/* module saa7111: just ident 101 */
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

	/* OmniVision sensors: reserved range 250-299 */
	V4L2_IDENT_OV7670 = 250,

	/* Conexant MPEG encoder/decoders: reserved range 410-420 */
	V4L2_IDENT_CX23415 = 415,
	V4L2_IDENT_CX23416 = 416,

	/* module wm8739: just ident 8739 */
	V4L2_IDENT_WM8739 = 8739,

	/* module wm8775: just ident 8775 */
	V4L2_IDENT_WM8775 = 8775,

	/* module cs53132a: just ident 53132 */
	V4L2_IDENT_CS53l32A = 53132,

	/* module upd64031a: just ident 64031 */
	V4L2_IDENT_UPD64031A = 64031,

	/* module upd64083: just ident 64083 */
	V4L2_IDENT_UPD64083 = 64083,

	/* module msp34xx: reserved range 34000-34999 */
	V4L2_IDENT_MSP3400B = 34002,
	V4L2_IDENT_MSP3410B = 34102,

	V4L2_IDENT_MSP3400C = 34003,
	V4L2_IDENT_MSP3410C = 34103,

	V4L2_IDENT_MSP3400D = 34004,
	V4L2_IDENT_MSP3410D = 34104,
	V4L2_IDENT_MSP3405D = 34054,
	V4L2_IDENT_MSP3415D = 34154,
	V4L2_IDENT_MSP3407D = 34074,
	V4L2_IDENT_MSP3417D = 34174,

	V4L2_IDENT_MSP3400G = 34007,
	V4L2_IDENT_MSP3410G = 34107,
	V4L2_IDENT_MSP3420G = 34207,
	V4L2_IDENT_MSP3430G = 34307,
	V4L2_IDENT_MSP3440G = 34407,
	V4L2_IDENT_MSP3450G = 34507,
	V4L2_IDENT_MSP3460G = 34607,

	V4L2_IDENT_MSP3401G = 34017,
	V4L2_IDENT_MSP3411G = 34117,
	V4L2_IDENT_MSP3421G = 34217,
	V4L2_IDENT_MSP3431G = 34317,
	V4L2_IDENT_MSP3441G = 34417,
	V4L2_IDENT_MSP3451G = 34517,
	V4L2_IDENT_MSP3461G = 34617,

	V4L2_IDENT_MSP3402G = 34027,
	V4L2_IDENT_MSP3412G = 34127,
	V4L2_IDENT_MSP3422G = 34227,
	V4L2_IDENT_MSP3442G = 34427,
	V4L2_IDENT_MSP3452G = 34527,

	V4L2_IDENT_MSP3405G = 34057,
	V4L2_IDENT_MSP3415G = 34157,
	V4L2_IDENT_MSP3425G = 34257,
	V4L2_IDENT_MSP3435G = 34357,
	V4L2_IDENT_MSP3445G = 34457,
	V4L2_IDENT_MSP3455G = 34557,
	V4L2_IDENT_MSP3465G = 34657,

	V4L2_IDENT_MSP3407G = 34077,
	V4L2_IDENT_MSP3417G = 34177,
	V4L2_IDENT_MSP3427G = 34277,
	V4L2_IDENT_MSP3437G = 34377,
	V4L2_IDENT_MSP3447G = 34477,
	V4L2_IDENT_MSP3457G = 34577,
	V4L2_IDENT_MSP3467G = 34677,

	/* module msp44xx: reserved range 44000-44999 */
	V4L2_IDENT_MSP4400G = 44007,
	V4L2_IDENT_MSP4410G = 44107,
	V4L2_IDENT_MSP4420G = 44207,
	V4L2_IDENT_MSP4440G = 44407,
	V4L2_IDENT_MSP4450G = 44507,

	V4L2_IDENT_MSP4408G = 44087,
	V4L2_IDENT_MSP4418G = 44187,
	V4L2_IDENT_MSP4428G = 44287,
	V4L2_IDENT_MSP4448G = 44487,
	V4L2_IDENT_MSP4458G = 44587,
};

#endif
