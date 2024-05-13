/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ALSA SoC PDM Driver
 *
 * Copyright (C) 2022, 2023 Advanced Micro Devices, Inc. All rights reserved.
 */

#include <sound/acp63_chip_offset_byte.h>

#define ACP_DEVICE_ID 0x15E2
#define ACP63_REG_START		0x1240000
#define ACP63_REG_END		0x1250200
#define ACP63_DEVS		5

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

#define ACP_DMIC_DEV	2

/* ACP63_PDM_MODE_DEVS corresponds to platform devices count for ACP PDM configuration */
#define ACP63_PDM_MODE_DEVS		3

/*
 * ACP63_SDW0_MODE_DEVS corresponds to platform devices count for
 * SW0 SoundWire manager instance configuration
 */
#define ACP63_SDW0_MODE_DEVS		2

/*
 * ACP63_SDW0_SDW1_MODE_DEVS corresponds to platform devices count for SW0 + SW1 SoundWire manager
 * instances configuration
 */
#define ACP63_SDW0_SDW1_MODE_DEVS	3

/*
 * ACP63_SDW0_PDM_MODE_DEVS corresponds to platform devices count for SW0 manager
 * instance + ACP PDM controller configuration
 */
#define ACP63_SDW0_PDM_MODE_DEVS	4

/*
 * ACP63_SDW0_SDW1_PDM_MODE_DEVS corresponds to platform devices count for
 * SW0 + SW1 SoundWire manager instances + ACP PDM controller configuration
 */
#define ACP63_SDW0_SDW1_PDM_MODE_DEVS   5
#define ACP63_DMIC_ADDR			2
#define ACP63_SDW_ADDR			5
#define AMD_SDW_MAX_MANAGERS		2

/* time in ms for acp timeout */
#define ACP_TIMEOUT		500

/* ACP63_PDM_DEV_CONFIG corresponds to platform device configuration for ACP PDM controller */
#define ACP63_PDM_DEV_CONFIG		BIT(0)

/* ACP63_SDW_DEV_CONFIG corresponds to platform device configuration for SDW manager instances */
#define ACP63_SDW_DEV_CONFIG		BIT(1)

/*
 * ACP63_SDW_PDM_DEV_CONFIG corresponds to platform device configuration for ACP PDM + SoundWire
 * manager instance combination.
 */
#define ACP63_SDW_PDM_DEV_CONFIG	GENMASK(1, 0)
#define ACP_SDW0_STAT			BIT(21)
#define ACP_SDW1_STAT			BIT(2)
#define ACP_ERROR_IRQ			BIT(29)

#define ACP_AUDIO0_TX_THRESHOLD		0x1c
#define ACP_AUDIO1_TX_THRESHOLD		0x1a
#define ACP_AUDIO2_TX_THRESHOLD		0x18
#define ACP_AUDIO0_RX_THRESHOLD		0x1b
#define ACP_AUDIO1_RX_THRESHOLD		0x19
#define ACP_AUDIO2_RX_THRESHOLD		0x17
#define ACP_P1_AUDIO1_TX_THRESHOLD	BIT(6)
#define ACP_P1_AUDIO1_RX_THRESHOLD	BIT(5)
#define ACP_SDW_DMA_IRQ_MASK		0x1F800000
#define ACP_P1_SDW_DMA_IRQ_MASK		0x60
#define ACP63_SDW0_DMA_MAX_STREAMS	6
#define ACP63_SDW1_DMA_MAX_STREAMS	2
#define ACP_P1_AUDIO_TX_THRESHOLD	6

/*
 * Below entries describes SDW0 instance DMA stream id and DMA irq bit mapping
 * in ACP_EXTENAL_INTR_CNTL register.
 * Stream id		IRQ Bit
 * 0 (SDW0_AUDIO0_TX)	28
 * 1 (SDW0_AUDIO1_TX)	26
 * 2 (SDW0_AUDIO2_TX)	24
 * 3 (SDW0_AUDIO0_RX)	27
 * 4 (SDW0_AUDIO1_RX)	25
 * 5 (SDW0_AUDIO2_RX)	23
 */
#define SDW0_DMA_TX_IRQ_MASK(i)	(ACP_AUDIO0_TX_THRESHOLD - (2 * (i)))
#define SDW0_DMA_RX_IRQ_MASK(i)	(ACP_AUDIO0_RX_THRESHOLD - (2 * ((i) - 3)))

/*
 * Below entries describes SDW1 instance DMA stream id and DMA irq bit mapping
 * in ACP_EXTENAL_INTR_CNTL1 register.
 * Stream id		IRQ Bit
 * 0 (SDW1_AUDIO1_TX)	6
 * 1 (SDW1_AUDIO1_RX)	5
 */
#define SDW1_DMA_IRQ_MASK(i)	(ACP_P1_AUDIO_TX_THRESHOLD - (i))

#define ACP_DELAY_US		5
#define ACP_SDW_RING_BUFF_ADDR_OFFSET (128 * 1024)
#define SDW0_MEM_WINDOW_START	0x4800000
#define ACP_SDW_SRAM_PTE_OFFSET	0x03800400
#define SDW0_PTE_OFFSET		0x400
#define SDW_FIFO_SIZE		0x100
#define SDW_DMA_SIZE		0x40
#define ACP_SDW0_FIFO_OFFSET	0x100
#define ACP_SDW_PTE_OFFSET	0x100
#define SDW_FIFO_OFFSET		0x100
#define SDW_PTE_OFFSET(i)	(SDW0_PTE_OFFSET + ((i) * 0x600))
#define ACP_SDW_FIFO_OFFSET(i)	(ACP_SDW0_FIFO_OFFSET + ((i) * 0x500))
#define SDW_MEM_WINDOW_START(i)	(SDW0_MEM_WINDOW_START + ((i) * 0xC0000))

#define SDW_PLAYBACK_MIN_NUM_PERIODS    2
#define SDW_PLAYBACK_MAX_NUM_PERIODS    8
#define SDW_PLAYBACK_MAX_PERIOD_SIZE    8192
#define SDW_PLAYBACK_MIN_PERIOD_SIZE    1024
#define SDW_CAPTURE_MIN_NUM_PERIODS     2
#define SDW_CAPTURE_MAX_NUM_PERIODS     8
#define SDW_CAPTURE_MAX_PERIOD_SIZE     8192
#define SDW_CAPTURE_MIN_PERIOD_SIZE     1024

#define SDW_MAX_BUFFER (SDW_PLAYBACK_MAX_PERIOD_SIZE * SDW_PLAYBACK_MAX_NUM_PERIODS)
#define SDW_MIN_BUFFER SDW_MAX_BUFFER

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

enum amd_sdw0_channel {
	ACP_SDW0_AUDIO0_TX = 0,
	ACP_SDW0_AUDIO1_TX,
	ACP_SDW0_AUDIO2_TX,
	ACP_SDW0_AUDIO0_RX,
	ACP_SDW0_AUDIO1_RX,
	ACP_SDW0_AUDIO2_RX,
};

enum amd_sdw1_channel {
	ACP_SDW1_AUDIO1_TX,
	ACP_SDW1_AUDIO1_RX,
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

struct sdw_dma_dev_data {
	void __iomem *acp_base;
	struct mutex *acp_lock; /* used to protect acp common register access */
	struct snd_pcm_substream *sdw0_dma_stream[ACP63_SDW0_DMA_MAX_STREAMS];
	struct snd_pcm_substream *sdw1_dma_stream[ACP63_SDW1_DMA_MAX_STREAMS];
};

struct acp_sdw_dma_stream {
	u16 num_pages;
	u16 channels;
	u32 stream_id;
	u32 instance;
	dma_addr_t dma_addr;
	u64 bytescount;
};

union acp_sdw_dma_count {
	struct {
		u32 low;
		u32 high;
	} bcount;
	u64 bytescount;
};

struct sdw_dma_ring_buf_reg {
	u32 reg_dma_size;
	u32 reg_fifo_addr;
	u32 reg_fifo_size;
	u32 reg_ring_buf_size;
	u32 reg_ring_buf_addr;
	u32 water_mark_size_reg;
	u32 pos_low_reg;
	u32 pos_high_reg;
};

/**
 * struct acp63_dev_data - acp pci driver context
 * @acp63_base: acp mmio base
 * @res: resource
 * @pdev: array of child platform device node structures
 * @acp_lock: used to protect acp common registers
 * @sdw_fw_node: SoundWire controller fw node handle
 * @pdev_config: platform device configuration
 * @pdev_count: platform devices count
 * @pdm_dev_index: pdm platform device index
 * @sdw_manager_count: SoundWire manager instance count
 * @sdw0_dev_index: SoundWire Manager-0 platform device index
 * @sdw1_dev_index: SoundWire Manager-1 platform device index
 * @sdw_dma_dev_index: SoundWire DMA controller platform device index
 * @sdw0-dma_intr_stat: DMA interrupt status array for SoundWire manager-SW0 instance
 * @sdw_dma_intr_stat: DMA interrupt status array for SoundWire manager-SW1 instance
 * @acp_reset: flag set to true when bus reset is applied across all
 * the active SoundWire manager instances
 */

struct acp63_dev_data {
	void __iomem *acp63_base;
	struct resource *res;
	struct platform_device *pdev[ACP63_DEVS];
	struct mutex acp_lock; /* protect shared registers */
	struct fwnode_handle *sdw_fw_node;
	u16 pdev_config;
	u16 pdev_count;
	u16 pdm_dev_index;
	u8 sdw_manager_count;
	u16 sdw0_dev_index;
	u16 sdw1_dev_index;
	u16 sdw_dma_dev_index;
	u16 sdw0_dma_intr_stat[ACP63_SDW0_DMA_MAX_STREAMS];
	u16 sdw1_dma_intr_stat[ACP63_SDW1_DMA_MAX_STREAMS];
	bool acp_reset;
};

int snd_amd_acp_find_config(struct pci_dev *pci);
