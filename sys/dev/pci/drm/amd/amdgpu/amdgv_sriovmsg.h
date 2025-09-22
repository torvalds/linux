/*
 * Copyright (c) 2018-2021 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef AMDGV_SRIOV_MSG__H_
#define AMDGV_SRIOV_MSG__H_

/* unit in kilobytes */
#define AMD_SRIOV_MSG_VBIOS_OFFSET	     0
#define AMD_SRIOV_MSG_VBIOS_SIZE_KB	     64
#define AMD_SRIOV_MSG_DATAEXCHANGE_OFFSET_KB AMD_SRIOV_MSG_VBIOS_SIZE_KB
#define AMD_SRIOV_MSG_DATAEXCHANGE_SIZE_KB   4

/*
 * layout
 * 0           64KB        65KB        66KB
 * |   VBIOS   |   PF2VF   |   VF2PF   |   Bad Page   | ...
 * |   64KB    |   1KB     |   1KB     |
 */
#define AMD_SRIOV_MSG_SIZE_KB                   1
#define AMD_SRIOV_MSG_PF2VF_OFFSET_KB           AMD_SRIOV_MSG_DATAEXCHANGE_OFFSET_KB
#define AMD_SRIOV_MSG_VF2PF_OFFSET_KB           (AMD_SRIOV_MSG_PF2VF_OFFSET_KB + AMD_SRIOV_MSG_SIZE_KB)
#define AMD_SRIOV_MSG_BAD_PAGE_OFFSET_KB        (AMD_SRIOV_MSG_VF2PF_OFFSET_KB + AMD_SRIOV_MSG_SIZE_KB)

/*
 * PF2VF history log:
 * v1 defined in amdgim
 * v2 current
 *
 * VF2PF history log:
 * v1 defined in amdgim
 * v2 defined in amdgim
 * v3 current
 */
#define AMD_SRIOV_MSG_FW_VRAM_PF2VF_VER 2
#define AMD_SRIOV_MSG_FW_VRAM_VF2PF_VER 3

#define AMD_SRIOV_MSG_RESERVE_UCODE 24

#define AMD_SRIOV_MSG_RESERVE_VCN_INST 4

enum amd_sriov_ucode_engine_id {
	AMD_SRIOV_UCODE_ID_VCE = 0,
	AMD_SRIOV_UCODE_ID_UVD,
	AMD_SRIOV_UCODE_ID_MC,
	AMD_SRIOV_UCODE_ID_ME,
	AMD_SRIOV_UCODE_ID_PFP,
	AMD_SRIOV_UCODE_ID_CE,
	AMD_SRIOV_UCODE_ID_RLC,
	AMD_SRIOV_UCODE_ID_RLC_SRLC,
	AMD_SRIOV_UCODE_ID_RLC_SRLG,
	AMD_SRIOV_UCODE_ID_RLC_SRLS,
	AMD_SRIOV_UCODE_ID_MEC,
	AMD_SRIOV_UCODE_ID_MEC2,
	AMD_SRIOV_UCODE_ID_SOS,
	AMD_SRIOV_UCODE_ID_ASD,
	AMD_SRIOV_UCODE_ID_TA_RAS,
	AMD_SRIOV_UCODE_ID_TA_XGMI,
	AMD_SRIOV_UCODE_ID_SMC,
	AMD_SRIOV_UCODE_ID_SDMA,
	AMD_SRIOV_UCODE_ID_SDMA2,
	AMD_SRIOV_UCODE_ID_VCN,
	AMD_SRIOV_UCODE_ID_DMCU,
	AMD_SRIOV_UCODE_ID__MAX
};

#pragma pack(push, 1) // PF2VF / VF2PF data areas are byte packed

union amd_sriov_msg_feature_flags {
	struct {
		uint32_t error_log_collect : 1;
		uint32_t host_load_ucodes  : 1;
		uint32_t host_flr_vramlost : 1;
		uint32_t mm_bw_management  : 1;
		uint32_t pp_one_vf_mode    : 1;
		uint32_t reg_indirect_acc  : 1;
		uint32_t av1_support       : 1;
		uint32_t vcn_rb_decouple   : 1;
		uint32_t mes_info_enable   : 1;
		uint32_t reserved          : 23;
	} flags;
	uint32_t all;
};

union amd_sriov_reg_access_flags {
	struct {
		uint32_t vf_reg_access_ih 	 : 1;
		uint32_t vf_reg_access_mmhub : 1;
		uint32_t vf_reg_access_gc 	 : 1;
		uint32_t reserved	         : 29;
	} flags;
	uint32_t all;
};

union amd_sriov_msg_os_info {
	struct {
		uint32_t windows  : 1;
		uint32_t reserved : 31;
	} info;
	uint32_t all;
};

struct amd_sriov_msg_uuid_info {
	union {
		struct {
			uint32_t did	: 16;
			uint32_t fcn	: 8;
			uint32_t asic_7 : 8;
		};
		uint32_t time_low;
	};

	struct {
		uint32_t time_mid  : 16;
		uint32_t time_high : 12;
		uint32_t version   : 4;
	};

	struct {
		struct {
			uint8_t clk_seq_hi : 6;
			uint8_t variant    : 2;
		};
		union {
			uint8_t clk_seq_low;
			uint8_t asic_6;
		};
		uint16_t asic_4;
	};

	uint32_t asic_0;
};

struct amd_sriov_msg_pf2vf_info_header {
	/* the total structure size in byte */
	uint32_t size;
	/* version of this structure, written by the HOST */
	uint32_t version;
	/* reserved */
	uint32_t reserved[2];
};

#define AMD_SRIOV_MSG_PF2VF_INFO_FILLED_SIZE (49)
struct amd_sriov_msg_pf2vf_info {
	/* header contains size and version */
	struct amd_sriov_msg_pf2vf_info_header header;
	/* use private key from mailbox 2 to create checksum */
	uint32_t checksum;
	/* The features flags of the HOST driver supports */
	union amd_sriov_msg_feature_flags feature_flags;
	/* (max_width * max_height * fps) / (16 * 16) */
	uint32_t hevc_enc_max_mb_per_second;
	/* (max_width * max_height) / (16 * 16) */
	uint32_t hevc_enc_max_mb_per_frame;
	/* (max_width * max_height * fps) / (16 * 16) */
	uint32_t avc_enc_max_mb_per_second;
	/* (max_width * max_height) / (16 * 16) */
	uint32_t avc_enc_max_mb_per_frame;
	/* MEC FW position in BYTE from the start of VF visible frame buffer */
	uint64_t mecfw_offset;
	/* MEC FW size in BYTE */
	uint32_t mecfw_size;
	/* UVD FW position in BYTE from the start of VF visible frame buffer */
	uint64_t uvdfw_offset;
	/* UVD FW size in BYTE */
	uint32_t uvdfw_size;
	/* VCE FW position in BYTE from the start of VF visible frame buffer */
	uint64_t vcefw_offset;
	/* VCE FW size in BYTE */
	uint32_t vcefw_size;
	/* Bad pages block position in BYTE */
	uint32_t bp_block_offset_low;
	uint32_t bp_block_offset_high;
	/* Bad pages block size in BYTE */
	uint32_t bp_block_size;
	/* frequency for VF to update the VF2PF area in msec, 0 = manual */
	uint32_t vf2pf_update_interval_ms;
	/* identification in ROCm SMI */
	uint64_t uuid;
	uint32_t fcn_idx;
	/* flags to indicate which register access method VF should use */
	union amd_sriov_reg_access_flags reg_access_flags;
	/* MM BW management */
	struct {
		uint32_t decode_max_dimension_pixels;
		uint32_t decode_max_frame_pixels;
		uint32_t encode_max_dimension_pixels;
		uint32_t encode_max_frame_pixels;
	} mm_bw_management[AMD_SRIOV_MSG_RESERVE_VCN_INST];
	/* UUID info */
	struct amd_sriov_msg_uuid_info uuid_info;
	/* PCIE atomic ops support flag */
	uint32_t pcie_atomic_ops_support_flags;
	/* Portion of GPU memory occupied by VF.  MAX value is 65535, but set to uint32_t to maintain alignment with reserved size */
	uint32_t gpu_capacity;
	/* reserved */
	uint32_t reserved[256 - AMD_SRIOV_MSG_PF2VF_INFO_FILLED_SIZE];
} __packed;

struct amd_sriov_msg_vf2pf_info_header {
	/* the total structure size in byte */
	uint32_t size;
	/* version of this structure, written by the guest */
	uint32_t version;
	/* reserved */
	uint32_t reserved[2];
};

#define AMD_SRIOV_MSG_VF2PF_INFO_FILLED_SIZE (73)
struct amd_sriov_msg_vf2pf_info {
	/* header contains size and version */
	struct amd_sriov_msg_vf2pf_info_header header;
	uint32_t checksum;
	/* driver version */
	uint8_t driver_version[64];
	/* driver certification, 1=WHQL, 0=None */
	uint32_t driver_cert;
	/* guest OS type and version */
	union amd_sriov_msg_os_info os_info;
	/* guest fb information in the unit of MB */
	uint32_t fb_usage;
	/* guest gfx engine usage percentage */
	uint32_t gfx_usage;
	/* guest gfx engine health percentage */
	uint32_t gfx_health;
	/* guest compute engine usage percentage */
	uint32_t compute_usage;
	/* guest compute engine health percentage */
	uint32_t compute_health;
	/* guest avc engine usage percentage. 0xffff means N/A */
	uint32_t avc_enc_usage;
	/* guest avc engine health percentage. 0xffff means N/A */
	uint32_t avc_enc_health;
	/* guest hevc engine usage percentage. 0xffff means N/A */
	uint32_t hevc_enc_usage;
	/* guest hevc engine usage percentage. 0xffff means N/A */
	uint32_t hevc_enc_health;
	/* combined encode/decode usage */
	uint32_t encode_usage;
	uint32_t decode_usage;
	/* Version of PF2VF that VF understands */
	uint32_t pf2vf_version_required;
	/* additional FB usage */
	uint32_t fb_vis_usage;
	uint32_t fb_vis_size;
	uint32_t fb_size;
	/* guest ucode data, each one is 1.25 Dword */
	struct {
		uint8_t id;
		uint32_t version;
	} ucode_info[AMD_SRIOV_MSG_RESERVE_UCODE];
	uint64_t dummy_page_addr;
	/* FB allocated for guest MES to record UQ info */
	uint64_t mes_info_addr;
	uint32_t mes_info_size;
	/* reserved */
	uint32_t reserved[256 - AMD_SRIOV_MSG_VF2PF_INFO_FILLED_SIZE];
} __packed;

/* mailbox message send from guest to host  */
enum amd_sriov_mailbox_request_message {
	MB_REQ_MSG_REQ_GPU_INIT_ACCESS = 1,
	MB_REQ_MSG_REL_GPU_INIT_ACCESS,
	MB_REQ_MSG_REQ_GPU_FINI_ACCESS,
	MB_REQ_MSG_REL_GPU_FINI_ACCESS,
	MB_REQ_MSG_REQ_GPU_RESET_ACCESS,
	MB_REQ_MSG_REQ_GPU_INIT_DATA,

	MB_REQ_MSG_LOG_VF_ERROR = 200,
};

/* mailbox message send from host to guest  */
enum amd_sriov_mailbox_response_message {
	MB_RES_MSG_CLR_MSG_BUF = 0,
	MB_RES_MSG_READY_TO_ACCESS_GPU = 1,
	MB_RES_MSG_FLR_NOTIFICATION,
	MB_RES_MSG_FLR_NOTIFICATION_COMPLETION,
	MB_RES_MSG_SUCCESS,
	MB_RES_MSG_FAIL,
	MB_RES_MSG_QUERY_ALIVE,
	MB_RES_MSG_GPU_INIT_DATA_READY,

	MB_RES_MSG_TEXT_MESSAGE = 255
};

/* version data stored in MAILBOX_MSGBUF_RCV_DW1 for future expansion */
enum amd_sriov_gpu_init_data_version {
	GPU_INIT_DATA_READY_V1 = 1,
};

#pragma pack(pop) // Restore previous packing option

/* checksum function between host and guest */
unsigned int amd_sriov_msg_checksum(void *obj, unsigned long obj_size, unsigned int key,
				    unsigned int checksum);

/* assertion at compile time */
#ifdef __linux__
#define stringification(s)  _stringification(s)
#define _stringification(s) #s

_Static_assert(
	sizeof(struct amd_sriov_msg_vf2pf_info) == AMD_SRIOV_MSG_SIZE_KB << 10,
	"amd_sriov_msg_vf2pf_info must be " stringification(AMD_SRIOV_MSG_SIZE_KB) " KB");

_Static_assert(
	sizeof(struct amd_sriov_msg_pf2vf_info) == AMD_SRIOV_MSG_SIZE_KB << 10,
	"amd_sriov_msg_pf2vf_info must be " stringification(AMD_SRIOV_MSG_SIZE_KB) " KB");

_Static_assert(AMD_SRIOV_MSG_RESERVE_UCODE % 4 == 0,
	       "AMD_SRIOV_MSG_RESERVE_UCODE must be multiple of 4");

_Static_assert(AMD_SRIOV_MSG_RESERVE_UCODE > AMD_SRIOV_UCODE_ID__MAX,
	       "AMD_SRIOV_MSG_RESERVE_UCODE must be bigger than AMD_SRIOV_UCODE_ID__MAX");

#undef _stringification
#undef stringification
#endif

#endif /* AMDGV_SRIOV_MSG__H_ */
