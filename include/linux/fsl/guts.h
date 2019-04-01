/**
 * Freecale 85xx and 86xx Global Utilties register set
 *
 * Authors: Jeff Brown
 *          Timur Tabi <timur@freescale.com>
 *
 * Copyright 2004,2007,2012 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __FSL_GUTS_H__
#define __FSL_GUTS_H__

#include <linux/types.h>
#include <linux/io.h>

/**
 * Global Utility Registers.
 *
 * Not all registers defined in this structure are available on all chips, so
 * you are expected to know whether a given register actually exists on your
 * chip before you access it.
 *
 * Also, some registers are similar on different chips but have slightly
 * different names.  In these cases, one name is chosen to avoid extraneous
 * #ifdefs.
 */
struct ccsr_guts {
	u32	porpllsr;	/* 0x.0000 - POR PLL Ratio Status Register */
	u32	porbmsr;	/* 0x.0004 - POR Boot Mode Status Register */
	u32	porimpscr;	/* 0x.0008 - POR I/O Impedance Status and
				 *           Control Register
				 */
	u32	pordevsr;	/* 0x.000c - POR I/O Device Status Register */
	u32	pordbgmsr;	/* 0x.0010 - POR Debug Mode Status Register */
	u32	pordevsr2;	/* 0x.0014 - POR device status register 2 */
	u8	res018[0x20 - 0x18];
	u32	porcir;		/* 0x.0020 - POR Configuration Information
				 *           Register
				 */
	u8	res024[0x30 - 0x24];
	u32	gpiocr;		/* 0x.0030 - GPIO Control Register */
	u8	res034[0x40 - 0x34];
	u32	gpoutdr;	/* 0x.0040 - General-Purpose Output Data
				 *           Register
				 */
	u8	res044[0x50 - 0x44];
	u32	gpindr;		/* 0x.0050 - General-Purpose Input Data
				 *           Register
				 */
	u8	res054[0x60 - 0x54];
	u32	pmuxcr;		/* 0x.0060 - Alternate Function Signal
				 *           Multiplex Control
				 */
	u32	pmuxcr2;	/* 0x.0064 - Alternate function signal
				 *           multiplex control 2
				 */
	u32	dmuxcr;		/* 0x.0068 - DMA Mux Control Register */
        u8	res06c[0x70 - 0x6c];
	u32	devdisr;	/* 0x.0070 - Device Disable Control */
#define CCSR_GUTS_DEVDISR_TB1	0x00001000
#define CCSR_GUTS_DEVDISR_TB0	0x00004000
	u32	devdisr2;	/* 0x.0074 - Device Disable Control 2 */
	u8	res078[0x7c - 0x78];
	u32	pmjcr;		/* 0x.007c - 4 Power Management Jog Control
				 *           Register
				 */
	u32	powmgtcsr;	/* 0x.0080 - Power Management Status and
				 *           Control Register
				 */
	u32	pmrccr;		/* 0x.0084 - Power Management Reset Counter
				 *           Configuration Register
				 */
	u32	pmpdccr;	/* 0x.0088 - Power Management Power Down Counter
				 *           Configuration Register
				 */
	u32	pmcdr;		/* 0x.008c - 4Power management clock disable
				 *           register
				 */
	u32	mcpsumr;	/* 0x.0090 - Machine Check Summary Register */
	u32	rstrscr;	/* 0x.0094 - Reset Request Status and
				 *           Control Register
				 */
	u32	ectrstcr;	/* 0x.0098 - Exception reset control register */
	u32	autorstsr;	/* 0x.009c - Automatic reset status register */
	u32	pvr;		/* 0x.00a0 - Processor Version Register */
	u32	svr;		/* 0x.00a4 - System Version Register */
	u8	res0a8[0xb0 - 0xa8];
	u32	rstcr;		/* 0x.00b0 - Reset Control Register */
	u8	res0b4[0xc0 - 0xb4];
	u32	iovselsr;	/* 0x.00c0 - I/O voltage select status register
					     Called 'elbcvselcr' on 86xx SOCs */
	u8	res0c4[0x100 - 0xc4];
	u32	rcwsr[16];	/* 0x.0100 - Reset Control Word Status registers
					     There are 16 registers */
	u8	res140[0x224 - 0x140];
	u32	iodelay1;	/* 0x.0224 - IO delay control register 1 */
	u32	iodelay2;	/* 0x.0228 - IO delay control register 2 */
	u8	res22c[0x604 - 0x22c];
	u32	pamubypenr;	/* 0x.604 - PAMU bypass enable register */
	u8	res608[0x800 - 0x608];
	u32	clkdvdr;	/* 0x.0800 - Clock Divide Register */
	u8	res804[0x900 - 0x804];
	u32	ircr;		/* 0x.0900 - Infrared Control Register */
	u8	res904[0x908 - 0x904];
	u32	dmacr;		/* 0x.0908 - DMA Control Register */
	u8	res90c[0x914 - 0x90c];
	u32	elbccr;		/* 0x.0914 - eLBC Control Register */
	u8	res918[0xb20 - 0x918];
	u32	ddr1clkdr;	/* 0x.0b20 - DDR1 Clock Disable Register */
	u32	ddr2clkdr;	/* 0x.0b24 - DDR2 Clock Disable Register */
	u32	ddrclkdr;	/* 0x.0b28 - DDR Clock Disable Register */
	u8	resb2c[0xe00 - 0xb2c];
	u32	clkocr;		/* 0x.0e00 - Clock Out Select Register */
	u8	rese04[0xe10 - 0xe04];
	u32	ddrdllcr;	/* 0x.0e10 - DDR DLL Control Register */
	u8	rese14[0xe20 - 0xe14];
	u32	lbcdllcr;	/* 0x.0e20 - LBC DLL Control Register */
	u32	cpfor;		/* 0x.0e24 - L2 charge pump fuse override
				 *           register
				 */
	u8	rese28[0xf04 - 0xe28];
	u32	srds1cr0;	/* 0x.0f04 - SerDes1 Control Register 0 */
	u32	srds1cr1;	/* 0x.0f08 - SerDes1 Control Register 0 */
	u8	resf0c[0xf2c - 0xf0c];
	u32	itcr;		/* 0x.0f2c - Internal transaction control
				 *           register
				 */
	u8	resf30[0xf40 - 0xf30];
	u32	srds2cr0;	/* 0x.0f40 - SerDes2 Control Register 0 */
	u32	srds2cr1;	/* 0x.0f44 - SerDes2 Control Register 0 */
} __attribute__ ((packed));

/* Alternate function signal multiplex control */
#define MPC85xx_PMUXCR_QE(x) (0x8000 >> (x))

#ifdef CONFIG_PPC_86xx

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
 * co: The DMA controller (0 or 1)
 * ch: The channel on the DMA controller (0, 1, 2, or 3)
 * device: The device to set as the source (CCSR_GUTS_DMACR_DEV_xx)
 */
static inline void guts_set_dmacr(struct ccsr_guts __iomem *guts,
	unsigned int co, unsigned int ch, unsigned int device)
{
	unsigned int shift = 16 + (8 * (1 - co) + 2 * (3 - ch));

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

/*
 * Set the DMA external control bits in the GUTS
 *
 * The DMA external control bits in the PMUXCR are only meaningful for
 * channels 0 and 3.  Any other channels are ignored.
 *
 * guts: Pointer to GUTS structure
 * co: The DMA controller (0 or 1)
 * ch: The channel on the DMA controller (0, 1, 2, or 3)
 * value: the new value for the bit (0 or 1)
 */
static inline void guts_set_pmuxcr_dma(struct ccsr_guts __iomem *guts,
	unsigned int co, unsigned int ch, unsigned int value)
{
	if ((ch == 0) || (ch == 3)) {
		unsigned int shift = 2 * (co + 1) - (ch & 1) - 1;

		clrsetbits_be32(&guts->pmuxcr, 1 << shift, value << shift);
	}
}

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

#endif

struct ccsr_rcpm_v1 {
	u8	res0000[4];
	__be32	cdozsr;	    /* 0x0004 Core Doze Status Register */
	u8	res0008[4];
	__be32	cdozcr;	    /* 0x000c Core Doze Control Register */
	u8	res0010[4];
	__be32	cnapsr;	    /* 0x0014 Core Nap Status Register */
	u8	res0018[4];
	__be32	cnapcr;	    /* 0x001c Core Nap Control Register */
	u8	res0020[4];
	__be32	cdozpsr;    /* 0x0024 Core Doze Previous Status Register */
	u8	res0028[4];
	__be32	cnappsr;    /* 0x002c Core Nap Previous Status Register */
	u8	res0030[4];
	__be32	cwaitsr;    /* 0x0034 Core Wait Status Register */
	u8	res0038[4];
	__be32	cwdtdsr;    /* 0x003c Core Watchdog Detect Status Register */
	__be32	powmgtcsr;  /* 0x0040 PM Control&Status Register */
#define RCPM_POWMGTCSR_SLP	0x00020000
	u8	res0044[12];
	__be32	ippdexpcr;  /* 0x0050 IP Powerdown Exception Control Register */
	u8	res0054[16];
	__be32	cpmimr;	    /* 0x0064 Core PM IRQ Mask Register */
	u8	res0068[4];
	__be32	cpmcimr;    /* 0x006c Core PM Critical IRQ Mask Register */
	u8	res0070[4];
	__be32	cpmmcmr;    /* 0x0074 Core PM Machine Check Mask Register */
	u8	res0078[4];
	__be32	cpmnmimr;   /* 0x007c Core PM NMI Mask Register */
	u8	res0080[4];
	__be32	ctbenr;	    /* 0x0084 Core Time Base Enable Register */
	u8	res0088[4];
	__be32	ctbckselr;  /* 0x008c Core Time Base Clock Select Register */
	u8	res0090[4];
	__be32	ctbhltcr;   /* 0x0094 Core Time Base Halt Control Register */
	u8	res0098[4];
	__be32	cmcpmaskcr; /* 0x00a4 Core Machine Check Mask Register */
};

struct ccsr_rcpm_v2 {
	u8	res_00[12];
	__be32	tph10sr0;	/* Thread PH10 Status Register */
	u8	res_10[12];
	__be32	tph10setr0;	/* Thread PH10 Set Control Register */
	u8	res_20[12];
	__be32	tph10clrr0;	/* Thread PH10 Clear Control Register */
	u8	res_30[12];
	__be32	tph10psr0;	/* Thread PH10 Previous Status Register */
	u8	res_40[12];
	__be32	twaitsr0;	/* Thread Wait Status Register */
	u8	res_50[96];
	__be32	pcph15sr;	/* Physical Core PH15 Status Register */
	__be32	pcph15setr;	/* Physical Core PH15 Set Control Register */
	__be32	pcph15clrr;	/* Physical Core PH15 Clear Control Register */
	__be32	pcph15psr;	/* Physical Core PH15 Prev Status Register */
	u8	res_c0[16];
	__be32	pcph20sr;	/* Physical Core PH20 Status Register */
	__be32	pcph20setr;	/* Physical Core PH20 Set Control Register */
	__be32	pcph20clrr;	/* Physical Core PH20 Clear Control Register */
	__be32	pcph20psr;	/* Physical Core PH20 Prev Status Register */
	__be32	pcpw20sr;	/* Physical Core PW20 Status Register */
	u8	res_e0[12];
	__be32	pcph30sr;	/* Physical Core PH30 Status Register */
	__be32	pcph30setr;	/* Physical Core PH30 Set Control Register */
	__be32	pcph30clrr;	/* Physical Core PH30 Clear Control Register */
	__be32	pcph30psr;	/* Physical Core PH30 Prev Status Register */
	u8	res_100[32];
	__be32	ippwrgatecr;	/* IP Power Gating Control Register */
	u8	res_124[12];
	__be32	powmgtcsr;	/* Power Management Control & Status Reg */
#define RCPM_POWMGTCSR_LPM20_RQ		0x00100000
#define RCPM_POWMGTCSR_LPM20_ST		0x00000200
#define RCPM_POWMGTCSR_P_LPM20_ST	0x00000100
	u8	res_134[12];
	__be32	ippdexpcr[4];	/* IP Powerdown Exception Control Reg */
	u8	res_150[12];
	__be32	tpmimr0;	/* Thread PM Interrupt Mask Reg */
	u8	res_160[12];
	__be32	tpmcimr0;	/* Thread PM Crit Interrupt Mask Reg */
	u8	res_170[12];
	__be32	tpmmcmr0;	/* Thread PM Machine Check Interrupt Mask Reg */
	u8	res_180[12];
	__be32	tpmnmimr0;	/* Thread PM NMI Mask Reg */
	u8	res_190[12];
	__be32	tmcpmaskcr0;	/* Thread Machine Check Mask Control Reg */
	__be32	pctbenr;	/* Physical Core Time Base Enable Reg */
	__be32	pctbclkselr;	/* Physical Core Time Base Clock Select */
	__be32	tbclkdivr;	/* Time Base Clock Divider Register */
	u8	res_1ac[4];
	__be32	ttbhltcr[4];	/* Thread Time Base Halt Control Register */
	__be32	clpcl10sr;	/* Cluster PCL10 Status Register */
	__be32	clpcl10setr;	/* Cluster PCL30 Set Control Register */
	__be32	clpcl10clrr;	/* Cluster PCL30 Clear Control Register */
	__be32	clpcl10psr;	/* Cluster PCL30 Prev Status Register */
	__be32	cddslpsetr;	/* Core Domain Deep Sleep Set Register */
	__be32	cddslpclrr;	/* Core Domain Deep Sleep Clear Register */
	__be32	cdpwroksetr;	/* Core Domain Power OK Set Register */
	__be32	cdpwrokclrr;	/* Core Domain Power OK Clear Register */
	__be32	cdpwrensr;	/* Core Domain Power Enable Status Register */
	__be32	cddslsr;	/* Core Domain Deep Sleep Status Register */
	u8	res_1e8[8];
	__be32	dslpcntcr[8];	/* Deep Sleep Counter Cfg Register */
	u8	res_300[3568];
};

#endif
