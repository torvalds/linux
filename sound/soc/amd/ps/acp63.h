/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ALSA SoC PDM Driver
 *
 * Copyright (C) 2022 Advanced Micro Devices, Inc. All rights reserved.
 */

#include <sound/acp63_chip_offset_byte.h>

#define ACP_DEVICE_ID 0x15E2
#define ACP63_REG_START		0x1240000
#define ACP63_REG_END		0x1250200
#define ACP63_DEVS		3

#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK	0x00010001
#define ACP_PGFSM_CNTL_POWER_ON_MASK	1
#define ACP_PGFSM_CNTL_POWER_OFF_MASK	0
#define ACP_PGFSM_STATUS_MASK		3
#define ACP_POWERED_ON			0
#define ACP_POWER_ON_IN_PROGRESS	1
#define ACP_POWERED_OFF		2
#define ACP_POWER_OFF_IN_PROGRESS	3

#define ACP_ERROR_MASK 0x20000000
#define ACP_EXT_INTR_STAT_CLEAR_MASK 0xFFFFFFFF
#define PDM_DMA_STAT 0x10

#define PDM_DMA_INTR_MASK	0x10000
#define ACP_ERROR_STAT	29
#define PDM_DECIMATION_FACTOR	2
#define ACP_PDM_CLK_FREQ_MASK	7
#define ACP_WOV_GAIN_CONTROL	GENMASK(4, 3)
#define ACP_PDM_ENABLE		1
#define ACP_PDM_DISABLE		0
#define ACP_PDM_DMA_EN_STATUS	2
#define TWO_CH		2
#define DELAY_US	5
#define ACP_COUNTER	20000

#define ACP_SRAM_PTE_OFFSET	0x03800000
#define PAGE_SIZE_4K_ENABLE	2
#define PDM_PTE_OFFSET		0
#define PDM_MEM_WINDOW_START	0x4000000

#define CAPTURE_MIN_NUM_PERIODS     4
#define CAPTURE_MAX_NUM_PERIODS     4
#define CAPTURE_MAX_PERIOD_SIZE     8192
#define CAPTURE_MIN_PERIOD_SIZE     4096

#define MAX_BUFFER (CAPTURE_MAX_PERIOD_SIZE * CAPTURE_MAX_NUM_PERIODS)
#define MIN_BUFFER MAX_BUFFER

/* time in ms for runtime suspend delay */
#define ACP_SUSPEND_DELAY_MS	2000

#define ACP63_DMIC_ADDR		2
#define ACP63_PDM_MODE_DEVS		3
#define ACP63_PDM_DEV_MASK		1
#define ACP_DMIC_DEV	2

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

struct pdm_stream_instance {
	u16 num_pages;
	u16 channels;
	dma_addr_t dma_addr;
	u64 bytescount;
	void __iomem *acp63_base;
};

struct pdm_dev_data {
	u32 pdm_irq;
	void __iomem *acp63_base;
	struct mutex *acp_lock;
	struct snd_pcm_substream *capture_stream;
};

static inline u32 acp63_readl(void __iomem *base_addr)
{
	return readl(base_addr);
}

static inline void acp63_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr);
}

struct acp63_dev_data {
	void __iomem *acp63_base;
	struct resource *res;
	struct platform_device *pdev[ACP63_DEVS];
	struct mutex acp_lock; /* protect shared registers */
	u16 pdev_mask;
	u16 pdev_count;
	u16 pdm_dev_index;
};
