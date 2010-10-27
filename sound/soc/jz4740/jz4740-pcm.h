/*
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _JZ4740_PCM_H
#define _JZ4740_PCM_H

#include <linux/dma-mapping.h>
#include <asm/mach-jz4740/dma.h>

/* platform data */
extern struct snd_soc_platform jz4740_soc_platform;

struct jz4740_pcm_config {
	struct jz4740_dma_config dma_config;
	phys_addr_t fifo_addr;
};

#endif
