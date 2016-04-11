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

#include <net/sock.h>
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
#define rxrpc_queue_conn(CONN)	rxrpc_queue_work(&(CONN)->processor)

/*
 * sk_state for RxRPC sockets
 */
enum {
	RXRPC_UNCONNECTED = 0,
	RXRPC_CLIENT_BOUND,		/* client local address bound */
	RXRPC_CLIENT_CONNECTED,		/* client is connected */
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
	struct rxrpc_transport	*trans;		/* transport handler */
	struct rxrpc_conn_bundle *bundle;	/* virtual connection bundle */
	struct rxrpc_connection	*conn;		/* exclusive virtual connection */
	struct list_head	listen_link;	/* link in the local endpoint's listen list */
	struct list_head	secureq;	/* calls awaiting connection security clearance */
	struct list_head	acceptq;	/* calls awaiting acceptance */
	struct key		*key;		/* security for this socket */
	struct key		*securities;	/* list of server security descriptors */
	struct rb_root		calls;		/* outstanding calls on this socket */
	unsigned long		flags;
#define RXRPC_SOCK_EXCLUSIVE_CONN	1	/* exclusive connection for a client socket */
	rwlock_t		call_lock;	/* lock for calls */
	u32			min_sec_level;	/* minimum security level */
#define RXRPC_SECURITY_MAX	RXRPC_SECURITY_ENCRYPT
	struct sockaddr_rxrpc	srx;		/* local address */
	sa_family_t		proto;		/* protocol created with */
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
	void (*prime_packet_security)(struct rxrpc_connection *);

	/* impose security on a packet */
	int (*secure_packet)(const struct rxrpc_call *,
			     struct sk_buff *,
			     size_t,
			     void *);

	/* verify the security on a received packet */
	int (*verify_packet)(const struct rxrpc_call *, struct sk_buff *,
			     u32 *);

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
 * RxRPC local transport endpoint definition
 * - matched by local port, address and protocol type
 */
struct rxrpc_local {
	struct socket		*socket;	/* my UDP socket */
	struct work_struct	destroyer;	/* endpoint destroyer */
	struct work_struct	acceptor;	/* incoming call processor */
	struct work_struct	rejecter;	/* packet reject writer */
	struct work_struct	event_processor; /* endpoint event processor */
	struct list_head	services;	/* services listening on this endpoint */
	struct list_head	link;		/* link in endpoint list */
	struct rw_semaphore	defrag_sem;	/* control re-enablement of IP DF bit */
	struct sk_buff_head	accept_queue;	/* incoming calls awaiting acceptance */
	struct sk_buff_head	reject_queue;	/* packets awaiting rejection */
	struct sk_buff_head	event_queue;	/* endpoint event packets awaiting processing */
	spinlock_t		lock;		/* access lock */
	rwlock_t		services_lock;	/* lock for services list */
	atomic_t		usage;
	int			debug_id;	/* debug ID for printks */
	volatile char		error_rcvd;	/* T if received ICMP error outstanding */
	struct sockaddr_rxrpc	srx;		/* local address */
};

/*
 * RxRPC remote transport endpoint definition
 * - matched by remote port, address and protocol type
 * - holds the connection ID counter for connections between the two endpoints
 */
struct rxrpc_peer {
	struct work_struct	destroyer;	/* peer destroyer */
	struct list_head	link;		/* link in master peer list */
	struct list_head	error_targets;	/* targets for net error distribution */
	spinlock_t		lock;		/* access lock */
	atomic_t		usage;
	unsigned int		if_mtu;		/* interface MTU for this peer */
	unsigned int		mtu;		/* network MTU for this peer */
	unsigned int		maxdata;	/* data size (MTU - hdrsize) */
	unsigned short		hdrsize;	/* header size (IP + UDP + RxRPC) */
	int			debug_id;	/* debug ID for printks */
	int			net_error;	/* network error distributed */
	struct sockaddr_rxrpc	srx;		/* remote address */

	/* calculated RTT cache */
#define RXRPC_RTT_CACHE_SIZE 32
	suseconds_t		rtt;		/* current RTT estimate (in uS) */
	unsigned int		rtt_point;	/* next entry at which to insert */
	unsigned int		rtt_usage;	/* amount of cache actually used */
	suseconds_t		rtt_cache[RXRPC_RTT_CACHE_SIZE]; /* calculated RTT cache */
};

/*
 * RxRPC point-to-point transport / connection manager definition
 * - handles a bundle of connections between two endpoints
 * - matched by { local, peer }
 */
struct rxrpc_transport {
	struct rxrpc_local	*local;		/* local transport endpoint */
	struct rxrpc_peer	*peer;		/* remote transport endpoint */
	struct work_struct	error_handler;	/* network error distributor */
	struct rb_root		bundles;	/* client connection bundles on this transport */
	struct rb_root		client_conns;	/* client connections on this transport */
	struct rb_root		server_conns;	/* server connections on this transport */
	struct list_head	link;		/* link in master session list */
	struct sk_buff_head	error_queue;	/* error packets awaiting processing */
	unsigned long		put_time;	/* time at which to reap */
	spinlock_t		client_lock;	/* client connection allocation lock */
	rwlock_t		conn_lock;	/* lock for active/dead connections */
	atomic_t		usage;
	int			debug_id;	/* debug ID for printks */
	unsigned int		conn_idcounter;	/* connection ID counter (client) */
};

/*
 * RxRPC client connection bundle
 * - matched by { transport, service_id, key }
 */
struct rxrpc_conn_bundle {
	struct rb_node		node;		/* node in transport's lookup tree */
	struct list_head	unused_conns;	/* unused connections in this bundle */
	struct list_head	avail_conns;	/* available connections in this bundle */
	struct list_head	busy_conns;	/* busy connections in this bundle */
	struct key		*key;		/* security for this bundle */
	wait_queue_head_t	chanwait;	/* wait for channel to become available */
	atomic_t		usage;
	int			debug_id;	/* debug ID for printks */
	unsigned short		num_conns;	/* number of connections in this bundle */
	u16			service_id;	/* Service ID for this bundle */
	u8			security_ix;	/* security type */
};

/*
 * RxRPC connection definition
 * - matched by { transport, service_id, conn_id, direction, key }
 * - each connection can only handle four simultaneous calls
 */
struct rxrpc_connection {
	struct rxrpc_transport	*trans;		/* transport session */
	struct rxrpc_conn_bundle *bundle;	/* connection bundle (client) */
	struct work_struct	processor;	/* connection event processor */
	struct rb_node		node;		/* node in transport's lookup tree */
	struct list_head	link;		/* link in master connection list */
	struct list_head	bundle_link;	/* link in bundle */
	struct rb_root		calls;		/* calls on this connection */
	struct sk_buff_head	rx_queue;	/* received conn-level packets */
	struct rxrpc_call	*channels[RXRPC_MAXCALLS]; /* channels (active calls) */
	const struct rxrpc_security *security;	/* applied security module */
	struct key		*key;		/* security for this connection (client) */
	struct key		*server_key;	/* security for this service */
	struct crypto_skcipher	*cipher;	/* encryption handle */
	struct rxrpc_crypt	csum_iv;	/* packet checksum base */
	unsigned long		events;
#define RXRPC_CONN_CHALLENGE	0		/* send challenge packet */
	unsigned long		put_time;	/* time at which to reap */
	rwlock_t		lock;		/* access lock */
	spinlock_t		state_lock;	/* state-change lock */
	atomic_t		usage;
	enum {					/* current state of connection */
		RXRPC_CONN_UNUSED,		/* - connection not yet attempted */
		RXRPC_CONN_CLIENT,		/* - client connection */
		RXRPC_CONN_SERVER_UNSECURED,	/* - server unsecured connection */
		RXRPC_CONN_SERVER_CHALLENGING,	/* - server challenging for security */
		RXRPC_CONN_SERVER,		/* - server secured connection */
		RXRPC_CONN_REMOTELY_ABORTED,	/* - conn aborted by peer */
		RXRPC_CONN_LOCALLY_ABORTED,	/* - conn aborted locally */
		RXRPC_CONN_NETWORK_ERROR,	/* - conn terminated by network error */
	} state;
	u32			local_abort;	/* local abort code */
	u32			remote_abort;	/* remote abort code */
	int			error;		/* local error incurred */
	int			debug_id;	/* debug ID for printks */
	unsigned int		call_counter;	/* call ID counter */
	atomic_t		serial;		/* packet serial number counter */
	atomic_t		hi_serial;	/* highest serial number received */
	u8			avail_calls;	/* number of calls available */
	u8			size_align;	/* data size alignment (for security) */
	u8			header_size;	/* rxrpc + security header size */
	u8			security_size;	/* security header size */
	u32			security_level;	/* security level negotiated */
	u32			security_nonce;	/* response re-use preventer */
	u32			epoch;		/* epoch of this connection */
	u32			cid;		/* connection ID */
	u16			service_id;	/* service ID for this connection */
	u8			security_ix;	/* security type */
	u8			in_clientflag;	/* RXRPC_CLIENT_INITIATED if we are server */
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
	RXRPC_CALL_PROC_BUSY,		/* the processor is busy */
	RXRPC_CALL_INIT_ACCEPT,		/* acceptance was initiated */
	RXRPC_CALL_HAS_USERID,		/* has a user ID attached */
	RXRPC_CALL_EXPECT_OOS,		/* expect out of sequence packets */
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
	struct rxrpc_connection	*conn;		/* connection carrying call */
	struct rxrpc_sock	*socket;	/* socket responsible */
	struct timer_list	lifetimer;	/* lifetime remaining on call */
	struct timer_list	deadspan;	/* reap timer for re-ACK'ing, etc  */
	struct timer_list	ack_timer;	/* ACK generation timer */
	struct timer_list	resend_timer;	/* Tx resend timer */
	struct work_struct	destroyer;	/* call destroyer */
	struct work_struct	processor;	/* packet processor and ACK generator */
	struct list_head	link;		/* link in master call list */
	struct list_head	error_link;	/* link in error distribution list */
	struct list_head	accept_link;	/* calls awaiting acceptance */
	struct rb_node		sock_node;	/* node in socket call tree */
	struct rb_node		conn_node;	/* node in connection call tree */
	struct sk_buff_head	rx_queue;	/* received packets */
	struct sk_buff_head	rx_oos_queue;	/* packets received out of sequence */
	struct sk_buff		*tx_pending;	/* Tx socket buffer being filled */
	wait_queue_head_t	tx_waitq;	/* wait for Tx window space to become available */
	unsigned long		user_call_ID;	/* user-defined call ID */
	unsigned long		creation_jif;	/* time of call creation */
	unsigned long		flags;
	unsigned long		events;
	spinlock_t		lock;
	rwlock_t		state_lock;	/* lock for state transition */
	atomic_t		usage;
	atomic_t		sequence;	/* Tx data packet sequence counter */
	u32			local_abort;	/* local abort code */
	u32			remote_abort;	/* remote abort code */
	int			error;		/* local error incurred */
	enum rxrpc_call_state	state : 8;	/* current state of call */
	int			debug_id;	/* debug ID for printks */
	u8			channel;	/* connection channel occupied by this call */

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
	rxrpc_serial_t		ackr_serial;	/* serial of packet being ACK'd */
	atomic_t		ackr_not_idle;	/* number of packets in Rx queue */

	/* received packet records, 1 bit per record */
#define RXRPC_ACKR_WINDOW_ASZ DIV_ROUND_UP(RXRPC_MAXACKS, BITS_PER_LONG)
	unsigned long		ackr_window[RXRPC_ACKR_WINDOW_ASZ + 1];

	struct hlist_node	hash_node;
	unsigned long		hash_key;	/* Full hash key */
	u8			in_clientflag;	/* Copy of conn->in_clientflag for hashing */
	struct rxrpc_local	*local;		/* Local endpoint. Used for hashing. */
	sa_family_t		proto;		/* Frame protocol */
	u32			call_id;	/* call ID on connection  */
	u32			cid;		/* connection ID plus channel index */
	u32			epoch;		/* epoch of this connection */
	u16			service_id;	/* service ID */
	union {					/* Peer IP address for hashing */
		__be32	ipv4_addr;
		__u8	ipv6_addr[16];		/* Anticipates eventual IPv6 support */
	} peer_ip;
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

/*
 * af_rxrpc.c
 */
extern atomic_t rxrpc_n_skbs;
extern u32 rxrpc_epoch;
extern atomic_t rxrpc_debug_id;
extern struct workqueue_struct *rxrpc_workqueue;

/*
 * ar-accept.c
 */
void rxrpc_accept_incoming_calls(struct work_struct *);
struct rxrpc_call *rxrpc_accept_call(struct rxrpc_sock *, unsigned long);
int rxrpc_reject_call(struct rxrpc_sock *);

/*
 * ar-ack.c
 */
void __rxrpc_propose_ACK(struct rxrpc_call *, u8, u32, bool);
void rxrpc_propose_ACK(struct rxrpc_call *, u8, u32, bool);
void rxrpc_process_call(struct work_struct *);

/*
 * ar-call.c
 */
extern unsigned int rxrpc_max_call_lifetime;
extern unsigned int rxrpc_dead_call_expiry;
extern struct kmem_cache *rxrpc_call_jar;
extern struct list_head rxrpc_calls;
extern rwlock_t rxrpc_call_lock;

struct rxrpc_call *rxrpc_find_call_hash(struct rxrpc_host_header *,
					void *, sa_family_t, const void *);
struct rxrpc_call *rxrpc_get_client_call(struct rxrpc_sock *,
					 struct rxrpc_transport *,
					 struct rxrpc_conn_bundle *,
					 unsigned long, int, gfp_t);
struct rxrpc_call *rxrpc_incoming_call(struct rxrpc_sock *,
				       struct rxrpc_connection *,
				       struct rxrpc_host_header *);
struct rxrpc_call *rxrpc_find_server_call(struct rxrpc_sock *, unsigned long);
void rxrpc_release_call(struct rxrpc_call *);
void rxrpc_release_calls_on_socket(struct rxrpc_sock *);
void __rxrpc_put_call(struct rxrpc_call *);
void __exit rxrpc_destroy_all_calls(void);

/*
 * ar-connection.c
 */
extern unsigned int rxrpc_connection_expiry;
extern struct list_head rxrpc_connections;
extern rwlock_t rxrpc_connection_lock;

struct rxrpc_conn_bundle *rxrpc_get_bundle(struct rxrpc_sock *,
					   struct rxrpc_transport *,
					   struct key *, u16, gfp_t);
void rxrpc_put_bundle(struct rxrpc_transport *, struct rxrpc_conn_bundle *);
int rxrpc_connect_call(struct rxrpc_sock *, struct rxrpc_transport *,
		       struct rxrpc_conn_bundle *, struct rxrpc_call *, gfp_t);
void rxrpc_put_connection(struct rxrpc_connection *);
void __exit rxrpc_destroy_all_connections(void);
struct rxrpc_connection *rxrpc_find_connection(struct rxrpc_transport *,
					       struct rxrpc_host_header *);
extern struct rxrpc_connection *
rxrpc_incoming_connection(struct rxrpc_transport *, struct rxrpc_host_header *);

/*
 * ar-connevent.c
 */
void rxrpc_process_connection(struct work_struct *);
void rxrpc_reject_packet(struct rxrpc_local *, struct sk_buff *);
void rxrpc_reject_packets(struct work_struct *);

/*
 * ar-error.c
 */
void rxrpc_UDP_error_report(struct sock *);
void rxrpc_UDP_error_handler(struct work_struct *);

/*
 * ar-input.c
 */
void rxrpc_data_ready(struct sock *);
int rxrpc_queue_rcv_skb(struct rxrpc_call *, struct sk_buff *, bool, bool);
void rxrpc_fast_process_packet(struct rxrpc_call *, struct sk_buff *);

/*
 * ar-local.c
 */
extern rwlock_t rxrpc_local_lock;

struct rxrpc_local *rxrpc_lookup_local(struct sockaddr_rxrpc *);
void rxrpc_put_local(struct rxrpc_local *);
void __exit rxrpc_destroy_all_locals(void);

/*
 * ar-key.c
 */
extern struct key_type key_type_rxrpc;
extern struct key_type key_type_rxrpc_s;

int rxrpc_request_key(struct rxrpc_sock *, char __user *, int);
int rxrpc_server_keyring(struct rxrpc_sock *, char __user *, int);
int rxrpc_get_server_data_key(struct rxrpc_connection *, const void *, time_t,
			      u32);

/*
 * ar-output.c
 */
extern unsigned int rxrpc_resend_timeout;

int rxrpc_send_packet(struct rxrpc_transport *, struct sk_buff *);
int rxrpc_client_sendmsg(struct rxrpc_sock *, struct rxrpc_transport *,
			 struct msghdr *, size_t);
int rxrpc_server_sendmsg(struct rxrpc_sock *, struct msghdr *, size_t);

/*
 * ar-peer.c
 */
struct rxrpc_peer *rxrpc_get_peer(struct sockaddr_rxrpc *, gfp_t);
void rxrpc_put_peer(struct rxrpc_peer *);
struct rxrpc_peer *rxrpc_find_peer(struct rxrpc_local *, __be32, __be16);
void __exit rxrpc_destroy_all_peers(void);

/*
 * ar-proc.c
 */
extern const char *const rxrpc_call_states[];
extern const struct file_operations rxrpc_call_seq_fops;
extern const struct file_operations rxrpc_connection_seq_fops;

/*
 * ar-recvmsg.c
 */
void rxrpc_remove_user_ID(struct rxrpc_sock *, struct rxrpc_call *);
int rxrpc_recvmsg(struct socket *, struct msghdr *, size_t, int);

/*
 * ar-security.c
 */
int __init rxrpc_init_security(void);
void rxrpc_exit_security(void);
int rxrpc_init_client_conn_security(struct rxrpc_connection *);
int rxrpc_init_server_conn_security(struct rxrpc_connection *);

/*
 * ar-skbuff.c
 */
void rxrpc_packet_destructor(struct sk_buff *);

/*
 * ar-transport.c
 */
extern unsigned int rxrpc_transport_expiry;

struct rxrpc_transport *rxrpc_get_transport(struct rxrpc_local *,
					    struct rxrpc_peer *, gfp_t);
void rxrpc_put_transport(struct rxrpc_transport *);
void __exit rxrpc_destroy_all_transports(void);
struct rxrpc_transport *rxrpc_find_transport(struct rxrpc_local *,
					     struct rxrpc_peer *);

/*
 * insecure.c
 */
extern const struct rxrpc_security rxrpc_no_security;

/*
 * misc.c
 */
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
 * rxkad.c
 */
#ifdef CONFIG_RXKAD
extern const struct rxrpc_security rxkad;
#endif

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
		printk(KERN_ERR "\n");				\
		printk(KERN_ERR "RxRPC: Assertion failed\n");	\
		BUG();						\
	}							\
} while (0)

#define ASSERTCMP(X, OP, Y)						\
do {									\
	if (unlikely(!((X) OP (Y)))) {					\
		printk(KERN_ERR "\n");					\
		printk(KERN_ERR "RxRPC: Assertion failed\n");		\
		printk(KERN_ERR "%lu " #OP " %lu is false\n",		\
		       (unsigned long)(X), (unsigned long)(Y));		\
		printk(KERN_ERR "0x%lx " #OP " 0x%lx is false\n",	\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while (0)

#define ASSERTIF(C, X)						\
do {								\
	if (unlikely((C) && !(X))) {				\
		printk(KERN_ERR "\n");				\
		printk(KERN_ERR "RxRPC: Assertion failed\n");	\
		BUG();						\
	}							\
} while (0)

#define ASSERTIFCMP(C, X, OP, Y)					\
do {									\
	if (unlikely((C) && !((X) OP (Y)))) {				\
		printk(KERN_ERR "\n");					\
		printk(KERN_ERR "RxRPC: Assertion failed\n");		\
		printk(KERN_ERR "%lu " #OP " %lu is false\n",		\
		       (unsigned long)(X), (unsigned long)(Y));		\
		printk(KERN_ERR "0x%lx " #OP " 0x%lx is false\n",	\
		       (unsigned long)(X), (unsigned long)(Y));		\
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

/*
 * socket buffer accounting / leak finding
 */
static inline void __rxrpc_new_skb(struct sk_buff *skb, const char *fn)
{
	//_net("new skb %p %s [%d]", skb, fn, atomic_read(&rxrpc_n_skbs));
	//atomic_inc(&rxrpc_n_skbs);
}

#define rxrpc_new_skb(skb) __rxrpc_new_skb((skb), __func__)

static inline void __rxrpc_kill_skb(struct sk_buff *skb, const char *fn)
{
	//_net("kill skb %p %s [%d]", skb, fn, atomic_read(&rxrpc_n_skbs));
	//atomic_dec(&rxrpc_n_skbs);
}

#define rxrpc_kill_skb(skb) __rxrpc_kill_skb((skb), __func__)

static inline void __rxrpc_free_skb(struct sk_buff *skb, const char *fn)
{
	if (skb) {
		CHECK_SLAB_OKAY(&skb->users);
		//_net("free skb %p %s [%d]",
		//     skb, fn, atomic_read(&rxrpc_n_skbs));
		//atomic_dec(&rxrpc_n_skbs);
		kfree_skb(skb);
	}
}

#define rxrpc_free_skb(skb) __rxrpc_free_skb((skb), __func__)

static inline void rxrpc_purge_queue(struct sk_buff_head *list)
{
	struct sk_buff *skb;
	while ((skb = skb_dequeue((list))) != NULL)
		rxrpc_free_skb(skb);
}

static inline void __rxrpc_get_local(struct rxrpc_local *local, const char *f)
{
	CHECK_SLAB_OKAY(&local->usage);
	if (atomic_inc_return(&local->usage) == 1)
		printk("resurrected (%s)\n", f);
}

#define rxrpc_get_local(LOCAL) __rxrpc_get_local((LOCAL), __func__)

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
