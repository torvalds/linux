/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2004-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 */

#ifndef __LINUX_DMA_IMX_H
#define __LINUX_DMA_IMX_H

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
	IMX_DMATYPE_MULTI_SAI,	/* MULTI FIFOs For Audio */
	IMX_DMATYPE_HDMI,       /* HDMI Audio */
	IMX_DMATYPE_I2C,	/* I2C */
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

/**
 * struct sdma_peripheral_config - SDMA config for audio
 * @n_fifos_src: Number of FIFOs for recording
 * @n_fifos_dst: Number of FIFOs for playback
 * @stride_fifos_src: FIFO address stride for recording, 0 means all FIFOs are
 *                    continuous, 1 means 1 word stride between FIFOs. All stride
 *                    between FIFOs should be same.
 * @stride_fifos_dst: FIFO address stride for playback
 * @words_per_fifo: numbers of words per FIFO fetch/fill, 1 means
 *                  one channel per FIFO, 2 means 2 channels per FIFO..
 *                  If 'n_fifos_src =  4' and 'words_per_fifo = 2', it
 *                  means the first two words(channels) fetch from FIFO0
 *                  and then jump to FIFO1 for next two words, and so on
 *                  after the last FIFO3 fetched, roll back to FIFO0.
 * @sw_done: Use software done. Needed for PDM (micfil)
 *
 * Some i.MX Audio devices (SAI, micfil) have multiple successive FIFO
 * registers. For multichannel recording/playback the SAI/micfil have
 * one FIFO register per channel and the SDMA engine has to read/write
 * the next channel from/to the next register and wrap around to the
 * first register when all channels are handled. The number of active
 * channels must be communicated to the SDMA engine using this struct.
 */
struct sdma_peripheral_config {
	int n_fifos_src;
	int n_fifos_dst;
	int stride_fifos_src;
	int stride_fifos_dst;
	int words_per_fifo;
	bool sw_done;
};

#endif /* __LINUX_DMA_IMX_H */
