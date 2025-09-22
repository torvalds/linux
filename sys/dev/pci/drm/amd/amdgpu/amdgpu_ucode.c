/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "amdgpu.h"
#include "amdgpu_ucode.h"

#define AMDGPU_UCODE_NAME_MAX		(128)

static const struct kicker_device kicker_device_list[] = {
	{0x744B, 0x00},
};

static void amdgpu_ucode_print_common_hdr(const struct common_firmware_header *hdr)
{
	DRM_DEBUG("size_bytes: %u\n", le32_to_cpu(hdr->size_bytes));
	DRM_DEBUG("header_size_bytes: %u\n", le32_to_cpu(hdr->header_size_bytes));
	DRM_DEBUG("header_version_major: %u\n", le16_to_cpu(hdr->header_version_major));
	DRM_DEBUG("header_version_minor: %u\n", le16_to_cpu(hdr->header_version_minor));
	DRM_DEBUG("ip_version_major: %u\n", le16_to_cpu(hdr->ip_version_major));
	DRM_DEBUG("ip_version_minor: %u\n", le16_to_cpu(hdr->ip_version_minor));
	DRM_DEBUG("ucode_version: 0x%08x\n", le32_to_cpu(hdr->ucode_version));
	DRM_DEBUG("ucode_size_bytes: %u\n", le32_to_cpu(hdr->ucode_size_bytes));
	DRM_DEBUG("ucode_array_offset_bytes: %u\n",
		  le32_to_cpu(hdr->ucode_array_offset_bytes));
	DRM_DEBUG("crc32: 0x%08x\n", le32_to_cpu(hdr->crc32));
}

void amdgpu_ucode_print_mc_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("MC\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct mc_firmware_header_v1_0 *mc_hdr =
			container_of(hdr, struct mc_firmware_header_v1_0, header);

		DRM_DEBUG("io_debug_size_bytes: %u\n",
			  le32_to_cpu(mc_hdr->io_debug_size_bytes));
		DRM_DEBUG("io_debug_array_offset_bytes: %u\n",
			  le32_to_cpu(mc_hdr->io_debug_array_offset_bytes));
	} else {
		DRM_ERROR("Unknown MC ucode version: %u.%u\n", version_major, version_minor);
	}
}

void amdgpu_ucode_print_smc_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);
	const struct smc_firmware_header_v1_0 *v1_0_hdr;
	const struct smc_firmware_header_v2_0 *v2_0_hdr;
	const struct smc_firmware_header_v2_1 *v2_1_hdr;

	DRM_DEBUG("SMC\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		v1_0_hdr = container_of(hdr, struct smc_firmware_header_v1_0, header);
		DRM_DEBUG("ucode_start_addr: %u\n", le32_to_cpu(v1_0_hdr->ucode_start_addr));
	} else if (version_major == 2) {
		switch (version_minor) {
		case 0:
			v2_0_hdr = container_of(hdr, struct smc_firmware_header_v2_0, v1_0.header);
			DRM_DEBUG("ppt_offset_bytes: %u\n", le32_to_cpu(v2_0_hdr->ppt_offset_bytes));
			DRM_DEBUG("ppt_size_bytes: %u\n", le32_to_cpu(v2_0_hdr->ppt_size_bytes));
			break;
		case 1:
			v2_1_hdr = container_of(hdr, struct smc_firmware_header_v2_1, v1_0.header);
			DRM_DEBUG("pptable_count: %u\n", le32_to_cpu(v2_1_hdr->pptable_count));
			DRM_DEBUG("pptable_entry_offset: %u\n", le32_to_cpu(v2_1_hdr->pptable_entry_offset));
			break;
		default:
			break;
		}

	} else {
		DRM_ERROR("Unknown SMC ucode version: %u.%u\n", version_major, version_minor);
	}
}

void amdgpu_ucode_print_gfx_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("GFX\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct gfx_firmware_header_v1_0 *gfx_hdr =
			container_of(hdr, struct gfx_firmware_header_v1_0, header);

		DRM_DEBUG("ucode_feature_version: %u\n",
			  le32_to_cpu(gfx_hdr->ucode_feature_version));
		DRM_DEBUG("jt_offset: %u\n", le32_to_cpu(gfx_hdr->jt_offset));
		DRM_DEBUG("jt_size: %u\n", le32_to_cpu(gfx_hdr->jt_size));
	} else if (version_major == 2) {
		const struct gfx_firmware_header_v2_0 *gfx_hdr =
			container_of(hdr, struct gfx_firmware_header_v2_0, header);

		DRM_DEBUG("ucode_feature_version: %u\n",
			  le32_to_cpu(gfx_hdr->ucode_feature_version));
	} else {
		DRM_ERROR("Unknown GFX ucode version: %u.%u\n", version_major, version_minor);
	}
}

void amdgpu_ucode_print_rlc_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("RLC\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct rlc_firmware_header_v1_0 *rlc_hdr =
			container_of(hdr, struct rlc_firmware_header_v1_0, header);

		DRM_DEBUG("ucode_feature_version: %u\n",
			  le32_to_cpu(rlc_hdr->ucode_feature_version));
		DRM_DEBUG("save_and_restore_offset: %u\n",
			  le32_to_cpu(rlc_hdr->save_and_restore_offset));
		DRM_DEBUG("clear_state_descriptor_offset: %u\n",
			  le32_to_cpu(rlc_hdr->clear_state_descriptor_offset));
		DRM_DEBUG("avail_scratch_ram_locations: %u\n",
			  le32_to_cpu(rlc_hdr->avail_scratch_ram_locations));
		DRM_DEBUG("master_pkt_description_offset: %u\n",
			  le32_to_cpu(rlc_hdr->master_pkt_description_offset));
	} else if (version_major == 2) {
		const struct rlc_firmware_header_v2_0 *rlc_hdr =
			container_of(hdr, struct rlc_firmware_header_v2_0, header);
		const struct rlc_firmware_header_v2_1 *rlc_hdr_v2_1 =
			container_of(rlc_hdr, struct rlc_firmware_header_v2_1, v2_0);
		const struct rlc_firmware_header_v2_2 *rlc_hdr_v2_2 =
			container_of(rlc_hdr_v2_1, struct rlc_firmware_header_v2_2, v2_1);
		const struct rlc_firmware_header_v2_3 *rlc_hdr_v2_3 =
			container_of(rlc_hdr_v2_2, struct rlc_firmware_header_v2_3, v2_2);
		const struct rlc_firmware_header_v2_4 *rlc_hdr_v2_4 =
			container_of(rlc_hdr_v2_3, struct rlc_firmware_header_v2_4, v2_3);

		switch (version_minor) {
		case 0:
			/* rlc_hdr v2_0 */
			DRM_DEBUG("ucode_feature_version: %u\n",
				  le32_to_cpu(rlc_hdr->ucode_feature_version));
			DRM_DEBUG("jt_offset: %u\n", le32_to_cpu(rlc_hdr->jt_offset));
			DRM_DEBUG("jt_size: %u\n", le32_to_cpu(rlc_hdr->jt_size));
			DRM_DEBUG("save_and_restore_offset: %u\n",
				  le32_to_cpu(rlc_hdr->save_and_restore_offset));
			DRM_DEBUG("clear_state_descriptor_offset: %u\n",
				  le32_to_cpu(rlc_hdr->clear_state_descriptor_offset));
			DRM_DEBUG("avail_scratch_ram_locations: %u\n",
				  le32_to_cpu(rlc_hdr->avail_scratch_ram_locations));
			DRM_DEBUG("reg_restore_list_size: %u\n",
				  le32_to_cpu(rlc_hdr->reg_restore_list_size));
			DRM_DEBUG("reg_list_format_start: %u\n",
				  le32_to_cpu(rlc_hdr->reg_list_format_start));
			DRM_DEBUG("reg_list_format_separate_start: %u\n",
				  le32_to_cpu(rlc_hdr->reg_list_format_separate_start));
			DRM_DEBUG("starting_offsets_start: %u\n",
				  le32_to_cpu(rlc_hdr->starting_offsets_start));
			DRM_DEBUG("reg_list_format_size_bytes: %u\n",
				  le32_to_cpu(rlc_hdr->reg_list_format_size_bytes));
			DRM_DEBUG("reg_list_format_array_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr->reg_list_format_array_offset_bytes));
			DRM_DEBUG("reg_list_size_bytes: %u\n",
				  le32_to_cpu(rlc_hdr->reg_list_size_bytes));
			DRM_DEBUG("reg_list_array_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr->reg_list_array_offset_bytes));
			DRM_DEBUG("reg_list_format_separate_size_bytes: %u\n",
				  le32_to_cpu(rlc_hdr->reg_list_format_separate_size_bytes));
			DRM_DEBUG("reg_list_format_separate_array_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr->reg_list_format_separate_array_offset_bytes));
			DRM_DEBUG("reg_list_separate_size_bytes: %u\n",
				  le32_to_cpu(rlc_hdr->reg_list_separate_size_bytes));
			DRM_DEBUG("reg_list_separate_array_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr->reg_list_separate_array_offset_bytes));
			break;
		case 1:
			/* rlc_hdr v2_1 */
			DRM_DEBUG("reg_list_format_direct_reg_list_length: %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->reg_list_format_direct_reg_list_length));
			DRM_DEBUG("save_restore_list_cntl_ucode_ver: %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_cntl_ucode_ver));
			DRM_DEBUG("save_restore_list_cntl_feature_ver: %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_cntl_feature_ver));
			DRM_DEBUG("save_restore_list_cntl_size_bytes %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_cntl_size_bytes));
			DRM_DEBUG("save_restore_list_cntl_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_cntl_offset_bytes));
			DRM_DEBUG("save_restore_list_gpm_ucode_ver: %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_gpm_ucode_ver));
			DRM_DEBUG("save_restore_list_gpm_feature_ver: %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_gpm_feature_ver));
			DRM_DEBUG("save_restore_list_gpm_size_bytes %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_gpm_size_bytes));
			DRM_DEBUG("save_restore_list_gpm_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_gpm_offset_bytes));
			DRM_DEBUG("save_restore_list_srm_ucode_ver: %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_srm_ucode_ver));
			DRM_DEBUG("save_restore_list_srm_feature_ver: %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_srm_feature_ver));
			DRM_DEBUG("save_restore_list_srm_size_bytes %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_srm_size_bytes));
			DRM_DEBUG("save_restore_list_srm_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_1->save_restore_list_srm_offset_bytes));
			break;
		case 2:
			/* rlc_hdr v2_2 */
			DRM_DEBUG("rlc_iram_ucode_size_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_2->rlc_iram_ucode_size_bytes));
			DRM_DEBUG("rlc_iram_ucode_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_2->rlc_iram_ucode_offset_bytes));
			DRM_DEBUG("rlc_dram_ucode_size_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_2->rlc_dram_ucode_size_bytes));
			DRM_DEBUG("rlc_dram_ucode_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_2->rlc_dram_ucode_offset_bytes));
			break;
		case 3:
			/* rlc_hdr v2_3 */
			DRM_DEBUG("rlcp_ucode_version: %u\n",
				  le32_to_cpu(rlc_hdr_v2_3->rlcp_ucode_version));
			DRM_DEBUG("rlcp_ucode_feature_version: %u\n",
				  le32_to_cpu(rlc_hdr_v2_3->rlcp_ucode_feature_version));
			DRM_DEBUG("rlcp_ucode_size_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_3->rlcp_ucode_size_bytes));
			DRM_DEBUG("rlcp_ucode_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_3->rlcp_ucode_offset_bytes));
			DRM_DEBUG("rlcv_ucode_version: %u\n",
				  le32_to_cpu(rlc_hdr_v2_3->rlcv_ucode_version));
			DRM_DEBUG("rlcv_ucode_feature_version: %u\n",
				  le32_to_cpu(rlc_hdr_v2_3->rlcv_ucode_feature_version));
			DRM_DEBUG("rlcv_ucode_size_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_3->rlcv_ucode_size_bytes));
			DRM_DEBUG("rlcv_ucode_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_3->rlcv_ucode_offset_bytes));
			break;
		case 4:
			/* rlc_hdr v2_4 */
			DRM_DEBUG("global_tap_delays_ucode_size_bytes :%u\n",
				  le32_to_cpu(rlc_hdr_v2_4->global_tap_delays_ucode_size_bytes));
			DRM_DEBUG("global_tap_delays_ucode_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_4->global_tap_delays_ucode_offset_bytes));
			DRM_DEBUG("se0_tap_delays_ucode_size_bytes :%u\n",
				  le32_to_cpu(rlc_hdr_v2_4->se0_tap_delays_ucode_size_bytes));
			DRM_DEBUG("se0_tap_delays_ucode_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_4->se0_tap_delays_ucode_offset_bytes));
			DRM_DEBUG("se1_tap_delays_ucode_size_bytes :%u\n",
				  le32_to_cpu(rlc_hdr_v2_4->se1_tap_delays_ucode_size_bytes));
			DRM_DEBUG("se1_tap_delays_ucode_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_4->se1_tap_delays_ucode_offset_bytes));
			DRM_DEBUG("se2_tap_delays_ucode_size_bytes :%u\n",
				  le32_to_cpu(rlc_hdr_v2_4->se2_tap_delays_ucode_size_bytes));
			DRM_DEBUG("se2_tap_delays_ucode_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_4->se2_tap_delays_ucode_offset_bytes));
			DRM_DEBUG("se3_tap_delays_ucode_size_bytes :%u\n",
				  le32_to_cpu(rlc_hdr_v2_4->se3_tap_delays_ucode_size_bytes));
			DRM_DEBUG("se3_tap_delays_ucode_offset_bytes: %u\n",
				  le32_to_cpu(rlc_hdr_v2_4->se3_tap_delays_ucode_offset_bytes));
			break;
		default:
			DRM_ERROR("Unknown RLC v2 ucode: v2.%u\n", version_minor);
			break;
		}
	} else {
		DRM_ERROR("Unknown RLC ucode version: %u.%u\n", version_major, version_minor);
	}
}

void amdgpu_ucode_print_sdma_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("SDMA\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct sdma_firmware_header_v1_0 *sdma_hdr =
			container_of(hdr, struct sdma_firmware_header_v1_0, header);

		DRM_DEBUG("ucode_feature_version: %u\n",
			  le32_to_cpu(sdma_hdr->ucode_feature_version));
		DRM_DEBUG("ucode_change_version: %u\n",
			  le32_to_cpu(sdma_hdr->ucode_change_version));
		DRM_DEBUG("jt_offset: %u\n", le32_to_cpu(sdma_hdr->jt_offset));
		DRM_DEBUG("jt_size: %u\n", le32_to_cpu(sdma_hdr->jt_size));
		if (version_minor >= 1) {
			const struct sdma_firmware_header_v1_1 *sdma_v1_1_hdr =
				container_of(sdma_hdr, struct sdma_firmware_header_v1_1, v1_0);
			DRM_DEBUG("digest_size: %u\n", le32_to_cpu(sdma_v1_1_hdr->digest_size));
		}
	} else if (version_major == 2) {
		const struct sdma_firmware_header_v2_0 *sdma_hdr =
			container_of(hdr, struct sdma_firmware_header_v2_0, header);

		DRM_DEBUG("ucode_feature_version: %u\n",
			  le32_to_cpu(sdma_hdr->ucode_feature_version));
		DRM_DEBUG("ctx_jt_offset: %u\n", le32_to_cpu(sdma_hdr->ctx_jt_offset));
		DRM_DEBUG("ctx_jt_size: %u\n", le32_to_cpu(sdma_hdr->ctx_jt_size));
		DRM_DEBUG("ctl_ucode_offset: %u\n", le32_to_cpu(sdma_hdr->ctl_ucode_offset));
		DRM_DEBUG("ctl_jt_offset: %u\n", le32_to_cpu(sdma_hdr->ctl_jt_offset));
		DRM_DEBUG("ctl_jt_size: %u\n", le32_to_cpu(sdma_hdr->ctl_jt_size));
	} else if (version_major == 3) {
		const struct sdma_firmware_header_v3_0 *sdma_hdr =
			container_of(hdr, struct sdma_firmware_header_v3_0, header);

		DRM_DEBUG("ucode_reversion: %u\n",
			  le32_to_cpu(sdma_hdr->ucode_feature_version));
	} else {
		DRM_ERROR("Unknown SDMA ucode version: %u.%u\n",
			  version_major, version_minor);
	}
}

void amdgpu_ucode_print_psp_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);
	uint32_t fw_index;
	const struct psp_fw_bin_desc *desc;

	DRM_DEBUG("PSP\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct psp_firmware_header_v1_0 *psp_hdr =
			container_of(hdr, struct psp_firmware_header_v1_0, header);

		DRM_DEBUG("ucode_feature_version: %u\n",
			  le32_to_cpu(psp_hdr->sos.fw_version));
		DRM_DEBUG("sos_offset_bytes: %u\n",
			  le32_to_cpu(psp_hdr->sos.offset_bytes));
		DRM_DEBUG("sos_size_bytes: %u\n",
			  le32_to_cpu(psp_hdr->sos.size_bytes));
		if (version_minor == 1) {
			const struct psp_firmware_header_v1_1 *psp_hdr_v1_1 =
				container_of(psp_hdr, struct psp_firmware_header_v1_1, v1_0);
			DRM_DEBUG("toc_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->toc.fw_version));
			DRM_DEBUG("toc_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->toc.offset_bytes));
			DRM_DEBUG("toc_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->toc.size_bytes));
			DRM_DEBUG("kdb_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->kdb.fw_version));
			DRM_DEBUG("kdb_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->kdb.offset_bytes));
			DRM_DEBUG("kdb_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->kdb.size_bytes));
		}
		if (version_minor == 2) {
			const struct psp_firmware_header_v1_2 *psp_hdr_v1_2 =
				container_of(psp_hdr, struct psp_firmware_header_v1_2, v1_0);
			DRM_DEBUG("kdb_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_2->kdb.fw_version));
			DRM_DEBUG("kdb_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_2->kdb.offset_bytes));
			DRM_DEBUG("kdb_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_2->kdb.size_bytes));
		}
		if (version_minor == 3) {
			const struct psp_firmware_header_v1_1 *psp_hdr_v1_1 =
				container_of(psp_hdr, struct psp_firmware_header_v1_1, v1_0);
			const struct psp_firmware_header_v1_3 *psp_hdr_v1_3 =
				container_of(psp_hdr_v1_1, struct psp_firmware_header_v1_3, v1_1);
			DRM_DEBUG("toc_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.toc.fw_version));
			DRM_DEBUG("toc_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.toc.offset_bytes));
			DRM_DEBUG("toc_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.toc.size_bytes));
			DRM_DEBUG("kdb_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.kdb.fw_version));
			DRM_DEBUG("kdb_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.kdb.offset_bytes));
			DRM_DEBUG("kdb_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.kdb.size_bytes));
			DRM_DEBUG("spl_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->spl.fw_version));
			DRM_DEBUG("spl_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->spl.offset_bytes));
			DRM_DEBUG("spl_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->spl.size_bytes));
		}
	} else if (version_major == 2) {
		const struct psp_firmware_header_v2_0 *psp_hdr_v2_0 =
			 container_of(hdr, struct psp_firmware_header_v2_0, header);
		for (fw_index = 0; fw_index < le32_to_cpu(psp_hdr_v2_0->psp_fw_bin_count); fw_index++) {
			desc = &(psp_hdr_v2_0->psp_fw_bin[fw_index]);
			switch (desc->fw_type) {
			case PSP_FW_TYPE_PSP_SOS:
				DRM_DEBUG("psp_sos_version: %u\n",
					  le32_to_cpu(desc->fw_version));
				DRM_DEBUG("psp_sos_size_bytes: %u\n",
					  le32_to_cpu(desc->size_bytes));
				break;
			case PSP_FW_TYPE_PSP_SYS_DRV:
				DRM_DEBUG("psp_sys_drv_version: %u\n",
					  le32_to_cpu(desc->fw_version));
				DRM_DEBUG("psp_sys_drv_size_bytes: %u\n",
					  le32_to_cpu(desc->size_bytes));
				break;
			case PSP_FW_TYPE_PSP_KDB:
				DRM_DEBUG("psp_kdb_version: %u\n",
					  le32_to_cpu(desc->fw_version));
				DRM_DEBUG("psp_kdb_size_bytes: %u\n",
					  le32_to_cpu(desc->size_bytes));
				break;
			case PSP_FW_TYPE_PSP_TOC:
				DRM_DEBUG("psp_toc_version: %u\n",
					  le32_to_cpu(desc->fw_version));
				DRM_DEBUG("psp_toc_size_bytes: %u\n",
					  le32_to_cpu(desc->size_bytes));
				break;
			case PSP_FW_TYPE_PSP_SPL:
				DRM_DEBUG("psp_spl_version: %u\n",
					  le32_to_cpu(desc->fw_version));
				DRM_DEBUG("psp_spl_size_bytes: %u\n",
					  le32_to_cpu(desc->size_bytes));
				break;
			case PSP_FW_TYPE_PSP_RL:
				DRM_DEBUG("psp_rl_version: %u\n",
					  le32_to_cpu(desc->fw_version));
				DRM_DEBUG("psp_rl_size_bytes: %u\n",
					  le32_to_cpu(desc->size_bytes));
				break;
			case PSP_FW_TYPE_PSP_SOC_DRV:
				DRM_DEBUG("psp_soc_drv_version: %u\n",
					  le32_to_cpu(desc->fw_version));
				DRM_DEBUG("psp_soc_drv_size_bytes: %u\n",
					  le32_to_cpu(desc->size_bytes));
				break;
			case PSP_FW_TYPE_PSP_INTF_DRV:
				DRM_DEBUG("psp_intf_drv_version: %u\n",
					  le32_to_cpu(desc->fw_version));
				DRM_DEBUG("psp_intf_drv_size_bytes: %u\n",
					  le32_to_cpu(desc->size_bytes));
				break;
			case PSP_FW_TYPE_PSP_DBG_DRV:
				DRM_DEBUG("psp_dbg_drv_version: %u\n",
					  le32_to_cpu(desc->fw_version));
				DRM_DEBUG("psp_dbg_drv_size_bytes: %u\n",
					  le32_to_cpu(desc->size_bytes));
				break;
			case PSP_FW_TYPE_PSP_RAS_DRV:
				DRM_DEBUG("psp_ras_drv_version: %u\n",
					  le32_to_cpu(desc->fw_version));
				DRM_DEBUG("psp_ras_drv_size_bytes: %u\n",
					  le32_to_cpu(desc->size_bytes));
				break;
			default:
				DRM_DEBUG("Unsupported PSP fw type: %d\n", desc->fw_type);
				break;
			}
		}
	} else {
		DRM_ERROR("Unknown PSP ucode version: %u.%u\n",
			  version_major, version_minor);
	}
}

void amdgpu_ucode_print_gpu_info_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("GPU_INFO\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct gpu_info_firmware_header_v1_0 *gpu_info_hdr =
			container_of(hdr, struct gpu_info_firmware_header_v1_0, header);

		DRM_DEBUG("version_major: %u\n",
			  le16_to_cpu(gpu_info_hdr->version_major));
		DRM_DEBUG("version_minor: %u\n",
			  le16_to_cpu(gpu_info_hdr->version_minor));
	} else {
		DRM_ERROR("Unknown gpu_info ucode version: %u.%u\n", version_major, version_minor);
	}
}

static int amdgpu_ucode_validate(const struct firmware *fw)
{
	const struct common_firmware_header *hdr =
		(const struct common_firmware_header *)fw->data;

	if (fw->size == le32_to_cpu(hdr->size_bytes))
		return 0;

	return -EINVAL;
}

bool amdgpu_ucode_hdr_version(union amdgpu_firmware_header *hdr,
				uint16_t hdr_major, uint16_t hdr_minor)
{
	if ((hdr->common.header_version_major == hdr_major) &&
		(hdr->common.header_version_minor == hdr_minor))
		return true;
	return false;
}

enum amdgpu_firmware_load_type
amdgpu_ucode_get_load_type(struct amdgpu_device *adev, int load_type)
{
	switch (adev->asic_type) {
#ifdef CONFIG_DRM_AMDGPU_SI
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
	case CHIP_HAINAN:
		return AMDGPU_FW_LOAD_DIRECT;
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
	case CHIP_BONAIRE:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_HAWAII:
	case CHIP_MULLINS:
		return AMDGPU_FW_LOAD_DIRECT;
#endif
	case CHIP_TOPAZ:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		return AMDGPU_FW_LOAD_SMU;
	case CHIP_CYAN_SKILLFISH:
		if (!(load_type &&
		      adev->apu_flags & AMD_APU_IS_CYAN_SKILLFISH2))
			return AMDGPU_FW_LOAD_DIRECT;
		else
			return AMDGPU_FW_LOAD_PSP;
	default:
		if (!load_type)
			return AMDGPU_FW_LOAD_DIRECT;
		else if (load_type == 3)
			return AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO;
		else
			return AMDGPU_FW_LOAD_PSP;
	}
}

const char *amdgpu_ucode_name(enum AMDGPU_UCODE_ID ucode_id)
{
	switch (ucode_id) {
	case AMDGPU_UCODE_ID_SDMA0:
		return "SDMA0";
	case AMDGPU_UCODE_ID_SDMA1:
		return "SDMA1";
	case AMDGPU_UCODE_ID_SDMA2:
		return "SDMA2";
	case AMDGPU_UCODE_ID_SDMA3:
		return "SDMA3";
	case AMDGPU_UCODE_ID_SDMA4:
		return "SDMA4";
	case AMDGPU_UCODE_ID_SDMA5:
		return "SDMA5";
	case AMDGPU_UCODE_ID_SDMA6:
		return "SDMA6";
	case AMDGPU_UCODE_ID_SDMA7:
		return "SDMA7";
	case AMDGPU_UCODE_ID_SDMA_UCODE_TH0:
		return "SDMA_CTX";
	case AMDGPU_UCODE_ID_SDMA_UCODE_TH1:
		return "SDMA_CTL";
	case AMDGPU_UCODE_ID_CP_CE:
		return "CP_CE";
	case AMDGPU_UCODE_ID_CP_PFP:
		return "CP_PFP";
	case AMDGPU_UCODE_ID_CP_ME:
		return "CP_ME";
	case AMDGPU_UCODE_ID_CP_MEC1:
		return "CP_MEC1";
	case AMDGPU_UCODE_ID_CP_MEC1_JT:
		return "CP_MEC1_JT";
	case AMDGPU_UCODE_ID_CP_MEC2:
		return "CP_MEC2";
	case AMDGPU_UCODE_ID_CP_MEC2_JT:
		return "CP_MEC2_JT";
	case AMDGPU_UCODE_ID_CP_MES:
		return "CP_MES";
	case AMDGPU_UCODE_ID_CP_MES_DATA:
		return "CP_MES_DATA";
	case AMDGPU_UCODE_ID_CP_MES1:
		return "CP_MES_KIQ";
	case AMDGPU_UCODE_ID_CP_MES1_DATA:
		return "CP_MES_KIQ_DATA";
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL:
		return "RLC_RESTORE_LIST_CNTL";
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM:
		return "RLC_RESTORE_LIST_GPM_MEM";
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM:
		return "RLC_RESTORE_LIST_SRM_MEM";
	case AMDGPU_UCODE_ID_RLC_IRAM:
		return "RLC_IRAM";
	case AMDGPU_UCODE_ID_RLC_DRAM:
		return "RLC_DRAM";
	case AMDGPU_UCODE_ID_RLC_G:
		return "RLC_G";
	case AMDGPU_UCODE_ID_RLC_P:
		return "RLC_P";
	case AMDGPU_UCODE_ID_RLC_V:
		return "RLC_V";
	case AMDGPU_UCODE_ID_GLOBAL_TAP_DELAYS:
		return "GLOBAL_TAP_DELAYS";
	case AMDGPU_UCODE_ID_SE0_TAP_DELAYS:
		return "SE0_TAP_DELAYS";
	case AMDGPU_UCODE_ID_SE1_TAP_DELAYS:
		return "SE1_TAP_DELAYS";
	case AMDGPU_UCODE_ID_SE2_TAP_DELAYS:
		return "SE2_TAP_DELAYS";
	case AMDGPU_UCODE_ID_SE3_TAP_DELAYS:
		return "SE3_TAP_DELAYS";
	case AMDGPU_UCODE_ID_IMU_I:
		return "IMU_I";
	case AMDGPU_UCODE_ID_IMU_D:
		return "IMU_D";
	case AMDGPU_UCODE_ID_STORAGE:
		return "STORAGE";
	case AMDGPU_UCODE_ID_SMC:
		return "SMC";
	case AMDGPU_UCODE_ID_PPTABLE:
		return "PPTABLE";
	case AMDGPU_UCODE_ID_P2S_TABLE:
		return "P2STABLE";
	case AMDGPU_UCODE_ID_UVD:
		return "UVD";
	case AMDGPU_UCODE_ID_UVD1:
		return "UVD1";
	case AMDGPU_UCODE_ID_VCE:
		return "VCE";
	case AMDGPU_UCODE_ID_VCN:
		return "VCN";
	case AMDGPU_UCODE_ID_VCN1:
		return "VCN1";
	case AMDGPU_UCODE_ID_DMCU_ERAM:
		return "DMCU_ERAM";
	case AMDGPU_UCODE_ID_DMCU_INTV:
		return "DMCU_INTV";
	case AMDGPU_UCODE_ID_VCN0_RAM:
		return "VCN0_RAM";
	case AMDGPU_UCODE_ID_VCN1_RAM:
		return "VCN1_RAM";
	case AMDGPU_UCODE_ID_DMCUB:
		return "DMCUB";
	case AMDGPU_UCODE_ID_CAP:
		return "CAP";
	case AMDGPU_UCODE_ID_VPE_CTX:
		return "VPE_CTX";
	case AMDGPU_UCODE_ID_VPE_CTL:
		return "VPE_CTL";
	case AMDGPU_UCODE_ID_VPE:
		return "VPE";
	case AMDGPU_UCODE_ID_UMSCH_MM_UCODE:
		return "UMSCH_MM_UCODE";
	case AMDGPU_UCODE_ID_UMSCH_MM_DATA:
		return "UMSCH_MM_DATA";
	case AMDGPU_UCODE_ID_UMSCH_MM_CMD_BUFFER:
		return "UMSCH_MM_CMD_BUFFER";
	case AMDGPU_UCODE_ID_JPEG_RAM:
		return "JPEG";
	case AMDGPU_UCODE_ID_SDMA_RS64:
		return "RS64_SDMA";
	case AMDGPU_UCODE_ID_CP_RS64_PFP:
		return "RS64_PFP";
	case AMDGPU_UCODE_ID_CP_RS64_ME:
		return "RS64_ME";
	case AMDGPU_UCODE_ID_CP_RS64_MEC:
		return "RS64_MEC";
	case AMDGPU_UCODE_ID_CP_RS64_PFP_P0_STACK:
		return "RS64_PFP_P0_STACK";
	case AMDGPU_UCODE_ID_CP_RS64_PFP_P1_STACK:
		return "RS64_PFP_P1_STACK";
	case AMDGPU_UCODE_ID_CP_RS64_ME_P0_STACK:
		return "RS64_ME_P0_STACK";
	case AMDGPU_UCODE_ID_CP_RS64_ME_P1_STACK:
		return "RS64_ME_P1_STACK";
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P0_STACK:
		return "RS64_MEC_P0_STACK";
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P1_STACK:
		return "RS64_MEC_P1_STACK";
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P2_STACK:
		return "RS64_MEC_P2_STACK";
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P3_STACK:
		return "RS64_MEC_P3_STACK";
	case AMDGPU_UCODE_ID_ISP:
		return "ISP";
	default:
		return "UNKNOWN UCODE";
	}
}

static inline int amdgpu_ucode_is_valid(uint32_t fw_version)
{
	if (!fw_version)
		return -EINVAL;

	return 0;
}

#ifdef __linux__

#define FW_VERSION_ATTR(name, mode, field)				\
static ssize_t show_##name(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct drm_device *ddev = dev_get_drvdata(dev);			\
	struct amdgpu_device *adev = drm_to_adev(ddev);			\
									\
	if (!buf)							\
		return amdgpu_ucode_is_valid(adev->field);		\
									\
	return sysfs_emit(buf, "0x%08x\n", adev->field);		\
}									\
static DEVICE_ATTR(name, mode, show_##name, NULL)

FW_VERSION_ATTR(vce_fw_version, 0444, vce.fw_version);
FW_VERSION_ATTR(uvd_fw_version, 0444, uvd.fw_version);
FW_VERSION_ATTR(mc_fw_version, 0444, gmc.fw_version);
FW_VERSION_ATTR(me_fw_version, 0444, gfx.me_fw_version);
FW_VERSION_ATTR(pfp_fw_version, 0444, gfx.pfp_fw_version);
FW_VERSION_ATTR(ce_fw_version, 0444, gfx.ce_fw_version);
FW_VERSION_ATTR(rlc_fw_version, 0444, gfx.rlc_fw_version);
FW_VERSION_ATTR(rlc_srlc_fw_version, 0444, gfx.rlc_srlc_fw_version);
FW_VERSION_ATTR(rlc_srlg_fw_version, 0444, gfx.rlc_srlg_fw_version);
FW_VERSION_ATTR(rlc_srls_fw_version, 0444, gfx.rlc_srls_fw_version);
FW_VERSION_ATTR(mec_fw_version, 0444, gfx.mec_fw_version);
FW_VERSION_ATTR(mec2_fw_version, 0444, gfx.mec2_fw_version);
FW_VERSION_ATTR(imu_fw_version, 0444, gfx.imu_fw_version);
FW_VERSION_ATTR(sos_fw_version, 0444, psp.sos.fw_version);
FW_VERSION_ATTR(asd_fw_version, 0444, psp.asd_context.bin_desc.fw_version);
FW_VERSION_ATTR(ta_ras_fw_version, 0444, psp.ras_context.context.bin_desc.fw_version);
FW_VERSION_ATTR(ta_xgmi_fw_version, 0444, psp.xgmi_context.context.bin_desc.fw_version);
FW_VERSION_ATTR(smc_fw_version, 0444, pm.fw_version);
FW_VERSION_ATTR(sdma_fw_version, 0444, sdma.instance[0].fw_version);
FW_VERSION_ATTR(sdma2_fw_version, 0444, sdma.instance[1].fw_version);
FW_VERSION_ATTR(vcn_fw_version, 0444, vcn.fw_version);
FW_VERSION_ATTR(dmcu_fw_version, 0444, dm.dmcu_fw_version);
FW_VERSION_ATTR(mes_fw_version, 0444, mes.sched_version & AMDGPU_MES_VERSION_MASK);
FW_VERSION_ATTR(mes_kiq_fw_version, 0444, mes.kiq_version & AMDGPU_MES_VERSION_MASK);

static struct attribute *fw_attrs[] = {
	&dev_attr_vce_fw_version.attr, &dev_attr_uvd_fw_version.attr,
	&dev_attr_mc_fw_version.attr, &dev_attr_me_fw_version.attr,
	&dev_attr_pfp_fw_version.attr, &dev_attr_ce_fw_version.attr,
	&dev_attr_rlc_fw_version.attr, &dev_attr_rlc_srlc_fw_version.attr,
	&dev_attr_rlc_srlg_fw_version.attr, &dev_attr_rlc_srls_fw_version.attr,
	&dev_attr_mec_fw_version.attr, &dev_attr_mec2_fw_version.attr,
	&dev_attr_sos_fw_version.attr, &dev_attr_asd_fw_version.attr,
	&dev_attr_ta_ras_fw_version.attr, &dev_attr_ta_xgmi_fw_version.attr,
	&dev_attr_smc_fw_version.attr, &dev_attr_sdma_fw_version.attr,
	&dev_attr_sdma2_fw_version.attr, &dev_attr_vcn_fw_version.attr,
	&dev_attr_dmcu_fw_version.attr, &dev_attr_imu_fw_version.attr,
	&dev_attr_mes_fw_version.attr, &dev_attr_mes_kiq_fw_version.attr,
	NULL
};

#define to_dev_attr(x) container_of(x, struct device_attribute, attr)

static umode_t amdgpu_ucode_sys_visible(struct kobject *kobj,
					struct attribute *attr, int idx)
{
	struct device_attribute *dev_attr = to_dev_attr(attr);
	struct device *dev = kobj_to_dev(kobj);

	if (dev_attr->show(dev, dev_attr, NULL) == -EINVAL)
		return 0;

	return attr->mode;
}

static const struct attribute_group fw_attr_group = {
	.name = "fw_version",
	.attrs = fw_attrs,
	.is_visible = amdgpu_ucode_sys_visible
};

#endif /* __linux__ */

int amdgpu_ucode_sysfs_init(struct amdgpu_device *adev)
{
	return sysfs_create_group(&adev->dev->kobj, &fw_attr_group);
}

void amdgpu_ucode_sysfs_fini(struct amdgpu_device *adev)
{
	sysfs_remove_group(&adev->dev->kobj, &fw_attr_group);
}

static int amdgpu_ucode_init_single_fw(struct amdgpu_device *adev,
				       struct amdgpu_firmware_info *ucode,
				       uint64_t mc_addr, void *kptr)
{
	const struct common_firmware_header *header = NULL;
	const struct gfx_firmware_header_v1_0 *cp_hdr = NULL;
	const struct gfx_firmware_header_v2_0 *cpv2_hdr = NULL;
	const struct dmcu_firmware_header_v1_0 *dmcu_hdr = NULL;
	const struct dmcub_firmware_header_v1_0 *dmcub_hdr = NULL;
	const struct mes_firmware_header_v1_0 *mes_hdr = NULL;
	const struct sdma_firmware_header_v2_0 *sdma_hdr = NULL;
	const struct sdma_firmware_header_v3_0 *sdmav3_hdr = NULL;
	const struct imu_firmware_header_v1_0 *imu_hdr = NULL;
	const struct vpe_firmware_header_v1_0 *vpe_hdr = NULL;
	const struct umsch_mm_firmware_header_v1_0 *umsch_mm_hdr = NULL;
	u8 *ucode_addr;

	if (!ucode->fw)
		return 0;

	ucode->mc_addr = mc_addr;
	ucode->kaddr = kptr;

	if (ucode->ucode_id == AMDGPU_UCODE_ID_STORAGE)
		return 0;

	header = (const struct common_firmware_header *)ucode->fw->data;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;
	cpv2_hdr = (const struct gfx_firmware_header_v2_0 *)ucode->fw->data;
	dmcu_hdr = (const struct dmcu_firmware_header_v1_0 *)ucode->fw->data;
	dmcub_hdr = (const struct dmcub_firmware_header_v1_0 *)ucode->fw->data;
	mes_hdr = (const struct mes_firmware_header_v1_0 *)ucode->fw->data;
	sdma_hdr = (const struct sdma_firmware_header_v2_0 *)ucode->fw->data;
	sdmav3_hdr = (const struct sdma_firmware_header_v3_0 *)ucode->fw->data;
	imu_hdr = (const struct imu_firmware_header_v1_0 *)ucode->fw->data;
	vpe_hdr = (const struct vpe_firmware_header_v1_0 *)ucode->fw->data;
	umsch_mm_hdr = (const struct umsch_mm_firmware_header_v1_0 *)ucode->fw->data;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		switch (ucode->ucode_id) {
		case AMDGPU_UCODE_ID_SDMA_UCODE_TH0:
			ucode->ucode_size = le32_to_cpu(sdma_hdr->ctx_ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(sdma_hdr->header.ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_SDMA_UCODE_TH1:
			ucode->ucode_size = le32_to_cpu(sdma_hdr->ctl_ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(sdma_hdr->ctl_ucode_offset);
			break;
		case AMDGPU_UCODE_ID_SDMA_RS64:
			ucode->ucode_size = le32_to_cpu(sdmav3_hdr->ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(sdmav3_hdr->header.ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_MEC1:
		case AMDGPU_UCODE_ID_CP_MEC2:
			ucode->ucode_size = le32_to_cpu(header->ucode_size_bytes) -
				le32_to_cpu(cp_hdr->jt_size) * 4;
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_MEC1_JT:
		case AMDGPU_UCODE_ID_CP_MEC2_JT:
			ucode->ucode_size = le32_to_cpu(cp_hdr->jt_size) * 4;
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes) +
				le32_to_cpu(cp_hdr->jt_offset) * 4;
			break;
		case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL:
			ucode->ucode_size = adev->gfx.rlc.save_restore_list_cntl_size_bytes;
			ucode_addr = adev->gfx.rlc.save_restore_list_cntl;
			break;
		case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM:
			ucode->ucode_size = adev->gfx.rlc.save_restore_list_gpm_size_bytes;
			ucode_addr = adev->gfx.rlc.save_restore_list_gpm;
			break;
		case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM:
			ucode->ucode_size = adev->gfx.rlc.save_restore_list_srm_size_bytes;
			ucode_addr = adev->gfx.rlc.save_restore_list_srm;
			break;
		case AMDGPU_UCODE_ID_RLC_IRAM:
			ucode->ucode_size = adev->gfx.rlc.rlc_iram_ucode_size_bytes;
			ucode_addr = adev->gfx.rlc.rlc_iram_ucode;
			break;
		case AMDGPU_UCODE_ID_RLC_DRAM:
			ucode->ucode_size = adev->gfx.rlc.rlc_dram_ucode_size_bytes;
			ucode_addr = adev->gfx.rlc.rlc_dram_ucode;
			break;
		case AMDGPU_UCODE_ID_RLC_P:
			ucode->ucode_size = adev->gfx.rlc.rlcp_ucode_size_bytes;
			ucode_addr = adev->gfx.rlc.rlcp_ucode;
			break;
		case AMDGPU_UCODE_ID_RLC_V:
			ucode->ucode_size = adev->gfx.rlc.rlcv_ucode_size_bytes;
			ucode_addr = adev->gfx.rlc.rlcv_ucode;
			break;
		case AMDGPU_UCODE_ID_GLOBAL_TAP_DELAYS:
			ucode->ucode_size = adev->gfx.rlc.global_tap_delays_ucode_size_bytes;
			ucode_addr = adev->gfx.rlc.global_tap_delays_ucode;
			break;
		case AMDGPU_UCODE_ID_SE0_TAP_DELAYS:
			ucode->ucode_size = adev->gfx.rlc.se0_tap_delays_ucode_size_bytes;
			ucode_addr = adev->gfx.rlc.se0_tap_delays_ucode;
			break;
		case AMDGPU_UCODE_ID_SE1_TAP_DELAYS:
			ucode->ucode_size = adev->gfx.rlc.se1_tap_delays_ucode_size_bytes;
			ucode_addr = adev->gfx.rlc.se1_tap_delays_ucode;
			break;
		case AMDGPU_UCODE_ID_SE2_TAP_DELAYS:
			ucode->ucode_size = adev->gfx.rlc.se2_tap_delays_ucode_size_bytes;
			ucode_addr = adev->gfx.rlc.se2_tap_delays_ucode;
			break;
		case AMDGPU_UCODE_ID_SE3_TAP_DELAYS:
			ucode->ucode_size = adev->gfx.rlc.se3_tap_delays_ucode_size_bytes;
			ucode_addr = adev->gfx.rlc.se3_tap_delays_ucode;
			break;
		case AMDGPU_UCODE_ID_CP_MES:
			ucode->ucode_size = le32_to_cpu(mes_hdr->mes_ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(mes_hdr->mes_ucode_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_MES_DATA:
			ucode->ucode_size = le32_to_cpu(mes_hdr->mes_ucode_data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(mes_hdr->mes_ucode_data_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_MES1:
			ucode->ucode_size = le32_to_cpu(mes_hdr->mes_ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(mes_hdr->mes_ucode_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_MES1_DATA:
			ucode->ucode_size = le32_to_cpu(mes_hdr->mes_ucode_data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(mes_hdr->mes_ucode_data_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_DMCU_ERAM:
			ucode->ucode_size = le32_to_cpu(header->ucode_size_bytes) -
				le32_to_cpu(dmcu_hdr->intv_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_DMCU_INTV:
			ucode->ucode_size = le32_to_cpu(dmcu_hdr->intv_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes) +
				le32_to_cpu(dmcu_hdr->intv_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_DMCUB:
			ucode->ucode_size = le32_to_cpu(dmcub_hdr->inst_const_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_PPTABLE:
			ucode->ucode_size = ucode->fw->size;
			ucode_addr = (u8 *)ucode->fw->data;
			break;
		case AMDGPU_UCODE_ID_P2S_TABLE:
			ucode->ucode_size = ucode->fw->size;
			ucode_addr = (u8 *)ucode->fw->data;
			break;
		case AMDGPU_UCODE_ID_IMU_I:
			ucode->ucode_size = le32_to_cpu(imu_hdr->imu_iram_ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(imu_hdr->header.ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_IMU_D:
			ucode->ucode_size = le32_to_cpu(imu_hdr->imu_dram_ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(imu_hdr->header.ucode_array_offset_bytes) +
				le32_to_cpu(imu_hdr->imu_iram_ucode_size_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_RS64_PFP:
			ucode->ucode_size = le32_to_cpu(cpv2_hdr->ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_RS64_PFP_P0_STACK:
			ucode->ucode_size = le32_to_cpu(cpv2_hdr->data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(cpv2_hdr->data_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_RS64_PFP_P1_STACK:
			ucode->ucode_size = le32_to_cpu(cpv2_hdr->data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(cpv2_hdr->data_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_RS64_ME:
			ucode->ucode_size = le32_to_cpu(cpv2_hdr->ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_RS64_ME_P0_STACK:
			ucode->ucode_size = le32_to_cpu(cpv2_hdr->data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(cpv2_hdr->data_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_RS64_ME_P1_STACK:
			ucode->ucode_size = le32_to_cpu(cpv2_hdr->data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(cpv2_hdr->data_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_RS64_MEC:
			ucode->ucode_size = le32_to_cpu(cpv2_hdr->ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_RS64_MEC_P0_STACK:
			ucode->ucode_size = le32_to_cpu(cpv2_hdr->data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(cpv2_hdr->data_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_RS64_MEC_P1_STACK:
			ucode->ucode_size = le32_to_cpu(cpv2_hdr->data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(cpv2_hdr->data_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_RS64_MEC_P2_STACK:
			ucode->ucode_size = le32_to_cpu(cpv2_hdr->data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(cpv2_hdr->data_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_RS64_MEC_P3_STACK:
			ucode->ucode_size = le32_to_cpu(cpv2_hdr->data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(cpv2_hdr->data_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_VPE_CTX:
			ucode->ucode_size = le32_to_cpu(vpe_hdr->ctx_ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(vpe_hdr->header.ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_VPE_CTL:
			ucode->ucode_size = le32_to_cpu(vpe_hdr->ctl_ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(vpe_hdr->ctl_ucode_offset);
			break;
		case AMDGPU_UCODE_ID_UMSCH_MM_UCODE:
			ucode->ucode_size = le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(umsch_mm_hdr->header.ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_UMSCH_MM_DATA:
			ucode->ucode_size = le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_data_offset_bytes);
			break;
		default:
			ucode->ucode_size = le32_to_cpu(header->ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes);
			break;
		}
	} else {
		ucode->ucode_size = le32_to_cpu(header->ucode_size_bytes);
		ucode_addr = (u8 *)ucode->fw->data +
			le32_to_cpu(header->ucode_array_offset_bytes);
	}

	memcpy(ucode->kaddr, ucode_addr, ucode->ucode_size);

	return 0;
}

static int amdgpu_ucode_patch_jt(struct amdgpu_firmware_info *ucode,
				uint64_t mc_addr, void *kptr)
{
	const struct gfx_firmware_header_v1_0 *header = NULL;
	const struct common_firmware_header *comm_hdr = NULL;
	uint8_t *src_addr = NULL;
	uint8_t *dst_addr = NULL;

	if (!ucode->fw)
		return 0;

	comm_hdr = (const struct common_firmware_header *)ucode->fw->data;
	header = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;
	dst_addr = ucode->kaddr +
			   ALIGN(le32_to_cpu(comm_hdr->ucode_size_bytes),
			   PAGE_SIZE);
	src_addr = (uint8_t *)ucode->fw->data +
			   le32_to_cpu(comm_hdr->ucode_array_offset_bytes) +
			   (le32_to_cpu(header->jt_offset) * 4);
	memcpy(dst_addr, src_addr, le32_to_cpu(header->jt_size) * 4);

	return 0;
}

int amdgpu_ucode_create_bo(struct amdgpu_device *adev)
{
	if ((adev->firmware.load_type != AMDGPU_FW_LOAD_DIRECT) &&
	    (adev->firmware.load_type != AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO)) {
		amdgpu_bo_create_kernel(adev, adev->firmware.fw_size, PAGE_SIZE,
			(amdgpu_sriov_vf(adev) || adev->debug_use_vram_fw_buf) ?
			AMDGPU_GEM_DOMAIN_VRAM : AMDGPU_GEM_DOMAIN_GTT,
			&adev->firmware.fw_buf,
			&adev->firmware.fw_buf_mc,
			&adev->firmware.fw_buf_ptr);
		if (!adev->firmware.fw_buf) {
			dev_err(adev->dev, "failed to create kernel buffer for firmware.fw_buf\n");
			return -ENOMEM;
		} else if (amdgpu_sriov_vf(adev)) {
			memset(adev->firmware.fw_buf_ptr, 0, adev->firmware.fw_size);
		}
	}
	return 0;
}

void amdgpu_ucode_free_bo(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->firmware.fw_buf,
		&adev->firmware.fw_buf_mc,
		&adev->firmware.fw_buf_ptr);
}

int amdgpu_ucode_init_bo(struct amdgpu_device *adev)
{
	uint64_t fw_offset = 0;
	int i;
	struct amdgpu_firmware_info *ucode = NULL;

 /* for baremetal, the ucode is allocated in gtt, so don't need to fill the bo when reset/suspend */
	if (!amdgpu_sriov_vf(adev) && (amdgpu_in_reset(adev) || adev->in_suspend))
		return 0;
	/*
	 * if SMU loaded firmware, it needn't add SMC, UVD, and VCE
	 * ucode info here
	 */
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		if (amdgpu_sriov_vf(adev))
			adev->firmware.max_ucodes = AMDGPU_UCODE_ID_MAXIMUM - 3;
		else
			adev->firmware.max_ucodes = AMDGPU_UCODE_ID_MAXIMUM - 4;
	} else {
		adev->firmware.max_ucodes = AMDGPU_UCODE_ID_MAXIMUM;
	}

	for (i = 0; i < adev->firmware.max_ucodes; i++) {
		ucode = &adev->firmware.ucode[i];
		if (ucode->fw) {
			amdgpu_ucode_init_single_fw(adev, ucode, adev->firmware.fw_buf_mc + fw_offset,
						    adev->firmware.fw_buf_ptr + fw_offset);
			if (i == AMDGPU_UCODE_ID_CP_MEC1 &&
			    adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
				const struct gfx_firmware_header_v1_0 *cp_hdr;

				cp_hdr = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;
				amdgpu_ucode_patch_jt(ucode,  adev->firmware.fw_buf_mc + fw_offset,
						    adev->firmware.fw_buf_ptr + fw_offset);
				fw_offset += ALIGN(le32_to_cpu(cp_hdr->jt_size) << 2, PAGE_SIZE);
			}
			fw_offset += ALIGN(ucode->ucode_size, PAGE_SIZE);
		}
	}
	return 0;
}

static const char *amdgpu_ucode_legacy_naming(struct amdgpu_device *adev, int block_type)
{
	if (block_type == MP0_HWIP) {
		switch (amdgpu_ip_version(adev, MP0_HWIP, 0)) {
		case IP_VERSION(9, 0, 0):
			switch (adev->asic_type) {
			case CHIP_VEGA10:
				return "vega10";
			case CHIP_VEGA12:
				return "vega12";
			default:
				return NULL;
			}
		case IP_VERSION(10, 0, 0):
		case IP_VERSION(10, 0, 1):
			if (adev->asic_type == CHIP_RAVEN) {
				if (adev->apu_flags & AMD_APU_IS_RAVEN2)
					return "raven2";
				else if (adev->apu_flags & AMD_APU_IS_PICASSO)
					return "picasso";
				return "raven";
			}
			break;
		case IP_VERSION(11, 0, 0):
			return "navi10";
		case IP_VERSION(11, 0, 2):
			return "vega20";
		case IP_VERSION(11, 0, 3):
			return "renoir";
		case IP_VERSION(11, 0, 4):
			return "arcturus";
		case IP_VERSION(11, 0, 5):
			return "navi14";
		case IP_VERSION(11, 0, 7):
			return "sienna_cichlid";
		case IP_VERSION(11, 0, 9):
			return "navi12";
		case IP_VERSION(11, 0, 11):
			return "navy_flounder";
		case IP_VERSION(11, 0, 12):
			return "dimgrey_cavefish";
		case IP_VERSION(11, 0, 13):
			return "beige_goby";
		case IP_VERSION(11, 5, 0):
			return "vangogh";
		case IP_VERSION(12, 0, 1):
			return "green_sardine";
		case IP_VERSION(13, 0, 2):
			return "aldebaran";
		case IP_VERSION(13, 0, 1):
		case IP_VERSION(13, 0, 3):
			return "yellow_carp";
		}
	} else if (block_type == MP1_HWIP) {
		switch (amdgpu_ip_version(adev, MP1_HWIP, 0)) {
		case IP_VERSION(9, 0, 0):
		case IP_VERSION(10, 0, 0):
		case IP_VERSION(10, 0, 1):
		case IP_VERSION(11, 0, 2):
			if (adev->asic_type == CHIP_ARCTURUS)
				return "arcturus_smc";
			return NULL;
		case IP_VERSION(11, 0, 0):
			return "navi10_smc";
		case IP_VERSION(11, 0, 5):
			return "navi14_smc";
		case IP_VERSION(11, 0, 9):
			return "navi12_smc";
		case IP_VERSION(11, 0, 7):
			return "sienna_cichlid_smc";
		case IP_VERSION(11, 0, 11):
			return "navy_flounder_smc";
		case IP_VERSION(11, 0, 12):
			return "dimgrey_cavefish_smc";
		case IP_VERSION(11, 0, 13):
			return "beige_goby_smc";
		case IP_VERSION(13, 0, 2):
			return "aldebaran_smc";
		}
	} else if (block_type == SDMA0_HWIP) {
		switch (amdgpu_ip_version(adev, SDMA0_HWIP, 0)) {
		case IP_VERSION(4, 0, 0):
			return "vega10_sdma";
		case IP_VERSION(4, 0, 1):
			return "vega12_sdma";
		case IP_VERSION(4, 1, 0):
		case IP_VERSION(4, 1, 1):
			if (adev->apu_flags & AMD_APU_IS_RAVEN2)
				return "raven2_sdma";
			else if (adev->apu_flags & AMD_APU_IS_PICASSO)
				return "picasso_sdma";
			return "raven_sdma";
		case IP_VERSION(4, 1, 2):
			if (adev->apu_flags & AMD_APU_IS_RENOIR)
				return "renoir_sdma";
			return "green_sardine_sdma";
		case IP_VERSION(4, 2, 0):
			return "vega20_sdma";
		case IP_VERSION(4, 2, 2):
			return "arcturus_sdma";
		case IP_VERSION(4, 4, 0):
			return "aldebaran_sdma";
		case IP_VERSION(5, 0, 0):
			return "navi10_sdma";
		case IP_VERSION(5, 0, 1):
			return "cyan_skillfish2_sdma";
		case IP_VERSION(5, 0, 2):
			return "navi14_sdma";
		case IP_VERSION(5, 0, 5):
			return "navi12_sdma";
		case IP_VERSION(5, 2, 0):
			return "sienna_cichlid_sdma";
		case IP_VERSION(5, 2, 2):
			return "navy_flounder_sdma";
		case IP_VERSION(5, 2, 4):
			return "dimgrey_cavefish_sdma";
		case IP_VERSION(5, 2, 5):
			return "beige_goby_sdma";
		case IP_VERSION(5, 2, 3):
			return "yellow_carp_sdma";
		case IP_VERSION(5, 2, 1):
			return "vangogh_sdma";
		}
	} else if (block_type == UVD_HWIP) {
		switch (amdgpu_ip_version(adev, UVD_HWIP, 0)) {
		case IP_VERSION(1, 0, 0):
		case IP_VERSION(1, 0, 1):
			if (adev->apu_flags & AMD_APU_IS_RAVEN2)
				return "raven2_vcn";
			else if (adev->apu_flags & AMD_APU_IS_PICASSO)
				return "picasso_vcn";
			return "raven_vcn";
		case IP_VERSION(2, 5, 0):
			return "arcturus_vcn";
		case IP_VERSION(2, 2, 0):
			if (adev->apu_flags & AMD_APU_IS_RENOIR)
				return "renoir_vcn";
			return "green_sardine_vcn";
		case IP_VERSION(2, 6, 0):
			return "aldebaran_vcn";
		case IP_VERSION(2, 0, 0):
			return "navi10_vcn";
		case IP_VERSION(2, 0, 2):
			if (adev->asic_type == CHIP_NAVI12)
				return "navi12_vcn";
			return "navi14_vcn";
		case IP_VERSION(3, 0, 0):
		case IP_VERSION(3, 0, 64):
		case IP_VERSION(3, 0, 192):
			if (amdgpu_ip_version(adev, GC_HWIP, 0) ==
			    IP_VERSION(10, 3, 0))
				return "sienna_cichlid_vcn";
			return "navy_flounder_vcn";
		case IP_VERSION(3, 0, 2):
			return "vangogh_vcn";
		case IP_VERSION(3, 0, 16):
			return "dimgrey_cavefish_vcn";
		case IP_VERSION(3, 0, 33):
			return "beige_goby_vcn";
		case IP_VERSION(3, 1, 1):
			return "yellow_carp_vcn";
		}
	} else if (block_type == GC_HWIP) {
		switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
		case IP_VERSION(9, 0, 1):
			return "vega10";
		case IP_VERSION(9, 2, 1):
			return "vega12";
		case IP_VERSION(9, 4, 0):
			return "vega20";
		case IP_VERSION(9, 2, 2):
		case IP_VERSION(9, 1, 0):
			if (adev->apu_flags & AMD_APU_IS_RAVEN2)
				return "raven2";
			else if (adev->apu_flags & AMD_APU_IS_PICASSO)
				return "picasso";
			return "raven";
		case IP_VERSION(9, 4, 1):
			return "arcturus";
		case IP_VERSION(9, 3, 0):
			if (adev->apu_flags & AMD_APU_IS_RENOIR)
				return "renoir";
			return "green_sardine";
		case IP_VERSION(9, 4, 2):
			return "aldebaran";
		case IP_VERSION(10, 1, 10):
			return "navi10";
		case IP_VERSION(10, 1, 1):
			return "navi14";
		case IP_VERSION(10, 1, 2):
			return "navi12";
		case IP_VERSION(10, 3, 0):
			return "sienna_cichlid";
		case IP_VERSION(10, 3, 2):
			return "navy_flounder";
		case IP_VERSION(10, 3, 1):
			return "vangogh";
		case IP_VERSION(10, 3, 4):
			return "dimgrey_cavefish";
		case IP_VERSION(10, 3, 5):
			return "beige_goby";
		case IP_VERSION(10, 3, 3):
			return "yellow_carp";
		case IP_VERSION(10, 1, 3):
		case IP_VERSION(10, 1, 4):
			return "cyan_skillfish2";
		}
	}
	return NULL;
}

bool amdgpu_is_kicker_fw(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kicker_device_list); i++) {
		if (adev->pdev->device == kicker_device_list[i].device &&
			adev->pdev->revision == kicker_device_list[i].revision)
		return true;
	}

	return false;
}

void amdgpu_ucode_ip_version_decode(struct amdgpu_device *adev, int block_type, char *ucode_prefix, int len)
{
	int maj, min, rev;
	char *ip_name;
	const char *legacy;
	uint32_t version = amdgpu_ip_version(adev, block_type, 0);

	legacy = amdgpu_ucode_legacy_naming(adev, block_type);
	if (legacy) {
		snprintf(ucode_prefix, len, "%s", legacy);
		return;
	}

	switch (block_type) {
	case GC_HWIP:
		ip_name = "gc";
		break;
	case SDMA0_HWIP:
		ip_name = "sdma";
		break;
	case MP0_HWIP:
		ip_name = "psp";
		break;
	case MP1_HWIP:
		ip_name = "smu";
		break;
	case UVD_HWIP:
		ip_name = "vcn";
		break;
	case VPE_HWIP:
		ip_name = "vpe";
		break;
	case ISP_HWIP:
		ip_name = "isp";
		break;
	default:
		BUG();
	}

	maj = IP_VERSION_MAJ(version);
	min = IP_VERSION_MIN(version);
	rev = IP_VERSION_REV(version);

	snprintf(ucode_prefix, len, "%s_%d_%d_%d", ip_name, maj, min, rev);
}

/*
 * amdgpu_ucode_request - Fetch and validate amdgpu microcode
 *
 * @adev: amdgpu device
 * @fw: pointer to load firmware to
 * @fmt: firmware name format string
 * @...: variable arguments
 *
 * This is a helper that will use request_firmware and amdgpu_ucode_validate
 * to load and run basic validation on firmware. If the load fails, remap
 * the error code to -ENODEV, so that early_init functions will fail to load.
 */
int amdgpu_ucode_request(struct amdgpu_device *adev, const struct firmware **fw,
			 const char *fmt, ...)
{
	char fname[AMDGPU_UCODE_NAME_MAX];
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vsnprintf(fname, sizeof(fname), fmt, ap);
	va_end(ap);
	if (r == sizeof(fname)) {
		dev_warn(adev->dev, "amdgpu firmware name buffer overflow\n");
		return -EOVERFLOW;
	}

	r = request_firmware(fw, fname, adev->dev);
	if (r)
		return -ENODEV;

	r = amdgpu_ucode_validate(*fw);
	if (r) {
		dev_dbg(adev->dev, "\"%s\" failed to validate\n", fname);
		release_firmware(*fw);
		*fw = NULL;
	}

	return r;
}

/*
 * amdgpu_ucode_release - Release firmware microcode
 *
 * @fw: pointer to firmware to release
 */
void amdgpu_ucode_release(const struct firmware **fw)
{
	release_firmware(*fw);
	*fw = NULL;
}
