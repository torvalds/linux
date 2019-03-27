/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 */

/* This file implements defines and helper functions. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/uio.h>

#include <machine/bus.h>

#include "vmci.h"
#include "vmci_defs.h"
#include "vmci_kernel_defs.h"
#include "vmci_kernel_if.h"
#include "vmci_queue.h"

struct vmci_queue_kernel_if {
	size_t			num_pages;	/* Num pages incl. header. */
	struct vmci_dma_alloc	*dmas;		/* For dma alloc. */
};

/*
 *------------------------------------------------------------------------------
 *
 * vmci_init_lock
 *
 *     Initializes the lock. Must be called before use.
 *
 * Results:
 *     Always VMCI_SUCCESS.
 *
 * Side effects:
 *     Thread can block.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_init_lock(vmci_lock *lock, char *name)
{

	mtx_init(lock, name, NULL, MTX_DEF | MTX_NOWITNESS);
	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_cleanup_lock
 *
 *     Cleanup the lock. Must be called before deallocating lock.
 *
 * Results:
 *     None
 *
 * Side effects:
 *     Deletes kernel lock state
 *
 *------------------------------------------------------------------------------
 */

void
vmci_cleanup_lock(vmci_lock *lock)
{

	mtx_destroy(lock);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_grab_lock
 *
 *     Grabs the given lock.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Thread can block.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_grab_lock(vmci_lock *lock)
{

	mtx_lock(lock);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_release_lock
 *
 *     Releases the given lock.
 *
 * Results:
 *     None
 *
 * Side effects:
 *     A thread blocked on this lock may wake up.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_release_lock(vmci_lock *lock)
{

	mtx_unlock(lock);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_grab_lock_bh
 *
 *     Grabs the given lock.
 *
 * Results:
 *     None
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_grab_lock_bh(vmci_lock *lock)
{

	mtx_lock(lock);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_release_lock_bh
 *
 *     Releases the given lock.
 *
 * Results:
 *     None
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_release_lock_bh(vmci_lock *lock)
{

	mtx_unlock(lock);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_alloc_kernel_mem
 *
 *     Allocate physically contiguous memory for the VMCI driver.
 *
 * Results:
 *     The address allocated or NULL on error.
 *
 *
 * Side effects:
 *     Memory may be allocated.
 *
 *------------------------------------------------------------------------------
 */

void *
vmci_alloc_kernel_mem(size_t size, int flags)
{
	void *ptr;

	if ((flags & VMCI_MEMORY_ATOMIC) != 0)
		ptr = contigmalloc(size, M_DEVBUF, M_NOWAIT, 0, 0xFFFFFFFF,
		    8, 1024 * 1024);
	else
		ptr = contigmalloc(size, M_DEVBUF, M_WAITOK, 0, 0xFFFFFFFF,
		    8, 1024 * 1024);

	return (ptr);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_free_kernel_mem
 *
 *     Free kernel memory allocated for the VMCI driver.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Memory is freed.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_free_kernel_mem(void *ptr, size_t size)
{

	contigfree(ptr, size, M_DEVBUF);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_can_schedule_delayed_work --
 *
 *     Checks to see if the given platform supports delayed work callbacks.
 *
 * Results:
 *     true if it does. false otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

bool
vmci_can_schedule_delayed_work(void)
{

	return (true);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_schedule_delayed_work --
 *
 *     Schedule the specified callback.
 *
 * Results:
 *     Zero on success, error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_schedule_delayed_work(vmci_work_fn *work_fn, void *data)
{

	return (vmci_schedule_delayed_work_fn(work_fn, data));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_create_event --
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_create_event(vmci_event *event)
{

	sema_init(event, 0, "vmci_event");
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_destroy_event --
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_destroy_event(vmci_event *event)
{

	if (mtx_owned(&event->sema_mtx))
		sema_destroy(event);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_signal_event --
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_signal_event(vmci_event *event)
{

	sema_post(event);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_wait_on_event --
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_wait_on_event(vmci_event *event, vmci_event_release_cb release_cb,
    void *client_data)
{

	release_cb(client_data);
	sema_wait(event);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_mutex_init --
 *
 *     Initializes the mutex. Must be called before use.
 *
 * Results:
 *     Success.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_mutex_init(vmci_mutex *mutex, char *name)
{

	mtx_init(mutex, name, NULL, MTX_DEF | MTX_NOWITNESS);
	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_mutex_destroy --
 *
 *     Destroys the mutex.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_mutex_destroy(vmci_mutex *mutex)
{

	mtx_destroy(mutex);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_mutex_acquire --
 *
 *     Acquires the mutex.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Thread may block.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_mutex_acquire(vmci_mutex *mutex)
{

	mtx_lock(mutex);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_mutex_release --
 *
 *     Releases the mutex.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     May wake up the thread blocking on this mutex.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_mutex_release(vmci_mutex *mutex)
{

	mtx_unlock(mutex);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_alloc_queue --
 *
 *     Allocates kernel queue pages of specified size with IOMMU mappings, plus
 *     space for the queue structure/kernel interface and the queue header.
 *
 * Results:
 *     Pointer to the queue on success, NULL otherwise.
 *
 * Side effects:
 *     Memory is allocated.
 *
 *------------------------------------------------------------------------------
 */

void *
vmci_alloc_queue(uint64_t size, uint32_t flags)
{
	struct vmci_queue *queue;
	size_t i;
	const size_t num_pages = CEILING(size, PAGE_SIZE) + 1;
	const size_t dmas_size = num_pages * sizeof(struct vmci_dma_alloc);
	const size_t queue_size =
	    sizeof(*queue) + sizeof(*(queue->kernel_if)) + dmas_size;

	/* Size should be enforced by vmci_qpair_alloc(), double-check here. */
	if (size > VMCI_MAX_GUEST_QP_MEMORY) {
		ASSERT(false);
		return (NULL);
	}

	queue = malloc(queue_size, M_DEVBUF, M_NOWAIT);
	if (!queue)
		return (NULL);

	queue->q_header = NULL;
	queue->saved_header = NULL;
	queue->kernel_if = (struct vmci_queue_kernel_if *)(queue + 1);
	queue->kernel_if->num_pages = num_pages;
	queue->kernel_if->dmas = (struct vmci_dma_alloc *)(queue->kernel_if +
	    1);
	for (i = 0; i < num_pages; i++) {
		vmci_dma_malloc(PAGE_SIZE, 1, &queue->kernel_if->dmas[i]);
		if (!queue->kernel_if->dmas[i].dma_vaddr) {
			/* Size excl. the header. */
			vmci_free_queue(queue, i * PAGE_SIZE);
			return (NULL);
		}
	}

	/* Queue header is the first page. */
	queue->q_header = (void *)queue->kernel_if->dmas[0].dma_vaddr;

	return ((void *)queue);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_free_queue --
 *
 *     Frees kernel VA space for a given queue and its queue header, and frees
 *     physical data pages.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Memory is freed.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_free_queue(void *q, uint64_t size)
{
	struct vmci_queue *queue = q;

	if (queue) {
		const size_t num_pages = CEILING(size, PAGE_SIZE) + 1;
		uint64_t i;

		/* Given size doesn't include header, so add in a page here. */
		for (i = 0; i < num_pages; i++)
			vmci_dma_free(&queue->kernel_if->dmas[i]);
		free(queue, M_DEVBUF);
	}
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_alloc_ppn_set --
 *
 *     Allocates two list of PPNs --- one for the pages in the produce queue,
 *     and the other for the pages in the consume queue. Intializes the list of
 *     PPNs with the page frame numbers of the KVA for the two queues (and the
 *     queue headers).
 *
 * Results:
 *     Success or failure.
 *
 * Side effects:
 *     Memory may be allocated.
 *
 *-----------------------------------------------------------------------------
 */

int
vmci_alloc_ppn_set(void *prod_q, uint64_t num_produce_pages, void *cons_q,
    uint64_t num_consume_pages, struct ppn_set *ppn_set)
{
	struct vmci_queue *consume_q = cons_q;
	struct vmci_queue *produce_q = prod_q;
	vmci_ppn_list consume_ppns;
	vmci_ppn_list produce_ppns;
	uint64_t i;

	if (!produce_q || !num_produce_pages || !consume_q ||
	    !num_consume_pages || !ppn_set)
		return (VMCI_ERROR_INVALID_ARGS);

	if (ppn_set->initialized)
		return (VMCI_ERROR_ALREADY_EXISTS);

	produce_ppns =
	    vmci_alloc_kernel_mem(num_produce_pages * sizeof(*produce_ppns),
	    VMCI_MEMORY_NORMAL);
	if (!produce_ppns)
		return (VMCI_ERROR_NO_MEM);

	consume_ppns =
	    vmci_alloc_kernel_mem(num_consume_pages * sizeof(*consume_ppns),
	    VMCI_MEMORY_NORMAL);
	if (!consume_ppns) {
		vmci_free_kernel_mem(produce_ppns,
		    num_produce_pages * sizeof(*produce_ppns));
		return (VMCI_ERROR_NO_MEM);
	}

	for (i = 0; i < num_produce_pages; i++) {
		unsigned long pfn;

		produce_ppns[i] =
		    pfn = produce_q->kernel_if->dmas[i].dma_paddr >> PAGE_SHIFT;

		/*
		 * Fail allocation if PFN isn't supported by hypervisor.
		 */

		if (sizeof(pfn) >
		    sizeof(*produce_ppns) && pfn != produce_ppns[i])
			goto ppn_error;
	}
	for (i = 0; i < num_consume_pages; i++) {
		unsigned long pfn;

		consume_ppns[i] =
		    pfn = consume_q->kernel_if->dmas[i].dma_paddr >> PAGE_SHIFT;

		/*
		 * Fail allocation if PFN isn't supported by hypervisor.
		 */

		if (sizeof(pfn) >
		    sizeof(*consume_ppns) && pfn != consume_ppns[i])
			goto ppn_error;

	}

	ppn_set->num_produce_pages = num_produce_pages;
	ppn_set->num_consume_pages = num_consume_pages;
	ppn_set->produce_ppns = produce_ppns;
	ppn_set->consume_ppns = consume_ppns;
	ppn_set->initialized = true;
	return (VMCI_SUCCESS);

ppn_error:
	vmci_free_kernel_mem(produce_ppns, num_produce_pages *
	    sizeof(*produce_ppns));
	vmci_free_kernel_mem(consume_ppns, num_consume_pages *
	    sizeof(*consume_ppns));
	return (VMCI_ERROR_INVALID_ARGS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_free_ppn_set --
 *
 *     Frees the two list of PPNs for a queue pair.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_free_ppn_set(struct ppn_set *ppn_set)
{

	ASSERT(ppn_set);
	if (ppn_set->initialized) {
		/* Do not call these functions on NULL inputs. */
		ASSERT(ppn_set->produce_ppns && ppn_set->consume_ppns);
		vmci_free_kernel_mem(ppn_set->produce_ppns,
		    ppn_set->num_produce_pages *
		    sizeof(*ppn_set->produce_ppns));
		vmci_free_kernel_mem(ppn_set->consume_ppns,
		    ppn_set->num_consume_pages *
		    sizeof(*ppn_set->consume_ppns));
	}
	memset(ppn_set, 0, sizeof(*ppn_set));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_populate_ppn_list --
 *
 *     Populates the list of PPNs in the hypercall structure with the PPNS
 *     of the produce queue and the consume queue.
 *
 * Results:
 *     VMCI_SUCCESS.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_populate_ppn_list(uint8_t *call_buf, const struct ppn_set *ppn_set)
{

	ASSERT(call_buf && ppn_set && ppn_set->initialized);
	memcpy(call_buf, ppn_set->produce_ppns,
	    ppn_set->num_produce_pages * sizeof(*ppn_set->produce_ppns));
	memcpy(call_buf + ppn_set->num_produce_pages *
	    sizeof(*ppn_set->produce_ppns), ppn_set->consume_ppns,
	    ppn_set->num_consume_pages * sizeof(*ppn_set->consume_ppns));

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_memcpy_{to,from}iovec --
 *
 *     These helper routines will copy the specified bytes to/from memory that's
 *     specified as a struct iovec.  The routines can not verify the correctness
 *     of the struct iovec's contents.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *------------------------------------------------------------------------------
 */

static inline void
vmci_memcpy_toiovec(struct iovec *iov, uint8_t *src, size_t len)
{

	while (len > 0) {
		if (iov->iov_len) {
			size_t to_copy = MIN(iov->iov_len, len);
			memcpy(iov->iov_base, src, to_copy);
			src += to_copy;
			len -= to_copy;
			iov->iov_base = (void *)((uintptr_t) iov->iov_base +
			    to_copy);
			iov->iov_len -= to_copy;
		}
		iov++;
	}
}

static inline void
vmci_memcpy_fromiovec(uint8_t *dst, struct iovec *iov, size_t len)
{

	while (len > 0) {
		if (iov->iov_len) {
			size_t to_copy = MIN(iov->iov_len, len);
			memcpy(dst, iov->iov_base, to_copy);
			dst += to_copy;
			len -= to_copy;
			iov->iov_base = (void *)((uintptr_t) iov->iov_base +
			    to_copy);
			iov->iov_len -= to_copy;
		}
		iov++;
	}
}

/*
 *------------------------------------------------------------------------------
 *
 * __vmci_memcpy_to_queue --
 *
 *     Copies from a given buffer or iovector to a VMCI Queue. Assumes that
 *     offset + size does not wrap around in the queue.
 *
 * Results:
 *     Zero on success, negative error code on failure.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

#pragma GCC diagnostic ignored "-Wcast-qual"
static int
__vmci_memcpy_to_queue(struct vmci_queue *queue, uint64_t queue_offset,
    const void *src, size_t size, bool is_iovec)
{
	struct vmci_queue_kernel_if *kernel_if = queue->kernel_if;
	size_t bytes_copied = 0;

	while (bytes_copied < size) {
		const uint64_t page_index =
		    (queue_offset + bytes_copied) / PAGE_SIZE;
		const size_t page_offset =
		    (queue_offset + bytes_copied) & (PAGE_SIZE - 1);
		void *va;
		size_t to_copy;

		/* Skip header. */
		va = (void *)kernel_if->dmas[page_index + 1].dma_vaddr;

		ASSERT(va);
		/*
		 * Fill up the page if we have enough payload, or else
		 * copy the remaining bytes.
		 */
		to_copy = MIN(PAGE_SIZE - page_offset, size - bytes_copied);

		if (is_iovec) {
			struct iovec *iov = (struct iovec *)src;

			/* The iovec will track bytes_copied internally. */
			vmci_memcpy_fromiovec((uint8_t *)va + page_offset,
			    iov, to_copy);
		} else
			memcpy((uint8_t *)va + page_offset,
			    (uint8_t *)src + bytes_copied, to_copy);
		bytes_copied += to_copy;
	}

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * __vmci_memcpy_from_queue --
 *
 *     Copies to a given buffer or iovector from a VMCI Queue. Assumes that
 *     offset + size does not wrap around in the queue.
 *
 * Results:
 *     Zero on success, negative error code on failure.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
__vmci_memcpy_from_queue(void *dest, const struct vmci_queue *queue,
    uint64_t queue_offset, size_t size, bool is_iovec)
{
	struct vmci_queue_kernel_if *kernel_if = queue->kernel_if;
	size_t bytes_copied = 0;

	while (bytes_copied < size) {
		const uint64_t page_index =
		    (queue_offset + bytes_copied) / PAGE_SIZE;
		const size_t page_offset =
		    (queue_offset + bytes_copied) & (PAGE_SIZE - 1);
		void *va;
		size_t to_copy;

		/* Skip header. */
		va = (void *)kernel_if->dmas[page_index + 1].dma_vaddr;

		ASSERT(va);
		/*
		 * Fill up the page if we have enough payload, or else
		 * copy the remaining bytes.
		 */
		to_copy = MIN(PAGE_SIZE - page_offset, size - bytes_copied);

		if (is_iovec) {
			struct iovec *iov = (struct iovec *)dest;

			/* The iovec will track bytesCopied internally. */
			vmci_memcpy_toiovec(iov, (uint8_t *)va +
			    page_offset, to_copy);
		} else
			memcpy((uint8_t *)dest + bytes_copied,
			    (uint8_t *)va + page_offset, to_copy);

		bytes_copied += to_copy;
	}

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_memcpy_to_queue --
 *
 *     Copies from a given buffer to a VMCI Queue.
 *
 * Results:
 *     Zero on success, negative error code on failure.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_memcpy_to_queue(struct vmci_queue *queue, uint64_t queue_offset,
    const void *src, size_t src_offset, size_t size, int buf_type,
    bool can_block)
{

	ASSERT(can_block);

	return (__vmci_memcpy_to_queue(queue, queue_offset,
	    (uint8_t *)src + src_offset, size, false));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_memcpy_from_queue --
 *
 *      Copies to a given buffer from a VMCI Queue.
 *
 * Results:
 *      Zero on success, negative error code on failure.
 *
 * Side effects:
 *      None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_memcpy_from_queue(void *dest, size_t dest_offset,
    const struct vmci_queue *queue, uint64_t queue_offset, size_t size,
    int buf_type, bool can_block)
{

	ASSERT(can_block);

	return (__vmci_memcpy_from_queue((uint8_t *)dest + dest_offset,
	    queue, queue_offset, size, false));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_memcpy_to_queue_local --
 *
 *     Copies from a given buffer to a local VMCI queue. This is the
 *     same as a regular copy.
 *
 * Results:
 *     Zero on success, negative error code on failure.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_memcpy_to_queue_local(struct vmci_queue *queue, uint64_t queue_offset,
    const void *src, size_t src_offset, size_t size, int buf_type,
    bool can_block)
{

	ASSERT(can_block);

	return (__vmci_memcpy_to_queue(queue, queue_offset,
	    (uint8_t *)src + src_offset, size, false));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_memcpy_from_queue_local --
 *
 *     Copies to a given buffer from a VMCI Queue.
 *
 * Results:
 *     Zero on success, negative error code on failure.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_memcpy_from_queue_local(void *dest, size_t dest_offset,
    const struct vmci_queue *queue, uint64_t queue_offset, size_t size,
    int buf_type, bool can_block)
{

	ASSERT(can_block);

	return (__vmci_memcpy_from_queue((uint8_t *)dest + dest_offset,
	    queue, queue_offset, size, false));
}

/*------------------------------------------------------------------------------
 *
 * vmci_memcpy_to_queue_v --
 *
 *     Copies from a given iovec from a VMCI Queue.
 *
 * Results:
 *     Zero on success, negative error code on failure.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_memcpy_to_queue_v(struct vmci_queue *queue, uint64_t queue_offset,
    const void *src, size_t src_offset, size_t size, int buf_type,
    bool can_block)
{

	ASSERT(can_block);

	/*
	 * We ignore src_offset because src is really a struct iovec * and will
	 * maintain offset internally.
	 */
	return (__vmci_memcpy_to_queue(queue, queue_offset, src, size,
	    true));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_memcpy_from_queue_v --
 *
 *     Copies to a given iovec from a VMCI Queue.
 *
 * Results:
 *     Zero on success, negative error code on failure.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_memcpy_from_queue_v(void *dest, size_t dest_offset,
    const struct vmci_queue *queue, uint64_t queue_offset, size_t size,
    int buf_type, bool can_block)
{

	ASSERT(can_block);

	/*
	 * We ignore dest_offset because dest is really a struct iovec * and
	 * will maintain offset internally.
	 */
	return (__vmci_memcpy_from_queue(dest, queue, queue_offset, size,
	    true));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_read_port_bytes --
 *
 *     Copy memory from an I/O port to kernel memory.
 *
 * Results:
 *     No results.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_read_port_bytes(vmci_io_handle handle, vmci_io_port port, uint8_t *buffer,
    size_t buffer_length)
{

	insb(port, buffer, buffer_length);
}
