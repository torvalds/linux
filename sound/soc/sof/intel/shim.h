/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __SOF_INTEL_SHIM_H
#define __SOF_INTEL_SHIM_H

/*
 * SHIM registers for BYT, BSW, CHT, HSW, BDW
 */

#define SHIM_CSR		(SHIM_OFFSET + 0x00)
#define SHIM_PISR		(SHIM_OFFSET + 0x08)
#define SHIM_PIMR		(SHIM_OFFSET + 0x10)
#define SHIM_ISRX		(SHIM_OFFSET + 0x18)
#define SHIM_ISRD		(SHIM_OFFSET + 0x20)
#define SHIM_IMRX		(SHIM_OFFSET + 0x28)
#define SHIM_IMRD		(SHIM_OFFSET + 0x30)
#define SHIM_IPCX		(SHIM_OFFSET + 0x38)
#define SHIM_IPCD		(SHIM_OFFSET + 0x40)
#define SHIM_ISRSC		(SHIM_OFFSET + 0x48)
#define SHIM_ISRLPESC		(SHIM_OFFSET + 0x50)
#define SHIM_IMRSC		(SHIM_OFFSET + 0x58)
#define SHIM_IMRLPESC		(SHIM_OFFSET + 0x60)
#define SHIM_IPCSC		(SHIM_OFFSET + 0x68)
#define SHIM_IPCLPESC		(SHIM_OFFSET + 0x70)
#define SHIM_CLKCTL		(SHIM_OFFSET + 0x78)
#define SHIM_CSR2		(SHIM_OFFSET + 0x80)
#define SHIM_LTRC		(SHIM_OFFSET + 0xE0)
#define SHIM_HMDC		(SHIM_OFFSET + 0xE8)

#define SHIM_PWMCTRL		0x1000

/*
 * SST SHIM register bits for BYT, BSW, CHT HSW, BDW
 * Register bit naming and functionaility can differ between devices.
 */

/* CSR / CS */
#define SHIM_CSR_RST		(0x1 << 1)
#define SHIM_CSR_SBCS0		(0x1 << 2)
#define SHIM_CSR_SBCS1		(0x1 << 3)
#define SHIM_CSR_DCS(x)		((x) << 4)
#define SHIM_CSR_DCS_MASK	(0x7 << 4)
#define SHIM_CSR_STALL		(0x1 << 10)
#define SHIM_CSR_S0IOCS		(0x1 << 21)
#define SHIM_CSR_S1IOCS		(0x1 << 23)
#define SHIM_CSR_LPCS		(0x1 << 31)
#define SHIM_CSR_24MHZ_LPCS \
	(SHIM_CSR_SBCS0 | SHIM_CSR_SBCS1 | SHIM_CSR_LPCS)
#define SHIM_CSR_24MHZ_NO_LPCS	(SHIM_CSR_SBCS0 | SHIM_CSR_SBCS1)
#define SHIM_BYT_CSR_RST	(0x1 << 0)
#define SHIM_BYT_CSR_VECTOR_SEL	(0x1 << 1)
#define SHIM_BYT_CSR_STALL	(0x1 << 2)
#define SHIM_BYT_CSR_PWAITMODE	(0x1 << 3)

/*  ISRX / ISC */
#define SHIM_ISRX_BUSY		(0x1 << 1)
#define SHIM_ISRX_DONE		(0x1 << 0)
#define SHIM_BYT_ISRX_REQUEST	(0x1 << 1)

/*  ISRD / ISD */
#define SHIM_ISRD_BUSY		(0x1 << 1)
#define SHIM_ISRD_DONE		(0x1 << 0)

/* IMRX / IMC */
#define SHIM_IMRX_BUSY		(0x1 << 1)
#define SHIM_IMRX_DONE		(0x1 << 0)
#define SHIM_BYT_IMRX_REQUEST	(0x1 << 1)

/* IMRD / IMD */
#define SHIM_IMRD_DONE		(0x1 << 0)
#define SHIM_IMRD_BUSY		(0x1 << 1)
#define SHIM_IMRD_SSP0		(0x1 << 16)
#define SHIM_IMRD_DMAC0		(0x1 << 21)
#define SHIM_IMRD_DMAC1		(0x1 << 22)
#define SHIM_IMRD_DMAC		(SHIM_IMRD_DMAC0 | SHIM_IMRD_DMAC1)

/*  IPCX / IPCC */
#define	SHIM_IPCX_DONE		(0x1 << 30)
#define	SHIM_IPCX_BUSY		(0x1 << 31)
#define SHIM_BYT_IPCX_DONE	((u64)0x1 << 62)
#define SHIM_BYT_IPCX_BUSY	((u64)0x1 << 63)

/*  IPCD */
#define	SHIM_IPCD_DONE		(0x1 << 30)
#define	SHIM_IPCD_BUSY		(0x1 << 31)
#define SHIM_BYT_IPCD_DONE	((u64)0x1 << 62)
#define SHIM_BYT_IPCD_BUSY	((u64)0x1 << 63)

/* CLKCTL */
#define SHIM_CLKCTL_SMOS(x)	((x) << 24)
#define SHIM_CLKCTL_MASK	(3 << 24)
#define SHIM_CLKCTL_DCPLCG	BIT(18)
#define SHIM_CLKCTL_SCOE1	BIT(17)
#define SHIM_CLKCTL_SCOE0	BIT(16)

/* CSR2 / CS2 */
#define SHIM_CSR2_SDFD_SSP0	BIT(1)
#define SHIM_CSR2_SDFD_SSP1	BIT(2)

/* LTRC */
#define SHIM_LTRC_VAL(x)	((x) << 0)

/* HMDC */
#define SHIM_HMDC_HDDA0(x)	((x) << 0)
#define SHIM_HMDC_HDDA1(x)	((x) << 7)
#define SHIM_HMDC_HDDA_E0_CH0	1
#define SHIM_HMDC_HDDA_E0_CH1	2
#define SHIM_HMDC_HDDA_E0_CH2	4
#define SHIM_HMDC_HDDA_E0_CH3	8
#define SHIM_HMDC_HDDA_E1_CH0	SHIM_HMDC_HDDA1(SHIM_HMDC_HDDA_E0_CH0)
#define SHIM_HMDC_HDDA_E1_CH1	SHIM_HMDC_HDDA1(SHIM_HMDC_HDDA_E0_CH1)
#define SHIM_HMDC_HDDA_E1_CH2	SHIM_HMDC_HDDA1(SHIM_HMDC_HDDA_E0_CH2)
#define SHIM_HMDC_HDDA_E1_CH3	SHIM_HMDC_HDDA1(SHIM_HMDC_HDDA_E0_CH3)
#define SHIM_HMDC_HDDA_E0_ALLCH	\
	(SHIM_HMDC_HDDA_E0_CH0 | SHIM_HMDC_HDDA_E0_CH1 | \
	 SHIM_HMDC_HDDA_E0_CH2 | SHIM_HMDC_HDDA_E0_CH3)
#define SHIM_HMDC_HDDA_E1_ALLCH	\
	(SHIM_HMDC_HDDA_E1_CH0 | SHIM_HMDC_HDDA_E1_CH1 | \
	 SHIM_HMDC_HDDA_E1_CH2 | SHIM_HMDC_HDDA_E1_CH3)

/* Audio DSP PCI registers */
#define PCI_VDRTCTL0		0xa0
#define PCI_VDRTCTL1		0xa4
#define PCI_VDRTCTL2		0xa8
#define PCI_VDRTCTL3		0xaC

/* VDRTCTL0 */
#define PCI_VDRTCL0_D3PGD		BIT(0)
#define PCI_VDRTCL0_D3SRAMPGD		BIT(1)
#define PCI_VDRTCL0_DSRAMPGE_SHIFT	12
#define PCI_VDRTCL0_DSRAMPGE_MASK	(0xfffff << PCI_VDRTCL0_DSRAMPGE_SHIFT)
#define PCI_VDRTCL0_ISRAMPGE_SHIFT	2
#define PCI_VDRTCL0_ISRAMPGE_MASK	(0x3ff << PCI_VDRTCL0_ISRAMPGE_SHIFT)

/* VDRTCTL2 */
#define PCI_VDRTCL2_DCLCGE		BIT(1)
#define PCI_VDRTCL2_DTCGE		BIT(10)
#define PCI_VDRTCL2_APLLSE_MASK		BIT(31)

/* PMCS */
#define PCI_PMCS		0x84
#define PCI_PMCS_PS_MASK	0x3

extern struct snd_sof_dsp_ops sof_byt_ops;
extern struct snd_sof_dsp_ops sof_cht_ops;
extern struct snd_sof_dsp_ops sof_hsw_ops;
extern struct snd_sof_dsp_ops sof_bdw_ops;

#endif
