/* ------------------------------------------------------------------------- */
/* 									     */
/* i2c-id.h - identifier values for i2c drivers and adapters		     */
/* 									     */
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

/*
 * ---- Driver types -----------------------------------------------------
 *       device id name + number        function description, i2c address(es)
 *
 *  Range 1000-1999 range is defined in sensors/sensors.h 
 *  Range 0x100 - 0x1ff is for V4L2 Common Components 
 *  Range 0xf000 - 0xffff is reserved for local experimentation, and should
 *        never be used in official drivers 
 */

#define I2C_DRIVERID_MSP3400	 1
#define I2C_DRIVERID_TUNER	 2
#define I2C_DRIVERID_VIDEOTEX	 3	/* please rename		*/
#define I2C_DRIVERID_TDA8425	 4	/* stereo sound processor	*/
#define I2C_DRIVERID_TEA6420	 5	/* audio matrix switch		*/
#define I2C_DRIVERID_TEA6415C	 6	/* video matrix switch		*/
#define I2C_DRIVERID_TDA9840	 7	/* stereo sound processor	*/
#define I2C_DRIVERID_SAA7111A	 8	/* video input processor	*/
#define I2C_DRIVERID_SAA5281	 9	/* videotext decoder		*/
#define I2C_DRIVERID_SAA7112	10	/* video decoder, image scaler	*/
#define I2C_DRIVERID_SAA7120	11	/* video encoder		*/
#define I2C_DRIVERID_SAA7121	12	/* video encoder		*/
#define I2C_DRIVERID_SAA7185B	13	/* video encoder		*/
#define I2C_DRIVERID_CH7003	14	/* digital pc to tv encoder 	*/
#define I2C_DRIVERID_PCF8574A	15	/* i2c expander - 8 bit in/out	*/
#define I2C_DRIVERID_PCF8582C	16	/* eeprom			*/
#define I2C_DRIVERID_AT24Cxx	17	/* eeprom 1/2/4/8/16 K 		*/
#define I2C_DRIVERID_TEA6300	18	/* audio mixer			*/
#define I2C_DRIVERID_BT829	19	/* pc to tv encoder		*/
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
#define I2C_DRIVERID_DPL3518    30      /* Dolby decoder chip           */
#define I2C_DRIVERID_TDA9873    31      /* TV sound decoder chip        */
#define I2C_DRIVERID_TDA9875    32      /* TV sound decoder chip        */
#define I2C_DRIVERID_PIC16C54_PV9 33    /* Audio mux/ir receiver        */

#define I2C_DRIVERID_SBATT      34     /* Smart Battery Device		*/
#define I2C_DRIVERID_SBS        35     /* SB System Manager		*/
#define I2C_DRIVERID_VES1893	36     /* VLSI DVB-S decoder		*/
#define I2C_DRIVERID_VES1820	37     /* VLSI DVB-C decoder		*/
#define I2C_DRIVERID_SAA7113	38     /* video decoder			*/
#define I2C_DRIVERID_TDA8444	39     /* octuple 6-bit DAC             */
#define I2C_DRIVERID_BT819	40     /* video decoder			*/
#define I2C_DRIVERID_BT856	41     /* video encoder			*/
#define I2C_DRIVERID_VPX3220	42     /* video decoder+vbi/vtxt	*/
#define I2C_DRIVERID_DRP3510	43     /* ADR decoder (Astra Radio)	*/
#define I2C_DRIVERID_SP5055	44     /* Satellite tuner		*/
#define I2C_DRIVERID_STV0030	45     /* Multipurpose switch		*/
#define I2C_DRIVERID_SAA7108	46     /* video decoder, image scaler   */
#define I2C_DRIVERID_DS1307	47     /* DS1307 real time clock	*/
#define I2C_DRIVERID_ADV7175	48     /* ADV 7175/7176 video encoder	*/
#define I2C_DRIVERID_SAA7114	49	/* video decoder		*/
#define I2C_DRIVERID_ZR36120	50     /* Zoran 36120 video encoder	*/
#define I2C_DRIVERID_24LC32A	51	/* Microchip 24LC32A 32k EEPROM	*/
#define I2C_DRIVERID_STM41T00	52	/* real time clock		*/
#define I2C_DRIVERID_UDA1342	53	/* UDA1342 audio codec		*/
#define I2C_DRIVERID_ADV7170	54	/* video encoder		*/
#define I2C_DRIVERID_RADEON	55	/* I2C bus on Radeon boards	*/
#define I2C_DRIVERID_MAX1617	56	/* temp sensor			*/
#define I2C_DRIVERID_SAA7191	57	/* video encoder		*/
#define I2C_DRIVERID_INDYCAM	58	/* SGI IndyCam			*/
#define I2C_DRIVERID_BT832	59	/* CMOS camera video processor	*/
#define I2C_DRIVERID_TDA9887	60	/* TDA988x IF-PLL demodulator	*/
#define I2C_DRIVERID_OVCAMCHIP	61	/* OmniVision CMOS image sens.	*/
#define I2C_DRIVERID_TDA7313	62	/* TDA7313 audio processor	*/
#define I2C_DRIVERID_MAX6900	63	/* MAX6900 real-time clock	*/
#define I2C_DRIVERID_SAA7114H	64	/* video decoder		*/
#define I2C_DRIVERID_DS1374	65	/* DS1374 real time clock	*/


#define I2C_DRIVERID_EXP0	0xF0	/* experimental use id's	*/
#define I2C_DRIVERID_EXP1	0xF1
#define I2C_DRIVERID_EXP2	0xF2
#define I2C_DRIVERID_EXP3	0xF3

#define I2C_DRIVERID_I2CDEV	900
#define I2C_DRIVERID_I2CPROC	901
#define I2C_DRIVERID_ARP        902    /* SMBus ARP Client              */
#define I2C_DRIVERID_ALERT      903    /* SMBus Alert Responder Client  */

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
#define I2C_DRIVERID_SIS5595 1011
#define I2C_DRIVERID_ICSPLL 1012
#define I2C_DRIVERID_BT869 1013
#define I2C_DRIVERID_MAXILIFE 1014
#define I2C_DRIVERID_MATORB 1015
#define I2C_DRIVERID_GL520 1016
#define I2C_DRIVERID_THMC50 1017
#define I2C_DRIVERID_DDCMON 1018
#define I2C_DRIVERID_VIA686A 1019
#define I2C_DRIVERID_ADM1025 1020
#define I2C_DRIVERID_LM87 1021
#define I2C_DRIVERID_PCF8574 1022
#define I2C_DRIVERID_MTP008 1023
#define I2C_DRIVERID_DS1621 1024
#define I2C_DRIVERID_ADM1024 1025
#define I2C_DRIVERID_IT87 1026
#define I2C_DRIVERID_CH700X 1027 /* single driver for CH7003-7009 digital pc to tv encoders */
#define I2C_DRIVERID_FSCPOS 1028
#define I2C_DRIVERID_FSCSCY 1029
#define I2C_DRIVERID_PCF8591 1030
#define I2C_DRIVERID_SMSC47M1 1031
#define I2C_DRIVERID_VT1211 1032
#define I2C_DRIVERID_LM92 1033
#define I2C_DRIVERID_VT8231 1034
#define I2C_DRIVERID_SMARTBATT 1035
#define I2C_DRIVERID_BMCSENSORS 1036
#define I2C_DRIVERID_FS451 1037
#define I2C_DRIVERID_W83627HF 1038
#define I2C_DRIVERID_LM85 1039
#define I2C_DRIVERID_LM83 1040
#define I2C_DRIVERID_LM90 1042
#define I2C_DRIVERID_ASB100 1043
#define I2C_DRIVERID_FSCHER 1046
#define I2C_DRIVERID_W83L785TS 1047
#define I2C_DRIVERID_SMSC47B397 1050

/*
 * ---- Adapter types ----------------------------------------------------
 */

/* --- Bit algorithm adapters 						*/
#define I2C_HW_B_LP		0x010000 /* Parallel port Philips style */
#define I2C_HW_B_SER		0x010002 /* Serial line interface */
#define I2C_HW_B_BT848		0x010005 /* BT848 video boards */
#define I2C_HW_B_WNV		0x010006 /* Winnov Videums */
#define I2C_HW_B_VIA		0x010007 /* Via vt82c586b */
#define I2C_HW_B_HYDRA		0x010008 /* Apple Hydra Mac I/O */
#define I2C_HW_B_G400		0x010009 /* Matrox G400 */
#define I2C_HW_B_I810		0x01000a /* Intel I810 */
#define I2C_HW_B_VOO		0x01000b /* 3dfx Voodoo 3 / Banshee */
#define I2C_HW_B_PPORT		0x01000c /* Primitive parallel port adapter */
#define I2C_HW_B_SAVG		0x01000d /* Savage 4 */
#define I2C_HW_B_SCX200		0x01000e /* Nat'l Semi SCx200 I2C */
#define I2C_HW_B_RIVA		0x010010 /* Riva based graphics cards */
#define I2C_HW_B_IOC		0x010011 /* IOC bit-wiggling */
#define I2C_HW_B_TSUNA		0x010012 /* DEC Tsunami chipset */
#define I2C_HW_B_FRODO		0x010013 /* 2d3D SA-1110 Development Board */
#define I2C_HW_B_OMAHA		0x010014 /* Omaha I2C interface (ARM) */
#define I2C_HW_B_GUIDE		0x010015 /* Guide bit-basher */
#define I2C_HW_B_IXP2000	0x010016 /* GPIO on IXP2000 systems */
#define I2C_HW_B_IXP4XX		0x010017 /* GPIO on IXP4XX systems */
#define I2C_HW_B_S3VIA		0x010018 /* S3Via ProSavage adapter */
#define I2C_HW_B_ZR36067	0x010019 /* Zoran-36057/36067 based boards */
#define I2C_HW_B_PCILYNX	0x01001a /* TI PCILynx I2C adapter */
#define I2C_HW_B_CX2388x	0x01001b /* connexant 2388x based tv cards */
#define I2C_HW_B_NVIDIA		0x01001c /* nvidia framebuffer driver */
#define I2C_HW_B_SAVAGE		0x01001d /* savage framebuffer driver */
#define I2C_HW_B_RADEON		0x01001e /* radeon framebuffer driver */

/* --- PCF 8584 based algorithms					*/
#define I2C_HW_P_LP		0x020000 /* Parallel port interface */
#define I2C_HW_P_ISA		0x020001 /* generic ISA Bus inteface card */
#define I2C_HW_P_ELEK		0x020002 /* Elektor ISA Bus inteface card */

/* --- PCA 9564 based algorithms */
#define I2C_HW_A_ISA		0x1a0000 /* generic ISA Bus interface card */

/* --- ACPI Embedded controller algorithms                              */
#define I2C_HW_ACPI_EC          0x1f0000

/* --- MPC824x PowerPC adapters						*/
#define I2C_HW_MPC824X		0x100001 /* Motorola 8240 / 8245 */

/* --- MPC8xx PowerPC adapters						*/
#define I2C_HW_MPC8XX_EPON	0x110000 /* Eponymous MPC8xx I2C adapter */

/* --- ITE based algorithms						*/
#define I2C_HW_I_IIC		0x080000 /* controller on the ITE */

/* --- PowerPC on-chip adapters						*/
#define I2C_HW_OCP		0x120000 /* IBM on-chip I2C adapter */

/* --- Broadcom SiByte adapters						*/
#define I2C_HW_SIBYTE		0x150000

/* --- SGI adapters							*/
#define I2C_HW_SGI_VINO		0x160000
#define I2C_HW_SGI_MACE		0x160001

/* --- XSCALE on-chip adapters                          */
#define I2C_HW_IOP3XX		0x140000

/* --- Au1550 PSC adapters adapters					*/
#define I2C_HW_AU1550_PSC	0x1b0000

/* --- SMBus only adapters						*/
#define I2C_HW_SMBUS_PIIX4	0x040000
#define I2C_HW_SMBUS_ALI15X3	0x040001
#define I2C_HW_SMBUS_VIA2	0x040002
#define I2C_HW_SMBUS_VOODOO3	0x040003
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
#define I2C_HW_SMBUS_OV519	0x040010 /* OV519 USB 1.1 webcam IC */
#define I2C_HW_SMBUS_OVFX2	0x040011 /* Cypress/OmniVision FX2 webcam */

/* --- ISA pseudo-adapter						*/
#define I2C_HW_ISA		0x050000

/* --- IPMI pseudo-adapter						*/
#define I2C_HW_IPMI		0x0b0000

/* --- IPMB adapter						*/
#define I2C_HW_IPMB		0x0c0000

/* --- MCP107 adapter */
#define I2C_HW_MPC107		0x0d0000

/* --- Marvell mv64xxx i2c adapter */
#define I2C_HW_MV64XXX		0x190000

/* --- Miscellaneous adapters */
#define I2C_HW_SAA7146		0x060000 /* SAA7146 video decoder bus */
#define I2C_HW_SAA7134		0x090000 /* SAA7134 video decoder bus */

#endif /* LINUX_I2C_ID_H */
