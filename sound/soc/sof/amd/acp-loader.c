// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021, 2023 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>

/*
 * Hardware interface for ACP DSP Firmware binaries loader
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "../ops.h"
#include "acp-dsp-offset.h"
#include "acp.h"

#define FW_BIN			0
#define FW_DATA_BIN		1
#define FW_SRAM_DATA_BIN	2

#define FW_BIN_PTE_OFFSET	0x00
#define FW_DATA_BIN_PTE_OFFSET	0x08

#define ACP_DSP_RUN	0x00

int acp_dsp_block_read(struct snd_sof_dev *sdev, enum snd_sof_fw_blk_type blk_type,
		       u32 offset, void *dest, size_t size)
{
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	switch (blk_type) {
	case SOF_FW_BLK_TYPE_SRAM:
		offset = offset - desc->sram_pte_offset;
		memcpy_from_scratch(sdev, offset, dest, size);
		break;
	default:
		dev_err(sdev->dev, "bad blk type 0x%x\n", blk_type);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_NS(acp_dsp_block_read, SND_SOC_SOF_AMD_COMMON);

int acp_dsp_block_write(struct snd_sof_dev *sdev, enum snd_sof_fw_blk_type blk_type,
			u32 offset, void *src, size_t size)
{
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	struct acp_dev_data *adata;
	void *dest;
	u32 dma_size, page_count;
	unsigned int size_fw;

	adata = sdev->pdata->hw_pdata;

	switch (blk_type) {
	case SOF_FW_BLK_TYPE_IRAM:
		if (!adata->bin_buf) {
			size_fw = sdev->basefw.fw->size;
			page_count = PAGE_ALIGN(size_fw) >> PAGE_SHIFT;
			dma_size = page_count * ACP_PAGE_SIZE;
			adata->bin_buf = dma_alloc_coherent(&pci->dev, dma_size,
							    &adata->sha_dma_addr,
							    GFP_ATOMIC);
			if (!adata->bin_buf)
				return -ENOMEM;
		}
		adata->fw_bin_size = size + offset;
		dest = adata->bin_buf + offset;
		break;
	case SOF_FW_BLK_TYPE_DRAM:
		if (!adata->data_buf) {
			adata->data_buf = dma_alloc_coherent(&pci->dev,
							     ACP_DEFAULT_DRAM_LENGTH,
							     &adata->dma_addr,
							     GFP_ATOMIC);
			if (!adata->data_buf)
				return -ENOMEM;
		}
		dest = adata->data_buf + offset;
		adata->fw_data_bin_size = size + offset;
		adata->is_dram_in_use = true;
		break;
	case SOF_FW_BLK_TYPE_SRAM:
		if (!adata->sram_data_buf) {
			adata->sram_data_buf = dma_alloc_coherent(&pci->dev,
								  ACP_DEFAULT_SRAM_LENGTH,
								  &adata->sram_dma_addr,
								  GFP_ATOMIC);
			if (!adata->sram_data_buf)
				return -ENOMEM;
		}
		adata->fw_sram_data_bin_size = size + offset;
		dest = adata->sram_data_buf + offset;
		adata->is_sram_in_use = true;
		break;
	default:
		dev_err(sdev->dev, "bad blk type 0x%x\n", blk_type);
		return -EINVAL;
	}

	memcpy(dest, src, size);
	return 0;
}
EXPORT_SYMBOL_NS(acp_dsp_block_write, SND_SOC_SOF_AMD_COMMON);

int acp_get_bar_index(struct snd_sof_dev *sdev, u32 type)
{
	return type;
}
EXPORT_SYMBOL_NS(acp_get_bar_index, SND_SOC_SOF_AMD_COMMON);

static void configure_pte_for_fw_loading(int type, int num_pages, struct acp_dev_data *adata)
{
	struct snd_sof_dev *sdev = adata->dev;
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	unsigned int low, high;
	dma_addr_t addr;
	u16 page_idx;
	u32 offset;

	switch (type) {
	case FW_BIN:
		offset = FW_BIN_PTE_OFFSET;
		addr = adata->sha_dma_addr;
		break;
	case FW_DATA_BIN:
		offset = adata->fw_bin_page_count * 8;
		addr = adata->dma_addr;
		break;
	case FW_SRAM_DATA_BIN:
		offset = (adata->fw_bin_page_count + ACP_DRAM_PAGE_COUNT) * 8;
		addr = adata->sram_dma_addr;
		break;
	default:
		dev_err(sdev->dev, "Invalid data type %x\n", type);
		return;
	}

	/* Group Enable */
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACPAXI2AXI_ATU_BASE_ADDR_GRP_1,
			  desc->sram_pte_offset | BIT(31));
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACPAXI2AXI_ATU_PAGE_SIZE_GRP_1,
			  PAGE_SIZE_4K_ENABLE);

	for (page_idx = 0; page_idx < num_pages; page_idx++) {
		low = lower_32_bits(addr);
		high = upper_32_bits(addr);
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SCRATCH_REG_0 + offset, low);
		high |= BIT(31);
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SCRATCH_REG_0 + offset + 4, high);
		offset += 8;
		addr += PAGE_SIZE;
	}

	/* Flush ATU Cache after PTE Update */
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACPAXI2AXI_ATU_CTRL, ACP_ATU_CACHE_INVALID);
}

/* pre fw run operations */
int acp_dsp_pre_fw_run(struct snd_sof_dev *sdev)
{
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	struct acp_dev_data *adata;
	unsigned int src_addr, size_fw, dest_addr;
	u32 page_count, dma_size;
	int ret;

	adata = sdev->pdata->hw_pdata;

	if (adata->signed_fw_image)
		size_fw = adata->fw_bin_size - ACP_FIRMWARE_SIGNATURE;
	else
		size_fw = adata->fw_bin_size;

	page_count = PAGE_ALIGN(size_fw) >> PAGE_SHIFT;
	adata->fw_bin_page_count = page_count;

	configure_pte_for_fw_loading(FW_BIN, page_count, adata);
	ret = configure_and_run_sha_dma(adata, adata->bin_buf, ACP_SYSTEM_MEMORY_WINDOW,
					ACP_IRAM_BASE_ADDRESS, size_fw);
	if (ret < 0) {
		dev_err(sdev->dev, "SHA DMA transfer failed status: %d\n", ret);
		return ret;
	}
	if (adata->is_dram_in_use) {
		configure_pte_for_fw_loading(FW_DATA_BIN, ACP_DRAM_PAGE_COUNT, adata);
		src_addr = ACP_SYSTEM_MEMORY_WINDOW + (page_count * ACP_PAGE_SIZE);
		dest_addr = ACP_DRAM_BASE_ADDRESS;

		ret = configure_and_run_dma(adata, src_addr, dest_addr, adata->fw_data_bin_size);
		if (ret < 0) {
			dev_err(sdev->dev, "acp dma configuration failed: %d\n", ret);
			return ret;
		}
		ret = acp_dma_status(adata, 0);
		if (ret < 0)
			dev_err(sdev->dev, "acp dma transfer status: %d\n", ret);
	}
	if (adata->is_sram_in_use) {
		configure_pte_for_fw_loading(FW_SRAM_DATA_BIN, ACP_SRAM_PAGE_COUNT, adata);
		src_addr = ACP_SYSTEM_MEMORY_WINDOW + ACP_DEFAULT_SRAM_LENGTH +
			   (page_count * ACP_PAGE_SIZE);
		dest_addr = ACP_SRAM_BASE_ADDRESS;

		ret = configure_and_run_dma(adata, src_addr, dest_addr,
					    adata->fw_sram_data_bin_size);
		if (ret < 0) {
			dev_err(sdev->dev, "acp dma configuration failed: %d\n", ret);
			return ret;
		}
		ret = acp_dma_status(adata, 0);
		if (ret < 0)
			dev_err(sdev->dev, "acp dma transfer status: %d\n", ret);
	}

	if (desc->rev > 3) {
		/* Cache Window enable */
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DSP0_CACHE_OFFSET0, desc->sram_pte_offset);
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DSP0_CACHE_SIZE0, SRAM1_SIZE | BIT(31));
	}

	/* Free memory once DMA is complete */
	dma_size =  (PAGE_ALIGN(sdev->basefw.fw->size) >> PAGE_SHIFT) * ACP_PAGE_SIZE;
	dma_free_coherent(&pci->dev, dma_size, adata->bin_buf, adata->sha_dma_addr);
	adata->bin_buf = NULL;
	if (adata->is_dram_in_use) {
		dma_free_coherent(&pci->dev, ACP_DEFAULT_DRAM_LENGTH, adata->data_buf,
				  adata->dma_addr);
		adata->data_buf = NULL;
	}
	if (adata->is_sram_in_use) {
		dma_free_coherent(&pci->dev, ACP_DEFAULT_SRAM_LENGTH, adata->sram_data_buf,
				  adata->sram_dma_addr);
		adata->sram_data_buf = NULL;
	}
	return ret;
}
EXPORT_SYMBOL_NS(acp_dsp_pre_fw_run, SND_SOC_SOF_AMD_COMMON);

int acp_sof_dsp_run(struct snd_sof_dev *sdev)
{
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	int val;

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DSP0_RUNSTALL, ACP_DSP_RUN);
	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_DSP0_RUNSTALL);
	dev_dbg(sdev->dev, "ACP_DSP0_RUNSTALL : 0x%0x\n", val);

	/* Some platforms won't support fusion DSP,keep offset zero for no support */
	if (desc->fusion_dsp_offset && adata->enable_fw_debug) {
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->fusion_dsp_offset, ACP_DSP_RUN);
		val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, desc->fusion_dsp_offset);
		dev_dbg(sdev->dev, "ACP_DSP0_FUSION_RUNSTALL : 0x%0x\n", val);
	}
	return 0;
}
EXPORT_SYMBOL_NS(acp_sof_dsp_run, SND_SOC_SOF_AMD_COMMON);

int acp_sof_load_signed_firmware(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	struct acp_dev_data *adata = plat_data->hw_pdata;
	const char *fw_filename;
	int ret;

	fw_filename = kasprintf(GFP_KERNEL, "%s/%s",
				plat_data->fw_filename_prefix,
				adata->fw_code_bin);
	if (!fw_filename)
		return -ENOMEM;

	ret = request_firmware(&sdev->basefw.fw, fw_filename, sdev->dev);
	if (ret < 0) {
		kfree(fw_filename);
		dev_err(sdev->dev, "sof signed firmware code bin is missing\n");
		return ret;
	} else {
		dev_dbg(sdev->dev, "request_firmware %s successful\n", fw_filename);
	}
	kfree(fw_filename);

	ret = snd_sof_dsp_block_write(sdev, SOF_FW_BLK_TYPE_IRAM, 0,
				      (void *)sdev->basefw.fw->data,
				      sdev->basefw.fw->size);

	fw_filename = kasprintf(GFP_KERNEL, "%s/%s",
				plat_data->fw_filename_prefix,
				adata->fw_data_bin);
	if (!fw_filename)
		return -ENOMEM;

	ret = request_firmware(&adata->fw_dbin, fw_filename, sdev->dev);
	if (ret < 0) {
		kfree(fw_filename);
		dev_err(sdev->dev, "sof signed firmware data bin is missing\n");
		return ret;

	} else {
		dev_dbg(sdev->dev, "request_firmware %s successful\n", fw_filename);
	}
	kfree(fw_filename);

	ret = snd_sof_dsp_block_write(sdev, SOF_FW_BLK_TYPE_DRAM, 0,
				      (void *)adata->fw_dbin->data,
				      adata->fw_dbin->size);
	return ret;
}
EXPORT_SYMBOL_NS(acp_sof_load_signed_firmware, SND_SOC_SOF_AMD_COMMON);
