/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MXS_DMA_H_
#define _MXS_DMA_H_

#include <linux/dmaengine.h>

/*
 * The mxs dmaengine can do PIO transfers. We pass a pointer to the PIO words
 * in the second argument to dmaengine_prep_slave_sg when the direction is
 * set to DMA_TRANS_NONE. To make this clear and to prevent users from doing
 * the error prone casting we have this wrapper function
 */
static inline struct dma_async_tx_descriptor *mxs_dmaengine_prep_pio(
        struct dma_chan *chan, u32 *pio, unsigned int npio,
        enum dma_transfer_direction dir, unsigned long flags)
{
	return dmaengine_prep_slave_sg(chan, (struct scatterlist *)pio, npio,
				       dir, flags);
}

#endif /* _MXS_DMA_H_ */
