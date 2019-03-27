/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 */

/* This file implements Queue accessor methods. */

/*
 * vmci_qpair is an interface that hides the queue pair internals. Rather than
 * access each queue in a pair directly, operations are performed on the queue
 * as a whole. This is simpler and less error-prone, and allows for future
 * queue pair features to be added under the hood with no change to the client
 * code.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "vmci_kernel_api.h"
#include "vmci_kernel_defs.h"
#include "vmci_kernel_if.h"
#include "vmci_queue.h"
#include "vmci_queue_pair.h"

/* This structure is opaque to the clients. */
struct vmci_qpair {
	struct vmci_handle	handle;
	struct vmci_queue	*produce_q;
	struct vmci_queue	*consume_q;
	uint64_t		produce_q_size;
	uint64_t		consume_q_size;
	vmci_id			peer;
	uint32_t		flags;
	vmci_privilege_flags	priv_flags;
	uint32_t		blocked;
	vmci_event		event;
};

static void	vmci_qpair_get_queue_headers(const struct vmci_qpair *qpair,
		    struct vmci_queue_header **produce_q_header,
		    struct vmci_queue_header **consume_q_header);

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_add_producer_tail --
 *
 *     Helper routine to increment the Producer Tail.
 *
 * Results:
 *     VMCI_ERROR_NOT_FOUND if the vmm_world registered with the queue cannot
 *     be found. Otherwise VMCI_SUCCESS.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static inline int
vmci_queue_add_producer_tail(struct vmci_queue *queue,
    size_t add, uint64_t queue_size)
{

	vmci_queue_header_add_producer_tail(queue->q_header, add, queue_size);
	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_add_consumer_head --
 *
 *     Helper routine to increment the Consumer Head.
 *
 * Results:
 *     VMCI_ERROR_NOT_FOUND if the vmm_world registered with the queue cannot
 *     be found. Otherwise VMCI_SUCCESS.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static inline int
vmci_queue_add_consumer_head(struct vmci_queue *queue,
    size_t add, uint64_t queue_size)
{

	vmci_queue_header_add_consumer_head(queue->q_header, add, queue_size);
	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_get_queue_headers --
 *
 *     Helper routine that will retrieve the produce and consume headers of a
 *     given queue pair.
 *
 * Results:
 *     VMCI_SUCCESS if either current or saved queue headers are found.
 *     Appropriate error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_qpair_get_queue_headers(const struct vmci_qpair *qpair,
    struct vmci_queue_header **produce_q_header,
    struct vmci_queue_header **consume_q_header)
{

	ASSERT((qpair->produce_q != NULL) && (qpair->consume_q != NULL));
	*produce_q_header = qpair->produce_q->q_header;
	*consume_q_header = qpair->consume_q->q_header;
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_alloc --
 *
 *     This is the client interface for allocating the memory for a vmci_qpair
 *     structure and then attaching to the underlying queue. If an error occurs
 *     allocating the memory for the vmci_qpair structure, no attempt is made to
 *     attach. If an error occurs attaching, then there's the vmci_qpair
 *     structure is freed.
 *
 * Results:
 *     An err, if < 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_qpair_alloc(struct vmci_qpair **qpair, struct vmci_handle *handle,
    uint64_t produce_q_size, uint64_t consume_q_size, vmci_id peer,
    uint32_t flags, vmci_privilege_flags priv_flags)
{
	struct vmci_qpair *my_qpair;
	vmci_event_release_cb wakeup_cb;
	void *client_data;
	int retval;

	/*
	 * Restrict the size of a queuepair. Though the device enforces a limit
	 * on the total amount of memory that can be allocated to queuepairs for
	 * a guest, we avoid unnecessarily allocating a lot of memory. Also, we
	 * try to allocate this memory before we make the queuepair allocation
	 * hypercall.
	 *
	 * (Note that this doesn't prevent all cases; a user with only this much
	 * physical memory could still get into trouble.) The error used by the
	 * device is NO_RESOURCES, so use that here too.
	 */

	if (produce_q_size + consume_q_size <
	    MAX(produce_q_size, consume_q_size) ||
	    produce_q_size + consume_q_size > VMCI_MAX_GUEST_QP_MEMORY)
		return (VMCI_ERROR_NO_RESOURCES);

	if (flags & VMCI_QPFLAG_NONBLOCK)
		return (VMCI_ERROR_INVALID_ARGS);

	my_qpair = vmci_alloc_kernel_mem(sizeof(*my_qpair), VMCI_MEMORY_NORMAL);
	if (!my_qpair)
		return (VMCI_ERROR_NO_MEM);

	my_qpair->produce_q_size = produce_q_size;
	my_qpair->consume_q_size = consume_q_size;
	my_qpair->peer = peer;
	my_qpair->flags = flags;
	my_qpair->priv_flags = priv_flags;

	client_data = NULL;
	wakeup_cb = NULL;

	retval = vmci_queue_pair_alloc(handle, &my_qpair->produce_q,
	    my_qpair->produce_q_size, &my_qpair->consume_q,
	    my_qpair->consume_q_size, my_qpair->peer, my_qpair->flags,
	    my_qpair->priv_flags);

	if (retval < VMCI_SUCCESS) {
		vmci_free_kernel_mem(my_qpair, sizeof(*my_qpair));
		return (retval);
	}

	*qpair = my_qpair;
	my_qpair->handle = *handle;

	return (retval);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_detach --
 *
 *     This is the client interface for detaching from a vmci_qpair. Note that
 *     this routine will free the memory allocated for the vmci_qpair structure,
 *     too.
 *
 * Results:
 *     An error, if < 0.
 *
 * Side effects:
 *     Will clear the caller's pointer to the vmci_qpair structure.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_qpair_detach(struct vmci_qpair **qpair)
{
	struct vmci_qpair *old_qpair;
	int result;

	if (!qpair || !(*qpair))
		return (VMCI_ERROR_INVALID_ARGS);

	old_qpair = *qpair;
	result = vmci_queue_pair_detach(old_qpair->handle);

	/*
	 * The guest can fail to detach for a number of reasons, and if it does
	 * so, it will cleanup the entry (if there is one). We need to release
	 * the qpair struct here; there isn't much the caller can do, and we
	 * don't want to leak.
	 */

	if (old_qpair->flags & VMCI_QPFLAG_LOCAL)
		vmci_destroy_event(&old_qpair->event);

	vmci_free_kernel_mem(old_qpair, sizeof(*old_qpair));
	*qpair = NULL;

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_get_produce_indexes --
 *
 *     This is the client interface for getting the current indexes of the
 *     qpair from the point of the view of the caller as the producer.
 *
 * Results:
 *     err, if < 0
 *     Success otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_qpair_get_produce_indexes(const struct vmci_qpair *qpair,
    uint64_t *producer_tail, uint64_t *consumer_head)
{
	struct vmci_queue_header *consume_q_header;
	struct vmci_queue_header *produce_q_header;

	if (!qpair)
		return (VMCI_ERROR_INVALID_ARGS);

	vmci_qpair_get_queue_headers(qpair, &produce_q_header,
	    &consume_q_header);
	vmci_queue_header_get_pointers(produce_q_header, consume_q_header,
	    producer_tail, consumer_head);

	if ((producer_tail && *producer_tail >= qpair->produce_q_size) ||
	    (consumer_head && *consumer_head >= qpair->produce_q_size))
		return (VMCI_ERROR_INVALID_SIZE);

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_get_consume_indexes --
 *
 *     This is the client interface for getting the current indexes of the
 *     QPair from the point of the view of the caller as the consumer.
 *
 * Results:
 *     err, if < 0
 *     Success otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_qpair_get_consume_indexes(const struct vmci_qpair *qpair,
    uint64_t *consumer_tail, uint64_t *producer_head)
{
	struct vmci_queue_header *consume_q_header;
	struct vmci_queue_header *produce_q_header;

	if (!qpair)
		return (VMCI_ERROR_INVALID_ARGS);

	vmci_qpair_get_queue_headers(qpair, &produce_q_header,
	    &consume_q_header);
	vmci_queue_header_get_pointers(consume_q_header, produce_q_header,
	    consumer_tail, producer_head);

	if ((consumer_tail && *consumer_tail >= qpair->consume_q_size) ||
	    (producer_head && *producer_head >= qpair->consume_q_size))
		return (VMCI_ERROR_INVALID_SIZE);

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_produce_free_space --
 *
 *     This is the client interface for getting the amount of free space in the
 *     QPair from the point of the view of the caller as the producer which is
 *     the common case.
 *
 * Results:
 *     Err, if < 0.
 *     Full queue if = 0.
 *     Number of available bytes into which data can be enqueued if > 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int64_t
vmci_qpair_produce_free_space(const struct vmci_qpair *qpair)
{
	struct vmci_queue_header *consume_q_header;
	struct vmci_queue_header *produce_q_header;
	int64_t result;

	if (!qpair)
		return (VMCI_ERROR_INVALID_ARGS);

	vmci_qpair_get_queue_headers(qpair, &produce_q_header,
	    &consume_q_header);
	result = vmci_queue_header_free_space(produce_q_header, consume_q_header,
	    qpair->produce_q_size);

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_consume_free_space --
 *
 *     This is the client interface for getting the amount of free space in the
 *     QPair from the point of the view of the caller as the consumer which is
 *     not the common case (see vmci_qpair_Produce_free_space(), above).
 *
 * Results:
 *     Err, if < 0.
 *     Full queue if = 0.
 *     Number of available bytes into which data can be enqueued if > 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int64_t
vmci_qpair_consume_free_space(const struct vmci_qpair *qpair)
{
	struct vmci_queue_header *consume_q_header;
	struct vmci_queue_header *produce_q_header;
	int64_t result;

	if (!qpair)
		return (VMCI_ERROR_INVALID_ARGS);

	vmci_qpair_get_queue_headers(qpair, &produce_q_header,
	    &consume_q_header);
	result = vmci_queue_header_free_space(consume_q_header, produce_q_header,
	    qpair->consume_q_size);

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_produce_buf_ready --
 *
 *     This is the client interface for getting the amount of enqueued data in
 *     the QPair from the point of the view of the caller as the producer which
 *     is not the common case (see vmci_qpair_Consume_buf_ready(), above).
 *
 * Results:
 *     Err, if < 0.
 *     Empty queue if = 0.
 *     Number of bytes ready to be dequeued if > 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int64_t
vmci_qpair_produce_buf_ready(const struct vmci_qpair *qpair)
{
	struct vmci_queue_header *consume_q_header;
	struct vmci_queue_header *produce_q_header;
	int64_t result;

	if (!qpair)
		return (VMCI_ERROR_INVALID_ARGS);

	vmci_qpair_get_queue_headers(qpair, &produce_q_header,
	    &consume_q_header);
	result = vmci_queue_header_buf_ready(produce_q_header, consume_q_header,
	    qpair->produce_q_size);

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_consume_buf_ready --
 *
 *     This is the client interface for getting the amount of enqueued data in
 *     the QPair from the point of the view of the caller as the consumer which
 *     is the normal case.
 *
 * Results:
 *     Err, if < 0.
 *     Empty queue if = 0.
 *     Number of bytes ready to be dequeued if > 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int64_t
vmci_qpair_consume_buf_ready(const struct vmci_qpair *qpair)
{
	struct vmci_queue_header *consume_q_header;
	struct vmci_queue_header *produce_q_header;
	int64_t result;

	if (!qpair)
		return (VMCI_ERROR_INVALID_ARGS);

	vmci_qpair_get_queue_headers(qpair, &produce_q_header,
	    &consume_q_header);
	result = vmci_queue_header_buf_ready(consume_q_header, produce_q_header,
	    qpair->consume_q_size);

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * enqueue --
 *
 *     Enqueues a given buffer to the produce queue using the provided function.
 *     As many bytes as possible (space available in the queue) are enqueued.
 *
 * Results:
 *     VMCI_ERROR_QUEUEPAIR_NOSPACE if no space was available to enqueue data.
 *     VMCI_ERROR_INVALID_SIZE, if any queue pointer is outside the queue
 *     (as defined by the queue size).
 *     VMCI_ERROR_INVALID_ARGS, if an error occured when accessing the buffer.
 *     VMCI_ERROR_QUEUEPAIR_NOTATTACHED, if the queue pair pages aren't
 *     available.
 *     Otherwise, the number of bytes written to the queue is returned.
 *
 * Side effects:
 *     Updates the tail pointer of the produce queue.
 *
 *------------------------------------------------------------------------------
 */

static ssize_t
enqueue(struct vmci_queue *produce_q, struct vmci_queue *consume_q,
    const uint64_t produce_q_size, const void *buf, size_t buf_size,
    int buf_type, vmci_memcpy_to_queue_func memcpy_to_queue, bool can_block)
{
	ssize_t result;
	size_t written;
	int64_t free_space;
	uint64_t tail;

	ASSERT((produce_q != NULL) && (consume_q != NULL));

	free_space = vmci_queue_header_free_space(produce_q->q_header,
	    consume_q->q_header,
	    produce_q_size);
	if (free_space == 0)
		return (VMCI_ERROR_QUEUEPAIR_NOSPACE);

	if (free_space < VMCI_SUCCESS)
		return ((ssize_t)free_space);

	written = (size_t)(free_space > buf_size ? buf_size : free_space);
	tail = vmci_queue_header_producer_tail(produce_q->q_header);
	if (LIKELY(tail + written < produce_q_size))
		result = memcpy_to_queue(produce_q, tail, buf, 0, written,
		    buf_type, can_block);
	else {
		/* Tail pointer wraps around. */

		const size_t tmp = (size_t)(produce_q_size - tail);

		result = memcpy_to_queue(produce_q, tail, buf, 0, tmp, buf_type,
		    can_block);
		if (result >= VMCI_SUCCESS)
			result = memcpy_to_queue(produce_q, 0, buf, tmp,
			    written - tmp, buf_type, can_block);
	}

	if (result < VMCI_SUCCESS)
		return (result);

	result = vmci_queue_add_producer_tail(produce_q, written,
	    produce_q_size);
	if (result < VMCI_SUCCESS)
		return (result);
	return (written);
}

/*
 *------------------------------------------------------------------------------
 *
 * dequeue --
 *
 *     Dequeues data (if available) from the given consume queue. Writes data
 *     to the user provided buffer using the provided function.
 *
 * Results:
 *     VMCI_ERROR_QUEUEPAIR_NODATA if no data was available to dequeue.
 *     VMCI_ERROR_INVALID_SIZE, if any queue pointer is outside the queue
 *     (as defined by the queue size).
 *     VMCI_ERROR_INVALID_ARGS, if an error occured when accessing the buffer.
 *     VMCI_ERROR_NOT_FOUND, if the vmm_world registered with the queue pair
 *     cannot be found.
 *     Otherwise the number of bytes dequeued is returned.
 *
 * Side effects:
 *     Updates the head pointer of the consume queue.
 *
 *------------------------------------------------------------------------------
 */

static ssize_t
dequeue(struct vmci_queue *produce_q,
    struct vmci_queue *consume_q, const uint64_t consume_q_size, void *buf,
    size_t buf_size, int buf_type,
    vmci_memcpy_from_queue_func memcpy_from_queue, bool update_consumer,
    bool can_block)
{
	ssize_t result;
	size_t read;
	int64_t buf_ready;
	uint64_t head;

	ASSERT((produce_q != NULL) && (consume_q != NULL));

	buf_ready = vmci_queue_header_buf_ready(consume_q->q_header,
	    produce_q->q_header, consume_q_size);
	if (buf_ready == 0)
		return (VMCI_ERROR_QUEUEPAIR_NODATA);
	if (buf_ready < VMCI_SUCCESS)
		return ((ssize_t)buf_ready);

	read = (size_t)(buf_ready > buf_size ? buf_size : buf_ready);
	head = vmci_queue_header_consumer_head(produce_q->q_header);
	if (LIKELY(head + read < consume_q_size))
		result = memcpy_from_queue(buf, 0, consume_q, head, read,
		    buf_type, can_block);
	else {
		/* Head pointer wraps around. */

		const size_t tmp = (size_t)(consume_q_size - head);

		result = memcpy_from_queue(buf, 0, consume_q, head, tmp,
		    buf_type, can_block);
		if (result >= VMCI_SUCCESS)
			result = memcpy_from_queue(buf, tmp, consume_q, 0,
			    read - tmp, buf_type, can_block);
	}

	if (result < VMCI_SUCCESS)
		return (result);

	if (update_consumer) {
		result = vmci_queue_add_consumer_head(produce_q, read,
		    consume_q_size);
		if (result < VMCI_SUCCESS)
			return (result);
	}

	return (read);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_enqueue --
 *
 *     This is the client interface for enqueueing data into the queue.
 *
 * Results:
 *     Err, if < 0.
 *     Number of bytes enqueued if >= 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

ssize_t
vmci_qpair_enqueue(struct vmci_qpair *qpair, const void *buf, size_t buf_size,
    int buf_type)
{
	ssize_t result;

	if (!qpair || !buf)
		return (VMCI_ERROR_INVALID_ARGS);

	result = enqueue(qpair->produce_q, qpair->consume_q,
	    qpair->produce_q_size, buf, buf_size, buf_type,
	    qpair->flags & VMCI_QPFLAG_LOCAL?
	    vmci_memcpy_to_queue_local : vmci_memcpy_to_queue,
	    !(qpair->flags & VMCI_QPFLAG_NONBLOCK));

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_dequeue --
 *
 *     This is the client interface for dequeueing data from the queue.
 *
 * Results:
 *     Err, if < 0.
 *     Number of bytes dequeued if >= 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

ssize_t
vmci_qpair_dequeue(struct vmci_qpair *qpair, void *buf, size_t buf_size,
    int buf_type)
{
	ssize_t result;

	if (!qpair || !buf)
		return (VMCI_ERROR_INVALID_ARGS);

	result = dequeue(qpair->produce_q, qpair->consume_q,
	    qpair->consume_q_size, buf, buf_size, buf_type,
	    qpair->flags & VMCI_QPFLAG_LOCAL?
	    vmci_memcpy_from_queue_local : vmci_memcpy_from_queue, true,
	    !(qpair->flags & VMCI_QPFLAG_NONBLOCK));

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_peek --
 *
 *     This is the client interface for peeking into a queue.  (I.e., copy
 *     data from the queue without updating the head pointer.)
 *
 * Results:
 *     Err, if < 0.
 *     Number of bytes peeked, if >= 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

ssize_t
vmci_qpair_peek(struct vmci_qpair *qpair, void *buf, size_t buf_size,
    int buf_type)
{
	ssize_t result;

	if (!qpair || !buf)
		return (VMCI_ERROR_INVALID_ARGS);

	result = dequeue(qpair->produce_q, qpair->consume_q,
	    qpair->consume_q_size, buf, buf_size, buf_type,
	    qpair->flags & VMCI_QPFLAG_LOCAL?
	    vmci_memcpy_from_queue_local : vmci_memcpy_from_queue, false,
	    !(qpair->flags & VMCI_QPFLAG_NONBLOCK));

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_enquev --
 *
 *     This is the client interface for enqueueing data into the queue.
 *
 * Results:
 *     Err, if < 0.
 *     Number of bytes enqueued if >= 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

ssize_t
vmci_qpair_enquev(struct vmci_qpair *qpair, void *iov, size_t iov_size,
    int buf_type)
{
	ssize_t result;

	if (!qpair || !iov)
		return (VMCI_ERROR_INVALID_ARGS);

	result = enqueue(qpair->produce_q, qpair->consume_q,
	    qpair->produce_q_size, iov, iov_size, buf_type,
	    qpair->flags & VMCI_QPFLAG_LOCAL?
	    vmci_memcpy_to_queue_v_local : vmci_memcpy_to_queue_v,
	    !(qpair->flags & VMCI_QPFLAG_NONBLOCK));

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_dequev --
 *
 *     This is the client interface for dequeueing data from the queue.
 *
 * Results:
 *     Err, if < 0.
 *     Number of bytes dequeued if >= 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

ssize_t
vmci_qpair_dequev(struct vmci_qpair *qpair, void *iov, size_t iov_size,
    int buf_type)
{
	ssize_t result;

	if (!qpair || !iov)
		return (VMCI_ERROR_INVALID_ARGS);

	result = dequeue(qpair->produce_q, qpair->consume_q,
	    qpair->consume_q_size, iov, iov_size, buf_type,
	    qpair->flags & VMCI_QPFLAG_LOCAL?
	    vmci_memcpy_from_queue_v_local : vmci_memcpy_from_queue_v, true,
	    !(qpair->flags & VMCI_QPFLAG_NONBLOCK));

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qpair_peekv --
 *
 *     This is the client interface for peeking into a queue.  (I.e., copy
 *     data from the queue without updating the head pointer.)
 *
 * Results:
 *     Err, if < 0.
 *     Number of bytes peeked, if >= 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

ssize_t
vmci_qpair_peekv(struct vmci_qpair *qpair, void *iov, size_t iov_size,
    int buf_type)
{
	ssize_t result;

	if (!qpair || !iov)
		return (VMCI_ERROR_INVALID_ARGS);

	result = dequeue(qpair->produce_q, qpair->consume_q,
	    qpair->consume_q_size, iov, iov_size, buf_type,
	    qpair->flags & VMCI_QPFLAG_LOCAL?
	    vmci_memcpy_from_queue_v_local : vmci_memcpy_from_queue_v, false,
	    !(qpair->flags & VMCI_QPFLAG_NONBLOCK));

	return (result);
}
