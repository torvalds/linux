/*
 * sound\soc\sunxi\i2s\sunxi-i2sdma.h
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


#ifndef SUNXI_PCM_H_
#define SUNXI_PCM_H_

#define ST_RUNNING    (1<<0)
#define ST_OPENED     (1<<1)

#define SUNXI_DAI_I2S			1

enum sunxi_dma_buffresult {
	SUNXI_RES_OK,
	SUNXI_RES_ERR,
	SUNXI_RES_ABORT
};

/* platform data */
extern struct snd_soc_platform sunxi_soc_platform_i2s;
extern struct sunxi_i2s_info sunxi_iis;

#endif //SUNXI_PCM_H_
