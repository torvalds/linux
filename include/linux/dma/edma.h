/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA core driver
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#ifndef _DW_EDMA_H
#define _DW_EDMA_H

#include <linux/device.h>
#include <linux/dmaengine.h>

struct dw_edma;

/**
 * struct dw_edma_chip - representation of DesignWare eDMA controller hardware
 * @dev:		 struct device of the eDMA controller
 * @id:			 instance ID
 * @irq:		 irq line
 * @dw:			 struct dw_edma that is filed by dw_edma_probe()
 */
struct dw_edma_chip {
	struct device		*dev;
	int			id;
	int			irq;
	struct dw_edma		*dw;
};

/* Export to the platform drivers */
#if IS_ENABLED(CONFIG_DW_EDMA)
int dw_edma_probe(struct dw_edma_chip *chip);
int dw_edma_remove(struct dw_edma_chip *chip);
#else
static inline int dw_edma_probe(struct dw_edma_chip *chip)
{
	return -ENODEV;
}

static inline int dw_edma_remove(struct dw_edma_chip *chip)
{
	return 0;
}
#endif /* CONFIG_DW_EDMA */

#endif /* _DW_EDMA_H */
