/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* Defines the queue structure and helper functions to enqueue/dequeue items. */

#ifndef _VMCI_QUEUE_H_
#define _VMCI_QUEUE_H_

/*
 * vmci_queue
 *
 * This data type contains the information about a queue.
 *
 * There are two queues (hence, queue pairs) per transaction model between a
 * pair of end points, A & B. One queue is used by end point A to transmit
 * commands and responses to B. The other queue is used by B to transmit
 * commands and responses.
 *
 * vmci_queue_kernel_if is a per-OS defined queue structure. It contains
 * either a direct pointer to the linear address of the buffer contents or a
 * pointer to structures which help the OS locate those data pages.
 * See vmci_kernel_if.c for its definition.
 */

struct vmci_queue_kernel_if;

struct vmci_queue {
	struct vmci_queue_header	*q_header;
	struct vmci_queue_header	*saved_header;
	struct vmci_queue_kernel_if	*kernel_if;
};

#define BUF_TYPE	int

/*
 *------------------------------------------------------------------------------
 *
 * vmci_memcpy{to,from}_queue_func() prototypes. Functions of these types are
 * passed around to enqueue and dequeue routines. Note that often the functions
 * passed are simply wrappers around memcpy itself.
 *
 * Note: In order for the memcpy typedefs to be compatible with the VMKernel,
 * there's an unused last parameter for the hosted side. In ESX, that parameter
 * holds a buffer type.
 *
 *------------------------------------------------------------------------------
 */
typedef	int vmci_memcpy_to_queue_func(struct vmci_queue *queue,
	    uint64_t queue_offset, const void *src, size_t src_offset,
	    size_t size, BUF_TYPE buf_type, bool can_block);
typedef	int vmci_memcpy_from_queue_func(void *dest, size_t dest_offset,
	    const struct vmci_queue *queue, uint64_t queue_offset, size_t size,
	    BUF_TYPE buf_type, bool can_block);

/*
 *------------------------------------------------------------------------------
 *
 * vmci_memcpy{to,from}_queue_[v]_[local]() prototypes
 *
 * Note that these routines are NOT SAFE to call on a host end-point until the
 * guest end of the queue pair has attached -AND- SetPageStore(). The VMX
 * crosstalk device will issue the SetPageStore() on behalf of the guest when
 * the guest creates a QueuePair or attaches to one created by the host. So, if
 * the guest notifies the host that it's attached then the queue is safe to use.
 * Also, if the host registers notification of the connection of the guest, then
 * it will only receive that notification when the guest has issued the
 * SetPageStore() call and not before (when the guest had attached).
 *
 *------------------------------------------------------------------------------
 */

int	vmci_memcpy_to_queue(struct vmci_queue *queue, uint64_t queue_offset,
	    const void *src, size_t src_offset, size_t size, BUF_TYPE buf_type,
	    bool can_block);
int	vmci_memcpy_from_queue(void *dest, size_t dest_offset,
	    const struct vmci_queue *queue, uint64_t queue_offset, size_t size,
	    BUF_TYPE buf_type, bool can_block);
int	vmci_memcpy_to_queue_local(struct vmci_queue *queue,
	    uint64_t queue_offset, const void *src, size_t src_offset,
	    size_t size, BUF_TYPE buf_type, bool can_block);
int	vmci_memcpy_from_queue_local(void *dest, size_t dest_offset,
	    const struct vmci_queue *queue, uint64_t queue_offset, size_t size,
	    BUF_TYPE buf_type, bool can_block);

int	vmci_memcpy_to_queue_v(struct vmci_queue *queue, uint64_t queue_offset,
	    const void *src, size_t src_offset, size_t size, BUF_TYPE buf_type,
	    bool can_block);
int	vmci_memcpy_from_queue_v(void *dest, size_t dest_offset,
	    const struct vmci_queue *queue, uint64_t queue_offset, size_t size,
	    BUF_TYPE buf_type, bool can_block);

static inline int
vmci_memcpy_to_queue_v_local(struct vmci_queue *queue, uint64_t queue_offset,
   const void *src, size_t src_offset, size_t size, int buf_type,
   bool can_block)
{

	return (vmci_memcpy_to_queue_v(queue, queue_offset, src, src_offset,
	    size, buf_type, can_block));
}

static inline int
vmci_memcpy_from_queue_v_local(void *dest, size_t dest_offset,
    const struct vmci_queue *queue, uint64_t queue_offset, size_t size,
    int buf_type, bool can_block)
{

	return (vmci_memcpy_from_queue_v(dest, dest_offset, queue, queue_offset,
	    size, buf_type, can_block));
}

#endif /* !_VMCI_QUEUE_H_ */
