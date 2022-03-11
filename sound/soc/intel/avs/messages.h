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

#endif /* __SOUND_SOC_INTEL_AVS_MSGS_H */
