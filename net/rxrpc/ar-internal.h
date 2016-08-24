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
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <rxrpc/packet.h>

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

#define rxrpc_queue_call(CALL)	rxrpc_queue_work(&(CALL)->processor)

struct rxrpc_connection;

/*
 * sk_state for RxRPC sockets
 */
enum {
	RXRPC_UNBOUND = 0,
	RXRPC_CLIENT_UNBOUND,		/* Unbound socket used as client */
	RXRPC_CLIENT_BOUND,		/* client local address bound */
	RXRPC_SERVER_BOUND,		/* server local address bound */
	RXRPC_SERVER_LISTENING,		/* server listening for connections */
	RXRPC_CLOSE,			/* socket is being closed */
};

/*
 * RxRPC socket definition
 */
struct rxrpc_sock {
	/* WARNING: sk has to be the first member */
	struct sock		sk;
	rxrpc_interceptor_t	interceptor;	/* kernel service Rx interceptor function */
	struct rxrpc_local	*local;		/* local endpoint */
	struct list_head	listen_link;	/* link in the local endpoint's listen list */
	struct list_head	secureq;	/* calls awaiting connection security clearance */
	struct list_head	acceptq;	/* calls awaiting acceptance */
	struct key		*key;		/* security for this socket */
	struct key		*securities;	/* list of server security descriptors */
	struct rb_root		calls;		/* outstanding calls on this socket */
	unsigned long		flags;
#define RXRPC_SOCK_CONNECTED		0	/* connect_srx is set */
	rwlock_t		call_lock;	/* lock for calls */
	u32			min_sec_level;	/* minimum security level */
#define RXRPC_SECURITY_MAX	RXRPC_SECURITY_ENCRYPT
	bool			exclusive;	/* Exclusive connection for a client socket */
	sa_family_t		family;		/* Protocol family created with */
	struct sockaddr_rxrpc	srx;		/* local address */
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
	struct rxrpc_call	*call;		/* call with which associated */
	unsigned long		resend_at;	/* time in jiffies at which to resend */
	union {
		unsigned int	offset;		/* offset into buffer of next read */
		int		remain;		/* amount of space remaining for next write */
		u32		error;		/* network error code */
		bool		need_resend;	/* T if needs resending */
	};

	struct rxrpc_host_header hdr;		/* RxRPC packet header from this packet */
};

#define rxrpc_skb(__skb) ((struct rxrpc_skb_priv *) &(__skb)->cb)

enum rxrpc_command {
	RXRPC_CMD_SEND_DATA,		/* send data message */
	RXRPC_CMD_SEND_ABORT,		/* request abort generation */
	RXRPC_CMD_ACCEPT,		/* [server] accept incoming call */
	RXRPC_CMD_REJECT_BUSY,		/* [server] reject a call as busy */
};

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
	int (*verify_packet)(struct rxrpc_call *, struct sk_buff *, u32 *);

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
	atomic_t		usage;
	struct list_head	link;
	struct socket		*socket;	/* my UDP socket */
	struct work_struct	processor;
	struct list_head	services;	/* services listening on this endpoint */
	struct rw_semaphore	defrag_sem;	/* control re-enablement of IP DF bit */
	struct sk_buff_head	accept_queue;	/* incoming calls awaiting acceptance */
	struct sk_buff_head	reject_queue;	/* packets awaiting rejection */
	struct sk_buff_head	event_queue;	/* endpoint event packets awaiting processing */
	struct rb_root		client_conns;	/* Client connections by socket params */
	spinlock_t		client_conns_lock; /* Lock for client_conns */
	spinlock_t		lock;		/* access lock */
	rwlock_t		services_lock;	/* lock for services list */
	int			debug_id;	/* debug ID for printks */
	bool			dead;
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
	struct work_struct	error_distributor;
	struct rb_root		service_conns;	/* Service connections */
	seqlock_t		service_conn_lock;
	spinlock_t		lock;		/* access lock */
	unsigned int		if_mtu;		/* interface MTU for this peer */
	unsigned int		mtu;		/* network MTU for this peer */
	unsigned int		maxdata;	/* data size (MTU - hdrsize) */
	unsigned short		hdrsize;	/* header size (IP + UDP + RxRPC) */
	int			debug_id;	/* debug ID for printks */
	int			error_report;	/* Net (+0) or local (+1000000) to distribute */
#define RXRPC_LOCAL_ERROR_OFFSET 1000000
	struct sockaddr_rxrpc	srx;		/* remote address */

	/* calculated RTT cache */
#define RXRPC_RTT_CACHE_SIZE 32
	suseconds_t		rtt;		/* current RTT estimate (in uS) */
	unsigned int		rtt_point;	/* next entry at which to insert */
	unsigned int		rtt_usage;	/* amount of cache actually used */
	suseconds_t		rtt_cache[RXRPC_RTT_CACHE_SIZE]; /* calculated RTT cache */
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
};

/*
 * Events that can be raised upon a connection.
 */
enum rxrpc_conn_event {
	RXRPC_CONN_EV_CHALLENGE,	/* Send challenge packet */
};

/*
 * The connection protocol state.
 */
enum rxrpc_conn_proto_state {
	RXRPC_CONN_UNUSED,		/* Connection not yet attempted */
	RXRPC_CONN_CLIENT,		/* Client connection */
	RXRPC_CONN_SERVICE_UNSECURED,	/* Service unsecured connection */
	RXRPC_CONN_SERVICE_CHALLENGING,	/* Service challenging for security */
	RXRPC_CONN_SERVICE,		/* Service secured connection */
	RXRPC_CONN_REMOTELY_ABORTED,	/* Conn aborted by peer */
	RXRPC_CONN_LOCALLY_ABORTED,	/* Conn aborted locally */
	RXRPC_CONN_NETWORK_ERROR,	/* Conn terminated by network error */
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

	spinlock_t		channel_lock;

	struct rxrpc_channel {
		struct rxrpc_call __rcu	*call;		/* Active call */
		u32			call_id;	/* ID of current call */
		u32			call_counter;	/* Call ID counter */
		u32			last_call;	/* ID of last call */
		u8			last_type;	/* Type of last packet */
		u16			last_service_id;
		union {
			u32		last_seq;
			u32		last_abort;
		};
	} channels[RXRPC_MAXCALLS];
	wait_queue_head_t	channel_wq;	/* queue to wait for channel to become available */

	struct rcu_head		rcu;
	struct work_struct	processor;	/* connection event processor */
	union {
		struct rb_node	client_node;	/* Node in local->client_conns */
		struct rb_node	service_node;	/* Node in peer->service_conns */
	};
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
	atomic_t		usage;
	enum rxrpc_conn_proto_state state : 8;	/* current state of connection */
	u32			local_abort;	/* local abort code */
	u32			remote_abort;	/* remote abort code */
	int			error;		/* local error incurred */
	int			debug_id;	/* debug ID for printks */
	atomic_t		serial;		/* packet serial number counter */
	unsigned int		hi_serial;	/* highest serial number received */
	atomic_t		avail_chans;	/* number of channels available */
	u8			size_align;	/* data size alignment (for security) */
	u8			header_size;	/* rxrpc + security header size */
	u8			security_size;	/* security header size */
	u32			security_nonce;	/* response re-use preventer */
	u8			security_ix;	/* security type */
	u8			out_clientflag;	/* RXRPC_CLIENT_INITIATED if we are client */
};

/*
 * Flags in call->flags.
 */
enum rxrpc_call_flag {
	RXRPC_CALL_RELEASED,		/* call has been released - no more message to userspace */
	RXRPC_CALL_TERMINAL_MSG,	/* call has given the socket its final message */
	RXRPC_CALL_RCVD_LAST,		/* all packets received */
	RXRPC_CALL_RUN_RTIMER,		/* Tx resend timer started */
	RXRPC_CALL_TX_SOFT_ACK,		/* sent some soft ACKs */
	RXRPC_CALL_INIT_ACCEPT,		/* acceptance was initiated */
	RXRPC_CALL_HAS_USERID,		/* has a user ID attached */
	RXRPC_CALL_EXPECT_OOS,		/* expect out of sequence packets */
	RXRPC_CALL_IS_SERVICE,		/* Call is service call */
};

/*
 * Events that can be raised on a call.
 */
enum rxrpc_call_event {
	RXRPC_CALL_EV_RCVD_ACKALL,	/* ACKALL or reply received */
	RXRPC_CALL_EV_RCVD_BUSY,	/* busy packet received */
	RXRPC_CALL_EV_RCVD_ABORT,	/* abort packet received */
	RXRPC_CALL_EV_RCVD_ERROR,	/* network error received */
	RXRPC_CALL_EV_ACK_FINAL,	/* need to generate final ACK (and release call) */
	RXRPC_CALL_EV_ACK,		/* need to generate ACK */
	RXRPC_CALL_EV_REJECT_BUSY,	/* need to generate busy message */
	RXRPC_CALL_EV_ABORT,		/* need to generate abort */
	RXRPC_CALL_EV_CONN_ABORT,	/* local connection abort generated */
	RXRPC_CALL_EV_RESEND_TIMER,	/* Tx resend timer expired */
	RXRPC_CALL_EV_RESEND,		/* Tx resend required */
	RXRPC_CALL_EV_DRAIN_RX_OOS,	/* drain the Rx out of sequence queue */
	RXRPC_CALL_EV_LIFE_TIMER,	/* call's lifetimer ran out */
	RXRPC_CALL_EV_ACCEPTED,		/* incoming call accepted by userspace app */
	RXRPC_CALL_EV_SECURED,		/* incoming call's connection is now secure */
	RXRPC_CALL_EV_POST_ACCEPT,	/* need to post an "accept?" message to the app */
	RXRPC_CALL_EV_RELEASE,		/* need to release the call's resources */
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
	RXRPC_CALL_CLIENT_FINAL_ACK,	/* - client sending final ACK phase */
	RXRPC_CALL_SERVER_SECURING,	/* - server securing request connection */
	RXRPC_CALL_SERVER_ACCEPTING,	/* - server accepting request */
	RXRPC_CALL_SERVER_RECV_REQUEST,	/* - server receiving request */
	RXRPC_CALL_SERVER_ACK_REQUEST,	/* - server pending ACK of request */
	RXRPC_CALL_SERVER_SEND_REPLY,	/* - server sending reply */
	RXRPC_CALL_SERVER_AWAIT_ACK,	/* - server awaiting final ACK */
	RXRPC_CALL_COMPLETE,		/* - call completed */
	RXRPC_CALL_SERVER_BUSY,		/* - call rejected by busy server */
	RXRPC_CALL_REMOTELY_ABORTED,	/* - call aborted by peer */
	RXRPC_CALL_LOCALLY_ABORTED,	/* - call aborted locally on error or close */
	RXRPC_CALL_NETWORK_ERROR,	/* - call terminated by network error */
	RXRPC_CALL_DEAD,		/* - call is dead */
	NR__RXRPC_CALL_STATES
};

/*
 * RxRPC call definition
 * - matched by { connection, call_id }
 */
struct rxrpc_call {
	struct rcu_head		rcu;
	struct rxrpc_connection	*conn;		/* connection carrying call */
	struct rxrpc_peer	*peer;		/* Peer record for remote address */
	struct rxrpc_sock	*socket;	/* socket responsible */
	struct timer_list	lifetimer;	/* lifetime remaining on call */
	struct timer_list	deadspan;	/* reap timer for re-ACK'ing, etc  */
	struct timer_list	ack_timer;	/* ACK generation timer */
	struct timer_list	resend_timer;	/* Tx resend timer */
	struct work_struct	destroyer;	/* call destroyer */
	struct work_struct	processor;	/* packet processor and ACK generator */
	struct list_head	link;		/* link in master call list */
	struct hlist_node	error_link;	/* link in error distribution list */
	struct list_head	accept_link;	/* calls awaiting acceptance */
	struct rb_node		sock_node;	/* node in socket call tree */
	struct sk_buff_head	rx_queue;	/* received packets */
	struct sk_buff_head	rx_oos_queue;	/* packets received out of sequence */
	struct sk_buff		*tx_pending;	/* Tx socket buffer being filled */
	wait_queue_head_t	tx_waitq;	/* wait for Tx window space to become available */
	__be32			crypto_buf[2];	/* Temporary packet crypto buffer */
	unsigned long		user_call_ID;	/* user-defined call ID */
	unsigned long		creation_jif;	/* time of call creation */
	unsigned long		flags;
	unsigned long		events;
	spinlock_t		lock;
	rwlock_t		state_lock;	/* lock for state transition */
	atomic_t		usage;
	atomic_t		skb_count;	/* Outstanding packets on this call */
	atomic_t		sequence;	/* Tx data packet sequence counter */
	u32			local_abort;	/* local abort code */
	u32			remote_abort;	/* remote abort code */
	int			error_report;	/* Network error (ICMP/local transport) */
	int			error;		/* Local error incurred */
	enum rxrpc_call_state	state : 8;	/* current state of call */
	u16			service_id;	/* service ID */
	u32			call_id;	/* call ID on connection  */
	u32			cid;		/* connection ID plus channel index */
	int			debug_id;	/* debug ID for printks */

	/* transmission-phase ACK management */
	u8			acks_head;	/* offset into window of first entry */
	u8			acks_tail;	/* offset into window of last entry */
	u8			acks_winsz;	/* size of un-ACK'd window */
	u8			acks_unacked;	/* lowest unacked packet in last ACK received */
	int			acks_latest;	/* serial number of latest ACK received */
	rxrpc_seq_t		acks_hard;	/* highest definitively ACK'd msg seq */
	unsigned long		*acks_window;	/* sent packet window
						 * - elements are pointers with LSB set if ACK'd
						 */

	/* receive-phase ACK management */
	rxrpc_seq_t		rx_data_expect;	/* next data seq ID expected to be received */
	rxrpc_seq_t		rx_data_post;	/* next data seq ID expected to be posted */
	rxrpc_seq_t		rx_data_recv;	/* last data seq ID encountered by recvmsg */
	rxrpc_seq_t		rx_data_eaten;	/* last data seq ID consumed by recvmsg */
	rxrpc_seq_t		rx_first_oos;	/* first packet in rx_oos_queue (or 0) */
	rxrpc_seq_t		ackr_win_top;	/* top of ACK window (rx_data_eaten is bottom) */
	rxrpc_seq_t		ackr_prev_seq;	/* previous sequence number received */
	u8			ackr_reason;	/* reason to ACK */
	u16			ackr_skew;	/* skew on packet being ACK'd */
	rxrpc_serial_t		ackr_serial;	/* serial of packet being ACK'd */
	atomic_t		ackr_not_idle;	/* number of packets in Rx queue */

	/* received packet records, 1 bit per record */
#define RXRPC_ACKR_WINDOW_ASZ DIV_ROUND_UP(RXRPC_MAXACKS, BITS_PER_LONG)
	unsigned long		ackr_window[RXRPC_ACKR_WINDOW_ASZ + 1];
};

/*
 * locally abort an RxRPC call
 */
static inline void rxrpc_abort_call(struct rxrpc_call *call, u32 abort_code)
{
	write_lock_bh(&call->state_lock);
	if (call->state < RXRPC_CALL_COMPLETE) {
		call->local_abort = abort_code;
		call->state = RXRPC_CALL_LOCALLY_ABORTED;
		set_bit(RXRPC_CALL_EV_ABORT, &call->events);
	}
	write_unlock_bh(&call->state_lock);
}

#include <trace/events/rxrpc.h>

/*
 * af_rxrpc.c
 */
extern atomic_t rxrpc_n_skbs;
extern u32 rxrpc_epoch;
extern atomic_t rxrpc_debug_id;
extern struct workqueue_struct *rxrpc_workqueue;

/*
 * call_accept.c
 */
void rxrpc_accept_incoming_calls(struct rxrpc_local *);
struct rxrpc_call *rxrpc_accept_call(struct rxrpc_sock *, unsigned long);
int rxrpc_reject_call(struct rxrpc_sock *);

/*
 * call_event.c
 */
void __rxrpc_propose_ACK(struct rxrpc_call *, u8, u16, u32, bool);
void rxrpc_propose_ACK(struct rxrpc_call *, u8, u16, u32, bool);
void rxrpc_process_call(struct work_struct *);

/*
 * call_object.c
 */
extern unsigned int rxrpc_max_call_lifetime;
extern unsigned int rxrpc_dead_call_expiry;
extern struct kmem_cache *rxrpc_call_jar;
extern struct list_head rxrpc_calls;
extern rwlock_t rxrpc_call_lock;

struct rxrpc_call *rxrpc_find_call_by_user_ID(struct rxrpc_sock *, unsigned long);
struct rxrpc_call *rxrpc_new_client_call(struct rxrpc_sock *,
					 struct rxrpc_conn_parameters *,
					 struct sockaddr_rxrpc *,
					 unsigned long, gfp_t);
struct rxrpc_call *rxrpc_incoming_call(struct rxrpc_sock *,
				       struct rxrpc_connection *,
				       struct sk_buff *);
void rxrpc_release_call(struct rxrpc_call *);
void rxrpc_release_calls_on_socket(struct rxrpc_sock *);
void __rxrpc_put_call(struct rxrpc_call *);
void __exit rxrpc_destroy_all_calls(void);

static inline bool rxrpc_is_service_call(const struct rxrpc_call *call)
{
	return test_bit(RXRPC_CALL_IS_SERVICE, &call->flags);
}

static inline bool rxrpc_is_client_call(const struct rxrpc_call *call)
{
	return !rxrpc_is_service_call(call);
}

/*
 * conn_client.c
 */
extern struct idr rxrpc_client_conn_ids;

void rxrpc_destroy_client_conn_ids(void);
int rxrpc_connect_call(struct rxrpc_call *, struct rxrpc_conn_parameters *,
		       struct sockaddr_rxrpc *, gfp_t);
void rxrpc_unpublish_client_conn(struct rxrpc_connection *);

/*
 * conn_event.c
 */
void rxrpc_process_connection(struct work_struct *);
void rxrpc_reject_packet(struct rxrpc_local *, struct sk_buff *);
void rxrpc_reject_packets(struct rxrpc_local *);

/*
 * conn_object.c
 */
extern unsigned int rxrpc_connection_expiry;
extern struct list_head rxrpc_connections;
extern rwlock_t rxrpc_connection_lock;

int rxrpc_extract_addr_from_skb(struct sockaddr_rxrpc *, struct sk_buff *);
struct rxrpc_connection *rxrpc_alloc_connection(gfp_t);
struct rxrpc_connection *rxrpc_find_connection_rcu(struct rxrpc_local *,
						   struct sk_buff *);
void __rxrpc_disconnect_call(struct rxrpc_call *);
void rxrpc_disconnect_call(struct rxrpc_call *);
void __rxrpc_put_connection(struct rxrpc_connection *);
void __exit rxrpc_destroy_all_connections(void);

static inline bool rxrpc_conn_is_client(const struct rxrpc_connection *conn)
{
	return conn->out_clientflag;
}

static inline bool rxrpc_conn_is_service(const struct rxrpc_connection *conn)
{
	return !rxrpc_conn_is_client(conn);
}

static inline void rxrpc_get_connection(struct rxrpc_connection *conn)
{
	atomic_inc(&conn->usage);
}

static inline
struct rxrpc_connection *rxrpc_get_connection_maybe(struct rxrpc_connection *conn)
{
	return atomic_inc_not_zero(&conn->usage) ? conn : NULL;
}

static inline void rxrpc_put_connection(struct rxrpc_connection *conn)
{
	if (conn && atomic_dec_return(&conn->usage) == 1)
		__rxrpc_put_connection(conn);
}


static inline bool rxrpc_queue_conn(struct rxrpc_connection *conn)
{
	if (!rxrpc_get_connection_maybe(conn))
		return false;
	if (!rxrpc_queue_work(&conn->processor))
		rxrpc_put_connection(conn);
	return true;
}

/*
 * conn_service.c
 */
struct rxrpc_connection *rxrpc_find_service_conn_rcu(struct rxrpc_peer *,
						     struct sk_buff *);
struct rxrpc_connection *rxrpc_incoming_connection(struct rxrpc_local *,
						   struct sockaddr_rxrpc *,
						   struct sk_buff *);
void rxrpc_unpublish_service_conn(struct rxrpc_connection *);

/*
 * input.c
 */
void rxrpc_data_ready(struct sock *);
int rxrpc_queue_rcv_skb(struct rxrpc_call *, struct sk_buff *, bool, bool);
void rxrpc_fast_process_packet(struct rxrpc_call *, struct sk_buff *);

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
int rxrpc_get_server_data_key(struct rxrpc_connection *, const void *, time_t,
			      u32);

/*
 * local_event.c
 */
extern void rxrpc_process_local_events(struct rxrpc_local *);

/*
 * local_object.c
 */
struct rxrpc_local *rxrpc_lookup_local(const struct sockaddr_rxrpc *);
void __rxrpc_put_local(struct rxrpc_local *);
void __exit rxrpc_destroy_all_locals(void);

static inline void rxrpc_get_local(struct rxrpc_local *local)
{
	atomic_inc(&local->usage);
}

static inline
struct rxrpc_local *rxrpc_get_local_maybe(struct rxrpc_local *local)
{
	return atomic_inc_not_zero(&local->usage) ? local : NULL;
}

static inline void rxrpc_put_local(struct rxrpc_local *local)
{
	if (local && atomic_dec_and_test(&local->usage))
		__rxrpc_put_local(local);
}

static inline void rxrpc_queue_local(struct rxrpc_local *local)
{
	rxrpc_queue_work(&local->processor);
}

/*
 * misc.c
 */
extern unsigned int rxrpc_max_backlog __read_mostly;
extern unsigned int rxrpc_requested_ack_delay;
extern unsigned int rxrpc_soft_ack_delay;
extern unsigned int rxrpc_idle_ack_delay;
extern unsigned int rxrpc_rx_window_size;
extern unsigned int rxrpc_rx_mtu;
extern unsigned int rxrpc_rx_jumbo_max;

extern const char *const rxrpc_pkts[];
extern const s8 rxrpc_ack_priority[];

extern const char *rxrpc_acks(u8 reason);

/*
 * output.c
 */
extern unsigned int rxrpc_resend_timeout;

int rxrpc_send_data_packet(struct rxrpc_connection *, struct sk_buff *);
int rxrpc_do_sendmsg(struct rxrpc_sock *, struct msghdr *, size_t);

/*
 * peer_event.c
 */
void rxrpc_error_report(struct sock *);
void rxrpc_peer_error_distributor(struct work_struct *);

/*
 * peer_object.c
 */
struct rxrpc_peer *rxrpc_lookup_peer_rcu(struct rxrpc_local *,
					 const struct sockaddr_rxrpc *);
struct rxrpc_peer *rxrpc_lookup_peer(struct rxrpc_local *,
				     struct sockaddr_rxrpc *, gfp_t);
struct rxrpc_peer *rxrpc_alloc_peer(struct rxrpc_local *, gfp_t);

static inline struct rxrpc_peer *rxrpc_get_peer(struct rxrpc_peer *peer)
{
	atomic_inc(&peer->usage);
	return peer;
}

static inline
struct rxrpc_peer *rxrpc_get_peer_maybe(struct rxrpc_peer *peer)
{
	return atomic_inc_not_zero(&peer->usage) ? peer : NULL;
}

extern void __rxrpc_put_peer(struct rxrpc_peer *peer);
static inline void rxrpc_put_peer(struct rxrpc_peer *peer)
{
	if (peer && atomic_dec_and_test(&peer->usage))
		__rxrpc_put_peer(peer);
}

/*
 * proc.c
 */
extern const char *const rxrpc_call_states[];
extern const struct file_operations rxrpc_call_seq_fops;
extern const struct file_operations rxrpc_connection_seq_fops;

/*
 * recvmsg.c
 */
void rxrpc_remove_user_ID(struct rxrpc_sock *, struct rxrpc_call *);
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
 * skbuff.c
 */
void rxrpc_packet_destructor(struct sk_buff *);
void rxrpc_new_skb(struct sk_buff *);
void rxrpc_see_skb(struct sk_buff *);
void rxrpc_get_skb(struct sk_buff *);
void rxrpc_free_skb(struct sk_buff *);
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
int rxrpc_extract_addr_from_skb(struct sockaddr_rxrpc *, struct sk_buff *);

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
	unsigned long _x = (unsigned long)(X);				\
	unsigned long _y = (unsigned long)(Y);				\
	if (unlikely(!(_x OP _y))) {					\
		pr_err("Assertion failed - %lu(0x%lx) %s %lu(0x%lx) is false\n",			\
		       _x, _x, #OP, _y, _y);				\
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
	unsigned long _x = (unsigned long)(X);				\
	unsigned long _y = (unsigned long)(Y);				\
	if (unlikely((C) && !(_x OP _y))) {				\
		pr_err("Assertion failed - %lu(0x%lx) %s %lu(0x%lx) is false\n", \
		       _x, _x, #OP, _y, _y);				\
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


#define rxrpc_get_call(CALL)				\
do {							\
	CHECK_SLAB_OKAY(&(CALL)->usage);		\
	if (atomic_inc_return(&(CALL)->usage) == 1)	\
		BUG();					\
} while (0)

#define rxrpc_put_call(CALL)				\
do {							\
	__rxrpc_put_call(CALL);				\
} while (0)
