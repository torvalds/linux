/* AF_RXRPC internal definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/atomic.h>
#include <linux/seqlock.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "protocol.h"

#if 0
#define CHECK_SLAB_OKAY(X)				     \
	BUG_ON(atomic_read((X)) >> (sizeof(atomic_t) - 2) == \
	       (POISON_FREE << 8 | POISON_FREE))
#else
#define CHECK_SLAB_OKAY(X) do {} while (0)
#endif

#define FCRYPT_BSIZE 8
struct rxrpc_crypt {
	union {
		u8	x[FCRYPT_BSIZE];
		__be32	n[2];
	};
} __attribute__((aligned(8)));

#define rxrpc_queue_work(WS)	queue_work(rxrpc_workqueue, (WS))
#define rxrpc_queue_delayed_work(WS,D)	\
	queue_delayed_work(rxrpc_workqueue, (WS), (D))

struct rxrpc_connection;

/*
 * Mark applied to socket buffers in skb->mark.  skb->priority is used
 * to pass supplementary information.
 */
enum rxrpc_skb_mark {
	RXRPC_SKB_MARK_REJECT_BUSY,	/* Reject with BUSY */
	RXRPC_SKB_MARK_REJECT_ABORT,	/* Reject with ABORT (code in skb->priority) */
};

/*
 * sk_state for RxRPC sockets
 */
enum {
	RXRPC_UNBOUND = 0,
	RXRPC_CLIENT_UNBOUND,		/* Unbound socket used as client */
	RXRPC_CLIENT_BOUND,		/* client local address bound */
	RXRPC_SERVER_BOUND,		/* server local address bound */
	RXRPC_SERVER_BOUND2,		/* second server local address bound */
	RXRPC_SERVER_LISTENING,		/* server listening for connections */
	RXRPC_SERVER_LISTEN_DISABLED,	/* server listening disabled */
	RXRPC_CLOSE,			/* socket is being closed */
};

/*
 * Per-network namespace data.
 */
struct rxrpc_net {
	struct proc_dir_entry	*proc_net;	/* Subdir in /proc/net */
	u32			epoch;		/* Local epoch for detecting local-end reset */
	struct list_head	calls;		/* List of calls active in this namespace */
	rwlock_t		call_lock;	/* Lock for ->calls */
	atomic_t		nr_calls;	/* Count of allocated calls */

	atomic_t		nr_conns;
	struct list_head	conn_proc_list;	/* List of conns in this namespace for proc */
	struct list_head	service_conns;	/* Service conns in this namespace */
	rwlock_t		conn_lock;	/* Lock for ->conn_proc_list, ->service_conns */
	struct work_struct	service_conn_reaper;
	struct timer_list	service_conn_reap_timer;

	unsigned int		nr_client_conns;
	unsigned int		nr_active_client_conns;
	bool			kill_all_client_conns;
	bool			live;
	spinlock_t		client_conn_cache_lock; /* Lock for ->*_client_conns */
	spinlock_t		client_conn_discard_lock; /* Prevent multiple discarders */
	struct list_head	waiting_client_conns;
	struct list_head	active_client_conns;
	struct list_head	idle_client_conns;
	struct work_struct	client_conn_reaper;
	struct timer_list	client_conn_reap_timer;

	struct list_head	local_endpoints;
	struct mutex		local_mutex;	/* Lock for ->local_endpoints */

	DECLARE_HASHTABLE	(peer_hash, 10);
	spinlock_t		peer_hash_lock;	/* Lock for ->peer_hash */

#define RXRPC_KEEPALIVE_TIME 20 /* NAT keepalive time in seconds */
	u8			peer_keepalive_cursor;
	time64_t		peer_keepalive_base;
	struct list_head	peer_keepalive[32];
	struct list_head	peer_keepalive_new;
	struct timer_list	peer_keepalive_timer;
	struct work_struct	peer_keepalive_work;
};

/*
 * Service backlog preallocation.
 *
 * This contains circular buffers of preallocated peers, connections and calls
 * for incoming service calls and their head and tail pointers.  This allows
 * calls to be set up in the data_ready handler, thereby avoiding the need to
 * shuffle packets around so much.
 */
struct rxrpc_backlog {
	unsigned short		peer_backlog_head;
	unsigned short		peer_backlog_tail;
	unsigned short		conn_backlog_head;
	unsigned short		conn_backlog_tail;
	unsigned short		call_backlog_head;
	unsigned short		call_backlog_tail;
#define RXRPC_BACKLOG_MAX	32
	struct rxrpc_peer	*peer_backlog[RXRPC_BACKLOG_MAX];
	struct rxrpc_connection	*conn_backlog[RXRPC_BACKLOG_MAX];
	struct rxrpc_call	*call_backlog[RXRPC_BACKLOG_MAX];
};

/*
 * RxRPC socket definition
 */
struct rxrpc_sock {
	/* WARNING: sk has to be the first member */
	struct sock		sk;
	rxrpc_notify_new_call_t	notify_new_call; /* Func to notify of new call */
	rxrpc_discard_new_call_t discard_new_call; /* Func to discard a new call */
	struct rxrpc_local	*local;		/* local endpoint */
	struct rxrpc_backlog	*backlog;	/* Preallocation for services */
	spinlock_t		incoming_lock;	/* Incoming call vs service shutdown lock */
	struct list_head	sock_calls;	/* List of calls owned by this socket */
	struct list_head	to_be_accepted;	/* calls awaiting acceptance */
	struct list_head	recvmsg_q;	/* Calls awaiting recvmsg's attention  */
	rwlock_t		recvmsg_lock;	/* Lock for recvmsg_q */
	struct key		*key;		/* security for this socket */
	struct key		*securities;	/* list of server security descriptors */
	struct rb_root		calls;		/* User ID -> call mapping */
	unsigned long		flags;
#define RXRPC_SOCK_CONNECTED		0	/* connect_srx is set */
	rwlock_t		call_lock;	/* lock for calls */
	u32			min_sec_level;	/* minimum security level */
#define RXRPC_SECURITY_MAX	RXRPC_SECURITY_ENCRYPT
	bool			exclusive;	/* Exclusive connection for a client socket */
	u16			second_service;	/* Additional service bound to the endpoint */
	struct {
		/* Service upgrade information */
		u16		from;		/* Service ID to upgrade (if not 0) */
		u16		to;		/* service ID to upgrade to */
	} service_upgrade;
	sa_family_t		family;		/* Protocol family created with */
	struct sockaddr_rxrpc	srx;		/* Primary Service/local addresses */
	struct sockaddr_rxrpc	connect_srx;	/* Default client address from connect() */
};

#define rxrpc_sk(__sk) container_of((__sk), struct rxrpc_sock, sk)

/*
 * CPU-byteorder normalised Rx packet header.
 */
struct rxrpc_host_header {
	u32		epoch;		/* client boot timestamp */
	u32		cid;		/* connection and channel ID */
	u32		callNumber;	/* call ID (0 for connection-level packets) */
	u32		seq;		/* sequence number of pkt in call stream */
	u32		serial;		/* serial number of pkt sent to network */
	u8		type;		/* packet type */
	u8		flags;		/* packet flags */
	u8		userStatus;	/* app-layer defined status */
	u8		securityIndex;	/* security protocol ID */
	union {
		u16	_rsvd;		/* reserved */
		u16	cksum;		/* kerberos security checksum */
	};
	u16		serviceId;	/* service ID */
} __packed;

/*
 * RxRPC socket buffer private variables
 * - max 48 bytes (struct sk_buff::cb)
 */
struct rxrpc_skb_priv {
	union {
		u8		nr_jumbo;	/* Number of jumbo subpackets */
	};
	union {
		int		remain;		/* amount of space remaining for next write */
	};

	struct rxrpc_host_header hdr;		/* RxRPC packet header from this packet */
};

#define rxrpc_skb(__skb) ((struct rxrpc_skb_priv *) &(__skb)->cb)

/*
 * RxRPC security module interface
 */
struct rxrpc_security {
	const char		*name;		/* name of this service */
	u8			security_index;	/* security type provided */

	/* Initialise a security service */
	int (*init)(void);

	/* Clean up a security service */
	void (*exit)(void);

	/* initialise a connection's security */
	int (*init_connection_security)(struct rxrpc_connection *);

	/* prime a connection's packet security */
	int (*prime_packet_security)(struct rxrpc_connection *);

	/* impose security on a packet */
	int (*secure_packet)(struct rxrpc_call *,
			     struct sk_buff *,
			     size_t,
			     void *);

	/* verify the security on a received packet */
	int (*verify_packet)(struct rxrpc_call *, struct sk_buff *,
			     unsigned int, unsigned int, rxrpc_seq_t, u16);

	/* Locate the data in a received packet that has been verified. */
	void (*locate_data)(struct rxrpc_call *, struct sk_buff *,
			    unsigned int *, unsigned int *);

	/* issue a challenge */
	int (*issue_challenge)(struct rxrpc_connection *);

	/* respond to a challenge */
	int (*respond_to_challenge)(struct rxrpc_connection *,
				    struct sk_buff *,
				    u32 *);

	/* verify a response */
	int (*verify_response)(struct rxrpc_connection *,
			       struct sk_buff *,
			       u32 *);

	/* clear connection security */
	void (*clear)(struct rxrpc_connection *);
};

/*
 * RxRPC local transport endpoint description
 * - owned by a single AF_RXRPC socket
 * - pointed to by transport socket struct sk_user_data
 */
struct rxrpc_local {
	struct rcu_head		rcu;
	atomic_t		active_users;	/* Number of users of the local endpoint */
	atomic_t		usage;		/* Number of references to the structure */
	struct rxrpc_net	*rxnet;		/* The network ns in which this resides */
	struct list_head	link;
	struct socket		*socket;	/* my UDP socket */
	struct work_struct	processor;
	struct rxrpc_sock __rcu	*service;	/* Service(s) listening on this endpoint */
	struct rw_semaphore	defrag_sem;	/* control re-enablement of IP DF bit */
	struct sk_buff_head	reject_queue;	/* packets awaiting rejection */
	struct sk_buff_head	event_queue;	/* endpoint event packets awaiting processing */
	struct rb_root		client_conns;	/* Client connections by socket params */
	spinlock_t		client_conns_lock; /* Lock for client_conns */
	spinlock_t		lock;		/* access lock */
	rwlock_t		services_lock;	/* lock for services list */
	int			debug_id;	/* debug ID for printks */
	bool			dead;
	bool			service_closed;	/* Service socket closed */
	struct sockaddr_rxrpc	srx;		/* local address */
};

/*
 * RxRPC remote transport endpoint definition
 * - matched by local endpoint, remote port, address and protocol type
 */
struct rxrpc_peer {
	struct rcu_head		rcu;		/* This must be first */
	atomic_t		usage;
	unsigned long		hash_key;
	struct hlist_node	hash_link;
	struct rxrpc_local	*local;
	struct hlist_head	error_targets;	/* targets for net error distribution */
	struct rb_root		service_conns;	/* Service connections */
	struct list_head	keepalive_link;	/* Link in net->peer_keepalive[] */
	time64_t		last_tx_at;	/* Last time packet sent here */
	seqlock_t		service_conn_lock;
	spinlock_t		lock;		/* access lock */
	unsigned int		if_mtu;		/* interface MTU for this peer */
	unsigned int		mtu;		/* network MTU for this peer */
	unsigned int		maxdata;	/* data size (MTU - hdrsize) */
	unsigned short		hdrsize;	/* header size (IP + UDP + RxRPC) */
	int			debug_id;	/* debug ID for printks */
	struct sockaddr_rxrpc	srx;		/* remote address */

	/* calculated RTT cache */
#define RXRPC_RTT_CACHE_SIZE 32
	spinlock_t		rtt_input_lock;	/* RTT lock for input routine */
	ktime_t			rtt_last_req;	/* Time of last RTT request */
	u64			rtt;		/* Current RTT estimate (in nS) */
	u64			rtt_sum;	/* Sum of cache contents */
	u64			rtt_cache[RXRPC_RTT_CACHE_SIZE]; /* Determined RTT cache */
	u8			rtt_cursor;	/* next entry at which to insert */
	u8			rtt_usage;	/* amount of cache actually used */

	u8			cong_cwnd;	/* Congestion window size */
};

/*
 * Keys for matching a connection.
 */
struct rxrpc_conn_proto {
	union {
		struct {
			u32	epoch;		/* epoch of this connection */
			u32	cid;		/* connection ID */
		};
		u64		index_key;
	};
};

struct rxrpc_conn_parameters {
	struct rxrpc_local	*local;		/* Representation of local endpoint */
	struct rxrpc_peer	*peer;		/* Remote endpoint */
	struct key		*key;		/* Security details */
	bool			exclusive;	/* T if conn is exclusive */
	bool			upgrade;	/* T if service ID can be upgraded */
	u16			service_id;	/* Service ID for this connection */
	u32			security_level;	/* Security level selected */
};

/*
 * Bits in the connection flags.
 */
enum rxrpc_conn_flag {
	RXRPC_CONN_HAS_IDR,		/* Has a client conn ID assigned */
	RXRPC_CONN_IN_SERVICE_CONNS,	/* Conn is in peer->service_conns */
	RXRPC_CONN_IN_CLIENT_CONNS,	/* Conn is in local->client_conns */
	RXRPC_CONN_EXPOSED,		/* Conn has extra ref for exposure */
	RXRPC_CONN_DONT_REUSE,		/* Don't reuse this connection */
	RXRPC_CONN_COUNTED,		/* Counted by rxrpc_nr_client_conns */
	RXRPC_CONN_PROBING_FOR_UPGRADE,	/* Probing for service upgrade */
	RXRPC_CONN_FINAL_ACK_0,		/* Need final ACK for channel 0 */
	RXRPC_CONN_FINAL_ACK_1,		/* Need final ACK for channel 1 */
	RXRPC_CONN_FINAL_ACK_2,		/* Need final ACK for channel 2 */
	RXRPC_CONN_FINAL_ACK_3,		/* Need final ACK for channel 3 */
};

#define RXRPC_CONN_FINAL_ACK_MASK ((1UL << RXRPC_CONN_FINAL_ACK_0) |	\
				   (1UL << RXRPC_CONN_FINAL_ACK_1) |	\
				   (1UL << RXRPC_CONN_FINAL_ACK_2) |	\
				   (1UL << RXRPC_CONN_FINAL_ACK_3))

/*
 * Events that can be raised upon a connection.
 */
enum rxrpc_conn_event {
	RXRPC_CONN_EV_CHALLENGE,	/* Send challenge packet */
};

/*
 * The connection cache state.
 */
enum rxrpc_conn_cache_state {
	RXRPC_CONN_CLIENT_INACTIVE,	/* Conn is not yet listed */
	RXRPC_CONN_CLIENT_WAITING,	/* Conn is on wait list, waiting for capacity */
	RXRPC_CONN_CLIENT_ACTIVE,	/* Conn is on active list, doing calls */
	RXRPC_CONN_CLIENT_UPGRADE,	/* Conn is on active list, probing for upgrade */
	RXRPC_CONN_CLIENT_CULLED,	/* Conn is culled and delisted, doing calls */
	RXRPC_CONN_CLIENT_IDLE,		/* Conn is on idle list, doing mostly nothing */
	RXRPC_CONN__NR_CACHE_STATES
};

/*
 * The connection protocol state.
 */
enum rxrpc_conn_proto_state {
	RXRPC_CONN_UNUSED,		/* Connection not yet attempted */
	RXRPC_CONN_CLIENT,		/* Client connection */
	RXRPC_CONN_SERVICE_PREALLOC,	/* Service connection preallocation */
	RXRPC_CONN_SERVICE_UNSECURED,	/* Service unsecured connection */
	RXRPC_CONN_SERVICE_CHALLENGING,	/* Service challenging for security */
	RXRPC_CONN_SERVICE,		/* Service secured connection */
	RXRPC_CONN_REMOTELY_ABORTED,	/* Conn aborted by peer */
	RXRPC_CONN_LOCALLY_ABORTED,	/* Conn aborted locally */
	RXRPC_CONN__NR_STATES
};

/*
 * RxRPC connection definition
 * - matched by { local, peer, epoch, conn_id, direction }
 * - each connection can only handle four simultaneous calls
 */
struct rxrpc_connection {
	struct rxrpc_conn_proto	proto;
	struct rxrpc_conn_parameters params;

	atomic_t		usage;
	struct rcu_head		rcu;
	struct list_head	cache_link;

	spinlock_t		channel_lock;
	unsigned char		active_chans;	/* Mask of active channels */
#define RXRPC_ACTIVE_CHANS_MASK	((1 << RXRPC_MAXCALLS) - 1)
	struct list_head	waiting_calls;	/* Calls waiting for channels */
	struct rxrpc_channel {
		unsigned long		final_ack_at;	/* Time at which to issue final ACK */
		struct rxrpc_call __rcu	*call;		/* Active call */
		unsigned int		call_debug_id;	/* call->debug_id */
		u32			call_id;	/* ID of current call */
		u32			call_counter;	/* Call ID counter */
		u32			last_call;	/* ID of last call */
		u8			last_type;	/* Type of last packet */
		union {
			u32		last_seq;
			u32		last_abort;
		};
	} channels[RXRPC_MAXCALLS];

	struct timer_list	timer;		/* Conn event timer */
	struct work_struct	processor;	/* connection event processor */
	union {
		struct rb_node	client_node;	/* Node in local->client_conns */
		struct rb_node	service_node;	/* Node in peer->service_conns */
	};
	struct list_head	proc_link;	/* link in procfs list */
	struct list_head	link;		/* link in master connection list */
	struct sk_buff_head	rx_queue;	/* received conn-level packets */
	const struct rxrpc_security *security;	/* applied security module */
	struct key		*server_key;	/* security for this service */
	struct crypto_skcipher	*cipher;	/* encryption handle */
	struct rxrpc_crypt	csum_iv;	/* packet checksum base */
	unsigned long		flags;
	unsigned long		events;
	unsigned long		idle_timestamp;	/* Time at which last became idle */
	spinlock_t		state_lock;	/* state-change lock */
	enum rxrpc_conn_cache_state cache_state;
	enum rxrpc_conn_proto_state state;	/* current state of connection */
	u32			abort_code;	/* Abort code of connection abort */
	int			debug_id;	/* debug ID for printks */
	atomic_t		serial;		/* packet serial number counter */
	unsigned int		hi_serial;	/* highest serial number received */
	u32			security_nonce;	/* response re-use preventer */
	u32			service_id;	/* Service ID, possibly upgraded */
	u8			size_align;	/* data size alignment (for security) */
	u8			security_size;	/* security header size */
	u8			security_ix;	/* security type */
	u8			out_clientflag;	/* RXRPC_CLIENT_INITIATED if we are client */
	short			error;		/* Local error code */
};

static inline bool rxrpc_to_server(const struct rxrpc_skb_priv *sp)
{
	return sp->hdr.flags & RXRPC_CLIENT_INITIATED;
}

static inline bool rxrpc_to_client(const struct rxrpc_skb_priv *sp)
{
	return !rxrpc_to_server(sp);
}

/*
 * Flags in call->flags.
 */
enum rxrpc_call_flag {
	RXRPC_CALL_RELEASED,		/* call has been released - no more message to userspace */
	RXRPC_CALL_HAS_USERID,		/* has a user ID attached */
	RXRPC_CALL_IS_SERVICE,		/* Call is service call */
	RXRPC_CALL_EXPOSED,		/* The call was exposed to the world */
	RXRPC_CALL_RX_LAST,		/* Received the last packet (at rxtx_top) */
	RXRPC_CALL_TX_LAST,		/* Last packet in Tx buffer (at rxtx_top) */
	RXRPC_CALL_TX_LASTQ,		/* Last packet has been queued */
	RXRPC_CALL_SEND_PING,		/* A ping will need to be sent */
	RXRPC_CALL_PINGING,		/* Ping in process */
	RXRPC_CALL_RETRANS_TIMEOUT,	/* Retransmission due to timeout occurred */
	RXRPC_CALL_BEGAN_RX_TIMER,	/* We began the expect_rx_by timer */
	RXRPC_CALL_RX_HEARD,		/* The peer responded at least once to this call */
	RXRPC_CALL_RX_UNDERRUN,		/* Got data underrun */
};

/*
 * Events that can be raised on a call.
 */
enum rxrpc_call_event {
	RXRPC_CALL_EV_ACK,		/* need to generate ACK */
	RXRPC_CALL_EV_ABORT,		/* need to generate abort */
	RXRPC_CALL_EV_RESEND,		/* Tx resend required */
	RXRPC_CALL_EV_PING,		/* Ping send required */
	RXRPC_CALL_EV_EXPIRED,		/* Expiry occurred */
	RXRPC_CALL_EV_ACK_LOST,		/* ACK may be lost, send ping */
};

/*
 * The states that a call can be in.
 */
enum rxrpc_call_state {
	RXRPC_CALL_UNINITIALISED,
	RXRPC_CALL_CLIENT_AWAIT_CONN,	/* - client waiting for connection to become available */
	RXRPC_CALL_CLIENT_SEND_REQUEST,	/* - client sending request phase */
	RXRPC_CALL_CLIENT_AWAIT_REPLY,	/* - client awaiting reply */
	RXRPC_CALL_CLIENT_RECV_REPLY,	/* - client receiving reply phase */
	RXRPC_CALL_SERVER_PREALLOC,	/* - service preallocation */
	RXRPC_CALL_SERVER_SECURING,	/* - server securing request connection */
	RXRPC_CALL_SERVER_ACCEPTING,	/* - server accepting request */
	RXRPC_CALL_SERVER_RECV_REQUEST,	/* - server receiving request */
	RXRPC_CALL_SERVER_ACK_REQUEST,	/* - server pending ACK of request */
	RXRPC_CALL_SERVER_SEND_REPLY,	/* - server sending reply */
	RXRPC_CALL_SERVER_AWAIT_ACK,	/* - server awaiting final ACK */
	RXRPC_CALL_COMPLETE,		/* - call complete */
	NR__RXRPC_CALL_STATES
};

/*
 * Call Tx congestion management modes.
 */
enum rxrpc_congest_mode {
	RXRPC_CALL_SLOW_START,
	RXRPC_CALL_CONGEST_AVOIDANCE,
	RXRPC_CALL_PACKET_LOSS,
	RXRPC_CALL_FAST_RETRANSMIT,
	NR__RXRPC_CONGEST_MODES
};

/*
 * RxRPC call definition
 * - matched by { connection, call_id }
 */
struct rxrpc_call {
	struct rcu_head		rcu;
	struct rxrpc_connection	*conn;		/* connection carrying call */
	struct rxrpc_peer	*peer;		/* Peer record for remote address */
	struct rxrpc_sock __rcu	*socket;	/* socket responsible */
	struct rxrpc_net	*rxnet;		/* Network namespace to which call belongs */
	struct mutex		user_mutex;	/* User access mutex */
	unsigned long		ack_at;		/* When deferred ACK needs to happen */
	unsigned long		ack_lost_at;	/* When ACK is figured as lost */
	unsigned long		resend_at;	/* When next resend needs to happen */
	unsigned long		ping_at;	/* When next to send a ping */
	unsigned long		keepalive_at;	/* When next to send a keepalive ping */
	unsigned long		expect_rx_by;	/* When we expect to get a packet by */
	unsigned long		expect_req_by;	/* When we expect to get a request DATA packet by */
	unsigned long		expect_term_by;	/* When we expect call termination by */
	u32			next_rx_timo;	/* Timeout for next Rx packet (jif) */
	u32			next_req_timo;	/* Timeout for next Rx request packet (jif) */
	struct timer_list	timer;		/* Combined event timer */
	struct work_struct	processor;	/* Event processor */
	rxrpc_notify_rx_t	notify_rx;	/* kernel service Rx notification function */
	struct list_head	link;		/* link in master call list */
	struct list_head	chan_wait_link;	/* Link in conn->waiting_calls */
	struct hlist_node	error_link;	/* link in error distribution list */
	struct list_head	accept_link;	/* Link in rx->acceptq */
	struct list_head	recvmsg_link;	/* Link in rx->recvmsg_q */
	struct list_head	sock_link;	/* Link in rx->sock_calls */
	struct rb_node		sock_node;	/* Node in rx->calls */
	struct sk_buff		*tx_pending;	/* Tx socket buffer being filled */
	wait_queue_head_t	waitq;		/* Wait queue for channel or Tx */
	s64			tx_total_len;	/* Total length left to be transmitted (or -1) */
	__be32			crypto_buf[2];	/* Temporary packet crypto buffer */
	unsigned long		user_call_ID;	/* user-defined call ID */
	unsigned long		flags;
	unsigned long		events;
	spinlock_t		lock;
	spinlock_t		notify_lock;	/* Kernel notification lock */
	rwlock_t		state_lock;	/* lock for state transition */
	u32			abort_code;	/* Local/remote abort code */
	int			error;		/* Local error incurred */
	enum rxrpc_call_state	state;		/* current state of call */
	enum rxrpc_call_completion completion;	/* Call completion condition */
	atomic_t		usage;
	u16			service_id;	/* service ID */
	u8			security_ix;	/* Security type */
	u32			call_id;	/* call ID on connection  */
	u32			cid;		/* connection ID plus channel index */
	int			debug_id;	/* debug ID for printks */
	unsigned short		rx_pkt_offset;	/* Current recvmsg packet offset */
	unsigned short		rx_pkt_len;	/* Current recvmsg packet len */

	/* Rx/Tx circular buffer, depending on phase.
	 *
	 * In the Rx phase, packets are annotated with 0 or the number of the
	 * segment of a jumbo packet each buffer refers to.  There can be up to
	 * 47 segments in a maximum-size UDP packet.
	 *
	 * In the Tx phase, packets are annotated with which buffers have been
	 * acked.
	 */
#define RXRPC_RXTX_BUFF_SIZE	64
#define RXRPC_RXTX_BUFF_MASK	(RXRPC_RXTX_BUFF_SIZE - 1)
#define RXRPC_INIT_RX_WINDOW_SIZE 63
	struct sk_buff		**rxtx_buffer;
	u8			*rxtx_annotations;
#define RXRPC_TX_ANNO_ACK	0
#define RXRPC_TX_ANNO_UNACK	1
#define RXRPC_TX_ANNO_NAK	2
#define RXRPC_TX_ANNO_RETRANS	3
#define RXRPC_TX_ANNO_MASK	0x03
#define RXRPC_TX_ANNO_LAST	0x04
#define RXRPC_TX_ANNO_RESENT	0x08

#define RXRPC_RX_ANNO_JUMBO	0x3f		/* Jumbo subpacket number + 1 if not zero */
#define RXRPC_RX_ANNO_JLAST	0x40		/* Set if last element of a jumbo packet */
#define RXRPC_RX_ANNO_VERIFIED	0x80		/* Set if verified and decrypted */
	rxrpc_seq_t		tx_hard_ack;	/* Dead slot in buffer; the first transmitted but
						 * not hard-ACK'd packet follows this.
						 */
	rxrpc_seq_t		tx_top;		/* Highest Tx slot allocated. */
	u16			tx_backoff;	/* Delay to insert due to Tx failure */

	/* TCP-style slow-start congestion control [RFC5681].  Since the SMSS
	 * is fixed, we keep these numbers in terms of segments (ie. DATA
	 * packets) rather than bytes.
	 */
#define RXRPC_TX_SMSS		RXRPC_JUMBO_DATALEN
	u8			cong_cwnd;	/* Congestion window size */
	u8			cong_extra;	/* Extra to send for congestion management */
	u8			cong_ssthresh;	/* Slow-start threshold */
	enum rxrpc_congest_mode	cong_mode:8;	/* Congestion management mode */
	u8			cong_dup_acks;	/* Count of ACKs showing missing packets */
	u8			cong_cumul_acks; /* Cumulative ACK count */
	ktime_t			cong_tstamp;	/* Last time cwnd was changed */

	rxrpc_seq_t		rx_hard_ack;	/* Dead slot in buffer; the first received but not
						 * consumed packet follows this.
						 */
	rxrpc_seq_t		rx_top;		/* Highest Rx slot allocated. */
	rxrpc_seq_t		rx_expect_next;	/* Expected next packet sequence number */
	rxrpc_serial_t		rx_serial;	/* Highest serial received for this call */
	u8			rx_winsize;	/* Size of Rx window */
	u8			tx_winsize;	/* Maximum size of Tx window */
	bool			tx_phase;	/* T if transmission phase, F if receive phase */
	u8			nr_jumbo_bad;	/* Number of jumbo dups/exceeds-windows */

	spinlock_t		input_lock;	/* Lock for packet input to this call */

	/* receive-phase ACK management */
	u8			ackr_reason;	/* reason to ACK */
	u16			ackr_skew;	/* skew on packet being ACK'd */
	rxrpc_serial_t		ackr_serial;	/* serial of packet being ACK'd */
	rxrpc_serial_t		ackr_first_seq;	/* first sequence number received */
	rxrpc_seq_t		ackr_prev_seq;	/* previous sequence number received */
	rxrpc_seq_t		ackr_consumed;	/* Highest packet shown consumed */
	rxrpc_seq_t		ackr_seen;	/* Highest packet shown seen */

	/* ping management */
	rxrpc_serial_t		ping_serial;	/* Last ping sent */
	ktime_t			ping_time;	/* Time last ping sent */

	/* transmission-phase ACK management */
	ktime_t			acks_latest_ts;	/* Timestamp of latest ACK received */
	rxrpc_serial_t		acks_latest;	/* serial number of latest ACK received */
	rxrpc_seq_t		acks_lowest_nak; /* Lowest NACK in the buffer (or ==tx_hard_ack) */
	rxrpc_seq_t		acks_lost_top;	/* tx_top at the time lost-ack ping sent */
	rxrpc_serial_t		acks_lost_ping;	/* Serial number of probe ACK */
};

/*
 * Summary of a new ACK and the changes it made to the Tx buffer packet states.
 */
struct rxrpc_ack_summary {
	u8			ack_reason;
	u8			nr_acks;		/* Number of ACKs in packet */
	u8			nr_nacks;		/* Number of NACKs in packet */
	u8			nr_new_acks;		/* Number of new ACKs in packet */
	u8			nr_new_nacks;		/* Number of new NACKs in packet */
	u8			nr_rot_new_acks;	/* Number of rotated new ACKs */
	bool			new_low_nack;		/* T if new low NACK found */
	bool			retrans_timeo;		/* T if reTx due to timeout happened */
	u8			flight_size;		/* Number of unreceived transmissions */
	/* Place to stash values for tracing */
	enum rxrpc_congest_mode	mode:8;
	u8			cwnd;
	u8			ssthresh;
	u8			dup_acks;
	u8			cumulative_acks;
};

/*
 * sendmsg() cmsg-specified parameters.
 */
enum rxrpc_command {
	RXRPC_CMD_SEND_DATA,		/* send data message */
	RXRPC_CMD_SEND_ABORT,		/* request abort generation */
	RXRPC_CMD_ACCEPT,		/* [server] accept incoming call */
	RXRPC_CMD_REJECT_BUSY,		/* [server] reject a call as busy */
};

struct rxrpc_call_params {
	s64			tx_total_len;	/* Total Tx data length (if send data) */
	unsigned long		user_call_ID;	/* User's call ID */
	struct {
		u32		hard;		/* Maximum lifetime (sec) */
		u32		idle;		/* Max time since last data packet (msec) */
		u32		normal;		/* Max time since last call packet (msec) */
	} timeouts;
	u8			nr_timeouts;	/* Number of timeouts specified */
};

struct rxrpc_send_params {
	struct rxrpc_call_params call;
	u32			abort_code;	/* Abort code to Tx (if abort) */
	enum rxrpc_command	command : 8;	/* The command to implement */
	bool			exclusive;	/* Shared or exclusive call */
	bool			upgrade;	/* If the connection is upgradeable */
};

#include <trace/events/rxrpc.h>

/*
 * af_rxrpc.c
 */
extern atomic_t rxrpc_n_tx_skbs, rxrpc_n_rx_skbs;
extern struct workqueue_struct *rxrpc_workqueue;

/*
 * call_accept.c
 */
int rxrpc_service_prealloc(struct rxrpc_sock *, gfp_t);
void rxrpc_discard_prealloc(struct rxrpc_sock *);
struct rxrpc_call *rxrpc_new_incoming_call(struct rxrpc_local *,
					   struct rxrpc_sock *,
					   struct sk_buff *);
void rxrpc_accept_incoming_calls(struct rxrpc_local *);
struct rxrpc_call *rxrpc_accept_call(struct rxrpc_sock *, unsigned long,
				     rxrpc_notify_rx_t);
int rxrpc_reject_call(struct rxrpc_sock *);

/*
 * call_event.c
 */
void rxrpc_propose_ACK(struct rxrpc_call *, u8, u16, u32, bool, bool,
		       enum rxrpc_propose_ack_trace);
void rxrpc_process_call(struct work_struct *);

static inline void rxrpc_reduce_call_timer(struct rxrpc_call *call,
					   unsigned long expire_at,
					   unsigned long now,
					   enum rxrpc_timer_trace why)
{
	trace_rxrpc_timer(call, why, now);
	timer_reduce(&call->timer, expire_at);
}

/*
 * call_object.c
 */
extern const char *const rxrpc_call_states[];
extern const char *const rxrpc_call_completions[];
extern unsigned int rxrpc_max_call_lifetime;
extern struct kmem_cache *rxrpc_call_jar;

struct rxrpc_call *rxrpc_find_call_by_user_ID(struct rxrpc_sock *, unsigned long);
struct rxrpc_call *rxrpc_alloc_call(struct rxrpc_sock *, gfp_t, unsigned int);
struct rxrpc_call *rxrpc_new_client_call(struct rxrpc_sock *,
					 struct rxrpc_conn_parameters *,
					 struct sockaddr_rxrpc *,
					 struct rxrpc_call_params *, gfp_t,
					 unsigned int);
int rxrpc_retry_client_call(struct rxrpc_sock *,
			    struct rxrpc_call *,
			    struct rxrpc_conn_parameters *,
			    struct sockaddr_rxrpc *,
			    gfp_t);
void rxrpc_incoming_call(struct rxrpc_sock *, struct rxrpc_call *,
			 struct sk_buff *);
void rxrpc_release_call(struct rxrpc_sock *, struct rxrpc_call *);
int rxrpc_prepare_call_for_retry(struct rxrpc_sock *, struct rxrpc_call *);
void rxrpc_release_calls_on_socket(struct rxrpc_sock *);
bool __rxrpc_queue_call(struct rxrpc_call *);
bool rxrpc_queue_call(struct rxrpc_call *);
void rxrpc_see_call(struct rxrpc_call *);
void rxrpc_get_call(struct rxrpc_call *, enum rxrpc_call_trace);
void rxrpc_put_call(struct rxrpc_call *, enum rxrpc_call_trace);
void rxrpc_cleanup_call(struct rxrpc_call *);
void rxrpc_destroy_all_calls(struct rxrpc_net *);

static inline bool rxrpc_is_service_call(const struct rxrpc_call *call)
{
	return test_bit(RXRPC_CALL_IS_SERVICE, &call->flags);
}

static inline bool rxrpc_is_client_call(const struct rxrpc_call *call)
{
	return !rxrpc_is_service_call(call);
}

/*
 * Transition a call to the complete state.
 */
static inline bool __rxrpc_set_call_completion(struct rxrpc_call *call,
					       enum rxrpc_call_completion compl,
					       u32 abort_code,
					       int error)
{
	if (call->state < RXRPC_CALL_COMPLETE) {
		call->abort_code = abort_code;
		call->error = error;
		call->completion = compl,
		call->state = RXRPC_CALL_COMPLETE;
		trace_rxrpc_call_complete(call);
		wake_up(&call->waitq);
		return true;
	}
	return false;
}

static inline bool rxrpc_set_call_completion(struct rxrpc_call *call,
					     enum rxrpc_call_completion compl,
					     u32 abort_code,
					     int error)
{
	bool ret;

	write_lock_bh(&call->state_lock);
	ret = __rxrpc_set_call_completion(call, compl, abort_code, error);
	write_unlock_bh(&call->state_lock);
	return ret;
}

/*
 * Record that a call successfully completed.
 */
static inline bool __rxrpc_call_completed(struct rxrpc_call *call)
{
	return __rxrpc_set_call_completion(call, RXRPC_CALL_SUCCEEDED, 0, 0);
}

static inline bool rxrpc_call_completed(struct rxrpc_call *call)
{
	bool ret;

	write_lock_bh(&call->state_lock);
	ret = __rxrpc_call_completed(call);
	write_unlock_bh(&call->state_lock);
	return ret;
}

/*
 * Record that a call is locally aborted.
 */
static inline bool __rxrpc_abort_call(const char *why, struct rxrpc_call *call,
				      rxrpc_seq_t seq,
				      u32 abort_code, int error)
{
	trace_rxrpc_abort(call->debug_id, why, call->cid, call->call_id, seq,
			  abort_code, error);
	return __rxrpc_set_call_completion(call, RXRPC_CALL_LOCALLY_ABORTED,
					   abort_code, error);
}

static inline bool rxrpc_abort_call(const char *why, struct rxrpc_call *call,
				    rxrpc_seq_t seq, u32 abort_code, int error)
{
	bool ret;

	write_lock_bh(&call->state_lock);
	ret = __rxrpc_abort_call(why, call, seq, abort_code, error);
	write_unlock_bh(&call->state_lock);
	return ret;
}

/*
 * Abort a call due to a protocol error.
 */
static inline bool __rxrpc_abort_eproto(struct rxrpc_call *call,
					struct sk_buff *skb,
					const char *eproto_why,
					const char *why,
					u32 abort_code)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	trace_rxrpc_rx_eproto(call, sp->hdr.serial, eproto_why);
	return rxrpc_abort_call(why, call, sp->hdr.seq, abort_code, -EPROTO);
}

#define rxrpc_abort_eproto(call, skb, eproto_why, abort_why, abort_code) \
	__rxrpc_abort_eproto((call), (skb), tracepoint_string(eproto_why), \
			     (abort_why), (abort_code))

/*
 * conn_client.c
 */
extern unsigned int rxrpc_max_client_connections;
extern unsigned int rxrpc_reap_client_connections;
extern unsigned long rxrpc_conn_idle_client_expiry;
extern unsigned long rxrpc_conn_idle_client_fast_expiry;
extern struct idr rxrpc_client_conn_ids;

void rxrpc_destroy_client_conn_ids(void);
int rxrpc_connect_call(struct rxrpc_sock *, struct rxrpc_call *,
		       struct rxrpc_conn_parameters *, struct sockaddr_rxrpc *,
		       gfp_t);
void rxrpc_expose_client_call(struct rxrpc_call *);
void rxrpc_disconnect_client_call(struct rxrpc_call *);
void rxrpc_put_client_conn(struct rxrpc_connection *);
void rxrpc_discard_expired_client_conns(struct work_struct *);
void rxrpc_destroy_all_client_connections(struct rxrpc_net *);

/*
 * conn_event.c
 */
void rxrpc_process_connection(struct work_struct *);

/*
 * conn_object.c
 */
extern unsigned int rxrpc_connection_expiry;
extern unsigned int rxrpc_closed_conn_expiry;

struct rxrpc_connection *rxrpc_alloc_connection(gfp_t);
struct rxrpc_connection *rxrpc_find_connection_rcu(struct rxrpc_local *,
						   struct sk_buff *,
						   struct rxrpc_peer **);
void __rxrpc_disconnect_call(struct rxrpc_connection *, struct rxrpc_call *);
void rxrpc_disconnect_call(struct rxrpc_call *);
void rxrpc_kill_connection(struct rxrpc_connection *);
bool rxrpc_queue_conn(struct rxrpc_connection *);
void rxrpc_see_connection(struct rxrpc_connection *);
void rxrpc_get_connection(struct rxrpc_connection *);
struct rxrpc_connection *rxrpc_get_connection_maybe(struct rxrpc_connection *);
void rxrpc_put_service_conn(struct rxrpc_connection *);
void rxrpc_service_connection_reaper(struct work_struct *);
void rxrpc_destroy_all_connections(struct rxrpc_net *);

static inline bool rxrpc_conn_is_client(const struct rxrpc_connection *conn)
{
	return conn->out_clientflag;
}

static inline bool rxrpc_conn_is_service(const struct rxrpc_connection *conn)
{
	return !rxrpc_conn_is_client(conn);
}

static inline void rxrpc_put_connection(struct rxrpc_connection *conn)
{
	if (!conn)
		return;

	if (rxrpc_conn_is_client(conn))
		rxrpc_put_client_conn(conn);
	else
		rxrpc_put_service_conn(conn);
}

static inline void rxrpc_reduce_conn_timer(struct rxrpc_connection *conn,
					   unsigned long expire_at)
{
	timer_reduce(&conn->timer, expire_at);
}

/*
 * conn_service.c
 */
struct rxrpc_connection *rxrpc_find_service_conn_rcu(struct rxrpc_peer *,
						     struct sk_buff *);
struct rxrpc_connection *rxrpc_prealloc_service_connection(struct rxrpc_net *, gfp_t);
void rxrpc_new_incoming_connection(struct rxrpc_sock *,
				   struct rxrpc_connection *, struct sk_buff *);
void rxrpc_unpublish_service_conn(struct rxrpc_connection *);

/*
 * input.c
 */
int rxrpc_input_packet(struct sock *, struct sk_buff *);

/*
 * insecure.c
 */
extern const struct rxrpc_security rxrpc_no_security;

/*
 * key.c
 */
extern struct key_type key_type_rxrpc;
extern struct key_type key_type_rxrpc_s;

int rxrpc_request_key(struct rxrpc_sock *, char __user *, int);
int rxrpc_server_keyring(struct rxrpc_sock *, char __user *, int);
int rxrpc_get_server_data_key(struct rxrpc_connection *, const void *, time64_t,
			      u32);

/*
 * local_event.c
 */
extern void rxrpc_process_local_events(struct rxrpc_local *);

/*
 * local_object.c
 */
struct rxrpc_local *rxrpc_lookup_local(struct net *, const struct sockaddr_rxrpc *);
struct rxrpc_local *rxrpc_get_local(struct rxrpc_local *);
struct rxrpc_local *rxrpc_get_local_maybe(struct rxrpc_local *);
void rxrpc_put_local(struct rxrpc_local *);
struct rxrpc_local *rxrpc_use_local(struct rxrpc_local *);
void rxrpc_unuse_local(struct rxrpc_local *);
void rxrpc_queue_local(struct rxrpc_local *);
void rxrpc_destroy_all_locals(struct rxrpc_net *);

/*
 * misc.c
 */
extern unsigned int rxrpc_max_backlog __read_mostly;
extern unsigned long rxrpc_requested_ack_delay;
extern unsigned long rxrpc_soft_ack_delay;
extern unsigned long rxrpc_idle_ack_delay;
extern unsigned int rxrpc_rx_window_size;
extern unsigned int rxrpc_rx_mtu;
extern unsigned int rxrpc_rx_jumbo_max;
extern unsigned long rxrpc_resend_timeout;

extern const s8 rxrpc_ack_priority[];

/*
 * net_ns.c
 */
extern unsigned int rxrpc_net_id;
extern struct pernet_operations rxrpc_net_ops;

static inline struct rxrpc_net *rxrpc_net(struct net *net)
{
	return net_generic(net, rxrpc_net_id);
}

/*
 * output.c
 */
int rxrpc_send_ack_packet(struct rxrpc_call *, bool, rxrpc_serial_t *);
int rxrpc_send_abort_packet(struct rxrpc_call *);
int rxrpc_send_data_packet(struct rxrpc_call *, struct sk_buff *, bool);
void rxrpc_reject_packets(struct rxrpc_local *);
void rxrpc_send_keepalive(struct rxrpc_peer *);

/*
 * peer_event.c
 */
void rxrpc_error_report(struct sock *);
void rxrpc_peer_add_rtt(struct rxrpc_call *, enum rxrpc_rtt_rx_trace,
			rxrpc_serial_t, rxrpc_serial_t, ktime_t, ktime_t);
void rxrpc_peer_keepalive_worker(struct work_struct *);

/*
 * peer_object.c
 */
struct rxrpc_peer *rxrpc_lookup_peer_rcu(struct rxrpc_local *,
					 const struct sockaddr_rxrpc *);
struct rxrpc_peer *rxrpc_lookup_peer(struct rxrpc_sock *, struct rxrpc_local *,
				     struct sockaddr_rxrpc *, gfp_t);
struct rxrpc_peer *rxrpc_alloc_peer(struct rxrpc_local *, gfp_t);
void rxrpc_new_incoming_peer(struct rxrpc_sock *, struct rxrpc_local *,
			     struct rxrpc_peer *);
void rxrpc_destroy_all_peers(struct rxrpc_net *);
struct rxrpc_peer *rxrpc_get_peer(struct rxrpc_peer *);
struct rxrpc_peer *rxrpc_get_peer_maybe(struct rxrpc_peer *);
void rxrpc_put_peer(struct rxrpc_peer *);
void rxrpc_put_peer_locked(struct rxrpc_peer *);

/*
 * proc.c
 */
extern const struct seq_operations rxrpc_call_seq_ops;
extern const struct seq_operations rxrpc_connection_seq_ops;

/*
 * recvmsg.c
 */
void rxrpc_notify_socket(struct rxrpc_call *);
int rxrpc_recvmsg(struct socket *, struct msghdr *, size_t, int);

/*
 * rxkad.c
 */
#ifdef CONFIG_RXKAD
extern const struct rxrpc_security rxkad;
#endif

/*
 * security.c
 */
int __init rxrpc_init_security(void);
void rxrpc_exit_security(void);
int rxrpc_init_client_conn_security(struct rxrpc_connection *);
int rxrpc_init_server_conn_security(struct rxrpc_connection *);

/*
 * sendmsg.c
 */
int rxrpc_do_sendmsg(struct rxrpc_sock *, struct msghdr *, size_t);

/*
 * skbuff.c
 */
void rxrpc_kernel_data_consumed(struct rxrpc_call *, struct sk_buff *);
void rxrpc_packet_destructor(struct sk_buff *);
void rxrpc_new_skb(struct sk_buff *, enum rxrpc_skb_trace);
void rxrpc_see_skb(struct sk_buff *, enum rxrpc_skb_trace);
void rxrpc_get_skb(struct sk_buff *, enum rxrpc_skb_trace);
void rxrpc_free_skb(struct sk_buff *, enum rxrpc_skb_trace);
void rxrpc_lose_skb(struct sk_buff *, enum rxrpc_skb_trace);
void rxrpc_purge_queue(struct sk_buff_head *);

/*
 * sysctl.c
 */
#ifdef CONFIG_SYSCTL
extern int __init rxrpc_sysctl_init(void);
extern void rxrpc_sysctl_exit(void);
#else
static inline int __init rxrpc_sysctl_init(void) { return 0; }
static inline void rxrpc_sysctl_exit(void) {}
#endif

/*
 * utils.c
 */
int rxrpc_extract_addr_from_skb(struct rxrpc_local *, struct sockaddr_rxrpc *,
				struct sk_buff *);

static inline bool before(u32 seq1, u32 seq2)
{
        return (s32)(seq1 - seq2) < 0;
}
static inline bool before_eq(u32 seq1, u32 seq2)
{
        return (s32)(seq1 - seq2) <= 0;
}
static inline bool after(u32 seq1, u32 seq2)
{
        return (s32)(seq1 - seq2) > 0;
}
static inline bool after_eq(u32 seq1, u32 seq2)
{
        return (s32)(seq1 - seq2) >= 0;
}

/*
 * debug tracing
 */
extern unsigned int rxrpc_debug;

#define dbgprintk(FMT,...) \
	printk("[%-6.6s] "FMT"\n", current->comm ,##__VA_ARGS__)

#define kenter(FMT,...)	dbgprintk("==> %s("FMT")",__func__ ,##__VA_ARGS__)
#define kleave(FMT,...)	dbgprintk("<== %s()"FMT"",__func__ ,##__VA_ARGS__)
#define kdebug(FMT,...)	dbgprintk("    "FMT ,##__VA_ARGS__)
#define kproto(FMT,...)	dbgprintk("### "FMT ,##__VA_ARGS__)
#define knet(FMT,...)	dbgprintk("@@@ "FMT ,##__VA_ARGS__)


#if defined(__KDEBUG)
#define _enter(FMT,...)	kenter(FMT,##__VA_ARGS__)
#define _leave(FMT,...)	kleave(FMT,##__VA_ARGS__)
#define _debug(FMT,...)	kdebug(FMT,##__VA_ARGS__)
#define _proto(FMT,...)	kproto(FMT,##__VA_ARGS__)
#define _net(FMT,...)	knet(FMT,##__VA_ARGS__)

#elif defined(CONFIG_AF_RXRPC_DEBUG)
#define RXRPC_DEBUG_KENTER	0x01
#define RXRPC_DEBUG_KLEAVE	0x02
#define RXRPC_DEBUG_KDEBUG	0x04
#define RXRPC_DEBUG_KPROTO	0x08
#define RXRPC_DEBUG_KNET	0x10

#define _enter(FMT,...)					\
do {							\
	if (unlikely(rxrpc_debug & RXRPC_DEBUG_KENTER))	\
		kenter(FMT,##__VA_ARGS__);		\
} while (0)

#define _leave(FMT,...)					\
do {							\
	if (unlikely(rxrpc_debug & RXRPC_DEBUG_KLEAVE))	\
		kleave(FMT,##__VA_ARGS__);		\
} while (0)

#define _debug(FMT,...)					\
do {							\
	if (unlikely(rxrpc_debug & RXRPC_DEBUG_KDEBUG))	\
		kdebug(FMT,##__VA_ARGS__);		\
} while (0)

#define _proto(FMT,...)					\
do {							\
	if (unlikely(rxrpc_debug & RXRPC_DEBUG_KPROTO))	\
		kproto(FMT,##__VA_ARGS__);		\
} while (0)

#define _net(FMT,...)					\
do {							\
	if (unlikely(rxrpc_debug & RXRPC_DEBUG_KNET))	\
		knet(FMT,##__VA_ARGS__);		\
} while (0)

#else
#define _enter(FMT,...)	no_printk("==> %s("FMT")",__func__ ,##__VA_ARGS__)
#define _leave(FMT,...)	no_printk("<== %s()"FMT"",__func__ ,##__VA_ARGS__)
#define _debug(FMT,...)	no_printk("    "FMT ,##__VA_ARGS__)
#define _proto(FMT,...)	no_printk("### "FMT ,##__VA_ARGS__)
#define _net(FMT,...)	no_printk("@@@ "FMT ,##__VA_ARGS__)
#endif

/*
 * debug assertion checking
 */
#if 1 // defined(__KDEBUGALL)

#define ASSERT(X)						\
do {								\
	if (unlikely(!(X))) {					\
		pr_err("Assertion failed\n");			\
		BUG();						\
	}							\
} while (0)

#define ASSERTCMP(X, OP, Y)						\
do {									\
	__typeof__(X) _x = (X);						\
	__typeof__(Y) _y = (__typeof__(X))(Y);				\
	if (unlikely(!(_x OP _y))) {					\
		pr_err("Assertion failed - %lu(0x%lx) %s %lu(0x%lx) is false\n", \
		       (unsigned long)_x, (unsigned long)_x, #OP,	\
		       (unsigned long)_y, (unsigned long)_y);		\
		BUG();							\
	}								\
} while (0)

#define ASSERTIF(C, X)						\
do {								\
	if (unlikely((C) && !(X))) {				\
		pr_err("Assertion failed\n");			\
		BUG();						\
	}							\
} while (0)

#define ASSERTIFCMP(C, X, OP, Y)					\
do {									\
	__typeof__(X) _x = (X);						\
	__typeof__(Y) _y = (__typeof__(X))(Y);				\
	if (unlikely((C) && !(_x OP _y))) {				\
		pr_err("Assertion failed - %lu(0x%lx) %s %lu(0x%lx) is false\n", \
		       (unsigned long)_x, (unsigned long)_x, #OP,	\
		       (unsigned long)_y, (unsigned long)_y);		\
		BUG();							\
	}								\
} while (0)

#else

#define ASSERT(X)				\
do {						\
} while (0)

#define ASSERTCMP(X, OP, Y)			\
do {						\
} while (0)

#define ASSERTIF(C, X)				\
do {						\
} while (0)

#define ASSERTIFCMP(C, X, OP, Y)		\
do {						\
} while (0)

#endif /* __KDEBUGALL */
