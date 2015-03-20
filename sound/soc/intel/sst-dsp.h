/*
 * Intel Smart Sound Technology (SST) Core
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __SOUND_SOC_SST_DSP_H
#define __SOUND_SOC_SST_DSP_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>

/* SST Device IDs  */
#define SST_DEV_ID_LYNX_POINT		0x33C8
#define SST_DEV_ID_WILDCAT_POINT	0x3438
#define SST_DEV_ID_BYT			0x0F28

/* Supported SST DMA Devices */
#define SST_DMA_TYPE_DW		1
#define SST_DMA_TYPE_MID	2

/* autosuspend delay 5s*/
#define SST_RUNTIME_SUSPEND_DELAY	(5 * 1000)

/* SST Shim register map
 * The register naming can differ between products. Some products also
 * contain extra functionality.
 */
#define SST_CSR			0x00
#define SST_PISR		0x08
#define SST_PIMR		0x10
#define SST_ISRX		0x18
#define SST_ISRD		0x20
#define SST_IMRX		0x28
#define SST_IMRD		0x30
#define SST_IPCX		0x38 /* IPC IA -> SST */
#define SST_IPCD		0x40 /* IPC SST -> IA */
#define SST_ISRSC		0x48
#define SST_ISRLPESC		0x50
#define SST_IMRSC		0x58
#define SST_IMRLPESC		0x60
#define SST_IPCSC		0x68
#define SST_IPCLPESC		0x70
#define SST_CLKCTL		0x78
#define SST_CSR2		0x80
#define SST_LTRC		0xE0
#define SST_HMDC		0xE8

#define SST_SHIM_BEGIN		SST_CSR
#define SST_SHIM_END		SST_HDMC

#define SST_DBGO		0xF0

#define SST_SHIM_SIZE		0x100
#define SST_PWMCTRL             0x1000

/* SST Shim Register bits
 * The register bit naming can differ between products. Some products also
 * contain extra functionality.
 */

/* CSR / CS */
#define SST_CSR_RST		(0x1 << 1)
#define SST_CSR_SBCS0		(0x1 << 2)
#define SST_CSR_SBCS1		(0x1 << 3)
#define SST_CSR_DCS(x)		(x << 4)
#define SST_CSR_DCS_MASK	(0x7 << 4)
#define SST_CSR_STALL		(0x1 << 10)
#define SST_CSR_S0IOCS		(0x1 << 21)
#define SST_CSR_S1IOCS		(0x1 << 23)
#define SST_CSR_LPCS		(0x1 << 31)
#define SST_CSR_24MHZ_LPCS	(SST_CSR_SBCS0 | SST_CSR_SBCS1 | SST_CSR_LPCS)
#define SST_CSR_24MHZ_NO_LPCS	(SST_CSR_SBCS0 | SST_CSR_SBCS1)
#define SST_BYT_CSR_RST		(0x1 << 0)
#define SST_BYT_CSR_VECTOR_SEL	(0x1 << 1)
#define SST_BYT_CSR_STALL	(0x1 << 2)
#define SST_BYT_CSR_PWAITMODE	(0x1 << 3)

/*  ISRX / ISC */
#define SST_ISRX_BUSY		(0x1 << 1)
#define SST_ISRX_DONE		(0x1 << 0)
#define SST_BYT_ISRX_REQUEST	(0x1 << 1)

/*  ISRD / ISD */
#define SST_ISRD_BUSY		(0x1 << 1)
#define SST_ISRD_DONE		(0x1 << 0)

/* IMRX / IMC */
#define SST_IMRX_BUSY		(0x1 << 1)
#define SST_IMRX_DONE		(0x1 << 0)
#define SST_BYT_IMRX_REQUEST	(0x1 << 1)

/* IMRD / IMD */
#define SST_IMRD_DONE		(0x1 << 0)
#define SST_IMRD_BUSY		(0x1 << 1)
#define SST_IMRD_SSP0		(0x1 << 16)
#define SST_IMRD_DMAC0		(0x1 << 21)
#define SST_IMRD_DMAC1		(0x1 << 22)
#define SST_IMRD_DMAC		(SST_IMRD_DMAC0 | SST_IMRD_DMAC1)

/*  IPCX / IPCC */
#define	SST_IPCX_DONE		(0x1 << 30)
#define	SST_IPCX_BUSY		(0x1 << 31)
#define SST_BYT_IPCX_DONE	((u64)0x1 << 62)
#define SST_BYT_IPCX_BUSY	((u64)0x1 << 63)

/*  IPCD */
#define	SST_IPCD_DONE		(0x1 << 30)
#define	SST_IPCD_BUSY		(0x1 << 31)
#define SST_BYT_IPCD_DONE	((u64)0x1 << 62)
#define SST_BYT_IPCD_BUSY	((u64)0x1 << 63)

/* CLKCTL */
#define SST_CLKCTL_SMOS(x)	(x << 24)
#define SST_CLKCTL_MASK		(3 << 24)
#define SST_CLKCTL_DCPLCG	(1 << 18)
#define SST_CLKCTL_SCOE1	(1 << 17)
#define SST_CLKCTL_SCOE0	(1 << 16)

/* CSR2 / CS2 */
#define SST_CSR2_SDFD_SSP0	(1 << 1)
#define SST_CSR2_SDFD_SSP1	(1 << 2)

/* LTRC */
#define SST_LTRC_VAL(x)		(x << 0)

/* HMDC */
#define SST_HMDC_HDDA0(x)	(x << 0)
#define SST_HMDC_HDDA1(x)	(x << 7)
#define SST_HMDC_HDDA_E0_CH0	1
#define SST_HMDC_HDDA_E0_CH1	2
#define SST_HMDC_HDDA_E0_CH2	4
#define SST_HMDC_HDDA_E0_CH3	8
#define SST_HMDC_HDDA_E1_CH0	SST_HMDC_HDDA1(SST_HMDC_HDDA_E0_CH0)
#define SST_HMDC_HDDA_E1_CH1	SST_HMDC_HDDA1(SST_HMDC_HDDA_E0_CH1)
#define SST_HMDC_HDDA_E1_CH2	SST_HMDC_HDDA1(SST_HMDC_HDDA_E0_CH2)
#define SST_HMDC_HDDA_E1_CH3	SST_HMDC_HDDA1(SST_HMDC_HDDA_E0_CH3)
#define SST_HMDC_HDDA_E0_ALLCH	(SST_HMDC_HDDA_E0_CH0 | SST_HMDC_HDDA_E0_CH1 | \
				 SST_HMDC_HDDA_E0_CH2 | SST_HMDC_HDDA_E0_CH3)
#define SST_HMDC_HDDA_E1_ALLCH	(SST_HMDC_HDDA_E1_CH0 | SST_HMDC_HDDA_E1_CH1 | \
				 SST_HMDC_HDDA_E1_CH2 | SST_HMDC_HDDA_E1_CH3)


/* SST Vendor Defined Registers and bits */
#define SST_VDRTCTL0		0xa0
#define SST_VDRTCTL1		0xa4
#define SST_VDRTCTL2		0xa8
#define SST_VDRTCTL3		0xaC

/* VDRTCTL0 */
#define SST_VDRTCL0_D3PGD		(1 << 0)
#define SST_VDRTCL0_D3SRAMPGD		(1 << 1)
#define SST_VDRTCL0_DSRAMPGE_SHIFT	12
#define SST_VDRTCL0_DSRAMPGE_MASK	(0xfffff << SST_VDRTCL0_DSRAMPGE_SHIFT)
#define SST_VDRTCL0_ISRAMPGE_SHIFT	2
#define SST_VDRTCL0_ISRAMPGE_MASK	(0x3ff << SST_VDRTCL0_ISRAMPGE_SHIFT)

/* VDRTCTL2 */
#define SST_VDRTCL2_DCLCGE		(1 << 1)
#define SST_VDRTCL2_DTCGE		(1 << 10)
#define SST_VDRTCL2_APLLSE_MASK		(1 << 31)

/* PMCS */
#define SST_PMCS		0x84
#define SST_PMCS_PS_MASK	0x3

struct sst_dsp;

/*
 * SST Device.
 *
 * This structure is populated by the SST core driver.
 */
struct sst_dsp_device {
	/* Mandatory fields */
	struct sst_ops *ops;
	irqreturn_t (*thread)(int irq, void *context);
	void *thread_context;
};

/*
 * SST Platform Data.
 */
struct sst_pdata {
	/* ACPI data */
	u32 lpe_base;
	u32 lpe_size;
	u32 pcicfg_base;
	u32 pcicfg_size;
	u32 fw_base;
	u32 fw_size;
	int irq;

	/* Firmware */
	const struct firmware *fw;

	/* DMA */
	int resindex_dma_base; /* other fields invalid if equals to -1 */
	u32 dma_base;
	u32 dma_size;
	int dma_engine;
	struct device *dma_dev;

	/* DSP */
	u32 id;
	void *dsp;
};

/* Initialization */
struct sst_dsp *sst_dsp_new(struct device *dev,
	struct sst_dsp_device *sst_dev, struct sst_pdata *pdata);
void sst_dsp_free(struct sst_dsp *sst);

/* SHIM Read / Write */
void sst_dsp_shim_write(struct sst_dsp *sst, u32 offset, u32 value);
u32 sst_dsp_shim_read(struct sst_dsp *sst, u32 offset);
int sst_dsp_shim_update_bits(struct sst_dsp *sst, u32 offset,
				u32 mask, u32 value);
void sst_dsp_shim_write64(struct sst_dsp *sst, u32 offset, u64 value);
u64 sst_dsp_shim_read64(struct sst_dsp *sst, u32 offset);
int sst_dsp_shim_update_bits64(struct sst_dsp *sst, u32 offset,
				u64 mask, u64 value);

/* SHIM Read / Write Unlocked for callers already holding sst lock */
void sst_dsp_shim_write_unlocked(struct sst_dsp *sst, u32 offset, u32 value);
u32 sst_dsp_shim_read_unlocked(struct sst_dsp *sst, u32 offset);
int sst_dsp_shim_update_bits_unlocked(struct sst_dsp *sst, u32 offset,
				u32 mask, u32 value);
void sst_dsp_shim_write64_unlocked(struct sst_dsp *sst, u32 offset, u64 value);
u64 sst_dsp_shim_read64_unlocked(struct sst_dsp *sst, u32 offset);
int sst_dsp_shim_update_bits64_unlocked(struct sst_dsp *sst, u32 offset,
					u64 mask, u64 value);

/* Internal generic low-level SST IO functions - can be overidden */
void sst_shim32_write(void __iomem *addr, u32 offset, u32 value);
u32 sst_shim32_read(void __iomem *addr, u32 offset);
void sst_shim32_write64(void __iomem *addr, u32 offset, u64 value);
u64 sst_shim32_read64(void __iomem *addr, u32 offset);
void sst_memcpy_toio_32(struct sst_dsp *sst,
			void __iomem *dest, void *src, size_t bytes);
void sst_memcpy_fromio_32(struct sst_dsp *sst,
			  void *dest, void __iomem *src, size_t bytes);

/* DSP reset & boot */
void sst_dsp_reset(struct sst_dsp *sst);
int sst_dsp_boot(struct sst_dsp *sst);
int sst_dsp_wake(struct sst_dsp *sst);
void sst_dsp_sleep(struct sst_dsp *sst);
void sst_dsp_stall(struct sst_dsp *sst);

/* DMA */
int sst_dsp_dma_get_channel(struct sst_dsp *dsp, int chan_id);
void sst_dsp_dma_put_channel(struct sst_dsp *dsp);
int sst_dsp_dma_copyfrom(struct sst_dsp *sst, dma_addr_t dest_addr,
	dma_addr_t src_addr, size_t size);
int sst_dsp_dma_copyto(struct sst_dsp *sst, dma_addr_t dest_addr,
	dma_addr_t src_addr, size_t size);

/* Msg IO */
void sst_dsp_ipc_msg_tx(struct sst_dsp *dsp, u32 msg);
u32 sst_dsp_ipc_msg_rx(struct sst_dsp *dsp);

/* Mailbox management */
int sst_dsp_mailbox_init(struct sst_dsp *dsp, u32 inbox_offset,
	size_t inbox_size, u32 outbox_offset, size_t outbox_size);
void sst_dsp_inbox_write(struct sst_dsp *dsp, void *message, size_t bytes);
void sst_dsp_inbox_read(struct sst_dsp *dsp, void *message, size_t bytes);
void sst_dsp_outbox_write(struct sst_dsp *dsp, void *message, size_t bytes);
void sst_dsp_outbox_read(struct sst_dsp *dsp, void *message, size_t bytes);
void sst_dsp_mailbox_dump(struct sst_dsp *dsp, size_t bytes);

/* Debug */
void sst_dsp_dump(struct sst_dsp *sst);

#endif
