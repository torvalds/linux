/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 *
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef QAIC_ACCEL_H_
#define QAIC_ACCEL_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* The length(4K) includes len and count fields of qaic_manage_msg */
#define QAIC_MANAGE_MAX_MSG_LENGTH SZ_4K

/* semaphore flags */
#define QAIC_SEM_INSYNCFENCE	2
#define QAIC_SEM_OUTSYNCFENCE	1

/* Semaphore commands */
#define QAIC_SEM_NOP		0
#define QAIC_SEM_INIT		1
#define QAIC_SEM_INC		2
#define QAIC_SEM_DEC		3
#define QAIC_SEM_WAIT_EQUAL	4
#define QAIC_SEM_WAIT_GT_EQ	5 /* Greater than or equal */
#define QAIC_SEM_WAIT_GT_0	6 /* Greater than 0 */

#define QAIC_TRANS_UNDEFINED			0
#define QAIC_TRANS_PASSTHROUGH_FROM_USR		1
#define QAIC_TRANS_PASSTHROUGH_TO_USR		2
#define QAIC_TRANS_PASSTHROUGH_FROM_DEV		3
#define QAIC_TRANS_PASSTHROUGH_TO_DEV		4
#define QAIC_TRANS_DMA_XFER_FROM_USR		5
#define QAIC_TRANS_DMA_XFER_TO_DEV		6
#define QAIC_TRANS_ACTIVATE_FROM_USR		7
#define QAIC_TRANS_ACTIVATE_FROM_DEV		8
#define QAIC_TRANS_ACTIVATE_TO_DEV		9
#define QAIC_TRANS_DEACTIVATE_FROM_USR		10
#define QAIC_TRANS_DEACTIVATE_FROM_DEV		11
#define QAIC_TRANS_STATUS_FROM_USR		12
#define QAIC_TRANS_STATUS_TO_USR		13
#define QAIC_TRANS_STATUS_FROM_DEV		14
#define QAIC_TRANS_STATUS_TO_DEV		15
#define QAIC_TRANS_TERMINATE_FROM_DEV		16
#define QAIC_TRANS_TERMINATE_TO_DEV		17
#define QAIC_TRANS_DMA_XFER_CONT		18
#define QAIC_TRANS_VALIDATE_PARTITION_FROM_DEV	19
#define QAIC_TRANS_VALIDATE_PARTITION_TO_DEV	20

/**
 * struct qaic_manage_trans_hdr - Header for a transaction in a manage message.
 * @type: In. Identifies this transaction. See QAIC_TRANS_* defines.
 * @len: In. Length of this transaction, including this header.
 */
struct qaic_manage_trans_hdr {
	__u32 type;
	__u32 len;
};

/**
 * struct qaic_manage_trans_passthrough - Defines a passthrough transaction.
 * @hdr: In. Header to identify this transaction.
 * @data: In. Payload of this ransaction. Opaque to the driver. Userspace must
 *	  encode in little endian and align/pad to 64-bit.
 */
struct qaic_manage_trans_passthrough {
	struct qaic_manage_trans_hdr hdr;
	__u8 data[];
};

/**
 * struct qaic_manage_trans_dma_xfer - Defines a DMA transfer transaction.
 * @hdr: In. Header to identify this transaction.
 * @tag: In. Identified this transfer in other transactions. Opaque to the
 *	 driver.
 * @pad: Structure padding.
 * @addr: In. Address of the data to DMA to the device.
 * @size: In. Length of the data to DMA to the device.
 */
struct qaic_manage_trans_dma_xfer {
	struct qaic_manage_trans_hdr hdr;
	__u32 tag;
	__u32 pad;
	__u64 addr;
	__u64 size;
};

/**
 * struct qaic_manage_trans_activate_to_dev - Defines an activate request.
 * @hdr: In. Header to identify this transaction.
 * @queue_size: In. Number of elements for DBC request and response queues.
 * @eventfd: Unused.
 * @options: In. Device specific options for this activate.
 * @pad: Structure padding.  Must be 0.
 */
struct qaic_manage_trans_activate_to_dev {
	struct qaic_manage_trans_hdr hdr;
	__u32 queue_size;
	__u32 eventfd;
	__u32 options;
	__u32 pad;
};

/**
 * struct qaic_manage_trans_activate_from_dev - Defines an activate response.
 * @hdr: Out. Header to identify this transaction.
 * @status: Out. Return code of the request from the device.
 * @dbc_id: Out. Id of the assigned DBC for successful request.
 * @options: Out. Device specific options for this activate.
 */
struct qaic_manage_trans_activate_from_dev {
	struct qaic_manage_trans_hdr hdr;
	__u32 status;
	__u32 dbc_id;
	__u64 options;
};

/**
 * struct qaic_manage_trans_deactivate - Defines a deactivate request.
 * @hdr: In. Header to identify this transaction.
 * @dbc_id: In. Id of assigned DBC.
 * @pad: Structure padding.  Must be 0.
 */
struct qaic_manage_trans_deactivate {
	struct qaic_manage_trans_hdr hdr;
	__u32 dbc_id;
	__u32 pad;
};

/**
 * struct qaic_manage_trans_status_to_dev - Defines a status request.
 * @hdr: In. Header to identify this transaction.
 */
struct qaic_manage_trans_status_to_dev {
	struct qaic_manage_trans_hdr hdr;
};

/**
 * struct qaic_manage_trans_status_from_dev - Defines a status response.
 * @hdr: Out. Header to identify this transaction.
 * @major: Out. NNC protocol version major number.
 * @minor: Out. NNC protocol version minor number.
 * @status: Out. Return code from device.
 * @status_flags: Out. Flags from device.  Bit 0 indicates if CRCs are required.
 */
struct qaic_manage_trans_status_from_dev {
	struct qaic_manage_trans_hdr hdr;
	__u16 major;
	__u16 minor;
	__u32 status;
	__u64 status_flags;
};

/**
 * struct qaic_manage_msg - Defines a message to the device.
 * @len: In. Length of all the transactions contained within this message.
 * @count: In. Number of transactions in this message.
 * @data: In. Address to an array where the transactions can be found.
 */
struct qaic_manage_msg {
	__u32 len;
	__u32 count;
	__u64 data;
};

/**
 * struct qaic_create_bo - Defines a request to create a buffer object.
 * @size: In.  Size of the buffer in bytes.
 * @handle: Out. GEM handle for the BO.
 * @pad: Structure padding. Must be 0.
 */
struct qaic_create_bo {
	__u64 size;
	__u32 handle;
	__u32 pad;
};

/**
 * struct qaic_mmap_bo - Defines a request to prepare a BO for mmap().
 * @handle: In.  Handle of the GEM BO to prepare for mmap().
 * @pad: Structure padding. Must be 0.
 * @offset: Out. Offset value to provide to mmap().
 */
struct qaic_mmap_bo {
	__u32 handle;
	__u32 pad;
	__u64 offset;
};

/**
 * struct qaic_sem - Defines a semaphore command for a BO slice.
 * @val: In. Only lower 12 bits are valid.
 * @index: In. Only lower 5 bits are valid.
 * @presync: In. 1 if presync operation, 0 if postsync.
 * @cmd: In. One of QAIC_SEM_*.
 * @flags: In. Bitfield. See QAIC_SEM_INSYNCFENCE and QAIC_SEM_OUTSYNCFENCE
 * @pad: Structure padding.  Must be 0.
 */
struct qaic_sem {
	__u16 val;
	__u8  index;
	__u8  presync;
	__u8  cmd;
	__u8  flags;
	__u16 pad;
};

/**
 * struct qaic_attach_slice_entry - Defines a single BO slice.
 * @size: In. Size of this slice in bytes.
 * @sem0: In. Semaphore command 0. Must be 0 is not valid.
 * @sem1: In. Semaphore command 1. Must be 0 is not valid.
 * @sem2: In. Semaphore command 2. Must be 0 is not valid.
 * @sem3: In. Semaphore command 3. Must be 0 is not valid.
 * @dev_addr: In. Device address this slice pushes to or pulls from.
 * @db_addr: In. Address of the doorbell to ring.
 * @db_data: In. Data to write to the doorbell.
 * @db_len: In. Size of the doorbell data in bits - 32, 16, or 8.  0 is for
 *	    inactive doorbells.
 * @offset: In. Start of this slice as an offset from the start of the BO.
 */
struct qaic_attach_slice_entry {
	__u64 size;
	struct qaic_sem	sem0;
	struct qaic_sem	sem1;
	struct qaic_sem	sem2;
	struct qaic_sem	sem3;
	__u64 dev_addr;
	__u64 db_addr;
	__u32 db_data;
	__u32 db_len;
	__u64 offset;
};

/**
 * struct qaic_attach_slice_hdr - Defines metadata for a set of BO slices.
 * @count: In. Number of slices for this BO.
 * @dbc_id: In. Associate the sliced BO with this DBC.
 * @handle: In. GEM handle of the BO to slice.
 * @dir: In. Direction of data flow. 1 = DMA_TO_DEVICE, 2 = DMA_FROM_DEVICE
 * @size: In. Total length of BO being used. This should not exceed base
 *	  size of BO (struct drm_gem_object.base)
 *	  For BOs being allocated using DRM_IOCTL_QAIC_CREATE_BO, size of
 *	  BO requested is PAGE_SIZE aligned then allocated hence allocated
 *	  BO size maybe bigger. This size should not exceed the new
 *	  PAGE_SIZE aligned BO size.
 * @dev_addr: In. Device address this slice pushes to or pulls from.
 * @db_addr: In. Address of the doorbell to ring.
 * @db_data: In. Data to write to the doorbell.
 * @db_len: In. Size of the doorbell data in bits - 32, 16, or 8.  0 is for
 *	    inactive doorbells.
 * @offset: In. Start of this slice as an offset from the start of the BO.
 */
struct qaic_attach_slice_hdr {
	__u32 count;
	__u32 dbc_id;
	__u32 handle;
	__u32 dir;
	__u64 size;
};

/**
 * struct qaic_attach_slice - Defines a set of BO slices.
 * @hdr: In. Metadata of the set of slices.
 * @data: In. Pointer to an array containing the slice definitions.
 */
struct qaic_attach_slice {
	struct qaic_attach_slice_hdr hdr;
	__u64 data;
};

/**
 * struct qaic_execute_entry - Defines a BO to submit to the device.
 * @handle: In. GEM handle of the BO to commit to the device.
 * @dir: In. Direction of data. 1 = to device, 2 = from device.
 */
struct qaic_execute_entry {
	__u32 handle;
	__u32 dir;
};

/**
 * struct qaic_partial_execute_entry - Defines a BO to resize and submit.
 * @handle: In. GEM handle of the BO to commit to the device.
 * @dir: In. Direction of data. 1 = to device, 2 = from device.
 * @resize: In. New size of the BO.  Must be <= the original BO size.
 *	    @resize as 0 would be interpreted as no DMA transfer is
 *	    involved.
 */
struct qaic_partial_execute_entry {
	__u32 handle;
	__u32 dir;
	__u64 resize;
};

/**
 * struct qaic_execute_hdr - Defines metadata for BO submission.
 * @count: In. Number of BOs to submit.
 * @dbc_id: In. DBC to submit the BOs on.
 */
struct qaic_execute_hdr {
	__u32 count;
	__u32 dbc_id;
};

/**
 * struct qaic_execute - Defines a list of BOs to submit to the device.
 * @hdr: In. BO list metadata.
 * @data: In. Pointer to an array of BOs to submit.
 */
struct qaic_execute {
	struct qaic_execute_hdr hdr;
	__u64 data;
};

/**
 * struct qaic_wait - Defines a blocking wait for BO execution.
 * @handle: In. GEM handle of the BO to wait on.
 * @timeout: In. Maximum time in ms to wait for the BO.
 * @dbc_id: In. DBC the BO is submitted to.
 * @pad: Structure padding. Must be 0.
 */
struct qaic_wait {
	__u32 handle;
	__u32 timeout;
	__u32 dbc_id;
	__u32 pad;
};

/**
 * struct qaic_perf_stats_hdr - Defines metadata for getting BO perf info.
 * @count: In. Number of BOs requested.
 * @pad: Structure padding. Must be 0.
 * @dbc_id: In. DBC the BO are associated with.
 */
struct qaic_perf_stats_hdr {
	__u16 count;
	__u16 pad;
	__u32 dbc_id;
};

/**
 * struct qaic_perf_stats - Defines a request for getting BO perf info.
 * @hdr: In. Request metadata
 * @data: In. Pointer to array of stats structures that will receive the data.
 */
struct qaic_perf_stats {
	struct qaic_perf_stats_hdr hdr;
	__u64 data;
};

/**
 * struct qaic_perf_stats_entry - Defines a BO perf info.
 * @handle: In. GEM handle of the BO to get perf stats for.
 * @queue_level_before: Out. Number of elements in the queue before this BO
 *			was submitted.
 * @num_queue_element: Out. Number of elements added to the queue to submit
 *		       this BO.
 * @submit_latency_us: Out. Time taken by the driver to submit this BO.
 * @device_latency_us: Out. Time taken by the device to execute this BO.
 * @pad: Structure padding. Must be 0.
 */
struct qaic_perf_stats_entry {
	__u32 handle;
	__u32 queue_level_before;
	__u32 num_queue_element;
	__u32 submit_latency_us;
	__u32 device_latency_us;
	__u32 pad;
};

/**
 * struct qaic_detach_slice - Detaches slicing configuration from BO.
 * @handle: In. GEM handle of the BO to detach slicing configuration.
 * @pad: Structure padding. Must be 0.
 */
struct qaic_detach_slice {
	__u32 handle;
	__u32 pad;
};

#define DRM_QAIC_MANAGE				0x00
#define DRM_QAIC_CREATE_BO			0x01
#define DRM_QAIC_MMAP_BO			0x02
#define DRM_QAIC_ATTACH_SLICE_BO		0x03
#define DRM_QAIC_EXECUTE_BO			0x04
#define DRM_QAIC_PARTIAL_EXECUTE_BO		0x05
#define DRM_QAIC_WAIT_BO			0x06
#define DRM_QAIC_PERF_STATS_BO			0x07
#define DRM_QAIC_DETACH_SLICE_BO		0x08

#define DRM_IOCTL_QAIC_MANAGE			DRM_IOWR(DRM_COMMAND_BASE + DRM_QAIC_MANAGE, struct qaic_manage_msg)
#define DRM_IOCTL_QAIC_CREATE_BO		DRM_IOWR(DRM_COMMAND_BASE + DRM_QAIC_CREATE_BO,	struct qaic_create_bo)
#define DRM_IOCTL_QAIC_MMAP_BO			DRM_IOWR(DRM_COMMAND_BASE + DRM_QAIC_MMAP_BO, struct qaic_mmap_bo)
#define DRM_IOCTL_QAIC_ATTACH_SLICE_BO		DRM_IOW(DRM_COMMAND_BASE + DRM_QAIC_ATTACH_SLICE_BO, struct qaic_attach_slice)
#define DRM_IOCTL_QAIC_EXECUTE_BO		DRM_IOW(DRM_COMMAND_BASE + DRM_QAIC_EXECUTE_BO,	struct qaic_execute)
#define DRM_IOCTL_QAIC_PARTIAL_EXECUTE_BO	DRM_IOW(DRM_COMMAND_BASE + DRM_QAIC_PARTIAL_EXECUTE_BO,	struct qaic_execute)
#define DRM_IOCTL_QAIC_WAIT_BO			DRM_IOW(DRM_COMMAND_BASE + DRM_QAIC_WAIT_BO, struct qaic_wait)
#define DRM_IOCTL_QAIC_PERF_STATS_BO		DRM_IOWR(DRM_COMMAND_BASE + DRM_QAIC_PERF_STATS_BO, struct qaic_perf_stats)
#define DRM_IOCTL_QAIC_DETACH_SLICE_BO		DRM_IOW(DRM_COMMAND_BASE + DRM_QAIC_DETACH_SLICE_BO, struct qaic_detach_slice)

#if defined(__cplusplus)
}
#endif

#endif /* QAIC_ACCEL_H_ */
