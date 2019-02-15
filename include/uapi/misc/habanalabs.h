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

#define HL_COMMAND_START	0x02
#define HL_COMMAND_END		0x03

#endif /* HABANALABS_H_ */
