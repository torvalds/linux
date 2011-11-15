/*
********************************************************************************************************
*                          SUN4I----HDMI AUDIO
*                   (c) Copyright 2002-2004, All winners Co,Ld.
*                          All Right Reserved
*
* FileName: sun4i-hdmipcm.h   author:chenpailin
* Description:
* Others:
* History:
*   <author>      <time>      <version>   <desc>
*   chenpailin   2011-07-19     1.0      modify this module
********************************************************************************************************
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
