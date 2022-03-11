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
	AVS_NOTIFY_FW_READY = 8,
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
		};
		union {
			u32 val;
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
	AVS_BASEFW_FIRMWARE_CONFIG = 7,
	AVS_BASEFW_HARDWARE_CONFIG = 8,
	AVS_BASEFW_MODULES_INFO = 9,
	AVS_BASEFW_LIBRARIES_INFO = 16,
};

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

#endif /* __SOUND_SOC_INTEL_AVS_MSGS_H */
