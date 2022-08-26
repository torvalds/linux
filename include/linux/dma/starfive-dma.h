/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _STARFIVE_DMA_H_
#define _STARFIVE_DMA_H_

#include <linux/dmaengine.h>

void axi_dma_cyclic_stop(struct dma_chan *chan);

#endif /* _STARFIVE_DMA_H_ */
