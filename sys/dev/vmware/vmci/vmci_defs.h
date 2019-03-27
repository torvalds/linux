/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

#ifndef _VMCI_DEFS_H_
#define _VMCI_DEFS_H_

#include <sys/types.h>
#include <machine/atomic.h>

#include "vmci_kernel_defs.h"

#pragma GCC diagnostic ignored "-Wcast-qual"

/* Register offsets. */
#define VMCI_STATUS_ADDR		0x00
#define VMCI_CONTROL_ADDR		0x04
#define VMCI_ICR_ADDR			0x08
#define VMCI_IMR_ADDR			0x0c
#define VMCI_DATA_OUT_ADDR		0x10
#define VMCI_DATA_IN_ADDR		0x14
#define VMCI_CAPS_ADDR			0x18
#define VMCI_RESULT_LOW_ADDR		0x1c
#define VMCI_RESULT_HIGH_ADDR		0x20

/* Status register bits. */
#define VMCI_STATUS_INT_ON		0x1

/* Control register bits. */
#define VMCI_CONTROL_RESET		0x1
#define VMCI_CONTROL_INT_ENABLE		0x2
#define VMCI_CONTROL_INT_DISABLE	0x4

/* Capabilities register bits. */
#define VMCI_CAPS_HYPERCALL		0x1
#define VMCI_CAPS_GUESTCALL		0x2
#define VMCI_CAPS_DATAGRAM		0x4
#define VMCI_CAPS_NOTIFICATIONS		0x8

/* Interrupt Cause register bits. */
#define VMCI_ICR_DATAGRAM		0x1
#define VMCI_ICR_NOTIFICATION		0x2

/* Interrupt Mask register bits. */
#define VMCI_IMR_DATAGRAM		0x1
#define VMCI_IMR_NOTIFICATION		0x2

/* Interrupt type. */
typedef enum vmci_intr_type {
	VMCI_INTR_TYPE_INTX =	0,
	VMCI_INTR_TYPE_MSI =	1,
	VMCI_INTR_TYPE_MSIX =	2
} vmci_intr_type;

/*
 * Maximum MSI/MSI-X interrupt vectors in the device.
 */
#define VMCI_MAX_INTRS			2

/*
 * Supported interrupt vectors. There is one for each ICR value above,
 * but here they indicate the position in the vector array/message ID.
 */
#define VMCI_INTR_DATAGRAM		0
#define VMCI_INTR_NOTIFICATION		1

/*
 * A single VMCI device has an upper limit of 128 MiB on the amount of
 * memory that can be used for queue pairs.
 */
#define VMCI_MAX_GUEST_QP_MEMORY	(128 * 1024 * 1024)

/*
 * We have a fixed set of resource IDs available in the VMX.
 * This allows us to have a very simple implementation since we statically
 * know how many will create datagram handles. If a new caller arrives and
 * we have run out of slots we can manually increment the maximum size of
 * available resource IDs.
 */

typedef uint32_t vmci_resource;

/* VMCI reserved hypervisor datagram resource IDs. */
#define VMCI_RESOURCES_QUERY		0
#define VMCI_GET_CONTEXT_ID		1
#define VMCI_SET_NOTIFY_BITMAP		2
#define VMCI_DOORBELL_LINK		3
#define VMCI_DOORBELL_UNLINK		4
#define VMCI_DOORBELL_NOTIFY		5
/*
 * VMCI_DATAGRAM_REQUEST_MAP and VMCI_DATAGRAM_REMOVE_MAP are
 * obsoleted by the removal of VM to VM communication.
 */
#define VMCI_DATAGRAM_REQUEST_MAP	6
#define VMCI_DATAGRAM_REMOVE_MAP	7
#define VMCI_EVENT_SUBSCRIBE		8
#define VMCI_EVENT_UNSUBSCRIBE		9
#define VMCI_QUEUEPAIR_ALLOC		10
#define VMCI_QUEUEPAIR_DETACH		11
/*
 * VMCI_VSOCK_VMX_LOOKUP was assigned to 12 for Fusion 3.0/3.1,
 * WS 7.0/7.1 and ESX 4.1
 */
#define VMCI_HGFS_TRANSPORT		13
#define VMCI_UNITY_PBRPC_REGISTER	14
/*
 * This resource is used for VMCI socket control packets sent to the
 * hypervisor (CID 0) because RID 1 is already reserved.
 */
#define VSOCK_PACKET_HYPERVISOR_RID	15
#define VMCI_RESOURCE_MAX		16
/*
 * The core VMCI device functionality only requires the resource IDs of
 * VMCI_QUEUEPAIR_DETACH and below.
 */
#define VMCI_CORE_DEVICE_RESOURCE_MAX	VMCI_QUEUEPAIR_DETACH

/*
 * VMCI reserved host datagram resource IDs.
 * vsock control channel has resource id 1.
 */
#define VMCI_DVFILTER_DATA_PATH_DATAGRAM	2

/* VMCI Ids. */
typedef uint32_t vmci_id;

struct vmci_id_range {
	int8_t	action;	/* VMCI_FA_X, for use in filters. */
	vmci_id	begin;	/* Beginning of range. */
	vmci_id	end;	/* End of range. */
};

struct vmci_handle {
	vmci_id	context;
	vmci_id	resource;
};

static inline struct vmci_handle
VMCI_MAKE_HANDLE(vmci_id cid, vmci_id rid)
{
	struct vmci_handle h;

	h.context = cid;
	h.resource = rid;
	return (h);
}

#define VMCI_HANDLE_TO_CONTEXT_ID(_handle)				\
	((_handle).context)
#define VMCI_HANDLE_TO_RESOURCE_ID(_handle)				\
	((_handle).resource)
#define VMCI_HANDLE_EQUAL(_h1, _h2)					\
	((_h1).context == (_h2).context && (_h1).resource == (_h2).resource)

#define VMCI_INVALID_ID			0xFFFFFFFF
static const struct vmci_handle VMCI_INVALID_HANDLE = {VMCI_INVALID_ID,
	    VMCI_INVALID_ID};

#define VMCI_HANDLE_INVALID(_handle)					\
	VMCI_HANDLE_EQUAL((_handle), VMCI_INVALID_HANDLE)

/*
 * The below defines can be used to send anonymous requests.
 * This also indicates that no response is expected.
 */
#define VMCI_ANON_SRC_CONTEXT_ID					\
	VMCI_INVALID_ID
#define VMCI_ANON_SRC_RESOURCE_ID					\
	VMCI_INVALID_ID
#define VMCI_ANON_SRC_HANDLE						\
	VMCI_MAKE_HANDLE(VMCI_ANON_SRC_CONTEXT_ID,			\
	VMCI_ANON_SRC_RESOURCE_ID)

/* The lowest 16 context ids are reserved for internal use. */
#define VMCI_RESERVED_CID_LIMIT		16

/*
 * Hypervisor context id, used for calling into hypervisor
 * supplied services from the VM.
 */
#define VMCI_HYPERVISOR_CONTEXT_ID	0

/*
 * Well-known context id, a logical context that contains a set of
 * well-known services. This context ID is now obsolete.
 */
#define VMCI_WELL_KNOWN_CONTEXT_ID	1

/*
 * Context ID used by host endpoints.
 */
#define VMCI_HOST_CONTEXT_ID		2
#define VMCI_HOST_CONTEXT_INVALID_EVENT	((uintptr_t)~0)

#define VMCI_CONTEXT_IS_VM(_cid)					\
	(VMCI_INVALID_ID != _cid && _cid > VMCI_HOST_CONTEXT_ID)

/*
 * The VMCI_CONTEXT_RESOURCE_ID is used together with VMCI_MAKE_HANDLE to make
 * handles that refer to a specific context.
 */
#define VMCI_CONTEXT_RESOURCE_ID	0

/*
 *------------------------------------------------------------------------------
 *
 * VMCI error codes.
 *
 *------------------------------------------------------------------------------
 */

#define VMCI_SUCCESS_QUEUEPAIR_ATTACH		5
#define VMCI_SUCCESS_QUEUEPAIR_CREATE		4
#define VMCI_SUCCESS_LAST_DETACH		3
#define VMCI_SUCCESS_ACCESS_GRANTED		2
#define VMCI_SUCCESS_ENTRY_DEAD			1
#define VMCI_SUCCESS				0LL
#define VMCI_ERROR_INVALID_RESOURCE		(-1)
#define VMCI_ERROR_INVALID_ARGS			(-2)
#define VMCI_ERROR_NO_MEM			(-3)
#define VMCI_ERROR_DATAGRAM_FAILED		(-4)
#define VMCI_ERROR_MORE_DATA			(-5)
#define VMCI_ERROR_NO_MORE_DATAGRAMS		(-6)
#define VMCI_ERROR_NO_ACCESS			(-7)
#define VMCI_ERROR_NO_HANDLE			(-8)
#define VMCI_ERROR_DUPLICATE_ENTRY		(-9)
#define VMCI_ERROR_DST_UNREACHABLE		(-10)
#define VMCI_ERROR_PAYLOAD_TOO_LARGE		(-11)
#define VMCI_ERROR_INVALID_PRIV			(-12)
#define VMCI_ERROR_GENERIC			(-13)
#define VMCI_ERROR_PAGE_ALREADY_SHARED		(-14)
#define VMCI_ERROR_CANNOT_SHARE_PAGE		(-15)
#define VMCI_ERROR_CANNOT_UNSHARE_PAGE		(-16)
#define VMCI_ERROR_NO_PROCESS			(-17)
#define VMCI_ERROR_NO_DATAGRAM			(-18)
#define VMCI_ERROR_NO_RESOURCES			(-19)
#define VMCI_ERROR_UNAVAILABLE			(-20)
#define VMCI_ERROR_NOT_FOUND			(-21)
#define VMCI_ERROR_ALREADY_EXISTS		(-22)
#define VMCI_ERROR_NOT_PAGE_ALIGNED		(-23)
#define VMCI_ERROR_INVALID_SIZE			(-24)
#define VMCI_ERROR_REGION_ALREADY_SHARED	(-25)
#define VMCI_ERROR_TIMEOUT			(-26)
#define VMCI_ERROR_DATAGRAM_INCOMPLETE		(-27)
#define VMCI_ERROR_INCORRECT_IRQL		(-28)
#define VMCI_ERROR_EVENT_UNKNOWN		(-29)
#define VMCI_ERROR_OBSOLETE			(-30)
#define VMCI_ERROR_QUEUEPAIR_MISMATCH		(-31)
#define VMCI_ERROR_QUEUEPAIR_NOTSET		(-32)
#define VMCI_ERROR_QUEUEPAIR_NOTOWNER		(-33)
#define VMCI_ERROR_QUEUEPAIR_NOTATTACHED	(-34)
#define VMCI_ERROR_QUEUEPAIR_NOSPACE		(-35)
#define VMCI_ERROR_QUEUEPAIR_NODATA		(-36)
#define VMCI_ERROR_BUSMEM_INVALIDATION		(-37)
#define VMCI_ERROR_MODULE_NOT_LOADED		(-38)
#define VMCI_ERROR_DEVICE_NOT_FOUND		(-39)
#define VMCI_ERROR_QUEUEPAIR_NOT_READY		(-40)
#define VMCI_ERROR_WOULD_BLOCK			(-41)

/* VMCI clients should return error code withing this range */
#define VMCI_ERROR_CLIENT_MIN			(-500)
#define VMCI_ERROR_CLIENT_MAX			(-550)

/* Internal error codes. */
#define VMCI_SHAREDMEM_ERROR_BAD_CONTEXT	(-1000)

#define VMCI_PATH_MAX				256

/* VMCI reserved events. */
typedef uint32_t vmci_event_type;

#define VMCI_EVENT_CTX_ID_UPDATE	0	// Only applicable to guest
						// endpoints
#define VMCI_EVENT_CTX_REMOVED		1	// Applicable to guest and host
#define VMCI_EVENT_QP_RESUMED		2	// Only applicable to guest
						// endpoints
#define VMCI_EVENT_QP_PEER_ATTACH	3	// Applicable to guest, host
						// and VMX
#define VMCI_EVENT_QP_PEER_DETACH	4	// Applicable to guest, host
						// and VMX
#define VMCI_EVENT_MEM_ACCESS_ON	5	// Applicable to VMX and vmk. On
						// vmk, this event has the
						// Context payload type
#define VMCI_EVENT_MEM_ACCESS_OFF	6	// Applicable to VMX and vmk.
						// Same as above for the payload
						// type
#define VMCI_EVENT_GUEST_PAUSED		7	// Applicable to vmk. This
						// event has the Context
						// payload type
#define VMCI_EVENT_GUEST_UNPAUSED	8	// Applicable to vmk. Same as
						// above for the payload type.
#define VMCI_EVENT_MAX			9

/*
 * Of the above events, a few are reserved for use in the VMX, and other
 * endpoints (guest and host kernel) should not use them. For the rest of the
 * events, we allow both host and guest endpoints to subscribe to them, to
 * maintain the same API for host and guest endpoints.
 */

#define VMCI_EVENT_VALID_VMX(_event)					\
	(_event == VMCI_EVENT_QP_PEER_ATTACH ||				\
	_event == VMCI_EVENT_QP_PEER_DETACH ||				\
	_event == VMCI_EVENT_MEM_ACCESS_ON ||				\
	_event == VMCI_EVENT_MEM_ACCESS_OFF)

#define VMCI_EVENT_VALID(_event)					\
	(_event < VMCI_EVENT_MAX &&					\
	_event != VMCI_EVENT_MEM_ACCESS_ON &&				\
	_event != VMCI_EVENT_MEM_ACCESS_OFF &&				\
	_event != VMCI_EVENT_GUEST_PAUSED &&				\
	_event != VMCI_EVENT_GUEST_UNPAUSED)

/* Reserved guest datagram resource ids. */
#define VMCI_EVENT_HANDLER		0

/*
 * VMCI coarse-grained privileges (per context or host process/endpoint. An
 * entity with the restricted flag is only allowed to interact with the
 * hypervisor and trusted entities.
 */
typedef uint32_t vmci_privilege_flags;

#define VMCI_PRIVILEGE_FLAG_RESTRICTED		0x01
#define VMCI_PRIVILEGE_FLAG_TRUSTED		0x02
#define VMCI_PRIVILEGE_ALL_FLAGS					\
	(VMCI_PRIVILEGE_FLAG_RESTRICTED | VMCI_PRIVILEGE_FLAG_TRUSTED)
#define VMCI_NO_PRIVILEGE_FLAGS			0x00
#define VMCI_DEFAULT_PROC_PRIVILEGE_FLAGS	VMCI_NO_PRIVILEGE_FLAGS
#define VMCI_LEAST_PRIVILEGE_FLAGS		VMCI_PRIVILEGE_FLAG_RESTRICTED
#define VMCI_MAX_PRIVILEGE_FLAGS		VMCI_PRIVILEGE_FLAG_TRUSTED

/* 0 through VMCI_RESERVED_RESOURCE_ID_MAX are reserved. */
#define VMCI_RESERVED_RESOURCE_ID_MAX		1023

#define VMCI_DOMAIN_NAME_MAXLEN			32

#define VMCI_LGPFX				"vmci: "

/*
 * struct vmci_queue_header
 *
 * A Queue cannot stand by itself as designed. Each Queue's header contains a
 * pointer into itself (the producer_tail) and into its peer (consumer_head).
 * The reason for the separation is one of accessibility: Each end-point can
 * modify two things: where the next location to enqueue is within its produce_q
 * (producer_tail); and where the next dequeue location is in its consume_q
 * (consumer_head).
 *
 * An end-point cannot modify the pointers of its peer (guest to guest; NOTE
 * that in the host both queue headers are mapped r/w). But, each end-point
 * needs read access to both Queue header structures in order to determine how
 * much space is used (or left) in the Queue. This is because for an end-point
 * to know how full its produce_q is, it needs to use the consumer_head that
 * points into the produce_q but -that- consumer_head is in the Queue header
 * for that end-points consume_q.
 *
 * Thoroughly confused?  Sorry.
 *
 * producer_tail: the point to enqueue new entrants.  When you approach a line
 * in a store, for example, you walk up to the tail.
 *
 * consumer_head: the point in the queue from which the next element is
 * dequeued. In other words, who is next in line is he who is at the head of
 * the line.
 *
 * Also, producer_tail points to an empty byte in the Queue, whereas
 * consumer_head points to a valid byte of data (unless producer_tail ==
 * consumer_head in which case consumerHead does not point to a valid byte of
 * data).
 *
 * For a queue of buffer 'size' bytes, the tail and head pointers will be in
 * the range [0, size-1].
 *
 * If produce_q_header->producer_tail == consume_q_header->consumer_head then
 * the produce_q is empty.
 */
struct vmci_queue_header {
	/* All fields are 64bit and aligned. */
	struct vmci_handle	handle;		/* Identifier. */
	volatile uint64_t	producer_tail;	/* Offset in this queue. */
	volatile uint64_t	consumer_head;	/* Offset in peer queue. */
};


/*
 * If one client of a QueuePair is a 32bit entity, we restrict the QueuePair
 * size to be less than 4GB, and use 32bit atomic operations on the head and
 * tail pointers. 64bit atomic read on a 32bit entity involves cmpxchg8b which
 * is an atomic read-modify-write. This will cause traces to fire when a 32bit
 * consumer tries to read the producer's tail pointer, for example, because the
 * consumer has read-only access to the producer's tail pointer.
 *
 * We provide the following macros to invoke 32bit or 64bit atomic operations
 * based on the architecture the code is being compiled on.
 */

#ifdef __x86_64__
#define QP_MAX_QUEUE_SIZE_ARCH		CONST64U(0xffffffffffffffff)
#define qp_atomic_read_offset(x)	atomic_load_64(x)
#define qp_atomic_write_offset(x, y)	atomic_store_64(x, y)
#else /* __x86_64__ */
	/*
	 * Wrappers below are being used because atomic_store_<type> operates
	 * on a specific <type>. Likewise for atomic_load_<type>
	 */

	static inline uint32_t
	type_safe_atomic_read_32(void *var)
	{
		return (atomic_load_32((volatile uint32_t *)(var)));
	}

	static inline void
	type_safe_atomic_write_32(void *var, uint32_t val)
	{
		atomic_store_32((volatile uint32_t *)(var), (uint32_t)(val));
	}

#define QP_MAX_QUEUE_SIZE_ARCH		CONST64U(0xffffffff)
#define qp_atomic_read_offset(x)	type_safe_atomic_read_32((void *)(x))
#define qp_atomic_write_offset(x, y)					\
	type_safe_atomic_write_32((void *)(x), (uint32_t)(y))
#endif /* __x86_64__ */

/*
 *------------------------------------------------------------------------------
 *
 * qp_add_pointer --
 *
 *     Helper to add a given offset to a head or tail pointer. Wraps the value
 *     of the pointer around the max size of the queue.
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
qp_add_pointer(volatile uint64_t *var, size_t add, uint64_t size)
{
	uint64_t new_val = qp_atomic_read_offset(var);

	if (new_val >= size - add)
		new_val -= size;

	new_val += add;
	qp_atomic_write_offset(var, new_val);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_header_producer_tail --
 *
 *     Helper routine to get the Producer Tail from the supplied queue.
 *
 * Results:
 *     The contents of the queue's producer tail.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static inline uint64_t
vmci_queue_header_producer_tail(const struct vmci_queue_header *q_header)
{
	struct vmci_queue_header *qh = (struct vmci_queue_header *)q_header;
	return (qp_atomic_read_offset(&qh->producer_tail));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_header_consumer_head --
 *
 *     Helper routine to get the Consumer Head from the supplied queue.
 *
 * Results:
 *     The contents of the queue's consumer tail.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static inline uint64_t
vmci_queue_header_consumer_head(const struct vmci_queue_header *q_header)
{
	struct vmci_queue_header *qh = (struct vmci_queue_header *)q_header;
	return (qp_atomic_read_offset(&qh->consumer_head));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_header_add_producer_tail --
 *
 *     Helper routine to increment the Producer Tail. Fundamentally,
 *     qp_add_pointer() is used to manipulate the tail itself.
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
vmci_queue_header_add_producer_tail(struct vmci_queue_header *q_header,
    size_t add, uint64_t queue_size)
{

	qp_add_pointer(&q_header->producer_tail, add, queue_size);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_header_add_consumer_head --
 *
 *     Helper routine to increment the Consumer Head. Fundamentally,
 *     qp_add_pointer() is used to manipulate the head itself.
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
vmci_queue_header_add_consumer_head(struct vmci_queue_header *q_header,
    size_t add, uint64_t queue_size)
{

	qp_add_pointer(&q_header->consumer_head, add, queue_size);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_header_get_pointers --
 *
 *     Helper routine for getting the head and the tail pointer for a queue.
 *     Both the VMCIQueues are needed to get both the pointers for one queue.
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
vmci_queue_header_get_pointers(const struct vmci_queue_header *produce_q_header,
    const struct vmci_queue_header *consume_q_header, uint64_t *producer_tail,
    uint64_t *consumer_head)
{

	if (producer_tail)
		*producer_tail =
		    vmci_queue_header_producer_tail(produce_q_header);

	if (consumer_head)
		*consumer_head =
		    vmci_queue_header_consumer_head(consume_q_header);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_header_reset_pointers --
 *
 *     Reset the tail pointer (of "this" queue) and the head pointer (of "peer"
 *     queue).
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
vmci_queue_header_reset_pointers(struct vmci_queue_header *q_header)
{

	qp_atomic_write_offset(&q_header->producer_tail, CONST64U(0));
	qp_atomic_write_offset(&q_header->consumer_head, CONST64U(0));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_header_init --
 *
 *     Initializes a queue's state (head & tail pointers).
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
vmci_queue_header_init(struct vmci_queue_header *q_header,
    const struct vmci_handle handle)
{

	q_header->handle = handle;
	vmci_queue_header_reset_pointers(q_header);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_header_free_space --
 *
 *     Finds available free space in a produce queue to enqueue more data or
 *     reports an error if queue pair corruption is detected.
 *
 * Results:
 *     Free space size in bytes or an error code.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static inline int64_t
vmci_queue_header_free_space(const struct vmci_queue_header *produce_q_header,
    const struct vmci_queue_header *consume_q_header,
    const uint64_t produce_q_size)
{
	uint64_t free_space;
	uint64_t head;
	uint64_t tail;

	tail = vmci_queue_header_producer_tail(produce_q_header);
	head = vmci_queue_header_consumer_head(consume_q_header);

	if (tail >= produce_q_size || head >= produce_q_size)
		return (VMCI_ERROR_INVALID_SIZE);

	/*
	 * Deduct 1 to avoid tail becoming equal to head which causes ambiguity.
	 * If head and tail are equal it means that the queue is empty.
	 */

	if (tail >= head)
		free_space = produce_q_size - (tail - head) - 1;
	else
		free_space = head - tail - 1;

	return (free_space);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_queue_header_buf_ready --
 *
 *     vmci_queue_header_free_space() does all the heavy lifting of determing
 *     the number of free bytes in a Queue. This routine, then subtracts that
 *     size from the full size of the Queue so the caller knows how many bytes
 *     are ready to be dequeued.
 *
 * Results:
 *     On success, available data size in bytes (up to MAX_INT64).
 *     On failure, appropriate error code.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static inline int64_t
vmci_queue_header_buf_ready(const struct vmci_queue_header *consume_q_header,
    const struct vmci_queue_header *produce_q_header,
    const uint64_t consume_q_size)
{
	int64_t free_space;

	free_space = vmci_queue_header_free_space(consume_q_header,
	    produce_q_header, consume_q_size);
	if (free_space < VMCI_SUCCESS)
		return (free_space);
	else
		return (consume_q_size - free_space - 1);
}

#endif /* !_VMCI_DEFS_H_ */
