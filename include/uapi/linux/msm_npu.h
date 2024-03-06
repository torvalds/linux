/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2021-2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UAPI_MSM_NPU_H_
#define _UAPI_MSM_NPU_H_

/* -------------------------------------------------------------------------
 * Includes
 * -------------------------------------------------------------------------
 */
#include <linux/types.h>

/* -------------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------------
 */
#define MSM_NPU_IOCTL_MAGIC 'n'

/* get npu info */
#define MSM_NPU_GET_INFO \
	_IOWR(MSM_NPU_IOCTL_MAGIC, 1, struct msm_npu_get_info_ioctl)

/* map buf */
#define MSM_NPU_MAP_BUF \
	_IOWR(MSM_NPU_IOCTL_MAGIC, 2, struct msm_npu_map_buf_ioctl)

/* map buf */
#define MSM_NPU_UNMAP_BUF \
	_IOWR(MSM_NPU_IOCTL_MAGIC, 3, struct msm_npu_unmap_buf_ioctl)

/* load network */
#define MSM_NPU_LOAD_NETWORK \
	_IOWR(MSM_NPU_IOCTL_MAGIC, 4, struct msm_npu_load_network_ioctl)

/* unload network */
#define MSM_NPU_UNLOAD_NETWORK \
	_IOWR(MSM_NPU_IOCTL_MAGIC, 5, struct msm_npu_unload_network_ioctl)

/* exec network */
#define MSM_NPU_EXEC_NETWORK \
	_IOWR(MSM_NPU_IOCTL_MAGIC, 6, struct msm_npu_exec_network_ioctl)

/* load network v2 */
#define MSM_NPU_LOAD_NETWORK_V2 \
	_IOWR(MSM_NPU_IOCTL_MAGIC, 7, struct msm_npu_load_network_ioctl_v2)

/* exec network v2 */
#define MSM_NPU_EXEC_NETWORK_V2 \
	_IOWR(MSM_NPU_IOCTL_MAGIC, 8, struct msm_npu_exec_network_ioctl_v2)

/* receive event */
#define MSM_NPU_RECEIVE_EVENT \
	_IOR(MSM_NPU_IOCTL_MAGIC, 9, struct msm_npu_event)

/* set property */
#define MSM_NPU_SET_PROP \
	_IOW(MSM_NPU_IOCTL_MAGIC, 10, struct msm_npu_property)

/* get property */
#define MSM_NPU_GET_PROP \
	_IOW(MSM_NPU_IOCTL_MAGIC, 11, struct msm_npu_property)

#define MSM_NPU_EVENT_TYPE_START 0x10000000
#define MSM_NPU_EVENT_TYPE_EXEC_DONE (MSM_NPU_EVENT_TYPE_START + 1)
#define MSM_NPU_EVENT_TYPE_EXEC_V2_DONE (MSM_NPU_EVENT_TYPE_START + 2)
#define MSM_NPU_EVENT_TYPE_SSR (MSM_NPU_EVENT_TYPE_START + 3)

#define MSM_NPU_MAX_INPUT_LAYER_NUM 8
#define MSM_NPU_MAX_OUTPUT_LAYER_NUM 4
#define MSM_NPU_MAX_PATCH_LAYER_NUM (MSM_NPU_MAX_INPUT_LAYER_NUM +\
	MSM_NPU_MAX_OUTPUT_LAYER_NUM)

#define MSM_NPU_PROP_ID_START 0x100
#define MSM_NPU_PROP_ID_FW_STATE (MSM_NPU_PROP_ID_START + 0)
#define MSM_NPU_PROP_ID_PERF_MODE (MSM_NPU_PROP_ID_START + 1)
#define MSM_NPU_PROP_ID_PERF_MODE_MAX (MSM_NPU_PROP_ID_START + 2)
#define MSM_NPU_PROP_ID_DRV_VERSION (MSM_NPU_PROP_ID_START + 3)
#define MSM_NPU_PROP_ID_HARDWARE_VERSION (MSM_NPU_PROP_ID_START + 4)
#define MSM_NPU_PROP_ID_IPC_QUEUE_INFO (MSM_NPU_PROP_ID_START + 5)
#define MSM_NPU_PROP_ID_DRV_FEATURE (MSM_NPU_PROP_ID_START + 6)

#define MSM_NPU_FW_PROP_ID_START 0x1000
#define MSM_NPU_PROP_ID_DCVS_MODE (MSM_NPU_FW_PROP_ID_START + 0)
#define MSM_NPU_PROP_ID_DCVS_MODE_MAX (MSM_NPU_FW_PROP_ID_START + 1)
#define MSM_NPU_PROP_ID_CLK_GATING_MODE (MSM_NPU_FW_PROP_ID_START + 2)
#define MSM_NPU_PROP_ID_HW_VERSION (MSM_NPU_FW_PROP_ID_START + 3)
#define MSM_NPU_PROP_ID_FW_VERSION (MSM_NPU_FW_PROP_ID_START + 4)
#define MSM_NPU_PROP_ID_FW_GETCAPS (MSM_NPU_FW_PROP_ID_START + 5)

/* features supported by driver */
#define MSM_NPU_FEATURE_MULTI_EXECUTE  0x1
#define MSM_NPU_FEATURE_ASYNC_EXECUTE  0x2
#define MSM_NPU_FEATURE_DSP_SID_MAPPED 0x8

#define PROP_PARAM_MAX_SIZE 8

/* -------------------------------------------------------------------------
 * Data Structures
 * -------------------------------------------------------------------------
 */
struct msm_npu_patch_info {
	/* chunk id */
	__u32 chunk_id;
	/* instruction size in bytes */
	__u16 instruction_size_in_bytes;
	/* variable size in bits */
	__u16 variable_size_in_bits;
	/* shift value in bits */
	__u16 shift_value_in_bits;
	/* location offset */
	__u32 loc_offset;
};

struct msm_npu_layer {
	/* layer id */
	__u32 layer_id;
	/* patch information*/
	struct msm_npu_patch_info patch_info;
	/* buffer handle */
	__s32 buf_hdl;
	/* buffer size */
	__u32 buf_size;
	/* physical address */
	__u64 buf_phys_addr;
};

struct msm_npu_patch_info_v2 {
	/* patch value */
	__u32 value;
	/* chunk id */
	__u32 chunk_id;
	/* instruction size in bytes */
	__u32 instruction_size_in_bytes;
	/* variable size in bits */
	__u32 variable_size_in_bits;
	/* shift value in bits */
	__u32 shift_value_in_bits;
	/* location offset */
	__u32 loc_offset;
};

struct msm_npu_patch_buf_info {
	/* physical address to be patched */
	__u64 buf_phys_addr;
	/* buffer id */
	__u32 buf_id;
};

/* -------------------------------------------------------------------------
 * Data Structures - IOCTLs
 * -------------------------------------------------------------------------
 */
struct msm_npu_map_buf_ioctl {
	/* buffer ion handle */
	__s32 buf_ion_hdl;
	/* buffer size */
	__u32 size;
	/* iommu mapped physical address */
	__u64 npu_phys_addr;
};

struct msm_npu_unmap_buf_ioctl {
	/* buffer ion handle */
	__s32 buf_ion_hdl;
	/* iommu mapped physical address */
	__u64 npu_phys_addr;
};

struct msm_npu_get_info_ioctl {
	/* firmware version */
	__u32 firmware_version;
	/* reserved */
	__u32 flags;
};

struct msm_npu_load_network_ioctl {
	/* buffer ion handle */
	__s32 buf_ion_hdl;
	/* physical address */
	__u64 buf_phys_addr;
	/* buffer size */
	__u32 buf_size;
	/* first block size */
	__u32 first_block_size;
	/* reserved */
	__u32 flags;
	/* network handle */
	__u32 network_hdl;
	/* priority */
	__u32 priority;
	/* perf mode */
	__u32 perf_mode;
};

struct msm_npu_load_network_ioctl_v2 {
	/* physical address */
	__u64 buf_phys_addr;
	/* patch info(v2) for all input/output layers */
	__u64 patch_info;
	/* buffer ion handle */
	__s32 buf_ion_hdl;
	/* buffer size */
	__u32 buf_size;
	/* first block size */
	__u32 first_block_size;
	/* load flags */
	__u32 flags;
	/* network handle */
	__u32 network_hdl;
	/* priority */
	__u32 priority;
	/* perf mode */
	__u32 perf_mode;
	/* number of layers in the network */
	__u32 num_layers;
	/* number of layers to be patched */
	__u32 patch_info_num;
	/* reserved */
	__u32 reserved;
};

struct msm_npu_unload_network_ioctl {
	/* network handle */
	__u32 network_hdl;
};

struct msm_npu_exec_network_ioctl {
	/* network handle */
	__u32 network_hdl;
	/* input layer number */
	__u32 input_layer_num;
	/* input layer info */
	struct msm_npu_layer input_layers[MSM_NPU_MAX_INPUT_LAYER_NUM];
	/* output layer number */
	__u32 output_layer_num;
	/* output layer info */
	struct msm_npu_layer output_layers[MSM_NPU_MAX_OUTPUT_LAYER_NUM];
	/* patching is required */
	__u32 patching_required;
	/* asynchronous execution */
	__u32 async;
	/* reserved */
	__u32 flags;
};

struct msm_npu_exec_network_ioctl_v2 {
	/* stats buffer to be filled with execution stats */
	__u64 stats_buf_addr;
	/* patch buf info for both input and output layers */
	__u64 patch_buf_info;
	/* network handle */
	__u32 network_hdl;
	/* asynchronous execution */
	__u32 async;
	/* execution flags */
	__u32 flags;
	/* stats buf size allocated */
	__u32 stats_buf_size;
	/* number of layers to be patched */
	__u32 patch_buf_info_num;
	/* reserved */
	__u32 reserved;
};

struct msm_npu_event_execute_done {
	__u32 network_hdl;
	__s32 exec_result;
};

struct msm_npu_event_execute_v2_done {
	__u32 network_hdl;
	__s32 exec_result;
	/* stats buf size filled */
	__u32 stats_buf_size;
};

struct msm_npu_event_ssr {
	__u32 network_hdl;
};

struct msm_npu_event {
	__u32 type;
	union {
		struct msm_npu_event_execute_done exec_done;
		struct msm_npu_event_execute_v2_done exec_v2_done;
		struct msm_npu_event_ssr ssr;
		__u8 data[128];
	} u;
	__u32 reserved[4];
};

struct msm_npu_property {
	__u32 prop_id;
	__u32 num_of_params;
	__u32 network_hdl;
	__u32 prop_param[PROP_PARAM_MAX_SIZE];
};

#endif /*_UAPI_MSM_NPU_H_*/
