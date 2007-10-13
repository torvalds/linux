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
	__be32	devdisr2;	/* 0x.0074 - Device Disable Control 2 */
	u8	res7[0x80 - 0x78];
	__be32	powmgtcsr;	/* 0x.0080 - Power Management Status and Control Register */
	u8	res8[0x90 - 0x84];
	__be32	mcpsumr;	/* 0x.0090 - Machine Check Summary Register */
	__be32	rstrscr;	/* 0x.0094 - Reset Request Status and Control Register */
	u8	res9[0xA0 - 0x98];
	__be32	pvr;		/* 0x.00a0 - Processor Version Register */
	__be32	svr;		/* 0x.00a4 - System Version Register */
	u8	res10[0xB0 - 0xA8];
	__be32	rstcr;		/* 0x.00b0 - Reset Control Register */
	u8	res11[0xC0 - 0xB4];
	__be32	elbcvselcr;	/* 0x.00c0 - eLBC Voltage Select Ctrl Reg */
	u8	res12[0x800 - 0xC4];
	__be32	clkdvdr;	/* 0x.0800 - Clock Divide Register */
	u8	res13[0x900 - 0x804];
	__be32	ircr;		/* 0x.0900 - Infrared Control Register */
	u8	res14[0x908 - 0x904];
	__be32	dmacr;		/* 0x.0908 - DMA Control Register */
	u8	res15[0x914 - 0x90C];
	__be32	elbccr;		/* 0x.0914 - eLBC Control Register */
	u8	res16[0xB20 - 0x918];
	__be32	ddr1clkdr;	/* 0x.0b20 - DDR1 Clock Disable Register */
	__be32	ddr2clkdr;	/* 0x.0b24 - DDR2 Clock Disable Register */
	__be32	ddrclkdr;	/* 0x.0b28 - DDR Clock Disable Register */
	u8	res17[0xE00 - 0xB2C];
	__be32	clkocr;		/* 0x.0e00 - Clock Out Select Register */
	u8	res18[0xE10 - 0xE04];
	__be32	ddrdllcr;	/* 0x.0e10 - DDR DLL Control Register */
	u8	res19[0xE20 - 0xE14];
	__be32	lbcdllcr;	/* 0x.0e20 - LBC DLL Control Register */
	u8	res20[0xF04 - 0xE24];
	__be32	srds1cr0;	/* 0x.0f04 - SerDes1 Control Register 0 */
	__be32	srds1cr1;	/* 0x.0f08 - SerDes1 Control Register 0 */
	u8	res21[0xF40 - 0xF0C];
	__be32	srds2cr0;	/* 0x.0f40 - SerDes1 Control Register 0 */
	__be32	srds2cr1;	/* 0x.0f44 - SerDes1 Control Register 0 */
} __attribute__ ((packed));

#define CCSR_GUTS_DMACR_DEV_SSI	0	/* DMA controller/channel set to SSI */
#define CCSR_GUTS_DMACR_DEV_IR	1	/* DMA controller/channel set to IR */

/*
 * Set the DMACR register in the GUTS
 *
 * The DMACR register determines the source of initiated transfers for each
 * channel on each DMA controller.  Rather than have a bunch of repetitive
 * macros for the bit patterns, we just have a function that calculates
 * them.
 *
 * guts: Pointer to GUTS structure
 * co: The DMA controller (1 or 2)
 * ch: The channel on the DMA controller (0, 1, 2, or 3)
 * device: The device to set as the source (CCSR_GUTS_DMACR_DEV_xx)
 */
static inline void guts_set_dmacr(struct ccsr_guts __iomem *guts,
	unsigned int co, unsigned int ch, unsigned int device)
{
	unsigned int shift = 16 + (8 * (2 - co) + 2 * (3 - ch));

	clrsetbits_be32(&guts->dmacr, 3 << shift, device << shift);
}

#define CCSR_GUTS_PMUXCR_LDPSEL		0x00010000
#define CCSR_GUTS_PMUXCR_SSI1_MASK	0x0000C000	/* Bitmask for SSI1 */
#define CCSR_GUTS_PMUXCR_SSI1_LA	0x00000000	/* Latched address */
#define CCSR_GUTS_PMUXCR_SSI1_HI	0x00004000	/* High impedance */
#define CCSR_GUTS_PMUXCR_SSI1_SSI	0x00008000	/* Used for SSI1 */
#define CCSR_GUTS_PMUXCR_SSI2_MASK	0x00003000	/* Bitmask for SSI2 */
#define CCSR_GUTS_PMUXCR_SSI2_LA	0x00000000	/* Latched address */
#define CCSR_GUTS_PMUXCR_SSI2_HI	0x00001000	/* High impedance */
#define CCSR_GUTS_PMUXCR_SSI2_SSI	0x00002000	/* Used for SSI2 */
#define CCSR_GUTS_PMUXCR_LA_22_25_LA	0x00000000	/* Latched Address */
#define CCSR_GUTS_PMUXCR_LA_22_25_HI	0x00000400	/* High impedance */
#define CCSR_GUTS_PMUXCR_DBGDRV		0x00000200	/* Signals not driven */
#define CCSR_GUTS_PMUXCR_DMA2_0		0x00000008
#define CCSR_GUTS_PMUXCR_DMA2_3		0x00000004
#define CCSR_GUTS_PMUXCR_DMA1_0		0x00000002
#define CCSR_GUTS_PMUXCR_DMA1_3		0x00000001

#define CCSR_GUTS_CLKDVDR_PXCKEN	0x80000000
#define CCSR_GUTS_CLKDVDR_SSICKEN	0x20000000
#define CCSR_GUTS_CLKDVDR_PXCKINV	0x10000000
#define CCSR_GUTS_CLKDVDR_PXCKDLY_SHIFT 25
#define CCSR_GUTS_CLKDVDR_PXCKDLY_MASK	0x06000000
#define CCSR_GUTS_CLKDVDR_PXCKDLY(x) \
	(((x) & 3) << CCSR_GUTS_CLKDVDR_PXCKDLY_SHIFT)
#define CCSR_GUTS_CLKDVDR_PXCLK_SHIFT	16
#define CCSR_GUTS_CLKDVDR_PXCLK_MASK	0x001F0000
#define CCSR_GUTS_CLKDVDR_PXCLK(x) (((x) & 31) << CCSR_GUTS_CLKDVDR_PXCLK_SHIFT)
#define CCSR_GUTS_CLKDVDR_SSICLK_MASK	0x000000FF
#define CCSR_GUTS_CLKDVDR_SSICLK(x) ((x) & CCSR_GUTS_CLKDVDR_SSICLK_MASK)

#endif /* __ASM_POWERPC_IMMAP_86XX_H__ */
#endif /* __KERNEL__ */
