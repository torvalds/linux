/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *		http://www.samsung.com
 */

#ifndef __SND_SOC_SAMSUNG_IDMA_H_
#define __SND_SOC_SAMSUNG_IDMA_H_

extern void idma_reg_addr_init(void __iomem *regs, dma_addr_t addr);

/* dma_state */
#define LPAM_DMA_STOP	0
#define LPAM_DMA_START	1

#define MAX_IDMA_PERIOD (128 * 1024)
#define MAX_IDMA_BUFFER (160 * 1024)

#endif /* __SND_SOC_SAMSUNG_IDMA_H_ */
