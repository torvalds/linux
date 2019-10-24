/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 */

#ifndef _VMW_VMCI_DEF_H_
#define _VMW_VMCI_DEF_H_

#include <linux/atomic.h>
#include <linux/bits.h>

/* Register offsets. */
#define VMCI_STATUS_ADDR      0x00
#define VMCI_CONTROL_ADDR     0x04
#define VMCI_ICR_ADDR	      0x08
#define VMCI_IMR_ADDR         0x0c
#define VMCI_DATA_OUT_ADDR    0x10
#define VMCI_DATA_IN_ADDR     0x14
#define VMCI_CAPS_ADDR        0x18
#define VMCI_RESULT_LOW_ADDR  0x1c
#define VMCI_RESULT_HIGH_ADDR 0x20

/* Max number of devices. */
#define VMCI_MAX_DEVICES 1

/* Status register bits. */
#define VMCI_STATUS_INT_ON     BIT(0)

/* Control register bits. */
#define VMCI_CONTROL_RESET        BIT(0)
#define VMCI_CONTROL_INT_ENABLE   BIT(1)
#define VMCI_CONTROL_INT_DISABLE  BIT(2)

/* Capabilities register bits. */
#define VMCI_CAPS_HYPERCALL     BIT(0)
#define VMCI_CAPS_GUESTCALL     BIT(1)
#define VMCI_CAPS_DATAGRAM      BIT(2)
#define VMCI_CAPS_NOTIFICATIONS BIT(3)
#define VMCI_CAPS_PPN64         BIT(4)

/* Interrupt Cause register bits. */
#define VMCI_ICR_DATAGRAM      BIT(0)
#define VMCI_ICR_NOTIFICATION  BIT(1)

/* Interrupt Mask register bits. */
#define VMCI_IMR_DATAGRAM      BIT(0)
#define VMCI_IMR_NOTIFICATION  BIT(1)

/* Maximum MSI/MSI-X interrupt vectors in the device. */
#define VMCI_MAX_INTRS 2

/*
 * Supported interrupt vectors.  There is one for each ICR value above,
 * but here they indicate the position in the vector array/message ID.
 */
enum {
	VMCI_INTR_DATAGRAM = 0,
	VMCI_INTR_NOTIFICATION = 1,
};

/*
 * A single VMCI device has an upper limit of 128MB on the amount of
 * memory that can be used for queue pairs. Since each queue pair
 * consists of at least two pages, the memory limit also dictates the
 * number of queue pairs a guest can create.
 */
#define VMCI_MAX_GUEST_QP_MEMORY (128 * 1024 * 1024)
#define VMCI_MAX_GUEST_QP_COUNT  (VMCI_MAX_GUEST_QP_MEMORY / PAGE_SIZE / 2)

/*
 * There can be at most PAGE_SIZE doorbells since there is one doorbell
 * per byte in the doorbell bitmap page.
 */
#define VMCI_MAX_GUEST_DOORBELL_COUNT PAGE_SIZE

/*
 * Queues with pre-mapped data pages must be small, so that we don't pin
 * too much kernel memory (especially on vmkernel).  We limit a queuepair to
 * 32 KB, or 16 KB per queue for symmetrical pairs.
 */
#define VMCI_MAX_PINNED_QP_MEMORY (32 * 1024)

/*
 * We have a fixed set of resource IDs available in the VMX.
 * This allows us to have a very simple implementation since we statically
 * know how many will create datagram handles. If a new caller arrives and
 * we have run out of slots we can manually increment the maximum size of
 * available resource IDs.
 *
 * VMCI reserved hypervisor datagram resource IDs.
 */
enum {
	VMCI_RESOURCES_QUERY = 0,
	VMCI_GET_CONTEXT_ID = 1,
	VMCI_SET_NOTIFY_BITMAP = 2,
	VMCI_DOORBELL_LINK = 3,
	VMCI_DOORBELL_UNLINK = 4,
	VMCI_DOORBELL_NOTIFY = 5,
	/*
	 * VMCI_DATAGRAM_REQUEST_MAP and VMCI_DATAGRAM_REMOVE_MAP are
	 * obsoleted by the removal of VM to VM communication.
	 */
	VMCI_DATAGRAM_REQUEST_MAP = 6,
	VMCI_DATAGRAM_REMOVE_MAP = 7,
	VMCI_EVENT_SUBSCRIBE = 8,
	VMCI_EVENT_UNSUBSCRIBE = 9,
	VMCI_QUEUEPAIR_ALLOC = 10,
	VMCI_QUEUEPAIR_DETACH = 11,

	/*
	 * VMCI_VSOCK_VMX_LOOKUP was assigned to 12 for Fusion 3.0/3.1,
	 * WS 7.0/7.1 and ESX 4.1
	 */
	VMCI_HGFS_TRANSPORT = 13,
	VMCI_UNITY_PBRPC_REGISTER = 14,
	VMCI_RPC_PRIVILEGED = 15,
	VMCI_RPC_UNPRIVILEGED = 16,
	VMCI_RESOURCE_MAX = 17,
};

/*
 * struct vmci_handle - Ownership information structure
 * @context:    The VMX context ID.
 * @resource:   The resource ID (used for locating in resource hash).
 *
 * The vmci_handle structure is used to track resources used within
 * vmw_vmci.
 */
struct vmci_handle {
	u32 context;
	u32 resource;
};

#define vmci_make_handle(_cid, _rid) \
	(struct vmci_handle){ .context = _cid, .resource = _rid }

static inline bool vmci_handle_is_equal(struct vmci_handle h1,
					struct vmci_handle h2)
{
	return h1.context == h2.context && h1.resource == h2.resource;
}

#define VMCI_INVALID_ID ~0
static const struct vmci_handle VMCI_INVALID_HANDLE = {
	.context = VMCI_INVALID_ID,
	.resource = VMCI_INVALID_ID
};

static inline bool vmci_handle_is_invalid(struct vmci_handle h)
{
	return vmci_handle_is_equal(h, VMCI_INVALID_HANDLE);
}

/*
 * The below defines can be used to send anonymous requests.
 * This also indicates that no response is expected.
 */
#define VMCI_ANON_SRC_CONTEXT_ID   VMCI_INVALID_ID
#define VMCI_ANON_SRC_RESOURCE_ID  VMCI_INVALID_ID
static const struct vmci_handle VMCI_ANON_SRC_HANDLE = {
	.context = VMCI_ANON_SRC_CONTEXT_ID,
	.resource = VMCI_ANON_SRC_RESOURCE_ID
};

/* The lowest 16 context ids are reserved for internal use. */
#define VMCI_RESERVED_CID_LIMIT ((u32) 16)

/*
 * Hypervisor context id, used for calling into hypervisor
 * supplied services from the VM.
 */
#define VMCI_HYPERVISOR_CONTEXT_ID 0

/*
 * Well-known context id, a logical context that contains a set of
 * well-known services. This context ID is now obsolete.
 */
#define VMCI_WELL_KNOWN_CONTEXT_ID 1

/*
 * Context ID used by host endpoints.
 */
#define VMCI_HOST_CONTEXT_ID  2

#define VMCI_CONTEXT_IS_VM(_cid) (VMCI_INVALID_ID != (_cid) &&		\
				  (_cid) > VMCI_HOST_CONTEXT_ID)

/*
 * The VMCI_CONTEXT_RESOURCE_ID is used together with vmci_make_handle to make
 * handles that refer to a specific context.
 */
#define VMCI_CONTEXT_RESOURCE_ID 0

/*
 * VMCI error codes.
 */
enum {
	VMCI_SUCCESS_QUEUEPAIR_ATTACH	= 5,
	VMCI_SUCCESS_QUEUEPAIR_CREATE	= 4,
	VMCI_SUCCESS_LAST_DETACH	= 3,
	VMCI_SUCCESS_ACCESS_GRANTED	= 2,
	VMCI_SUCCESS_ENTRY_DEAD		= 1,
	VMCI_SUCCESS			 = 0,
	VMCI_ERROR_INVALID_RESOURCE	 = (-1),
	VMCI_ERROR_INVALID_ARGS		 = (-2),
	VMCI_ERROR_NO_MEM		 = (-3),
	VMCI_ERROR_DATAGRAM_FAILED	 = (-4),
	VMCI_ERROR_MORE_DATA		 = (-5),
	VMCI_ERROR_NO_MORE_DATAGRAMS	 = (-6),
	VMCI_ERROR_NO_ACCESS		 = (-7),
	VMCI_ERROR_NO_HANDLE		 = (-8),
	VMCI_ERROR_DUPLICATE_ENTRY	 = (-9),
	VMCI_ERROR_DST_UNREACHABLE	 = (-10),
	VMCI_ERROR_PAYLOAD_TOO_LARGE	 = (-11),
	VMCI_ERROR_INVALID_PRIV		 = (-12),
	VMCI_ERROR_GENERIC		 = (-13),
	VMCI_ERROR_PAGE_ALREADY_SHARED	 = (-14),
	VMCI_ERROR_CANNOT_SHARE_PAGE	 = (-15),
	VMCI_ERROR_CANNOT_UNSHARE_PAGE	 = (-16),
	VMCI_ERROR_NO_PROCESS		 = (-17),
	VMCI_ERROR_NO_DATAGRAM		 = (-18),
	VMCI_ERROR_NO_RESOURCES		 = (-19),
	VMCI_ERROR_UNAVAILABLE		 = (-20),
	VMCI_ERROR_NOT_FOUND		 = (-21),
	VMCI_ERROR_ALREADY_EXISTS	 = (-22),
	VMCI_ERROR_NOT_PAGE_ALIGNED	 = (-23),
	VMCI_ERROR_INVALID_SIZE		 = (-24),
	VMCI_ERROR_REGION_ALREADY_SHARED = (-25),
	VMCI_ERROR_TIMEOUT		 = (-26),
	VMCI_ERROR_DATAGRAM_INCOMPLETE	 = (-27),
	VMCI_ERROR_INCORRECT_IRQL	 = (-28),
	VMCI_ERROR_EVENT_UNKNOWN	 = (-29),
	VMCI_ERROR_OBSOLETE		 = (-30),
	VMCI_ERROR_QUEUEPAIR_MISMATCH	 = (-31),
	VMCI_ERROR_QUEUEPAIR_NOTSET	 = (-32),
	VMCI_ERROR_QUEUEPAIR_NOTOWNER	 = (-33),
	VMCI_ERROR_QUEUEPAIR_NOTATTACHED = (-34),
	VMCI_ERROR_QUEUEPAIR_NOSPACE	 = (-35),
	VMCI_ERROR_QUEUEPAIR_NODATA	 = (-36),
	VMCI_ERROR_BUSMEM_INVALIDATION	 = (-37),
	VMCI_ERROR_MODULE_NOT_LOADED	 = (-38),
	VMCI_ERROR_DEVICE_NOT_FOUND	 = (-39),
	VMCI_ERROR_QUEUEPAIR_NOT_READY	 = (-40),
	VMCI_ERROR_WOULD_BLOCK		 = (-41),

	/* VMCI clients should return error code within this range */
	VMCI_ERROR_CLIENT_MIN		 = (-500),
	VMCI_ERROR_CLIENT_MAX		 = (-550),

	/* Internal error codes. */
	VMCI_SHAREDMEM_ERROR_BAD_CONTEXT = (-1000),
};

/* VMCI reserved events. */
enum {
	/* Only applicable to guest endpoints */
	VMCI_EVENT_CTX_ID_UPDATE  = 0,

	/* Applicable to guest and host */
	VMCI_EVENT_CTX_REMOVED	  = 1,

	/* Only applicable to guest endpoints */
	VMCI_EVENT_QP_RESUMED	  = 2,

	/* Applicable to guest and host */
	VMCI_EVENT_QP_PEER_ATTACH = 3,

	/* Applicable to guest and host */
	VMCI_EVENT_QP_PEER_DETACH = 4,

	/*
	 * Applicable to VMX and vmk.  On vmk,
	 * this event has the Context payload type.
	 */
	VMCI_EVENT_MEM_ACCESS_ON  = 5,

	/*
	 * Applicable to VMX and vmk.  Same as
	 * above for the payload type.
	 */
	VMCI_EVENT_MEM_ACCESS_OFF = 6,
	VMCI_EVENT_MAX		  = 7,
};

/*
 * Of the above events, a few are reserved for use in the VMX, and
 * other endpoints (guest and host kernel) should not use them. For
 * the rest of the events, we allow both host and guest endpoints to
 * subscribe to them, to maintain the same API for host and guest
 * endpoints.
 */
#define VMCI_EVENT_VALID_VMX(_event) ((_event) == VMCI_EVENT_MEM_ACCESS_ON || \
				      (_event) == VMCI_EVENT_MEM_ACCESS_OFF)

#define VMCI_EVENT_VALID(_event) ((_event) < VMCI_EVENT_MAX &&		\
				  !VMCI_EVENT_VALID_VMX(_event))

/* Reserved guest datagram resource ids. */
#define VMCI_EVENT_HANDLER 0

/*
 * VMCI coarse-grained privileges (per context or host
 * process/endpoint. An entity with the restricted flag is only
 * allowed to interact with the hypervisor and trusted entities.
 */
enum {
	VMCI_NO_PRIVILEGE_FLAGS = 0,
	VMCI_PRIVILEGE_FLAG_RESTRICTED = 1,
	VMCI_PRIVILEGE_FLAG_TRUSTED = 2,
	VMCI_PRIVILEGE_ALL_FLAGS = (VMCI_PRIVILEGE_FLAG_RESTRICTED |
				    VMCI_PRIVILEGE_FLAG_TRUSTED),
	VMCI_DEFAULT_PROC_PRIVILEGE_FLAGS = VMCI_NO_PRIVILEGE_FLAGS,
	VMCI_LEAST_PRIVILEGE_FLAGS = VMCI_PRIVILEGE_FLAG_RESTRICTED,
	VMCI_MAX_PRIVILEGE_FLAGS = VMCI_PRIVILEGE_FLAG_TRUSTED,
};

/* 0 through VMCI_RESERVED_RESOURCE_ID_MAX are reserved. */
#define VMCI_RESERVED_RESOURCE_ID_MAX 1023

/*
 * Driver version.
 *
 * Increment major version when you make an incompatible change.
 * Compatibility goes both ways (old driver with new executable
 * as well as new driver with old executable).
 */

/* Never change VMCI_VERSION_SHIFT_WIDTH */
#define VMCI_VERSION_SHIFT_WIDTH 16
#define VMCI_MAKE_VERSION(_major, _minor)			\
	((_major) << VMCI_VERSION_SHIFT_WIDTH | (u16) (_minor))

#define VMCI_VERSION_MAJOR(v)  ((u32) (v) >> VMCI_VERSION_SHIFT_WIDTH)
#define VMCI_VERSION_MINOR(v)  ((u16) (v))

/*
 * VMCI_VERSION is always the current version.  Subsequently listed
 * versions are ways of detecting previous versions of the connecting
 * application (i.e., VMX).
 *
 * VMCI_VERSION_NOVMVM: This version removed support for VM to VM
 * communication.
 *
 * VMCI_VERSION_NOTIFY: This version introduced doorbell notification
 * support.
 *
 * VMCI_VERSION_HOSTQP: This version introduced host end point support
 * for hosted products.
 *
 * VMCI_VERSION_PREHOSTQP: This is the version prior to the adoption of
 * support for host end-points.
 *
 * VMCI_VERSION_PREVERS2: This fictional version number is intended to
 * represent the version of a VMX which doesn't call into the driver
 * with ioctl VERSION2 and thus doesn't establish its version with the
 * driver.
 */

#define VMCI_VERSION                VMCI_VERSION_NOVMVM
#define VMCI_VERSION_NOVMVM         VMCI_MAKE_VERSION(11, 0)
#define VMCI_VERSION_NOTIFY         VMCI_MAKE_VERSION(10, 0)
#define VMCI_VERSION_HOSTQP         VMCI_MAKE_VERSION(9, 0)
#define VMCI_VERSION_PREHOSTQP      VMCI_MAKE_VERSION(8, 0)
#define VMCI_VERSION_PREVERS2       VMCI_MAKE_VERSION(1, 0)

#define VMCI_SOCKETS_MAKE_VERSION(_p)					\
	((((_p)[0] & 0xFF) << 24) | (((_p)[1] & 0xFF) << 16) | ((_p)[2]))

/*
 * The VMCI IOCTLs.  We use identity code 7, as noted in ioctl-number.h, and
 * we start at sequence 9f.  This gives us the same values that our shipping
 * products use, starting at 1951, provided we leave out the direction and
 * structure size.  Note that VMMon occupies the block following us, starting
 * at 2001.
 */
#define IOCTL_VMCI_VERSION			_IO(7, 0x9f)	/* 1951 */
#define IOCTL_VMCI_INIT_CONTEXT			_IO(7, 0xa0)
#define IOCTL_VMCI_QUEUEPAIR_SETVA		_IO(7, 0xa4)
#define IOCTL_VMCI_NOTIFY_RESOURCE		_IO(7, 0xa5)
#define IOCTL_VMCI_NOTIFICATIONS_RECEIVE	_IO(7, 0xa6)
#define IOCTL_VMCI_VERSION2			_IO(7, 0xa7)
#define IOCTL_VMCI_QUEUEPAIR_ALLOC		_IO(7, 0xa8)
#define IOCTL_VMCI_QUEUEPAIR_SETPAGEFILE	_IO(7, 0xa9)
#define IOCTL_VMCI_QUEUEPAIR_DETACH		_IO(7, 0xaa)
#define IOCTL_VMCI_DATAGRAM_SEND		_IO(7, 0xab)
#define IOCTL_VMCI_DATAGRAM_RECEIVE		_IO(7, 0xac)
#define IOCTL_VMCI_CTX_ADD_NOTIFICATION		_IO(7, 0xaf)
#define IOCTL_VMCI_CTX_REMOVE_NOTIFICATION	_IO(7, 0xb0)
#define IOCTL_VMCI_CTX_GET_CPT_STATE		_IO(7, 0xb1)
#define IOCTL_VMCI_CTX_SET_CPT_STATE		_IO(7, 0xb2)
#define IOCTL_VMCI_GET_CONTEXT_ID		_IO(7, 0xb3)
#define IOCTL_VMCI_SOCKETS_VERSION		_IO(7, 0xb4)
#define IOCTL_VMCI_SOCKETS_GET_AF_VALUE		_IO(7, 0xb8)
#define IOCTL_VMCI_SOCKETS_GET_LOCAL_CID	_IO(7, 0xb9)
#define IOCTL_VMCI_SET_NOTIFY			_IO(7, 0xcb)	/* 1995 */
/*IOCTL_VMMON_START				_IO(7, 0xd1)*/	/* 2001 */

/*
 * struct vmci_queue_header - VMCI Queue Header information.
 *
 * A Queue cannot stand by itself as designed.  Each Queue's header
 * contains a pointer into itself (the producer_tail) and into its peer
 * (consumer_head).  The reason for the separation is one of
 * accessibility: Each end-point can modify two things: where the next
 * location to enqueue is within its produce_q (producer_tail); and
 * where the next dequeue location is in its consume_q (consumer_head).
 *
 * An end-point cannot modify the pointers of its peer (guest to
 * guest; NOTE that in the host both queue headers are mapped r/w).
 * But, each end-point needs read access to both Queue header
 * structures in order to determine how much space is used (or left)
 * in the Queue.  This is because for an end-point to know how full
 * its produce_q is, it needs to use the consumer_head that points into
 * the produce_q but -that- consumer_head is in the Queue header for
 * that end-points consume_q.
 *
 * Thoroughly confused?  Sorry.
 *
 * producer_tail: the point to enqueue new entrants.  When you approach
 * a line in a store, for example, you walk up to the tail.
 *
 * consumer_head: the point in the queue from which the next element is
 * dequeued.  In other words, who is next in line is he who is at the
 * head of the line.
 *
 * Also, producer_tail points to an empty byte in the Queue, whereas
 * consumer_head points to a valid byte of data (unless producer_tail ==
 * consumer_head in which case consumer_head does not point to a valid
 * byte of data).
 *
 * For a queue of buffer 'size' bytes, the tail and head pointers will be in
 * the range [0, size-1].
 *
 * If produce_q_header->producer_tail == consume_q_header->consumer_head
 * then the produce_q is empty.
 */
struct vmci_queue_header {
	/* All fields are 64bit and aligned. */
	struct vmci_handle handle;	/* Identifier. */
	u64 producer_tail;	/* Offset in this queue. */
	u64 consumer_head;	/* Offset in peer queue. */
};

/*
 * struct vmci_datagram - Base struct for vmci datagrams.
 * @dst:        A vmci_handle that tracks the destination of the datagram.
 * @src:        A vmci_handle that tracks the source of the datagram.
 * @payload_size:       The size of the payload.
 *
 * vmci_datagram structs are used when sending vmci datagrams.  They include
 * the necessary source and destination information to properly route
 * the information along with the size of the package.
 */
struct vmci_datagram {
	struct vmci_handle dst;
	struct vmci_handle src;
	u64 payload_size;
};

/*
 * Second flag is for creating a well-known handle instead of a per context
 * handle.  Next flag is for deferring datagram delivery, so that the
 * datagram callback is invoked in a delayed context (not interrupt context).
 */
#define VMCI_FLAG_DG_NONE          0
#define VMCI_FLAG_WELLKNOWN_DG_HND BIT(0)
#define VMCI_FLAG_ANYCID_DG_HND    BIT(1)
#define VMCI_FLAG_DG_DELAYED_CB    BIT(2)

/*
 * Maximum supported size of a VMCI datagram for routable datagrams.
 * Datagrams going to the hypervisor are allowed to be larger.
 */
#define VMCI_MAX_DG_SIZE (17 * 4096)
#define VMCI_MAX_DG_PAYLOAD_SIZE (VMCI_MAX_DG_SIZE - \
				  sizeof(struct vmci_datagram))
#define VMCI_DG_PAYLOAD(_dg) (void *)((char *)(_dg) +			\
				      sizeof(struct vmci_datagram))
#define VMCI_DG_HEADERSIZE sizeof(struct vmci_datagram)
#define VMCI_DG_SIZE(_dg) (VMCI_DG_HEADERSIZE + (size_t)(_dg)->payload_size)
#define VMCI_DG_SIZE_ALIGNED(_dg) ((VMCI_DG_SIZE(_dg) + 7) & (~((size_t) 0x7)))
#define VMCI_MAX_DATAGRAM_QUEUE_SIZE (VMCI_MAX_DG_SIZE * 2)

struct vmci_event_payload_qp {
	struct vmci_handle handle;  /* queue_pair handle. */
	u32 peer_id;		    /* Context id of attaching/detaching VM. */
	u32 _pad;
};

/* Flags for VMCI queue_pair API. */
enum {
	/* Fail alloc if QP not created by peer. */
	VMCI_QPFLAG_ATTACH_ONLY = 1 << 0,

	/* Only allow attaches from local context. */
	VMCI_QPFLAG_LOCAL = 1 << 1,

	/* Host won't block when guest is quiesced. */
	VMCI_QPFLAG_NONBLOCK = 1 << 2,

	/* Pin data pages in ESX.  Used with NONBLOCK */
	VMCI_QPFLAG_PINNED = 1 << 3,

	/* Update the following flag when adding new flags. */
	VMCI_QP_ALL_FLAGS = (VMCI_QPFLAG_ATTACH_ONLY | VMCI_QPFLAG_LOCAL |
			     VMCI_QPFLAG_NONBLOCK | VMCI_QPFLAG_PINNED),

	/* Convenience flags */
	VMCI_QP_ASYMM = (VMCI_QPFLAG_NONBLOCK | VMCI_QPFLAG_PINNED),
	VMCI_QP_ASYMM_PEER = (VMCI_QPFLAG_ATTACH_ONLY | VMCI_QP_ASYMM),
};

/*
 * We allow at least 1024 more event datagrams from the hypervisor past the
 * normally allowed datagrams pending for a given context.  We define this
 * limit on event datagrams from the hypervisor to guard against DoS attack
 * from a malicious VM which could repeatedly attach to and detach from a queue
 * pair, causing events to be queued at the destination VM.  However, the rate
 * at which such events can be generated is small since it requires a VM exit
 * and handling of queue pair attach/detach call at the hypervisor.  Event
 * datagrams may be queued up at the destination VM if it has interrupts
 * disabled or if it is not draining events for some other reason.  1024
 * datagrams is a grossly conservative estimate of the time for which
 * interrupts may be disabled in the destination VM, but at the same time does
 * not exacerbate the memory pressure problem on the host by much (size of each
 * event datagram is small).
 */
#define VMCI_MAX_DATAGRAM_AND_EVENT_QUEUE_SIZE				\
	(VMCI_MAX_DATAGRAM_QUEUE_SIZE +					\
	 1024 * (sizeof(struct vmci_datagram) +				\
		 sizeof(struct vmci_event_data_max)))

/*
 * Struct used for querying, via VMCI_RESOURCES_QUERY, the availability of
 * hypervisor resources.  Struct size is 16 bytes. All fields in struct are
 * aligned to their natural alignment.
 */
struct vmci_resource_query_hdr {
	struct vmci_datagram hdr;
	u32 num_resources;
	u32 _padding;
};

/*
 * Convenience struct for negotiating vectors. Must match layout of
 * VMCIResourceQueryHdr minus the struct vmci_datagram header.
 */
struct vmci_resource_query_msg {
	u32 num_resources;
	u32 _padding;
	u32 resources[1];
};

/*
 * The maximum number of resources that can be queried using
 * VMCI_RESOURCE_QUERY is 31, as the result is encoded in the lower 31
 * bits of a positive return value. Negative values are reserved for
 * errors.
 */
#define VMCI_RESOURCE_QUERY_MAX_NUM 31

/* Maximum size for the VMCI_RESOURCE_QUERY request. */
#define VMCI_RESOURCE_QUERY_MAX_SIZE				\
	(sizeof(struct vmci_resource_query_hdr) +		\
	 sizeof(u32) * VMCI_RESOURCE_QUERY_MAX_NUM)

/*
 * Struct used for setting the notification bitmap.  All fields in
 * struct are aligned to their natural alignment.
 */
struct vmci_notify_bm_set_msg {
	struct vmci_datagram hdr;
	union {
		u32 bitmap_ppn32;
		u64 bitmap_ppn64;
	};
};

/*
 * Struct used for linking a doorbell handle with an index in the
 * notify bitmap. All fields in struct are aligned to their natural
 * alignment.
 */
struct vmci_doorbell_link_msg {
	struct vmci_datagram hdr;
	struct vmci_handle handle;
	u64 notify_idx;
};

/*
 * Struct used for unlinking a doorbell handle from an index in the
 * notify bitmap. All fields in struct are aligned to their natural
 * alignment.
 */
struct vmci_doorbell_unlink_msg {
	struct vmci_datagram hdr;
	struct vmci_handle handle;
};

/*
 * Struct used for generating a notification on a doorbell handle. All
 * fields in struct are aligned to their natural alignment.
 */
struct vmci_doorbell_notify_msg {
	struct vmci_datagram hdr;
	struct vmci_handle handle;
};

/*
 * This struct is used to contain data for events.  Size of this struct is a
 * multiple of 8 bytes, and all fields are aligned to their natural alignment.
 */
struct vmci_event_data {
	u32 event;		/* 4 bytes. */
	u32 _pad;
	/* Event payload is put here. */
};

/*
 * Define the different VMCI_EVENT payload data types here.  All structs must
 * be a multiple of 8 bytes, and fields must be aligned to their natural
 * alignment.
 */
struct vmci_event_payld_ctx {
	u32 context_id;	/* 4 bytes. */
	u32 _pad;
};

struct vmci_event_payld_qp {
	struct vmci_handle handle;  /* queue_pair handle. */
	u32 peer_id;	    /* Context id of attaching/detaching VM. */
	u32 _pad;
};

/*
 * We define the following struct to get the size of the maximum event
 * data the hypervisor may send to the guest.  If adding a new event
 * payload type above, add it to the following struct too (inside the
 * union).
 */
struct vmci_event_data_max {
	struct vmci_event_data event_data;
	union {
		struct vmci_event_payld_ctx context_payload;
		struct vmci_event_payld_qp qp_payload;
	} ev_data_payload;
};

/*
 * Struct used for VMCI_EVENT_SUBSCRIBE/UNSUBSCRIBE and
 * VMCI_EVENT_HANDLER messages.  Struct size is 32 bytes.  All fields
 * in struct are aligned to their natural alignment.
 */
struct vmci_event_msg {
	struct vmci_datagram hdr;

	/* Has event type and payload. */
	struct vmci_event_data event_data;

	/* Payload gets put here. */
};

/* Event with context payload. */
struct vmci_event_ctx {
	struct vmci_event_msg msg;
	struct vmci_event_payld_ctx payload;
};

/* Event with QP payload. */
struct vmci_event_qp {
	struct vmci_event_msg msg;
	struct vmci_event_payld_qp payload;
};

/*
 * Structs used for queue_pair alloc and detach messages.  We align fields of
 * these structs to 64bit boundaries.
 */
struct vmci_qp_alloc_msg {
	struct vmci_datagram hdr;
	struct vmci_handle handle;
	u32 peer;
	u32 flags;
	u64 produce_size;
	u64 consume_size;
	u64 num_ppns;

	/* List of PPNs placed here. */
};

struct vmci_qp_detach_msg {
	struct vmci_datagram hdr;
	struct vmci_handle handle;
};

/* VMCI Doorbell API. */
#define VMCI_FLAG_DELAYED_CB BIT(0)

typedef void (*vmci_callback) (void *client_data);

/*
 * struct vmci_qp - A vmw_vmci queue pair handle.
 *
 * This structure is used as a handle to a queue pair created by
 * VMCI.  It is intentionally left opaque to clients.
 */
struct vmci_qp;

/* Callback needed for correctly waiting on events. */
typedef int (*vmci_datagram_recv_cb) (void *client_data,
				      struct vmci_datagram *msg);

/* VMCI Event API. */
typedef void (*vmci_event_cb) (u32 sub_id, const struct vmci_event_data *ed,
			       void *client_data);

/*
 * We use the following inline function to access the payload data
 * associated with an event data.
 */
static inline const void *
vmci_event_data_const_payload(const struct vmci_event_data *ev_data)
{
	return (const char *)ev_data + sizeof(*ev_data);
}

static inline void *vmci_event_data_payload(struct vmci_event_data *ev_data)
{
	return (void *)vmci_event_data_const_payload(ev_data);
}

/*
 * Helper to read a value from a head or tail pointer. For X86_32, the
 * pointer is treated as a 32bit value, since the pointer value
 * never exceeds a 32bit value in this case. Also, doing an
 * atomic64_read on X86_32 uniprocessor systems may be implemented
 * as a non locked cmpxchg8b, that may end up overwriting updates done
 * by the VMCI device to the memory location. On 32bit SMP, the lock
 * prefix will be used, so correctness isn't an issue, but using a
 * 64bit operation still adds unnecessary overhead.
 */
static inline u64 vmci_q_read_pointer(u64 *var)
{
	return READ_ONCE(*(unsigned long *)var);
}

/*
 * Helper to set the value of a head or tail pointer. For X86_32, the
 * pointer is treated as a 32bit value, since the pointer value
 * never exceeds a 32bit value in this case. On 32bit SMP, using a
 * locked cmpxchg8b adds unnecessary overhead.
 */
static inline void vmci_q_set_pointer(u64 *var, u64 new_val)
{
	/* XXX buggered on big-endian */
	WRITE_ONCE(*(unsigned long *)var, (unsigned long)new_val);
}

/*
 * Helper to add a given offset to a head or tail pointer. Wraps the
 * value of the pointer around the max size of the queue.
 */
static inline void vmci_qp_add_pointer(u64 *var, size_t add, u64 size)
{
	u64 new_val = vmci_q_read_pointer(var);

	if (new_val >= size - add)
		new_val -= size;

	new_val += add;

	vmci_q_set_pointer(var, new_val);
}

/*
 * Helper routine to get the Producer Tail from the supplied queue.
 */
static inline u64
vmci_q_header_producer_tail(const struct vmci_queue_header *q_header)
{
	struct vmci_queue_header *qh = (struct vmci_queue_header *)q_header;
	return vmci_q_read_pointer(&qh->producer_tail);
}

/*
 * Helper routine to get the Consumer Head from the supplied queue.
 */
static inline u64
vmci_q_header_consumer_head(const struct vmci_queue_header *q_header)
{
	struct vmci_queue_header *qh = (struct vmci_queue_header *)q_header;
	return vmci_q_read_pointer(&qh->consumer_head);
}

/*
 * Helper routine to increment the Producer Tail.  Fundamentally,
 * vmci_qp_add_pointer() is used to manipulate the tail itself.
 */
static inline void
vmci_q_header_add_producer_tail(struct vmci_queue_header *q_header,
				size_t add,
				u64 queue_size)
{
	vmci_qp_add_pointer(&q_header->producer_tail, add, queue_size);
}

/*
 * Helper routine to increment the Consumer Head.  Fundamentally,
 * vmci_qp_add_pointer() is used to manipulate the head itself.
 */
static inline void
vmci_q_header_add_consumer_head(struct vmci_queue_header *q_header,
				size_t add,
				u64 queue_size)
{
	vmci_qp_add_pointer(&q_header->consumer_head, add, queue_size);
}

/*
 * Helper routine for getting the head and the tail pointer for a queue.
 * Both the VMCIQueues are needed to get both the pointers for one queue.
 */
static inline void
vmci_q_header_get_pointers(const struct vmci_queue_header *produce_q_header,
			   const struct vmci_queue_header *consume_q_header,
			   u64 *producer_tail,
			   u64 *consumer_head)
{
	if (producer_tail)
		*producer_tail = vmci_q_header_producer_tail(produce_q_header);

	if (consumer_head)
		*consumer_head = vmci_q_header_consumer_head(consume_q_header);
}

static inline void vmci_q_header_init(struct vmci_queue_header *q_header,
				      const struct vmci_handle handle)
{
	q_header->handle = handle;
	q_header->producer_tail = 0;
	q_header->consumer_head = 0;
}

/*
 * Finds available free space in a produce queue to enqueue more
 * data or reports an error if queue pair corruption is detected.
 */
static s64
vmci_q_header_free_space(const struct vmci_queue_header *produce_q_header,
			 const struct vmci_queue_header *consume_q_header,
			 const u64 produce_q_size)
{
	u64 tail;
	u64 head;
	u64 free_space;

	tail = vmci_q_header_producer_tail(produce_q_header);
	head = vmci_q_header_consumer_head(consume_q_header);

	if (tail >= produce_q_size || head >= produce_q_size)
		return VMCI_ERROR_INVALID_SIZE;

	/*
	 * Deduct 1 to avoid tail becoming equal to head which causes
	 * ambiguity. If head and tail are equal it means that the
	 * queue is empty.
	 */
	if (tail >= head)
		free_space = produce_q_size - (tail - head) - 1;
	else
		free_space = head - tail - 1;

	return free_space;
}

/*
 * vmci_q_header_free_space() does all the heavy lifting of
 * determing the number of free bytes in a Queue.  This routine,
 * then subtracts that size from the full size of the Queue so
 * the caller knows how many bytes are ready to be dequeued.
 * Results:
 * On success, available data size in bytes (up to MAX_INT64).
 * On failure, appropriate error code.
 */
static inline s64
vmci_q_header_buf_ready(const struct vmci_queue_header *consume_q_header,
			const struct vmci_queue_header *produce_q_header,
			const u64 consume_q_size)
{
	s64 free_space;

	free_space = vmci_q_header_free_space(consume_q_header,
					      produce_q_header, consume_q_size);
	if (free_space < VMCI_SUCCESS)
		return free_space;

	return consume_q_size - free_space - 1;
}


#endif /* _VMW_VMCI_DEF_H_ */
