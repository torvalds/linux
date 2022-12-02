/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#ifndef __SOUND_SOC_INTEL_AVS_MSGS_H
#define __SOUND_SOC_INTEL_AVS_MSGS_H

struct avs_dev;

#define AVS_MAILBOX_SIZE 4096

enum avs_msg_target {
	AVS_FW_GEN_MSG = 0,
	AVS_MOD_MSG = 1
};

enum avs_msg_direction {
	AVS_MSG_REQUEST = 0,
	AVS_MSG_REPLY = 1
};

enum avs_global_msg_type {
	AVS_GLB_ROM_CONTROL = 1,
	AVS_GLB_LOAD_MULTIPLE_MODULES = 15,
	AVS_GLB_UNLOAD_MULTIPLE_MODULES = 16,
	AVS_GLB_CREATE_PIPELINE = 17,
	AVS_GLB_DELETE_PIPELINE = 18,
	AVS_GLB_SET_PIPELINE_STATE = 19,
	AVS_GLB_GET_PIPELINE_STATE = 20,
	AVS_GLB_LOAD_LIBRARY = 24,
	AVS_GLB_NOTIFICATION = 27,
};

union avs_global_msg {
	u64 val;
	struct {
		union {
			u32 primary;
			struct {
				u32 rsvd:24;
				u32 global_msg_type:5;
				u32 msg_direction:1;
				u32 msg_target:1;
			};
			/* set boot config */
			struct {
				u32 rom_ctrl_msg_type:9;
				u32 dma_id:5;
				u32 purge_request:1;
			} boot_cfg;
			/* module loading */
			struct {
				u32 mod_cnt:8;
			} load_multi_mods;
			/* pipeline management */
			struct {
				u32 ppl_mem_size:11;
				u32 ppl_priority:5;
				u32 instance_id:8;
			} create_ppl;
			struct {
				u32 rsvd:16;
				u32 instance_id:8;
			} ppl; /* generic ppl request */
			struct {
				u32 state:16;
				u32 ppl_id:8;
			} set_ppl_state;
			struct {
				u32 ppl_id:8;
			} get_ppl_state;
			/* library loading */
			struct {
				u32 dma_id:5;
				u32 rsvd:11;
				u32 lib_id:4;
			} load_lib;
		};
		union {
			u32 val;
			/* pipeline management */
			struct {
				u32 lp:1; /* low power flag */
				u32 rsvd:3;
				u32 attributes:16; /* additional scheduling flags */
			} create_ppl;
		} ext;
	};
} __packed;

struct avs_tlv {
	u32 type;
	u32 length;
	u32 value[];
} __packed;

enum avs_module_msg_type {
	AVS_MOD_INIT_INSTANCE = 0,
	AVS_MOD_LARGE_CONFIG_GET = 3,
	AVS_MOD_LARGE_CONFIG_SET = 4,
	AVS_MOD_BIND = 5,
	AVS_MOD_UNBIND = 6,
	AVS_MOD_SET_DX = 7,
	AVS_MOD_SET_D0IX = 8,
	AVS_MOD_DELETE_INSTANCE = 11,
};

union avs_module_msg {
	u64 val;
	struct {
		union {
			u32 primary;
			struct {
				u32 module_id:16;
				u32 instance_id:8;
				u32 module_msg_type:5;
				u32 msg_direction:1;
				u32 msg_target:1;
			};
		};
		union {
			u32 val;
			struct {
				u32 param_block_size:16;
				u32 ppl_instance_id:8;
				u32 core_id:4;
				u32 proc_domain:1;
			} init_instance;
			struct {
				u32 data_off_size:20;
				u32 large_param_id:8;
				u32 final_block:1;
				u32 init_block:1;
			} large_config;
			struct {
				u32 dst_module_id:16;
				u32 dst_instance_id:8;
				u32 dst_queue:3;
				u32 src_queue:3;
			} bind_unbind;
			struct {
				u32 wake:1;
				u32 streaming:1;
			} set_d0ix;
		} ext;
	};
} __packed;

#define AVS_IPC_NOT_SUPPORTED 15

union avs_reply_msg {
	u64 val;
	struct {
		union {
			u32 primary;
			struct {
				u32 status:24;
				u32 global_msg_type:5;
				u32 msg_direction:1;
				u32 msg_target:1;
			};
		};
		union {
			u32 val;
			/* module loading */
			struct {
				u32 err_mod_id:16;
			} load_multi_mods;
			/* pipeline management */
			struct {
				u32 state:5;
			} get_ppl_state;
			/* module management */
			struct {
				u32 data_off_size:20;
				u32 large_param_id:8;
				u32 final_block:1;
				u32 init_block:1;
			} large_config;
		} ext;
	};
} __packed;

enum avs_notify_msg_type {
	AVS_NOTIFY_PHRASE_DETECTED = 4,
	AVS_NOTIFY_RESOURCE_EVENT = 5,
	AVS_NOTIFY_LOG_BUFFER_STATUS = 6,
	AVS_NOTIFY_FW_READY = 8,
	AVS_NOTIFY_EXCEPTION_CAUGHT = 10,
	AVS_NOTIFY_MODULE_EVENT = 12,
};

union avs_notify_msg {
	u64 val;
	struct {
		union {
			u32 primary;
			struct {
				u32 rsvd:16;
				u32 notify_msg_type:8;
				u32 global_msg_type:5;
				u32 msg_direction:1;
				u32 msg_target:1;
			};
			struct {
				u16 rsvd:12;
				u16 core:4;
			} log;
		};
		union {
			u32 val;
			struct {
				u32 core_id:2;
				u32 stack_dump_size:16;
			} coredump;
		} ext;
	};
} __packed;

#define AVS_MSG(hdr) { .val = hdr }

#define AVS_GLOBAL_REQUEST(msg_type)		\
{						\
	.global_msg_type = AVS_GLB_##msg_type,	\
	.msg_direction = AVS_MSG_REQUEST,	\
	.msg_target = AVS_FW_GEN_MSG,		\
}

#define AVS_MODULE_REQUEST(msg_type)		\
{						\
	.module_msg_type = AVS_MOD_##msg_type,	\
	.msg_direction = AVS_MSG_REQUEST,	\
	.msg_target = AVS_MOD_MSG,		\
}

#define AVS_NOTIFICATION(msg_type)		\
{						\
	.notify_msg_type = AVS_NOTIFY_##msg_type,\
	.global_msg_type = AVS_GLB_NOTIFICATION,\
	.msg_direction = AVS_MSG_REPLY,		\
	.msg_target = AVS_FW_GEN_MSG,		\
}

#define avs_msg_is_reply(hdr) \
({ \
	union avs_reply_msg __msg = AVS_MSG(hdr); \
	__msg.msg_direction == AVS_MSG_REPLY && \
	__msg.global_msg_type != AVS_GLB_NOTIFICATION; \
})

/* Notification types */

struct avs_notify_voice_data {
	u16 kpd_score;
	u16 reserved;
} __packed;

struct avs_notify_res_data {
	u32 resource_type;
	u32 resource_id;
	u32 event_type;
	u32 reserved;
	u32 data[6];
} __packed;

struct avs_notify_mod_data {
	u32 module_instance_id;
	u32 event_id;
	u32 data_size;
	u32 data[];
} __packed;

/* ROM messages */
enum avs_rom_control_msg_type {
	AVS_ROM_SET_BOOT_CONFIG = 0,
};

int avs_ipc_set_boot_config(struct avs_dev *adev, u32 dma_id, u32 purge);

/* Code loading messages */
int avs_ipc_load_modules(struct avs_dev *adev, u16 *mod_ids, u32 num_mod_ids);
int avs_ipc_unload_modules(struct avs_dev *adev, u16 *mod_ids, u32 num_mod_ids);
int avs_ipc_load_library(struct avs_dev *adev, u32 dma_id, u32 lib_id);

/* Pipeline management messages */
enum avs_pipeline_state {
	AVS_PPL_STATE_INVALID,
	AVS_PPL_STATE_UNINITIALIZED,
	AVS_PPL_STATE_RESET,
	AVS_PPL_STATE_PAUSED,
	AVS_PPL_STATE_RUNNING,
};

int avs_ipc_create_pipeline(struct avs_dev *adev, u16 req_size, u8 priority,
			    u8 instance_id, bool lp, u16 attributes);
int avs_ipc_delete_pipeline(struct avs_dev *adev, u8 instance_id);
int avs_ipc_set_pipeline_state(struct avs_dev *adev, u8 instance_id,
			       enum avs_pipeline_state state);
int avs_ipc_get_pipeline_state(struct avs_dev *adev, u8 instance_id,
			       enum avs_pipeline_state *state);

/* Module management messages */
int avs_ipc_init_instance(struct avs_dev *adev, u16 module_id, u8 instance_id,
			  u8 ppl_id, u8 core_id, u8 domain,
			  void *param, u32 param_size);
int avs_ipc_delete_instance(struct avs_dev *adev, u16 module_id, u8 instance_id);
int avs_ipc_bind(struct avs_dev *adev, u16 module_id, u8 instance_id,
		 u16 dst_module_id, u8 dst_instance_id,
		 u8 dst_queue, u8 src_queue);
int avs_ipc_unbind(struct avs_dev *adev, u16 module_id, u8 instance_id,
		   u16 dst_module_id, u8 dst_instance_id,
		   u8 dst_queue, u8 src_queue);
int avs_ipc_set_large_config(struct avs_dev *adev, u16 module_id,
			     u8 instance_id, u8 param_id,
			     u8 *request, size_t request_size);
int avs_ipc_get_large_config(struct avs_dev *adev, u16 module_id, u8 instance_id,
			     u8 param_id, u8 *request_data, size_t request_size,
			     u8 **reply_data, size_t *reply_size);

/* DSP cores and domains power management messages */
struct avs_dxstate_info {
	u32 core_mask;	/* which cores are subject for power transition */
	u32 dx_mask;	/* bit[n]=1 core n goes to D0, bit[n]=0 it goes to D3 */
} __packed;

int avs_ipc_set_dx(struct avs_dev *adev, u32 core_mask, bool powerup);
int avs_ipc_set_d0ix(struct avs_dev *adev, bool enable_pg, bool streaming);

/* Base-firmware runtime parameters */

#define AVS_BASEFW_MOD_ID	0
#define AVS_BASEFW_INST_ID	0

enum avs_basefw_runtime_param {
	AVS_BASEFW_ENABLE_LOGS = 6,
	AVS_BASEFW_FIRMWARE_CONFIG = 7,
	AVS_BASEFW_HARDWARE_CONFIG = 8,
	AVS_BASEFW_MODULES_INFO = 9,
	AVS_BASEFW_LIBRARIES_INFO = 16,
	AVS_BASEFW_SYSTEM_TIME = 20,
};

enum avs_log_enable {
	AVS_LOG_DISABLE = 0,
	AVS_LOG_ENABLE = 1
};

enum avs_skl_log_priority {
	AVS_SKL_LOG_CRITICAL = 1,
	AVS_SKL_LOG_HIGH,
	AVS_SKL_LOG_MEDIUM,
	AVS_SKL_LOG_LOW,
	AVS_SKL_LOG_VERBOSE,
};

struct skl_log_state {
	u32 enable;
	u32 min_priority;
} __packed;

struct skl_log_state_info {
	u32 core_mask;
	struct skl_log_state logs_core[];
} __packed;

struct apl_log_state_info {
	u32 aging_timer_period;
	u32 fifo_full_timer_period;
	u32 core_mask;
	struct skl_log_state logs_core[];
} __packed;

int avs_ipc_set_enable_logs(struct avs_dev *adev, u8 *log_info, size_t size);

struct avs_fw_version {
	u16 major;
	u16 minor;
	u16 hotfix;
	u16 build;
};

enum avs_fw_cfg_params {
	AVS_FW_CFG_FW_VERSION = 0,
	AVS_FW_CFG_MEMORY_RECLAIMED,
	AVS_FW_CFG_SLOW_CLOCK_FREQ_HZ,
	AVS_FW_CFG_FAST_CLOCK_FREQ_HZ,
	AVS_FW_CFG_DMA_BUFFER_CONFIG,
	AVS_FW_CFG_ALH_SUPPORT_LEVEL,
	AVS_FW_CFG_IPC_DL_MAILBOX_BYTES,
	AVS_FW_CFG_IPC_UL_MAILBOX_BYTES,
	AVS_FW_CFG_TRACE_LOG_BYTES,
	AVS_FW_CFG_MAX_PPL_COUNT,
	AVS_FW_CFG_MAX_ASTATE_COUNT,
	AVS_FW_CFG_MAX_MODULE_PIN_COUNT,
	AVS_FW_CFG_MODULES_COUNT,
	AVS_FW_CFG_MAX_MOD_INST_COUNT,
	AVS_FW_CFG_MAX_LL_TASKS_PER_PRI_COUNT,
	AVS_FW_CFG_LL_PRI_COUNT,
	AVS_FW_CFG_MAX_DP_TASKS_COUNT,
	AVS_FW_CFG_MAX_LIBS_COUNT,
	AVS_FW_CFG_SCHEDULER_CONFIG,
	AVS_FW_CFG_XTAL_FREQ_HZ,
	AVS_FW_CFG_CLOCKS_CONFIG,
	AVS_FW_CFG_RESERVED,
	AVS_FW_CFG_POWER_GATING_POLICY,
	AVS_FW_CFG_ASSERT_MODE,
};

struct avs_fw_cfg {
	struct avs_fw_version fw_version;
	u32 memory_reclaimed;
	u32 slow_clock_freq_hz;
	u32 fast_clock_freq_hz;
	u32 alh_support;
	u32 ipc_dl_mailbox_bytes;
	u32 ipc_ul_mailbox_bytes;
	u32 trace_log_bytes;
	u32 max_ppl_count;
	u32 max_astate_count;
	u32 max_module_pin_count;
	u32 modules_count;
	u32 max_mod_inst_count;
	u32 max_ll_tasks_per_pri_count;
	u32 ll_pri_count;
	u32 max_dp_tasks_count;
	u32 max_libs_count;
	u32 xtal_freq_hz;
	u32 power_gating_policy;
};

int avs_ipc_get_fw_config(struct avs_dev *adev, struct avs_fw_cfg *cfg);

enum avs_hw_cfg_params {
	AVS_HW_CFG_AVS_VER,
	AVS_HW_CFG_DSP_CORES,
	AVS_HW_CFG_MEM_PAGE_BYTES,
	AVS_HW_CFG_TOTAL_PHYS_MEM_PAGES,
	AVS_HW_CFG_I2S_CAPS,
	AVS_HW_CFG_GPDMA_CAPS,
	AVS_HW_CFG_GATEWAY_COUNT,
	AVS_HW_CFG_HP_EBB_COUNT,
	AVS_HW_CFG_LP_EBB_COUNT,
	AVS_HW_CFG_EBB_SIZE_BYTES,
};

enum avs_iface_version {
	AVS_AVS_VER_1_5 = 0x10005,
	AVS_AVS_VER_1_8 = 0x10008,
};

enum avs_i2s_version {
	AVS_I2S_VER_15_SKYLAKE   = 0x00000,
	AVS_I2S_VER_15_BROXTON   = 0x10000,
	AVS_I2S_VER_15_BROXTON_P = 0x20000,
	AVS_I2S_VER_18_KBL_CNL   = 0x30000,
};

struct avs_i2s_caps {
	u32 i2s_version;
	u32 ctrl_count;
	u32 *ctrl_base_addr;
};

struct avs_hw_cfg {
	u32 avs_version;
	u32 dsp_cores;
	u32 mem_page_bytes;
	u32 total_phys_mem_pages;
	struct avs_i2s_caps i2s_caps;
	u32 gateway_count;
	u32 hp_ebb_count;
	u32 lp_ebb_count;
	u32 ebb_size_bytes;
};

int avs_ipc_get_hw_config(struct avs_dev *adev, struct avs_hw_cfg *cfg);

#define AVS_MODULE_LOAD_TYPE_BUILTIN	0
#define AVS_MODULE_LOAD_TYPE_LOADABLE	1
#define AVS_MODULE_STATE_LOADED		BIT(0)

struct avs_module_type {
	u32 load_type:4;
	u32 auto_start:1;
	u32 domain_ll:1;
	u32 domain_dp:1;
	u32 lib_code:1;
	u32 rsvd:24;
} __packed;

union avs_segment_flags {
	u32 ul;
	struct {
		u32 contents:1;
		u32 alloc:1;
		u32 load:1;
		u32 readonly:1;
		u32 code:1;
		u32 data:1;
		u32 rsvd_1:2;
		u32 type:4;
		u32 rsvd_2:4;
		u32 length:16;
	};
} __packed;

struct avs_segment_desc {
	union avs_segment_flags flags;
	u32 v_base_addr;
	u32 file_offset;
} __packed;

struct avs_module_entry {
	u16 module_id;
	u16 state_flags;
	u8 name[8];
	guid_t uuid;
	struct avs_module_type type;
	u8 hash[32];
	u32 entry_point;
	u16 cfg_offset;
	u16 cfg_count;
	u32 affinity_mask;
	u16 instance_max_count;
	u16 instance_bss_size;
	struct avs_segment_desc segments[3];
} __packed;

struct avs_mods_info {
	u32 count;
	struct avs_module_entry entries[];
} __packed;

static inline bool avs_module_entry_is_loaded(struct avs_module_entry *mentry)
{
	return mentry->type.load_type == AVS_MODULE_LOAD_TYPE_BUILTIN ||
	       mentry->state_flags & AVS_MODULE_STATE_LOADED;
}

int avs_ipc_get_modules_info(struct avs_dev *adev, struct avs_mods_info **info);

struct avs_sys_time {
	u32 val_l;
	u32 val_u;
} __packed;

int avs_ipc_set_system_time(struct avs_dev *adev);

/* Module configuration */

#define AVS_MIXIN_MOD_UUID \
	GUID_INIT(0x39656EB2, 0x3B71, 0x4049, 0x8D, 0x3F, 0xF9, 0x2C, 0xD5, 0xC4, 0x3C, 0x09)

#define AVS_MIXOUT_MOD_UUID \
	GUID_INIT(0x3C56505A, 0x24D7, 0x418F, 0xBD, 0xDC, 0xC1, 0xF5, 0xA3, 0xAC, 0x2A, 0xE0)

#define AVS_COPIER_MOD_UUID \
	GUID_INIT(0x9BA00C83, 0xCA12, 0x4A83, 0x94, 0x3C, 0x1F, 0xA2, 0xE8, 0x2F, 0x9D, 0xDA)

#define AVS_KPBUFF_MOD_UUID \
	GUID_INIT(0xA8A0CB32, 0x4A77, 0x4DB1, 0x85, 0xC7, 0x53, 0xD7, 0xEE, 0x07, 0xBC, 0xE6)

#define AVS_MICSEL_MOD_UUID \
	GUID_INIT(0x32FE92C1, 0x1E17, 0x4FC2, 0x97, 0x58, 0xC7, 0xF3, 0x54, 0x2E, 0x98, 0x0A)

#define AVS_MUX_MOD_UUID \
	GUID_INIT(0x64CE6E35, 0x857A, 0x4878, 0xAC, 0xE8, 0xE2, 0xA2, 0xF4, 0x2e, 0x30, 0x69)

#define AVS_UPDWMIX_MOD_UUID \
	GUID_INIT(0x42F8060C, 0x832F, 0x4DBF, 0xB2, 0x47, 0x51, 0xE9, 0x61, 0x99, 0x7b, 0x35)

#define AVS_SRCINTC_MOD_UUID \
	GUID_INIT(0xE61BB28D, 0x149A, 0x4C1F, 0xB7, 0x09, 0x46, 0x82, 0x3E, 0xF5, 0xF5, 0xAE)

#define AVS_PROBE_MOD_UUID \
	GUID_INIT(0x7CAD0808, 0xAB10, 0xCD23, 0xEF, 0x45, 0x12, 0xAB, 0x34, 0xCD, 0x56, 0xEF)

#define AVS_AEC_MOD_UUID \
	GUID_INIT(0x46CB87FB, 0xD2C9, 0x4970, 0x96, 0xD2, 0x6D, 0x7E, 0x61, 0x4B, 0xB6, 0x05)

#define AVS_ASRC_MOD_UUID \
	GUID_INIT(0x66B4402D, 0xB468, 0x42F2, 0x81, 0xA7, 0xB3, 0x71, 0x21, 0x86, 0x3D, 0xD4)

#define AVS_INTELWOV_MOD_UUID \
	GUID_INIT(0xEC774FA9, 0x28D3, 0x424A, 0x90, 0xE4, 0x69, 0xF9, 0x84, 0xF1, 0xEE, 0xB7)

/* channel map */
enum avs_channel_index {
	AVS_CHANNEL_LEFT = 0,
	AVS_CHANNEL_RIGHT = 1,
	AVS_CHANNEL_CENTER = 2,
	AVS_CHANNEL_LEFT_SURROUND = 3,
	AVS_CHANNEL_CENTER_SURROUND = 3,
	AVS_CHANNEL_RIGHT_SURROUND = 4,
	AVS_CHANNEL_LFE = 7,
	AVS_CHANNEL_INVALID = 0xF,
};

enum avs_channel_config {
	AVS_CHANNEL_CONFIG_MONO = 0,
	AVS_CHANNEL_CONFIG_STEREO = 1,
	AVS_CHANNEL_CONFIG_2_1 = 2,
	AVS_CHANNEL_CONFIG_3_0 = 3,
	AVS_CHANNEL_CONFIG_3_1 = 4,
	AVS_CHANNEL_CONFIG_QUATRO = 5,
	AVS_CHANNEL_CONFIG_4_0 = 6,
	AVS_CHANNEL_CONFIG_5_0 = 7,
	AVS_CHANNEL_CONFIG_5_1 = 8,
	AVS_CHANNEL_CONFIG_DUAL_MONO = 9,
	AVS_CHANNEL_CONFIG_I2S_DUAL_STEREO_0 = 10,
	AVS_CHANNEL_CONFIG_I2S_DUAL_STEREO_1 = 11,
	AVS_CHANNEL_CONFIG_4_CHANNEL = 12,
	AVS_CHANNEL_CONFIG_INVALID
};

enum avs_interleaving {
	AVS_INTERLEAVING_PER_CHANNEL = 0,
	AVS_INTERLEAVING_PER_SAMPLE = 1,
};

enum avs_sample_type {
	AVS_SAMPLE_TYPE_INT_MSB = 0,
	AVS_SAMPLE_TYPE_INT_LSB = 1,
	AVS_SAMPLE_TYPE_INT_SIGNED = 2,
	AVS_SAMPLE_TYPE_INT_UNSIGNED = 3,
	AVS_SAMPLE_TYPE_FLOAT = 4,
};

#define AVS_CHANNELS_MAX	8
#define AVS_ALL_CHANNELS_MASK	UINT_MAX

struct avs_audio_format {
	u32 sampling_freq;
	u32 bit_depth;
	u32 channel_map;
	u32 channel_config;
	u32 interleaving;
	u32 num_channels:8;
	u32 valid_bit_depth:8;
	u32 sample_type:8;
	u32 reserved:8;
} __packed;

struct avs_modcfg_base {
	u32 cpc;
	u32 ibs;
	u32 obs;
	u32 is_pages;
	struct avs_audio_format audio_fmt;
} __packed;

struct avs_pin_format {
	u32 pin_index;
	u32 iobs;
	struct avs_audio_format audio_fmt;
} __packed;

struct avs_modcfg_ext {
	struct avs_modcfg_base base;
	u16 num_input_pins;
	u16 num_output_pins;
	u8 reserved[12];
	/* input pin formats followed by output ones */
	struct avs_pin_format pin_fmts[];
} __packed;

enum avs_dma_type {
	AVS_DMA_HDA_HOST_OUTPUT = 0,
	AVS_DMA_HDA_HOST_INPUT = 1,
	AVS_DMA_HDA_LINK_OUTPUT = 8,
	AVS_DMA_HDA_LINK_INPUT = 9,
	AVS_DMA_DMIC_LINK_INPUT = 11,
	AVS_DMA_I2S_LINK_OUTPUT = 12,
	AVS_DMA_I2S_LINK_INPUT = 13,
};

union avs_virtual_index {
	u8 val;
	struct {
		u8 time_slot:4;
		u8 instance:4;
	} i2s;
	struct {
		u8 queue_id:3;
		u8 time_slot:2;
		u8 instance:3;
	} dmic;
} __packed;

union avs_connector_node_id {
	u32 val;
	struct {
		u32 vindex:8;
		u32 dma_type:5;
		u32 rsvd:19;
	};
} __packed;

#define INVALID_PIPELINE_ID	0xFF
#define INVALID_NODE_ID \
	((union avs_connector_node_id) { UINT_MAX })

union avs_gtw_attributes {
	u32 val;
	struct {
		u32 lp_buffer_alloc:1;
		u32 rsvd:31;
	};
} __packed;

struct avs_copier_gtw_cfg {
	union avs_connector_node_id node_id;
	u32 dma_buffer_size;
	u32 config_length;
	struct {
		union avs_gtw_attributes attrs;
		u32 blob[];
	} config;
} __packed;

struct avs_copier_cfg {
	struct avs_modcfg_base base;
	struct avs_audio_format out_fmt;
	u32 feature_mask;
	struct avs_copier_gtw_cfg gtw_cfg;
} __packed;

struct avs_micsel_cfg {
	struct avs_modcfg_base base;
	struct avs_audio_format out_fmt;
} __packed;

struct avs_mux_cfg {
	struct avs_modcfg_base base;
	struct avs_audio_format ref_fmt;
	struct avs_audio_format out_fmt;
} __packed;

struct avs_updown_mixer_cfg {
	struct avs_modcfg_base base;
	u32 out_channel_config;
	u32 coefficients_select;
	s32 coefficients[AVS_CHANNELS_MAX];
	u32 channel_map;
} __packed;

struct avs_src_cfg {
	struct avs_modcfg_base base;
	u32 out_freq;
} __packed;

struct avs_probe_gtw_cfg {
	union avs_connector_node_id node_id;
	u32 dma_buffer_size;
} __packed;

struct avs_probe_cfg {
	struct avs_modcfg_base base;
	struct avs_probe_gtw_cfg gtw_cfg;
} __packed;

struct avs_aec_cfg {
	struct avs_modcfg_base base;
	struct avs_audio_format ref_fmt;
	struct avs_audio_format out_fmt;
	u32 cpc_lp_mode;
} __packed;

struct avs_asrc_cfg {
	struct avs_modcfg_base base;
	u32 out_freq;
	u32 rsvd0:1;
	u32 mode:1;
	u32 rsvd2:2;
	u32 disable_jitter_buffer:1;
	u32 rsvd3:27;
} __packed;

struct avs_wov_cfg {
	struct avs_modcfg_base base;
	u32 cpc_lp_mode;
} __packed;

/* Module runtime parameters */

enum avs_copier_runtime_param {
	AVS_COPIER_SET_SINK_FORMAT = 2,
};

struct avs_copier_sink_format {
	u32 sink_id;
	struct avs_audio_format src_fmt;
	struct avs_audio_format sink_fmt;
} __packed;

int avs_ipc_copier_set_sink_format(struct avs_dev *adev, u16 module_id,
				   u8 instance_id, u32 sink_id,
				   const struct avs_audio_format *src_fmt,
				   const struct avs_audio_format *sink_fmt);

#define AVS_PROBE_INST_ID	0

enum avs_probe_runtime_param {
	AVS_PROBE_INJECTION_DMA = 1,
	AVS_PROBE_INJECTION_DMA_DETACH,
	AVS_PROBE_POINTS,
	AVS_PROBE_POINTS_DISCONNECT,
};

struct avs_probe_dma {
	union avs_connector_node_id node_id;
	u32 dma_buffer_size;
} __packed;

enum avs_probe_type {
	AVS_PROBE_TYPE_INPUT = 0,
	AVS_PROBE_TYPE_OUTPUT,
	AVS_PROBE_TYPE_INTERNAL
};

union avs_probe_point_id {
	u32 value;
	struct {
		u32 module_id:16;
		u32 instance_id:8;
		u32 type:2;
		u32 index:6;
	} id;
} __packed;

enum avs_connection_purpose {
	AVS_CONNECTION_PURPOSE_EXTRACT = 0,
	AVS_CONNECTION_PURPOSE_INJECT,
	AVS_CONNECTION_PURPOSE_INJECT_REEXTRACT,
};

struct avs_probe_point_desc {
	union avs_probe_point_id id;
	u32 purpose;
	union avs_connector_node_id node_id;
} __packed;

int avs_ipc_probe_get_dma(struct avs_dev *adev, struct avs_probe_dma **dmas, size_t *num_dmas);
int avs_ipc_probe_attach_dma(struct avs_dev *adev, struct avs_probe_dma *dmas, size_t num_dmas);
int avs_ipc_probe_detach_dma(struct avs_dev *adev, union avs_connector_node_id *node_ids,
			     size_t num_node_ids);
int avs_ipc_probe_get_points(struct avs_dev *adev, struct avs_probe_point_desc **descs,
			     size_t *num_descs);
int avs_ipc_probe_connect_points(struct avs_dev *adev, struct avs_probe_point_desc *descs,
				 size_t num_descs);
int avs_ipc_probe_disconnect_points(struct avs_dev *adev, union avs_probe_point_id *ids,
				    size_t num_ids);

#endif /* __SOUND_SOC_INTEL_AVS_MSGS_H */
