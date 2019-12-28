/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ALSA SoC PCM Driver
 *
 * Copyright 2016 Advanced Micro Devices, Inc.
 */

#include "chip_offset_byte.h"
#include <sound/pcm.h>

#define ACP3x_DEVS		3
#define ACP3x_PHY_BASE_ADDRESS 0x1240000
#define	ACP3x_I2S_MODE	0
#define	ACP3x_REG_START	0x1240000
#define	ACP3x_REG_END	0x1250200
#define ACP3x_I2STDM_REG_START	0x1242400
#define ACP3x_I2STDM_REG_END	0x1242410
#define ACP3x_BT_TDM_REG_START	0x1242800
#define ACP3x_BT_TDM_REG_END	0x1242810
#define I2S_MODE	0x04
#define	BT_TX_THRESHOLD 26
#define	BT_RX_THRESHOLD 25
#define ACP3x_POWER_ON 0x00
#define ACP3x_POWER_ON_IN_PROGRESS 0x01
#define ACP3x_POWER_OFF 0x02
#define ACP3x_POWER_OFF_IN_PROGRESS 0x03
#define ACP3x_SOFT_RESET__SoftResetAudDone_MASK	0x00010001

#define ACP_SRAM_PTE_OFFSET	0x02050000
#define PAGE_SIZE_4K_ENABLE 0x2
#define MEM_WINDOW_START	0x4000000
#define PLAYBACK_FIFO_ADDR_OFFSET 0x400
#define CAPTURE_FIFO_ADDR_OFFSET  0x500

#define PLAYBACK_MIN_NUM_PERIODS    2
#define PLAYBACK_MAX_NUM_PERIODS    8
#define PLAYBACK_MAX_PERIOD_SIZE    16384
#define PLAYBACK_MIN_PERIOD_SIZE    4096
#define CAPTURE_MIN_NUM_PERIODS     2
#define CAPTURE_MAX_NUM_PERIODS     8
#define CAPTURE_MAX_PERIOD_SIZE     16384
#define CAPTURE_MIN_PERIOD_SIZE     4096

#define MAX_BUFFER (PLAYBACK_MAX_PERIOD_SIZE * PLAYBACK_MAX_NUM_PERIODS)
#define MIN_BUFFER MAX_BUFFER
#define FIFO_SIZE 0x100
#define DMA_SIZE 0x40
#define FRM_LEN 0x100

#define SLOT_WIDTH_8 0x08
#define SLOT_WIDTH_16 0x10
#define SLOT_WIDTH_24 0x18
#define SLOT_WIDTH_32 0x20

struct acp3x_platform_info {
	u16 play_i2s_instance;
	u16 cap_i2s_instance;
	u16 capture_channel;
};

struct i2s_dev_data {
	bool tdm_mode;
	unsigned int i2s_irq;
	u32 tdm_fmt;
	u32 substream_type;
	void __iomem *acp3x_base;
	struct snd_pcm_substream *play_stream;
	struct snd_pcm_substream *capture_stream;
};

struct i2s_stream_instance {
	u16 num_pages;
	u16 channels;
	u32 xfer_resolution;
	u64 bytescount;
	dma_addr_t dma_addr;
	void __iomem *acp3x_base;
};

static inline u32 rv_readl(void __iomem *base_addr)
{
	return readl(base_addr - ACP3x_PHY_BASE_ADDRESS);
}

static inline void rv_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr - ACP3x_PHY_BASE_ADDRESS);
}

static inline u64 acp_get_byte_count(struct i2s_stream_instance *rtd,
							int direction)
{
	u64 byte_count;

	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		byte_count = rv_readl(rtd->acp3x_base +
				mmACP_BT_TX_LINEARPOSITIONCNTR_HIGH);
		byte_count |= rv_readl(rtd->acp3x_base +
				mmACP_BT_TX_LINEARPOSITIONCNTR_LOW);
	} else {
		byte_count = rv_readl(rtd->acp3x_base +
				mmACP_BT_RX_LINEARPOSITIONCNTR_HIGH);
		byte_count |= rv_readl(rtd->acp3x_base +
				mmACP_BT_RX_LINEARPOSITIONCNTR_LOW);
	}
	return byte_count;
}
