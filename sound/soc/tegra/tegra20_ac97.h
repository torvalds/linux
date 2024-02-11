/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tegra20_ac97.h - Definitions for the Tegra20 AC97 controller driver
 *
 * Copyright (c) 2012 Lucas Stach <dev@lynxeye.de>
 *
 * Partly based on code copyright/by:
 *
 * Copyright (c) 2011,2012 Toradex Inc.
 */

#ifndef __TEGRA20_AC97_H__
#define __TEGRA20_AC97_H__

#include "tegra_pcm.h"

#define TEGRA20_AC97_CTRL				0x00
#define TEGRA20_AC97_CMD				0x04
#define TEGRA20_AC97_STATUS1				0x08
/* ... */
#define TEGRA20_AC97_FIFO1_SCR				0x1c
/* ... */
#define TEGRA20_AC97_FIFO_TX1				0x40
#define TEGRA20_AC97_FIFO_RX1				0x80

/* TEGRA20_AC97_CTRL */
#define TEGRA20_AC97_CTRL_STM2_EN			(1 << 16)
#define TEGRA20_AC97_CTRL_DOUBLE_SAMPLING_EN		(1 << 11)
#define TEGRA20_AC97_CTRL_IO_CNTRL_EN			(1 << 10)
#define TEGRA20_AC97_CTRL_HSET_DAC_EN			(1 << 9)
#define TEGRA20_AC97_CTRL_LINE2_DAC_EN			(1 << 8)
#define TEGRA20_AC97_CTRL_PCM_LFE_EN			(1 << 7)
#define TEGRA20_AC97_CTRL_PCM_SUR_EN			(1 << 6)
#define TEGRA20_AC97_CTRL_PCM_CEN_DAC_EN		(1 << 5)
#define TEGRA20_AC97_CTRL_LINE1_DAC_EN			(1 << 4)
#define TEGRA20_AC97_CTRL_PCM_DAC_EN			(1 << 3)
#define TEGRA20_AC97_CTRL_COLD_RESET			(1 << 2)
#define TEGRA20_AC97_CTRL_WARM_RESET			(1 << 1)
#define TEGRA20_AC97_CTRL_STM_EN			(1 << 0)

/* TEGRA20_AC97_CMD */
#define TEGRA20_AC97_CMD_CMD_ADDR_SHIFT			24
#define TEGRA20_AC97_CMD_CMD_ADDR_MASK			(0xff << TEGRA20_AC97_CMD_CMD_ADDR_SHIFT)
#define TEGRA20_AC97_CMD_CMD_DATA_SHIFT			8
#define TEGRA20_AC97_CMD_CMD_DATA_MASK			(0xffff << TEGRA20_AC97_CMD_CMD_DATA_SHIFT)
#define TEGRA20_AC97_CMD_CMD_ID_SHIFT			2
#define TEGRA20_AC97_CMD_CMD_ID_MASK			(0x3 << TEGRA20_AC97_CMD_CMD_ID_SHIFT)
#define TEGRA20_AC97_CMD_BUSY				(1 << 0)

/* TEGRA20_AC97_STATUS1 */
#define TEGRA20_AC97_STATUS1_STA_ADDR1_SHIFT		24
#define TEGRA20_AC97_STATUS1_STA_ADDR1_MASK		(0xff << TEGRA20_AC97_STATUS1_STA_ADDR1_SHIFT)
#define TEGRA20_AC97_STATUS1_STA_DATA1_SHIFT		8
#define TEGRA20_AC97_STATUS1_STA_DATA1_MASK		(0xffff << TEGRA20_AC97_STATUS1_STA_DATA1_SHIFT)
#define TEGRA20_AC97_STATUS1_STA_VALID1			(1 << 2)
#define TEGRA20_AC97_STATUS1_STANDBY1			(1 << 1)
#define TEGRA20_AC97_STATUS1_CODEC1_RDY			(1 << 0)

/* TEGRA20_AC97_FIFO1_SCR */
#define TEGRA20_AC97_FIFO_SCR_REC_MT_CNT_SHIFT		27
#define TEGRA20_AC97_FIFO_SCR_REC_MT_CNT_MASK		(0x1f << TEGRA20_AC97_FIFO_SCR_REC_MT_CNT_SHIFT)
#define TEGRA20_AC97_FIFO_SCR_PB_MT_CNT_SHIFT		22
#define TEGRA20_AC97_FIFO_SCR_PB_MT_CNT_MASK		(0x1f << TEGRA20_AC97_FIFO_SCR_PB_MT_CNT_SHIFT)
#define TEGRA20_AC97_FIFO_SCR_REC_OVERRUN_INT_STA	(1 << 19)
#define TEGRA20_AC97_FIFO_SCR_PB_UNDERRUN_INT_STA	(1 << 18)
#define TEGRA20_AC97_FIFO_SCR_REC_FORCE_MT		(1 << 17)
#define TEGRA20_AC97_FIFO_SCR_PB_FORCE_MT		(1 << 16)
#define TEGRA20_AC97_FIFO_SCR_REC_FULL_EN		(1 << 15)
#define TEGRA20_AC97_FIFO_SCR_REC_3QRT_FULL_EN		(1 << 14)
#define TEGRA20_AC97_FIFO_SCR_REC_QRT_FULL_EN		(1 << 13)
#define TEGRA20_AC97_FIFO_SCR_REC_EMPTY_EN		(1 << 12)
#define TEGRA20_AC97_FIFO_SCR_PB_NOT_FULL_EN		(1 << 11)
#define TEGRA20_AC97_FIFO_SCR_PB_QRT_MT_EN		(1 << 10)
#define TEGRA20_AC97_FIFO_SCR_PB_3QRT_MT_EN		(1 << 9)
#define TEGRA20_AC97_FIFO_SCR_PB_EMPTY_MT_EN		(1 << 8)

struct tegra20_ac97 {
	struct clk *clk_ac97;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct reset_control *reset;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *sync_gpio;
};
#endif /* __TEGRA20_AC97_H__ */
