/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UAPI_MSM_HGSL_H
#define _UAPI_MSM_HGSL_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define HGSL_SYNC_TYPE_HSYNC 1
#define HGSL_SYNC_TYPE_ISYNC 2

struct hgsl_ibdesc {
	__u64 gpuaddr;
	__u64 sizedwords;
};

struct hgsl_mem_object {
	__u64 gpuaddr;
	__u64 sizedwords;
};

#define HGSL_IOCTL_BASE	'h'
#define HGSL_IORW(n, t)	_IOWR(HGSL_IOCTL_BASE, n, t)
#define HGSL_IOW(n, t)	_IOW(HGSL_IOCTL_BASE, n, t)


/**
 * return current status of Doorbell system
 */
#define DB_STATE_Q_MASK         0xffff
#define DB_STATE_Q_UNINIT       1
#define DB_STATE_Q_INIT_DONE    2
#define DB_STATE_Q_FAULT        3

#define HGSL_IOCTL_DBQ_GETSTATE	HGSL_IORW(0x01, __s32)

/**
 * struct hgsl_db_queue_inf - initialize Doorbell
 * @fd: File descriptor of DB queue
 * @head_dwords: Size of DB queue header
 * @head_off_dwords: offset of DB queue header
 * @queue_dwords: size of DB queue
 * @queue_off_dwords: offset of DB queue
 * @db_signal: DB signal type, One of DB_SIGNAL_* values
 *
 **/
struct hgsl_db_queue_inf {
	__s32 fd;
	__u32 head_dwords;
	__s32 head_off_dwords;
	__u32 queue_dwords;
	__s32 queue_off_dwords;
	__u32 db_signal;
};

#define DB_SIGNAL_INVALID       0
#define DB_SIGNAL_GLOBAL_0      1
#define DB_SIGNAL_GLOBAL_1      2
#define DB_SIGNAL_LOCAL         3
#define DB_SIGNAL_MAX           DB_SIGNAL_LOCAL

#define HGSL_IOCTL_DBQ_INIT	\
	HGSL_IORW(0x02, struct hgsl_db_queue_inf)

/**
 * Assign a DB to current instance, input is DB index.
 */
#define HGSL_IOCTL_DBQ_ASSIGN	HGSL_IORW(0x03, __u32)

/**
 * struct hgsl_dbq_release_info - release DB queue
 * @ref_count; userspace could inform PVM to do cleanup
 * if this param is zero
 * @ctxt_id: Current context
 */
struct hgsl_dbq_release_info {
	__u32 ref_count;
	__u32 ctxt_id;
};

#define HGSL_IOCTL_DBQ_RELEASE	\
	HGSL_IORW(0x04, struct hgsl_dbq_release_info)


/**
 * struct hgsl_fhi_issud_cmds - submit cmds to DB queue
 * @ibs: db commands list
 * @num_ibs: Number of db commands
 * @bos: buffer objects list
 * @num_bos: Number of buffer objects
 * @context_id: Current context for these cmds
 * @flags: reserved
 * @timestamp: Userspace time stamp
 */
struct hgsl_fhi_issud_cmds {
	__u64 ibs;
	__u32 num_ibs;
	__u64 bos;
	__u32 num_bos;
	__u32 context_id;
	__u32 flags;
	__u32 timestamp;
	__u32 padding;
};

#define HGSL_IOCTL_ISSUE_CMDS	\
	HGSL_IORW(0x05, struct hgsl_fhi_issud_cmds)

/**
 * struct hgsl_ctxt_create_info - create a DB context
 * @context_id: Current context for these cmds
 * @shadow_fd: File descriptor of time stamp buffer
 * @shadow_sop_offset: Offset for start timestamp written
 * by GPU before cmds
 * @shadow_eop_offset: Offset for end timestamp written
 * by GPU after cmds
 */
struct hgsl_ctxt_create_info {
	__u32 context_id;
	__s32 shadow_fd;
	__u32 shadow_sop_offset;
	__u32 shadow_eop_offset;
};

struct hgsl_ioctl_ctxt_create_params {
	__u32 devhandle;
	__u32 type;
	__u32 flags;
	__u32 ctxthandle;
	__u32 sync_type;
	__u32 padding;
};

#define HGSL_IOCTL_CTXT_CREATE	HGSL_IORW(0x10,  \
	struct hgsl_ioctl_ctxt_create_params)

struct hgsl_ioctl_ctxt_destroy_params {
	__u32 devhandle;
	__u32 ctxthandle;
	__u32 rval;
	__u32 padding;
};

#define HGSL_IOCTL_CTXT_DESTROY	HGSL_IORW(0x11,  \
	struct hgsl_ioctl_ctxt_destroy_params)

/**
 * struct hgsl_wait_ts_info - wait a timestamp to be retired
 * @timestamp: The user timestamp to wait
 * @timeout: Expiry timeout
 */
struct hgsl_wait_ts_info {
	__u32 context_id;
	__u32 timestamp;
	__u32 timeout;
	__u32 padding;
	__u32 devhandle;
	__u32 channel_id;
};

#define HGSL_IOCTL_WAIT_TIMESTAMP \
	HGSL_IORW(0x12,  struct hgsl_wait_ts_info)

/**
 * struct hgsl_ioctl_issueib_params - submit cmds to GPU
 * @ibs: db commands list
 * @devhandle: GPU device handle
 * @ctxthandle: Current context handle for the cmds
 * @timestamp: Userspace time stamp
 * @flags: reserved
 * @num_ibs: Number of ib cmds
 * @rval: return value from host
 * @channel_id: hab channel id
 */
struct hgsl_ioctl_issueib_params {
	__u64 ibs;
	__u32 devhandle;
	__u32 ctxthandle;
	__u32 timestamp;
	__u32 flags;
	__u32 num_ibs;
	__u32 rval;
	__u32 channel_id;
	__u32 padding;
};
#define HGSL_IOCTL_ISSUE_IB \
	HGSL_IORW(0x20, struct hgsl_ioctl_issueib_params)

struct hgsl_ioctl_issueib_with_alloc_list_params {
	__u64 rpc_syncobj;
	__u64 ibs;
	__u64 allocations;
	__u64 be_data;
	__u32 devhandle;
	__u32 ctxthandle;
	__u32 timestamp;
	__u32 flags;
	__u32 num_ibs;
	__u32 num_allocations;
	__u32 rval;
	__u32 channel_id;
};

#define HGSL_IOCTL_ISSUIB_WITH_ALLOC_LIST \
	HGSL_IORW(0x21,  struct hgsl_ioctl_issueib_with_alloc_list_params)

#define HGSL_HYP_GENERAL_MAX_SEND_NUM     2
#define HGSL_HYP_GENERAL_MAX_REPLY_NUM    1

struct hgsl_ioctl_hyp_generic_transaction_params {
	__u64 send_data[HGSL_HYP_GENERAL_MAX_SEND_NUM];
	__u64 reply_data[HGSL_HYP_GENERAL_MAX_REPLY_NUM];
	__u32 send_size[HGSL_HYP_GENERAL_MAX_SEND_NUM];
	__u32 reply_size[HGSL_HYP_GENERAL_MAX_REPLY_NUM];
	__u32 send_num;
	__u32 reply_num;
	__u32 cmd_id;
	__u64 ret_value;
};

#define HGSL_IOCTL_HYP_GENERIC_TRANSACTION \
	HGSL_IORW(0x22,  struct hgsl_ioctl_hyp_generic_transaction_params)

struct hgsl_ioctl_get_shadowts_mem_params {
	__u64 size;
	__u32 device_id;
	__u32 ctxthandle;
	__u32 flags;
	__s32 fd;
	__u32 devhandle;
	__u32 padding;
};

#define HGSL_IOCTL_GET_SHADOWTS_MEM \
	HGSL_IORW(0x23,  struct hgsl_ioctl_get_shadowts_mem_params)

struct hgsl_ioctl_put_shadowts_mem_params {
	__u32 ctxthandle;
	__u32 padding;
	__u32 devhandle;
	__u32 padding_1;
};

#define HGSL_IOCTL_PUT_SHADOWTS_MEM \
	HGSL_IOW(0x24,	struct hgsl_ioctl_put_shadowts_mem_params)

struct hgsl_ioctl_mem_alloc_params {
	__u64 memdesc;
	__u32 sizebytes;
	__u32 flags;
	__s32 fd;
	__u32 padding;
};

#define HGSL_IOCTL_MEM_ALLOC \
	HGSL_IORW(0x25,  struct hgsl_ioctl_mem_alloc_params)

struct hgsl_ioctl_mem_free_params {
	__u64 memdesc;
};

#define HGSL_IOCTL_MEM_FREE \
	HGSL_IORW(0x26,  struct hgsl_ioctl_mem_free_params)

struct hgsl_ioctl_mem_map_smmu_params {
	__u64 size;
	__u64 offset;
	__u64 uva;
	__u64 memdesc;
	__s32 fd;
	__u32 memtype;
	__u32 flags;
	__u32 padding;
};

#define HGSL_IOCTL_MEM_MAP_SMMU \
	HGSL_IORW(0x27,  struct hgsl_ioctl_mem_map_smmu_params)

struct hgsl_ioctl_mem_unmap_smmu_params {
	__u64 gpuaddr;
	__u64 size;
	__u32 memtype;
	__u32 padding;
};

#define HGSL_IOCTL_MEM_UNMAP_SMMU \
	HGSL_IOW(0x28,  struct hgsl_ioctl_mem_unmap_smmu_params)

struct hgsl_ioctl_mem_cache_operation_params {
	__u64 offsetbytes;
	__u64 sizebytes;
	__u64 gpuaddr;
	__u32 operation;
	__u32 padding;
};

#define HGSL_IOCTL_MEM_CACHE_OPERATION \
	HGSL_IORW(0x29,  struct hgsl_ioctl_mem_cache_operation_params)

struct hgsl_ioctl_read_ts_params {
	__u32 devhandle;
	__u32 ctxthandle;
	__u32 type;
	__u32 timestamp;
};

#define HGSL_IOCTL_READ_TIMESTAMP \
	HGSL_IORW(0x2A,  struct hgsl_ioctl_read_ts_params)

struct hgsl_ioctl_check_ts_params {
	__u32 devhandle;
	__u32 ctxthandle;
	__u32 type;
	__u32 timestamp;
	__u32 rval;
	__u32 padding;
};

#define HGSL_IOCTL_CHECK_TIMESTAMP \
	HGSL_IORW(0x2B,  struct hgsl_ioctl_check_ts_params)

struct hgsl_ioctl_syncobj_wait_multiple_params {
	__u64 num_syncobjs;
	__u64 rpc_syncobj;
	__u64 status;
	__u32 timeout_ms;
	__u32 result;
};

#define HGSL_IOCTL_SYNCOBJ_WAIT_MULTIPLE \
	HGSL_IORW(0x2C,  struct hgsl_ioctl_syncobj_wait_multiple_params)

struct hgsl_ioctl_set_metainfo_params {
	__u64  memdesc_priv;
	__u64  metainfo;
	__u32  flags;
	__u32  metainfo_len;
};

#define HGSL_IOCTL_SET_METAINFO \
	HGSL_IORW(0x2D,  struct hgsl_ioctl_set_metainfo_params)

#define HGSL_IOCTL_GET_SYSTEM_TIME \
	HGSL_IORW(0x2E,  __u64)

struct hgsl_ioctl_perfcounter_select_params {
	__u64 groups;
	__u64 counter_ids;
	__u64 counter_val_regs;
	__u64 counter_val_hi_regs;
	__u32 devhandle;
	__u32 ctxthandle;
	__u32 num_counters;
	__u32 rval;
};

#define HGSL_IOCTL_PERFCOUNTER_SELECT \
	HGSL_IORW(0x30,  struct hgsl_ioctl_perfcounter_select_params)

struct hgsl_ioctl_perfcounter_deselect_params {
	__u64 groups;
	__u64 counter_ids;
	__u32 devhandle;
	__u32 ctxthandle;
	__u32 timestamp;
	__u32 num_counters;
};

#define HGSL_IOCTL_PERFCOUNTER_DESELECT \
	HGSL_IOW(0x31,  struct hgsl_ioctl_perfcounter_deselect_params)

struct hgsl_ioctl_perfcounter_query_selections_params {
	__u64 selections;
	__u32 devhandle;
	__u32 ctxthandle;
	__u32 num_counters;
	__u32 group;
	__u32 max_counters;
	__u32 padding;
};
#define HGSL_IOCTL_PERFCOUNTER_QUERY_SELECTION \
	HGSL_IORW(0x32,  struct hgsl_ioctl_perfcounter_query_selections_params)

struct hgsl_ioctl_perfcounter_read_params {
	__u64 value;
	__u32 devhandle;
	__u32 group;
	__u32 counter;
	__u32 rval;
};

#define HGSL_IOCTL_PERFCOUNTER_READ \
		HGSL_IORW(0x33,  struct hgsl_ioctl_perfcounter_read_params)

/**
 * struct hgsl_hsync_fence_create - wait a h-sync fence
 * @timestamp: The user timestamp attached to the fence
 * @context_id: The conext to create fence
 * @fence_fd: File descriptor of the new created fence
 * @devhandle GPU device handle
 */
struct hgsl_hsync_fence_create {
	__u32 timestamp;
	__u32 padding;
	__s32 context_id;
	__s32 fence_fd;
	__u32 devhandle;
	__u32 padding_1;
};

#define HGSL_IOCTL_HSYNC_FENCE_CREATE \
				HGSL_IORW(0x13, struct hgsl_hsync_fence_create)

/**
 * Create an i-fence timeline - param is id of the new timeline
 */
#define HGSL_IOCTL_ISYNC_TIMELINE_CREATE \
				HGSL_IORW(0x14, __u32)

/**
 * Destroy an i-fence timeline - param is id of timeline to be released
 */
#define HGSL_IOCTL_ISYNC_TIMELINE_DESTROY \
				HGSL_IOW(0x15, __u32)

/* Use padding field to tell if ts is valid when create isync fence */
#define HGSL_ISYNC_FENCE_CREATE_USE_TS	1

/**
 * struct hgsl_isync_create_fence - wait an i-sync fence
 * @timeline_id: The timestamp for the new fence
 * @fence_id: id of new created fence
 * @ts: option, should set it if want to use isync forward
 */
struct hgsl_isync_create_fence {
	__u32 timeline_id;
	__s32 fence_id;
	__u32 ts;
	__u32 padding;
};
#define HGSL_IOCTL_ISYNC_FENCE_CREATE	\
				HGSL_IORW(0x16,  \
					 struct hgsl_isync_create_fence)

/**
 * struct hgsl_isync_signal_fence - signal an i-sync fence
 * @timeline_id: The timestamp for current fence
 * @fence_id: id of fence to be signalled
 */
struct hgsl_isync_signal_fence {
	__u32 timeline_id;
	__s32 fence_id;
};
#define HGSL_IOCTL_ISYNC_FENCE_SIGNAL \
				HGSL_IOW(0x17, \
					 struct hgsl_isync_signal_fence)


/**
 * struct hgsl_isync_signal_fence - signal an i-sync fence
 * @timeline_id: The timestamp for current fence
 * @fence_id: id of fence to be signalled
 */
struct hgsl_isync_forward {
	__u32 timeline_id;
	__s32 ts;
};
#define HGSL_IOCTL_ISYNC_FORWARD \
				HGSL_IOW(0x18, \
					 struct hgsl_isync_forward)

/**
 * struct hgsl_isync_timeline_create64 - create a isync timeline with 64 or 32bits timestamp
 * @initial_ts: The initial timestamp value.
 * @timeline_id: The timestamp for current fence
 * @flags: flags used to timeline
 */
struct hgsl_timeline_create {
	__u64 initial_ts;
	__u32 timeline_id;
	__s32 flags;
};

#define HGSL_IOCTL_TIMELINE_CREATE \
				HGSL_IORW(0x19, struct hgsl_timeline_create)

/**
 * struct hgsl_timeline_val - A container to store a timeline/sequence number
 * pair.
 * @timepoint: timepoint to signal/query/wait
 * @timeline: The timeline identifier to signal/query/wait
 *
 * A container to store a timeline/timepoint pair used by the query, signal and wait
 * ioctls.
 */
struct hgsl_timeline_val {
	__u64 timepoint;
	__u32 timeline_id;
	__u32 padding;
};

/**
 * struct hgsl_timeline_query - query multiple timelines
 * @timelines: Address of an array of &struct hgsl_timeline_val entries
 * @count: Number of entries in @timeline
 * @timelines_size: Size of each entry in @timelines
 */
struct hgsl_timeline_query {
	__u64 timelines;
	__u32 count;
	__u32 timelines_size;
};

#define HGSL_IOCTL_TIMELINE_QUERY \
				HGSL_IORW(0x1A, struct hgsl_timeline_query)

/**
 * struct hgsl_timeline_signal - signal multiple timelines
 * @timelines: Address of an array of &struct hgsl_timeline_val entries
 * @count: Number of entries in @timeline
 * @timelines_size: Size of each entry in @timelines
 */
struct hgsl_timeline_signal {
	__u64 timelines;
	__u32 count;
	__u32 timelines_size;
};

#define HGSL_IOCTL_TIMELINE_SIGNAL \
				HGSL_IOW(0x1B, struct hgsl_timeline_signal)

#define HGSL_TIMELINE_WAIT_ALL 1
#define HGSL_TIMELINE_WAIT_ANY 2

/**
 * struct hgsl_timeline_wait - wait multiple timelines
 * @timeout_nanosec: Number of nanoseconds to wait for the signal
 * @timelines: Address of an array of &struct hgsl_timeline_val entries
 * @count: Number of entries in @timeline
 * @timelines_size: Size of each entry in @timelines
 * @flags: One of HGSL_TIMELINE_WAIT_ALL or HGSL_TIMELINE_WAIT_ANY
 */
struct hgsl_timeline_wait {
	__u64 timeout_nanosec;
	__u64 timelines;
	__u32 count;
	__u32 timelines_size;
	__u32 flags;
/* private: padding for 64 bit compatibility */
	__u32 padding;
};

#define HGSL_IOCTL_TIMELINE_WAIT \
				HGSL_IOW(0x1C, struct hgsl_timeline_wait)

/**
 * struct hgsl_ioctl_mem_get_fd_params - get fd from memdesc
 * @memdesc: According to memdesc to find the mem node
 * @fd: The fd of dmabuf mem node
 */
struct hgsl_ioctl_mem_get_fd_params {
	__u64 memdesc;
	__s32 fd;
};

#define HGSL_IOCTL_MEM_GET_FD \
				HGSL_IORW(0x1D, struct hgsl_ioctl_mem_get_fd_params)

#endif /* _UAPI_MSM_HGSL_H */
