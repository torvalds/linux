/*
 * Driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2007 Atmel Corporation
 * Copyright (C) 2010-2011 ST Microelectronics
 * Copyright (C) 2014 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _DMA_DW_H
#define _DMA_DW_H

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dmaengine.h>

#include <linux/platform_data/dma-dw.h>

struct dw_dma;

/**
 * struct dw_dma_chip - representation of DesignWare DMA controller hardware
 * @dev:		struct device of the DMA controller
 * @id:			instance ID
 * @irq:		irq line
 * @regs:		memory mapped I/O space
 * @clk:		hclk clock
 * @dw:			struct dw_dma that is filed by dw_dma_probe()
 * @pdata:		pointer to platform data
 */
struct dw_dma_chip {
	struct device	*dev;
	int		id;
	int		irq;
	void __iomem	*regs;
	struct clk	*clk;
	struct dw_dma	*dw;

	const struct dw_dma_platform_data	*pdata;
};

/* Export to the platform drivers */
#if IS_ENABLED(CONFIG_DW_DMAC_CORE)
int dw_dma_probe(struct dw_dma_chip *chip);
int dw_dma_remove(struct dw_dma_chip *chip);
#else
static inline int dw_dma_probe(struct dw_dma_chip *chip) { return -ENODEV; }
static inline int dw_dma_remove(struct dw_dma_chip *chip) { return 0; }
#endif /* CONFIG_DW_DMAC_CORE */

/* DMA API extensions */
struct dw_desc;

struct dw_cyclic_desc {
	struct dw_desc	**desc;
	unsigned long	periods;
	void		(*period_callback)(void *param);
	void		*period_callback_param;
};

struct dw_cyclic_desc *dw_dma_cyclic_prep(struct dma_chan *chan,
		dma_addr_t buf_addr, size_t buf_len, size_t period_len,
		enum dma_transfer_direction direction);
void dw_dma_cyclic_free(struct dma_chan *chan);
int dw_dma_cyclic_start(struct dma_chan *chan);
void dw_dma_cyclic_stop(struct dma_chan *chan);

dma_addr_t dw_dma_get_src_addr(struct dma_chan *chan);

dma_addr_t dw_dma_get_dst_addr(struct dma_chan *chan);

#endif /* _DMA_DW_H */
