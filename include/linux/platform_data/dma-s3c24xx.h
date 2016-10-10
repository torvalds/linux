/*
 * S3C24XX DMA handling
 *
 * Copyright (c) 2013 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/* Helper to encode the source selection constraints for early s3c socs. */
#define S3C24XX_DMA_CHANREQ(src, chan)	((BIT(3) | src) << chan * 4)

enum s3c24xx_dma_bus {
	S3C24XX_DMA_APB,
	S3C24XX_DMA_AHB,
};

/**
 * @bus: on which bus does the peripheral reside - AHB or APB.
 * @handshake: is a handshake with the peripheral necessary
 * @chansel: channel selection information, depending on variant; reqsel for
 *	     s3c2443 and later and channel-selection map for earlier SoCs
 *	     see CHANSEL doc in s3c2443-dma.c
 */
struct s3c24xx_dma_channel {
	enum s3c24xx_dma_bus bus;
	bool handshake;
	u16 chansel;
};

struct dma_slave_map;

/**
 * struct s3c24xx_dma_platdata - platform specific settings
 * @num_phy_channels: number of physical channels
 * @channels: array of virtual channel descriptions
 * @num_channels: number of virtual channels
 * @slave_map: dma slave map matching table
 * @slavecnt: number of elements in slave_map
 */
struct s3c24xx_dma_platdata {
	int num_phy_channels;
	struct s3c24xx_dma_channel *channels;
	int num_channels;
	const struct dma_slave_map *slave_map;
	int slavecnt;
};

struct dma_chan;
bool s3c24xx_dma_filter(struct dma_chan *chan, void *param);
