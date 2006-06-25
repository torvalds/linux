/*
 * MPC86xx Internal Memory Map
 *
 * Author: Jeff Brown
 *
 * Copyright 2004 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __ASM_POWERPC_IMMAP_86XX_H__
#define __ASM_POWERPC_IMMAP_86XX_H__
#ifdef __KERNEL__

/* Eventually this should define all the IO block registers in 86xx */

/* PCI Registers */
typedef struct ccsr_pci {
	uint	cfg_addr;	/* 0x.000 - PCI Configuration Address Register */
	uint	cfg_data;	/* 0x.004 - PCI Configuration Data Register */
	uint	int_ack;	/* 0x.008 - PCI Interrupt Acknowledge Register */
	char	res1[3060];
	uint	potar0;		/* 0x.c00 - PCI Outbound Transaction Address Register 0 */
	uint	potear0;	/* 0x.c04 - PCI Outbound Translation Extended Address Register 0 */
	uint	powbar0;	/* 0x.c08 - PCI Outbound Window Base Address Register 0 */
	char	res2[4];
	uint	powar0;		/* 0x.c10 - PCI Outbound Window Attributes Register 0 */
	char	res3[12];
	uint	potar1;		/* 0x.c20 - PCI Outbound Transaction Address Register 1 */
	uint	potear1;	/* 0x.c24 - PCI Outbound Translation Extended Address Register 1 */
	uint	powbar1;	/* 0x.c28 - PCI Outbound Window Base Address Register 1 */
	char	res4[4];
	uint	powar1;		/* 0x.c30 - PCI Outbound Window Attributes Register 1 */
	char	res5[12];
	uint	potar2;		/* 0x.c40 - PCI Outbound Transaction Address Register 2 */
	uint	potear2;	/* 0x.c44 - PCI Outbound Translation Extended Address Register 2 */
	uint	powbar2;	/* 0x.c48 - PCI Outbound Window Base Address Register 2 */
	char	res6[4];
	uint	powar2;		/* 0x.c50 - PCI Outbound Window Attributes Register 2 */
	char	res7[12];
	uint	potar3;		/* 0x.c60 - PCI Outbound Transaction Address Register 3 */
	uint	potear3;	/* 0x.c64 - PCI Outbound Translation Extended Address Register 3 */
	uint	powbar3;	/* 0x.c68 - PCI Outbound Window Base Address Register 3 */
	char	res8[4];
	uint	powar3;		/* 0x.c70 - PCI Outbound Window Attributes Register 3 */
	char	res9[12];
	uint	potar4;		/* 0x.c80 - PCI Outbound Transaction Address Register 4 */
	uint	potear4;	/* 0x.c84 - PCI Outbound Translation Extended Address Register 4 */
	uint	powbar4;	/* 0x.c88 - PCI Outbound Window Base Address Register 4 */
	char	res10[4];
	uint	powar4;		/* 0x.c90 - PCI Outbound Window Attributes Register 4 */
	char	res11[268];
	uint	pitar3;		/* 0x.da0 - PCI Inbound Translation Address Register 3  */
	char	res12[4];
	uint	piwbar3;	/* 0x.da8 - PCI Inbound Window Base Address Register 3 */
	uint	piwbear3;	/* 0x.dac - PCI Inbound Window Base Extended Address Register 3 */
	uint	piwar3;		/* 0x.db0 - PCI Inbound Window Attributes Register 3 */
	char	res13[12];
	uint	pitar2;		/* 0x.dc0 - PCI Inbound Translation Address Register 2  */
	char	res14[4];
	uint	piwbar2;	/* 0x.dc8 - PCI Inbound Window Base Address Register 2 */
	uint	piwbear2;	/* 0x.dcc - PCI Inbound Window Base Extended Address Register 2 */
	uint	piwar2;		/* 0x.dd0 - PCI Inbound Window Attributes Register 2 */
	char	res15[12];
	uint	pitar1;		/* 0x.de0 - PCI Inbound Translation Address Register 1  */
	char	res16[4];
	uint	piwbar1;	/* 0x.de8 - PCI Inbound Window Base Address Register 1 */
	char	res17[4];
	uint	piwar1;		/* 0x.df0 - PCI Inbound Window Attributes Register 1 */
	char	res18[12];
	uint	err_dr;		/* 0x.e00 - PCI Error Detect Register */
	uint	err_cap_dr;	/* 0x.e04 - PCI Error Capture Disable Register */
	uint	err_en;		/* 0x.e08 - PCI Error Enable Register */
	uint	err_attrib;	/* 0x.e0c - PCI Error Attributes Capture Register */
	uint	err_addr;	/* 0x.e10 - PCI Error Address Capture Register */
	uint	err_ext_addr;	/* 0x.e14 - PCI Error Extended Address Capture Register */
	uint	err_dl;		/* 0x.e18 - PCI Error Data Low Capture Register */
	uint	err_dh;		/* 0x.e1c - PCI Error Data High Capture Register */
	uint	gas_timr;	/* 0x.e20 - PCI Gasket Timer Register */
	uint	pci_timr;	/* 0x.e24 - PCI Timer Register */
	char	res19[472];
} ccsr_pci_t;

/* PCI Express Registers */
typedef struct ccsr_pex {
	uint    pex_config_addr;        /* 0x.000 - PCI Express Configuration Address Register */
	uint    pex_config_data;        /* 0x.004 - PCI Express Configuration Data Register */
	char    res1[4];
	uint    pex_otb_cpl_tor;        /* 0x.00c - PCI Express Outbound completion timeout register */
	uint    pex_conf_tor;           /* 0x.010 - PCI Express configuration timeout register */
	char    res2[12];
	uint    pex_pme_mes_dr;         /* 0x.020 - PCI Express PME and message detect register */
	uint    pex_pme_mes_disr;       /* 0x.024 - PCI Express PME and message disable register */
	uint    pex_pme_mes_ier;        /* 0x.028 - PCI Express PME and message interrupt enable register */
	uint    pex_pmcr;               /* 0x.02c - PCI Express power management command register */
	char    res3[3024];
	uint    pexotar0;               /* 0x.c00 - PCI Express outbound translation address register 0 */
	uint    pexotear0;              /* 0x.c04 - PCI Express outbound translation extended address register 0*/
	char    res4[8];
	uint    pexowar0;               /* 0x.c10 - PCI Express outbound window attributes register 0*/
	char    res5[12];
	uint    pexotar1;               /* 0x.c20 - PCI Express outbound translation address register 1 */
	uint    pexotear1;              /* 0x.c24 - PCI Express outbound translation extended address register 1*/
	uint    pexowbar1;              /* 0x.c28 - PCI Express outbound window base address register 1*/
	char    res6[4];
	uint    pexowar1;               /* 0x.c30 - PCI Express outbound window attributes register 1*/
	char    res7[12];
	uint    pexotar2;               /* 0x.c40 - PCI Express outbound translation address register 2 */
	uint    pexotear2;              /* 0x.c44 - PCI Express outbound translation extended address register 2*/
	uint    pexowbar2;              /* 0x.c48 - PCI Express outbound window base address register 2*/
	char    res8[4];
	uint    pexowar2;               /* 0x.c50 - PCI Express outbound window attributes register 2*/
	char    res9[12];
	uint    pexotar3;               /* 0x.c60 - PCI Express outbound translation address register 3 */
	uint    pexotear3;              /* 0x.c64 - PCI Express outbound translation extended address register 3*/
	uint    pexowbar3;              /* 0x.c68 - PCI Express outbound window base address register 3*/
	char    res10[4];
	uint    pexowar3;               /* 0x.c70 - PCI Express outbound window attributes register 3*/
	char    res11[12];
	uint    pexotar4;               /* 0x.c80 - PCI Express outbound translation address register 4 */
	uint    pexotear4;              /* 0x.c84 - PCI Express outbound translation extended address register 4*/
	uint    pexowbar4;              /* 0x.c88 - PCI Express outbound window base address register 4*/
	char    res12[4];
	uint    pexowar4;               /* 0x.c90 - PCI Express outbound window attributes register 4*/
	char    res13[12];
	char    res14[256];
	uint    pexitar3;               /* 0x.da0 - PCI Express inbound translation address register 3 */
	char    res15[4];
	uint    pexiwbar3;              /* 0x.da8 - PCI Express inbound window base address register 3 */
	uint    pexiwbear3;             /* 0x.dac - PCI Express inbound window base extended address register 3 */
	uint    pexiwar3;               /* 0x.db0 - PCI Express inbound window attributes register 3 */
	char    res16[12];
	uint    pexitar2;               /* 0x.dc0 - PCI Express inbound translation address register 2 */
	char    res17[4];
	uint    pexiwbar2;              /* 0x.dc8 - PCI Express inbound window base address register 2 */
	uint    pexiwbear2;             /* 0x.dcc - PCI Express inbound window base extended address register 2 */
	uint    pexiwar2;               /* 0x.dd0 - PCI Express inbound window attributes register 2 */
	char    res18[12];
	uint    pexitar1;               /* 0x.de0 - PCI Express inbound translation address register 2 */
	char    res19[4];
	uint    pexiwbar1;              /* 0x.de8 - PCI Express inbound window base address register 2 */
	uint    pexiwbear1;             /* 0x.dec - PCI Express inbound window base extended address register 2 */
	uint    pexiwar1;               /* 0x.df0 - PCI Express inbound window attributes register 2 */
	char    res20[12];
	uint    pex_err_dr;             /* 0x.e00 - PCI Express error detect register */
	char    res21[4];
	uint    pex_err_en;             /* 0x.e08 - PCI Express error interrupt enable register */
	char    res22[4];
	uint    pex_err_disr;           /* 0x.e10 - PCI Express error disable register */
	char    res23[12];
	uint    pex_err_cap_stat;       /* 0x.e20 - PCI Express error capture status register */
	char    res24[4];
	uint    pex_err_cap_r0;         /* 0x.e28 - PCI Express error capture register 0 */
	uint    pex_err_cap_r1;         /* 0x.e2c - PCI Express error capture register 0 */
	uint    pex_err_cap_r2;         /* 0x.e30 - PCI Express error capture register 0 */
	uint    pex_err_cap_r3;         /* 0x.e34 - PCI Express error capture register 0 */
} ccsr_pex_t;

/* Global Utility Registers */
typedef struct ccsr_guts {
	uint	porpllsr;	/* 0x.0000 - POR PLL Ratio Status Register */
	uint	porbmsr;	/* 0x.0004 - POR Boot Mode Status Register */
	uint	porimpscr;	/* 0x.0008 - POR I/O Impedance Status and Control Register */
	uint	pordevsr;	/* 0x.000c - POR I/O Device Status Register */
	uint	pordbgmsr;	/* 0x.0010 - POR Debug Mode Status Register */
	char	res1[12];
	uint	gpporcr;	/* 0x.0020 - General-Purpose POR Configuration Register */
	char	res2[12];
	uint	gpiocr;		/* 0x.0030 - GPIO Control Register */
	char	res3[12];
	uint	gpoutdr;	/* 0x.0040 - General-Purpose Output Data Register */
	char	res4[12];
	uint	gpindr;		/* 0x.0050 - General-Purpose Input Data Register */
	char	res5[12];
	uint	pmuxcr;		/* 0x.0060 - Alternate Function Signal Multiplex Control */
	char	res6[12];
	uint	devdisr;	/* 0x.0070 - Device Disable Control */
	char	res7[12];
	uint	powmgtcsr;	/* 0x.0080 - Power Management Status and Control Register */
	char	res8[12];
	uint	mcpsumr;	/* 0x.0090 - Machine Check Summary Register */
	char	res9[12];
	uint	pvr;		/* 0x.00a0 - Processor Version Register */
	uint	svr;		/* 0x.00a4 - System Version Register */
	char	res10[3416];
	uint	clkocr;		/* 0x.0e00 - Clock Out Select Register */
	char	res11[12];
	uint	ddrdllcr;	/* 0x.0e10 - DDR DLL Control Register */
	char	res12[12];
	uint	lbcdllcr;	/* 0x.0e20 - LBC DLL Control Register */
	char	res13[61916];
} ccsr_guts_t;

#endif /* __ASM_POWERPC_IMMAP_86XX_H__ */
#endif /* __KERNEL__ */
