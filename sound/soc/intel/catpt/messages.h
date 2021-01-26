/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2020 Intel Corporation. All rights reserved.
 *
 * Author: Cezary Rojewski <cezary.rojewski@intel.com>
 */

#ifndef __SND_SOC_INTEL_CATPT_MSG_H
#define __SND_SOC_INTEL_CATPT_MSG_H

struct catpt_dev;

/* IPC messages base types  */

enum catpt_reply_status {
	CATPT_REPLY_SUCCESS = 0,
	CATPT_REPLY_ERROR_INVALID_PARAM = 1,
	CATPT_REPLY_UNKNOWN_MESSAGE_TYPE = 2,
	CATPT_REPLY_OUT_OF_RESOURCES = 3,
	CATPT_REPLY_BUSY = 4,
	CATPT_REPLY_PENDING = 5,
	CATPT_REPLY_FAILURE = 6,
	CATPT_REPLY_INVALID_REQUEST = 7,
	CATPT_REPLY_UNINITIALIZED = 8,
	CATPT_REPLY_NOT_FOUND = 9,
	CATPT_REPLY_SOURCE_NOT_STARTED = 10,
};

/* GLOBAL messages */

enum catpt_global_msg_type {
	CATPT_GLB_GET_FW_VERSION = 0,
	CATPT_GLB_ALLOCATE_STREAM = 3,
	CATPT_GLB_FREE_STREAM = 4,
	CATPT_GLB_STREAM_MESSAGE = 6,
	CATPT_GLB_REQUEST_CORE_DUMP = 7,
	CATPT_GLB_SET_DEVICE_FORMATS = 10,
	CATPT_GLB_ENTER_DX_STATE = 12,
	CATPT_GLB_GET_MIXER_STREAM_INFO = 13,
};

union catpt_global_msg {
	u32 val;
	struct {
		u32 status:5;
		u32 context:19; /* stream or module specific */
		u32 global_msg_type:5;
		u32 fw_ready:1;
		u32 done:1;
		u32 busy:1;
	};
} __packed;

#define CATPT_MSG(hdr) { .val = hdr }
#define CATPT_GLOBAL_MSG(msg_type) \
	{ .global_msg_type = CATPT_GLB_##msg_type }

#define BUILD_HASH_SIZE		40

struct catpt_fw_version {
	u8 build;
	u8 minor;
	u8 major;
	u8 type;
	u8 build_hash[BUILD_HASH_SIZE];
	u32 log_providers_hash;
} __packed;

int catpt_ipc_get_fw_version(struct catpt_dev *cdev,
			     struct catpt_fw_version *version);

enum catpt_pin_id {
	CATPT_PIN_ID_SYSTEM = 0,
	CATPT_PIN_ID_REFERENCE = 1,
	CATPT_PIN_ID_CAPTURE1 = 2,
	CATPT_PIN_ID_CAPTURE2 = 3,
	CATPT_PIN_ID_OFFLOAD1 = 4,
	CATPT_PIN_ID_OFFLOAD2 = 5,
	CATPT_PIN_ID_MIXER = 7,
	CATPT_PIN_ID_BLUETOOTH_CAPTURE = 8,
	CATPT_PIN_ID_BLUETOOTH_RENDER = 9,
};

enum catpt_path_id {
	CATPT_PATH_SSP0_OUT = 0,
	CATPT_PATH_SSP0_IN = 1,
	CATPT_PATH_SSP1_OUT = 2,
	CATPT_PATH_SSP1_IN = 3,
	/* duplicated audio in capture path */
	CATPT_PATH_SSP0_IN_DUP = 4,
};

enum catpt_stream_type {
	CATPT_STRM_TYPE_RENDER = 0, /* offload */
	CATPT_STRM_TYPE_SYSTEM = 1,
	CATPT_STRM_TYPE_CAPTURE = 2,
	CATPT_STRM_TYPE_LOOPBACK = 3,
	CATPT_STRM_TYPE_BLUETOOTH_RENDER = 4,
	CATPT_STRM_TYPE_BLUETOOTH_CAPTURE = 5,
};

enum catpt_format_id {
	CATPT_FORMAT_PCM = 0,
	CATPT_FORMAT_MP3 = 1,
	CATPT_FORMAT_AAC = 2,
	CATPT_FORMAT_WMA = 3,
};

enum catpt_channel_index {
	CATPT_CHANNEL_LEFT = 0x0,
	CATPT_CHANNEL_CENTER = 0x1,
	CATPT_CHANNEL_RIGHT = 0x2,
	CATPT_CHANNEL_LEFT_SURROUND = 0x3,
	CATPT_CHANNEL_CENTER_SURROUND = 0x3,
	CATPT_CHANNEL_RIGHT_SURROUND = 0x4,
	CATPT_CHANNEL_LFE = 0x7,
	CATPT_CHANNEL_INVALID = 0xF,
};

enum catpt_channel_config {
	CATPT_CHANNEL_CONFIG_MONO	= 0, /* One channel only */
	CATPT_CHANNEL_CONFIG_STEREO	= 1, /* L & R */
	CATPT_CHANNEL_CONFIG_2_POINT_1	= 2, /* L, R & LFE; PCM only */
	CATPT_CHANNEL_CONFIG_3_POINT_0	= 3, /* L, C & R; MP3 & AAC only */
	CATPT_CHANNEL_CONFIG_3_POINT_1	= 4, /* L, C, R & LFE; PCM only */
	CATPT_CHANNEL_CONFIG_QUATRO	= 5, /* L, R, Ls & Rs; PCM only */
	CATPT_CHANNEL_CONFIG_4_POINT_0	= 6, /* L, C, R & Cs; MP3 & AAC only */
	CATPT_CHANNEL_CONFIG_5_POINT_0	= 7, /* L, C, R, Ls & Rs */
	CATPT_CHANNEL_CONFIG_5_POINT_1	= 8, /* L, C, R, Ls, Rs & LFE */
	CATPT_CHANNEL_CONFIG_DUAL_MONO	= 9, /* One channel replicated in two */
	CATPT_CHANNEL_CONFIG_INVALID	= 10,
};

enum catpt_interleaving_style {
	CATPT_INTERLEAVING_PER_CHANNEL	= 0,
	CATPT_INTERLEAVING_PER_SAMPLE	= 1,
};

struct catpt_audio_format {
	u32 sample_rate;
	u32 bit_depth;
	u32 channel_map;
	u32 channel_config;
	u32 interleaving;
	u8 num_channels;
	u8 valid_bit_depth;
	u8 reserved[2];
} __packed;

struct catpt_ring_info {
	u32 page_table_addr;
	u32 num_pages;
	u32 size;
	u32 offset;
	u32 ring_first_page_pfn;
} __packed;

#define CATPT_MODULE_COUNT (CATPT_MODID_LAST + 1)

enum catpt_module_id {
	CATPT_MODID_BASE_FW		= 0x0,
	CATPT_MODID_MP3			= 0x1,
	CATPT_MODID_AAC_5_1		= 0x2,
	CATPT_MODID_AAC_2_0		= 0x3,
	CATPT_MODID_SRC			= 0x4,
	CATPT_MODID_WAVES		= 0x5,
	CATPT_MODID_DOLBY		= 0x6,
	CATPT_MODID_BOOST		= 0x7,
	CATPT_MODID_LPAL		= 0x8,
	CATPT_MODID_DTS			= 0x9,
	CATPT_MODID_PCM_CAPTURE		= 0xA,
	CATPT_MODID_PCM_SYSTEM		= 0xB,
	CATPT_MODID_PCM_REFERENCE	= 0xC,
	CATPT_MODID_PCM			= 0xD, /* offload */
	CATPT_MODID_BLUETOOTH_RENDER	= 0xE,
	CATPT_MODID_BLUETOOTH_CAPTURE	= 0xF,
	CATPT_MODID_LAST		= CATPT_MODID_BLUETOOTH_CAPTURE,
};

struct catpt_module_entry {
	u32 module_id;
	u32 entry_point;
} __packed;

struct catpt_module_map {
	u8 num_entries;
	struct catpt_module_entry entries[];
} __packed;

struct catpt_memory_info {
	u32 offset;
	u32 size;
} __packed;

#define CATPT_CHANNELS_MAX	4
#define CATPT_ALL_CHANNELS_MASK	UINT_MAX

struct catpt_stream_info {
	u32 stream_hw_id;
	u32 reserved;
	u32 read_pos_regaddr;
	u32 pres_pos_regaddr;
	u32 peak_meter_regaddr[CATPT_CHANNELS_MAX];
	u32 volume_regaddr[CATPT_CHANNELS_MAX];
} __packed;

int catpt_ipc_alloc_stream(struct catpt_dev *cdev,
			   enum catpt_path_id path_id,
			   enum catpt_stream_type type,
			   struct catpt_audio_format *afmt,
			   struct catpt_ring_info *rinfo,
			   u8 num_modules,
			   struct catpt_module_entry *modules,
			   struct resource *persistent,
			   struct resource *scratch,
			   struct catpt_stream_info *sinfo);
int catpt_ipc_free_stream(struct catpt_dev *cdev, u8 stream_hw_id);

enum catpt_ssp_iface {
	CATPT_SSP_IFACE_0 = 0,
	CATPT_SSP_IFACE_1 = 1,
	CATPT_SSP_IFACE_LAST = CATPT_SSP_IFACE_1,
};

#define CATPT_SSP_COUNT (CATPT_SSP_IFACE_LAST + 1)

enum catpt_mclk_frequency {
	CATPT_MCLK_OFF = 0,
	CATPT_MCLK_FREQ_6_MHZ = 1,
	CATPT_MCLK_FREQ_21_MHZ = 2,
	CATPT_MCLK_FREQ_24_MHZ = 3,
};

enum catpt_ssp_mode {
	CATPT_SSP_MODE_I2S_CONSUMER = 0,
	CATPT_SSP_MODE_I2S_PROVIDER = 1,
	CATPT_SSP_MODE_TDM_PROVIDER = 2,
};

struct catpt_ssp_device_format {
	u32 iface;
	u32 mclk;
	u32 mode;
	u16 clock_divider;
	u8 channels;
} __packed;

int catpt_ipc_set_device_format(struct catpt_dev *cdev,
				struct catpt_ssp_device_format *devfmt);

enum catpt_dx_state {
	CATPT_DX_STATE_D3 = 3,
};

enum catpt_dx_type {
	CATPT_DX_TYPE_FW_IMAGE = 0,
	CATPT_DX_TYPE_MEMORY_DUMP = 1,
};

struct catpt_save_meminfo {
	u32 offset;
	u32 size;
	u32 source;
} __packed;

#define SAVE_MEMINFO_MAX	14

struct catpt_dx_context {
	u32 num_meminfo;
	struct catpt_save_meminfo meminfo[SAVE_MEMINFO_MAX];
} __packed;

int catpt_ipc_enter_dxstate(struct catpt_dev *cdev, enum catpt_dx_state state,
			    struct catpt_dx_context *context);

struct catpt_mixer_stream_info {
	u32 mixer_hw_id;
	u32 peak_meter_regaddr[CATPT_CHANNELS_MAX];
	u32 volume_regaddr[CATPT_CHANNELS_MAX];
} __packed;

int catpt_ipc_get_mixer_stream_info(struct catpt_dev *cdev,
				    struct catpt_mixer_stream_info *info);

/* STREAM messages */

enum catpt_stream_msg_type {
	CATPT_STRM_RESET_STREAM = 0,
	CATPT_STRM_PAUSE_STREAM = 1,
	CATPT_STRM_RESUME_STREAM = 2,
	CATPT_STRM_STAGE_MESSAGE = 3,
	CATPT_STRM_NOTIFICATION = 4,
};

enum catpt_stage_action {
	CATPT_STG_SET_VOLUME = 1,
	CATPT_STG_SET_WRITE_POSITION = 2,
	CATPT_STG_MUTE_LOOPBACK = 3,
};

union catpt_stream_msg {
	u32 val;
	struct {
		u32 status:5;
		u32 reserved:7;
		u32 stage_action:4;
		u32 stream_hw_id:4;
		u32 stream_msg_type:4;
		u32 global_msg_type:5;
		u32 fw_ready:1;
		u32 done:1;
		u32 busy:1;
	};
} __packed;

#define CATPT_STREAM_MSG(msg_type) \
{ \
	.stream_msg_type = CATPT_STRM_##msg_type, \
	.global_msg_type = CATPT_GLB_STREAM_MESSAGE }
#define CATPT_STAGE_MSG(msg_type) \
{ \
	.stage_action = CATPT_STG_##msg_type, \
	.stream_msg_type = CATPT_STRM_STAGE_MESSAGE, \
	.global_msg_type = CATPT_GLB_STREAM_MESSAGE }

int catpt_ipc_reset_stream(struct catpt_dev *cdev, u8 stream_hw_id);
int catpt_ipc_pause_stream(struct catpt_dev *cdev, u8 stream_hw_id);
int catpt_ipc_resume_stream(struct catpt_dev *cdev, u8 stream_hw_id);

/* STREAM messages - STAGE subtype */

enum catpt_audio_curve_type {
	CATPT_AUDIO_CURVE_NONE = 0,
	CATPT_AUDIO_CURVE_WINDOWS_FADE = 1,
};

int catpt_ipc_set_volume(struct catpt_dev *cdev, u8 stream_hw_id,
			 u32 channel, u32 volume,
			 u32 curve_duration,
			 enum catpt_audio_curve_type curve_type);

int catpt_ipc_set_write_pos(struct catpt_dev *cdev, u8 stream_hw_id,
			    u32 pos, bool eob, bool ll);

int catpt_ipc_mute_loopback(struct catpt_dev *cdev, u8 stream_hw_id, bool mute);

/* NOTIFICATION messages */

enum catpt_notify_reason {
	CATPT_NOTIFY_POSITION_CHANGED = 0,
	CATPT_NOTIFY_GLITCH_OCCURRED = 1,
};

union catpt_notify_msg {
	u32 val;
	struct {
		u32 mailbox_address:29;
		u32 fw_ready:1;
		u32 done:1;
		u32 busy:1;
	};
	struct {
		u32 status:5;
		u32 reserved:7;
		u32 notify_reason:4;
		u32 stream_hw_id:4;
		u32 stream_msg_type:4;
		u32 global_msg_type:5;
		u32 hdr:3; /* fw_ready, done, busy */
	};
} __packed;

#define FW_INFO_SIZE_MAX	100

struct catpt_fw_ready {
	u32 inbox_offset;
	u32 outbox_offset;
	u32 inbox_size;
	u32 outbox_size;
	u32 fw_info_size;
	char fw_info[FW_INFO_SIZE_MAX];
} __packed;

struct catpt_notify_position {
	u32 stream_position;
	u32 fw_cycle_count;
} __packed;

enum catpt_glitch_type {
	CATPT_GLITCH_UNDERRUN = 1,
	CATPT_GLITCH_DECODER_ERROR = 2,
	CATPT_GLITCH_DOUBLED_WRITE_POS = 3,
};

struct catpt_notify_glitch {
	u32 type;
	u64 presentation_pos;
	u32 write_pos;
} __packed;

#endif
