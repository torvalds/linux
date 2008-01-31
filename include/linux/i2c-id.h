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
#define I2C_DRIVERID_TDA8425	 4	/* stereo sound processor	*/
#define I2C_DRIVERID_TEA6420	 5	/* audio matrix switch		*/
#define I2C_DRIVERID_TEA6415C	 6	/* video matrix switch		*/
#define I2C_DRIVERID_TDA9840	 7	/* stereo sound processor	*/
#define I2C_DRIVERID_SAA7111A	 8	/* video input processor	*/
#define I2C_DRIVERID_SAA7185B	13	/* video encoder		*/
#define I2C_DRIVERID_TEA6300	18	/* audio mixer			*/
#define I2C_DRIVERID_TDA9850	20	/* audio mixer			*/
#define I2C_DRIVERID_TDA9855	21	/* audio mixer			*/
#define I2C_DRIVERID_SAA7110	22	/* video decoder		*/
#define I2C_DRIVERID_MGATVO	23	/* Matrox TVOut			*/
#define I2C_DRIVERID_SAA5249	24	/* SAA5249 and compatibles	*/
#define I2C_DRIVERID_PCF8583	25	/* real time clock		*/
#define I2C_DRIVERID_SAB3036	26	/* SAB3036 tuner		*/
#define I2C_DRIVERID_TDA7432	27	/* Stereo sound processor	*/
#define I2C_DRIVERID_TVMIXER    28      /* Mixer driver for tv cards    */
#define I2C_DRIVERID_TVAUDIO    29      /* Generic TV sound driver      */
#define I2C_DRIVERID_TDA9873    31      /* TV sound decoder chip        */
#define I2C_DRIVERID_TDA9875    32      /* TV sound decoder chip        */
#define I2C_DRIVERID_PIC16C54_PV9 33    /* Audio mux/ir receiver        */
#define I2C_DRIVERID_BT819	40     /* video decoder			*/
#define I2C_DRIVERID_BT856	41     /* video encoder			*/
#define I2C_DRIVERID_VPX3220	42     /* video decoder+vbi/vtxt	*/
#define I2C_DRIVERID_ADV7175	48     /* ADV 7175/7176 video encoder	*/
#define I2C_DRIVERID_SAA7114	49	/* video decoder		*/
#define I2C_DRIVERID_ADV7170	54	/* video encoder		*/
#define I2C_DRIVERID_SAA7191	57	/* video decoder		*/
#define I2C_DRIVERID_INDYCAM	58	/* SGI IndyCam			*/
#define I2C_DRIVERID_OVCAMCHIP	61	/* OmniVision CMOS image sens.	*/
#define I2C_DRIVERID_MAX6900	63	/* MAX6900 real-time clock	*/
#define I2C_DRIVERID_TDA9874	66	/* TV sound decoder		*/
#define I2C_DRIVERID_SAA6752HS	67	/* MPEG2 encoder		*/
#define I2C_DRIVERID_TVEEPROM	68	/* TV EEPROM			*/
#define I2C_DRIVERID_WM8775	69	/* wm8775 audio processor	*/
#define I2C_DRIVERID_CS53L32A	70	/* cs53l32a audio processor	*/
#define I2C_DRIVERID_CX25840	71	/* cx2584x video encoder	*/
#define I2C_DRIVERID_SAA7127	72	/* saa7124 video encoder	*/
#define I2C_DRIVERID_SAA711X	73	/* saa711x video encoders	*/
#define I2C_DRIVERID_AKITAIOEXP	74	/* IO Expander on Sharp SL-C1000 */
#define I2C_DRIVERID_INFRARED	75	/* I2C InfraRed on Video boards */
#define I2C_DRIVERID_TVP5150	76	/* TVP5150 video decoder        */
#define I2C_DRIVERID_WM8739	77	/* wm8739 audio processor	*/
#define I2C_DRIVERID_UPD64083	78	/* upd64083 video processor	*/
#define I2C_DRIVERID_UPD64031A	79	/* upd64031a video processor	*/
#define I2C_DRIVERID_SAA717X	80	/* saa717x video encoder	*/
#define I2C_DRIVERID_DS1672	81	/* Dallas/Maxim DS1672 RTC	*/
#define I2C_DRIVERID_X1205	82	/* Xicor/Intersil X1205 RTC	*/
#define I2C_DRIVERID_PCF8563	83	/* Philips PCF8563 RTC		*/
#define I2C_DRIVERID_BT866	85	/* Conexant bt866 video encoder */
#define I2C_DRIVERID_KS0127	86	/* Samsung ks0127 video decoder */
#define I2C_DRIVERID_TLV320AIC23B 87	/* TI TLV320AIC23B audio codec  */
#define I2C_DRIVERID_ISL1208	88	/* Intersil ISL1208 RTC		*/
#define I2C_DRIVERID_WM8731	89	/* Wolfson WM8731 audio codec */
#define I2C_DRIVERID_WM8750	90	/* Wolfson WM8750 audio codec */
#define I2C_DRIVERID_WM8753	91	/* Wolfson WM8753 audio codec */
#define I2C_DRIVERID_LM4857 	92 	/* LM4857 Audio Amplifier */
#define I2C_DRIVERID_VP27SMPX	93	/* Panasonic VP27s tuner internal MPX */
#define I2C_DRIVERID_CS4270	94	/* Cirrus Logic 4270 audio codec */
#define I2C_DRIVERID_M52790 	95      /* Mitsubishi M52790SP/FP AV switch */
#define I2C_DRIVERID_CS5345	96	/* cs5345 audio processor	*/

#define I2C_DRIVERID_I2CDEV	900

/* IDs --   Use DRIVERIDs 1000-1999 for sensors.
   These were originally in sensors.h in the lm_sensors package */
#define I2C_DRIVERID_LM78 1002
#define I2C_DRIVERID_LM75 1003
#define I2C_DRIVERID_GL518 1004
#define I2C_DRIVERID_EEPROM 1005
#define I2C_DRIVERID_W83781D 1006
#define I2C_DRIVERID_LM80 1007
#define I2C_DRIVERID_ADM1021 1008
#define I2C_DRIVERID_ADM9240 1009
#define I2C_DRIVERID_LTC1710 1010
#define I2C_DRIVERID_BT869 1013
#define I2C_DRIVERID_MAXILIFE 1014
#define I2C_DRIVERID_MATORB 1015
#define I2C_DRIVERID_GL520 1016
#define I2C_DRIVERID_THMC50 1017
#define I2C_DRIVERID_ADM1025 1020
#define I2C_DRIVERID_LM87 1021
#define I2C_DRIVERID_PCF8574 1022
#define I2C_DRIVERID_MTP008 1023
#define I2C_DRIVERID_DS1621 1024
#define I2C_DRIVERID_ADM1024 1025
#define I2C_DRIVERID_CH700X 1027 /* single driver for CH7003-7009 digital pc to tv encoders */
#define I2C_DRIVERID_FSCPOS 1028
#define I2C_DRIVERID_FSCSCY 1029
#define I2C_DRIVERID_PCF8591 1030
#define I2C_DRIVERID_LM92 1033
#define I2C_DRIVERID_SMARTBATT 1035
#define I2C_DRIVERID_BMCSENSORS 1036
#define I2C_DRIVERID_FS451 1037
#define I2C_DRIVERID_LM85 1039
#define I2C_DRIVERID_LM83 1040
#define I2C_DRIVERID_LM90 1042
#define I2C_DRIVERID_ASB100 1043
#define I2C_DRIVERID_FSCHER 1046
#define I2C_DRIVERID_W83L785TS 1047
#define I2C_DRIVERID_OV7670 1048	/* Omnivision 7670 camera */

/*
 * ---- Adapter types ----------------------------------------------------
 */

/* --- Bit algorithm adapters						*/
#define I2C_HW_B_LP		0x010000 /* Parallel port Philips style */
#define I2C_HW_B_BT848		0x010005 /* BT848 video boards */
#define I2C_HW_B_VIA		0x010007 /* Via vt82c586b */
#define I2C_HW_B_HYDRA		0x010008 /* Apple Hydra Mac I/O */
#define I2C_HW_B_G400		0x010009 /* Matrox G400 */
#define I2C_HW_B_I810		0x01000a /* Intel I810 */
#define I2C_HW_B_VOO		0x01000b /* 3dfx Voodoo 3 / Banshee */
#define I2C_HW_B_SCX200		0x01000e /* Nat'l Semi SCx200 I2C */
#define I2C_HW_B_RIVA		0x010010 /* Riva based graphics cards */
#define I2C_HW_B_IOC		0x010011 /* IOC bit-wiggling */
#define I2C_HW_B_IXP2000	0x010016 /* GPIO on IXP2000 systems */
#define I2C_HW_B_S3VIA		0x010018 /* S3Via ProSavage adapter */
#define I2C_HW_B_ZR36067	0x010019 /* Zoran-36057/36067 based boards */
#define I2C_HW_B_PCILYNX	0x01001a /* TI PCILynx I2C adapter */
#define I2C_HW_B_CX2388x	0x01001b /* connexant 2388x based tv cards */
#define I2C_HW_B_NVIDIA		0x01001c /* nvidia framebuffer driver */
#define I2C_HW_B_SAVAGE		0x01001d /* savage framebuffer driver */
#define I2C_HW_B_RADEON		0x01001e /* radeon framebuffer driver */
#define I2C_HW_B_EM28XX		0x01001f /* em28xx video capture cards */
#define I2C_HW_B_CX2341X	0x010020 /* Conexant CX2341X MPEG encoder cards */
#define I2C_HW_B_INTELFB	0x010021 /* intel framebuffer driver */
#define I2C_HW_B_CX23885	0x010022 /* conexant 23885 based tv cards (bus1) */

/* --- PCF 8584 based algorithms					*/
#define I2C_HW_P_ELEK		0x020002 /* Elektor ISA Bus inteface card */

/* --- PCA 9564 based algorithms */
#define I2C_HW_A_ISA		0x1a0000 /* generic ISA Bus interface card */

/* --- PowerPC on-chip adapters						*/
#define I2C_HW_OCP		0x120000 /* IBM on-chip I2C adapter */

/* --- Broadcom SiByte adapters						*/
#define I2C_HW_SIBYTE		0x150000

/* --- SGI adapters							*/
#define I2C_HW_SGI_VINO		0x160000

/* --- XSCALE on-chip adapters                          */
#define I2C_HW_IOP3XX		0x140000

/* --- Au1550 PSC adapters adapters					*/
#define I2C_HW_AU1550_PSC	0x1b0000

/* --- SMBus only adapters						*/
#define I2C_HW_SMBUS_PIIX4	0x040000
#define I2C_HW_SMBUS_ALI15X3	0x040001
#define I2C_HW_SMBUS_VIA2	0x040002
#define I2C_HW_SMBUS_I801	0x040004
#define I2C_HW_SMBUS_AMD756	0x040005
#define I2C_HW_SMBUS_SIS5595	0x040006
#define I2C_HW_SMBUS_ALI1535	0x040007
#define I2C_HW_SMBUS_SIS630	0x040008
#define I2C_HW_SMBUS_SIS96X	0x040009
#define I2C_HW_SMBUS_AMD8111	0x04000a
#define I2C_HW_SMBUS_SCX200	0x04000b
#define I2C_HW_SMBUS_NFORCE2	0x04000c
#define I2C_HW_SMBUS_W9968CF	0x04000d
#define I2C_HW_SMBUS_OV511	0x04000e /* OV511(+) USB 1.1 webcam ICs */
#define I2C_HW_SMBUS_OV518	0x04000f /* OV518(+) USB 1.1 webcam ICs */
#define I2C_HW_SMBUS_OVFX2	0x040011 /* Cypress/OmniVision FX2 webcam */
#define I2C_HW_SMBUS_CAFE	0x040012 /* Marvell 88ALP01 "CAFE" cam  */
#define I2C_HW_SMBUS_ALI1563	0x040013

/* --- MCP107 adapter */
#define I2C_HW_MPC107		0x0d0000

/* --- Embedded adapters */
#define I2C_HW_MV64XXX		0x190000
#define I2C_HW_BLACKFIN		0x190001 /* ADI Blackfin I2C TWI driver */

/* --- Miscellaneous adapters */
#define I2C_HW_SAA7146		0x060000 /* SAA7146 video decoder bus */
#define I2C_HW_SAA7134		0x090000 /* SAA7134 video decoder bus */

#endif /* LINUX_I2C_ID_H */
