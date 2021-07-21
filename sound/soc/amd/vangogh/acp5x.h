/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ALSA SoC PCM Driver
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.
 */

#include "vg_chip_offset_byte.h"

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

#define ACP5x_DEVS 3
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

struct i2s_dev_data {
	unsigned int i2s_irq;
	void __iomem *acp5x_base;
	struct snd_pcm_substream *play_stream;
	struct snd_pcm_substream *capture_stream;
	struct snd_pcm_substream *i2ssp_play_stream;
	struct snd_pcm_substream *i2ssp_capture_stream;
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
