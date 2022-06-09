/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_SOUND_SOF_IPC4_TOPOLOGY_H__
#define __INCLUDE_SOUND_SOF_IPC4_TOPOLOGY_H__

#include <sound/sof/ipc4/header.h>

#define SOF_IPC4_FW_PAGE_SIZE BIT(12)
#define SOF_IPC4_FW_PAGE(x) ((((x) + BIT(12) - 1) & ~(BIT(12) - 1)) >> 12)
#define SOF_IPC4_FW_ROUNDUP(x) (((x) + BIT(6) - 1) & (~(BIT(6) - 1)))

#define SOF_IPC4_MODULE_LL      BIT(5)
#define SOF_IPC4_MODULE_INSTANCE_LIST_ITEM_SIZE 12
#define SOF_IPC4_PIPELINE_OBJECT_SIZE 448
#define SOF_IPC4_DATA_QUEUE_OBJECT_SIZE 128
#define SOF_IPC4_LL_TASK_OBJECT_SIZE 72
#define SOF_IPC4_DP_TASK_OBJECT_SIZE 104
#define SOF_IPC4_DP_TASK_LIST_SIZE (12 + 8)
#define SOF_IPC4_LL_TASK_LIST_ITEM_SIZE 12
#define SOF_IPC4_FW_MAX_PAGE_COUNT 20
#define SOF_IPC4_FW_MAX_QUEUE_COUNT 8

/* Node index and mask applicable for host copier */
#define SOF_IPC4_NODE_INDEX_MASK	0xFF
#define SOF_IPC4_NODE_INDEX(x)	((x) & SOF_IPC4_NODE_INDEX_MASK)
#define SOF_IPC4_NODE_TYPE(x)  ((x) << 8)

#define SOF_IPC4_GAIN_ALL_CHANNELS_MASK 0xffffffff
#define SOF_IPC4_VOL_ZERO_DB	0x7fffffff

/**
 * struct sof_ipc4_pipeline - pipeline config data
 * @priority: Priority of this pipeline
 * @lp_mode: Low power mode
 * @mem_usage: Memory usage
 * @state: Pipeline state
 * @msg: message structure for pipeline
 */
struct sof_ipc4_pipeline {
	uint32_t priority;
	uint32_t lp_mode;
	uint32_t mem_usage;
	int state;
	struct sof_ipc4_msg msg;
};

/**
 * struct sof_ipc4_available_audio_format - Available audio formats
 * @base_config: Available base config
 * @out_audio_fmt: Available output audio format
 * @ref_audio_fmt: Reference audio format to match runtime audio format
 * @dma_buffer_size: Available Gateway DMA buffer size (in bytes)
 * @audio_fmt_num: Number of available audio formats
 */
struct sof_ipc4_available_audio_format {
	struct sof_ipc4_base_module_cfg *base_config;
	struct sof_ipc4_audio_format *out_audio_fmt;
	struct sof_ipc4_audio_format *ref_audio_fmt;
	u32 *dma_buffer_size;
	int audio_fmt_num;
};

/**
 * struct sof_copier_gateway_cfg - IPC gateway configuration
 * @node_id: ID of Gateway Node
 * @dma_buffer_size: Preferred Gateway DMA buffer size (in bytes)
 * @config_length: Length of gateway node configuration blob specified in #config_data
 * config_data: Gateway node configuration blob
 */
struct sof_copier_gateway_cfg {
	uint32_t node_id;
	uint32_t dma_buffer_size;
	uint32_t config_length;
	uint32_t config_data[];
};

/**
 * struct sof_ipc4_copier_data - IPC data for copier
 * @base_config: Base configuration including input audio format
 * @out_format: Output audio format
 * @copier_feature_mask: Copier feature mask
 * @gtw_cfg: Gateway configuration
 */
struct sof_ipc4_copier_data {
	struct sof_ipc4_base_module_cfg base_config;
	struct sof_ipc4_audio_format out_format;
	uint32_t copier_feature_mask;
	struct sof_copier_gateway_cfg gtw_cfg;
};

/**
 * struct sof_ipc4_gtw_attributes: Gateway attributes
 * @lp_buffer_alloc: Gateway data requested in low power memory
 * @alloc_from_reg_file: Gateway data requested in register file memory
 * @rsvd: reserved for future use
 */
struct sof_ipc4_gtw_attributes {
	uint32_t lp_buffer_alloc : 1;
	uint32_t alloc_from_reg_file : 1;
	uint32_t rsvd : 30;
};

/**
 * struct sof_ipc4_copier - copier config data
 * @data: IPC copier data
 * @copier_config: Copier + blob
 * @ipc_config_size: Size of copier_config
 * @available_fmt: Available audio format
 * @frame_fmt: frame format
 * @msg: message structure for copier
 * @gtw_attr: Gateway attributes for copier blob
 * @dai_type: DAI type
 * @dai_index: DAI index
 */
struct sof_ipc4_copier {
	struct sof_ipc4_copier_data data;
	u32 *copier_config;
	uint32_t ipc_config_size;
	void *ipc_config_data;
	struct sof_ipc4_available_audio_format available_fmt;
	u32 frame_fmt;
	struct sof_ipc4_msg msg;
	struct sof_ipc4_gtw_attributes *gtw_attr;
	u32 dai_type;
	int dai_index;
};

/**
 * struct sof_ipc4_ctrl_value_chan: generic channel mapped value data
 * @channel: Channel ID
 * @value: gain value
 */
struct sof_ipc4_ctrl_value_chan {
	u32 channel;
	u32 value;
};

/**
 * struct sof_ipc4_control_data - IPC data for kcontrol IO
 * @msg: message structure for kcontrol IO
 * @index: pipeline ID
 * @chanv: channel ID and value array used by volume type controls
 * @data: data for binary kcontrols
 */
struct sof_ipc4_control_data {
	struct sof_ipc4_msg msg;
	int index;

	union {
		struct sof_ipc4_ctrl_value_chan chanv[0];
		struct sof_abi_hdr data[0];
	};
};

/**
 * struct sof_ipc4_gain_data - IPC gain blob
 * @channels: Channels
 * @init_val: Initial value
 * @curve_type: Curve type
 * @reserved: reserved for future use
 * @curve_duration: Curve duration
 */
struct sof_ipc4_gain_data {
	uint32_t channels;
	uint32_t init_val;
	uint32_t curve_type;
	uint32_t reserved;
	uint32_t curve_duration;
} __aligned(8);

/**
 * struct sof_ipc4_gain - gain config data
 * @base_config: IPC base config data
 * @data: IPC gain blob
 * @available_fmt: Available audio format
 * @msg: message structure for gain
 */
struct sof_ipc4_gain {
	struct sof_ipc4_base_module_cfg base_config;
	struct sof_ipc4_gain_data data;
	struct sof_ipc4_available_audio_format available_fmt;
	struct sof_ipc4_msg msg;
};

/**
 * struct sof_ipc4_mixer - mixer config data
 * @base_config: IPC base config data
 * @available_fmt: Available audio format
 * @msg: IPC4 message struct containing header and data info
 */
struct sof_ipc4_mixer {
	struct sof_ipc4_base_module_cfg base_config;
	struct sof_ipc4_available_audio_format available_fmt;
	struct sof_ipc4_msg msg;
};

#endif
