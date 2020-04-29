/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PXA_DMA_H_
#define _PXA_DMA_H_

enum pxad_chan_prio {
	PXAD_PRIO_HIGHEST = 0,
	PXAD_PRIO_NORMAL,
	PXAD_PRIO_LOW,
	PXAD_PRIO_LOWEST,
};

/**
 * struct pxad_param - dma channel request parameters
 * @drcmr: requestor line number
 * @prio: minimal mandatory priority of the channel
 *
 * If a requested channel is granted, its priority will be at least @prio,
 * ie. if PXAD_PRIO_LOW is required, the requested channel will be either
 * PXAD_PRIO_LOW, PXAD_PRIO_NORMAL or PXAD_PRIO_HIGHEST.
 */
struct pxad_param {
	unsigned int drcmr;
	enum pxad_chan_prio prio;
};

#endif /* _PXA_DMA_H_ */
