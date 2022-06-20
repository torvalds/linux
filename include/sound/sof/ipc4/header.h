/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_SOUND_SOF_IPC4_HEADER_H__
#define __INCLUDE_SOUND_SOF_IPC4_HEADER_H__

#include <linux/types.h>
#include <uapi/sound/sof/abi.h>

/* maximum message size for mailbox Tx/Rx */
#define SOF_IPC4_MSG_MAX_SIZE			4096

/** \addtogroup sof_uapi uAPI
 *  SOF uAPI specification.
 *  @{
 */

/**
 * struct sof_ipc4_msg - Placeholder of an IPC4 message
 * @header_u64:		IPC4 header as single u64 number
 * @primary:		Primary, mandatory part of the header
 * @extension:		Extended part of the header, if not used it should be
 *			set to 0
 * @data_size:		Size of data in bytes pointed by @data_ptr
 * @data_ptr:		Pointer to the optional payload of a message
 */
struct sof_ipc4_msg {
	union {
		u64 header_u64;
		struct {
			u32 primary;
			u32 extension;
		};
	};

	size_t data_size;
	void *data_ptr;
};

/**
 * struct sof_ipc4_tuple - Generic type/ID and parameter tuple
 * @type:		type/ID
 * @size:		size of the @value array in bytes
 * @value:		value for the given type
 */
struct sof_ipc4_tuple {
	uint32_t type;
	uint32_t size;
	uint32_t value[];
} __packed;

/*
 * IPC4 messages have two 32 bit identifier made up as follows :-
 *
 * header - msg type, msg id, msg direction ...
 * extension - extra params such as msg data size in mailbox
 *
 * These are sent at the start of the IPC message in the mailbox. Messages
 * should not be sent in the doorbell (special exceptions for firmware).
 */

/*
 * IPC4 primary header bit allocation for messages
 * bit 0-23:	message type specific
 * bit 24-28:	type:	enum sof_ipc4_global_msg if target is SOF_IPC4_FW_GEN_MSG
 *			enum sof_ipc4_module_type if target is SOF_IPC4_MODULE_MSG
 * bit 29:	response - sof_ipc4_msg_dir
 * bit 30:	target - enum sof_ipc4_msg_target
 * bit 31:	reserved, unused
 */

/* Value of target field - must fit into 1 bit */
enum sof_ipc4_msg_target {
	/* Global FW message */
	SOF_IPC4_FW_GEN_MSG,

	/* Module message */
	SOF_IPC4_MODULE_MSG
};

/* Value of type field - must fit into 5 bits */
enum sof_ipc4_global_msg {
	SOF_IPC4_GLB_BOOT_CONFIG,
	SOF_IPC4_GLB_ROM_CONTROL,
	SOF_IPC4_GLB_IPCGATEWAY_CMD,

	/* 3 .. 12: RESERVED - do not use */

	SOF_IPC4_GLB_PERF_MEASUREMENTS_CMD = 13,
	SOF_IPC4_GLB_CHAIN_DMA,

	SOF_IPC4_GLB_LOAD_MULTIPLE_MODULES,
	SOF_IPC4_GLB_UNLOAD_MULTIPLE_MODULES,

	/* pipeline settings */
	SOF_IPC4_GLB_CREATE_PIPELINE,
	SOF_IPC4_GLB_DELETE_PIPELINE,
	SOF_IPC4_GLB_SET_PIPELINE_STATE,
	SOF_IPC4_GLB_GET_PIPELINE_STATE,
	SOF_IPC4_GLB_GET_PIPELINE_CONTEXT_SIZE,
	SOF_IPC4_GLB_SAVE_PIPELINE,
	SOF_IPC4_GLB_RESTORE_PIPELINE,

	/* Loads library (using Code Load or HD/A Host Output DMA) */
	SOF_IPC4_GLB_LOAD_LIBRARY,

	/* 25: RESERVED - do not use */

	SOF_IPC4_GLB_INTERNAL_MESSAGE = 26,

	/* Notification (FW to SW driver) */
	SOF_IPC4_GLB_NOTIFICATION,

	/* 28 .. 31: RESERVED - do not use */

	SOF_IPC4_GLB_TYPE_LAST,
};

/* Value of response field - must fit into 1 bit */
enum sof_ipc4_msg_dir {
	SOF_IPC4_MSG_REQUEST,
	SOF_IPC4_MSG_REPLY,
};

enum sof_ipc4_pipeline_state {
	SOF_IPC4_PIPE_INVALID_STATE,
	SOF_IPC4_PIPE_UNINITIALIZED,
	SOF_IPC4_PIPE_RESET,
	SOF_IPC4_PIPE_PAUSED,
	SOF_IPC4_PIPE_RUNNING,
	SOF_IPC4_PIPE_EOS
};

/* Generic message fields (bit 24-30) */

/* encoded to header's msg_tgt field */
#define SOF_IPC4_MSG_TARGET_SHIFT		30
#define SOF_IPC4_MSG_TARGET_MASK		BIT(30)
#define SOF_IPC4_MSG_TARGET(x)			((x) << SOF_IPC4_MSG_TARGET_SHIFT)
#define SOF_IPC4_MSG_IS_MODULE_MSG(x)		((x) & SOF_IPC4_MSG_TARGET_MASK ? 1 : 0)

/* encoded to header's rsp field */
#define SOF_IPC4_MSG_DIR_SHIFT			29
#define SOF_IPC4_MSG_DIR_MASK			BIT(29)
#define SOF_IPC4_MSG_DIR(x)			((x) << SOF_IPC4_MSG_DIR_SHIFT)

/* encoded to header's type field */
#define SOF_IPC4_MSG_TYPE_SHIFT			24
#define SOF_IPC4_MSG_TYPE_MASK			GENMASK(28, 24)
#define SOF_IPC4_MSG_TYPE_SET(x)		(((x) << SOF_IPC4_MSG_TYPE_SHIFT) & \
						 SOF_IPC4_MSG_TYPE_MASK)
#define SOF_IPC4_MSG_TYPE_GET(x)		(((x) & SOF_IPC4_MSG_TYPE_MASK) >> \
						 SOF_IPC4_MSG_TYPE_SHIFT)

/* Global message type specific field definitions */

/* pipeline creation ipc msg */
#define SOF_IPC4_GLB_PIPE_INSTANCE_SHIFT	16
#define SOF_IPC4_GLB_PIPE_INSTANCE_MASK		GENMASK(23, 16)
#define SOF_IPC4_GLB_PIPE_INSTANCE_ID(x)	((x) << SOF_IPC4_GLB_PIPE_INSTANCE_SHIFT)

#define SOF_IPC4_GLB_PIPE_PRIORITY_SHIFT	11
#define SOF_IPC4_GLB_PIPE_PRIORITY_MASK		GENMASK(15, 11)
#define SOF_IPC4_GLB_PIPE_PRIORITY(x)		((x) << SOF_IPC4_GLB_PIPE_PRIORITY_SHIFT)

#define SOF_IPC4_GLB_PIPE_MEM_SIZE_SHIFT	0
#define SOF_IPC4_GLB_PIPE_MEM_SIZE_MASK		GENMASK(10, 0)
#define SOF_IPC4_GLB_PIPE_MEM_SIZE(x)		((x) << SOF_IPC4_GLB_PIPE_MEM_SIZE_SHIFT)

#define SOF_IPC4_GLB_PIPE_EXT_LP_SHIFT		0
#define SOF_IPC4_GLB_PIPE_EXT_LP_MASK		BIT(0)
#define SOF_IPC4_GLB_PIPE_EXT_LP(x)		((x) << SOF_IPC4_GLB_PIPE_EXT_LP_SHIFT)

/* pipeline set state ipc msg */
#define SOF_IPC4_GLB_PIPE_STATE_ID_SHIFT		16
#define SOF_IPC4_GLB_PIPE_STATE_ID_MASK		GENMASK(23, 16)
#define SOF_IPC4_GLB_PIPE_STATE_ID(x)		((x) << SOF_IPC4_GLB_PIPE_STATE_ID_SHIFT)

#define SOF_IPC4_GLB_PIPE_STATE_SHIFT		0
#define SOF_IPC4_GLB_PIPE_STATE_MASK		GENMASK(15, 0)
#define SOF_IPC4_GLB_PIPE_STATE(x)		((x) << SOF_IPC4_GLB_PIPE_STATE_SHIFT)

enum sof_ipc4_channel_config {
	/* one channel only. */
	SOF_IPC4_CHANNEL_CONFIG_MONO,
	/* L & R. */
	SOF_IPC4_CHANNEL_CONFIG_STEREO,
	/* L, R & LFE; PCM only. */
	SOF_IPC4_CHANNEL_CONFIG_2_POINT_1,
	/* L, C & R; MP3 & AAC only. */
	SOF_IPC4_CHANNEL_CONFIG_3_POINT_0,
	/* L, C, R & LFE; PCM only. */
	SOF_IPC4_CHANNEL_CONFIG_3_POINT_1,
	/* L, R, Ls & Rs; PCM only. */
	SOF_IPC4_CHANNEL_CONFIG_QUATRO,
	/* L, C, R & Cs; MP3 & AAC only. */
	SOF_IPC4_CHANNEL_CONFIG_4_POINT_0,
	/* L, C, R, Ls & Rs. */
	SOF_IPC4_CHANNEL_CONFIG_5_POINT_0,
	/* L, C, R, Ls, Rs & LFE. */
	SOF_IPC4_CHANNEL_CONFIG_5_POINT_1,
	/* one channel replicated in two. */
	SOF_IPC4_CHANNEL_CONFIG_DUAL_MONO,
	/* Stereo (L,R) in 4 slots, 1st stream: [ L, R, -, - ] */
	SOF_IPC4_CHANNEL_CONFIG_I2S_DUAL_STEREO_0,
	/* Stereo (L,R) in 4 slots, 2nd stream: [ -, -, L, R ] */
	SOF_IPC4_CHANNEL_CONFIG_I2S_DUAL_STEREO_1,
	/* L, C, R, Ls, Rs & LFE., LS, RS */
	SOF_IPC4_CHANNEL_CONFIG_7_POINT_1,
};

enum sof_ipc4_interleaved_style {
	SOF_IPC4_CHANNELS_INTERLEAVED,
	SOF_IPC4_CHANNELS_NONINTERLEAVED,
};

enum sof_ipc4_sample_type {
	SOF_IPC4_MSB_INTEGER, /* integer with Most Significant Byte first */
	SOF_IPC4_LSB_INTEGER, /* integer with Least Significant Byte first */
};

struct sof_ipc4_audio_format {
	uint32_t sampling_frequency;
	uint32_t bit_depth;
	uint32_t ch_map;
	uint32_t ch_cfg; /* sof_ipc4_channel_config */
	uint32_t interleaving_style;
	uint32_t fmt_cfg; /* channels_count valid_bit_depth s_type */
} __packed __aligned(4);

#define SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT_SHIFT	0
#define SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT_MASK	GENMASK(7, 0)
#define SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(x)	\
	((x) & SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT_MASK)
#define SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH_SHIFT	8
#define SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH_MASK	GENMASK(15, 8)
#define SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(x)	\
	(((x) & SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH_MASK) >> \
	 SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH_SHIFT)
#define SOF_IPC4_AUDIO_FORMAT_CFG_SAMPLE_TYPE_SHIFT	16
#define SOF_IPC4_AUDIO_FORMAT_CFG_SAMPLE_TYPE_MASK	GENMASK(23, 16)
#define SOF_IPC4_AUDIO_FORMAT_CFG_SAMPLE_TYPE(x)	\
	(((x) & SOF_IPC4_AUDIO_FORMAT_CFG_SAMPLE_TYPE_MASK) >>  \
	 SOF_IPC4_AUDIO_FORMAT_CFG_SAMPLE_TYPE_SHIFT)

/* Module message type specific field definitions */

enum sof_ipc4_module_type {
	SOF_IPC4_MOD_INIT_INSTANCE,
	SOF_IPC4_MOD_CONFIG_GET,
	SOF_IPC4_MOD_CONFIG_SET,
	SOF_IPC4_MOD_LARGE_CONFIG_GET,
	SOF_IPC4_MOD_LARGE_CONFIG_SET,
	SOF_IPC4_MOD_BIND,
	SOF_IPC4_MOD_UNBIND,
	SOF_IPC4_MOD_SET_DX,
	SOF_IPC4_MOD_SET_D0IX,
	SOF_IPC4_MOD_ENTER_MODULE_RESTORE,
	SOF_IPC4_MOD_EXIT_MODULE_RESTORE,
	SOF_IPC4_MOD_DELETE_INSTANCE,

	SOF_IPC4_MOD_TYPE_LAST,
};

struct sof_ipc4_base_module_cfg {
	uint32_t cpc; /* the max count of Cycles Per Chunk processing */
	uint32_t ibs; /* input Buffer Size (in bytes)  */
	uint32_t obs; /* output Buffer Size (in bytes) */
	uint32_t is_pages; /* number of physical pages used */
	struct sof_ipc4_audio_format audio_fmt;
} __packed __aligned(4);

/* common module ipc msg */
#define SOF_IPC4_MOD_INSTANCE_SHIFT		16
#define SOF_IPC4_MOD_INSTANCE_MASK		GENMASK(23, 16)
#define SOF_IPC4_MOD_INSTANCE(x)		((x) << SOF_IPC4_MOD_INSTANCE_SHIFT)

#define SOF_IPC4_MOD_ID_SHIFT			0
#define SOF_IPC4_MOD_ID_MASK			GENMASK(15, 0)
#define SOF_IPC4_MOD_ID(x)			((x) << SOF_IPC4_MOD_ID_SHIFT)

/* init module ipc msg */
#define SOF_IPC4_MOD_EXT_PARAM_SIZE_SHIFT	0
#define SOF_IPC4_MOD_EXT_PARAM_SIZE_MASK	GENMASK(15, 0)
#define SOF_IPC4_MOD_EXT_PARAM_SIZE(x)		((x) << SOF_IPC4_MOD_EXT_PARAM_SIZE_SHIFT)

#define SOF_IPC4_MOD_EXT_PPL_ID_SHIFT		16
#define SOF_IPC4_MOD_EXT_PPL_ID_MASK		GENMASK(23, 16)
#define SOF_IPC4_MOD_EXT_PPL_ID(x)		((x) << SOF_IPC4_MOD_EXT_PPL_ID_SHIFT)

#define SOF_IPC4_MOD_EXT_CORE_ID_SHIFT		24
#define SOF_IPC4_MOD_EXT_CORE_ID_MASK		GENMASK(27, 24)
#define SOF_IPC4_MOD_EXT_CORE_ID(x)		((x) << SOF_IPC4_MOD_EXT_CORE_ID_SHIFT)

#define SOF_IPC4_MOD_EXT_DOMAIN_SHIFT		28
#define SOF_IPC4_MOD_EXT_DOMAIN_MASK		BIT(28)
#define SOF_IPC4_MOD_EXT_DOMAIN(x)		((x) << SOF_IPC4_MOD_EXT_DOMAIN_SHIFT)

/*  bind/unbind module ipc msg */
#define SOF_IPC4_MOD_EXT_DST_MOD_ID_SHIFT	0
#define SOF_IPC4_MOD_EXT_DST_MOD_ID_MASK	GENMASK(15, 0)
#define SOF_IPC4_MOD_EXT_DST_MOD_ID(x)		((x) << SOF_IPC4_MOD_EXT_DST_MOD_ID_SHIFT)

#define SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE_SHIFT	16
#define SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE_MASK	GENMASK(23, 16)
#define SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(x)	((x) << SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE_SHIFT)

#define SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID_SHIFT	24
#define SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID_MASK	GENMASK(26, 24)
#define SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID(x)	((x) << SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID_SHIFT)

#define SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID_SHIFT	27
#define SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID_MASK	GENMASK(29, 27)
#define SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID(x)	((x) << SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID_SHIFT)

#define MOD_ENABLE_LOG	6
#define MOD_SYSTEM_TIME	20

/* set module large config */
#define SOF_IPC4_MOD_EXT_MSG_SIZE_SHIFT		0
#define SOF_IPC4_MOD_EXT_MSG_SIZE_MASK		GENMASK(19, 0)
#define SOF_IPC4_MOD_EXT_MSG_SIZE(x)		((x) << SOF_IPC4_MOD_EXT_MSG_SIZE_SHIFT)

#define SOF_IPC4_MOD_EXT_MSG_PARAM_ID_SHIFT	20
#define SOF_IPC4_MOD_EXT_MSG_PARAM_ID_MASK	GENMASK(27, 20)
#define SOF_IPC4_MOD_EXT_MSG_PARAM_ID(x)	((x) << SOF_IPC4_MOD_EXT_MSG_PARAM_ID_SHIFT)

#define SOF_IPC4_MOD_EXT_MSG_LAST_BLOCK_SHIFT	28
#define SOF_IPC4_MOD_EXT_MSG_LAST_BLOCK_MASK	BIT(28)
#define SOF_IPC4_MOD_EXT_MSG_LAST_BLOCK(x)	((x) << SOF_IPC4_MOD_EXT_MSG_LAST_BLOCK_SHIFT)

#define SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK_SHIFT	29
#define SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK_MASK	BIT(29)
#define SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK(x)	((x) << SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK_SHIFT)

/* Init instance messagees */
#define SOF_IPC4_MOD_INIT_BASEFW_MOD_ID		0
#define SOF_IPC4_MOD_INIT_BASEFW_INSTANCE_ID	0

enum sof_ipc4_base_fw_params {
	SOF_IPC4_FW_PARAM_ENABLE_LOGS = 6,
	SOF_IPC4_FW_PARAM_FW_CONFIG,
	SOF_IPC4_FW_PARAM_HW_CONFIG_GET,
	SOF_IPC4_FW_PARAM_MODULES_INFO_GET,
	SOF_IPC4_FW_PARAM_LIBRARIES_INFO_GET = 16,
	SOF_IPC4_FW_PARAM_SYSTEM_TIME = 20,
};

enum sof_ipc4_fw_config_params {
	SOF_IPC4_FW_CFG_FW_VERSION,
	SOF_IPC4_FW_CFG_MEMORY_RECLAIMED,
	SOF_IPC4_FW_CFG_SLOW_CLOCK_FREQ_HZ,
	SOF_IPC4_FW_CFG_FAST_CLOCK_FREQ_HZ,
	SOF_IPC4_FW_CFG_DMA_BUFFER_CONFIG,
	SOF_IPC4_FW_CFG_ALH_SUPPORT_LEVEL,
	SOF_IPC4_FW_CFG_DL_MAILBOX_BYTES,
	SOF_IPC4_FW_CFG_UL_MAILBOX_BYTES,
	SOF_IPC4_FW_CFG_TRACE_LOG_BYTES,
	SOF_IPC4_FW_CFG_MAX_PPL_COUNT,
	SOF_IPC4_FW_CFG_MAX_ASTATE_COUNT,
	SOF_IPC4_FW_CFG_MAX_MODULE_PIN_COUNT,
	SOF_IPC4_FW_CFG_MODULES_COUNT,
	SOF_IPC4_FW_CFG_MAX_MOD_INST_COUNT,
	SOF_IPC4_FW_CFG_MAX_LL_TASKS_PER_PRI_COUNT,
	SOF_IPC4_FW_CFG_LL_PRI_COUNT,
	SOF_IPC4_FW_CFG_MAX_DP_TASKS_COUNT,
	SOF_IPC4_FW_CFG_MAX_LIBS_COUNT,
	SOF_IPC4_FW_CFG_SCHEDULER_CONFIG,
	SOF_IPC4_FW_CFG_XTAL_FREQ_HZ,
	SOF_IPC4_FW_CFG_CLOCKS_CONFIG,
	SOF_IPC4_FW_CFG_RESERVED,
	SOF_IPC4_FW_CFG_POWER_GATING_POLICY,
	SOF_IPC4_FW_CFG_ASSERT_MODE,
};

struct sof_ipc4_fw_version {
	uint16_t major;
	uint16_t minor;
	uint16_t hotfix;
	uint16_t build;
} __packed;

/* Reply messages */

/*
 * IPC4 primary header bit allocation for replies
 * bit 0-23:	status
 * bit 24-28:	type:	enum sof_ipc4_global_msg if target is SOF_IPC4_FW_GEN_MSG
 *			enum sof_ipc4_module_type if target is SOF_IPC4_MODULE_MSG
 * bit 29:	response - sof_ipc4_msg_dir
 * bit 30:	target - enum sof_ipc4_msg_target
 * bit 31:	reserved, unused
 */

#define SOF_IPC4_REPLY_STATUS			GENMASK(23, 0)

/* Notification messages */

/*
 * IPC4 primary header bit allocation for notifications
 * bit 0-15:	notification type specific
 * bit 16-23:	enum sof_ipc4_notification_type
 * bit 24-28:	SOF_IPC4_GLB_NOTIFICATION
 * bit 29:	response - sof_ipc4_msg_dir
 * bit 30:	target - enum sof_ipc4_msg_target
 * bit 31:	reserved, unused
 */

#define SOF_IPC4_MSG_IS_NOTIFICATION(x)		(SOF_IPC4_MSG_TYPE_GET(x) == \
						 SOF_IPC4_GLB_NOTIFICATION)

#define SOF_IPC4_NOTIFICATION_TYPE_SHIFT	16
#define SOF_IPC4_NOTIFICATION_TYPE_MASK		GENMASK(23, 16)
#define SOF_IPC4_NOTIFICATION_TYPE_GET(x)	(((x) & SOF_IPC4_NOTIFICATION_TYPE_MASK) >> \
						 SOF_IPC4_NOTIFICATION_TYPE_SHIFT)

/* Value of notification type field - must fit into 8 bits */
enum sof_ipc4_notification_type {
	/* Phrase detected (notification from WoV module) */
	SOF_IPC4_NOTIFY_PHRASE_DETECTED = 4,
	/* Event from a resource (pipeline or module instance) */
	SOF_IPC4_NOTIFY_RESOURCE_EVENT,
	/* Debug log buffer status changed */
	SOF_IPC4_NOTIFY_LOG_BUFFER_STATUS,
	/* Timestamp captured at the link */
	SOF_IPC4_NOTIFY_TIMESTAMP_CAPTURED,
	/* FW complete initialization */
	SOF_IPC4_NOTIFY_FW_READY,
	/* Audio classifier result (ACA) */
	SOF_IPC4_NOTIFY_FW_AUD_CLASS_RESULT,
	/* Exception caught by DSP FW */
	SOF_IPC4_NOTIFY_EXCEPTION_CAUGHT,
	/* 11 is skipped by the existing cavs firmware */
	/* Custom module notification */
	SOF_IPC4_NOTIFY_MODULE_NOTIFICATION = 12,
	/* 13 is reserved - do not use */
	/* Probe notify data available */
	SOF_IPC4_NOTIFY_PROBE_DATA_AVAILABLE = 14,
	/* AM module notifications */
	SOF_IPC4_NOTIFY_ASYNC_MSG_SRVC_MESSAGE,

	SOF_IPC4_NOTIFY_TYPE_LAST,
};

struct sof_ipc4_notify_resource_data {
	uint32_t resource_type;
	uint32_t resource_id;
	uint32_t event_type;
	uint32_t reserved;
	uint32_t data[6];
} __packed __aligned(4);

/** @}*/

#endif
