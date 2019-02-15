/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABS_H_
#define HABANALABS_H_

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Defines that are asic-specific but constitutes as ABI between kernel driver
 * and userspace
 */
#define GOYA_KMD_SRAM_RESERVED_SIZE_FROM_START	0x8000	/* 32KB */

/*
 * Queue Numbering
 *
 * The external queues (DMA channels + CPU) MUST be before the internal queues
 * and each group (DMA channels + CPU and internal) must be contiguous inside
 * itself but there can be a gap between the two groups (although not
 * recommended)
 */

enum goya_queue_id {
	GOYA_QUEUE_ID_DMA_0 = 0,
	GOYA_QUEUE_ID_DMA_1,
	GOYA_QUEUE_ID_DMA_2,
	GOYA_QUEUE_ID_DMA_3,
	GOYA_QUEUE_ID_DMA_4,
	GOYA_QUEUE_ID_CPU_PQ,
	GOYA_QUEUE_ID_MME,
	GOYA_QUEUE_ID_TPC0,
	GOYA_QUEUE_ID_TPC1,
	GOYA_QUEUE_ID_TPC2,
	GOYA_QUEUE_ID_TPC3,
	GOYA_QUEUE_ID_TPC4,
	GOYA_QUEUE_ID_TPC5,
	GOYA_QUEUE_ID_TPC6,
	GOYA_QUEUE_ID_TPC7,
	GOYA_QUEUE_ID_SIZE
};


/* Opcode to create a new command buffer */
#define HL_CB_OP_CREATE		0
/* Opcode to destroy previously created command buffer */
#define HL_CB_OP_DESTROY	1

struct hl_cb_in {
	/* Handle of CB or 0 if we want to create one */
	__u64 cb_handle;
	/* HL_CB_OP_* */
	__u32 op;
	/* Size of CB. Minimum requested size must be PAGE_SIZE */
	__u32 cb_size;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
	__u32 pad;
};

struct hl_cb_out {
	/* Handle of CB */
	__u64 cb_handle;
};

union hl_cb_args {
	struct hl_cb_in in;
	struct hl_cb_out out;
};

/*
 * This structure size must always be fixed to 64-bytes for backward
 * compatibility
 */
struct hl_cs_chunk {
	/*
	 * For external queue, this represents a Handle of CB on the Host
	 * For internal queue, this represents an SRAM or DRAM address of the
	 * internal CB
	 */
	__u64 cb_handle;
	/* Index of queue to put the CB on */
	__u32 queue_index;
	/*
	 * Size of command buffer with valid packets
	 * Can be smaller then actual CB size
	 */
	__u32 cb_size;
	/* HL_CS_CHUNK_FLAGS_* */
	__u32 cs_chunk_flags;
	/* Align structure to 64 bytes */
	__u32 pad[11];
};

#define HL_CS_FLAGS_FORCE_RESTORE	0x1

#define HL_CS_STATUS_SUCCESS		0

struct hl_cs_in {
	/* this holds address of array of hl_cs_chunk for restore phase */
	__u64 chunks_restore;
	/* this holds address of array of hl_cs_chunk for execution phase */
	__u64 chunks_execute;
	/* this holds address of array of hl_cs_chunk for store phase -
	 * Currently not in use
	 */
	__u64 chunks_store;
	/* Number of chunks in restore phase array */
	__u32 num_chunks_restore;
	/* Number of chunks in execution array */
	__u32 num_chunks_execute;
	/* Number of chunks in restore phase array - Currently not in use */
	__u32 num_chunks_store;
	/* HL_CS_FLAGS_* */
	__u32 cs_flags;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
};

struct hl_cs_out {
	/* this holds the sequence number of the CS to pass to wait ioctl */
	__u64 seq;
	/* HL_CS_STATUS_* */
	__u32 status;
	__u32 pad;
};

union hl_cs_args {
	struct hl_cs_in in;
	struct hl_cs_out out;
};

struct hl_wait_cs_in {
	/* Command submission sequence number */
	__u64 seq;
	/* Absolute timeout to wait in microseconds */
	__u64 timeout_us;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
	__u32 pad;
};

#define HL_WAIT_CS_STATUS_COMPLETED	0
#define HL_WAIT_CS_STATUS_BUSY		1
#define HL_WAIT_CS_STATUS_TIMEDOUT	2
#define HL_WAIT_CS_STATUS_ABORTED	3
#define HL_WAIT_CS_STATUS_INTERRUPTED	4

struct hl_wait_cs_out {
	/* HL_WAIT_CS_STATUS_* */
	__u32 status;
	__u32 pad;
};

union hl_wait_cs_args {
	struct hl_wait_cs_in in;
	struct hl_wait_cs_out out;
};

/*
 * Command Buffer
 * - Request a Command Buffer
 * - Destroy a Command Buffer
 *
 * The command buffers are memory blocks that reside in DMA-able address
 * space and are physically contiguous so they can be accessed by the device
 * directly. They are allocated using the coherent DMA API.
 *
 * When creating a new CB, the IOCTL returns a handle of it, and the user-space
 * process needs to use that handle to mmap the buffer so it can access them.
 *
 */
#define HL_IOCTL_CB		\
		_IOWR('H', 0x02, union hl_cb_args)

/*
 * Command Submission
 *
 * To submit work to the device, the user need to call this IOCTL with a set
 * of JOBS. That set of JOBS constitutes a CS object.
 * Each JOB will be enqueued on a specific queue, according to the user's input.
 * There can be more then one JOB per queue.
 *
 * There are two types of queues - external and internal. External queues
 * are DMA queues which transfer data from/to the Host. All other queues are
 * internal. The driver will get completion notifications from the device only
 * on JOBS which are enqueued in the external queues.
 *
 * This IOCTL is asynchronous in regard to the actual execution of the CS. This
 * means it returns immediately after ALL the JOBS were enqueued on their
 * relevant queues. Therefore, the user mustn't assume the CS has been completed
 * or has even started to execute.
 *
 * Upon successful enqueue, the IOCTL returns an opaque handle which the user
 * can use with the "Wait for CS" IOCTL to check whether the handle's CS
 * external JOBS have been completed. Note that if the CS has internal JOBS
 * which can execute AFTER the external JOBS have finished, the driver might
 * report that the CS has finished executing BEFORE the internal JOBS have
 * actually finish executing.
 *
 * The CS IOCTL will receive three sets of JOBS. One set is for "restore" phase,
 * a second set is for "execution" phase and a third set is for "store" phase.
 * The JOBS on the "restore" phase are enqueued only after context-switch
 * (or if its the first CS for this context). The user can also order the
 * driver to run the "restore" phase explicitly
 *
 */
#define HL_IOCTL_CS			\
		_IOWR('H', 0x03, union hl_cs_args)

/*
 * Wait for Command Submission
 *
 * The user can call this IOCTL with a handle it received from the CS IOCTL
 * to wait until the handle's CS has finished executing. The user will wait
 * inside the kernel until the CS has finished or until the user-requeusted
 * timeout has expired.
 *
 * The return value of the IOCTL is a standard Linux error code. The possible
 * values are:
 *
 * EINTR     - Kernel waiting has been interrupted, e.g. due to OS signal
 *             that the user process received
 * ETIMEDOUT - The CS has caused a timeout on the device
 * EIO       - The CS was aborted (usually because the device was reset)
 * ENODEV    - The device wants to do hard-reset (so user need to close FD)
 *
 * The driver also returns a custom define inside the IOCTL which can be:
 *
 * HL_WAIT_CS_STATUS_COMPLETED   - The CS has been completed successfully (0)
 * HL_WAIT_CS_STATUS_BUSY        - The CS is still executing (0)
 * HL_WAIT_CS_STATUS_TIMEDOUT    - The CS has caused a timeout on the device
 *                                 (ETIMEDOUT)
 * HL_WAIT_CS_STATUS_ABORTED     - The CS was aborted, usually because the
 *                                 device was reset (EIO)
 * HL_WAIT_CS_STATUS_INTERRUPTED - Waiting for the CS was interrupted (EINTR)
 *
 */

#define HL_IOCTL_WAIT_CS			\
		_IOWR('H', 0x04, union hl_wait_cs_args)

#define HL_COMMAND_START	0x02
#define HL_COMMAND_END		0x05

#endif /* HABANALABS_H_ */
