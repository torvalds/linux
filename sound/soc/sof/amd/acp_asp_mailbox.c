// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Authors: Vijendar Mukunda <Vijendar.Mukunda@amd.com>

/*
 * ASP (AMD Security Processor) mailbox communication and ACP7.B/7.F carveout
 * memory firmware load/restore for the ACP7.B/7.F platform.
 */

#include <linux/acpi.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/unaligned.h>
#include <asm/amd/node.h>

#include "../ops.h"
#include "acp-dsp-offset.h"
#include "acp.h"

/*
 * acp7x_asp_send_cmd - send one ASP mailbox command via the ACPI AMSG method.
 *
 * The ACPI method "AMSG" (stored as 'GSMA' LE) is used to
 * proxy commands to ASP. Direct SMN writes to MPASP_C2PMSG_* do not work
 * because the BIOS gates host access to those registers and requires all
 * ASP mailbox traffic to go through this ACPI method.
 *
 * The caller must fill adata->asp_mbox_buf with the payload before calling.
 * On return the same buffer contains ASP's response fields.
 *
 * ACPI method signature: AMSG(CommandId, PayloadPhysAddrHi, PayloadPhysAddrLo)
 * GET_CARVEOUT_ADDR uses a short 50 ms probe timeout; other commands use 5 s.
 */
static int acp7x_asp_send_cmd(struct snd_sof_dev *sdev, u16 cmd_id)
{
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;
	struct acpi_device *adev = ACPI_COMPANION(&pci->dev);
	union acpi_object args[3];
	struct acpi_object_list arg_list = { ARRAY_SIZE(args), args };
	unsigned long timeout;
	acpi_handle handle;
	acpi_status status;
	unsigned int ready_timeout;
	u32 ctrl;
	int ret, smn_ret;

	if (!adev) {
		dev_err(sdev->dev, "ASP cmd 0x%04x: no ACPI companion device\n", cmd_id);
		return -ENODEV;
	}

	handle = adev->handle;

	/* Short timeout for GET_CARVEOUT_ADDR to fail fast if ASP lacks carveout support. */
	ready_timeout = (cmd_id == ASP_MBOX_CMD_GET_CARVEOUT_ADDR) ?
			ASP_MBOX_PROBE_TIMEOUT_US : ASP_MBOX_TIMEOUT_US;

	timeout = jiffies + usecs_to_jiffies(ready_timeout);
	ret = -ETIMEDOUT;
	do {
		smn_ret = amd_smn_read(0, MPASP_C2PMSG_173_REG, &ctrl);
		if (smn_ret) {
			dev_err(sdev->dev, "ASP mailbox SMN read failed: %d\n", smn_ret);
			return smn_ret;
		}
		if (ctrl & ASP_MBOX_READY_BIT) {
			ret = 0;
			break;
		}
		usleep_range(ASP_MBOX_POLL_INTERVAL_US, ASP_MBOX_POLL_INTERVAL_US + 100);
	} while (!time_after(jiffies, timeout));

	if (ret) {
		dev_err(sdev->dev, "ASP mailbox not ready before cmd 0x%04x\n", cmd_id);
		return -ETIMEDOUT;
	}

	args[0].type = ACPI_TYPE_INTEGER;
	args[0].integer.value = cmd_id;
	args[1].type = ACPI_TYPE_INTEGER;
	args[1].integer.value = upper_32_bits(adata->asp_mbox_buf_phys);
	args[2].type = ACPI_TYPE_INTEGER;
	args[2].integer.value = lower_32_bits(adata->asp_mbox_buf_phys);

	status = acpi_evaluate_object(handle, "AMSG", &arg_list, NULL);
	if (ACPI_FAILURE(status)) {
		dev_err(sdev->dev, "ASP AMSG cmd 0x%04x: ACPI method failed: %s\n",
			cmd_id, acpi_format_exception(status));
		return -EIO;
	}
	return 0;
}

/*
 * acp7x_configure_carveout_pte - program ATU groups for ACP7.B/7.F carveout.
 *
 * Programs PAGE_SIZE, BASE_ADDR and PTE entries for each ATU group covering
 * the carveout region using 2 MB pages with PAGE_Enable | MALL_Enable flags.
 * Must be called after GET_CARVEOUT_ADDR and on every D0 resume since
 * acp_init() resets the scratch SRAM and ATU registers.
 */
int acp7x_configure_carveout_pte(struct snd_sof_dev *sdev)
{
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	u64 phys_addr;
	u32 grp_start = adata->asp_carveout_group_start;
	u32 grp_count = adata->asp_carveout_group_count;
	u32 total_pages, pages_per_group;
	u32 grp, page_in_grp, page_idx;
	u32 pte_scratch_offset, page_size_reg, base_addr_reg, reg_val;
	u32 pte_lo_addr, pte_hi_addr;
	u32 low, high;
	u32 abs_grp;

	if (!adata->asp_carveout_base) {
		dev_err(sdev->dev, "carveout PTE: no valid carveout address\n");
		return -EINVAL;
	}

	if (!grp_count || grp_start < 8 || grp_start + grp_count > 16) {
		dev_err(sdev->dev, "carveout PTE: invalid ATU group range %u+%u\n",
			grp_start, grp_count);
		return -EINVAL;
	}

	/*
	 * ASP is expected to allocate exactly four contiguous 8 MB ATU groups
	 * for the fixed 32 MB carveout. Warn if the response differs so that
	 * unexpected firmware behaviour is clearly visible in the log.
	 */
	if (grp_count != 4)
		dev_warn(sdev->dev, "carveout PTE: expected 4 ATU groups, got %u\n",
			 grp_count);

	total_pages = adata->asp_carveout_size / ACP7X_CARVEOUT_PAGE_SIZE;
	pages_per_group = total_pages / grp_count;
	page_idx = 0;

	for (grp = 0; grp < grp_count; grp++) {
		abs_grp = grp_start + grp;

		/*
		 * asp_carveout_group_start is the 0-based index from
		 * __ffs(mall_addr_valid): bit N in MallGroupEntries represents
		 * hardware GRP_(N+1), so abs_grp is 0-based and GRP number is
		 * (abs_grp + 1). Example: bits 11-14 set -> abs_grp 11..14 ->
		 * programs GRP_12..GRP_15 (registers 0xC5C..0xC74).
		 * ATU_CTRL (0xC40) sits between GRP_8 and GRP_9, adding a
		 * 4-byte gap for abs_grp >= 8 (i.e. GRP_9 and above).
		 */
		if (abs_grp < 8)
			page_size_reg = ACPAXI2AXI_ATU_PAGE_SIZE_GRP_1 + abs_grp * 8;
		else
			page_size_reg = ACPAXI2AXI_ATU_PAGE_SIZE_GRP_1 + abs_grp * 8 + 4;

		base_addr_reg = page_size_reg + 4;

		pte_scratch_offset = abs_grp * 0x20;
		reg_val = desc->sram_pte_offset + pte_scratch_offset;

		snd_sof_dsp_write(sdev, ACP_DSP_BAR, page_size_reg, PAGE_SIZE_2M_ENABLE);
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, base_addr_reg, reg_val | BIT(31));

		dev_dbg(sdev->dev,
			"carveout ATU: abs_grp=%u PAGE_SIZE_reg=0x%x BASE_ADDR_reg=0x%x val=0x%x\n",
			abs_grp, page_size_reg, base_addr_reg,
			(u32)(reg_val | BIT(31)));

		for (page_in_grp = 0; page_in_grp < pages_per_group;
		     page_in_grp++, page_idx++) {
			pte_lo_addr = ACP_SCRATCH_REG_0 + pte_scratch_offset + (page_in_grp * 8);
			pte_hi_addr = pte_lo_addr + 4;
			phys_addr = adata->asp_carveout_base +
				    ((u64)page_idx * ACP7X_CARVEOUT_PAGE_SIZE);
			low  = lower_32_bits(phys_addr);
			high = upper_32_bits(phys_addr) | ACP_ATU_PTE_CARVEOUT_FLAGS;

			snd_sof_dsp_write(sdev, ACP_DSP_BAR, pte_lo_addr, low);
			snd_sof_dsp_write(sdev, ACP_DSP_BAR, pte_hi_addr, high);
		}
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACPAXI2AXI_ATU_CTRL, ACP_ATU_CACHE_INVALID);

	dev_dbg(sdev->dev,
		"carveout PTE done: %u groups [%u..%u] covering 0x%llx..0x%llx\n",
		grp_count, grp_start, grp_start + grp_count - 1,
		adata->asp_carveout_base,
		adata->asp_carveout_base + (u64)adata->asp_carveout_size);
	return 0;
}
EXPORT_SYMBOL_NS(acp7x_configure_carveout_pte, "SND_SOC_SOF_AMD_COMMON");

/*
 * acp7x_alloc_asp_payload - allocate a physically contiguous buffer below 4 GB.
 *
 * ASP accesses memory via the SoC fabric, bypassing the IOMMU, so the buffer
 * must be below 4 GB and page_to_phys() must return the real CPU physical
 * address.  Use __GFP_DMA32/__GFP_DMA as hard zone constraints (not hints)
 * to guarantee the physical address fits in 32 bits.
 */
static void *acp7x_alloc_asp_payload(struct snd_sof_dev *sdev, phys_addr_t *phys_out)
{
	struct page *page;
	void *vaddr;

	page = alloc_page(GFP_KERNEL | __GFP_DMA32 | __GFP_ZERO);
	if (!page)
		page = alloc_page(GFP_KERNEL | __GFP_DMA | __GFP_ZERO);
	if (!page) {
		dev_err(sdev->dev, "ASP mbox: failed to allocate below-4GB payload page\n");
		return NULL;
	}

	vaddr = page_address(page);
	*phys_out = page_to_phys(page);

	if (*phys_out > 0xFFFFFFFFULL) {
		dev_err(sdev->dev, "ASP mbox: page at phys=0x%llx still exceeds 4 GB\n",
			(u64)*phys_out);
		__free_page(page);
		return NULL;
	}

	return vaddr;
}

static void acp7x_free_asp_payload(void *vaddr)
{
	if (vaddr)
		__free_page(virt_to_page(vaddr));
}

int acp7x_query_asp_carveout(struct snd_sof_dev *sdev)
{
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;
	struct asp_get_carveout_payload *co_payload;
	phys_addr_t phys;
	u32 group_bits, first_bit, count;
	int ret = 0;

	co_payload = acp7x_alloc_asp_payload(sdev, &phys);
	if (!co_payload)
		return -ENOMEM;

	co_payload->cookie       = ASP_MBOX_COOKIE;
	/* Sentinel: ASP overwrites on success; 0xFFFFFFFF after the call means no carveout */
	co_payload->carveout_size   = 0xFFFFFFFF;
	co_payload->mall_addr_valid = 0xFFFFFFFF;
	co_payload->mall_valid      = 0xFFFFFFFF;

	adata->asp_mbox_buf     = co_payload;
	adata->asp_mbox_buf_phys = phys;

	ret = acp7x_asp_send_cmd(sdev, ASP_MBOX_CMD_GET_CARVEOUT_ADDR);

	dev_dbg(sdev->dev,
		"GET_CARVEOUT response: status=0x%x addr=0x%llx size=0x%x mall_valid=0x%x mall_addr_valid=0x%x\n",
		 co_payload->status, co_payload->carveout_addr,
		 co_payload->carveout_size, co_payload->mall_valid,
		 co_payload->mall_addr_valid);

	if (ret)
		goto out_free;

	if (co_payload->status || !co_payload->carveout_addr ||
	    !co_payload->carveout_size || co_payload->carveout_size == 0xFFFFFFFF) {
		dev_warn(sdev->dev,
			 "ASP GET_CARVEOUT_ADDR: status=0x%x addr=0x%llx size=0x%x - no carveout\n",
			 co_payload->status, co_payload->carveout_addr,
			 co_payload->carveout_size);
		ret = -ENODATA;
		goto out_free;
	}

	if (!IS_ALIGNED(co_payload->carveout_addr, ACP7X_CARVEOUT_PAGE_SIZE) ||
	    co_payload->carveout_size != ACP7X_CARVEOUT_MAX_SIZE) {
		dev_warn(sdev->dev,
			 "ASP carveout: unexpected base alignment or size (addr=0x%llx size=0x%x, expected 0x%x)\n",
			 co_payload->carveout_addr, co_payload->carveout_size,
			 ACP7X_CARVEOUT_MAX_SIZE);
		ret = -ENODATA;
		goto out_free;
	}
	/*
	 * Extract ATU group start and count from MallGroupEntries bits[15:0].
	 * Scan to find first set bit (start), then count
	 * contiguous set bits. Example: 0x7800 -> start=11, count=4.
	 * Defer committing asp_carveout_base/size until all validation passes
	 * so adata is never left with a non-zero base but zero group_count.
	 */
	group_bits = co_payload->mall_addr_valid & 0xFFFF;
	first_bit = 0;
	count = 0;

	if (group_bits) {
		first_bit = __ffs(group_bits);
		while ((group_bits >> (first_bit + count)) & 1)
			count++;
		/* Validate contiguous run - reject non-contiguous masks */
		if (group_bits != (((1u << count) - 1) << first_bit)) {
			dev_warn(sdev->dev,
				 "ASP carveout: non-contiguous mall_addr_valid 0x%x unsupported\n",
				 group_bits);
			ret = -ENODATA;
			goto out_free;
		}
	}
	if (!count) {
		dev_warn(sdev->dev, "ASP carveout: no valid ATU groups in mall_addr_valid\n");
		ret = -ENODATA;
		goto out_free;
	}
	adata->asp_carveout_base = co_payload->carveout_addr;
	adata->asp_carveout_size = co_payload->carveout_size;
	adata->asp_carveout_group_start = first_bit;
	adata->asp_carveout_group_count = count;

	dev_dbg(sdev->dev, "ASP carveout: base=0x%llx size=0x%x grp_start=%u grp_count=%u\n",
		adata->asp_carveout_base, adata->asp_carveout_size,
		adata->asp_carveout_group_start, adata->asp_carveout_group_count);

out_free:
	adata->asp_mbox_buf = NULL;
	adata->asp_mbox_buf_phys = 0;
	acp7x_free_asp_payload(co_payload);
	return ret;
}
EXPORT_SYMBOL_NS(acp7x_query_asp_carveout, "SND_SOC_SOF_AMD_COMMON");

int acp7x_load_firmware_carveout(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	struct acp_dev_data *adata = plat_data->hw_pdata;
	struct asp_validate_image_payload *vi_payload;
	const char *fw_filename;
	struct page *fw_page;
	phys_addr_t vi_phys;
	u64 dest;
	u32 page_count, dma_size;
	u32 size_fw_signed;
	unsigned int order;
	int ret;

	/*
	 * Carveout query was already done in amd_sof_acp7x_probe() via
	 * acp7x_query_asp_carveout(). If ASP did not return a valid carveout
	 * address, fail immediately - no fallback to legacy path.
	 */

	/* Program PTEs for the entire carveout region before any DMA into it.
	 * acp_init() in resume clears the scratch SRAM so PTEs must be reprogrammed
	 * on every boot including D3->D0 resume.
	 */
	ret = acp7x_configure_carveout_pte(sdev);
	if (ret)
		return ret;

	/*
	 * On D3->D0 resume the raw binary is already in the carveout region
	 * (placed by ASP during the previous VALIDATE_IMAGE call). Skip the
	 * filesystem load and VALIDATE_IMAGE - go directly to the data binary
	 * path. acp_dsp_pre_fw_run() will SHA-DMA from the preserved carveout
	 * address into DSP IRAM.
	 *
	 * On first boot fw_code_carveout_addr is 0 so the condition below is
	 * false and the full firmware load path executes.
	 */
	if (adata->fw_code_carveout_addr)
		goto load_data_binary;

	fw_filename = kasprintf(GFP_KERNEL, "%s/%s",
				plat_data->fw_filename_prefix, adata->fw_code_bin);
	if (!fw_filename)
		return -ENOMEM;

	/* Release any previously loaded firmware before requesting again */
	if (sdev->basefw.fw) {
		release_firmware(sdev->basefw.fw);
		sdev->basefw.fw = NULL;
	}

	ret = request_firmware(&sdev->basefw.fw, fw_filename, sdev->dev);
	kfree(fw_filename);
	if (ret < 0) {
		dev_err(sdev->dev, "request_firmware %s failed: %d\n",
			adata->fw_code_bin, ret);
		return ret;
	}

	page_count = PAGE_ALIGN(sdev->basefw.fw->size) >> PAGE_SHIFT;
	dma_size = page_count * ACP_PAGE_SIZE;

	/*
	 * Allocate the firmware staging buffer below 4 GB using alloc_pages so
	 * virt_to_phys() gives the real CPU physical address that ASP can read
	 * via the SoC fabric (bypassing the IOMMU). dma_alloc_coherent() gives
	 * an IOMMU-remapped DMA address which ASP cannot use.
	 *
	 * get_order() gives the smallest power-of-2 number of pages covering
	 * dma_size; alloc_pages() allocates that contiguous range.
	 */
	order = get_order(dma_size);
	fw_page = alloc_pages(GFP_KERNEL | __GFP_DMA32 | __GFP_ZERO, order);
	if (!fw_page)
		fw_page = alloc_pages(GFP_KERNEL | __GFP_DMA | __GFP_ZERO, order);
	if (!fw_page) {
		dev_err(sdev->dev, "failed to allocate FW staging buffer\n");
		release_firmware(sdev->basefw.fw);
		sdev->basefw.fw = NULL;
		return -ENOMEM;
	}
	adata->bin_buf     = page_address(fw_page);
	adata->sha_dma_addr = page_to_phys(fw_page);

	if (adata->sha_dma_addr > 0xFFFFFFFFULL) {
		dev_err(sdev->dev,
			"FW staging buffer at phys=0x%llx exceeds 4 GB\n",
			(u64)adata->sha_dma_addr);
		__free_pages(fw_page, order);
		adata->bin_buf = NULL;
		release_firmware(sdev->basefw.fw);
		sdev->basefw.fw = NULL;
		return -ENOMEM;
	}

	memcpy(adata->bin_buf, sdev->basefw.fw->data, sdev->basefw.fw->size);
	adata->fw_bin_size = sdev->basefw.fw->size;

	if (adata->fw_bin_size <= ACP_IMAGE_HEADER_SIZE) {
		dev_err(sdev->dev, "signed FW too small: %u\n", adata->fw_bin_size);
		ret = -EINVAL;
		goto free_bin;
	}
	/*
	 * SizeFWSigned (header[0x14]) is the size of the signed payload
	 * including the ASP signature trailer.  ASP strips the ACP image
	 * header (0x100 bytes) but preserves the signature, so the raw
	 * binary deposited in carveout has size = SizeFWSigned and the
	 * SHA DMA length must equal SizeFWSigned.
	 */
	size_fw_signed = get_unaligned_le32(adata->bin_buf + ACP_IMAGE_HDR_SIZE_FW_SIGNED_OFF);

	if (size_fw_signed <= ACP_ASP_SIGNATURE_LENGTH ||
	    size_fw_signed > adata->fw_bin_size - ACP_IMAGE_HEADER_SIZE) {
		dev_err(sdev->dev,
			"invalid SizeFWSigned 0x%x in carveout firmware header\n",
			size_fw_signed);
		ret = -EINVAL;
		goto free_bin;
	}
	adata->fw_code_raw_size = size_fw_signed;
	vi_payload = acp7x_alloc_asp_payload(sdev, &vi_phys);
	if (!vi_payload) {
		ret = -ENOMEM;
		goto free_bin;
	}

	vi_payload->cookie = ASP_MBOX_COOKIE;
	vi_payload->dest_offset = 0;
	/*
	 * ASP bypasses the CPU IOMMU and accesses memory via the SoC
	 * fabric directly, so it must be given a real CPU physical
	 * address for the staging buffer (not an IOMMU/DMA-translated
	 * address). Use virt_to_phys(bin_buf) which returns the real
	 * CPU physical address, same as for mailbox payload buffers.
	 */
	vi_payload->src_phys_addr = (u64)virt_to_phys(adata->bin_buf);
	vi_payload->fw_image_size = adata->fw_bin_size;

	adata->asp_mbox_buf = vi_payload;
	adata->asp_mbox_buf_phys = vi_phys;

	dev_dbg(sdev->dev, "sending VALIDATE_IMAGE(0x01): src=0x%llx size=0x%x dest_offset=0x%llx phys=0x%llx\n",
		vi_payload->src_phys_addr, vi_payload->fw_image_size,
		vi_payload->dest_offset, (u64)adata->asp_mbox_buf_phys);

	ret = acp7x_asp_send_cmd(sdev, ASP_MBOX_CMD_VALIDATE_IMAGE);

	/* Read result before freeing payload */
	if (!ret && !vi_payload->status) {
		dest = vi_payload->carveout_dest_addr;

		if (dest < adata->asp_carveout_base ||
		    dest - adata->asp_carveout_base + adata->fw_code_raw_size >
		    adata->asp_carveout_size) {
			dev_err(sdev->dev,
				"VALIDATE_IMAGE: dest 0x%llx outside carveout window\n",
				dest);
			ret = -EIO;
		} else {
			adata->fw_code_carveout_addr = dest;
		}
	} else if (!ret && vi_payload->status) {
		ret = -EIO;
	}

	adata->asp_mbox_buf     = NULL;
	adata->asp_mbox_buf_phys = 0;
	acp7x_free_asp_payload(vi_payload);

	if (ret) {
		if (ret == -EIO)
			dev_err(sdev->dev, "ASP VALIDATE_IMAGE failed\n");
		goto free_bin;
	}

	dev_dbg(sdev->dev, "ASP VALIDATE_IMAGE: raw binary at carveout 0x%llx size 0x%x\n",
		adata->fw_code_carveout_addr, adata->fw_code_raw_size);

	/* DDR staging buffer is no longer needed - raw binary is in carveout */
	free_pages((unsigned long)adata->bin_buf, get_order(dma_size));
	adata->bin_buf = NULL;

load_data_binary:
	fw_filename = kasprintf(GFP_KERNEL, "%s/%s",
				plat_data->fw_filename_prefix, adata->fw_data_bin);
	if (!fw_filename) {
		release_firmware(sdev->basefw.fw);
		sdev->basefw.fw = NULL;
		return -ENOMEM;
	}

	ret = request_firmware(&adata->fw_dbin, fw_filename, sdev->dev);
	kfree(fw_filename);
	if (ret < 0) {
		dev_err(sdev->dev, "request_firmware %s failed: %d\n",
			adata->fw_data_bin, ret);
		release_firmware(sdev->basefw.fw);
		sdev->basefw.fw = NULL;
		return ret;
	}

	ret = snd_sof_dsp_block_write(sdev, SOF_FW_BLK_TYPE_SRAM, 0,
				      (void *)adata->fw_dbin->data,
				      adata->fw_dbin->size);
	release_firmware(adata->fw_dbin);
	adata->fw_dbin = NULL;
	release_firmware(sdev->basefw.fw);
	sdev->basefw.fw = NULL;
	return ret;

free_bin:
	free_pages((unsigned long)adata->bin_buf, get_order(dma_size));
	adata->bin_buf = NULL;
	release_firmware(sdev->basefw.fw);
	sdev->basefw.fw = NULL;
	return ret;
}
EXPORT_SYMBOL_NS(acp7x_load_firmware_carveout, "SND_SOC_SOF_AMD_COMMON");
