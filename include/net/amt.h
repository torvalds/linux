/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2021 Taehee Yoo <ap420073@gmail.com>
 */
#ifndef _NET_AMT_H_
#define _NET_AMT_H_

#include <linux/siphash.h>
#include <linux/jhash.h>

enum amt_msg_type {
	AMT_MSG_DISCOVERY = 1,
	AMT_MSG_ADVERTISEMENT,
	AMT_MSG_REQUEST,
	AMT_MSG_MEMBERSHIP_QUERY,
	AMT_MSG_MEMBERSHIP_UPDATE,
	AMT_MSG_MULTICAST_DATA,
	AMT_MSG_TEARDOWN,
	__AMT_MSG_MAX,
};

#define AMT_MSG_MAX (__AMT_MSG_MAX - 1)

enum amt_ops {
	/* A*B */
	AMT_OPS_INT,
	/* A+B */
	AMT_OPS_UNI,
	/* A-B */
	AMT_OPS_SUB,
	/* B-A */
	AMT_OPS_SUB_REV,
	__AMT_OPS_MAX,
};

#define AMT_OPS_MAX (__AMT_OPS_MAX - 1)

enum amt_filter {
	AMT_FILTER_FWD,
	AMT_FILTER_D_FWD,
	AMT_FILTER_FWD_NEW,
	AMT_FILTER_D_FWD_NEW,
	AMT_FILTER_ALL,
	AMT_FILTER_NONE_NEW,
	AMT_FILTER_BOTH,
	AMT_FILTER_BOTH_NEW,
	__AMT_FILTER_MAX,
};

#define AMT_FILTER_MAX (__AMT_FILTER_MAX - 1)

enum amt_act {
	AMT_ACT_GMI,
	AMT_ACT_GMI_ZERO,
	AMT_ACT_GT,
	AMT_ACT_STATUS_FWD_NEW,
	AMT_ACT_STATUS_D_FWD_NEW,
	AMT_ACT_STATUS_NONE_NEW,
	__AMT_ACT_MAX,
};

#define AMT_ACT_MAX (__AMT_ACT_MAX - 1)

enum amt_status {
	AMT_STATUS_INIT,
	AMT_STATUS_SENT_DISCOVERY,
	AMT_STATUS_RECEIVED_DISCOVERY,
	AMT_STATUS_SENT_ADVERTISEMENT,
	AMT_STATUS_RECEIVED_ADVERTISEMENT,
	AMT_STATUS_SENT_REQUEST,
	AMT_STATUS_RECEIVED_REQUEST,
	AMT_STATUS_SENT_QUERY,
	AMT_STATUS_RECEIVED_QUERY,
	AMT_STATUS_SENT_UPDATE,
	AMT_STATUS_RECEIVED_UPDATE,
	__AMT_STATUS_MAX,
};

#define AMT_STATUS_MAX (__AMT_STATUS_MAX - 1)

/* Gateway events only */
enum amt_event {
	AMT_EVENT_NONE,
	AMT_EVENT_RECEIVE,
	AMT_EVENT_SEND_DISCOVERY,
	AMT_EVENT_SEND_REQUEST,
	__AMT_EVENT_MAX,
};

struct amt_header {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 type:4,
	   version:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u8 version:4,
	   type:4;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
} __packed;

struct amt_header_discovery {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u32	type:4,
		version:4,
		reserved:24;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u32	version:4,
		type:4,
		reserved:24;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
	__be32	nonce;
} __packed;

struct amt_header_advertisement {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u32	type:4,
		version:4,
		reserved:24;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u32	version:4,
		type:4,
		reserved:24;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
	__be32	nonce;
	__be32	ip4;
} __packed;

struct amt_header_request {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u32	type:4,
		version:4,
		reserved1:7,
		p:1,
		reserved2:16;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u32	version:4,
		type:4,
		p:1,
		reserved1:7,
		reserved2:16;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
	__be32	nonce;
} __packed;

struct amt_header_membership_query {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u64	type:4,
		version:4,
		reserved:6,
		l:1,
		g:1,
		response_mac:48;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u64	version:4,
		type:4,
		g:1,
		l:1,
		reserved:6,
		response_mac:48;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
	__be32	nonce;
} __packed;

struct amt_header_membership_update {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u64	type:4,
		version:4,
		reserved:8,
		response_mac:48;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u64	version:4,
		type:4,
		reserved:8,
		response_mac:48;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
	__be32	nonce;
} __packed;

struct amt_header_mcast_data {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u16	type:4,
		version:4,
		reserved:8;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u16	version:4,
		type:4,
		reserved:8;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
} __packed;

struct amt_headers {
	union {
		struct amt_header_discovery discovery;
		struct amt_header_advertisement advertisement;
		struct amt_header_request request;
		struct amt_header_membership_query query;
		struct amt_header_membership_update update;
		struct amt_header_mcast_data data;
	};
} __packed;

struct amt_gw_headers {
	union {
		struct amt_header_discovery discovery;
		struct amt_header_request request;
		struct amt_header_membership_update update;
	};
} __packed;

struct amt_relay_headers {
	union {
		struct amt_header_advertisement advertisement;
		struct amt_header_membership_query query;
		struct amt_header_mcast_data data;
	};
} __packed;

struct amt_skb_cb {
	struct amt_tunnel_list *tunnel;
};

struct amt_tunnel_list {
	struct list_head	list;
	/* Protect All resources under an amt_tunne_list */
	spinlock_t		lock;
	struct amt_dev		*amt;
	u32			nr_groups;
	u32			nr_sources;
	enum amt_status		status;
	struct delayed_work	gc_wq;
	__be16			source_port;
	__be32			ip4;
	__be32			nonce;
	siphash_key_t		key;
	u64			mac:48,
				reserved:16;
	struct rcu_head		rcu;
	struct hlist_head	groups[];
};

union amt_addr {
	__be32			ip4;
#if IS_ENABLED(CONFIG_IPV6)
	struct in6_addr		ip6;
#endif
};

/* RFC 3810
 *
 * When the router is in EXCLUDE mode, the router state is represented
 * by the notation EXCLUDE (X,Y), where X is called the "Requested List"
 * and Y is called the "Exclude List".  All sources, except those from
 * the Exclude List, will be forwarded by the router
 */
enum amt_source_status {
	AMT_SOURCE_STATUS_NONE,
	/* Node of Requested List */
	AMT_SOURCE_STATUS_FWD,
	/* Node of Exclude List */
	AMT_SOURCE_STATUS_D_FWD,
};

/* protected by gnode->lock */
struct amt_source_node {
	struct hlist_node	node;
	struct amt_group_node	*gnode;
	struct delayed_work     source_timer;
	union amt_addr		source_addr;
	enum amt_source_status	status;
#define AMT_SOURCE_OLD	0
#define AMT_SOURCE_NEW	1
	u8			flags;
	struct rcu_head		rcu;
};

/* Protected by amt_tunnel_list->lock */
struct amt_group_node {
	struct amt_dev		*amt;
	union amt_addr		group_addr;
	union amt_addr		host_addr;
	bool			v6;
	u8			filter_mode;
	u32			nr_sources;
	struct amt_tunnel_list	*tunnel_list;
	struct hlist_node	node;
	struct delayed_work     group_timer;
	struct rcu_head		rcu;
	struct hlist_head	sources[];
};

#define AMT_MAX_EVENTS	16
struct amt_events {
	enum amt_event event;
	struct sk_buff *skb;
};

struct amt_dev {
	struct net_device       *dev;
	struct net_device       *stream_dev;
	struct net		*net;
	/* Global lock for amt device */
	spinlock_t		lock;
	/* Used only in relay mode */
	struct list_head        tunnel_list;
	struct gro_cells	gro_cells;

	/* Protected by RTNL */
	struct delayed_work     discovery_wq;
	/* Protected by RTNL */
	struct delayed_work     req_wq;
	/* Protected by RTNL */
	struct delayed_work     secret_wq;
	struct work_struct	event_wq;
	/* AMT status */
	enum amt_status		status;
	/* Generated key */
	siphash_key_t		key;
	struct socket	  __rcu *sock;
	u32			max_groups;
	u32			max_sources;
	u32			hash_buckets;
	u32			hash_seed;
	/* Default 128 */
	u32                     max_tunnels;
	/* Default 128 */
	u32                     nr_tunnels;
	/* Gateway or Relay mode */
	u32                     mode;
	/* Default 2268 */
	__be16			relay_port;
	/* Default 2268 */
	__be16			gw_port;
	/* Outer local ip */
	__be32			local_ip;
	/* Outer remote ip */
	__be32			remote_ip;
	/* Outer discovery ip */
	__be32			discovery_ip;
	/* Only used in gateway mode */
	__be32			nonce;
	/* Gateway sent request and received query */
	bool			ready4;
	bool			ready6;
	u8			req_cnt;
	u8			qi;
	u64			qrv;
	u64			qri;
	/* Used only in gateway mode */
	u64			mac:48,
				reserved:16;
	/* AMT gateway side message handler queue */
	struct amt_events	events[AMT_MAX_EVENTS];
	u8			event_idx;
	u8			nr_events;
};

#define AMT_TOS			0xc0
#define AMT_IPHDR_OPTS		4
#define AMT_IP6HDR_OPTS		8
#define AMT_GC_INTERVAL		(30 * 1000)
#define AMT_MAX_GROUP		32
#define AMT_MAX_SOURCE		128
#define AMT_HSIZE_SHIFT		8
#define AMT_HSIZE		(1 << AMT_HSIZE_SHIFT)

#define AMT_DISCOVERY_TIMEOUT	5000
#define AMT_INIT_REQ_TIMEOUT	1
#define AMT_INIT_QUERY_INTERVAL	125
#define AMT_MAX_REQ_TIMEOUT	120
#define AMT_MAX_REQ_COUNT	3
#define AMT_SECRET_TIMEOUT	60000
#define IANA_AMT_UDP_PORT	2268
#define AMT_MAX_TUNNELS         128
#define AMT_MAX_REQS		128
#define AMT_GW_HLEN (sizeof(struct iphdr) + \
		     sizeof(struct udphdr) + \
		     sizeof(struct amt_gw_headers))
#define AMT_RELAY_HLEN (sizeof(struct iphdr) + \
		     sizeof(struct udphdr) + \
		     sizeof(struct amt_relay_headers))

static inline bool netif_is_amt(const struct net_device *dev)
{
	return dev->rtnl_link_ops && !strcmp(dev->rtnl_link_ops->kind, "amt");
}

static inline u64 amt_gmi(const struct amt_dev *amt)
{
	return ((amt->qrv * amt->qi) + amt->qri) * 1000;
}

#endif /* _NET_AMT_H_ */
