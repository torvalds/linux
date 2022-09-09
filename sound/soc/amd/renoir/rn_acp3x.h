/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ALSA SoC PDM Driver
 *
 * Copyright 2020 Advanced Micro Devices, Inc.
 */

#include "rn_chip_offset_byte.h"

#define ACP_DEVS		3
#define ACP_PHY_BASE_ADDRESS 0x1240000
#define	ACP_REG_START	0x1240000
#define	ACP_REG_END	0x1250200

#define ACP_DEVICE_ID 0x15E2
#define ACP_POWER_ON 0x00
#define ACP_POWER_ON_IN_PROGRESS 0x01
#define ACP_POWER_OFF 0x02
#define ACP_POWER_OFF_IN_PROGRESS 0x03
#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK	0x00010001

#define ACP_PGFSM_CNTL_POWER_ON_MASK    0x01
#define ACP_PGFSM_CNTL_POWER_OFF_MASK   0x00
#define ACP_PGFSM_STATUS_MASK           0x03
#define ACP_POWERED_ON                  0x00
#define ACP_POWER_ON_IN_PROGRESS        0x01
#define ACP_POWERED_OFF                 0x02
#define ACP_POWER_OFF_IN_PROGRESS       0x03

#define ACP_ERROR_MASK 0x20000000
#define ACP_EXT_INTR_STAT_CLEAR_MASK 0xFFFFFFFF
#define PDM_DMA_STAT 0x10
#define PDM_DMA_INTR_MASK  0x10000
#define ACP_ERROR_STAT 29
#define PDM_DECIMATION_FACTOR 0x2
#define ACP_PDM_CLK_FREQ_MASK 0x07
#define ACP_WOV_MISC_CTRL_MASK 0x10
#define ACP_PDM_ENABLE 0x01
#define ACP_PDM_DISABLE 0x00
#define ACP_PDM_DMA_EN_STATUS 0x02
#define TWO_CH 0x02
#define DELAY_US 5
#define ACP_COUNTER 20000
/* time in ms for runtime suspend delay */
#define ACP_SUSPEND_DELAY_MS	2000

#define ACP_SRAM_PTE_OFFSET	0x02050000
#define PAGE_SIZE_4K_ENABLE     0x2
#define MEM_WINDOW_START	0x4000000

#define CAPTURE_MIN_NUM_PERIODS     4
#define CAPTURE_MAX_NUM_PERIODS     4
#define CAPTURE_MAX_PERIOD_SIZE     8192
#define CAPTURE_MIN_PERIOD_SIZE     4096

#define MAX_BUFFER (CAPTURE_MAX_PERIOD_SIZE * CAPTURE_MAX_NUM_PERIODS)
#define MIN_BUFFER MAX_BUFFER
#define	ACP_DMIC_AUTO   -1

struct pdm_dev_data {
	u32 pdm_irq;
	void __iomem *acp_base;
	struct snd_pcm_substream *capture_stream;
};

struct pdm_stream_instance {
	u16 num_pages;
	u16 channels;
	dma_addr_t dma_addr;
	u64 bytescount;
	void __iomem *acp_base;
};

union acp_pdm_dma_count {
	struct {
	u32 low;
	u32 high;
	} bcount;
	u64 bytescount;
};

static inline u32 rn_readl(void __iomem *base_addr)
{
	return readl(base_addr - ACP_PHY_BASE_ADDRESS);
}

static inline void rn_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr - ACP_PHY_BASE_ADDRESS);
}

/* Machine configuration */
int snd_amd_acp_find_config(struct pci_dev *pci);
