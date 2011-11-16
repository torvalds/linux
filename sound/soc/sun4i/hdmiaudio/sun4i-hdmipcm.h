/*
 * sound\soc\sun4i\hdmiaudio\sun4i-hdmipcm.h
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

#ifndef SUN4I_HDMIPCM_H_
#define SUN4I_HDMIPCM_H_

struct sun4i_dma_params {
	struct sw_dma_client *client;	
	int channel;				
	dma_addr_t dma_addr;
	int dma_size;			
};

enum sun4i_dma_buffresult {
	SUN4I_RES_OK,
	SUN4I_RES_ERR,
	SUN4I_RES_ABORT
};

/* platform data */
extern int sw_dma_enqueue(unsigned int channel, void *id,
			dma_addr_t data, int size);
			
#endif //SUN4I_HDMIPCM_H_
