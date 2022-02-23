/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ALSA SoC PCM Driver
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.
 */

#include "vg_chip_offset_byte.h"
#include <sound/pcm.h>

#define ACP5x_PHY_BASE_ADDRESS 0x1240000
#define ACP_DEVICE_ID 0x15E2
#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK	0x00010001

#define ACP_PGFSM_CNTL_POWER_ON_MASK	0x01
#define ACP_PGFSM_CNTL_POWER_OFF_MASK	0x00
#define ACP_PGFSM_STATUS_MASK		0x03
#define ACP_POWERED_ON			0x00
#define ACP_POWER_ON_IN_PROGRESS	0x01
#define ACP_POWERED_OFF			0x02
#define ACP_POWER_OFF_IN_PROGRESS	0x03

#define ACP_ERR_INTR_MASK	0x20000000
#define ACP_EXT_INTR_STAT_CLEAR_MASK 0xFFFFFFFF

#define ACP5x_DEVS 4
#define	ACP5x_REG_START	0x1240000
#define	ACP5x_REG_END	0x1250200
#define ACP5x_I2STDM_REG_START	0x1242400
#define ACP5x_I2STDM_REG_END	0x1242410
#define ACP5x_HS_TDM_REG_START	0x1242814
#define ACP5x_HS_TDM_REG_END	0x1242824
#define I2S_MODE 0
#define ACP5x_I2S_MODE 1
#define ACP5x_RES 4
#define	I2S_RX_THRESHOLD 27
#define	I2S_TX_THRESHOLD 28
#define	HS_TX_THRESHOLD 24
#define	HS_RX_THRESHOLD 23

#define I2S_SP_INSTANCE                 1
#define I2S_HS_INSTANCE                 2

#define ACP_SRAM_PTE_OFFSET	0x02050000
#define ACP_SRAM_SP_PB_PTE_OFFSET	0x0
#define ACP_SRAM_SP_CP_PTE_OFFSET	0x100
#define ACP_SRAM_HS_PB_PTE_OFFSET	0x200
#define ACP_SRAM_HS_CP_PTE_OFFSET	0x300
#define PAGE_SIZE_4K_ENABLE 0x2
#define I2S_SP_TX_MEM_WINDOW_START	0x4000000
#define I2S_SP_RX_MEM_WINDOW_START	0x4020000
#define I2S_HS_TX_MEM_WINDOW_START	0x4040000
#define I2S_HS_RX_MEM_WINDOW_START	0x4060000

#define SP_PB_FIFO_ADDR_OFFSET		0x500
#define SP_CAPT_FIFO_ADDR_OFFSET	0x700
#define HS_PB_FIFO_ADDR_OFFSET		0x900
#define HS_CAPT_FIFO_ADDR_OFFSET	0xB00
#define PLAYBACK_MIN_NUM_PERIODS    2
#define PLAYBACK_MAX_NUM_PERIODS    8
#define PLAYBACK_MAX_PERIOD_SIZE    8192
#define PLAYBACK_MIN_PERIOD_SIZE    1024
#define CAPTURE_MIN_NUM_PERIODS     2
#define CAPTURE_MAX_NUM_PERIODS     8
#define CAPTURE_MAX_PERIOD_SIZE     8192
#define CAPTURE_MIN_PERIOD_SIZE     1024

#define MAX_BUFFER (PLAYBACK_MAX_PERIOD_SIZE * PLAYBACK_MAX_NUM_PERIODS)
#define MIN_BUFFER MAX_BUFFER
#define FIFO_SIZE 0x100
#define DMA_SIZE 0x40
#define FRM_LEN 0x100

#define I2S_MASTER_MODE_ENABLE 1
#define I2S_MASTER_MODE_DISABLE 0

#define SLOT_WIDTH_8 8
#define SLOT_WIDTH_16 16
#define SLOT_WIDTH_24 24
#define SLOT_WIDTH_32 32
#define TDM_ENABLE 1
#define TDM_DISABLE 0
#define ACP5x_ITER_IRER_SAMP_LEN_MASK	0x38

struct i2s_dev_data {
	bool tdm_mode;
	bool master_mode;
	unsigned int i2s_irq;
	u16 i2s_instance;
	u32 tdm_fmt;
	void __iomem *acp5x_base;
	struct snd_pcm_substream *play_stream;
	struct snd_pcm_substream *capture_stream;
	struct snd_pcm_substream *i2ssp_play_stream;
	struct snd_pcm_substream *i2ssp_capture_stream;
};

struct i2s_stream_instance {
	u16 num_pages;
	u16 i2s_instance;
	u16 direction;
	u16 channels;
	u32 xfer_resolution;
	u32 val;
	dma_addr_t dma_addr;
	u64 bytescount;
	void __iomem *acp5x_base;
	u32 lrclk_div;
	u32 bclk_div;
};

union acp_dma_count {
	struct {
		u32 low;
		u32 high;
	} bcount;
	u64 bytescount;
};

struct acp5x_platform_info {
	u16 play_i2s_instance;
	u16 cap_i2s_instance;
};

union acp_i2stdm_mstrclkgen {
	struct {
		u32 i2stdm_master_mode : 1;
		u32 i2stdm_format_mode : 1;
		u32 i2stdm_lrclk_div_val : 9;
		u32 i2stdm_bclk_div_val : 11;
		u32:10;
	} bitfields, bits;
	u32  u32_all;
};

/* common header file uses exact offset rather than relative
 * offset which requires subtraction logic from base_addr
 * for accessing ACP5x MMIO space registers
 */
static inline u32 acp_readl(void __iomem *base_addr)
{
	return readl(base_addr - ACP5x_PHY_BASE_ADDRESS);
}

static inline void acp_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr - ACP5x_PHY_BASE_ADDRESS);
}

static inline u64 acp_get_byte_count(struct i2s_stream_instance *rtd,
				     int direction)
{
	union acp_dma_count byte_count;

	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (rtd->i2s_instance) {
		case I2S_HS_INSTANCE:
			byte_count.bcount.high =
				acp_readl(rtd->acp5x_base +
					  ACP_HS_TX_LINEARPOSCNTR_HIGH);
			byte_count.bcount.low =
				acp_readl(rtd->acp5x_base +
					  ACP_HS_TX_LINEARPOSCNTR_LOW);
			break;
		case I2S_SP_INSTANCE:
		default:
			byte_count.bcount.high =
				acp_readl(rtd->acp5x_base +
					  ACP_I2S_TX_LINEARPOSCNTR_HIGH);
			byte_count.bcount.low =
				acp_readl(rtd->acp5x_base +
					  ACP_I2S_TX_LINEARPOSCNTR_LOW);
		}
	} else {
		switch (rtd->i2s_instance) {
		case I2S_HS_INSTANCE:
			byte_count.bcount.high =
				acp_readl(rtd->acp5x_base +
					  ACP_HS_RX_LINEARPOSCNTR_HIGH);
			byte_count.bcount.low =
				acp_readl(rtd->acp5x_base +
					  ACP_HS_RX_LINEARPOSCNTR_LOW);
			break;
		case I2S_SP_INSTANCE:
		default:
			byte_count.bcount.high =
				acp_readl(rtd->acp5x_base +
					  ACP_I2S_RX_LINEARPOSCNTR_HIGH);
			byte_count.bcount.low =
				acp_readl(rtd->acp5x_base +
					  ACP_I2S_RX_LINEARPOSCNTR_LOW);
		}
	}
	return byte_count.bytescount;
}

static inline void acp5x_set_i2s_clk(struct i2s_dev_data *adata,
				     struct i2s_stream_instance *rtd)
{
	union acp_i2stdm_mstrclkgen mclkgen;
	u32 master_reg;

	switch (rtd->i2s_instance) {
	case I2S_HS_INSTANCE:
		master_reg = ACP_I2STDM2_MSTRCLKGEN;
		break;
	case I2S_SP_INSTANCE:
	default:
		master_reg = ACP_I2STDM0_MSTRCLKGEN;
		break;
	}

	mclkgen.bits.i2stdm_master_mode = 0x1;
	if (adata->tdm_mode)
		mclkgen.bits.i2stdm_format_mode = 0x01;
	else
		mclkgen.bits.i2stdm_format_mode = 0x00;

	mclkgen.bits.i2stdm_bclk_div_val = rtd->bclk_div;
	mclkgen.bits.i2stdm_lrclk_div_val = rtd->lrclk_div;
	acp_writel(mclkgen.u32_all, rtd->acp5x_base + master_reg);
}
