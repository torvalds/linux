/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD Common ACP header file for ACP6.3, ACP7.0 & ACP7.1 platforms
 *
 * Copyright (C) 2022, 2023, 2025 Advanced Micro Devices, Inc. All rights reserved.
 */

#include <linux/soundwire/sdw_amd.h>
#include <sound/acp63_chip_offset_byte.h>

#define ACP_DEVICE_ID 0x15E2
#define ACP63_REG_START		0x1240000
#define ACP63_REG_END		0x125C000
#define ACP63_PCI_REV		0x63
#define ACP70_PCI_REV		0x70
#define ACP71_PCI_REV		0x71

#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK	0x00010001
#define ACP63_PGFSM_CNTL_POWER_ON_MASK	1
#define ACP63_PGFSM_CNTL_POWER_OFF_MASK	0
#define ACP63_PGFSM_STATUS_MASK		3
#define ACP63_POWERED_ON			0
#define ACP63_POWER_ON_IN_PROGRESS	1
#define ACP63_POWERED_OFF		2
#define ACP63_POWER_OFF_IN_PROGRESS	3

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

#define ACP63_DMIC_ADDR			2
#define ACP63_SDW_ADDR			5
#define AMD_SDW_MAX_MANAGERS		2

/* time in ms for acp timeout */
#define ACP63_TIMEOUT		500

#define ACP_SDW0_STAT			BIT(21)
#define ACP_SDW1_STAT			BIT(2)
#define ACP_ERROR_IRQ			BIT(29)

#define ACP_AUDIO0_TX_THRESHOLD		0x1c
#define ACP_AUDIO1_TX_THRESHOLD		0x1a
#define ACP_AUDIO2_TX_THRESHOLD		0x18
#define ACP_AUDIO0_RX_THRESHOLD		0x1b
#define ACP_AUDIO1_RX_THRESHOLD		0x19
#define ACP_AUDIO2_RX_THRESHOLD		0x17
#define ACP63_P1_AUDIO1_TX_THRESHOLD	BIT(6)
#define ACP63_P1_AUDIO1_RX_THRESHOLD	BIT(5)
#define ACP63_SDW_DMA_IRQ_MASK		0x1F800000
#define ACP63_P1_SDW_DMA_IRQ_MASK	0x60
#define ACP63_SDW0_DMA_MAX_STREAMS	6
#define ACP63_SDW1_DMA_MAX_STREAMS	2
#define ACP63_P1_AUDIO_TX_THRESHOLD	6

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
#define ACP63_SDW0_DMA_TX_IRQ_MASK(i)	(ACP_AUDIO0_TX_THRESHOLD - (2 * (i)))
#define ACP63_SDW0_DMA_RX_IRQ_MASK(i)	(ACP_AUDIO0_RX_THRESHOLD - (2 * ((i) - 3)))

/*
 * Below entries describes SDW1 instance DMA stream id and DMA irq bit mapping
 * in ACP_EXTENAL_INTR_CNTL1 register.
 * Stream id		IRQ Bit
 * 0 (SDW1_AUDIO1_TX)	6
 * 1 (SDW1_AUDIO1_RX)	5
 */
#define ACP63_SDW1_DMA_IRQ_MASK(i)	(ACP63_P1_AUDIO_TX_THRESHOLD - (i))

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

#define ACP_HW_OPS(acp_data, cb)	((acp_data)->hw_ops->cb)

#define ACP70_PGFSM_CNTL_POWER_ON_MASK		0x1F
#define ACP70_PGFSM_CNTL_POWER_OFF_MASK		0
#define ACP70_PGFSM_STATUS_MASK			0xFF
#define ACP70_TIMEOUT				2000
#define ACP70_SDW_HOST_WAKE_MASK	0x0C00000
#define ACP70_SDW0_HOST_WAKE_STAT	BIT(24)
#define ACP70_SDW1_HOST_WAKE_STAT	BIT(25)
#define ACP70_SDW0_PME_STAT		BIT(26)
#define ACP70_SDW1_PME_STAT		BIT(27)

#define ACP70_SDW0_DMA_MAX_STREAMS	6
#define ACP70_SDW1_DMA_MAX_STREAMS	ACP70_SDW0_DMA_MAX_STREAMS
#define ACP70_SDW_DMA_IRQ_MASK		0x1F800000
#define ACP70_P1_SDW_DMA_IRQ_MASK	0x1F8

#define ACP70_P1_AUDIO0_TX_THRESHOLD      0x8
#define ACP70_P1_AUDIO1_TX_THRESHOLD      0x6
#define ACP70_P1_AUDIO2_TX_THRESHOLD      0x4
#define ACP70_P1_AUDIO0_RX_THRESHOLD      0x7
#define ACP70_P1_AUDIO1_RX_THRESHOLD      0x5
#define ACP70_P1_AUDIO2_RX_THRESHOLD      0x3

#define ACP70_SDW0_DMA_TX_IRQ_MASK(i)	(ACP_AUDIO0_TX_THRESHOLD - (2 * (i)))
#define ACP70_SDW0_DMA_RX_IRQ_MASK(i)	(ACP_AUDIO0_RX_THRESHOLD - (2 * ((i) - 3)))

/*
 * Below entries describes SDW1 instance DMA stream id and DMA irq bit mapping
 * in ACP_EXTENAL_INTR_CNTL1 register for ACP70/ACP71 platforms
 * Stream id		IRQ Bit
 * 0 (SDW1_AUDIO0_TX)	8
 * 1 (SDW1_AUDIO1_TX)	6
 * 2 (SDW1_AUDIO2_TX)	4
 * 3 (SDW1_AUDIO0_RX)	7
 * 4 (SDW1_AUDIO1_RX)	5
 * 5 (SDW1_AUDIO2_RX)	3
 */
#define ACP70_SDW1_DMA_TX_IRQ_MASK(i)	(ACP70_P1_AUDIO0_TX_THRESHOLD - (2 * (i)))
#define ACP70_SDW1_DMA_RX_IRQ_MASK(i)	(ACP70_P1_AUDIO0_RX_THRESHOLD - (2 * ((i) - 3)))

#define ACP70_SW0_AUDIO0_TX_EN		ACP_SW0_AUDIO0_TX_EN
#define ACP70_SW0_AUDIO1_TX_EN		ACP_SW0_AUDIO1_TX_EN
#define ACP70_SW0_AUDIO2_TX_EN		ACP_SW0_AUDIO2_TX_EN
#define ACP70_SW0_AUDIO0_RX_EN		ACP_SW0_AUDIO0_RX_EN
#define ACP70_SW0_AUDIO1_RX_EN		ACP_SW0_AUDIO1_RX_EN
#define ACP70_SW0_AUDIO2_RX_EN		ACP_SW0_AUDIO2_RX_EN

#define ACP70_SW1_AUDIO0_TX_EN		0x0003C10
#define ACP70_SW1_AUDIO1_TX_EN		0x0003C50
#define ACP70_SW1_AUDIO2_TX_EN		0x0003C6C
#define ACP70_SW1_AUDIO0_RX_EN		0x0003C88
#define ACP70_SW1_AUDIO1_RX_EN		0x0003D28
#define ACP70_SW1_AUDIO2_RX_EN		0x0003D44

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
	ACP_CONFIG_16,
	ACP_CONFIG_17,
	ACP_CONFIG_18,
	ACP_CONFIG_19,
	ACP_CONFIG_20,
};

enum amd_acp63_sdw0_channel {
	ACP63_SDW0_AUDIO0_TX = 0,
	ACP63_SDW0_AUDIO1_TX,
	ACP63_SDW0_AUDIO2_TX,
	ACP63_SDW0_AUDIO0_RX,
	ACP63_SDW0_AUDIO1_RX,
	ACP63_SDW0_AUDIO2_RX,
};

enum amd_acp63_sdw1_channel {
	ACP63_SDW1_AUDIO1_TX,
	ACP63_SDW1_AUDIO1_RX,
};

enum amd_acp70_sdw_channel {
	ACP70_SDW_AUDIO0_TX = 0,
	ACP70_SDW_AUDIO1_TX,
	ACP70_SDW_AUDIO2_TX,
	ACP70_SDW_AUDIO0_RX,
	ACP70_SDW_AUDIO1_RX,
	ACP70_SDW_AUDIO2_RX,
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
	u32 acp_rev;
	struct snd_pcm_substream *acp63_sdw0_dma_stream[ACP63_SDW0_DMA_MAX_STREAMS];
	struct snd_pcm_substream *acp63_sdw1_dma_stream[ACP63_SDW1_DMA_MAX_STREAMS];
	struct snd_pcm_substream *acp70_sdw0_dma_stream[ACP70_SDW0_DMA_MAX_STREAMS];
	struct snd_pcm_substream *acp70_sdw1_dma_stream[ACP70_SDW1_DMA_MAX_STREAMS];
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

struct acp63_dev_data;

/**
 * struct acp_hw_ops - ACP PCI driver platform specific ops
 * @acp_init: ACP initialization
 * @acp_deinit: ACP de-initialization
 * @acp_get_config: function to read the acp pin configuration
 * @acp_sdw_dma_irq_thread: ACP SoundWire DMA interrupt thread
 * acp_suspend: ACP system level suspend callback
 * acp_resume: ACP system level resume callback
 * acp_suspend_runtime: ACP runtime suspend callback
 * acp_resume_runtime: ACP runtime resume callback
 */
struct acp_hw_ops {
	int (*acp_init)(void __iomem *acp_base, struct device *dev);
	int (*acp_deinit)(void __iomem *acp_base, struct device *dev);
	void (*acp_get_config)(struct pci_dev *pci, struct acp63_dev_data *acp_data);
	void (*acp_sdw_dma_irq_thread)(struct acp63_dev_data *acp_data);
	int (*acp_suspend)(struct device *dev);
	int (*acp_resume)(struct device *dev);
	int (*acp_suspend_runtime)(struct device *dev);
	int (*acp_resume_runtime)(struct device *dev);
};

/**
 * struct acp63_dev_data - acp pci driver context
 * @acp63_base: acp mmio base
 * @res: resource
 * @hw_ops: ACP pci driver platform-specific ops
 * @pdm_dev: ACP PDM controller platform device
 * @dmic_codec: platform device for DMIC Codec
 * sdw_dma_dev: platform device for SoundWire DMA controller
 * @mach_dev: platform device for machine driver to support ACP PDM/SoundWire configuration
 * @acp_lock: used to protect acp common registers
 * @info: SoundWire AMD information found in ACPI tables
 * @sdw: SoundWire context for all SoundWire manager instances
 * @machine: ACPI machines for SoundWire interface
 * @is_sdw_dev: flag set to true when any SoundWire manager instances are available
 * @is_pdm_dev: flag set to true when ACP PDM controller exists
 * @is_pdm_config: flat set to true when PDM configuration is selected from BIOS
 * @is_sdw_config: flag set to true when SDW configuration is selected from BIOS
 * @sdw_en_stat: flag set to true when any one of the SoundWire manager instance is enabled
 * @acp70_sdw0_wake_event: flag set to true when wake irq asserted for SW0 instance
 * @acp70_sdw1_wake_event: flag set to true when wake irq asserted for SW1 instance
 * @addr: pci ioremap address
 * @reg_range: ACP reigister range
 * @acp_rev: ACP PCI revision id
 * @acp_sw_pad_keeper_en: store acp SoundWire pad keeper enable register value
 * @acp_pad_pulldown_ctrl: store acp pad pulldown control register value
 * @acp63_sdw0-dma_intr_stat: DMA interrupt status array for ACP6.3 platform SoundWire
 * manager-SW0 instance
 * @acp63_sdw_dma_intr_stat: DMA interrupt status array for ACP6.3 platform SoundWire
 * manager-SW1 instance
 * @acp70_sdw0-dma_intr_stat: DMA interrupt status array for ACP7.0 platform SoundWire
 * manager-SW0 instance
 * @acp70_sdw_dma_intr_stat: DMA interrupt status array for ACP7.0 platform SoundWire
 * manager-SW1 instance
 */

struct acp63_dev_data {
	void __iomem *acp63_base;
	struct resource *res;
	struct acp_hw_ops *hw_ops;
	struct platform_device *pdm_dev;
	struct platform_device *dmic_codec_dev;
	struct platform_device *sdw_dma_dev;
	struct platform_device *mach_dev;
	struct mutex acp_lock; /* protect shared registers */
	struct sdw_amd_acpi_info info;
	/* sdw context allocated by SoundWire driver */
	struct sdw_amd_ctx *sdw;
	struct snd_soc_acpi_mach *machines;
	bool is_sdw_dev;
	bool is_pdm_dev;
	bool is_pdm_config;
	bool is_sdw_config;
	bool sdw_en_stat;
	bool acp70_sdw0_wake_event;
	bool acp70_sdw1_wake_event;
	u32 addr;
	u32 reg_range;
	u32 acp_rev;
	u32 acp_sw_pad_keeper_en;
	u32 acp_pad_pulldown_ctrl;
	u16 acp63_sdw0_dma_intr_stat[ACP63_SDW0_DMA_MAX_STREAMS];
	u16 acp63_sdw1_dma_intr_stat[ACP63_SDW1_DMA_MAX_STREAMS];
	u16 acp70_sdw0_dma_intr_stat[ACP70_SDW0_DMA_MAX_STREAMS];
	u16 acp70_sdw1_dma_intr_stat[ACP70_SDW1_DMA_MAX_STREAMS];
};

void acp63_hw_init_ops(struct acp_hw_ops *hw_ops);
void acp70_hw_init_ops(struct acp_hw_ops *hw_ops);

static inline int acp_hw_init(struct acp63_dev_data *adata, struct device *dev)
{
	if (adata && adata->hw_ops && adata->hw_ops->acp_init)
		return ACP_HW_OPS(adata, acp_init)(adata->acp63_base, dev);
	return -EOPNOTSUPP;
}

static inline int acp_hw_deinit(struct acp63_dev_data *adata, struct device *dev)
{
	if (adata && adata->hw_ops && adata->hw_ops->acp_deinit)
		return ACP_HW_OPS(adata, acp_deinit)(adata->acp63_base, dev);
	return -EOPNOTSUPP;
}

static inline void acp_hw_get_config(struct pci_dev *pci, struct acp63_dev_data *adata)
{
	if (adata && adata->hw_ops && adata->hw_ops->acp_get_config)
		ACP_HW_OPS(adata, acp_get_config)(pci, adata);
}

static inline void acp_hw_sdw_dma_irq_thread(struct acp63_dev_data *adata)
{
	if (adata && adata->hw_ops && adata->hw_ops->acp_sdw_dma_irq_thread)
		ACP_HW_OPS(adata, acp_sdw_dma_irq_thread)(adata);
}

static inline int acp_hw_suspend(struct device *dev)
{
	struct acp63_dev_data *adata = dev_get_drvdata(dev);

	if (adata && adata->hw_ops && adata->hw_ops->acp_suspend)
		return ACP_HW_OPS(adata, acp_suspend)(dev);
	return -EOPNOTSUPP;
}

static inline int acp_hw_resume(struct device *dev)
{
	struct acp63_dev_data *adata = dev_get_drvdata(dev);

	if (adata && adata->hw_ops && adata->hw_ops->acp_resume)
		return ACP_HW_OPS(adata, acp_resume)(dev);
	return -EOPNOTSUPP;
}

static inline int acp_hw_suspend_runtime(struct device *dev)
{
	struct acp63_dev_data *adata = dev_get_drvdata(dev);

	if (adata && adata->hw_ops && adata->hw_ops->acp_suspend_runtime)
		return ACP_HW_OPS(adata, acp_suspend_runtime)(dev);
	return -EOPNOTSUPP;
}

static inline int acp_hw_runtime_resume(struct device *dev)
{
	struct acp63_dev_data *adata = dev_get_drvdata(dev);

	if (adata && adata->hw_ops && adata->hw_ops->acp_resume_runtime)
		return ACP_HW_OPS(adata, acp_resume_runtime)(dev);
	return -EOPNOTSUPP;
}

int snd_amd_acp_find_config(struct pci_dev *pci);
