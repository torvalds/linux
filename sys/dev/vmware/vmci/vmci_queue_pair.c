/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 */

/* VMCI QueuePair API implementation. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "vmci.h"
#include "vmci_driver.h"
#include "vmci_event.h"
#include "vmci_kernel_api.h"
#include "vmci_kernel_defs.h"
#include "vmci_queue_pair.h"

#define LGPFX	"vmci_queue_pair: "

struct queue_pair_entry {
	vmci_list_item(queue_pair_entry) list_item;
	struct vmci_handle handle;
	vmci_id		peer;
	uint32_t	flags;
	uint64_t	produce_size;
	uint64_t	consume_size;
	uint32_t	ref_count;
};

struct qp_guest_endpoint {
	struct queue_pair_entry qp;
	uint64_t	num_ppns;
	void		*produce_q;
	void		*consume_q;
	bool		hibernate_failure;
	struct ppn_set	ppn_set;
};

struct queue_pair_list {
	vmci_list(queue_pair_entry) head;
	volatile int	hibernate;
	vmci_mutex	mutex;
};

#define QPE_NUM_PAGES(_QPE)						\
	((uint32_t)(CEILING(_QPE.produce_size, PAGE_SIZE) +		\
	CEILING(_QPE.consume_size, PAGE_SIZE) + 2))

static struct queue_pair_list qp_guest_endpoints;

static struct	queue_pair_entry *queue_pair_list_find_entry(
		    struct queue_pair_list *qp_list, struct vmci_handle handle);
static void	queue_pair_list_add_entry(struct queue_pair_list *qp_list,
		    struct queue_pair_entry *entry);
static void	queue_pair_list_remove_entry(struct queue_pair_list *qp_list,
		    struct queue_pair_entry *entry);
static struct	queue_pair_entry *queue_pair_list_get_head(
		    struct queue_pair_list *qp_list);
static int	queue_pair_notify_peer_local(bool attach,
		    struct vmci_handle handle);
static struct	qp_guest_endpoint *qp_guest_endpoint_create(
		    struct vmci_handle handle, vmci_id peer, uint32_t flags,
		    uint64_t produce_size, uint64_t consume_size,
		    void *produce_q, void *consume_q);
static void	qp_guest_endpoint_destroy(struct qp_guest_endpoint *entry);
static int	vmci_queue_pair_alloc_hypercall(
		    const struct qp_guest_endpoint *entry);
static int	vmci_queue_pair_alloc_guest_work(struct vmci_handle *handle,
		    struct vmci_queue **produce_q, uint64_t produce_size,
		    struct vmci_queue **consume_q, uint64_t consume_size,
		    vmci_id peer, uint32_t flags,
		    vmci_privilege_flags priv_flags);
static int	vmci_queue_pair_detach_guest_work(struct vmci_handle handle);
static int	vmci_queue_pair_detach_hypercall(struct vmci_handle handle);

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_pair_alloc --
 *
 *     Allocates a VMCI QueuePair. Only checks validity of input arguments. The
 *     real work is done in the host or guest specific function.
 *
 * Results:
 *     VMCI_SUCCESS on success, appropriate error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_queue_pair_alloc(struct vmci_handle *handle, struct vmci_queue **produce_q,
    uint64_t produce_size, struct vmci_queue **consume_q, uint64_t consume_size,
    vmci_id peer, uint32_t flags, vmci_privilege_flags priv_flags)
{

	if (!handle || !produce_q || !consume_q ||
	    (!produce_size && !consume_size) || (flags & ~VMCI_QP_ALL_FLAGS))
		return (VMCI_ERROR_INVALID_ARGS);

	return (vmci_queue_pair_alloc_guest_work(handle, produce_q,
	    produce_size, consume_q, consume_size, peer, flags, priv_flags));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_pair_detach --
 *
 *     Detaches from a VMCI QueuePair. Only checks validity of input argument.
 *     Real work is done in the host or guest specific function.
 *
 * Results:
 *     Success or failure.
 *
 * Side effects:
 *     Memory is freed.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_queue_pair_detach(struct vmci_handle handle)
{

	if (VMCI_HANDLE_INVALID(handle))
		return (VMCI_ERROR_INVALID_ARGS);

	return (vmci_queue_pair_detach_guest_work(handle));
}

/*
 *------------------------------------------------------------------------------
 *
 * queue_pair_list_init --
 *
 *     Initializes the list of QueuePairs.
 *
 * Results:
 *     Success or failure.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static inline int
queue_pair_list_init(struct queue_pair_list *qp_list)
{
	int ret;

	vmci_list_init(&qp_list->head);
	atomic_store_int(&qp_list->hibernate, 0);
	ret = vmci_mutex_init(&qp_list->mutex, "VMCI QP List lock");
	return (ret);
}

/*
 *------------------------------------------------------------------------------
 *
 * queue_pair_list_destroy --
 *
 *     Destroy the list's mutex.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static inline void
queue_pair_list_destroy(struct queue_pair_list *qp_list)
{

	vmci_mutex_destroy(&qp_list->mutex);
	vmci_list_init(&qp_list->head);
}

/*
 *------------------------------------------------------------------------------
 *
 * queue_pair_list_find_entry --
 *
 *     Finds the entry in the list corresponding to a given handle. Assumes that
 *     the list is locked.
 *
 * Results:
 *     Pointer to entry.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static struct queue_pair_entry *
queue_pair_list_find_entry(struct queue_pair_list *qp_list,
    struct vmci_handle handle)
{
	struct queue_pair_entry *next;

	if (VMCI_HANDLE_INVALID(handle))
		return (NULL);

	vmci_list_scan(next, &qp_list->head, list_item) {
		if (VMCI_HANDLE_EQUAL(next->handle, handle))
			return (next);
	}

	return (NULL);
}

/*
 *------------------------------------------------------------------------------
 *
 * queue_pair_list_add_entry --
 *
 *     Adds the given entry to the list. Assumes that the list is locked.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
queue_pair_list_add_entry(struct queue_pair_list *qp_list,
    struct queue_pair_entry *entry)
{

	if (entry)
		vmci_list_insert(&qp_list->head, entry, list_item);
}

/*
 *------------------------------------------------------------------------------
 *
 * queue_pair_list_remove_entry --
 *
 *     Removes the given entry from the list. Assumes that the list is locked.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
queue_pair_list_remove_entry(struct queue_pair_list *qp_list,
    struct queue_pair_entry *entry)
{

	if (entry)
		vmci_list_remove(entry, list_item);
}

/*
 *------------------------------------------------------------------------------
 *
 * queue_pair_list_get_head --
 *
 *     Returns the entry from the head of the list. Assumes that the list is
 *     locked.
 *
 * Results:
 *     Pointer to entry.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static struct queue_pair_entry *
queue_pair_list_get_head(struct queue_pair_list *qp_list)
{

	return (vmci_list_first(&qp_list->head));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qp_guest_endpoints_init --
 *
 *     Initalizes data structure state keeping track of queue pair guest
 *     endpoints.
 *
 * Results:
 *     VMCI_SUCCESS on success and appropriate failure code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_qp_guest_endpoints_init(void)
{

	return (queue_pair_list_init(&qp_guest_endpoints));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qp_guest_endpoints_exit --
 *
 *     Destroys all guest queue pair endpoints. If active guest queue pairs
 *     still exist, hypercalls to attempt detach from these queue pairs will be
 *     made. Any failure to detach is silently ignored.
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
vmci_qp_guest_endpoints_exit(void)
{
	struct qp_guest_endpoint *entry;

	vmci_mutex_acquire(&qp_guest_endpoints.mutex);

	while ((entry =
	    (struct qp_guest_endpoint *)queue_pair_list_get_head(
	    &qp_guest_endpoints)) != NULL) {
		/*
		 * Don't make a hypercall for local QueuePairs.
		 */
		if (!(entry->qp.flags & VMCI_QPFLAG_LOCAL))
			vmci_queue_pair_detach_hypercall(entry->qp.handle);
		/*
		 * We cannot fail the exit, so let's reset ref_count.
		 */
		entry->qp.ref_count = 0;
		queue_pair_list_remove_entry(&qp_guest_endpoints, &entry->qp);
		qp_guest_endpoint_destroy(entry);
	}

	atomic_store_int(&qp_guest_endpoints.hibernate, 0);
	vmci_mutex_release(&qp_guest_endpoints.mutex);
	queue_pair_list_destroy(&qp_guest_endpoints);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_qp_guest_endpoints_sync --
 *
 *     Use this as a synchronization point when setting globals, for example,
 *     during device shutdown.
 *
 * Results:
 *     true.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_qp_guest_endpoints_sync(void)
{

	vmci_mutex_acquire(&qp_guest_endpoints.mutex);
	vmci_mutex_release(&qp_guest_endpoints.mutex);
}

/*
 *------------------------------------------------------------------------------
 *
 * qp_guest_endpoint_create --
 *
 *     Allocates and initializes a qp_guest_endpoint structure. Allocates a
 *     QueuePair rid (and handle) iff the given entry has an invalid handle.
 *     0 through VMCI_RESERVED_RESOURCE_ID_MAX are reserved handles. Assumes
 *     that the QP list mutex is held by the caller.
 *
 * Results:
 *     Pointer to structure intialized.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

struct qp_guest_endpoint *
qp_guest_endpoint_create(struct vmci_handle handle, vmci_id peer,
    uint32_t flags, uint64_t produce_size, uint64_t consume_size,
    void *produce_q, void *consume_q)
{
	struct qp_guest_endpoint *entry;
	static vmci_id queue_pair_rid;
	const uint64_t num_ppns = CEILING(produce_size, PAGE_SIZE) +
	    CEILING(consume_size, PAGE_SIZE) +
	    2; /* One page each for the queue headers. */

	queue_pair_rid = VMCI_RESERVED_RESOURCE_ID_MAX + 1;

	ASSERT((produce_size || consume_size) && produce_q && consume_q);

	if (VMCI_HANDLE_INVALID(handle)) {
		vmci_id context_id = vmci_get_context_id();
		vmci_id old_rid = queue_pair_rid;

		/*
		 * Generate a unique QueuePair rid.  Keep on trying until we
		 * wrap around in the RID space.
		 */
		ASSERT(old_rid > VMCI_RESERVED_RESOURCE_ID_MAX);
		do {
			handle = VMCI_MAKE_HANDLE(context_id, queue_pair_rid);
			entry =
			    (struct qp_guest_endpoint *)
			    queue_pair_list_find_entry(&qp_guest_endpoints,
			    handle);
			queue_pair_rid++;
			if (UNLIKELY(!queue_pair_rid)) {
				/*
				 * Skip the reserved rids.
				 */
				queue_pair_rid =
				    VMCI_RESERVED_RESOURCE_ID_MAX + 1;
			}
		} while (entry && queue_pair_rid != old_rid);

		if (UNLIKELY(entry != NULL)) {
			ASSERT(queue_pair_rid == old_rid);
			/*
			 * We wrapped around --- no rids were free.
			 */
			return (NULL);
		}
	}

	ASSERT(!VMCI_HANDLE_INVALID(handle) &&
	    queue_pair_list_find_entry(&qp_guest_endpoints, handle) == NULL);
	entry = vmci_alloc_kernel_mem(sizeof(*entry), VMCI_MEMORY_NORMAL);
	if (entry) {
		entry->qp.handle = handle;
		entry->qp.peer = peer;
		entry->qp.flags = flags;
		entry->qp.produce_size = produce_size;
		entry->qp.consume_size = consume_size;
		entry->qp.ref_count = 0;
		entry->num_ppns = num_ppns;
		memset(&entry->ppn_set, 0, sizeof(entry->ppn_set));
		entry->produce_q = produce_q;
		entry->consume_q = consume_q;
	}
	return (entry);
}

/*
 *------------------------------------------------------------------------------
 *
 * qp_guest_endpoint_destroy --
 *
 *     Frees a qp_guest_endpoint structure.
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
qp_guest_endpoint_destroy(struct qp_guest_endpoint *entry)
{

	ASSERT(entry);
	ASSERT(entry->qp.ref_count == 0);

	vmci_free_ppn_set(&entry->ppn_set);
	vmci_free_queue(entry->produce_q, entry->qp.produce_size);
	vmci_free_queue(entry->consume_q, entry->qp.consume_size);
	vmci_free_kernel_mem(entry, sizeof(*entry));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_pair_alloc_hypercall --
 *
 *     Helper to make a QueuePairAlloc hypercall when the driver is
 *     supporting a guest device.
 *
 * Results:
 *     Result of the hypercall.
 *
 * Side effects:
 *     Memory is allocated & freed.
 *
 *------------------------------------------------------------------------------
 */
static int
vmci_queue_pair_alloc_hypercall(const struct qp_guest_endpoint *entry)
{
	struct vmci_queue_pair_alloc_msg *alloc_msg;
	size_t msg_size;
	int result;

	if (!entry || entry->num_ppns <= 2)
		return (VMCI_ERROR_INVALID_ARGS);

	ASSERT(!(entry->qp.flags & VMCI_QPFLAG_LOCAL));

	msg_size = sizeof(*alloc_msg) + (size_t)entry->num_ppns * sizeof(PPN);
	alloc_msg = vmci_alloc_kernel_mem(msg_size, VMCI_MEMORY_NORMAL);
	if (!alloc_msg)
		return (VMCI_ERROR_NO_MEM);

	alloc_msg->hdr.dst = VMCI_MAKE_HANDLE(VMCI_HYPERVISOR_CONTEXT_ID,
	    VMCI_QUEUEPAIR_ALLOC);
	alloc_msg->hdr.src = VMCI_ANON_SRC_HANDLE;
	alloc_msg->hdr.payload_size = msg_size - VMCI_DG_HEADERSIZE;
	alloc_msg->handle = entry->qp.handle;
	alloc_msg->peer = entry->qp.peer;
	alloc_msg->flags = entry->qp.flags;
	alloc_msg->produce_size = entry->qp.produce_size;
	alloc_msg->consume_size = entry->qp.consume_size;
	alloc_msg->num_ppns = entry->num_ppns;
	result = vmci_populate_ppn_list((uint8_t *)alloc_msg +
	    sizeof(*alloc_msg), &entry->ppn_set);
	if (result == VMCI_SUCCESS)
		result = vmci_send_datagram((struct vmci_datagram *)alloc_msg);
	vmci_free_kernel_mem(alloc_msg, msg_size);

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_pair_alloc_guest_work --
 *
 *     This functions handles the actual allocation of a VMCI queue pair guest
 *     endpoint. Allocates physical pages for the queue pair. It makes OS
 *     dependent calls through generic wrappers.
 *
 * Results:
 *     Success or failure.
 *
 * Side effects:
 *     Memory is allocated.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_queue_pair_alloc_guest_work(struct vmci_handle *handle,
    struct vmci_queue **produce_q, uint64_t produce_size,
    struct vmci_queue **consume_q, uint64_t consume_size, vmci_id peer,
    uint32_t flags, vmci_privilege_flags priv_flags)
{
	struct qp_guest_endpoint *queue_pair_entry = NULL;
	void *my_consume_q = NULL;
	void *my_produce_q = NULL;
	const uint64_t num_consume_pages = CEILING(consume_size, PAGE_SIZE) + 1;
	const uint64_t num_produce_pages = CEILING(produce_size, PAGE_SIZE) + 1;
	int result;

	ASSERT(handle && produce_q && consume_q &&
	    (produce_size || consume_size));

	if (priv_flags != VMCI_NO_PRIVILEGE_FLAGS)
		return (VMCI_ERROR_NO_ACCESS);

	vmci_mutex_acquire(&qp_guest_endpoints.mutex);

	if ((atomic_load_int(&qp_guest_endpoints.hibernate) == 1) &&
		 !(flags & VMCI_QPFLAG_LOCAL)) {
		/*
		 * While guest OS is in hibernate state, creating non-local
		 * queue pairs is not allowed after the point where the VMCI
		 * guest driver converted the existing queue pairs to local
		 * ones.
		 */

		result = VMCI_ERROR_UNAVAILABLE;
		goto error;
	}

	if ((queue_pair_entry =
	    (struct qp_guest_endpoint *)queue_pair_list_find_entry(
	    &qp_guest_endpoints, *handle)) != NULL) {
		if (queue_pair_entry->qp.flags & VMCI_QPFLAG_LOCAL) {
			/* Local attach case. */
			if (queue_pair_entry->qp.ref_count > 1) {
				VMCI_LOG_DEBUG(LGPFX"Error attempting to "
				    "attach more than once.\n");
				result = VMCI_ERROR_UNAVAILABLE;
				goto error_keep_entry;
			}

			if (queue_pair_entry->qp.produce_size != consume_size ||
			    queue_pair_entry->qp.consume_size != produce_size ||
			    queue_pair_entry->qp.flags !=
			    (flags & ~VMCI_QPFLAG_ATTACH_ONLY)) {
				VMCI_LOG_DEBUG(LGPFX"Error mismatched "
				    "queue pair in local attach.\n");
				result = VMCI_ERROR_QUEUEPAIR_MISMATCH;
				goto error_keep_entry;
			}

			/*
			 * Do a local attach. We swap the consume and produce
			 * queues for the attacher and deliver an attach event.
			 */
			result = queue_pair_notify_peer_local(true, *handle);
			if (result < VMCI_SUCCESS)
				goto error_keep_entry;
			my_produce_q = queue_pair_entry->consume_q;
			my_consume_q = queue_pair_entry->produce_q;
			goto out;
		}
		result = VMCI_ERROR_ALREADY_EXISTS;
		goto error_keep_entry;
	}

	my_produce_q = vmci_alloc_queue(produce_size, flags);
	if (!my_produce_q) {
		VMCI_LOG_WARNING(LGPFX"Error allocating pages for produce "
		    "queue.\n");
		result = VMCI_ERROR_NO_MEM;
		goto error;
	}

	my_consume_q = vmci_alloc_queue(consume_size, flags);
	if (!my_consume_q) {
		VMCI_LOG_WARNING(LGPFX"Error allocating pages for consume "
		    "queue.\n");
		result = VMCI_ERROR_NO_MEM;
		goto error;
	}

	queue_pair_entry = qp_guest_endpoint_create(*handle, peer, flags,
	    produce_size, consume_size, my_produce_q, my_consume_q);
	if (!queue_pair_entry) {
		VMCI_LOG_WARNING(LGPFX"Error allocating memory in %s.\n",
		    __FUNCTION__);
		result = VMCI_ERROR_NO_MEM;
		goto error;
	}

	result = vmci_alloc_ppn_set(my_produce_q, num_produce_pages,
	    my_consume_q, num_consume_pages, &queue_pair_entry->ppn_set);
	if (result < VMCI_SUCCESS) {
		VMCI_LOG_WARNING(LGPFX"vmci_alloc_ppn_set failed.\n");
		goto error;
	}

	/*
	 * It's only necessary to notify the host if this queue pair will be
	 * attached to from another context.
	 */
	if (queue_pair_entry->qp.flags & VMCI_QPFLAG_LOCAL) {
		/* Local create case. */
		vmci_id context_id = vmci_get_context_id();

		/*
		 * Enforce similar checks on local queue pairs as we do for
		 * regular ones. The handle's context must match the creator
		 * or attacher context id (here they are both the current
		 * context id) and the attach-only flag cannot exist during
		 * create. We also ensure specified peer is this context or
		 * an invalid one.
		 */
		if (queue_pair_entry->qp.handle.context != context_id ||
		    (queue_pair_entry->qp.peer != VMCI_INVALID_ID &&
		    queue_pair_entry->qp.peer != context_id)) {
			result = VMCI_ERROR_NO_ACCESS;
			goto error;
		}

		if (queue_pair_entry->qp.flags & VMCI_QPFLAG_ATTACH_ONLY) {
			result = VMCI_ERROR_NOT_FOUND;
			goto error;
		}
	} else {
		result = vmci_queue_pair_alloc_hypercall(queue_pair_entry);
		if (result < VMCI_SUCCESS) {
			VMCI_LOG_WARNING(
			    LGPFX"vmci_queue_pair_alloc_hypercall result = "
			    "%d.\n", result);
			goto error;
		}
	}

	queue_pair_list_add_entry(&qp_guest_endpoints, &queue_pair_entry->qp);

out:
	queue_pair_entry->qp.ref_count++;
	*handle = queue_pair_entry->qp.handle;
	*produce_q = (struct vmci_queue *)my_produce_q;
	*consume_q = (struct vmci_queue *)my_consume_q;

	/*
	 * We should initialize the queue pair header pages on a local queue
	 * pair create. For non-local queue pairs, the hypervisor initializes
	 * the header pages in the create step.
	 */
	if ((queue_pair_entry->qp.flags & VMCI_QPFLAG_LOCAL) &&
	    queue_pair_entry->qp.ref_count == 1) {
		vmci_queue_header_init((*produce_q)->q_header, *handle);
		vmci_queue_header_init((*consume_q)->q_header, *handle);
	}

	vmci_mutex_release(&qp_guest_endpoints.mutex);

	return (VMCI_SUCCESS);

error:
	vmci_mutex_release(&qp_guest_endpoints.mutex);
	if (queue_pair_entry) {
		/* The queues will be freed inside the destroy routine. */
		qp_guest_endpoint_destroy(queue_pair_entry);
	} else {
		if (my_produce_q)
			vmci_free_queue(my_produce_q, produce_size);
		if (my_consume_q)
			vmci_free_queue(my_consume_q, consume_size);
	}
	return (result);

error_keep_entry:
	/* This path should only be used when an existing entry was found. */
	ASSERT(queue_pair_entry->qp.ref_count > 0);
	vmci_mutex_release(&qp_guest_endpoints.mutex);
	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_pair_detach_hypercall --
 *
 *     Helper to make a QueuePairDetach hypercall when the driver is supporting
 *     a guest device.
 *
 * Results:
 *     Result of the hypercall.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_queue_pair_detach_hypercall(struct vmci_handle handle)
{
	struct vmci_queue_pair_detach_msg detach_msg;

	detach_msg.hdr.dst = VMCI_MAKE_HANDLE(VMCI_HYPERVISOR_CONTEXT_ID,
	    VMCI_QUEUEPAIR_DETACH);
	detach_msg.hdr.src = VMCI_ANON_SRC_HANDLE;
	detach_msg.hdr.payload_size = sizeof(handle);
	detach_msg.handle = handle;

	return (vmci_send_datagram((struct vmci_datagram *)&detach_msg));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_pair_detach_guest_work --
 *
 *     Helper for VMCI QueuePair detach interface. Frees the physical pages for
 *     the queue pair.
 *
 * Results:
 *     Success or failure.
 *
 * Side effects:
 *     Memory may be freed.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_queue_pair_detach_guest_work(struct vmci_handle handle)
{
	struct qp_guest_endpoint *entry;
	int result;
	uint32_t ref_count;

	ASSERT(!VMCI_HANDLE_INVALID(handle));

	vmci_mutex_acquire(&qp_guest_endpoints.mutex);

	entry = (struct qp_guest_endpoint *)queue_pair_list_find_entry(
	    &qp_guest_endpoints, handle);
	if (!entry) {
		vmci_mutex_release(&qp_guest_endpoints.mutex);
		return (VMCI_ERROR_NOT_FOUND);
	}

	ASSERT(entry->qp.ref_count >= 1);

	if (entry->qp.flags & VMCI_QPFLAG_LOCAL) {
		result = VMCI_SUCCESS;

		if (entry->qp.ref_count > 1) {
			result = queue_pair_notify_peer_local(false, handle);

			/*
			 * We can fail to notify a local queuepair because we
			 * can't allocate. We still want to release the entry
			 * if that happens, so don't bail out yet.
			 */
		}
	} else {
		result = vmci_queue_pair_detach_hypercall(handle);
		if (entry->hibernate_failure) {
			if (result == VMCI_ERROR_NOT_FOUND) {

				/*
				 * If a queue pair detach failed when entering
				 * hibernation, the guest driver and the device
				 * may disagree on its existence when coming
				 * out of hibernation. The guest driver will
				 * regard it as a non-local queue pair, but
				 * the device state is gone, since the device
				 * has been powered off. In this case, we
				 * treat the queue pair as a local queue pair
				 * with no peer.
				 */

				ASSERT(entry->qp.ref_count == 1);
				result = VMCI_SUCCESS;
			}
		}
		if (result < VMCI_SUCCESS) {

			/*
			 * We failed to notify a non-local queuepair. That other
			 * queuepair might still be accessing the shared
			 * memory, so don't release the entry yet. It will get
			 * cleaned up by vmci_queue_pair_Exit() if necessary
			 * (assuming we are going away, otherwise why did this
			 * fail?).
			 */

			vmci_mutex_release(&qp_guest_endpoints.mutex);
			return (result);
		}
	}

	/*
	 * If we get here then we either failed to notify a local queuepair, or
	 * we succeeded in all cases.  Release the entry if required.
	 */

	entry->qp.ref_count--;
	if (entry->qp.ref_count == 0)
		queue_pair_list_remove_entry(&qp_guest_endpoints, &entry->qp);

	/* If we didn't remove the entry, this could change once we unlock. */
	ref_count = entry ? entry->qp.ref_count :
	    0xffffffff; /*
			 * Value does not matter, silence the
			 * compiler.
			 */

	vmci_mutex_release(&qp_guest_endpoints.mutex);

	if (ref_count == 0)
		qp_guest_endpoint_destroy(entry);
	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * queue_pair_notify_peer_local --
 *
 *     Dispatches a queue pair event message directly into the local event
 *     queue.
 *
 * Results:
 *     VMCI_SUCCESS on success, error code otherwise
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
queue_pair_notify_peer_local(bool attach, struct vmci_handle handle)
{
	struct vmci_event_msg *e_msg;
	struct vmci_event_payload_qp *e_payload;
	/* buf is only 48 bytes. */
	vmci_id context_id;
	context_id = vmci_get_context_id();
	char buf[sizeof(*e_msg) + sizeof(*e_payload)];

	e_msg = (struct vmci_event_msg *)buf;
	e_payload = vmci_event_msg_payload(e_msg);

	e_msg->hdr.dst = VMCI_MAKE_HANDLE(context_id, VMCI_EVENT_HANDLER);
	e_msg->hdr.src = VMCI_MAKE_HANDLE(VMCI_HYPERVISOR_CONTEXT_ID,
	    VMCI_CONTEXT_RESOURCE_ID);
	e_msg->hdr.payload_size = sizeof(*e_msg) + sizeof(*e_payload) -
	    sizeof(e_msg->hdr);
	e_msg->event_data.event = attach ? VMCI_EVENT_QP_PEER_ATTACH :
	    VMCI_EVENT_QP_PEER_DETACH;
	e_payload->peer_id = context_id;
	e_payload->handle = handle;

	return (vmci_event_dispatch((struct vmci_datagram *)e_msg));
}
