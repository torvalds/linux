/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
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

enum sof_intel_hw_ip_version {
	SOF_INTEL_TANGIER,
	SOF_INTEL_BAYTRAIL,
	SOF_INTEL_BROADWELL,
	SOF_INTEL_CAVS_1_5,	/* SkyLake, KabyLake, AmberLake */
	SOF_INTEL_CAVS_1_5_PLUS,/* ApolloLake, GeminiLake */
	SOF_INTEL_CAVS_1_8,	/* CannonLake, CometLake, CoffeeLake */
	SOF_INTEL_CAVS_2_0,	/* IceLake, JasperLake */
	SOF_INTEL_CAVS_2_5,	/* TigerLake, AlderLake */
};

/*
 * SHIM registers for BYT, BSW, CHT, BDW
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
 * SST SHIM register bits for BYT, BSW, CHT, BDW
 * Register bit naming and functionaility can differ between devices.
 */

/* CSR / CS */
#define SHIM_CSR_RST		BIT(1)
#define SHIM_CSR_SBCS0		BIT(2)
#define SHIM_CSR_SBCS1		BIT(3)
#define SHIM_CSR_DCS(x)		((x) << 4)
#define SHIM_CSR_DCS_MASK	(0x7 << 4)
#define SHIM_CSR_STALL		BIT(10)
#define SHIM_CSR_S0IOCS		BIT(21)
#define SHIM_CSR_S1IOCS		BIT(23)
#define SHIM_CSR_LPCS		BIT(31)
#define SHIM_CSR_24MHZ_LPCS \
	(SHIM_CSR_SBCS0 | SHIM_CSR_SBCS1 | SHIM_CSR_LPCS)
#define SHIM_CSR_24MHZ_NO_LPCS	(SHIM_CSR_SBCS0 | SHIM_CSR_SBCS1)
#define SHIM_BYT_CSR_RST	BIT(0)
#define SHIM_BYT_CSR_VECTOR_SEL	BIT(1)
#define SHIM_BYT_CSR_STALL	BIT(2)
#define SHIM_BYT_CSR_PWAITMODE	BIT(3)

/*  ISRX / ISC */
#define SHIM_ISRX_BUSY		BIT(1)
#define SHIM_ISRX_DONE		BIT(0)
#define SHIM_BYT_ISRX_REQUEST	BIT(1)

/*  ISRD / ISD */
#define SHIM_ISRD_BUSY		BIT(1)
#define SHIM_ISRD_DONE		BIT(0)

/* IMRX / IMC */
#define SHIM_IMRX_BUSY		BIT(1)
#define SHIM_IMRX_DONE		BIT(0)
#define SHIM_BYT_IMRX_REQUEST	BIT(1)

/* IMRD / IMD */
#define SHIM_IMRD_DONE		BIT(0)
#define SHIM_IMRD_BUSY		BIT(1)
#define SHIM_IMRD_SSP0		BIT(16)
#define SHIM_IMRD_DMAC0		BIT(21)
#define SHIM_IMRD_DMAC1		BIT(22)
#define SHIM_IMRD_DMAC		(SHIM_IMRD_DMAC0 | SHIM_IMRD_DMAC1)

/*  IPCX / IPCC */
#define	SHIM_IPCX_DONE		BIT(30)
#define	SHIM_IPCX_BUSY		BIT(31)
#define SHIM_BYT_IPCX_DONE	BIT_ULL(62)
#define SHIM_BYT_IPCX_BUSY	BIT_ULL(63)

/*  IPCD */
#define	SHIM_IPCD_DONE		BIT(30)
#define	SHIM_IPCD_BUSY		BIT(31)
#define SHIM_BYT_IPCD_DONE	BIT_ULL(62)
#define SHIM_BYT_IPCD_BUSY	BIT_ULL(63)

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
#define PCI_VDRTCL0_DSRAMPGE_MASK	GENMASK(PCI_VDRTCL0_DSRAMPGE_SHIFT + 19,\
						PCI_VDRTCL0_DSRAMPGE_SHIFT)
#define PCI_VDRTCL0_ISRAMPGE_SHIFT	2
#define PCI_VDRTCL0_ISRAMPGE_MASK	GENMASK(PCI_VDRTCL0_ISRAMPGE_SHIFT + 9,\
						PCI_VDRTCL0_ISRAMPGE_SHIFT)

/* VDRTCTL2 */
#define PCI_VDRTCL2_DCLCGE		BIT(1)
#define PCI_VDRTCL2_DTCGE		BIT(10)
#define PCI_VDRTCL2_APLLSE_MASK		BIT(31)

/* PMCS */
#define PCI_PMCS		0x84
#define PCI_PMCS_PS_MASK	0x3

/* Intel quirks */
#define SOF_INTEL_PROCEN_FMT_QUIRK BIT(0)

/* DSP hardware descriptor */
struct sof_intel_dsp_desc {
	int cores_num;
	int host_managed_cores_mask;
	int init_core_mask; /* cores available after fw boot */
	int ipc_req;
	int ipc_req_mask;
	int ipc_ack;
	int ipc_ack_mask;
	int ipc_ctl;
	int rom_status_reg;
	int rom_init_timeout;
	int ssp_count;			/* ssp count of the platform */
	int ssp_base_offset;		/* base address of the SSPs */
	u32 sdw_shim_base;
	u32 sdw_alh_base;
	u32 quirks;
	enum sof_intel_hw_ip_version hw_ip_version;
	bool (*check_sdw_irq)(struct snd_sof_dev *sdev);
	bool (*check_ipc_irq)(struct snd_sof_dev *sdev);
};

extern struct snd_sof_dsp_ops sof_tng_ops;

extern const struct sof_intel_dsp_desc tng_chip_info;

struct sof_intel_stream {
	size_t posn_offset;
};

static inline const struct sof_intel_dsp_desc *get_chip_info(struct snd_sof_pdata *pdata)
{
	const struct sof_dev_desc *desc = pdata->desc;

	return desc->chip_info;
}

#endif
