/* ------------------------------------------------------------------------- */
/*									     */
/* i2c-id.h - identifier values for i2c drivers and adapters		     */
/*									     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-1999 Simon G. Vogl

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */

#ifndef LINUX_I2C_ID_H
#define LINUX_I2C_ID_H

/* Please note that I2C driver IDs are optional. They are only needed if a
   legacy chip driver needs to identify a bus or a bus driver needs to
   identify a legacy client. If you don't need them, just don't set them. */

/*
 * ---- Driver types -----------------------------------------------------
 */

#define I2C_DRIVERID_MSP3400	 1
#define I2C_DRIVERID_TUNER	 2
#define I2C_DRIVERID_TEA6420	 5	/* audio matrix switch		*/
#define I2C_DRIVERID_TEA6415C	 6	/* video matrix switch		*/
#define I2C_DRIVERID_TDA9840	 7	/* stereo sound processor	*/
#define I2C_DRIVERID_SAA7111A	 8	/* video input processor	*/
#define I2C_DRIVERID_SAA7185B	13	/* video encoder		*/
#define I2C_DRIVERID_SAA7110	22	/* video decoder		*/
#define I2C_DRIVERID_SAA5249	24	/* SAA5249 and compatibles	*/
#define I2C_DRIVERID_TDA7432	27	/* Stereo sound processor	*/
#define I2C_DRIVERID_TVAUDIO    29      /* Generic TV sound driver      */
#define I2C_DRIVERID_TDA9875    32      /* TV sound decoder chip        */
#define I2C_DRIVERID_BT819	40     /* video decoder			*/
#define I2C_DRIVERID_BT856	41     /* video encoder			*/
#define I2C_DRIVERID_VPX3220	42     /* video decoder+vbi/vtxt	*/
#define I2C_DRIVERID_ADV7175	48     /* ADV 7175/7176 video encoder	*/
#define I2C_DRIVERID_SAA7114	49	/* video decoder		*/
#define I2C_DRIVERID_ADV7170	54	/* video encoder		*/
#define I2C_DRIVERID_SAA7191	57	/* video decoder		*/
#define I2C_DRIVERID_INDYCAM	58	/* SGI IndyCam			*/
#define I2C_DRIVERID_OVCAMCHIP	61	/* OmniVision CMOS image sens.	*/
#define I2C_DRIVERID_SAA6752HS	67	/* MPEG2 encoder		*/
#define I2C_DRIVERID_TVEEPROM	68	/* TV EEPROM			*/
#define I2C_DRIVERID_WM8775	69	/* wm8775 audio processor	*/
#define I2C_DRIVERID_CS53L32A	70	/* cs53l32a audio processor	*/
#define I2C_DRIVERID_CX25840	71	/* cx2584x video encoder	*/
#define I2C_DRIVERID_SAA7127	72	/* saa7127 video encoder	*/
#define I2C_DRIVERID_SAA711X	73	/* saa711x video encoders	*/
#define I2C_DRIVERID_INFRARED	75	/* I2C InfraRed on Video boards */
#define I2C_DRIVERID_TVP5150	76	/* TVP5150 video decoder        */
#define I2C_DRIVERID_WM8739	77	/* wm8739 audio processor	*/
#define I2C_DRIVERID_UPD64083	78	/* upd64083 video processor	*/
#define I2C_DRIVERID_UPD64031A	79	/* upd64031a video processor	*/
#define I2C_DRIVERID_SAA717X	80	/* saa717x video encoder	*/
#define I2C_DRIVERID_BT866	85	/* Conexant bt866 video encoder */
#define I2C_DRIVERID_KS0127	86	/* Samsung ks0127 video decoder */
#define I2C_DRIVERID_TLV320AIC23B 87	/* TI TLV320AIC23B audio codec  */
#define I2C_DRIVERID_VP27SMPX	93	/* Panasonic VP27s tuner internal MPX */
#define I2C_DRIVERID_M52790 	95      /* Mitsubishi M52790SP/FP AV switch */
#define I2C_DRIVERID_CS5345	96	/* cs5345 audio processor	*/

#define I2C_DRIVERID_OV7670 1048	/* Omnivision 7670 camera */

/*
 * ---- Adapter types ----------------------------------------------------
 */

/* --- Bit algorithm adapters						*/
#define I2C_HW_B_BT848		0x010005 /* BT848 video boards */
#define I2C_HW_B_RIVA		0x010010 /* Riva based graphics cards */
#define I2C_HW_B_ZR36067	0x010019 /* Zoran-36057/36067 based boards */
#define I2C_HW_B_CX2388x	0x01001b /* connexant 2388x based tv cards */
#define I2C_HW_B_EM28XX		0x01001f /* em28xx video capture cards */
#define I2C_HW_B_CX2341X	0x010020 /* Conexant CX2341X MPEG encoder cards */
#define I2C_HW_B_CX23885	0x010022 /* conexant 23885 based tv cards (bus1) */
#define I2C_HW_B_AU0828		0x010023 /* auvitek au0828 usb bridge */

/* --- SGI adapters							*/
#define I2C_HW_SGI_VINO		0x160000

/* --- SMBus only adapters						*/
#define I2C_HW_SMBUS_W9968CF	0x04000d
#define I2C_HW_SMBUS_OV511	0x04000e /* OV511(+) USB 1.1 webcam ICs */
#define I2C_HW_SMBUS_OV518	0x04000f /* OV518(+) USB 1.1 webcam ICs */
#define I2C_HW_SMBUS_CAFE	0x040012 /* Marvell 88ALP01 "CAFE" cam  */

/* --- Miscellaneous adapters */
#define I2C_HW_SAA7146		0x060000 /* SAA7146 video decoder bus */
#define I2C_HW_SAA7134		0x090000 /* SAA7134 video decoder bus */

#endif /* LINUX_I2C_ID_H */
