/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022 Intel Corporation
 */

#ifndef __INCLUDE_SOUND_SOF_IPC4_TOPOLOGY_H__
#define __INCLUDE_SOUND_SOF_IPC4_TOPOLOGY_H__

#include <sound/sof/ipc4/header.h>

#define SOF_IPC4_FW_PAGE_SIZE BIT(12)
#define SOF_IPC4_FW_PAGE(x) ((((x) + BIT(12) - 1) & ~(BIT(12) - 1)) >> 12)
#define SOF_IPC4_FW_ROUNDUP(x) (((x) + BIT(6) - 1) & (~(BIT(6) - 1)))

#define SOF_IPC4_MODULE_LOAD_TYPE		GENMASK(3, 0)
#define SOF_IPC4_MODULE_AUTO_START		BIT(4)
/*
 * Two module schedule domains in fw :
 * LL domain - Low latency domain
 * DP domain - Data processing domain
 * The LL setting should be equal to !DP setting
 */
#define SOF_IPC4_MODULE_LL		BIT(5)
#define SOF_IPC4_MODULE_DP		BIT(6)
#define SOF_IPC4_MODULE_LIB_CODE		BIT(7)
#define SOF_IPC4_MODULE_INIT_CONFIG_MASK	GENMASK(11, 8)

#define SOF_IPC4_MODULE_INIT_CONFIG_TYPE_BASE_CFG		0
#define SOF_IPC4_MODULE_INIT_CONFIG_TYPE_BASE_CFG_WITH_EXT	1

#define SOF_IPC4_MODULE_INSTANCE_LIST_ITEM_SIZE 12
#define SOF_IPC4_PIPELINE_OBJECT_SIZE 448
#define SOF_IPC4_DATA_QUEUE_OBJECT_SIZE 128
#define SOF_IPC4_LL_TASK_OBJECT_SIZE 72
#define SOF_IPC4_DP_TASK_OBJECT_SIZE 104
#define SOF_IPC4_DP_TASK_LIST_SIZE (12 + 8)
#define SOF_IPC4_LL_TASK_LIST_ITEM_SIZE 12
#define SOF_IPC4_FW_MAX_PAGE_COUNT 20
#define SOF_IPC4_FW_MAX_QUEUE_COUNT 8

/* Node index and mask applicable for host copier and ALH/HDA type DAI copiers */
#define SOF_IPC4_NODE_INDEX_MASK	0xFF
#define SOF_IPC4_NODE_INDEX(x)	((x) & SOF_IPC4_NODE_INDEX_MASK)
#define SOF_IPC4_NODE_TYPE(x)  ((x) << 8)
#define SOF_IPC4_GET_NODE_TYPE(node_id) ((node_id) >> 8)

/* Node ID for SSP type DAI copiers */
#define SOF_IPC4_NODE_INDEX_INTEL_SSP(x) (((x) & 0xf) << 4)

/* Node ID for DMIC type DAI copiers */
#define SOF_IPC4_NODE_INDEX_INTEL_DMIC(x) ((x) & 0x7)

#define SOF_IPC4_GAIN_ALL_CHANNELS_MASK 0xffffffff
#define SOF_IPC4_VOL_ZERO_DB	0x7fffffff

#define SOF_IPC4_DMA_DEVICE_MAX_COUNT 16

#define SOF_IPC4_CHAIN_DMA_NODE_ID	0x7fffffff
#define SOF_IPC4_INVALID_NODE_ID	0xffffffff

/* FW requires minimum 2ms DMA buffer size */
#define SOF_IPC4_MIN_DMA_BUFFER_SIZE	2

/*
 * The base of multi-gateways. Multi-gateways addressing starts from
 * ALH_MULTI_GTW_BASE and there are ALH_MULTI_GTW_COUNT multi-sources
 * and ALH_MULTI_GTW_COUNT multi-sinks available.
 * Addressing is continuous from ALH_MULTI_GTW_BASE to
 * ALH_MULTI_GTW_BASE + ALH_MULTI_GTW_COUNT - 1.
 */
#define ALH_MULTI_GTW_BASE	0x50
/* A magic number from FW */
#define ALH_MULTI_GTW_COUNT	8

enum sof_ipc4_copier_module_config_params {
/*
 * Use LARGE_CONFIG_SET to initialize timestamp event. Ipc mailbox must
 * contain properly built CopierConfigTimestampInitData struct.
 */
	SOF_IPC4_COPIER_MODULE_CFG_PARAM_TIMESTAMP_INIT = 1,
/*
 * Use LARGE_CONFIG_SET to initialize copier sink. Ipc mailbox must contain
 * properly built CopierConfigSetSinkFormat struct.
 */
	SOF_IPC4_COPIER_MODULE_CFG_PARAM_SET_SINK_FORMAT,
/*
 * Use LARGE_CONFIG_SET to initialize and enable on Copier data segment
 * event. Ipc mailbox must contain properly built DataSegmentEnabled struct.
 */
	SOF_IPC4_COPIER_MODULE_CFG_PARAM_DATA_SEGMENT_ENABLED,
/*
 * Use LARGE_CONFIG_GET to retrieve Linear Link Position (LLP) value for non
 * HD-A gateways.
 */
	SOF_IPC4_COPIER_MODULE_CFG_PARAM_LLP_READING,
/*
 * Use LARGE_CONFIG_GET to retrieve Linear Link Position (LLP) value for non
 * HD-A gateways and corresponding total processed data
 */
	SOF_IPC4_COPIER_MODULE_CFG_PARAM_LLP_READING_EXTENDED,
/*
 * Use LARGE_CONFIG_SET to setup attenuation on output pins. Data is just uint32_t.
 * note Config is only allowed when output pin is set up for 32bit and source
 * is connected to Gateway
 */
	SOF_IPC4_COPIER_MODULE_CFG_ATTENUATION,
};

struct sof_ipc4_copier_config_set_sink_format {
/* Id of sink */
	u32 sink_id;
/*
 * Input format used by the source
 * attention must be the same as present if already initialized.
 */
	struct sof_ipc4_audio_format source_fmt;
/* Output format used by the sink */
	struct sof_ipc4_audio_format sink_fmt;
} __packed __aligned(4);

/**
 * struct sof_ipc4_pipeline - pipeline config data
 * @priority: Priority of this pipeline
 * @lp_mode: Low power mode
 * @mem_usage: Memory usage
 * @core_id: Target core for the pipeline
 * @state: Pipeline state
 * @use_chain_dma: flag to indicate if the firmware shall use chained DMA
 * @msg: message structure for pipeline
 * @skip_during_fe_trigger: skip triggering this pipeline during the FE DAI trigger
 */
struct sof_ipc4_pipeline {
	uint32_t priority;
	uint32_t lp_mode;
	uint32_t mem_usage;
	uint32_t core_id;
	int state;
	bool use_chain_dma;
	struct sof_ipc4_msg msg;
	bool skip_during_fe_trigger;
};

/**
 * struct sof_ipc4_multi_pipeline_data - multi pipeline trigger IPC data
 * @count: Number of pipelines to be triggered
 * @pipeline_instance_ids: Flexible array of IDs of the pipelines to be triggered
 */
struct ipc4_pipeline_set_state_data {
	u32 count;
	DECLARE_FLEX_ARRAY(u32, pipeline_instance_ids);
} __packed;

/**
 * struct sof_ipc4_pin_format - Module pin format
 * @pin_index: pin index
 * @buffer_size: buffer size in bytes
 * @audio_fmt: audio format for the pin
 *
 * This structure can be used for both output or input pins and the pin_index is relative to the
 * pin type i.e output/input pin
 */
struct sof_ipc4_pin_format {
	u32 pin_index;
	u32 buffer_size;
	struct sof_ipc4_audio_format audio_fmt;
};

/**
 * struct sof_ipc4_available_audio_format - Available audio formats
 * @output_pin_fmts: Available output pin formats
 * @input_pin_fmts: Available input pin formats
 * @num_input_formats: Number of input pin formats
 * @num_output_formats: Number of output pin formats
 */
struct sof_ipc4_available_audio_format {
	struct sof_ipc4_pin_format *output_pin_fmts;
	struct sof_ipc4_pin_format *input_pin_fmts;
	u32 num_input_formats;
	u32 num_output_formats;
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
 * struct sof_ipc4_dma_device_stream_ch_map: abstract representation of
 * channel mapping to DMAs
 * @device: representation of hardware device address or FIFO
 * @channel_mask: channels handled by @device. Channels are expected to be
 * contiguous
 */
struct sof_ipc4_dma_device_stream_ch_map {
	uint32_t device;
	uint32_t channel_mask;
};

/**
 * struct sof_ipc4_dma_stream_ch_map: DMA configuration data
 * @device_count: Number valid items in mapping array
 * @mapping: device address and channel mask
 */
struct sof_ipc4_dma_stream_ch_map {
	uint32_t device_count;
	struct sof_ipc4_dma_device_stream_ch_map mapping[SOF_IPC4_DMA_DEVICE_MAX_COUNT];
} __packed;

#define SOF_IPC4_DMA_METHOD_HDA   1
#define SOF_IPC4_DMA_METHOD_GPDMA 2 /* defined for consistency but not used */

/**
 * struct sof_ipc4_dma_config: DMA configuration
 * @dma_method: HDAudio or GPDMA
 * @pre_allocated_by_host: 1 if host driver allocates DMA channels, 0 otherwise
 * @dma_channel_id: for HDaudio defined as @stream_id - 1
 * @stream_id: HDaudio stream tag
 * @dma_stream_channel_map: array of device/channel mappings
 * @dma_priv_config_size: currently not used
 * @dma_priv_config: currently not used
 */
struct sof_ipc4_dma_config {
	uint8_t dma_method;
	uint8_t pre_allocated_by_host;
	uint16_t rsvd;
	uint32_t dma_channel_id;
	uint32_t stream_id;
	struct sof_ipc4_dma_stream_ch_map dma_stream_channel_map;
	uint32_t dma_priv_config_size;
	uint8_t dma_priv_config[];
} __packed;

#define SOF_IPC4_GTW_DMA_CONFIG_ID 0x1000

/**
 * struct sof_ipc4_dma_config: DMA configuration
 * @type: set to SOF_IPC4_GTW_DMA_CONFIG_ID
 * @length: sizeof(struct sof_ipc4_dma_config) + dma_config.dma_priv_config_size
 * @dma_config: actual DMA configuration
 */
struct sof_ipc4_dma_config_tlv {
	uint32_t type;
	uint32_t length;
	struct sof_ipc4_dma_config dma_config;
} __packed;

/** struct sof_ipc4_alh_configuration_blob: ALH blob
 * @gw_attr: Gateway attributes
 * @alh_cfg: ALH configuration data
 */
struct sof_ipc4_alh_configuration_blob {
	struct sof_ipc4_gtw_attributes gw_attr;
	struct sof_ipc4_dma_stream_ch_map alh_cfg;
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
 * @dma_config_tlv: DMA configuration
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
	struct sof_ipc4_dma_config_tlv dma_config_tlv[SOF_IPC4_DMA_DEVICE_MAX_COUNT];
};

/**
 * struct sof_ipc4_ctrl_value_chan: generic channel mapped value data
 * @channel: Channel ID
 * @value: Value associated with @channel
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
		DECLARE_FLEX_ARRAY(struct sof_ipc4_ctrl_value_chan, chanv);
		DECLARE_FLEX_ARRAY(struct sof_abi_hdr, data);
	};
};

#define SOF_IPC4_SWITCH_CONTROL_PARAM_ID	200
#define SOF_IPC4_ENUM_CONTROL_PARAM_ID		201

/**
 * struct sof_ipc4_control_msg_payload - IPC payload for kcontrol parameters
 * @id: unique id of the control
 * @num_elems: Number of elements in the chanv array
 * @reserved: reserved for future use, must be set to 0
 * @chanv: channel ID and value array
 */
struct sof_ipc4_control_msg_payload {
	uint16_t id;
	uint16_t num_elems;
	uint32_t reserved[4];
	DECLARE_FLEX_ARRAY(struct sof_ipc4_ctrl_value_chan, chanv);
} __packed;

/**
 * struct sof_ipc4_gain_params - IPC gain parameters
 * @channels: Channels
 * @init_val: Initial value
 * @curve_type: Curve type
 * @reserved: reserved for future use
 * @curve_duration_l: Curve duration low part
 * @curve_duration_h: Curve duration high part
 */
struct sof_ipc4_gain_params {
	uint32_t channels;
	uint32_t init_val;
	uint32_t curve_type;
	uint32_t reserved;
	uint32_t curve_duration_l;
	uint32_t curve_duration_h;
} __packed __aligned(4);

/**
 * struct sof_ipc4_gain_data - IPC gain init blob
 * @base_config: IPC base config data
 * @params: Initial parameters for the gain module
 */
struct sof_ipc4_gain_data {
	struct sof_ipc4_base_module_cfg base_config;
	struct sof_ipc4_gain_params params;
} __packed __aligned(4);

/**
 * struct sof_ipc4_gain - gain config data
 * @data: IPC gain blob
 * @available_fmt: Available audio format
 * @msg: message structure for gain
 */
struct sof_ipc4_gain {
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

/*
 * struct sof_ipc4_src_data - IPC data for SRC
 * @base_config: IPC base config data
 * @sink_rate: Output rate for sink module
 */
struct sof_ipc4_src_data {
	struct sof_ipc4_base_module_cfg base_config;
	uint32_t sink_rate;
} __packed __aligned(4);

/**
 * struct sof_ipc4_src - SRC config data
 * @data: IPC base config data
 * @available_fmt: Available audio format
 * @msg: IPC4 message struct containing header and data info
 */
struct sof_ipc4_src {
	struct sof_ipc4_src_data data;
	struct sof_ipc4_available_audio_format available_fmt;
	struct sof_ipc4_msg msg;
};

/*
 * struct sof_ipc4_asrc_data - IPC data for ASRC
 * @base_config: IPC base config data
 * @out_freq: Output rate for sink module, passed as such from topology to FW.
 * @asrc_mode: Control for ASRC features with bit-fields, passed as such from topolgy to FW.
 */
struct sof_ipc4_asrc_data {
	struct sof_ipc4_base_module_cfg base_config;
	uint32_t out_freq;
	uint32_t asrc_mode;
} __packed __aligned(4);

/**
 * struct sof_ipc4_asrc - ASRC config data
 * @data: IPC base config data
 * @available_fmt: Available audio format
 * @msg: IPC4 message struct containing header and data info
 */
struct sof_ipc4_asrc {
	struct sof_ipc4_asrc_data data;
	struct sof_ipc4_available_audio_format available_fmt;
	struct sof_ipc4_msg msg;
};

/**
 * struct sof_ipc4_base_module_cfg_ext - base module config extension containing the pin format
 * information for the module. Both @num_input_pin_fmts and @num_output_pin_fmts cannot be 0 for a
 * module.
 * @num_input_pin_fmts: number of input pin formats in the @pin_formats array
 * @num_output_pin_fmts: number of output pin formats in the @pin_formats array
 * @reserved: reserved for future use
 * @pin_formats: flexible array consisting of @num_input_pin_fmts input pin format items followed
 *		 by @num_output_pin_fmts output pin format items
 */
struct sof_ipc4_base_module_cfg_ext {
	u16 num_input_pin_fmts;
	u16 num_output_pin_fmts;
	u8 reserved[12];
	DECLARE_FLEX_ARRAY(struct sof_ipc4_pin_format, pin_formats);
} __packed;

/**
 * struct sof_ipc4_process - process config data
 * @base_config: IPC base config data
 * @base_config_ext: Base config extension data for module init
 * @output_format: Output audio format
 * @available_fmt: Available audio format
 * @ipc_config_data: Process module config data
 * @ipc_config_size: Size of process module config data
 * @msg: IPC4 message struct containing header and data info
 * @base_config_ext_size: Size of the base config extension data in bytes
 * @init_config: Module init config type (SOF_IPC4_MODULE_INIT_CONFIG_TYPE_*)
 */
struct sof_ipc4_process {
	struct sof_ipc4_base_module_cfg base_config;
	struct sof_ipc4_base_module_cfg_ext *base_config_ext;
	struct sof_ipc4_audio_format output_format;
	struct sof_ipc4_available_audio_format available_fmt;
	void *ipc_config_data;
	uint32_t ipc_config_size;
	struct sof_ipc4_msg msg;
	u32 base_config_ext_size;
	u32 init_config;
};

bool sof_ipc4_copier_is_single_bitdepth(struct snd_sof_dev *sdev,
					struct sof_ipc4_pin_format *pin_fmts,
					u32 pin_fmts_size);
#endif
