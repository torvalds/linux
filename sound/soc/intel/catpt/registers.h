/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2020 Intel Corporation
 *
 * Author: Cezary Rojewski <cezary.rojewski@intel.com>
 */

#ifndef __SND_SOC_INTEL_CATPT_REGS_H
#define __SND_SOC_INTEL_CATPT_REGS_H

#include <linux/bitops.h>
#include <linux/iopoll.h>
#include <uapi/linux/pci_regs.h>

#define CATPT_SHIM_REGS_SIZE	4096
#define CATPT_DMA_REGS_SIZE	1024
#define CATPT_DMA_COUNT		2
#define CATPT_SSP_REGS_SIZE	512

/* DSP Shim registers */

#define CATPT_SHIM_CS1		0x00
#define CATPT_SHIM_ISC		0x18
#define CATPT_SHIM_ISD		0x20
#define CATPT_SHIM_IMC		0x28
#define CATPT_SHIM_IMD		0x30
#define CATPT_SHIM_IPCC		0x38
#define CATPT_SHIM_IPCD		0x40
#define CATPT_SHIM_CLKCTL	0x78
#define CATPT_SHIM_CS2		0x80
#define CATPT_SHIM_LTRC		0xE0
#define CATPT_SHIM_HMDC		0xE8

#define CATPT_CS_LPCS		BIT(31)
#define CATPT_CS_SFCR(ssp)	BIT(27 + (ssp))
#define CATPT_CS_S1IOCS		BIT(23)
#define CATPT_CS_S0IOCS		BIT(21)
#define CATPT_CS_PCE		BIT(15)
#define CATPT_CS_SDPM(ssp)	BIT(11 + (ssp))
#define CATPT_CS_STALL		BIT(10)
#define CATPT_CS_DCS		GENMASK(6, 4)
/* b100 DSP core & audio fabric high clock */
#define CATPT_CS_DCS_HIGH	(0x4 << 4)
#define CATPT_CS_SBCS(ssp)	BIT(2 + (ssp))
#define CATPT_CS_RST		BIT(1)

#define CATPT_ISC_IPCDB		BIT(1)
#define CATPT_ISC_IPCCD		BIT(0)
#define CATPT_ISD_DCPWM		BIT(31)
#define CATPT_ISD_IPCCB		BIT(1)
#define CATPT_ISD_IPCDD		BIT(0)

#define CATPT_IMC_IPCDB		BIT(1)
#define CATPT_IMC_IPCCD		BIT(0)
#define CATPT_IMD_IPCCB		BIT(1)
#define CATPT_IMD_IPCDD		BIT(0)

#define CATPT_IPCC_BUSY		BIT(31)
#define CATPT_IPCC_DONE		BIT(30)
#define CATPT_IPCD_BUSY		BIT(31)
#define CATPT_IPCD_DONE		BIT(30)

#define CATPT_CLKCTL_CFCIP	BIT(31)
#define CATPT_CLKCTL_SMOS	GENMASK(25, 24)

#define CATPT_HMDC_HDDA(e, ch)	BIT(8 * (e) + (ch))

/* defaults to reset SHIM registers to after each power cycle */
#define CATPT_CS_DEFAULT	0x8480040E
#define CATPT_ISC_DEFAULT	0x0
#define CATPT_ISD_DEFAULT	0x0
#define CATPT_IMC_DEFAULT	0x7FFF0003
#define CATPT_IMD_DEFAULT	0x7FFF0003
#define CATPT_IPCC_DEFAULT	0x0
#define CATPT_IPCD_DEFAULT	0x0
#define CATPT_CLKCTL_DEFAULT	0x7FF
#define CATPT_CS2_DEFAULT	0x0
#define CATPT_LTRC_DEFAULT	0x0
#define CATPT_HMDC_DEFAULT	0x0

/* PCI Configuration registers */

#define CATPT_PCI_PMCAPID	0x80
#define CATPT_PCI_PMCS		(CATPT_PCI_PMCAPID + PCI_PM_CTRL)
#define CATPT_PCI_VDRTCTL0	0xA0
#define CATPT_PCI_VDRTCTL2	0xA8

#define CATPT_VDRTCTL2_DTCGE	BIT(10)
#define CATPT_VDRTCTL2_DCLCGE	BIT(1)
#define CATPT_VDRTCTL2_CGEALL	0xF7F

/* LPT PCI Configuration bits */

#define LPT_VDRTCTL0_DSRAMPGE(b)	BIT(16 + (b))
#define LPT_VDRTCTL0_DSRAMPGE_MASK	GENMASK(31, 16)
#define LPT_VDRTCTL0_ISRAMPGE(b)	BIT(6 + (b))
#define LPT_VDRTCTL0_ISRAMPGE_MASK	GENMASK(15, 6)
#define LPT_VDRTCTL0_D3SRAMPGD		BIT(2)
#define LPT_VDRTCTL0_D3PGD		BIT(1)
#define LPT_VDRTCTL0_APLLSE		BIT(0)

/* WPT PCI Configuration bits */

#define WPT_VDRTCTL0_DSRAMPGE(b)	BIT(12 + (b))
#define WPT_VDRTCTL0_DSRAMPGE_MASK	GENMASK(31, 12)
#define WPT_VDRTCTL0_ISRAMPGE(b)	BIT(2 + (b))
#define WPT_VDRTCTL0_ISRAMPGE_MASK	GENMASK(11, 2)
#define WPT_VDRTCTL0_D3SRAMPGD		BIT(1)
#define WPT_VDRTCTL0_D3PGD		BIT(0)

#define WPT_VDRTCTL2_APLLSE		BIT(31)

/* defaults to reset SSP registers to after each power cycle */
#define CATPT_SSC0_DEFAULT		0x0
#define CATPT_SSC1_DEFAULT		0x0
#define CATPT_SSS_DEFAULT		0xF004
#define CATPT_SSIT_DEFAULT		0x0
#define CATPT_SSD_DEFAULT		0xC43893A3
#define CATPT_SSTO_DEFAULT		0x0
#define CATPT_SSPSP_DEFAULT		0x0
#define CATPT_SSTSA_DEFAULT		0x0
#define CATPT_SSRSA_DEFAULT		0x0
#define CATPT_SSTSS_DEFAULT		0x0
#define CATPT_SSCR2_DEFAULT		0x0
#define CATPT_SSPSP2_DEFAULT		0x0

/* Physically the same block, access address differs between host and dsp */
#define CATPT_DSP_DRAM_OFFSET		0x400000
#define catpt_to_host_offset(offset)	((offset) & ~(CATPT_DSP_DRAM_OFFSET))
#define catpt_to_dsp_offset(offset)	((offset) | CATPT_DSP_DRAM_OFFSET)

#define CATPT_MEMBLOCK_SIZE	0x8000
#define catpt_num_dram(cdev)	(hweight_long((cdev)->spec->dram_mask))
#define catpt_num_iram(cdev)	(hweight_long((cdev)->spec->iram_mask))
#define catpt_dram_size(cdev)	(catpt_num_dram(cdev) * CATPT_MEMBLOCK_SIZE)
#define catpt_iram_size(cdev)	(catpt_num_iram(cdev) * CATPT_MEMBLOCK_SIZE)

/* registry I/O helpers */

#define catpt_shim_addr(cdev) \
	((cdev)->lpe_ba + (cdev)->spec->host_shim_offset)
#define catpt_dma_addr(cdev, dma) \
	((cdev)->lpe_ba + (cdev)->spec->host_dma_offset[dma])
#define catpt_ssp_addr(cdev, ssp) \
	((cdev)->lpe_ba + (cdev)->spec->host_ssp_offset[ssp])
#define catpt_inbox_addr(cdev) \
	((cdev)->lpe_ba + (cdev)->ipc.config.inbox_offset)
#define catpt_outbox_addr(cdev) \
	((cdev)->lpe_ba + (cdev)->ipc.config.outbox_offset)

#define catpt_writel_ssp(cdev, ssp, reg, val) \
	writel(val, catpt_ssp_addr(cdev, ssp) + (reg))

#define catpt_readl_shim(cdev, reg) \
	readl(catpt_shim_addr(cdev) + CATPT_SHIM_##reg)
#define catpt_writel_shim(cdev, reg, val) \
	writel(val, catpt_shim_addr(cdev) + CATPT_SHIM_##reg)
#define catpt_updatel_shim(cdev, reg, mask, val) \
	catpt_writel_shim(cdev, reg, \
			  (catpt_readl_shim(cdev, reg) & ~(mask)) | (val))

#define catpt_readl_poll_shim(cdev, reg, val, cond, delay_us, timeout_us) \
	readl_poll_timeout(catpt_shim_addr(cdev) + CATPT_SHIM_##reg, \
			   val, cond, delay_us, timeout_us)

#define catpt_readl_pci(cdev, reg) \
	readl(cdev->pci_ba + CATPT_PCI_##reg)
#define catpt_writel_pci(cdev, reg, val) \
	writel(val, cdev->pci_ba + CATPT_PCI_##reg)
#define catpt_updatel_pci(cdev, reg, mask, val) \
	catpt_writel_pci(cdev, reg, \
			 (catpt_readl_pci(cdev, reg) & ~(mask)) | (val))

#define catpt_readl_poll_pci(cdev, reg, val, cond, delay_us, timeout_us) \
	readl_poll_timeout((cdev)->pci_ba + CATPT_PCI_##reg, \
			   val, cond, delay_us, timeout_us)

#endif
