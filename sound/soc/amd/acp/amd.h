/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license. When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
 */

#ifndef __AMD_ACP_H
#define __AMD_ACP_H

#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dai.h>

#include "chip_offset_byte.h"

#define ACP3X_DEV			3
#define ACP6X_DEV			6
#define ACP63_DEV			0x63
#define ACP70_DEV			0x70
#define ACP71_DEV			0x71

#define DMIC_INSTANCE			0x00
#define I2S_SP_INSTANCE			0x01
#define I2S_BT_INSTANCE			0x02
#define I2S_HS_INSTANCE			0x03

#define MEM_WINDOW_START		0x4080000

#define ACP_I2S_REG_START		0x1242400
#define ACP_I2S_REG_END			0x1242810
#define ACP3x_I2STDM_REG_START		0x1242400
#define ACP3x_I2STDM_REG_END		0x1242410
#define ACP3x_BT_TDM_REG_START		0x1242800
#define ACP3x_BT_TDM_REG_END		0x1242810

#define THRESHOLD(bit, base)	((bit) + (base))
#define I2S_RX_THRESHOLD(base)	THRESHOLD(7, base)
#define I2S_TX_THRESHOLD(base)	THRESHOLD(8, base)
#define BT_TX_THRESHOLD(base)	THRESHOLD(6, base)
#define BT_RX_THRESHOLD(base)	THRESHOLD(5, base)
#define HS_TX_THRESHOLD(base)	THRESHOLD(4, base)
#define HS_RX_THRESHOLD(base)	THRESHOLD(3, base)

#define ACP_SRAM_SP_PB_PTE_OFFSET	0x0
#define ACP_SRAM_SP_CP_PTE_OFFSET	0x100
#define ACP_SRAM_BT_PB_PTE_OFFSET	0x200
#define ACP_SRAM_BT_CP_PTE_OFFSET	0x300
#define ACP_SRAM_PDM_PTE_OFFSET		0x400
#define ACP_SRAM_HS_PB_PTE_OFFSET       0x500
#define ACP_SRAM_HS_CP_PTE_OFFSET       0x600
#define PAGE_SIZE_4K_ENABLE		0x2

#define I2S_SP_TX_MEM_WINDOW_START	0x4000000
#define I2S_SP_RX_MEM_WINDOW_START	0x4020000
#define I2S_BT_TX_MEM_WINDOW_START	0x4040000
#define I2S_BT_RX_MEM_WINDOW_START	0x4060000
#define I2S_HS_TX_MEM_WINDOW_START      0x40A0000
#define I2S_HS_RX_MEM_WINDOW_START      0x40C0000

#define ACP7x_I2S_SP_TX_MEM_WINDOW_START	0x4000000
#define ACP7x_I2S_SP_RX_MEM_WINDOW_START	0x4200000
#define ACP7x_I2S_BT_TX_MEM_WINDOW_START	0x4400000
#define ACP7x_I2S_BT_RX_MEM_WINDOW_START	0x4600000
#define ACP7x_I2S_HS_TX_MEM_WINDOW_START	0x4800000
#define ACP7x_I2S_HS_RX_MEM_WINDOW_START	0x4A00000
#define ACP7x_DMIC_MEM_WINDOW_START			0x4C00000

#define SP_PB_FIFO_ADDR_OFFSET		0x500
#define SP_CAPT_FIFO_ADDR_OFFSET	0x700
#define BT_PB_FIFO_ADDR_OFFSET		0x900
#define BT_CAPT_FIFO_ADDR_OFFSET	0xB00
#define HS_PB_FIFO_ADDR_OFFSET		0xD00
#define HS_CAPT_FIFO_ADDR_OFFSET	0xF00
#define PLAYBACK_MIN_NUM_PERIODS	2
#define PLAYBACK_MAX_NUM_PERIODS	8
#define PLAYBACK_MAX_PERIOD_SIZE	8192
#define PLAYBACK_MIN_PERIOD_SIZE	1024
#define CAPTURE_MIN_NUM_PERIODS		2
#define CAPTURE_MAX_NUM_PERIODS		8
#define CAPTURE_MAX_PERIOD_SIZE		8192
#define CAPTURE_MIN_PERIOD_SIZE		1024

#define MAX_BUFFER			65536
#define MIN_BUFFER			MAX_BUFFER
#define FIFO_SIZE			0x100
#define DMA_SIZE			0x40
#define FRM_LEN				0x100

#define ACP3x_ITER_IRER_SAMP_LEN_MASK	0x38

#define ACP_MAX_STREAM			8

#define TDM_ENABLE	1
#define TDM_DISABLE	0

#define SLOT_WIDTH_8	0x8
#define SLOT_WIDTH_16	0x10
#define SLOT_WIDTH_24	0x18
#define SLOT_WIDTH_32	0x20

#define ACP6X_PGFSM_CONTROL                     0x1024
#define ACP6X_PGFSM_STATUS                      0x1028

#define ACP63_PGFSM_CONTROL			ACP6X_PGFSM_CONTROL
#define ACP63_PGFSM_STATUS			ACP6X_PGFSM_STATUS

#define ACP70_PGFSM_CONTROL			ACP6X_PGFSM_CONTROL
#define ACP70_PGFSM_STATUS			ACP6X_PGFSM_STATUS

#define ACP_ZSC_DSP_CTRL			0x0001014
#define ACP_ZSC_STS				0x0001018
#define ACP_SOFT_RST_DONE_MASK	0x00010001

#define ACP_PGFSM_CNTL_POWER_ON_MASK            0xffffffff
#define ACP_PGFSM_CNTL_POWER_OFF_MASK           0x00
#define ACP_PGFSM_STATUS_MASK                   0x03
#define ACP_POWERED_ON                          0x00
#define ACP_POWER_ON_IN_PROGRESS                0x01
#define ACP_POWERED_OFF                         0x02
#define ACP_POWER_OFF_IN_PROGRESS               0x03

#define ACP_ERROR_MASK                          0x20000000
#define ACP_EXT_INTR_STAT_CLEAR_MASK            0xffffffff

#define ACP_TIMEOUT		500
#define DELAY_US		5
#define ACP_SUSPEND_DELAY_MS   2000

#define PDM_DMA_STAT            0x10
#define PDM_DMA_INTR_MASK       0x10000
#define PDM_DEC_64              0x2
#define PDM_CLK_FREQ_MASK       0x07
#define PDM_MISC_CTRL_MASK      0x10
#define PDM_ENABLE              0x01
#define PDM_DISABLE             0x00
#define DMA_EN_MASK             0x02
#define DELAY_US                5
#define PDM_TIMEOUT             1000
#define ACP_REGION2_OFFSET      0x02000000

struct acp_chip_info {
	char *name;		/* Platform name */
	unsigned int acp_rev;	/* ACP Revision id */
	void __iomem *base;	/* ACP memory PCI base */
	struct platform_device *chip_pdev;
	unsigned int flag;	/* Distinguish b/w Legacy or Only PDM */
	bool is_pdm_dev;	/* flag set to true when ACP PDM controller exists */
	bool is_pdm_config;	/* flag set to true when PDM configuration is selected from BIOS */
	bool is_i2s_config;	/* flag set to true when I2S configuration is selected from BIOS */
};

struct acp_stream {
	struct list_head list;
	struct snd_pcm_substream *substream;
	int irq_bit;
	int dai_id;
	int id;
	int dir;
	u64 bytescount;
	u32 reg_offset;
	u32 pte_offset;
	u32 fifo_offset;
};

struct acp_resource {
	int offset;
	int no_of_ctrls;
	int irqp_used;
	bool soc_mclk;
	u32 irq_reg_offset;
	u64 scratch_reg_offset;
	u64 sram_pte_offset;
};

struct acp_dev_data {
	char *name;
	struct device *dev;
	void __iomem *acp_base;
	unsigned int i2s_irq;

	bool tdm_mode;
	bool is_i2s_config;
	/* SOC specific dais */
	struct snd_soc_dai_driver *dai_driver;
	int num_dai;

	struct list_head stream_list;
	spinlock_t acp_lock;

	struct snd_soc_acpi_mach *machines;
	struct platform_device *mach_dev;

	u32 bclk_div;
	u32 lrclk_div;

	struct acp_resource *rsrc;
	u32 ch_mask;
	u32 tdm_tx_fmt[3];
	u32 tdm_rx_fmt[3];
	u32 xfer_tx_resolution[3];
	u32 xfer_rx_resolution[3];
	unsigned int flag;
	unsigned int platform;
};

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

extern const struct snd_soc_dai_ops asoc_acp_cpu_dai_ops;
extern const struct snd_soc_dai_ops acp_dmic_dai_ops;

int acp_platform_register(struct device *dev);
int acp_platform_unregister(struct device *dev);

int acp_machine_select(struct acp_dev_data *adata);

int smn_read(struct pci_dev *dev, u32 smn_addr);
int smn_write(struct pci_dev *dev, u32 smn_addr, u32 data);

int acp_init(struct acp_chip_info *chip);
int acp_deinit(struct acp_chip_info *chip);
void acp_enable_interrupts(struct acp_dev_data *adata);
void acp_disable_interrupts(struct acp_dev_data *adata);
/* Machine configuration */
int snd_amd_acp_find_config(struct pci_dev *pci);

void config_pte_for_stream(struct acp_dev_data *adata, struct acp_stream *stream);
void config_acp_dma(struct acp_dev_data *adata, struct acp_stream *stream, int size);
void restore_acp_pdm_params(struct snd_pcm_substream *substream,
			    struct acp_dev_data *adata);

int restore_acp_i2s_params(struct snd_pcm_substream *substream,
			   struct acp_dev_data *adata, struct acp_stream *stream);

void check_acp_config(struct pci_dev *pci, struct acp_chip_info *chip);

static inline u64 acp_get_byte_count(struct acp_dev_data *adata, int dai_id, int direction)
{
	u64 byte_count = 0, low = 0, high = 0;

	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (dai_id) {
		case I2S_BT_INSTANCE:
			high = readl(adata->acp_base + ACP_BT_TX_LINEARPOSITIONCNTR_HIGH(adata));
			low = readl(adata->acp_base + ACP_BT_TX_LINEARPOSITIONCNTR_LOW(adata));
			break;
		case I2S_SP_INSTANCE:
			high = readl(adata->acp_base + ACP_I2S_TX_LINEARPOSITIONCNTR_HIGH(adata));
			low = readl(adata->acp_base + ACP_I2S_TX_LINEARPOSITIONCNTR_LOW(adata));
			break;
		case I2S_HS_INSTANCE:
			high = readl(adata->acp_base + ACP_HS_TX_LINEARPOSITIONCNTR_HIGH);
			low = readl(adata->acp_base + ACP_HS_TX_LINEARPOSITIONCNTR_LOW);
			break;
		default:
			dev_err(adata->dev, "Invalid dai id %x\n", dai_id);
			goto POINTER_RETURN_BYTES;
		}
	} else {
		switch (dai_id) {
		case I2S_BT_INSTANCE:
			high = readl(adata->acp_base + ACP_BT_RX_LINEARPOSITIONCNTR_HIGH(adata));
			low = readl(adata->acp_base + ACP_BT_RX_LINEARPOSITIONCNTR_LOW(adata));
			break;
		case I2S_SP_INSTANCE:
			high = readl(adata->acp_base + ACP_I2S_RX_LINEARPOSITIONCNTR_HIGH(adata));
			low = readl(adata->acp_base + ACP_I2S_RX_LINEARPOSITIONCNTR_LOW(adata));
			break;
		case I2S_HS_INSTANCE:
			high = readl(adata->acp_base + ACP_HS_RX_LINEARPOSITIONCNTR_HIGH);
			low = readl(adata->acp_base + ACP_HS_RX_LINEARPOSITIONCNTR_LOW);
			break;
		case DMIC_INSTANCE:
			high = readl(adata->acp_base + ACP_WOV_RX_LINEARPOSITIONCNTR_HIGH);
			low = readl(adata->acp_base + ACP_WOV_RX_LINEARPOSITIONCNTR_LOW);
			break;
		default:
			dev_err(adata->dev, "Invalid dai id %x\n", dai_id);
			goto POINTER_RETURN_BYTES;
		}
	}
	/* Get 64 bit value from two 32 bit registers */
	byte_count = (high << 32) | low;

POINTER_RETURN_BYTES:
	return byte_count;
}
#endif
