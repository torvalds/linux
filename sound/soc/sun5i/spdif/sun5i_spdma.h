/*
 * sound\soc\sun5i\spdif\sun5i_spdma.h
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * chenpailin <chenpailin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#ifndef SUN5I_SPDMA_H_
#define SUN5I_SPDMA_H_

#define ST_RUNNING    (1<<0)
#define ST_OPENED     (1<<1)

struct sun5i_dma_params {
	struct sw_dma_client *client;	/* stream identifier */
	unsigned int channel;				/* Channel ID */
	dma_addr_t dma_addr;
	int dma_size;			/* Size of the DMA transfer */
};

#define SUN5I_DAI_SPDIF			1

enum sun5idma_buffresult {
	SUN5I_RES_OK,
	SUN5I_RES_ERR,
	SUN5I_RES_ABORT
};
/* platform data */
extern int sw_dma_enqueue(unsigned int channel, void *id,
			dma_addr_t data, int size);

#endif