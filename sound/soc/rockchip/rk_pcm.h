/*
 * rockchip-pcm.h - ALSA PCM interface for the Rockchip rk28 SoC
 *
 * Driver for rockchip iis audio
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ROCKCHIP_PCM_H
#define _ROCKCHIP_PCM_H

#include <mach/hardware.h>

#define ST_RUNNING		(1<<0)
#define ST_OPENED		(1<<1)

struct rockchip_pcm_dma_params {
	struct rk29_dma_client *client;	/* stream identifier */
	int channel;				/* Channel ID */
	dma_addr_t dma_addr;
	int dma_size;				/* Size of the DMA transfer */
	int flag;                               /*burst change flag*/
};

extern struct snd_soc_platform rk29_soc_platform;

#endif /* _ROCKCHIP_PCM_H */
