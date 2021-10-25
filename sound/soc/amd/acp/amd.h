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
#include <sound/soc-acpi.h>
#include "chip_offset_byte.h"

#define I2S_SP_INSTANCE			0x00
#define I2S_BT_INSTANCE			0x01

#define MEM_WINDOW_START		0x4000000

#define ACP_I2S_REG_START		0x1242400
#define ACP_I2S_REG_END			0x1242810
#define ACP3x_I2STDM_REG_START		0x1242400
#define ACP3x_I2STDM_REG_END		0x1242410
#define ACP3x_BT_TDM_REG_START		0x1242800
#define ACP3x_BT_TDM_REG_END		0x1242810
#define I2S_MODE			0x04
#define I2S_RX_THRESHOLD		27
#define I2S_TX_THRESHOLD		28
#define BT_TX_THRESHOLD			26
#define BT_RX_THRESHOLD			25

#define ACP_SRAM_PTE_OFFSET		0x02052800

#define ACP_SRAM_SP_PB_PTE_OFFSET	0x0
#define ACP_SRAM_SP_CP_PTE_OFFSET	0x100
#define ACP_SRAM_BT_PB_PTE_OFFSET	0x200
#define ACP_SRAM_BT_CP_PTE_OFFSET	0x300
#define PAGE_SIZE_4K_ENABLE		0x2

#define I2S_SP_TX_MEM_WINDOW_START	0x4000000
#define I2S_SP_RX_MEM_WINDOW_START	0x4020000
#define I2S_BT_TX_MEM_WINDOW_START	0x4040000
#define I2S_BT_RX_MEM_WINDOW_START	0x4060000

#define SP_PB_FIFO_ADDR_OFFSET		0x500
#define SP_CAPT_FIFO_ADDR_OFFSET	0x700
#define BT_PB_FIFO_ADDR_OFFSET		0x900
#define BT_CAPT_FIFO_ADDR_OFFSET	0xB00
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

#define ACP_MAX_STREAM			6

struct acp_stream {
	struct snd_pcm_substream *substream;
	int irq_bit;
	int dai_id;
	int id;
	u64 bytescount;
	u32 reg_offset;
	u32 pte_offset;
	u32 fifo_offset;
};

struct acp_dev_data {
	char *name;
	struct device *dev;
	void __iomem *acp_base;
	unsigned int i2s_irq;

	/* SOC specific dais */
	struct snd_soc_dai_driver *dai_driver;
	int num_dai;

	struct acp_stream *stream[ACP_MAX_STREAM];

	struct snd_soc_acpi_mach *machines;
	struct platform_device *mach_dev;
};

extern const struct snd_soc_dai_ops asoc_acp_cpu_dai_ops;

int asoc_acp_i2s_probe(struct snd_soc_dai *dai);
int acp_platform_register(struct device *dev);
int acp_platform_unregister(struct device *dev);

int acp_machine_select(struct acp_dev_data *adata);

static inline u64 acp_get_byte_count(struct acp_dev_data *adata, int dai_id, int direction)
{
	u64 byte_count, low = 0, high = 0;

	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (dai_id) {
		case I2S_BT_INSTANCE:
			high = readl(adata->acp_base + ACP_BT_TX_LINEARPOSITIONCNTR_HIGH);
			low = readl(adata->acp_base + ACP_BT_TX_LINEARPOSITIONCNTR_LOW);
			break;
		case I2S_SP_INSTANCE:
			high = readl(adata->acp_base + ACP_I2S_TX_LINEARPOSITIONCNTR_HIGH);
			low = readl(adata->acp_base + ACP_I2S_TX_LINEARPOSITIONCNTR_LOW);
			break;
		default:
			dev_err(adata->dev, "Invalid dai id %x\n", dai_id);
			return -EINVAL;
		}
	} else {
		switch (dai_id) {
		case I2S_BT_INSTANCE:
			high = readl(adata->acp_base + ACP_BT_RX_LINEARPOSITIONCNTR_HIGH);
			low = readl(adata->acp_base + ACP_BT_RX_LINEARPOSITIONCNTR_LOW);
			break;
		case I2S_SP_INSTANCE:
			high = readl(adata->acp_base + ACP_I2S_RX_LINEARPOSITIONCNTR_HIGH);
			low = readl(adata->acp_base + ACP_I2S_RX_LINEARPOSITIONCNTR_LOW);
			break;
		default:
			dev_err(adata->dev, "Invalid dai id %x\n", dai_id);
			return -EINVAL;
		}
	}
	/* Get 64 bit value from two 32 bit registers */
	byte_count = (high << 32) | low;

	return byte_count;
}

#endif
