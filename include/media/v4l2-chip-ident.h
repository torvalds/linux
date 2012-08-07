/*
    v4l2 chip identifiers header

    This header provides a list of chip identifiers that can be returned
    through the VIDIOC_DBG_G_CHIP_IDENT ioctl.

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

/* VIDIOC_DBG_G_CHIP_IDENT: identifies the actual chip installed on the board */

/* KEEP THIS LIST ORDERED BY ID!
   Otherwise it will be hard to see which ranges are already in use when
   adding support to a new chip family. */
enum {
	/* general idents: reserved range 0-49 */
	V4L2_IDENT_NONE      = 0,       /* No chip matched */
	V4L2_IDENT_AMBIGUOUS = 1,       /* Match too general, multiple chips matched */
	V4L2_IDENT_UNKNOWN   = 2,       /* Chip found, but cannot identify */

	/* module tvaudio: reserved range 50-99 */
	V4L2_IDENT_TVAUDIO = 50,	/* A tvaudio chip, unknown which it is exactly */

	/* Sony IMX074 */
	V4L2_IDENT_IMX074 = 74,

	/* module saa7110: just ident 100 */
	V4L2_IDENT_SAA7110 = 100,

	/* module saa7115: reserved range 101-149 */
	V4L2_IDENT_SAA7111 = 101,
	V4L2_IDENT_SAA7111A = 102,
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
	V4L2_IDENT_OV7720 = 251,
	V4L2_IDENT_OV7725 = 252,
	V4L2_IDENT_OV7660 = 253,
	V4L2_IDENT_OV9650 = 254,
	V4L2_IDENT_OV9655 = 255,
	V4L2_IDENT_SOI968 = 256,
	V4L2_IDENT_OV9640 = 257,
	V4L2_IDENT_OV6650 = 258,
	V4L2_IDENT_OV2640 = 259,
	V4L2_IDENT_OV9740 = 260,
	V4L2_IDENT_OV5642 = 261,

	/* module saa7146: reserved range 300-309 */
	V4L2_IDENT_SAA7146 = 300,

	/* Conexant MPEG encoder/decoders: reserved range 400-420 */
	V4L2_IDENT_CX23418_843 = 403, /* Integrated A/V Decoder on the '418 */
	V4L2_IDENT_CX23415 = 415,
	V4L2_IDENT_CX23416 = 416,
	V4L2_IDENT_CX23417 = 417,
	V4L2_IDENT_CX23418 = 418,

	/* module bt819: reserved range 810-819 */
	V4L2_IDENT_BT815A = 815,
	V4L2_IDENT_BT817A = 817,
	V4L2_IDENT_BT819A = 819,

	/* module au0828 */
	V4L2_IDENT_AU0828 = 828,

	/* module bt856: just ident 856 */
	V4L2_IDENT_BT856 = 856,

	/* module bt866: just ident 866 */
	V4L2_IDENT_BT866 = 866,

	/* module ks0127: reserved range 1120-1129 */
	V4L2_IDENT_KS0122S = 1122,
	V4L2_IDENT_KS0127  = 1127,
	V4L2_IDENT_KS0127B = 1128,

	/* module indycam: just ident 2000 */
	V4L2_IDENT_INDYCAM = 2000,

	/* module vp27smpx: just ident 2700 */
	V4L2_IDENT_VP27SMPX = 2700,

	/* module vpx3220: reserved range: 3210-3229 */
	V4L2_IDENT_VPX3214C = 3214,
	V4L2_IDENT_VPX3216B = 3216,
	V4L2_IDENT_VPX3220A = 3220,

	/* VX855 just ident 3409 */
	/* Other via devs could use 3314, 3324, 3327, 3336, 3364, 3353 */
	V4L2_IDENT_VIA_VX855 = 3409,

	/* module tvp5150 */
	V4L2_IDENT_TVP5150 = 5150,

	/* module saa5246a: just ident 5246 */
	V4L2_IDENT_SAA5246A = 5246,

	/* module saa5249: just ident 5249 */
	V4L2_IDENT_SAA5249 = 5249,

	/* module cs5345: just ident 5345 */
	V4L2_IDENT_CS5345 = 5345,

	/* module tea6415c: just ident 6415 */
	V4L2_IDENT_TEA6415C = 6415,

	/* module tea6420: just ident 6420 */
	V4L2_IDENT_TEA6420 = 6420,

	/* module saa6588: just ident 6588 */
	V4L2_IDENT_SAA6588 = 6588,

	/* module vs6624: just ident 6624 */
	V4L2_IDENT_VS6624 = 6624,

	/* module saa6752hs: reserved range 6750-6759 */
	V4L2_IDENT_SAA6752HS = 6752,
	V4L2_IDENT_SAA6752HS_AC3 = 6753,

	/* modules tef6862: just ident 6862 */
	V4L2_IDENT_TEF6862 = 6862,

	/* module tvp7002: just ident 7002 */
	V4L2_IDENT_TVP7002 = 7002,

	/* module adv7170: just ident 7170 */
	V4L2_IDENT_ADV7170 = 7170,

	/* module adv7175: just ident 7175 */
	V4L2_IDENT_ADV7175 = 7175,

	/* module adv7180: just ident 7180 */
	V4L2_IDENT_ADV7180 = 7180,

	/* module adv7183: just ident 7183 */
	V4L2_IDENT_ADV7183 = 7183,

	/* module saa7185: just ident 7185 */
	V4L2_IDENT_SAA7185 = 7185,

	/* module saa7191: just ident 7191 */
	V4L2_IDENT_SAA7191 = 7191,

	/* module ths7303: just ident 7303 */
	V4L2_IDENT_THS7303 = 7303,

	/* module adv7343: just ident 7343 */
	V4L2_IDENT_ADV7343 = 7343,

	/* module adv7393: just ident 7393 */
	V4L2_IDENT_ADV7393 = 7393,

	/* module saa7706h: just ident 7706 */
	V4L2_IDENT_SAA7706H = 7706,

	/* module mt9v011, just ident 8243 */
	V4L2_IDENT_MT9V011 = 8243,

	/* module wm8739: just ident 8739 */
	V4L2_IDENT_WM8739 = 8739,

	/* module wm8775: just ident 8775 */
	V4L2_IDENT_WM8775 = 8775,

	/* Marvell controllers starting at 8801 */
	V4L2_IDENT_CAFE = 8801,
	V4L2_IDENT_ARMADA610 = 8802,

	/* AKM AK8813/AK8814 */
	V4L2_IDENT_AK8813 = 8813,
	V4L2_IDENT_AK8814 = 8814,

	/* module cx23885 and cx25840 */
	V4L2_IDENT_CX23885    = 8850,
	V4L2_IDENT_CX23885_AV = 8851, /* Integrated A/V decoder */
	V4L2_IDENT_CX23887    = 8870,
	V4L2_IDENT_CX23887_AV = 8871, /* Integrated A/V decoder */
	V4L2_IDENT_CX23888    = 8880,
	V4L2_IDENT_CX23888_AV = 8881, /* Integrated A/V decoder */
	V4L2_IDENT_CX23888_IR = 8882, /* Integrated infrared controller */

	/* module tda9840: just ident 9840 */
	V4L2_IDENT_TDA9840 = 9840,

	/* module tw9910: just ident 9910 */
	V4L2_IDENT_TW9910 = 9910,

	/* module sn9c20x: just ident 10000 */
	V4L2_IDENT_SN9C20X = 10000,

	/* module cx231xx and cx25840 */
	V4L2_IDENT_CX2310X_AV = 23099, /* Integrated A/V decoder; not in '100 */
	V4L2_IDENT_CX23100    = 23100,
	V4L2_IDENT_CX23101    = 23101,
	V4L2_IDENT_CX23102    = 23102,

	/* module msp3400: reserved range 34000-34999 for msp34xx */
	V4L2_IDENT_MSPX4XX  = 34000, /* generic MSPX4XX identifier, only
					use internally (tveeprom.c). */

	V4L2_IDENT_MSP3400B = 34002,
	V4L2_IDENT_MSP3400C = 34003,
	V4L2_IDENT_MSP3400D = 34004,
	V4L2_IDENT_MSP3400G = 34007,
	V4L2_IDENT_MSP3401G = 34017,
	V4L2_IDENT_MSP3402G = 34027,
	V4L2_IDENT_MSP3405D = 34054,
	V4L2_IDENT_MSP3405G = 34057,
	V4L2_IDENT_MSP3407D = 34074,
	V4L2_IDENT_MSP3407G = 34077,

	V4L2_IDENT_MSP3410B = 34102,
	V4L2_IDENT_MSP3410C = 34103,
	V4L2_IDENT_MSP3410D = 34104,
	V4L2_IDENT_MSP3410G = 34107,
	V4L2_IDENT_MSP3411G = 34117,
	V4L2_IDENT_MSP3412G = 34127,
	V4L2_IDENT_MSP3415D = 34154,
	V4L2_IDENT_MSP3415G = 34157,
	V4L2_IDENT_MSP3417D = 34174,
	V4L2_IDENT_MSP3417G = 34177,

	V4L2_IDENT_MSP3420G = 34207,
	V4L2_IDENT_MSP3421G = 34217,
	V4L2_IDENT_MSP3422G = 34227,
	V4L2_IDENT_MSP3425G = 34257,
	V4L2_IDENT_MSP3427G = 34277,

	V4L2_IDENT_MSP3430G = 34307,
	V4L2_IDENT_MSP3431G = 34317,
	V4L2_IDENT_MSP3435G = 34357,
	V4L2_IDENT_MSP3437G = 34377,

	V4L2_IDENT_MSP3440G = 34407,
	V4L2_IDENT_MSP3441G = 34417,
	V4L2_IDENT_MSP3442G = 34427,
	V4L2_IDENT_MSP3445G = 34457,
	V4L2_IDENT_MSP3447G = 34477,

	V4L2_IDENT_MSP3450G = 34507,
	V4L2_IDENT_MSP3451G = 34517,
	V4L2_IDENT_MSP3452G = 34527,
	V4L2_IDENT_MSP3455G = 34557,
	V4L2_IDENT_MSP3457G = 34577,

	V4L2_IDENT_MSP3460G = 34607,
	V4L2_IDENT_MSP3461G = 34617,
	V4L2_IDENT_MSP3465G = 34657,
	V4L2_IDENT_MSP3467G = 34677,

	/* module msp3400: reserved range 44000-44999 for msp44xx */
	V4L2_IDENT_MSP4400G = 44007,
	V4L2_IDENT_MSP4408G = 44087,
	V4L2_IDENT_MSP4410G = 44107,
	V4L2_IDENT_MSP4418G = 44187,
	V4L2_IDENT_MSP4420G = 44207,
	V4L2_IDENT_MSP4428G = 44287,
	V4L2_IDENT_MSP4440G = 44407,
	V4L2_IDENT_MSP4448G = 44487,
	V4L2_IDENT_MSP4450G = 44507,
	V4L2_IDENT_MSP4458G = 44587,

	/* Micron CMOS sensor chips: 45000-45099 */
	V4L2_IDENT_MT9M001C12ST		= 45000,
	V4L2_IDENT_MT9M001C12STM	= 45005,
	V4L2_IDENT_MT9M111		= 45007,
	V4L2_IDENT_MT9M112		= 45008,
	V4L2_IDENT_MT9V022IX7ATC	= 45010, /* No way to detect "normal" I77ATx */
	V4L2_IDENT_MT9V022IX7ATM	= 45015, /* and "lead free" IA7ATx chips */
	V4L2_IDENT_MT9T031		= 45020,
	V4L2_IDENT_MT9T111		= 45021,
	V4L2_IDENT_MT9T112		= 45022,
	V4L2_IDENT_MT9V111		= 45031,
	V4L2_IDENT_MT9V112		= 45032,

	/* HV7131R CMOS sensor: just ident 46000 */
	V4L2_IDENT_HV7131R		= 46000,

	/* Sharp RJ54N1CB0C, 0xCB0C = 51980 */
	V4L2_IDENT_RJ54N1CB0C = 51980,

	/* module m52790: just ident 52790 */
	V4L2_IDENT_M52790 = 52790,

	/* module cs53132a: just ident 53132 */
	V4L2_IDENT_CS53l32A = 53132,

	/* modules upd61151 MPEG2 encoder: just ident 54000 */
	V4L2_IDENT_UPD61161 = 54000,
	/* modules upd61152 MPEG2 encoder with AC3: just ident 54001 */
	V4L2_IDENT_UPD61162 = 54001,

	/* module upd64031a: just ident 64031 */
	V4L2_IDENT_UPD64031A = 64031,

	/* module upd64083: just ident 64083 */
	V4L2_IDENT_UPD64083 = 64083,

	/* Don't just add new IDs at the end: KEEP THIS LIST ORDERED BY ID! */
};

#endif
