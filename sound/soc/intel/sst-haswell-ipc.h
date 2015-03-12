/*
 * Intel SST Haswell/Broadwell IPC Support
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __SST_HASWELL_IPC_H
#define __SST_HASWELL_IPC_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <sound/asound.h>

#define SST_HSW_NO_CHANNELS		4
#define SST_HSW_MAX_DX_REGIONS		14
#define SST_HSW_DX_CONTEXT_SIZE        (640 * 1024)
#define SST_HSW_CHANNELS_ALL		0xffffffff

#define SST_HSW_FW_LOG_CONFIG_DWORDS	12
#define SST_HSW_GLOBAL_LOG		15

/**
 * Upfront defined maximum message size that is
 * expected by the in/out communication pipes in FW.
 */
#define SST_HSW_IPC_MAX_PAYLOAD_SIZE	400
#define SST_HSW_MAX_INFO_SIZE		64
#define SST_HSW_BUILD_HASH_LENGTH	40

struct sst_hsw;
struct sst_hsw_stream;
struct sst_hsw_log_stream;
struct sst_pdata;
struct sst_module;
struct sst_module_runtime;
extern struct sst_ops haswell_ops;

/* Stream Allocate Path ID */
enum sst_hsw_stream_path_id {
	SST_HSW_STREAM_PATH_SSP0_OUT = 0,
	SST_HSW_STREAM_PATH_SSP0_IN = 1,
	SST_HSW_STREAM_PATH_MAX_PATH_ID = 2,
};

/* Stream Allocate Stream Type */
enum sst_hsw_stream_type {
	SST_HSW_STREAM_TYPE_RENDER = 0,
	SST_HSW_STREAM_TYPE_SYSTEM = 1,
	SST_HSW_STREAM_TYPE_CAPTURE = 2,
	SST_HSW_STREAM_TYPE_LOOPBACK = 3,
	SST_HSW_STREAM_TYPE_MAX_STREAM_TYPE = 4,
};

/* Stream Allocate Stream Format */
enum sst_hsw_stream_format {
	SST_HSW_STREAM_FORMAT_PCM_FORMAT = 0,
	SST_HSW_STREAM_FORMAT_MP3_FORMAT = 1,
	SST_HSW_STREAM_FORMAT_AAC_FORMAT = 2,
	SST_HSW_STREAM_FORMAT_MAX_FORMAT_ID = 3,
};

/* Device ID */
enum sst_hsw_device_id {
	SST_HSW_DEVICE_SSP_0   = 0,
	SST_HSW_DEVICE_SSP_1   = 1,
};

/* Device Master Clock Frequency */
enum sst_hsw_device_mclk {
	SST_HSW_DEVICE_MCLK_OFF         = 0,
	SST_HSW_DEVICE_MCLK_FREQ_6_MHZ  = 1,
	SST_HSW_DEVICE_MCLK_FREQ_12_MHZ = 2,
	SST_HSW_DEVICE_MCLK_FREQ_24_MHZ = 3,
};

/* Device Clock Master */
enum sst_hsw_device_mode {
	SST_HSW_DEVICE_CLOCK_SLAVE   = 0,
	SST_HSW_DEVICE_CLOCK_MASTER  = 1,
	SST_HSW_DEVICE_TDM_CLOCK_MASTER = 2,
};

/* DX Power State */
enum sst_hsw_dx_state {
	SST_HSW_DX_STATE_D0     = 0,
	SST_HSW_DX_STATE_D1     = 1,
	SST_HSW_DX_STATE_D3     = 3,
	SST_HSW_DX_STATE_MAX	= 3,
};

/* Audio stream stage IDs */
enum sst_hsw_fx_stage_id {
	SST_HSW_STAGE_ID_WAVES = 0,
	SST_HSW_STAGE_ID_DTS   = 1,
	SST_HSW_STAGE_ID_DOLBY = 2,
	SST_HSW_STAGE_ID_BOOST = 3,
	SST_HSW_STAGE_ID_MAX_FX_ID
};

/* DX State Type */
enum sst_hsw_dx_type {
	SST_HSW_DX_TYPE_FW_IMAGE = 0,
	SST_HSW_DX_TYPE_MEMORY_DUMP = 1
};

/* Volume Curve Type*/
enum sst_hsw_volume_curve {
	SST_HSW_VOLUME_CURVE_NONE = 0,
	SST_HSW_VOLUME_CURVE_FADE = 1
};

/* Sample ordering */
enum sst_hsw_interleaving {
	SST_HSW_INTERLEAVING_PER_CHANNEL = 0,
	SST_HSW_INTERLEAVING_PER_SAMPLE  = 1,
};

/* Channel indices */
enum sst_hsw_channel_index {
	SST_HSW_CHANNEL_LEFT            = 0,
	SST_HSW_CHANNEL_CENTER          = 1,
	SST_HSW_CHANNEL_RIGHT           = 2,
	SST_HSW_CHANNEL_LEFT_SURROUND   = 3,
	SST_HSW_CHANNEL_CENTER_SURROUND = 3,
	SST_HSW_CHANNEL_RIGHT_SURROUND  = 4,
	SST_HSW_CHANNEL_LFE             = 7,
	SST_HSW_CHANNEL_INVALID         = 0xF,
};

/* List of supported channel maps. */
enum sst_hsw_channel_config {
	SST_HSW_CHANNEL_CONFIG_MONO      = 0, /* mono only. */
	SST_HSW_CHANNEL_CONFIG_STEREO    = 1, /* L & R. */
	SST_HSW_CHANNEL_CONFIG_2_POINT_1 = 2, /* L, R & LFE; PCM only. */
	SST_HSW_CHANNEL_CONFIG_3_POINT_0 = 3, /* L, C & R; MP3 & AAC only. */
	SST_HSW_CHANNEL_CONFIG_3_POINT_1 = 4, /* L, C, R & LFE; PCM only. */
	SST_HSW_CHANNEL_CONFIG_QUATRO    = 5, /* L, R, Ls & Rs; PCM only. */
	SST_HSW_CHANNEL_CONFIG_4_POINT_0 = 6, /* L, C, R & Cs; MP3 & AAC only. */
	SST_HSW_CHANNEL_CONFIG_5_POINT_0 = 7, /* L, C, R, Ls & Rs. */
	SST_HSW_CHANNEL_CONFIG_5_POINT_1 = 8, /* L, C, R, Ls, Rs & LFE. */
	SST_HSW_CHANNEL_CONFIG_DUAL_MONO = 9, /* One channel replicated in two. */
	SST_HSW_CHANNEL_CONFIG_INVALID,
};

/* List of supported bit depths. */
enum sst_hsw_bitdepth {
	SST_HSW_DEPTH_8BIT  = 8,
	SST_HSW_DEPTH_16BIT = 16,
	SST_HSW_DEPTH_24BIT = 24, /* Default. */
	SST_HSW_DEPTH_32BIT = 32,
	SST_HSW_DEPTH_INVALID = 33,
};

enum sst_hsw_module_id {
	SST_HSW_MODULE_BASE_FW = 0x0,
	SST_HSW_MODULE_MP3     = 0x1,
	SST_HSW_MODULE_AAC_5_1 = 0x2,
	SST_HSW_MODULE_AAC_2_0 = 0x3,
	SST_HSW_MODULE_SRC     = 0x4,
	SST_HSW_MODULE_WAVES   = 0x5,
	SST_HSW_MODULE_DOLBY   = 0x6,
	SST_HSW_MODULE_BOOST   = 0x7,
	SST_HSW_MODULE_LPAL    = 0x8,
	SST_HSW_MODULE_DTS     = 0x9,
	SST_HSW_MODULE_PCM_CAPTURE = 0xA,
	SST_HSW_MODULE_PCM_SYSTEM = 0xB,
	SST_HSW_MODULE_PCM_REFERENCE = 0xC,
	SST_HSW_MODULE_PCM = 0xD,
	SST_HSW_MODULE_BLUETOOTH_RENDER_MODULE = 0xE,
	SST_HSW_MODULE_BLUETOOTH_CAPTURE_MODULE = 0xF,
	SST_HSW_MAX_MODULE_ID,
};

enum sst_hsw_performance_action {
	SST_HSW_PERF_START = 0,
	SST_HSW_PERF_STOP = 1,
};

/* SST firmware module info */
struct sst_hsw_module_info {
	u8 name[SST_HSW_MAX_INFO_SIZE];
	u8 version[SST_HSW_MAX_INFO_SIZE];
} __attribute__((packed));

/* Module entry point */
struct sst_hsw_module_entry {
	enum sst_hsw_module_id module_id;
	u32 entry_point;
} __attribute__((packed));

/* Module map - alignement matches DSP */
struct sst_hsw_module_map {
	u8 module_entries_count;
	struct sst_hsw_module_entry module_entries[1];
} __attribute__((packed));

struct sst_hsw_memory_info {
	u32 offset;
	u32 size;
} __attribute__((packed));

struct sst_hsw_fx_enable {
	struct sst_hsw_module_map module_map;
	struct sst_hsw_memory_info persistent_mem;
} __attribute__((packed));

struct sst_hsw_ipc_module_config {
	struct sst_hsw_module_map map;
	struct sst_hsw_memory_info persistent_mem;
	struct sst_hsw_memory_info scratch_mem;
} __attribute__((packed));

struct sst_hsw_get_fx_param {
	u32 parameter_id;
	u32 param_size;
} __attribute__((packed));

struct sst_hsw_perf_action {
	u32 action;
} __attribute__((packed));

struct sst_hsw_perf_data {
	u64 timestamp;
	u64 cycles;
	u64 datatime;
} __attribute__((packed));

/* FW version */
struct sst_hsw_ipc_fw_version {
	u8 build;
	u8 minor;
	u8 major;
	u8 type;
	u8 fw_build_hash[SST_HSW_BUILD_HASH_LENGTH];
	u32 fw_log_providers_hash;
} __attribute__((packed));

/* Stream ring info */
struct sst_hsw_ipc_stream_ring {
	u32 ring_pt_address;
	u32 num_pages;
	u32 ring_size;
	u32 ring_offset;
	u32 ring_first_pfn;
} __attribute__((packed));

/* Debug Dump Log Enable Request */
struct sst_hsw_ipc_debug_log_enable_req {
	struct sst_hsw_ipc_stream_ring ringinfo;
	u32 config[SST_HSW_FW_LOG_CONFIG_DWORDS];
} __attribute__((packed));

/* Debug Dump Log Reply */
struct sst_hsw_ipc_debug_log_reply {
	u32 log_buffer_begining;
	u32 log_buffer_size;
} __attribute__((packed));

/* Stream glitch position */
struct sst_hsw_ipc_stream_glitch_position {
	u32 glitch_type;
	u32 present_pos;
	u32 write_pos;
} __attribute__((packed));

/* Stream get position */
struct sst_hsw_ipc_stream_get_position {
	u32 position;
	u32 fw_cycle_count;
} __attribute__((packed));

/* Stream set position */
struct sst_hsw_ipc_stream_set_position {
	u32 position;
	u32 end_of_buffer;
} __attribute__((packed));

/* Stream Free Request */
struct sst_hsw_ipc_stream_free_req {
	u8 stream_id;
	u8 reserved[3];
} __attribute__((packed));

/* Set Volume Request */
struct sst_hsw_ipc_volume_req {
	u32 channel;
	u32 target_volume;
	u64 curve_duration;
	u32 curve_type;
} __attribute__((packed));

/* Device Configuration Request */
struct sst_hsw_ipc_device_config_req {
	u32 ssp_interface;
	u32 clock_frequency;
	u32 mode;
	u16 clock_divider;
	u8 channels;
	u8 reserved;
} __attribute__((packed));

/* Audio Data formats */
struct sst_hsw_audio_data_format_ipc {
	u32 frequency;
	u32 bitdepth;
	u32 map;
	u32 config;
	u32 style;
	u8 ch_num;
	u8 valid_bit;
	u8 reserved[2];
} __attribute__((packed));

/* Stream Allocate Request */
struct sst_hsw_ipc_stream_alloc_req {
	u8 path_id;
	u8 stream_type;
	u8 format_id;
	u8 reserved;
	struct sst_hsw_audio_data_format_ipc format;
	struct sst_hsw_ipc_stream_ring ringinfo;
	struct sst_hsw_module_map map;
	struct sst_hsw_memory_info persistent_mem;
	struct sst_hsw_memory_info scratch_mem;
	u32 number_of_notifications;
} __attribute__((packed));

/* Stream Allocate Reply */
struct sst_hsw_ipc_stream_alloc_reply {
	u32 stream_hw_id;
	u32 mixer_hw_id; // returns rate ????
	u32 read_position_register_address;
	u32 presentation_position_register_address;
	u32 peak_meter_register_address[SST_HSW_NO_CHANNELS];
	u32 volume_register_address[SST_HSW_NO_CHANNELS];
} __attribute__((packed));

/* Get Mixer Stream Info */
struct sst_hsw_ipc_stream_info_reply {
	u32 mixer_hw_id;
	u32 peak_meter_register_address[SST_HSW_NO_CHANNELS];
	u32 volume_register_address[SST_HSW_NO_CHANNELS];
} __attribute__((packed));

/* DX State Request */
struct sst_hsw_ipc_dx_req {
	u8 state;
	u8 reserved[3];
} __attribute__((packed));

/* DX State Reply Memory Info Item */
struct sst_hsw_ipc_dx_memory_item {
	u32 offset;
	u32 size;
	u32 source;
} __attribute__((packed));

/* DX State Reply */
struct sst_hsw_ipc_dx_reply {
	u32 entries_no;
	struct sst_hsw_ipc_dx_memory_item mem_info[SST_HSW_MAX_DX_REGIONS];
} __attribute__((packed));

struct sst_hsw_ipc_fw_version;

/* SST Init & Free */
struct sst_hsw *sst_hsw_new(struct device *dev, const u8 *fw, size_t fw_length,
	u32 fw_offset);
void sst_hsw_free(struct sst_hsw *hsw);
int sst_hsw_fw_get_version(struct sst_hsw *hsw,
	struct sst_hsw_ipc_fw_version *version);
u32 create_channel_map(enum sst_hsw_channel_config config);

/* Stream Mixer Controls - */
int sst_hsw_stream_set_volume(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u32 stage_id, u32 channel, u32 volume);
int sst_hsw_stream_get_volume(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u32 stage_id, u32 channel, u32 *volume);

/* Global Mixer Controls - */
int sst_hsw_mixer_set_volume(struct sst_hsw *hsw, u32 stage_id, u32 channel,
	u32 volume);
int sst_hsw_mixer_get_volume(struct sst_hsw *hsw, u32 stage_id, u32 channel,
	u32 *volume);

/* Stream API */
struct sst_hsw_stream *sst_hsw_stream_new(struct sst_hsw *hsw, int id,
	u32 (*get_write_position)(struct sst_hsw_stream *stream, void *data),
	void *data);

int sst_hsw_stream_free(struct sst_hsw *hsw, struct sst_hsw_stream *stream);

/* Stream Configuration */
int sst_hsw_stream_format(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum sst_hsw_stream_path_id path_id,
	enum sst_hsw_stream_type stream_type,
	enum sst_hsw_stream_format format_id);

int sst_hsw_stream_buffer(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 ring_pt_address, u32 num_pages,
	u32 ring_size, u32 ring_offset, u32 ring_first_pfn);

int sst_hsw_stream_commit(struct sst_hsw *hsw, struct sst_hsw_stream *stream);

int sst_hsw_stream_set_valid(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 bits);
int sst_hsw_stream_set_rate(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	int rate);
int sst_hsw_stream_set_bits(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum sst_hsw_bitdepth bits);
int sst_hsw_stream_set_channels(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, int channels);
int sst_hsw_stream_set_map_config(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u32 map,
	enum sst_hsw_channel_config config);
int sst_hsw_stream_set_style(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum sst_hsw_interleaving style);
int sst_hsw_stream_set_module_info(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, struct sst_module_runtime *runtime);
int sst_hsw_stream_set_pmemory_info(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u32 offset, u32 size);
int sst_hsw_stream_set_smemory_info(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u32 offset, u32 size);
snd_pcm_uframes_t sst_hsw_stream_get_old_position(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream);
void sst_hsw_stream_set_old_position(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, snd_pcm_uframes_t val);
bool sst_hsw_stream_get_silence_start(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream);
void sst_hsw_stream_set_silence_start(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, bool val);
int sst_hsw_mixer_get_info(struct sst_hsw *hsw);

/* Stream ALSA trigger operations */
int sst_hsw_stream_pause(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	int wait);
int sst_hsw_stream_resume(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	int wait);
int sst_hsw_stream_reset(struct sst_hsw *hsw, struct sst_hsw_stream *stream);

/* Stream pointer positions */
int sst_hsw_stream_get_read_pos(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u32 *position);
int sst_hsw_stream_get_write_pos(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u32 *position);
u32 sst_hsw_get_dsp_position(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream);
u64 sst_hsw_get_dsp_presentation_position(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream);

/* HW port config */
int sst_hsw_device_set_config(struct sst_hsw *hsw,
	enum sst_hsw_device_id dev, enum sst_hsw_device_mclk mclk,
	enum sst_hsw_device_mode mode, u32 clock_divider);

/* DX Config */
int sst_hsw_dx_set_state(struct sst_hsw *hsw,
	enum sst_hsw_dx_state state, struct sst_hsw_ipc_dx_reply *dx);

/* init */
int sst_hsw_dsp_init(struct device *dev, struct sst_pdata *pdata);
void sst_hsw_dsp_free(struct device *dev, struct sst_pdata *pdata);
struct sst_dsp *sst_hsw_get_dsp(struct sst_hsw *hsw);

/* fw module function */
void sst_hsw_init_module_state(struct sst_hsw *hsw);
bool sst_hsw_is_module_loaded(struct sst_hsw *hsw, u32 module_id);
bool sst_hsw_is_module_active(struct sst_hsw *hsw, u32 module_id);
void sst_hsw_set_module_enabled_rtd3(struct sst_hsw *hsw, u32 module_id);
void sst_hsw_set_module_disabled_rtd3(struct sst_hsw *hsw, u32 module_id);
bool sst_hsw_is_module_enabled_rtd3(struct sst_hsw *hsw, u32 module_id);

int sst_hsw_module_load(struct sst_hsw *hsw,
	u32 module_id, u32 instance_id, char *name);
int sst_hsw_module_enable(struct sst_hsw *hsw,
	u32 module_id, u32 instance_id);
int sst_hsw_module_disable(struct sst_hsw *hsw,
	u32 module_id, u32 instance_id);

/* runtime module management */
struct sst_module_runtime *sst_hsw_runtime_module_create(struct sst_hsw *hsw,
	int mod_id, int offset);
void sst_hsw_runtime_module_free(struct sst_module_runtime *runtime);

/* PM */
int sst_hsw_dsp_runtime_resume(struct sst_hsw *hsw);
int sst_hsw_dsp_runtime_suspend(struct sst_hsw *hsw);
int sst_hsw_dsp_load(struct sst_hsw *hsw);
int sst_hsw_dsp_runtime_sleep(struct sst_hsw *hsw);

#endif
