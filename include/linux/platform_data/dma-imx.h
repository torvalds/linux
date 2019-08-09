/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2004-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 */

#ifndef __ASM_ARCH_MXC_DMA_H__
#define __ASM_ARCH_MXC_DMA_H__

#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/dmaengine.h>

/*
 * This enumerates peripheral types. Used for SDMA.
 */
enum sdma_peripheral_type {
	IMX_DMATYPE_SSI,	/* MCU domain SSI */
	IMX_DMATYPE_SSI_SP,	/* Shared SSI */
	IMX_DMATYPE_MMC,	/* MMC */
	IMX_DMATYPE_SDHC,	/* SDHC */
	IMX_DMATYPE_UART,	/* MCU domain UART */
	IMX_DMATYPE_UART_SP,	/* Shared UART */
	IMX_DMATYPE_FIRI,	/* FIRI */
	IMX_DMATYPE_CSPI,	/* MCU domain CSPI */
	IMX_DMATYPE_CSPI_SP,	/* Shared CSPI */
	IMX_DMATYPE_SIM,	/* SIM */
	IMX_DMATYPE_ATA,	/* ATA */
	IMX_DMATYPE_CCM,	/* CCM */
	IMX_DMATYPE_EXT,	/* External peripheral */
	IMX_DMATYPE_MSHC,	/* Memory Stick Host Controller */
	IMX_DMATYPE_MSHC_SP,	/* Shared Memory Stick Host Controller */
	IMX_DMATYPE_DSP,	/* DSP */
	IMX_DMATYPE_MEMORY,	/* Memory */
	IMX_DMATYPE_FIFO_MEMORY,/* FIFO type Memory */
	IMX_DMATYPE_SPDIF,	/* SPDIF */
	IMX_DMATYPE_IPU_MEMORY,	/* IPU Memory */
	IMX_DMATYPE_ASRC,	/* ASRC */
	IMX_DMATYPE_ESAI,	/* ESAI */
	IMX_DMATYPE_SSI_DUAL,	/* SSI Dual FIFO */
	IMX_DMATYPE_ASRC_SP,	/* Shared ASRC */
	IMX_DMATYPE_SAI,	/* SAI */
};

enum imx_dma_prio {
	DMA_PRIO_HIGH = 0,
	DMA_PRIO_MEDIUM = 1,
	DMA_PRIO_LOW = 2
};

struct imx_dma_data {
	int dma_request; /* DMA request line */
	int dma_request2; /* secondary DMA request line */
	enum sdma_peripheral_type peripheral_type;
	int priority;
};

static inline int imx_dma_is_ipu(struct dma_chan *chan)
{
	return !strcmp(dev_name(chan->device->dev), "ipu-core");
}

static inline int imx_dma_is_general_purpose(struct dma_chan *chan)
{
	return !strcmp(chan->device->dev->driver->name, "imx-sdma") ||
		!strcmp(chan->device->dev->driver->name, "imx-dma");
}

#endif
