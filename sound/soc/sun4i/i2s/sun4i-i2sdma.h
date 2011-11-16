/*
********************************************************************************************************
*                          SUN4I----HDMI AUDIO
*                   (c) Copyright 2002-2004, All winners Co,Ld.
*                          All Right Reserved
*
* FileName: sun4i-pcm.h   author:chenpailin  date:2011-07-19 
* Description: 
* Others: 
* History:
*   <author>      <time>      <version>   <desc> 
*   chenpailin   2011-07-19     1.0      modify this module 
********************************************************************************************************
*/

#ifndef SUN4I_PCM_H_
#define SUN4I_PCM_H_

#define ST_RUNNING    (1<<0)
#define ST_OPENED     (1<<1)

struct sun4i_dma_params {
	struct sw_dma_client *client;	
	int channel;				
	dma_addr_t dma_addr;
	int dma_size;			
};

#define SUN4I_DAI_I2S			1

enum sun4i_dma_buffresult {
	SUN4I_RES_OK,
	SUN4I_RES_ERR,
	SUN4I_RES_ABORT
};

/* platform data */
extern struct snd_soc_platform sun4i_soc_platform_i2s;
extern int sw_dma_enqueue(unsigned int channel, void *id,
			dma_addr_t data, int size);
extern struct sun4i_i2s_info sun4i_iis;

#endif //SUN4I_PCM_H_
