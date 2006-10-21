/*
 *  linux/include/asm-arm/imxads/dma.h
 *
 *  Copyright (C) 1997,1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <asm/dma.h>

#ifndef __ASM_ARCH_IMX_DMA_H
#define __ASM_ARCH_IMX_DMA_H

#define IMX_DMA_CHANNELS  11

/*
 * struct imx_dma_channel - i.MX specific DMA extension
 * @name: name specified by DMA client
 * @irq_handler: client callback for end of transfer
 * @err_handler: client callback for error condition
 * @data: clients context data for callbacks
 * @dma_mode: direction of the transfer %DMA_MODE_READ or %DMA_MODE_WRITE
 * @sg: pointer to the actual read/written chunk for scatter-gather emulation
 * @sgbc: counter of processed bytes in the actual read/written chunk
 * @resbytes: total residual number of bytes to transfer
 *            (it can be lower or same as sum of SG mapped chunk sizes)
 * @sgcount: number of chunks to be read/written
 *
 * Structure is used for IMX DMA processing. It would be probably good
 * @struct dma_struct in the future for external interfacing and use
 * @struct imx_dma_channel only as extension to it.
 */

struct imx_dma_channel {
	const char *name;
	void (*irq_handler) (int, void *);
	void (*err_handler) (int, void *, int errcode);
	void *data;
	dmamode_t  dma_mode;
	struct scatterlist *sg;
	unsigned int sgbc;
	unsigned int sgcount;
	unsigned int resbytes;
	int dma_num;
};

extern struct imx_dma_channel imx_dma_channels[IMX_DMA_CHANNELS];

#define IMX_DMA_ERR_BURST     1
#define IMX_DMA_ERR_REQUEST   2
#define IMX_DMA_ERR_TRANSFER  4
#define IMX_DMA_ERR_BUFFER    8

/* The type to distinguish channel numbers parameter from ordinal int type */
typedef int imx_dmach_t;

int
imx_dma_setup_single(imx_dmach_t dma_ch, dma_addr_t dma_address,
		unsigned int dma_length, unsigned int dev_addr, dmamode_t dmamode);

int
imx_dma_setup_sg(imx_dmach_t dma_ch,
		 struct scatterlist *sg, unsigned int sgcount, unsigned int dma_length,
		 unsigned int dev_addr, dmamode_t dmamode);

int
imx_dma_setup_handlers(imx_dmach_t dma_ch,
		void (*irq_handler) (int, void *),
		void (*err_handler) (int, void *, int), void *data);

void imx_dma_enable(imx_dmach_t dma_ch);

void imx_dma_disable(imx_dmach_t dma_ch);

int imx_dma_request(imx_dmach_t dma_ch, const char *name);

void imx_dma_free(imx_dmach_t dma_ch);

int imx_dma_request_by_prio(imx_dmach_t *pdma_ch, const char *name, imx_dma_prio prio);


#endif	/* _ASM_ARCH_IMX_DMA_H */
