/* linux/include/linux/amba/pl330.h
 *
 * Copyright (C) 2010 Samsung Electronics Co. Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef	__AMBA_PL330_H_
#define	__AMBA_PL330_H_

#include <linux/dmaengine.h>

struct dma_pl330_platdata {
	/*
	 * Number of valid peripherals connected to DMAC.
	 * This may be different from the value read from
	 * CR0, as the PL330 implementation might have 'holes'
	 * in the peri list or the peri could also be reached
	 * from another DMAC which the platform prefers.
	 */
	u8 nr_valid_peri;
	/* Array of valid peripherals */
	u8 *peri_id;
	/* Operational capabilities */
	dma_cap_mask_t cap_mask;
	/* Bytes to allocate for MC buffer */
	unsigned mcbuf_sz;
	/* Copy alignment */
	u8 copy_align;
};

extern bool pl330_filter(struct dma_chan *chan, void *param);
extern int pl330_dma_getposition(struct dma_chan *chan,
		dma_addr_t *src, dma_addr_t *dst);
#endif	/* __AMBA_PL330_H_ */
