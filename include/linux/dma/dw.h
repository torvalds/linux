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

#endif /* _DMA_DW_H */
