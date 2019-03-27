/******************************************************************************
 * netif.h
 * 
 * Unified network-device I/O interface for Xen guest OSes.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2003-2004, Keir Fraser
 */

#ifndef __XEN_PUBLIC_IO_NETIF_H__
#define __XEN_PUBLIC_IO_NETIF_H__

#include "ring.h"
#include "../grant_table.h"

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
 * and RX notification. Backend either doesn't support this feature or
 * advertises it via xenstore as 0 (disabled) or 1 (enabled).
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
 * "feature-multicast-control" advertises the capability to filter ethernet
 * multicast packets in the backend. To enable use of this capability the
 * frontend must set "request-multicast-control" before moving into the
 * connected state.
 *
 * If "request-multicast-control" is set then the backend transmit side should
 * no longer flood multicast packets to the frontend, it should instead drop any
 * multicast packet that does not match in a filter list. The list is
 * amended by the frontend by sending dummy transmit requests containing
 * XEN_NETIF_EXTRA_TYPE_MCAST_{ADD,DEL} extra-info fragments as specified below.
 * Once enabled by the frontend, the feature cannot be disabled except by
 * closing and re-connecting to the backend.
 */

/*
 * This is the 'wire' format for packets:
 *  Request 1: netif_tx_request_t -- NETTXF_* (any flags)
 * [Request 2: netif_extra_info_t] (only if request 1 has NETTXF_extra_info)
 * [Request 3: netif_extra_info_t] (only if request 2 has XEN_NETIF_EXTRA_MORE)
 *  Request 4: netif_tx_request_t -- NETTXF_more_data
 *  Request 5: netif_tx_request_t -- NETTXF_more_data
 *  ...
 *  Request N: netif_tx_request_t -- 0
 */

/*
 * Guest transmit
 * ==============
 *
 * Ring slot size is 12 octets, however not all request/response
 * structs use the full size.
 *
 * tx request data (netif_tx_request_t)
 * ------------------------------------
 *
 *    0     1     2     3     4     5     6     7  octet
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | grant ref             | offset    | flags     |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | id        | size      |
 * +-----+-----+-----+-----+
 *
 * grant ref: Reference to buffer page.
 * offset: Offset within buffer page.
 * flags: NETTXF_*.
 * id: request identifier, echoed in response.
 * size: packet size in bytes.
 *
 * tx response (netif_tx_response_t)
 * ---------------------------------
 *
 *    0     1     2     3     4     5     6     7  octet
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | id        | status    | unused                |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | unused                |
 * +-----+-----+-----+-----+
 *
 * id: reflects id in transmit request
 * status: NETIF_RSP_*
 *
 * Guest receive
 * =============
 *
 * Ring slot size is 8 octets.
 *
 * rx request (netif_rx_request_t)
 * -------------------------------
 *
 *    0     1     2     3     4     5     6     7  octet
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | id        | pad       | gref                  |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 *
 * id: request identifier, echoed in response.
 * gref: reference to incoming granted frame.
 *
 * rx response (netif_rx_response_t)
 * ---------------------------------
 *
 *    0     1     2     3     4     5     6     7  octet
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | id        | offset    | flags     | status    |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 *
 * id: reflects id in receive request
 * offset: offset in page of start of received packet
 * flags: NETRXF_*
 * status: -ve: NETIF_RSP_*; +ve: Rx'ed pkt size.
 *
 * Extra Info
 * ==========
 *
 * Can be present if initial request has NET{T,R}XF_extra_info, or
 * previous extra request has XEN_NETIF_EXTRA_MORE.
 *
 * The struct therefore needs to fit into either a tx or rx slot and
 * is therefore limited to 8 octets.
 *
 * extra info (netif_extra_info_t)
 * -------------------------------
 *
 * General format:
 *
 *    0     1     2     3     4     5     6     7  octet
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * |type |flags| type specfic data                 |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | padding for tx        |
 * +-----+-----+-----+-----+
 *
 * type: XEN_NETIF_EXTRA_TYPE_*
 * flags: XEN_NETIF_EXTRA_FLAG_*
 * padding for tx: present only in the tx case due to 8 octet limit
 *     from rx case. Not shown in type specific entries below.
 *
 * XEN_NETIF_EXTRA_TYPE_GSO:
 *
 *    0     1     2     3     4     5     6     7  octet
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * |type |flags| size      |type | pad | features  |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 *
 * type: Must be XEN_NETIF_EXTRA_TYPE_GSO
 * flags: XEN_NETIF_EXTRA_FLAG_*
 * size: Maximum payload size of each segment.
 * type: XEN_NETIF_GSO_TYPE_*
 * features: EN_NETIF_GSO_FEAT_*
 *
 * XEN_NETIF_EXTRA_TYPE_MCAST_{ADD,DEL}:
 *
 *    0     1     2     3     4     5     6     7  octet
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * |type |flags| addr                              |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 *
 * type: Must be XEN_NETIF_EXTRA_TYPE_MCAST_{ADD,DEL}
 * flags: XEN_NETIF_EXTRA_FLAG_*
 * addr: address to add/remove
 */

/* Protocol checksum field is blank in the packet (hardware offload)? */
#define _NETTXF_csum_blank     (0)
#define  NETTXF_csum_blank     (1U<<_NETTXF_csum_blank)

/* Packet data has been validated against protocol checksum. */
#define _NETTXF_data_validated (1)
#define  NETTXF_data_validated (1U<<_NETTXF_data_validated)

/* Packet continues in the next request descriptor. */
#define _NETTXF_more_data      (2)
#define  NETTXF_more_data      (1U<<_NETTXF_more_data)

/* Packet to be followed by extra descriptor(s). */
#define _NETTXF_extra_info     (3)
#define  NETTXF_extra_info     (1U<<_NETTXF_extra_info)

#define XEN_NETIF_MAX_TX_SIZE 0xFFFF
struct netif_tx_request {
    grant_ref_t gref;      /* Reference to buffer page */
    uint16_t offset;       /* Offset within buffer page */
    uint16_t flags;        /* NETTXF_* */
    uint16_t id;           /* Echoed in response message. */
    uint16_t size;         /* Packet size in bytes.       */
};
typedef struct netif_tx_request netif_tx_request_t;

/* Types of netif_extra_info descriptors. */
#define XEN_NETIF_EXTRA_TYPE_NONE      (0)  /* Never used - invalid */
#define XEN_NETIF_EXTRA_TYPE_GSO       (1)  /* u.gso */
#define XEN_NETIF_EXTRA_TYPE_MCAST_ADD (2)  /* u.mcast */
#define XEN_NETIF_EXTRA_TYPE_MCAST_DEL (3)  /* u.mcast */
#define XEN_NETIF_EXTRA_TYPE_MAX       (4)

/* netif_extra_info_t flags. */
#define _XEN_NETIF_EXTRA_FLAG_MORE (0)
#define XEN_NETIF_EXTRA_FLAG_MORE  (1U<<_XEN_NETIF_EXTRA_FLAG_MORE)

/* GSO types */
#define XEN_NETIF_GSO_TYPE_NONE         (0)
#define XEN_NETIF_GSO_TYPE_TCPV4        (1)
#define XEN_NETIF_GSO_TYPE_TCPV6        (2)

/*
 * This structure needs to fit within both netif_tx_request_t and
 * netif_rx_response_t for compatibility.
 */
struct netif_extra_info {
    uint8_t type;  /* XEN_NETIF_EXTRA_TYPE_* */
    uint8_t flags; /* XEN_NETIF_EXTRA_FLAG_* */

    union {
        /*
         * XEN_NETIF_EXTRA_TYPE_GSO:
         */
        struct {
            /*
             * Maximum payload size of each segment. For example, for TCP this
             * is just the path MSS.
             */
            uint16_t size;

            /*
             * GSO type. This determines the protocol of the packet and any
             * extra features required to segment the packet properly.
             */
            uint8_t type; /* XEN_NETIF_GSO_TYPE_* */

            /* Future expansion. */
            uint8_t pad;

            /*
             * GSO features. This specifies any extra GSO features required
             * to process this packet, such as ECN support for TCPv4.
             */
            uint16_t features; /* XEN_NETIF_GSO_FEAT_* */
        } gso;

        /*
         * XEN_NETIF_EXTRA_TYPE_MCAST_{ADD,DEL}:
         */
        struct {
            uint8_t addr[6]; /* Address to add/remove. */
        } mcast;

        uint16_t pad[3];
    } u;
};
typedef struct netif_extra_info netif_extra_info_t;

struct netif_tx_response {
    uint16_t id;
    int16_t  status;       /* NETIF_RSP_* */
};
typedef struct netif_tx_response netif_tx_response_t;

struct netif_rx_request {
    uint16_t    id;        /* Echoed in response message.        */
    uint16_t    pad;
    grant_ref_t gref;      /* Reference to incoming granted frame */
};
typedef struct netif_rx_request netif_rx_request_t;

/* Packet data has been validated against protocol checksum. */
#define _NETRXF_data_validated (0)
#define  NETRXF_data_validated (1U<<_NETRXF_data_validated)

/* Protocol checksum field is blank in the packet (hardware offload)? */
#define _NETRXF_csum_blank     (1)
#define  NETRXF_csum_blank     (1U<<_NETRXF_csum_blank)

/* Packet continues in the next request descriptor. */
#define _NETRXF_more_data      (2)
#define  NETRXF_more_data      (1U<<_NETRXF_more_data)

/* Packet to be followed by extra descriptor(s). */
#define _NETRXF_extra_info     (3)
#define  NETRXF_extra_info     (1U<<_NETRXF_extra_info)

struct netif_rx_response {
    uint16_t id;
    uint16_t offset;       /* Offset in page of start of received packet  */
    uint16_t flags;        /* NETRXF_* */
    int16_t  status;       /* -ve: NETIF_RSP_* ; +ve: Rx'ed pkt size. */
};
typedef struct netif_rx_response netif_rx_response_t;

/*
 * Generate netif ring structures and types.
 */

DEFINE_RING_TYPES(netif_tx, struct netif_tx_request, struct netif_tx_response);
DEFINE_RING_TYPES(netif_rx, struct netif_rx_request, struct netif_rx_response);

#define NETIF_RSP_DROPPED         -2
#define NETIF_RSP_ERROR           -1
#define NETIF_RSP_OKAY             0
/* No response: used for auxiliary requests (e.g., netif_extra_info_t). */
#define NETIF_RSP_NULL             1

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
