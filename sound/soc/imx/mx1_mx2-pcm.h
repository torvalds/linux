/*
 * mx1_mx2-pcm.h :- ASoC platform header for Freescale i.MX1x, i.MX2x
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MX1_MX2_PCM_H
#define _MX1_MX2_PCM_H

/* DMA information for mx1_mx2 platforms */
struct mx1_mx2_pcm_dma_params {
	char *name;			/* stream identifier */
	unsigned int transfer_type;	/* READ or WRITE DMA transfer */
	dma_addr_t per_address;		/* physical address of SSI fifo */
	int event_id;			/* fixed DMA number for SSI fifo */
	int watermark_level;		/* SSI fifo watermark level */
	int per_config;			/* DMA Config flags for peripheral */
	int mem_config;			/* DMA Config flags for RAM */
 };

/* platform data */
extern struct snd_soc_platform mx1_mx2_soc_platform;

#endif
