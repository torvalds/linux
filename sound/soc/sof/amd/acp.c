// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Authors: Vijendar Mukunda <Vijendar.Mukunda@amd.com>
//	    Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>

/*
 * Hardware interface for generic AMD ACP processor
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "../ops.h"
#include "acp.h"
#include "acp-dsp-offset.h"

static void configure_acp_groupregisters(struct acp_dev_data *adata)
{
	struct snd_sof_dev *sdev = adata->dev;

	/* Group Enable */
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACPAXI2AXI_ATU_BASE_ADDR_GRP_1,
			  ACP_SRAM_PTE_OFFSET | BIT(31));
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACPAXI2AXI_ATU_PAGE_SIZE_GRP_1,
			  PAGE_SIZE_4K_ENABLE);

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACPAXI2AXI_ATU_CTRL, ACP_ATU_CACHE_INVALID);
}

static void init_dma_descriptor(struct acp_dev_data *adata)
{
	struct snd_sof_dev *sdev = adata->dev;
	unsigned int addr;

	addr = ACP_SRAM_PTE_OFFSET + offsetof(struct scratch_reg_conf, dma_desc);

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_DESC_BASE_ADDR, addr);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_DESC_MAX_NUM_DSCR, ACP_MAX_DESC_CNT);
}

static void configure_dma_descriptor(struct acp_dev_data *adata, unsigned short idx,
				     struct dma_descriptor *dscr_info)
{
	struct snd_sof_dev *sdev = adata->dev;
	unsigned int offset;

	offset = ACP_SCRATCH_REG_0 + offsetof(struct scratch_reg_conf, dma_desc) +
		 idx * sizeof(struct dma_descriptor);

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, offset, dscr_info->src_addr);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, offset + 0x4, dscr_info->dest_addr);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, offset + 0x8, dscr_info->tx_cnt.u32_all);
}

static int config_dma_channel(struct acp_dev_data *adata, unsigned int ch,
			      unsigned int idx, unsigned int dscr_count)
{
	struct snd_sof_dev *sdev = adata->dev;
	unsigned int val, status;
	int ret;

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_CNTL_0 + ch * sizeof(u32),
			  ACP_DMA_CH_RST | ACP_DMA_CH_GRACEFUL_RST_EN);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_DMA_CH_RST_STS, val,
					    val & (1 << ch), ACP_REG_POLL_INTERVAL,
					    ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0) {
		status = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_ERROR_STATUS);
		val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_DMA_ERR_STS_0 + ch * sizeof(u32));

		dev_err(sdev->dev, "ACP_DMA_ERR_STS :0x%x ACP_ERROR_STATUS :0x%x\n", val, status);
		return ret;
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, (ACP_DMA_CNTL_0 + ch * sizeof(u32)), 0);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_DSCR_CNT_0 + ch * sizeof(u32), dscr_count);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_DSCR_STRT_IDX_0 + ch * sizeof(u32), idx);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_PRIO_0 + ch * sizeof(u32), 0);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_CNTL_0 + ch * sizeof(u32), ACP_DMA_CH_RUN);

	return ret;
}

static int acpbus_dma_start(struct acp_dev_data *adata, unsigned int ch,
			    unsigned int dscr_count, struct dma_descriptor *dscr_info)
{
	struct snd_sof_dev *sdev = adata->dev;
	int ret;
	u16 dscr;

	if (!dscr_info || !dscr_count)
		return -EINVAL;

	for (dscr = 0; dscr < dscr_count; dscr++)
		configure_dma_descriptor(adata, dscr, dscr_info++);

	ret = config_dma_channel(adata, ch, 0, dscr_count);
	if (ret < 0)
		dev_err(sdev->dev, "config dma ch failed:%d\n", ret);

	return ret;
}

int configure_and_run_dma(struct acp_dev_data *adata, unsigned int src_addr,
			  unsigned int dest_addr, int dsp_data_size)
{
	struct snd_sof_dev *sdev = adata->dev;
	unsigned int desc_count, index;
	int ret;

	for (desc_count = 0; desc_count < ACP_MAX_DESC && dsp_data_size >= 0;
	     desc_count++, dsp_data_size -= ACP_PAGE_SIZE) {
		adata->dscr_info[desc_count].src_addr = src_addr + desc_count * ACP_PAGE_SIZE;
		adata->dscr_info[desc_count].dest_addr = dest_addr + desc_count * ACP_PAGE_SIZE;
		adata->dscr_info[desc_count].tx_cnt.bits.count = ACP_PAGE_SIZE;
		if (dsp_data_size < ACP_PAGE_SIZE)
			adata->dscr_info[desc_count].tx_cnt.bits.count = dsp_data_size;
	}

	ret = acpbus_dma_start(adata, 0, desc_count, adata->dscr_info);
	if (ret)
		dev_err(sdev->dev, "acpbus_dma_start failed\n");

	/* Clear descriptor array */
	for (index = 0; index < desc_count; index++)
		memset(&adata->dscr_info[index], 0x00, sizeof(struct dma_descriptor));

	return ret;
}

int configure_and_run_sha_dma(struct acp_dev_data *adata, void *image_addr,
			      unsigned int start_addr, unsigned int dest_addr,
			      unsigned int image_length)
{
	struct snd_sof_dev *sdev = adata->dev;
	unsigned int tx_count, fw_qualifier, val;
	int ret;

	if (!image_addr) {
		dev_err(sdev->dev, "SHA DMA image address is NULL\n");
		return -EINVAL;
	}

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_SHA_DMA_CMD);
	if (val & ACP_SHA_RUN) {
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_DMA_CMD, ACP_SHA_RESET);
		ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SHA_DMA_CMD_STS,
						    val, val & ACP_SHA_RESET,
						    ACP_REG_POLL_INTERVAL,
						    ACP_REG_POLL_TIMEOUT_US);
		if (ret < 0) {
			dev_err(sdev->dev, "SHA DMA Failed to Reset\n");
			return ret;
		}
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_DMA_STRT_ADDR, start_addr);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_DMA_DESTINATION_ADDR, dest_addr);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_MSG_LENGTH, image_length);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_DMA_CMD, ACP_SHA_RUN);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SHA_TRANSFER_BYTE_CNT,
					    tx_count, tx_count == image_length,
					    ACP_REG_POLL_INTERVAL, ACP_DMA_COMPLETE_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev, "SHA DMA Failed to Transfer Length %x\n", tx_count);
		return ret;
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_DSP_FW_QUALIFIER, DSP_FW_RUN_ENABLE);

	fw_qualifier = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_SHA_DSP_FW_QUALIFIER);
	if (!(fw_qualifier & DSP_FW_RUN_ENABLE)) {
		dev_err(sdev->dev, "PSP validation failed\n");
		return -EINVAL;
	}

	return ret;
}

int acp_dma_status(struct acp_dev_data *adata, unsigned char ch)
{
	struct snd_sof_dev *sdev = adata->dev;
	unsigned int val;
	int ret = 0;

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_DMA_CNTL_0 + ch * sizeof(u32));
	if (val & ACP_DMA_CH_RUN) {
		ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_DMA_CH_STS, val, !val,
						    ACP_REG_POLL_INTERVAL,
						    ACP_DMA_COMPLETE_TIMEOUT_US);
		if (ret < 0)
			dev_err(sdev->dev, "DMA_CHANNEL %d status timeout\n", ch);
	}

	return ret;
}

void memcpy_from_scratch(struct snd_sof_dev *sdev, u32 offset, unsigned int *dst, size_t bytes)
{
	unsigned int reg_offset = offset + ACP_SCRATCH_REG_0;
	int i, j;

	for (i = 0, j = 0; i < bytes; i = i + 4, j++)
		dst[j] = snd_sof_dsp_read(sdev, ACP_DSP_BAR, reg_offset + i);
}

void memcpy_to_scratch(struct snd_sof_dev *sdev, u32 offset, unsigned int *src, size_t bytes)
{
	unsigned int reg_offset = offset + ACP_SCRATCH_REG_0;
	int i, j;

	for (i = 0, j = 0; i < bytes; i = i + 4, j++)
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, reg_offset + i, src[j]);
}

static int acp_memory_init(struct snd_sof_dev *sdev)
{
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;

	snd_sof_dsp_update_bits(sdev, ACP_DSP_BAR, ACP_DSP_SW_INTR_CNTL,
				ACP_DSP_INTR_EN_MASK, ACP_DSP_INTR_EN_MASK);
	configure_acp_groupregisters(adata);
	init_dma_descriptor(adata);

	return 0;
}

static int acp_power_on(struct snd_sof_dev *sdev)
{
	unsigned int val;
	int ret;

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_PGFSM_STATUS);

	if (val == ACP_POWERED_ON)
		return 0;

	if (val & ACP_PGFSM_STATUS_MASK)
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_PGFSM_CONTROL,
				  ACP_PGFSM_CNTL_POWER_ON_MASK);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_PGFSM_STATUS, val, !val,
					    ACP_REG_POLL_INTERVAL, ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0)
		dev_err(sdev->dev, "timeout in ACP_PGFSM_STATUS read\n");

	return ret;
}

static int acp_reset(struct snd_sof_dev *sdev)
{
	unsigned int val;
	int ret;

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, ACP_ASSERT_RESET);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, val,
					    val & ACP_SOFT_RESET_DONE_MASK,
					    ACP_REG_POLL_INTERVAL, ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev, "timeout asserting reset\n");
		return ret;
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, ACP_RELEASE_RESET);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, val, !val,
					    ACP_REG_POLL_INTERVAL, ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0)
		dev_err(sdev->dev, "timeout in releasing reset\n");

	return ret;
}

static int acp_init(struct snd_sof_dev *sdev)
{
	int ret;

	/* power on */
	ret = acp_power_on(sdev);
	if (ret) {
		dev_err(sdev->dev, "ACP power on failed\n");
		return ret;
	}
	/* Reset */
	return acp_reset(sdev);
}

int amd_sof_acp_probe(struct snd_sof_dev *sdev)
{
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	struct acp_dev_data *adata;
	unsigned int addr;
	int ret;

	adata = devm_kzalloc(sdev->dev, sizeof(struct acp_dev_data),
			     GFP_KERNEL);
	if (!adata)
		return -ENOMEM;

	adata->dev = sdev;
	addr = pci_resource_start(pci, ACP_DSP_BAR);
	sdev->bar[ACP_DSP_BAR] = devm_ioremap(sdev->dev, addr, pci_resource_len(pci, ACP_DSP_BAR));
	if (!sdev->bar[ACP_DSP_BAR]) {
		dev_err(sdev->dev, "ioremap error\n");
		return -ENXIO;
	}

	pci_set_master(pci);

	sdev->pdata->hw_pdata = adata;

	ret = acp_init(sdev);
	if (ret < 0)
		return ret;

	acp_memory_init(sdev);

	return 0;
}
EXPORT_SYMBOL_NS(amd_sof_acp_probe, SND_SOC_SOF_AMD_COMMON);

int amd_sof_acp_remove(struct snd_sof_dev *sdev)
{
	return acp_reset(sdev);
}
EXPORT_SYMBOL_NS(amd_sof_acp_remove, SND_SOC_SOF_AMD_COMMON);

MODULE_DESCRIPTION("AMD ACP sof driver");
MODULE_LICENSE("Dual BSD/GPL");
