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

#ifndef __AMDGPU_RLC_H__
#define __AMDGPU_RLC_H__

#include "clearstate_defs.h"

#define AMDGPU_MAX_RLC_INSTANCES	8

/* firmware ID used in rlc toc */
typedef enum _FIRMWARE_ID_ {
	FIRMWARE_ID_INVALID					= 0,
	FIRMWARE_ID_RLC_G_UCODE					= 1,
	FIRMWARE_ID_RLC_TOC					= 2,
	FIRMWARE_ID_RLCG_SCRATCH                                = 3,
	FIRMWARE_ID_RLC_SRM_ARAM                                = 4,
	FIRMWARE_ID_RLC_SRM_INDEX_ADDR                          = 5,
	FIRMWARE_ID_RLC_SRM_INDEX_DATA                          = 6,
	FIRMWARE_ID_RLC_P_UCODE                                 = 7,
	FIRMWARE_ID_RLC_V_UCODE                                 = 8,
	FIRMWARE_ID_RLX6_UCODE                                  = 9,
	FIRMWARE_ID_RLX6_DRAM_BOOT                              = 10,
	FIRMWARE_ID_GLOBAL_TAP_DELAYS                           = 11,
	FIRMWARE_ID_SE0_TAP_DELAYS                              = 12,
	FIRMWARE_ID_SE1_TAP_DELAYS                              = 13,
	FIRMWARE_ID_GLOBAL_SE0_SE1_SKEW_DELAYS                  = 14,
	FIRMWARE_ID_SDMA0_UCODE                                 = 15,
	FIRMWARE_ID_SDMA0_JT                                    = 16,
	FIRMWARE_ID_SDMA1_UCODE                                 = 17,
	FIRMWARE_ID_SDMA1_JT                                    = 18,
	FIRMWARE_ID_CP_CE                                       = 19,
	FIRMWARE_ID_CP_PFP                                      = 20,
	FIRMWARE_ID_CP_ME                                       = 21,
	FIRMWARE_ID_CP_MEC                                      = 22,
	FIRMWARE_ID_CP_MES                                      = 23,
	FIRMWARE_ID_MES_STACK                                   = 24,
	FIRMWARE_ID_RLC_SRM_DRAM_SR                             = 25,
	FIRMWARE_ID_RLCG_SCRATCH_SR                             = 26,
	FIRMWARE_ID_RLCP_SCRATCH_SR                             = 27,
	FIRMWARE_ID_RLCV_SCRATCH_SR                             = 28,
	FIRMWARE_ID_RLX6_DRAM_SR                                = 29,
	FIRMWARE_ID_SDMA0_PG_CONTEXT                            = 30,
	FIRMWARE_ID_SDMA1_PG_CONTEXT                            = 31,
	FIRMWARE_ID_GLOBAL_MUX_SELECT_RAM                       = 32,
	FIRMWARE_ID_SE0_MUX_SELECT_RAM                          = 33,
	FIRMWARE_ID_SE1_MUX_SELECT_RAM                          = 34,
	FIRMWARE_ID_ACCUM_CTRL_RAM                              = 35,
	FIRMWARE_ID_RLCP_CAM                                    = 36,
	FIRMWARE_ID_RLC_SPP_CAM_EXT                             = 37,
	FIRMWARE_ID_MAX                                         = 38,
} FIRMWARE_ID;

typedef enum _SOC21_FIRMWARE_ID_ {
    SOC21_FIRMWARE_ID_INVALID                     = 0,
    SOC21_FIRMWARE_ID_RLC_G_UCODE                 = 1,
    SOC21_FIRMWARE_ID_RLC_TOC                     = 2,
    SOC21_FIRMWARE_ID_RLCG_SCRATCH                = 3,
    SOC21_FIRMWARE_ID_RLC_SRM_ARAM                = 4,
    SOC21_FIRMWARE_ID_RLC_P_UCODE                 = 5,
    SOC21_FIRMWARE_ID_RLC_V_UCODE                 = 6,
    SOC21_FIRMWARE_ID_RLX6_UCODE                  = 7,
    SOC21_FIRMWARE_ID_RLX6_UCODE_CORE1            = 8,
    SOC21_FIRMWARE_ID_RLX6_DRAM_BOOT              = 9,
    SOC21_FIRMWARE_ID_RLX6_DRAM_BOOT_CORE1        = 10,
    SOC21_FIRMWARE_ID_SDMA_UCODE_TH0              = 11,
    SOC21_FIRMWARE_ID_SDMA_UCODE_TH1              = 12,
    SOC21_FIRMWARE_ID_CP_PFP                      = 13,
    SOC21_FIRMWARE_ID_CP_ME                       = 14,
    SOC21_FIRMWARE_ID_CP_MEC                      = 15,
    SOC21_FIRMWARE_ID_RS64_MES_P0                 = 16,
    SOC21_FIRMWARE_ID_RS64_MES_P1                 = 17,
    SOC21_FIRMWARE_ID_RS64_PFP                    = 18,
    SOC21_FIRMWARE_ID_RS64_ME                     = 19,
    SOC21_FIRMWARE_ID_RS64_MEC                    = 20,
    SOC21_FIRMWARE_ID_RS64_MES_P0_STACK           = 21,
    SOC21_FIRMWARE_ID_RS64_MES_P1_STACK           = 22,
    SOC21_FIRMWARE_ID_RS64_PFP_P0_STACK           = 23,
    SOC21_FIRMWARE_ID_RS64_PFP_P1_STACK           = 24,
    SOC21_FIRMWARE_ID_RS64_ME_P0_STACK            = 25,
    SOC21_FIRMWARE_ID_RS64_ME_P1_STACK            = 26,
    SOC21_FIRMWARE_ID_RS64_MEC_P0_STACK           = 27,
    SOC21_FIRMWARE_ID_RS64_MEC_P1_STACK           = 28,
    SOC21_FIRMWARE_ID_RS64_MEC_P2_STACK           = 29,
    SOC21_FIRMWARE_ID_RS64_MEC_P3_STACK           = 30,
    SOC21_FIRMWARE_ID_RLC_SRM_DRAM_SR             = 31,
    SOC21_FIRMWARE_ID_RLCG_SCRATCH_SR             = 32,
    SOC21_FIRMWARE_ID_RLCP_SCRATCH_SR             = 33,
    SOC21_FIRMWARE_ID_RLCV_SCRATCH_SR             = 34,
    SOC21_FIRMWARE_ID_RLX6_DRAM_SR                = 35,
    SOC21_FIRMWARE_ID_RLX6_DRAM_SR_CORE1          = 36,
    SOC21_FIRMWARE_ID_MAX                         = 37
} SOC21_FIRMWARE_ID;

typedef enum _SOC24_FIRMWARE_ID_ {
    SOC24_FIRMWARE_ID_INVALID                     = 0,
    SOC24_FIRMWARE_ID_RLC_G_UCODE                 = 1,
    SOC24_FIRMWARE_ID_RLC_TOC                     = 2,
    SOC24_FIRMWARE_ID_RLCG_SCRATCH                = 3,
    SOC24_FIRMWARE_ID_RLC_SRM_ARAM                = 4,
    SOC24_FIRMWARE_ID_RLC_P_UCODE                 = 5,
    SOC24_FIRMWARE_ID_RLC_V_UCODE                 = 6,
    SOC24_FIRMWARE_ID_RLX6_UCODE                  = 7,
    SOC24_FIRMWARE_ID_RLX6_UCODE_CORE1            = 8,
    SOC24_FIRMWARE_ID_RLX6_DRAM_BOOT              = 9,
    SOC24_FIRMWARE_ID_RLX6_DRAM_BOOT_CORE1        = 10,
    SOC24_FIRMWARE_ID_SDMA_UCODE_TH0              = 11,
    SOC24_FIRMWARE_ID_SDMA_UCODE_TH1              = 12,
    SOC24_FIRMWARE_ID_CP_PFP                      = 13,
    SOC24_FIRMWARE_ID_CP_ME                       = 14,
    SOC24_FIRMWARE_ID_CP_MEC                      = 15,
    SOC24_FIRMWARE_ID_RS64_MES_P0                 = 16,
    SOC24_FIRMWARE_ID_RS64_MES_P1                 = 17,
    SOC24_FIRMWARE_ID_RS64_PFP                    = 18,
    SOC24_FIRMWARE_ID_RS64_ME                     = 19,
    SOC24_FIRMWARE_ID_RS64_MEC                    = 20,
    SOC24_FIRMWARE_ID_RS64_MES_P0_STACK           = 21,
    SOC24_FIRMWARE_ID_RS64_MES_P1_STACK           = 22,
    SOC24_FIRMWARE_ID_RS64_PFP_P0_STACK           = 23,
    SOC24_FIRMWARE_ID_RS64_PFP_P1_STACK           = 24,
    SOC24_FIRMWARE_ID_RS64_ME_P0_STACK            = 25,
    SOC24_FIRMWARE_ID_RS64_ME_P1_STACK            = 26,
    SOC24_FIRMWARE_ID_RS64_MEC_P0_STACK           = 27,
    SOC24_FIRMWARE_ID_RS64_MEC_P1_STACK           = 28,
    SOC24_FIRMWARE_ID_RS64_MEC_P2_STACK           = 29,
    SOC24_FIRMWARE_ID_RS64_MEC_P3_STACK           = 30,
    SOC24_FIRMWARE_ID_RLC_SRM_DRAM_SR             = 31,
    SOC24_FIRMWARE_ID_RLCG_SCRATCH_SR             = 32,
    SOC24_FIRMWARE_ID_RLCP_SCRATCH_SR             = 33,
    SOC24_FIRMWARE_ID_RLCV_SCRATCH_SR             = 34,
    SOC24_FIRMWARE_ID_RLX6_DRAM_SR                = 35,
    SOC24_FIRMWARE_ID_RLX6_DRAM_SR_CORE1          = 36,
    SOC24_FIRMWARE_ID_RLCDEBUGLOG                 = 37,
    SOC24_FIRMWARE_ID_SRIOV_DEBUG                 = 38,
    SOC24_FIRMWARE_ID_SRIOV_CSA_RLC               = 39,
    SOC24_FIRMWARE_ID_SRIOV_CSA_SDMA              = 40,
    SOC24_FIRMWARE_ID_SRIOV_CSA_CP                = 41,
    SOC24_FIRMWARE_ID_UMF_ZONE_PAD                = 42,
    SOC24_FIRMWARE_ID_MAX                         = 43
} SOC24_FIRMWARE_ID;

typedef struct _RLC_TABLE_OF_CONTENT {
	union {
		unsigned int	DW0;
		struct {
			unsigned int	offset		: 25;
			unsigned int	id		: 7;
		};
	};

	union {
		unsigned int	DW1;
		struct {
			unsigned int	load_at_boot		: 1;
			unsigned int	load_at_vddgfx		: 1;
			unsigned int	load_at_reset		: 1;
			unsigned int	memory_destination	: 2;
			unsigned int	vfflr_image_code	: 4;
			unsigned int	load_mode_direct	: 1;
			unsigned int	save_for_vddgfx		: 1;
			unsigned int	save_for_vfflr		: 1;
			unsigned int	reserved		: 1;
			unsigned int	signed_source		: 1;
			unsigned int	size			: 18;
		};
	};

	union {
		unsigned int	DW2;
		struct {
			unsigned int	indirect_addr_reg	: 16;
			unsigned int	index			: 16;
		};
	};

	union {
		unsigned int	DW3;
		struct {
			unsigned int	indirect_data_reg	: 16;
			unsigned int	indirect_start_offset	: 16;
		};
	};
} RLC_TABLE_OF_CONTENT;

typedef struct _RLC_TABLE_OF_CONTENT_V2 {
	union {
		unsigned int    DW0;
		struct {
			uint32_t offset         : 25;
			uint32_t id             : 7;
		};
	};

	union {
		unsigned int    DW1;
		struct {
			uint32_t reserved0              : 1;
			uint32_t reserved1              : 1;
			uint32_t reserved2              : 1;
			uint32_t memory_destination     : 2;
			uint32_t vfflr_image_code       : 4;
			uint32_t reserved9              : 1;
			uint32_t reserved10             : 1;
			uint32_t reserved11             : 1;
			uint32_t size_x16               : 1;
			uint32_t reserved13             : 1;
			uint32_t size                   : 18;
		};
	};
} RLC_TABLE_OF_CONTENT_V2;

#define RLC_TOC_MAX_SIZE		64

struct amdgpu_rlc_funcs {
	bool (*is_rlc_enabled)(struct amdgpu_device *adev);
	void (*set_safe_mode)(struct amdgpu_device *adev, int xcc_id);
	void (*unset_safe_mode)(struct amdgpu_device *adev, int xcc_id);
	int  (*init)(struct amdgpu_device *adev);
	u32  (*get_csb_size)(struct amdgpu_device *adev);
	void (*get_csb_buffer)(struct amdgpu_device *adev, volatile u32 *buffer);
	int  (*get_cp_table_num)(struct amdgpu_device *adev);
	int  (*resume)(struct amdgpu_device *adev);
	void (*stop)(struct amdgpu_device *adev);
	void (*reset)(struct amdgpu_device *adev);
	void (*start)(struct amdgpu_device *adev);
	void (*update_spm_vmid)(struct amdgpu_device *adev, struct amdgpu_ring *ring, unsigned vmid);
	bool (*is_rlcg_access_range)(struct amdgpu_device *adev, uint32_t reg);
};

struct amdgpu_rlcg_reg_access_ctrl {
	uint32_t scratch_reg0;
	uint32_t scratch_reg1;
	uint32_t scratch_reg2;
	uint32_t scratch_reg3;
	uint32_t grbm_cntl;
	uint32_t grbm_idx;
	uint32_t spare_int;
};

struct amdgpu_rlc {
	/* for power gating */
	struct amdgpu_bo        *save_restore_obj;
	uint64_t                save_restore_gpu_addr;
	volatile uint32_t       *sr_ptr;
	const u32               *reg_list;
	u32                     reg_list_size;
	/* for clear state */
	struct amdgpu_bo        *clear_state_obj;
	uint64_t                clear_state_gpu_addr;
	volatile uint32_t       *cs_ptr;
	const struct cs_section_def   *cs_data;
	u32                     clear_state_size;
	/* for cp tables */
	struct amdgpu_bo        *cp_table_obj;
	uint64_t                cp_table_gpu_addr;
	volatile uint32_t       *cp_table_ptr;
	u32                     cp_table_size;

	/* safe mode for updating CG/PG state */
	bool in_safe_mode[AMDGPU_MAX_RLC_INSTANCES];
	const struct amdgpu_rlc_funcs *funcs;

	/* for firmware data */
	u32 save_and_restore_offset;
	u32 clear_state_descriptor_offset;
	u32 avail_scratch_ram_locations;
	u32 reg_restore_list_size;
	u32 reg_list_format_start;
	u32 reg_list_format_separate_start;
	u32 starting_offsets_start;
	u32 reg_list_format_size_bytes;
	u32 reg_list_size_bytes;
	u32 reg_list_format_direct_reg_list_length;
	u32 save_restore_list_cntl_size_bytes;
	u32 save_restore_list_gpm_size_bytes;
	u32 save_restore_list_srm_size_bytes;
	u32 rlc_iram_ucode_size_bytes;
	u32 rlc_dram_ucode_size_bytes;
	u32 rlcp_ucode_size_bytes;
	u32 rlcv_ucode_size_bytes;
	u32 global_tap_delays_ucode_size_bytes;
	u32 se0_tap_delays_ucode_size_bytes;
	u32 se1_tap_delays_ucode_size_bytes;
	u32 se2_tap_delays_ucode_size_bytes;
	u32 se3_tap_delays_ucode_size_bytes;

	u32 *register_list_format;
	u32 *register_restore;
	u8 *save_restore_list_cntl;
	u8 *save_restore_list_gpm;
	u8 *save_restore_list_srm;
	u8 *rlc_iram_ucode;
	u8 *rlc_dram_ucode;
	u8 *rlcp_ucode;
	u8 *rlcv_ucode;
	u8 *global_tap_delays_ucode;
	u8 *se0_tap_delays_ucode;
	u8 *se1_tap_delays_ucode;
	u8 *se2_tap_delays_ucode;
	u8 *se3_tap_delays_ucode;

	bool is_rlc_v2_1;

	/* for rlc autoload */
	struct amdgpu_bo	*rlc_autoload_bo;
	u64			rlc_autoload_gpu_addr;
	void			*rlc_autoload_ptr;

	/* rlc toc buffer */
	struct amdgpu_bo	*rlc_toc_bo;
	uint64_t		rlc_toc_gpu_addr;
	void			*rlc_toc_buf;

	bool rlcg_reg_access_supported;
	/* registers for rlcg indirect reg access */
	struct amdgpu_rlcg_reg_access_ctrl reg_access_ctrl[AMDGPU_MAX_RLC_INSTANCES];
};

void amdgpu_gfx_rlc_enter_safe_mode(struct amdgpu_device *adev, int xcc_id);
void amdgpu_gfx_rlc_exit_safe_mode(struct amdgpu_device *adev, int xcc_id);
int amdgpu_gfx_rlc_init_sr(struct amdgpu_device *adev, u32 dws);
int amdgpu_gfx_rlc_init_csb(struct amdgpu_device *adev);
int amdgpu_gfx_rlc_init_cpt(struct amdgpu_device *adev);
void amdgpu_gfx_rlc_setup_cp_table(struct amdgpu_device *adev);
void amdgpu_gfx_rlc_fini(struct amdgpu_device *adev);
int amdgpu_gfx_rlc_init_microcode(struct amdgpu_device *adev,
				  uint16_t version_major,
				  uint16_t version_minor);
#endif
