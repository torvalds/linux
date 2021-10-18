/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ALSA SoC PDM Driver
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.
 */

#include "acp6x_chip_offset_byte.h"

#define ACP_DEVICE_ID 0x15E2
#define ACP6x_PHY_BASE_ADDRESS 0x1240000
#define ACP6x_REG_START		0x1240000
#define ACP6x_REG_END		0x1250200
#define ACP6x_DEVS		2
#define ACP6x_PDM_MODE		1

#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK	0x00010001
#define ACP_PGFSM_CNTL_POWER_ON_MASK	1
#define ACP_PGFSM_CNTL_POWER_OFF_MASK	0
#define ACP_PGFSM_STATUS_MASK		3
#define ACP_POWERED_ON			0
#define ACP_POWER_ON_IN_PROGRESS	1
#define ACP_POWERED_OFF			2
#define ACP_POWER_OFF_IN_PROGRESS	3

#define ACP_ERROR_MASK 0x20000000
#define ACP_EXT_INTR_STAT_CLEAR_MASK 0xFFFFFFFF
#define PDM_DMA_STAT 0x10

enum acp_config {
	ACP_CONFIG_0 = 0,
	ACP_CONFIG_1,
	ACP_CONFIG_2,
	ACP_CONFIG_3,
	ACP_CONFIG_4,
	ACP_CONFIG_5,
	ACP_CONFIG_6,
	ACP_CONFIG_7,
	ACP_CONFIG_8,
	ACP_CONFIG_9,
	ACP_CONFIG_10,
	ACP_CONFIG_11,
	ACP_CONFIG_12,
	ACP_CONFIG_13,
	ACP_CONFIG_14,
	ACP_CONFIG_15,
};

struct pdm_dev_data {
	u32 pdm_irq;
	void __iomem *acp6x_base;
	struct snd_pcm_substream *capture_stream;
};

static inline u32 acp6x_readl(void __iomem *base_addr)
{
	return readl(base_addr - ACP6x_PHY_BASE_ADDRESS);
}

static inline void acp6x_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr - ACP6x_PHY_BASE_ADDRESS);
}
