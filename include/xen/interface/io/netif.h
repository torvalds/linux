/******************************************************************************
 * netif.h
 *
 * Unified network-device I/O interface for Xen guest OSes.
 *
 * Copyright (c) 2003-2004, Keir Fraser
 */

#ifndef __XEN_PUBLIC_IO_NETIF_H__
#define __XEN_PUBLIC_IO_NETIF_H__

#include <xen/interface/io/ring.h>
#include <xen/interface/grant_table.h>

/*
 * Older implementation of Xen network frontend / backend has an
 * implicit dependency on the MAX_SKB_FRAGS as the maximum number of
 * ring slots a skb can use. Netfront / netback may not work as
 * expected when frontend and backend have different MAX_SKB_FRAGS.
 *
 * A better approach is to add mechanism for netfront / netback to
 * negotiate this value. However we cannot fix all possible
 * frontends, so we need to define a value which states the minimum
 * slots backend must support.
 *
 * The minimum value derives from older Linux kernel's MAX_SKB_FRAGS
 * (18), which is proved to work with most frontends. Any new backend
 * which doesn't negotiate with frontend should expect frontend to
 * send a valid packet using slots up to this value.
 */
#define XEN_NETIF_NR_SLOTS_MIN 18

/*
 * Notifications after enqueuing any type of message should be conditional on
 * the appropriate req_event or rsp_event field in the shared ring.
 * If the client sends notification for rx requests then it should specify
 * feature 'feature-rx-notify' via xenbus. Otherwise the backend will assume
 * that it cannot safely queue packets (as it may not be kicked to send them).
 */

 /*
 * "feature-split-event-channels" is introduced to separate guest TX
 * and RX notificaion. Backend either doesn't support this feature or
 * advertise it via xenstore as 0 (disabled) or 1 (enabled).
 *
 * To make use of this feature, frontend should allocate two event
 * channels for TX and RX, advertise them to backend as
 * "event-channel-tx" and "event-channel-rx" respectively. If frontend
 * doesn't want to use this feature, it just writes "event-channel"
 * node as before.
 */

/*
 * Multiple transmit and receive queues:
 * If supported, the backend will write the key "multi-queue-max-queues" to
 * the directory for that vif, and set its value to the maximum supported
 * number of queues.
 * Frontends that are aware of this feature and wish to use it can write the
 * key "multi-queue-num-queues", set to the number they wish to use, which
 * must be greater than zero, and no more than the value reported by the backend
 * in "multi-queue-max-queues".
 *
 * Queues replicate the shared rings and event channels.
 * "feature-split-event-channels" may optionally be used when using
 * multiple queues, but is not mandatory.
 *
 * Each queue consists of one shared ring pair, i.e. there must be the same
 * number of tx and rx rings.
 *
 * For frontends requesting just one queue, the usual event-channel and
 * ring-ref keys are written as before, simplifying the backend processing
 * to avoid distinguishing between a frontend that doesn't understand the
 * multi-queue feature, and one that does, but requested only one queue.
 *
 * Frontends requesting two or more queues must not write the toplevel
 * event-channel (or event-channel-{tx,rx}) and {tx,rx}-ring-ref keys,
 * instead writing those keys under sub-keys having the name "queue-N" where
 * N is the integer ID of the queue for which those keys belong. Queues
 * are indexed from zero. For example, a frontend with two queues and split
 * event channels must write the following set of queue-related keys:
 *
 * /local/domain/1/device/vif/0/multi-queue-num-queues = "2"
 * /local/domain/1/device/vif/0/queue-0 = ""
 * /local/domain/1/device/vif/0/queue-0/tx-ring-ref = "<ring-ref-tx0>"
 * /local/domain/1/device/vif/0/queue-0/rx-ring-ref = "<ring-ref-rx0>"
 * /local/domain/1/device/vif/0/queue-0/event-channel-tx = "<evtchn-tx0>"
 * /local/domain/1/device/vif/0/queue-0/event-channel-rx = "<evtchn-rx0>"
 * /local/domain/1/device/vif/0/queue-1 = ""
 * /local/domain/1/device/vif/0/queue-1/tx-ring-ref = "<ring-ref-tx1>"
 * /local/domain/1/device/vif/0/queue-1/rx-ring-ref = "<ring-ref-rx1"
 * /local/domain/1/device/vif/0/queue-1/event-channel-tx = "<evtchn-tx1>"
 * /local/domain/1/device/vif/0/queue-1/event-channel-rx = "<evtchn-rx1>"
 *
 * If there is any inconsistency in the XenStore data, the backend may
 * choose not to connect any queues, instead treating the request as an
 * error. This includes scenarios where more (or fewer) queues were
 * requested than the frontend provided details for.
 *
 * Mapping of packets to queues is considered to be a function of the
 * transmitting system (backend or frontend) and is not negotiated
 * between the two. Guests are free to transmit packets on any queue
 * they choose, provided it has been set up correctly. Guests must be
 * prepared to receive packets on any queue they have requested be set up.
 */

/*
 * "feature-no-csum-offload" should be used to turn IPv4 TCP/UDP checksum
 * offload off or on. If it is missing then the feature is assumed to be on.
 * "feature-ipv6-csum-offload" should be used to turn IPv6 TCP/UDP checksum
 * offload on or off. If it is missing then the feature is assumed to be off.
 */

/*
 * "feature-gso-tcpv4" and "feature-gso-tcpv6" advertise the capability to
 * handle large TCP packets (in IPv4 or IPv6 form respectively). Neither
 * frontends nor backends are assumed to be capable unless the flags are
 * present.
 */

/*
 * This is the 'wire' format for packets:
 *  Request 1: xen_netif_tx_request  -- XEN_NETTXF_* (any flags)
 * [Request 2: xen_netif_extra_info]    (only if request 1 has XEN_NETTXF_extra_info)
 * [Request 3: xen_netif_extra_info]    (only if request 2 has XEN_NETIF_EXTRA_MORE)
 *  Request 4: xen_netif_tx_request  -- XEN_NETTXF_more_data
 *  Request 5: xen_netif_tx_request  -- XEN_NETTXF_more_data
 *  ...
 *  Request N: xen_netif_tx_request  -- 0
 */

/* Protocol checksum field is blank in the packet (hardware offload)? */
#define _XEN_NETTXF_csum_blank		(0)
#define  XEN_NETTXF_csum_blank		(1U<<_XEN_NETTXF_csum_blank)

/* Packet data has been validated against protocol checksum. */
#define _XEN_NETTXF_data_validated	(1)
#define  XEN_NETTXF_data_validated	(1U<<_XEN_NETTXF_data_validated)

/* Packet continues in the next request descriptor. */
#define _XEN_NETTXF_more_data		(2)
#define  XEN_NETTXF_more_data		(1U<<_XEN_NETTXF_more_data)

/* Packet to be followed by extra descriptor(s). */
#define _XEN_NETTXF_extra_info		(3)
#define  XEN_NETTXF_extra_info		(1U<<_XEN_NETTXF_extra_info)

#define XEN_NETIF_MAX_TX_SIZE 0xFFFF
struct xen_netif_tx_request {
    grant_ref_t gref;      /* Reference to buffer page */
    uint16_t offset;       /* Offset within buffer page */
    uint16_t flags;        /* XEN_NETTXF_* */
    uint16_t id;           /* Echoed in response message. */
    uint16_t size;         /* Packet size in bytes.       */
};

/* Types of xen_netif_extra_info descriptors. */
#define XEN_NETIF_EXTRA_TYPE_NONE	(0)  /* Never used - invalid */
#define XEN_NETIF_EXTRA_TYPE_GSO	(1)  /* u.gso */
#define XEN_NETIF_EXTRA_TYPE_MAX	(2)

/* xen_netif_extra_info flags. */
#define _XEN_NETIF_EXTRA_FLAG_MORE	(0)
#define  XEN_NETIF_EXTRA_FLAG_MORE	(1U<<_XEN_NETIF_EXTRA_FLAG_MORE)

/* GSO types */
#define XEN_NETIF_GSO_TYPE_NONE		(0)
#define XEN_NETIF_GSO_TYPE_TCPV4	(1)
#define XEN_NETIF_GSO_TYPE_TCPV6	(2)

/*
 * This structure needs to fit within both netif_tx_request and
 * netif_rx_response for compatibility.
 */
struct xen_netif_extra_info {
	uint8_t type;  /* XEN_NETIF_EXTRA_TYPE_* */
	uint8_t flags; /* XEN_NETIF_EXTRA_FLAG_* */

	union {
		struct {
			/*
			 * Maximum payload size of each segment. For
			 * example, for TCP this is just the path MSS.
			 */
			uint16_t size;

			/*
			 * GSO type. This determines the protocol of
			 * the packet and any extra features required
			 * to segment the packet properly.
			 */
			uint8_t type; /* XEN_NETIF_GSO_TYPE_* */

			/* Future expansion. */
			uint8_t pad;

			/*
			 * GSO features. This specifies any extra GSO
			 * features required to process this packet,
			 * such as ECN support for TCPv4.
			 */
			uint16_t features; /* XEN_NETIF_GSO_FEAT_* */
		} gso;

		uint16_t pad[3];
	} u;
};

struct xen_netif_tx_response {
	uint16_t id;
	int16_t  status;       /* XEN_NETIF_RSP_* */
};

struct xen_netif_rx_request {
	uint16_t    id;        /* Echoed in response message.        */
	grant_ref_t gref;      /* Reference to incoming granted frame */
};

/* Packet data has been validated against protocol checksum. */
#define _XEN_NETRXF_data_validated	(0)
#define  XEN_NETRXF_data_validated	(1U<<_XEN_NETRXF_data_validated)

/* Protocol checksum field is blank in the packet (hardware offload)? */
#define _XEN_NETRXF_csum_blank		(1)
#define  XEN_NETRXF_csum_blank		(1U<<_XEN_NETRXF_csum_blank)

/* Packet continues in the next request descriptor. */
#define _XEN_NETRXF_more_data		(2)
#define  XEN_NETRXF_more_data		(1U<<_XEN_NETRXF_more_data)

/* Packet to be followed by extra descriptor(s). */
#define _XEN_NETRXF_extra_info		(3)
#define  XEN_NETRXF_extra_info		(1U<<_XEN_NETRXF_extra_info)

/* GSO Prefix descriptor. */
#define _XEN_NETRXF_gso_prefix		(4)
#define  XEN_NETRXF_gso_prefix		(1U<<_XEN_NETRXF_gso_prefix)

struct xen_netif_rx_response {
    uint16_t id;
    uint16_t offset;       /* Offset in page of start of received packet  */
    uint16_t flags;        /* XEN_NETRXF_* */
    int16_t  status;       /* -ve: BLKIF_RSP_* ; +ve: Rx'ed pkt size. */
};

/*
 * Generate netif ring structures and types.
 */

DEFINE_RING_TYPES(xen_netif_tx,
		  struct xen_netif_tx_request,
		  struct xen_netif_tx_response);
DEFINE_RING_TYPES(xen_netif_rx,
		  struct xen_netif_rx_request,
		  struct xen_netif_rx_response);

#define XEN_NETIF_RSP_DROPPED	-2
#define XEN_NETIF_RSP_ERROR	-1
#define XEN_NETIF_RSP_OKAY	 0
/* No response: used for auxiliary requests (e.g., xen_netif_extra_info). */
#define XEN_NETIF_RSP_NULL	 1

#endif
