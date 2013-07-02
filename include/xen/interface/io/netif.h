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

/* GSO types - only TCPv4 currently supported. */
#define XEN_NETIF_GSO_TYPE_TCPV4	(1)

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
