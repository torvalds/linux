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
		};
		union {
			u32 val;
		} ext;
	};
} __packed;

struct avs_tlv {
	u32 type;
	u32 length;
	u32 value[];
} __packed;

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

#endif /* __SOUND_SOC_INTEL_AVS_MSGS_H */
