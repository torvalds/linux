/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

#ifndef _VMCI_CALL_DEFS_H_
#define _VMCI_CALL_DEFS_H_

#include "vmci_defs.h"

/*
 * All structs here are an integral size of their largest member, ie. a struct
 * with at least one 8-byte member will have a size that is an integral of 8.
 * A struct which has a largest member of size 4 will have a size that is an
 * integral of 4.
 */

/*
 * Base struct for vmci datagrams.
 */
struct vmci_datagram {
	struct vmci_handle	dst;
	struct vmci_handle	src;
	uint64_t		payload_size;
};

/*
 * Second flag is for creating a well-known handle instead of a per context
 * handle. Next flag is for deferring datagram delivery, so that the
 * datagram callback is invoked in a delayed context (not interrupt context).
 */
#define VMCI_FLAG_DG_NONE		0
#define VMCI_FLAG_WELLKNOWN_DG_HND	0x1
#define VMCI_FLAG_ANYCID_DG_HND		0x2
#define VMCI_FLAG_DG_DELAYED_CB		0x4

/* Event callback should fire in a delayed context (not interrupt context.) */
#define VMCI_FLAG_EVENT_NONE		0
#define VMCI_FLAG_EVENT_DELAYED_CB	0x1

/*
 * Maximum supported size of a VMCI datagram for routable datagrams.
 * Datagrams going to the hypervisor are allowed to be larger.
 */
#define VMCI_MAX_DG_SIZE						\
	(17 * 4096)
#define VMCI_MAX_DG_PAYLOAD_SIZE					\
	(VMCI_MAX_DG_SIZE - sizeof(struct vmci_datagram))
#define VMCI_DG_PAYLOAD(_dg)						\
	(void *)((char *)(_dg) + sizeof(struct vmci_datagram))
#define VMCI_DG_HEADERSIZE						\
	sizeof(struct vmci_datagram)
#define VMCI_DG_SIZE(_dg)						\
	(VMCI_DG_HEADERSIZE + (size_t)(_dg)->payload_size)
#define VMCI_DG_SIZE_ALIGNED(_dg)					\
	((VMCI_DG_SIZE(_dg) + 7) & (size_t)~7)

/*
 * Struct used for querying, via VMCI_RESOURCES_QUERY, the availability of
 * hypervisor resources.
 * Struct size is 16 bytes. All fields in struct are aligned to their natural
 * alignment.
 */
struct vmci_resources_query_hdr {
	struct vmci_datagram	hdr;
	uint32_t		num_resources;
	uint32_t		_padding;
};

/*
 * Convenience struct for negotiating vectors. Must match layout of
 * vmci_resource_query_hdr minus the struct vmci_datagram header.
 */
struct vmci_resources_query_msg {
	uint32_t		num_resources;
	uint32_t		_padding;
	vmci_resource		resources[1];
};

/*
 * Struct used for setting the notification bitmap. All fields in struct are
 * aligned to their natural alignment.
 */
struct vmci_notify_bitmap_set_msg {
	struct vmci_datagram	hdr;
	PPN			bitmap_ppn;
	uint32_t		_pad;
};

/*
 * Struct used for linking a doorbell handle with an index in the notify
 * bitmap. All fields in struct are aligned to their natural alignment.
 */
struct vmci_doorbell_link_msg {
	struct vmci_datagram	hdr;
	struct vmci_handle	handle;
	uint64_t		notify_idx;
};

/*
 * Struct used for unlinking a doorbell handle from an index in the notify
 * bitmap. All fields in struct are aligned to their natural alignment.
 */
struct vmci_doorbell_unlink_msg {
	struct vmci_datagram	hdr;
	struct vmci_handle	handle;
};

/*
 * Struct used for generating a notification on a doorbell handle. All fields
 * in struct are aligned to their natural alignment.
 */
struct vmci_doorbell_notify_msg {
	struct vmci_datagram	hdr;
	struct vmci_handle	handle;
};

/*
 * This struct is used to contain data for events. Size of this struct is a
 * multiple of 8 bytes, and all fields are aligned to their natural alignment.
 */
struct vmci_event_data {
	vmci_event_type		event;	/* 4 bytes. */
	uint32_t		_pad;
	/*
	 * Event payload is put here.
	 */
};

/* Callback needed for correctly waiting on events. */

typedef int
(*vmci_datagram_recv_cb)(void *client_data, struct vmci_datagram *msg);

/*
 * We use the following inline function to access the payload data associated
 * with an event data.
 */

static inline void *
vmci_event_data_payload(struct vmci_event_data *ev_data)
{

	return ((void *)((char *)ev_data + sizeof(*ev_data)));
}

/*
 * Define the different VMCI_EVENT payload data types here.  All structs must
 * be a multiple of 8 bytes, and fields must be aligned to their natural
 * alignment.
 */
struct vmci_event_payload_context {
	vmci_id			context_id;	/* 4 bytes. */
	uint32_t		_pad;
};

struct vmci_event_payload_qp {
	/* QueuePair handle. */
	struct vmci_handle	handle;
	/* Context id of attaching/detaching VM. */
	vmci_id			peer_id;
	uint32_t		_pad;
};

/*
 * We define the following struct to get the size of the maximum event data
 * the hypervisor may send to the guest. If adding a new event payload type
 * above, add it to the following struct too (inside the union).
 */
struct vmci_event_data_max {
	struct vmci_event_data	event_data;
	union {
		struct vmci_event_payload_context	context_payload;
		struct vmci_event_payload_qp		qp_payload;
	} ev_data_payload;
};

/*
 * Struct used for VMCI_EVENT_SUBSCRIBE/UNSUBSCRIBE and VMCI_EVENT_HANDLER
 * messages. Struct size is 32 bytes. All fields in struct are aligned to
 * their natural alignment.
 */
struct vmci_event_msg {
	struct vmci_datagram	hdr;
	struct vmci_event_data	event_data;	/* Has event type & payload. */
	/*
	 * Payload gets put here.
	 */
};

/*
 * We use the following inline function to access the payload data associated
 * with an event message.
 */

static inline void *
vmci_event_msg_payload(struct vmci_event_msg *e_msg)
{

	return (vmci_event_data_payload(&e_msg->event_data));
}

/* Flags for VMCI QueuePair API. */
#define VMCI_QPFLAG_ATTACH_ONLY						\
	0x1	/* Fail alloc if QP not created by peer. */
#define VMCI_QPFLAG_LOCAL						\
	0x2	/* Only allow attaches from local context. */
#define VMCI_QPFLAG_NONBLOCK						\
	0x4	/* Host won't block when guest is quiesced. */

/* For asymmetric queuepairs, update as new flags are added. */
#define VMCI_QP_ASYMM							\
	VMCI_QPFLAG_NONBLOCK
#define VMCI_QP_ASYMM_PEER						\
	(VMCI_QPFLAG_ATTACH_ONLY | VMCI_QP_ASYMM)

/* Update the following (bitwise OR flags) while adding new flags. */
#define VMCI_QP_ALL_FLAGS						\
	(VMCI_QPFLAG_ATTACH_ONLY | VMCI_QPFLAG_LOCAL | VMCI_QPFLAG_NONBLOCK)

/*
 * Structs used for QueuePair alloc and detach messages. We align fields of
 * these structs to 64 bit boundaries.
 */
struct vmci_queue_pair_alloc_msg {
	struct vmci_datagram	hdr;
	struct vmci_handle	handle;
	vmci_id			peer;		/* 32bit field. */
	uint32_t		flags;
	uint64_t		produce_size;
	uint64_t		consume_size;
	uint64_t		num_ppns;
	/* List of PPNs placed here. */
};

struct vmci_queue_pair_detach_msg {
	struct vmci_datagram	hdr;
	struct vmci_handle	handle;
};

#endif /* !_VMCI_CALL_DEFS_H_ */
