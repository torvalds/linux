/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2015, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _UAPI_KBASE_HWCNT_READER_H_
#define _UAPI_KBASE_HWCNT_READER_H_

#include <stddef.h>
#include <linux/types.h>

/* The ids of ioctl commands. */
#define KBASE_HWCNT_READER 0xBE
#define KBASE_HWCNT_READER_GET_HWVER       _IOR(KBASE_HWCNT_READER, 0x00, __u32)
#define KBASE_HWCNT_READER_GET_BUFFER_SIZE _IOR(KBASE_HWCNT_READER, 0x01, __u32)
#define KBASE_HWCNT_READER_DUMP            _IOW(KBASE_HWCNT_READER, 0x10, __u32)
#define KBASE_HWCNT_READER_CLEAR           _IOW(KBASE_HWCNT_READER, 0x11, __u32)
#define KBASE_HWCNT_READER_GET_BUFFER      _IOC(_IOC_READ, KBASE_HWCNT_READER, 0x20,\
		offsetof(struct kbase_hwcnt_reader_metadata, cycles))
#define KBASE_HWCNT_READER_GET_BUFFER_WITH_CYCLES      _IOR(KBASE_HWCNT_READER, 0x20,\
		struct kbase_hwcnt_reader_metadata)
#define KBASE_HWCNT_READER_PUT_BUFFER      _IOC(_IOC_WRITE, KBASE_HWCNT_READER, 0x21,\
		offsetof(struct kbase_hwcnt_reader_metadata, cycles))
#define KBASE_HWCNT_READER_PUT_BUFFER_WITH_CYCLES      _IOW(KBASE_HWCNT_READER, 0x21,\
		struct kbase_hwcnt_reader_metadata)
#define KBASE_HWCNT_READER_SET_INTERVAL    _IOW(KBASE_HWCNT_READER, 0x30, __u32)
#define KBASE_HWCNT_READER_ENABLE_EVENT    _IOW(KBASE_HWCNT_READER, 0x40, __u32)
#define KBASE_HWCNT_READER_DISABLE_EVENT   _IOW(KBASE_HWCNT_READER, 0x41, __u32)
#define KBASE_HWCNT_READER_GET_API_VERSION _IOW(KBASE_HWCNT_READER, 0xFF, __u32)
#define KBASE_HWCNT_READER_GET_API_VERSION_WITH_FEATURES \
		_IOW(KBASE_HWCNT_READER, 0xFF, \
		     struct kbase_hwcnt_reader_api_version)

/**
 * struct kbase_hwcnt_reader_metadata_cycles - GPU clock cycles
 * @top:           the number of cycles associated with the main clock for the
 *                 GPU
 * @shader_cores:  the cycles that have elapsed on the GPU shader cores
 */
struct kbase_hwcnt_reader_metadata_cycles {
	__u64 top;
	__u64 shader_cores;
};

/**
 * struct kbase_hwcnt_reader_metadata - hwcnt reader sample buffer metadata
 * @timestamp:  time when sample was collected
 * @event_id:   id of an event that triggered sample collection
 * @buffer_idx: position in sampling area where sample buffer was stored
 * @cycles:     the GPU cycles that occurred since the last sample
 */
struct kbase_hwcnt_reader_metadata {
	__u64 timestamp;
	__u32 event_id;
	__u32 buffer_idx;
	struct kbase_hwcnt_reader_metadata_cycles cycles;
};

/**
 * enum base_hwcnt_reader_event - hwcnt dumping events
 * @BASE_HWCNT_READER_EVENT_MANUAL:   manual request for dump
 * @BASE_HWCNT_READER_EVENT_PERIODIC: periodic dump
 * @BASE_HWCNT_READER_EVENT_PREJOB:   prejob dump request
 * @BASE_HWCNT_READER_EVENT_POSTJOB:  postjob dump request
 * @BASE_HWCNT_READER_EVENT_COUNT:    number of supported events
 */
enum base_hwcnt_reader_event {
	BASE_HWCNT_READER_EVENT_MANUAL,
	BASE_HWCNT_READER_EVENT_PERIODIC,
	BASE_HWCNT_READER_EVENT_PREJOB,
	BASE_HWCNT_READER_EVENT_POSTJOB,
	BASE_HWCNT_READER_EVENT_COUNT
};

#define KBASE_HWCNT_READER_API_VERSION_NO_FEATURE (0)
#define KBASE_HWCNT_READER_API_VERSION_FEATURE_CYCLES_TOP (1 << 0)
#define KBASE_HWCNT_READER_API_VERSION_FEATURE_CYCLES_SHADER_CORES (1 << 1)

/**
 * struct kbase_hwcnt_reader_api_version - hwcnt reader API version
 * @version:  API version
 * @features: available features in this API version
 */
struct kbase_hwcnt_reader_api_version {
	__u32 version;
	__u32 features;
};

/** Hardware counters reader API version */
#define PRFCNT_READER_API_VERSION (0)

/**
 * enum prfcnt_list_type - Type of list item
 * @PRFCNT_LIST_TYPE_ENUM:        Enumeration of performance counters.
 * @PRFCNT_LIST_TYPE_REQUEST:     Request for configuration setup.
 * @PRFCNT_LIST_TYPE_SAMPLE_META: Sample metadata.
 */
enum prfcnt_list_type {
	PRFCNT_LIST_TYPE_ENUM,
	PRFCNT_LIST_TYPE_REQUEST,
	PRFCNT_LIST_TYPE_SAMPLE_META,
};

#define FLEX_LIST_TYPE(type, subtype)                                          \
	(__u16)(((type & 0xf) << 12) | (subtype & 0xfff))
#define FLEX_LIST_TYPE_NONE FLEX_LIST_TYPE(0, 0)

#define PRFCNT_ENUM_TYPE_BLOCK FLEX_LIST_TYPE(PRFCNT_LIST_TYPE_ENUM, 0)
#define PRFCNT_ENUM_TYPE_REQUEST FLEX_LIST_TYPE(PRFCNT_LIST_TYPE_ENUM, 1)

#define PRFCNT_REQUEST_TYPE_MODE FLEX_LIST_TYPE(PRFCNT_LIST_TYPE_REQUEST, 0)
#define PRFCNT_REQUEST_TYPE_ENABLE FLEX_LIST_TYPE(PRFCNT_LIST_TYPE_REQUEST, 1)

#define PRFCNT_SAMPLE_META_TYPE_SAMPLE                                         \
	FLEX_LIST_TYPE(PRFCNT_LIST_TYPE_SAMPLE_META, 0)
#define PRFCNT_SAMPLE_META_TYPE_CLOCK                                          \
	FLEX_LIST_TYPE(PRFCNT_LIST_TYPE_SAMPLE_META, 1)
#define PRFCNT_SAMPLE_META_TYPE_BLOCK                                          \
	FLEX_LIST_TYPE(PRFCNT_LIST_TYPE_SAMPLE_META, 2)

/**
 * struct prfcnt_item_header - Header for an item of the list.
 * @item_type:    Type of item.
 * @item_version: Protocol version.
 */
struct prfcnt_item_header {
	__u16 item_type;
	__u16 item_version;
};

/**
 * enum prfcnt_block_type - Type of performance counter block.
 * @PRFCNT_BLOCK_TYPE_FE:          Front End.
 * @PRFCNT_BLOCK_TYPE_TILER:       Tiler.
 * @PRFCNT_BLOCK_TYPE_MEMORY:      Memory System.
 * @PRFCNT_BLOCK_TYPE_SHADER_CORE: Shader Core.
 */
enum prfcnt_block_type {
	PRFCNT_BLOCK_TYPE_FE,
	PRFCNT_BLOCK_TYPE_TILER,
	PRFCNT_BLOCK_TYPE_MEMORY,
	PRFCNT_BLOCK_TYPE_SHADER_CORE,
	PRFCNT_BLOCK_TYPE_RESERVED = 255,
};

/**
 * enum prfcnt_block_set - Type of performance counter block set.
 * @PRFCNT_SET_PRIMARY:   Primary.
 * @PRFCNT_SET_SECONDARY: Secondary.
 * @PRFCNT_SET_TERTIARY:  Tertiary.
 */
enum prfcnt_set {
	PRFCNT_SET_PRIMARY,
	PRFCNT_SET_SECONDARY,
	PRFCNT_SET_TERTIARY,
	PRFCNT_SET_RESERVED = 255,
};

/**
 * struct prfcnt_enum_block_counter - Performance counter block descriptor.
 * @block_type:    Type of performance counter block.
 * @set:           Which SET this represents: primary, secondary or tertiary.
 * @num_instances: How many instances of this block type exist in the hardware.
 * @num_values:    How many entries in the values array there are for samples
 *                 from this block.
 * @pad:           Padding bytes.
 * @counter_mask:  Bitmask that indicates the availability of counters in this
 *                 block.
 */
struct prfcnt_enum_block_counter {
	__u8 block_type;
	__u8 set;
	__u8 num_instances;
	__u8 num_values;
	__u8 pad[4];
	__u64 counter_mask[2];
};

/**
 * struct prfcnt_enum_request - Request descriptor.
 * @request_item_type:       Type of request.
 * @pad:                     Padding bytes.
 * @versions_mask: Bitmask of versions that support this request.
 */
struct prfcnt_enum_request {
	__u16 request_item_type;
	__u16 pad;
	__u32 versions_mask;
};

/**
 * struct prfcnt_enum_item - Performance counter enumeration item.
 * @hdr:           Header describing the type of item in the list.
 * @block_counter: Performance counter block descriptor.
 * @request:       Request descriptor.
 */
struct prfcnt_enum_item {
	struct prfcnt_item_header hdr;
	union {
		struct prfcnt_enum_block_counter block_counter;
		struct prfcnt_enum_request request;
	} u;
};

/**
 * enum prfcnt_mode - Capture mode for counter sampling.
 * @PRFCNT_MODE_MANUAL:   Manual sampling mode.
 * @PRFCNT_MODE_PERIODIC: Periodic sampling mode.
 */
enum prfcnt_mode {
	PRFCNT_MODE_MANUAL,
	PRFCNT_MODE_PERIODIC,
	PRFCNT_MODE_RESERVED = 255,
};

/**
 * struct prfcnt_request_mode - Mode request descriptor.
 * @mode:      Capture mode for the session, either manual or periodic.
 * @pad:       Padding bytes.
 * @period_us: Period in microseconds, for periodic mode.
 */
struct prfcnt_request_mode {
	__u8 mode;
	__u8 pad[7];
	union {
		struct {
			__u64 period_us;
		} periodic;
	} mode_config;
};

/**
 * struct prfcnt_request_enable - Enable request descriptor.
 * @block_type:  Type of performance counter block.
 * @set:         Which SET to use: primary, secondary or tertiary.
 * @pad:         Padding bytes.
 * @enable_mask: Bitmask that indicates which performance counters to enable.
 *               Unavailable counters will be ignored.
 */
struct prfcnt_request_enable {
	__u8 block_type;
	__u8 set;
	__u8 pad[6];
	__u64 enable_mask[2];
};

/**
 * struct prfcnt_request_item - Performance counter request item.
 * @hdr:        Header describing the type of item in the list.
 * @req_mode:   Mode request descriptor.
 * @req_enable: Enable request descriptor.
 */
struct prfcnt_request_item {
	struct prfcnt_item_header hdr;
	union {
		struct prfcnt_request_mode req_mode;
		struct prfcnt_request_enable req_enable;
	} u;
};

/**
 * enum prfcnt_request_type - Type of request descriptor.
 * @PRFCNT_REQUEST_MODE:   Specify the capture mode to be used for the session.
 * @PRFCNT_REQUEST_ENABLE: Specify which performance counters to capture.
 */
enum prfcnt_request_type {
	PRFCNT_REQUEST_MODE,
	PRFCNT_REQUEST_ENABLE,
};

/**
 * struct prfcnt_sample_metadata - Metadata for counter sample data.
 * @timestamp_start: Earliest timestamp that values in this sample represent.
 * @timestamp_end:   Latest timestamp that values in this sample represent.
 * @seq:             Sequence number of this sample. Must match the value from
 *                   GET_SAMPLE.
 * @user_data:       User data provided to HWC_CMD_START or HWC_CMD_SAMPLE_*
 * @flags:           Property flags.
 */
struct prfcnt_sample_metadata {
	__u64 timestamp_start;
	__u64 timestamp_end;
	__u64 seq;
	__u64 user_data;
	__u32 flags;
	__u32 pad;
};

/**
 * struct prfcnt_clock_metadata - Metadata for clock cycles.
 * @num_domains: Number of domains this metadata refers to.
 * @cycles:      Number of cycles elapsed in each counter domain between
 *               timestamp_start and timestamp_end.
 */
struct prfcnt_clock_metadata {
	__u32 num_domains;
	__u32 pad;
	__u64 *cycles;
};

/* This block was powered on for at least some portion of the sample */
#define BLOCK_STATE_ON (1 << 0)
/* This block was powered off for at least some portion of the sample */
#define BLOCK_STATE_OFF (1 << 1)
/* This block was available to this VM for at least some portion of the sample */
#define BLOCK_STATE_AVAILABLE (1 << 2)
/* This block was not available to this VM for at least some portion of the sample
 *  Note that no data is collected when the block is not available to the VM.
 */
#define BLOCK_STATE_UNAVAILABLE (1 << 3)
/* This block was operating in "normal" (non-protected) mode for at least some portion of the sample */
#define BLOCK_STATE_NORMAL (1 << 4)
/* This block was operating in "protected" mode for at least some portion of the sample.
 * Note that no data is collected when the block is in protected mode.
 */
#define BLOCK_STATE_PROTECTED (1 << 5)

/**
 * struct prfcnt_block_metadata - Metadata for counter block.
 * @block_type:    Type of performance counter block.
 * @block_idx:     Index of performance counter block.
 * @set:           Set of performance counter block.
 * @block_state:   Bits set indicate the states which the block is known
 *                 to have operated in during this sample.
 * @values_offset: Offset from the start of the mmapped region, to the values
 *                 for this block. The values themselves are an array of __u64.
 */
struct prfcnt_block_metadata {
	__u8 block_type;
	__u8 block_idx;
	__u8 set;
	__u8 pad_u8;
	__u32 block_state;
	__u32 values_offset;
	__u32 pad_u32;
};

struct prfcnt_metadata {
	struct prfcnt_item_header hdr;
	union {
		struct prfcnt_sample_metadata sample_md;
		struct prfcnt_clock_metadata clock_md;
		struct prfcnt_block_metadata block_md;
	} u;
};

#endif /* _UAPI_KBASE_HWCNT_READER_H_ */

