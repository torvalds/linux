// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>

/*
 * Hardware interface for generic AMD audio DSP ACP IP
 */

#include "../ops.h"
#include "acp-dsp-offset.h"
#include "acp.h"

#define PTE_GRP1_OFFSET		0x00000000
#define PTE_GRP2_OFFSET		0x00800000
#define PTE_GRP3_OFFSET		0x01000000
#define PTE_GRP4_OFFSET		0x01800000
#define PTE_GRP5_OFFSET		0x02000000
#define PTE_GRP6_OFFSET		0x02800000
#define PTE_GRP7_OFFSET		0x03000000
#define PTE_GRP8_OFFSET		0x03800000

int acp_dsp_stream_config(struct snd_sof_dev *sdev, struct acp_dsp_stream *stream)
{
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	unsigned int pte_reg, pte_size, phy_addr_offset, index;
	int stream_tag = stream->stream_tag;
	u32 low, high, offset, reg_val;
	dma_addr_t addr;
	int page_idx;

	switch (stream_tag) {
	case 1:
		pte_reg = ACPAXI2AXI_ATU_BASE_ADDR_GRP_1;
		pte_size = ACPAXI2AXI_ATU_PAGE_SIZE_GRP_1;
		offset = offsetof(struct scratch_reg_conf, grp1_pte);
		stream->reg_offset = PTE_GRP1_OFFSET;
		break;
	case 2:
		pte_reg = ACPAXI2AXI_ATU_BASE_ADDR_GRP_2;
		pte_size = ACPAXI2AXI_ATU_PAGE_SIZE_GRP_2;
		offset = offsetof(struct scratch_reg_conf, grp2_pte);
		stream->reg_offset = PTE_GRP2_OFFSET;
		break;
	case 3:
		pte_reg = ACPAXI2AXI_ATU_BASE_ADDR_GRP_3;
		pte_size = ACPAXI2AXI_ATU_PAGE_SIZE_GRP_3;
		offset = offsetof(struct scratch_reg_conf, grp3_pte);
		stream->reg_offset = PTE_GRP3_OFFSET;
		break;
	case 4:
		pte_reg = ACPAXI2AXI_ATU_BASE_ADDR_GRP_4;
		pte_size = ACPAXI2AXI_ATU_PAGE_SIZE_GRP_4;
		offset = offsetof(struct scratch_reg_conf, grp4_pte);
		stream->reg_offset = PTE_GRP4_OFFSET;
		break;
	case 5:
		pte_reg = ACPAXI2AXI_ATU_BASE_ADDR_GRP_5;
		pte_size = ACPAXI2AXI_ATU_PAGE_SIZE_GRP_5;
		offset = offsetof(struct scratch_reg_conf, grp5_pte);
		stream->reg_offset = PTE_GRP5_OFFSET;
		break;
	case 6:
		pte_reg = ACPAXI2AXI_ATU_BASE_ADDR_GRP_6;
		pte_size = ACPAXI2AXI_ATU_PAGE_SIZE_GRP_6;
		offset = offsetof(struct scratch_reg_conf, grp6_pte);
		stream->reg_offset = PTE_GRP6_OFFSET;
		break;
	case 7:
		pte_reg = ACPAXI2AXI_ATU_BASE_ADDR_GRP_7;
		pte_size = ACPAXI2AXI_ATU_PAGE_SIZE_GRP_7;
		offset = offsetof(struct scratch_reg_conf, grp7_pte);
		stream->reg_offset = PTE_GRP7_OFFSET;
		break;
	case 8:
		pte_reg = ACPAXI2AXI_ATU_BASE_ADDR_GRP_8;
		pte_size = ACPAXI2AXI_ATU_PAGE_SIZE_GRP_8;
		offset = offsetof(struct scratch_reg_conf, grp8_pte);
		stream->reg_offset = PTE_GRP8_OFFSET;
		break;
	default:
		dev_err(sdev->dev, "Invalid stream tag %d\n", stream_tag);
		return -EINVAL;
	}

	/* write phy_addr in scratch memory */

	phy_addr_offset = sdev->debug_box.offset +
			  offsetof(struct scratch_reg_conf, reg_offset);
	index = stream_tag - 1;
	phy_addr_offset = phy_addr_offset + index * 4;

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SCRATCH_REG_0 +
			  phy_addr_offset, stream->reg_offset);

	/* Group Enable */
	offset = offset + sdev->debug_box.offset;
	reg_val = desc->sram_pte_offset + offset;
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, pte_reg, reg_val | BIT(31));
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, pte_size, PAGE_SIZE_4K_ENABLE);

	for (page_idx = 0; page_idx < stream->num_pages; page_idx++) {
		addr = snd_sgbuf_get_addr(stream->dmab, page_idx * PAGE_SIZE);

		/* Load the low address of page int ACP SRAM through SRBM */
		low = lower_32_bits(addr);
		high = upper_32_bits(addr);

		snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SCRATCH_REG_0 + offset, low);

		high |= BIT(31);
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SCRATCH_REG_0 + offset + 4, high);
		/* Move to next physically contiguous page */
		offset += 8;
	}

	/* Flush ATU Cache after PTE Update */
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACPAXI2AXI_ATU_CTRL, ACP_ATU_CACHE_INVALID);

	return 0;
}

struct acp_dsp_stream *acp_dsp_stream_get(struct snd_sof_dev *sdev, int tag)
{
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;
	struct acp_dsp_stream *stream = adata->stream_buf;
	int i;

	for (i = 0; i < ACP_MAX_STREAM; i++, stream++) {
		if (stream->active)
			continue;

		/* return stream if tag not specified*/
		if (!tag) {
			stream->active = 1;
			return stream;
		}

		/* check if this is the requested stream tag */
		if (stream->stream_tag == tag) {
			stream->active = 1;
			return stream;
		}
	}

	dev_err(sdev->dev, "stream %d active or no inactive stream\n", tag);
	return NULL;
}
EXPORT_SYMBOL_NS(acp_dsp_stream_get, SND_SOC_SOF_AMD_COMMON);

int acp_dsp_stream_put(struct snd_sof_dev *sdev,
		       struct acp_dsp_stream *acp_stream)
{
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;
	struct acp_dsp_stream *stream = adata->stream_buf;
	int i;

	/* Free an active stream */
	for (i = 0; i < ACP_MAX_STREAM; i++, stream++) {
		if (stream == acp_stream) {
			stream->active = 0;
			return 0;
		}
	}

	dev_err(sdev->dev, "Cannot find active stream tag %d\n", acp_stream->stream_tag);
	return -EINVAL;
}
EXPORT_SYMBOL_NS(acp_dsp_stream_put, SND_SOC_SOF_AMD_COMMON);

int acp_dsp_stream_init(struct snd_sof_dev *sdev)
{
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;
	int i;

	for (i = 0; i < ACP_MAX_STREAM; i++) {
		adata->stream_buf[i].sdev = sdev;
		adata->stream_buf[i].active = 0;
		adata->stream_buf[i].stream_tag = i + 1;
	}
	return 0;
}
EXPORT_SYMBOL_NS(acp_dsp_stream_init, SND_SOC_SOF_AMD_COMMON);
