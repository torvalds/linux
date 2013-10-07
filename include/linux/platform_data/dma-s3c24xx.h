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

/**
 * struct s3c24xx_dma_platdata - platform specific settings
 * @num_phy_channels: number of physical channels
 * @channels: array of virtual channel descriptions
 * @num_channels: number of virtual channels
 */
struct s3c24xx_dma_platdata {
	int num_phy_channels;
	struct s3c24xx_dma_channel *channels;
	int num_channels;
};

struct dma_chan;
bool s3c24xx_dma_filter(struct dma_chan *chan, void *param);
