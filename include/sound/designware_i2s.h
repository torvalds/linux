/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (ST) 2012 Rajeev Kumar (rajeevkumar.linux@gmail.com)
 */

#ifndef __SOUND_DESIGNWARE_I2S_H
#define __SOUND_DESIGNWARE_I2S_H

#include <linux/dmaengine.h>
#include <linux/types.h>

/*
 * struct i2s_clk_config_data - represent i2s clk configuration data
 * @chan_nr: number of channel
 * @data_width: number of bits per sample (8/16/24/32 bit)
 * @sample_rate: sampling frequency (8Khz, 16Khz, 32Khz, 44Khz, 48Khz)
 */
struct i2s_clk_config_data {
	int chan_nr;
	u32 data_width;
	u32 sample_rate;
};

struct dw_i2s_dev;

struct i2s_platform_data {
	#define DWC_I2S_PLAY	(1 << 0)
	#define DWC_I2S_RECORD	(1 << 1)
	#define DW_I2S_SLAVE	(1 << 2)
	#define DW_I2S_MASTER	(1 << 3)
	unsigned int cap;
	int channel;
	u32 snd_fmts;
	u32 snd_rates;

	#define DW_I2S_QUIRK_COMP_REG_OFFSET	(1 << 0)
	#define DW_I2S_QUIRK_COMP_PARAM1	(1 << 1)
	#define DW_I2S_QUIRK_16BIT_IDX_OVERRIDE (1 << 2)
	unsigned int quirks;
	unsigned int i2s_reg_comp1;
	unsigned int i2s_reg_comp2;

	void *play_dma_data;
	void *capture_dma_data;
	bool (*filter)(struct dma_chan *chan, void *slave);
	int (*i2s_clk_cfg)(struct i2s_clk_config_data *config);
	int (*i2s_pd_init)(struct dw_i2s_dev *dev);
};

struct i2s_dma_data {
	void *data;
	dma_addr_t addr;
	u32 max_burst;
	enum dma_slave_buswidth addr_width;
	bool (*filter)(struct dma_chan *chan, void *slave);
};

/* I2S DMA registers */
#define I2S_RXDMA		0x01C0
#define I2S_TXDMA		0x01C8

#define TWO_CHANNEL_SUPPORT	2	/* up to 2.0 */
#define FOUR_CHANNEL_SUPPORT	4	/* up to 3.1 */
#define SIX_CHANNEL_SUPPORT	6	/* up to 5.1 */
#define EIGHT_CHANNEL_SUPPORT	8	/* up to 7.1 */

#endif /*  __SOUND_DESIGNWARE_I2S_H */
