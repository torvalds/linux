/**
 * MPC86xx Internal Memory Map
 *
 * Authors: Jeff Brown
 *          Timur Tabi <timur@freescale.com>
 *
 * Copyright 2004,2007 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This header file defines structures for various 86xx SOC devices that are
 * used by multiple source files.
 */

#ifndef __ASM_POWERPC_IMMAP_86XX_H__
#define __ASM_POWERPC_IMMAP_86XX_H__
#ifdef __KERNEL__

/* Global Utility Registers */
struct ccsr_guts {
	__be32	porpllsr;	/* 0x.0000 - POR PLL Ratio Status Register */
	__be32	porbmsr;	/* 0x.0004 - POR Boot Mode Status Register */
	__be32	porimpscr;	/* 0x.0008 - POR I/O Impedance Status and Control Register */
	__be32	pordevsr;	/* 0x.000c - POR I/O Device Status Register */
	__be32	pordbgmsr;	/* 0x.0010 - POR Debug Mode Status Register */
	u8	res1[0x20 - 0x14];
	__be32	porcir;		/* 0x.0020 - POR Configuration Information Register */
	u8	res2[0x30 - 0x24];
	__be32	gpiocr;		/* 0x.0030 - GPIO Control Register */
	u8	res3[0x40 - 0x34];
	__be32	gpoutdr;	/* 0x.0040 - General-Purpose Output Data Register */
	u8	res4[0x50 - 0x44];
	__be32	gpindr;		/* 0x.0050 - General-Purpose Input Data Register */
	u8	res5[0x60 - 0x54];
	__be32	pmuxcr;		/* 0x.0060 - Alternate Function Signal Multiplex Control */
	u8	res6[0x70 - 0x64];
	__be32	devdisr;	/* 0x.0070 - Device Disable Control */
	u8	res7[0x80 - 0x74];
	__be32	powmgtcsr;	/* 0x.0080 - Power Management Status and Control Register */
	u8	res8[0x90 - 0x84];
	__be32	mcpsumr;	/* 0x.0090 - Machine Check Summary Register */
	__be32	rstrscr;	/* 0x.0094 - Reset Request Status and Control Register */
	u8	res9[0xA0 - 0x98];
	__be32	pvr;		/* 0x.00a0 - Processor Version Register */
	__be32	svr;		/* 0x.00a4 - System Version Register */
	u8	res10[0xB0 - 0xA8];
	__be32	rstcr;		/* 0x.00b0 - Reset Control Register */
	u8	res11[0xB20 - 0xB4];
	__be32	ddr1clkdr;	/* 0x.0b20 - DDRC1 Clock Disable Register */
	__be32	ddr2clkdr;	/* 0x.0b24 - DDRC2 Clock Disable Register */
	u8	res12[0xE00 - 0xB28];
	__be32	clkocr;		/* 0x.0e00 - Clock Out Select Register */
	u8	res13[0xF04 - 0xE04];
	__be32	srds1cr0;	/* 0x.0f04 - SerDes1 Control Register 0 */
	__be32	srds1cr1;	/* 0x.0f08 - SerDes1 Control Register 0 */
	u8	res14[0xF40 - 0xF0C];
	__be32	srds2cr0;	/* 0x.0f40 - SerDes1 Control Register 0 */
	__be32	srds2cr1;	/* 0x.0f44 - SerDes1 Control Register 0 */
};

#endif /* __ASM_POWERPC_IMMAP_86XX_H__ */
#endif /* __KERNEL__ */
