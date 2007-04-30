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

#include <rxrpc/packet.h>

#if 0
#define CHECK_SLAB_OKAY(X)				     \
	BUG_ON(atomic_read((X)) >> (sizeof(atomic_t) - 2) == \
	       (POISON_FREE << 8 | POISON_FREE))
#else
#define CHECK_SLAB_OKAY(X) do {} while(0)
#endif

#define FCRYPT_BSIZE 8
struct rxrpc_crypt {
	union {
		u8	x[FCRYPT_BSIZE];
		u32	n[2];
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
	__be16			service_id;	/* service ID of local/remote service */
};

#define rxrpc_sk(__sk) container_of((__sk), struct rxrpc_sock, sk)

/*
 * RxRPC socket buffer private variables
 * - max 48 bytes (struct sk_buff::cb)
 */
struct rxrpc_skb_priv {
	struct rxrpc_call	*call;		/* call with which associated */
	unsigned long		resend_at;	/* time in jiffies at which to resend */
	union {
		unsigned	offset;		/* offset into buffer of next read */
		int		remain;		/* amount of space remaining for next write */
		u32		error;		/* network error code */
		bool		need_resend;	/* T if needs resending */
	};

	struct rxrpc_header	hdr;		/* RxRPC packet header from this packet */
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
	struct module		*owner;		/* providing module */
	struct list_head	link;		/* link in master list */
	const char		*name;		/* name of this service */
	u8			security_index;	/* security type provided */

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
	struct list_head	services;	/* services listening on this endpoint */
	struct list_head	link;		/* link in endpoint list */
	struct rw_semaphore	defrag_sem;	/* control re-enablement of IP DF bit */
	struct sk_buff_head	accept_queue;	/* incoming calls awaiting acceptance */
	struct sk_buff_head	reject_queue;	/* packets awaiting rejection */
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
	unsigned		if_mtu;		/* interface MTU for this peer */
	unsigned		mtu;		/* network MTU for this peer */
	unsigned		maxdata;	/* data size (MTU - hdrsize) */
	unsigned short		hdrsize;	/* header size (IP + UDP + RxRPC) */
	int			debug_id;	/* debug ID for printks */
	int			net_error;	/* network error distributed */
	struct sockaddr_rxrpc	srx;		/* remote address */

	/* calculated RTT cache */
#define RXRPC_RTT_CACHE_SIZE 32
	suseconds_t		rtt;		/* current RTT estimate (in uS) */
	unsigned		rtt_point;	/* next entry at which to insert */
	unsigned		rtt_usage;	/* amount of cache actually used */
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
	time_t			put_time;	/* time at which to reap */
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
	__be16			service_id;	/* service ID */
	uint8_t			security_ix;	/* security type */
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
	struct rxrpc_security	*security;	/* applied security module */
	struct key		*key;		/* security for this connection (client) */
	struct key		*server_key;	/* security for this service */
	struct crypto_blkcipher	*cipher;	/* encryption handle */
	struct rxrpc_crypt	csum_iv;	/* packet checksum base */
	unsigned long		events;
#define RXRPC_CONN_CHALLENGE	0		/* send challenge packet */
	time_t			put_time;	/* time at which to reap */
	rwlock_t		lock;		/* access lock */
	spinlock_t		state_lock;	/* state-change lock */
	atomic_t		usage;
	u32			real_conn_id;	/* connection ID (host-endian) */
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
	int			error;		/* error code for local abort */
	int			debug_id;	/* debug ID for printks */
	unsigned		call_counter;	/* call ID counter */
	atomic_t		serial;		/* packet serial number counter */
	atomic_t		hi_serial;	/* highest serial number received */
	u8			avail_calls;	/* number of calls available */
	u8			size_align;	/* data size alignment (for security) */
	u8			header_size;	/* rxrpc + security header size */
	u8			security_size;	/* security header size */
	u32			security_level;	/* security level negotiated */
	u32			security_nonce;	/* response re-use preventer */

	/* the following are all in net order */
	__be32			epoch;		/* epoch of this connection */
	__be32			cid;		/* connection ID */
	__be16			service_id;	/* service ID */
	u8			security_ix;	/* security type */
	u8			in_clientflag;	/* RXRPC_CLIENT_INITIATED if we are server */
	u8			out_clientflag;	/* RXRPC_CLIENT_INITIATED if we are client */
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
#define RXRPC_CALL_RELEASED	0	/* call has been released - no more message to userspace */
#define RXRPC_CALL_TERMINAL_MSG	1	/* call has given the socket its final message */
#define RXRPC_CALL_RCVD_LAST	2	/* all packets received */
#define RXRPC_CALL_RUN_RTIMER	3	/* Tx resend timer started */
#define RXRPC_CALL_TX_SOFT_ACK	4	/* sent some soft ACKs */
#define RXRPC_CALL_PROC_BUSY	5	/* the processor is busy */
#define RXRPC_CALL_INIT_ACCEPT	6	/* acceptance was initiated */
#define RXRPC_CALL_HAS_USERID	7	/* has a user ID attached */
#define RXRPC_CALL_EXPECT_OOS	8	/* expect out of sequence packets */
	unsigned long		events;
#define RXRPC_CALL_RCVD_ACKALL	0	/* ACKALL or reply received */
#define RXRPC_CALL_RCVD_BUSY	1	/* busy packet received */
#define RXRPC_CALL_RCVD_ABORT	2	/* abort packet received */
#define RXRPC_CALL_RCVD_ERROR	3	/* network error received */
#define RXRPC_CALL_ACK_FINAL	4	/* need to generate final ACK (and release call) */
#define RXRPC_CALL_ACK		5	/* need to generate ACK */
#define RXRPC_CALL_REJECT_BUSY	6	/* need to generate busy message */
#define RXRPC_CALL_ABORT	7	/* need to generate abort */
#define RXRPC_CALL_CONN_ABORT	8	/* local connection abort generated */
#define RXRPC_CALL_RESEND_TIMER	9	/* Tx resend timer expired */
#define RXRPC_CALL_RESEND	10	/* Tx resend required */
#define RXRPC_CALL_DRAIN_RX_OOS	11	/* drain the Rx out of sequence queue */
#define RXRPC_CALL_LIFE_TIMER	12	/* call's lifetimer ran out */
#define RXRPC_CALL_ACCEPTED	13	/* incoming call accepted by userspace app */
#define RXRPC_CALL_SECURED	14	/* incoming call's connection is now secure */
#define RXRPC_CALL_POST_ACCEPT	15	/* need to post an "accept?" message to the app */
#define RXRPC_CALL_RELEASE	16	/* need to release the call's resources */

	spinlock_t		lock;
	rwlock_t		state_lock;	/* lock for state transition */
	atomic_t		usage;
	atomic_t		sequence;	/* Tx data packet sequence counter */
	u32			abort_code;	/* local/remote abort code */
	enum {					/* current state of call */
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
	} state;
	int			debug_id;	/* debug ID for printks */
	u8			channel;	/* connection channel occupied by this call */

	/* transmission-phase ACK management */
	uint8_t			acks_head;	/* offset into window of first entry */
	uint8_t			acks_tail;	/* offset into window of last entry */
	uint8_t			acks_winsz;	/* size of un-ACK'd window */
	uint8_t			acks_unacked;	/* lowest unacked packet in last ACK received */
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
	rxrpc_seq_net_t		ackr_prev_seq;	/* previous sequence number received */
	uint8_t			ackr_reason;	/* reason to ACK */
	__be32			ackr_serial;	/* serial of packet being ACK'd */
	atomic_t		ackr_not_idle;	/* number of packets in Rx queue */

	/* received packet records, 1 bit per record */
#define RXRPC_ACKR_WINDOW_ASZ DIV_ROUND_UP(RXRPC_MAXACKS, BITS_PER_LONG)
	unsigned long		ackr_window[RXRPC_ACKR_WINDOW_ASZ + 1];

	/* the following should all be in net order */
	__be32			cid;		/* connection ID + channel index  */
	__be32			call_id;	/* call ID on connection  */
};

/*
 * RxRPC key for Kerberos (type-2 security)
 */
struct rxkad_key {
	u16	security_index;		/* RxRPC header security index */
	u16	ticket_len;		/* length of ticket[] */
	u32	expiry;			/* time at which expires */
	u32	kvno;			/* key version number */
	u8	session_key[8];		/* DES session key */
	u8	ticket[0];		/* the encrypted ticket */
};

struct rxrpc_key_payload {
	struct rxkad_key k;
};

/*
 * locally abort an RxRPC call
 */
static inline void rxrpc_abort_call(struct rxrpc_call *call, u32 abort_code)
{
	write_lock_bh(&call->state_lock);
	if (call->state < RXRPC_CALL_COMPLETE) {
		call->abort_code = abort_code;
		call->state = RXRPC_CALL_LOCALLY_ABORTED;
		set_bit(RXRPC_CALL_ABORT, &call->events);
	}
	write_unlock_bh(&call->state_lock);
}

/*
 * af_rxrpc.c
 */
extern atomic_t rxrpc_n_skbs;
extern __be32 rxrpc_epoch;
extern atomic_t rxrpc_debug_id;
extern struct workqueue_struct *rxrpc_workqueue;

/*
 * ar-accept.c
 */
extern void rxrpc_accept_incoming_calls(struct work_struct *);
extern struct rxrpc_call *rxrpc_accept_call(struct rxrpc_sock *,
					    unsigned long);
extern int rxrpc_reject_call(struct rxrpc_sock *);

/*
 * ar-ack.c
 */
extern void __rxrpc_propose_ACK(struct rxrpc_call *, uint8_t, __be32, bool);
extern void rxrpc_propose_ACK(struct rxrpc_call *, uint8_t, __be32, bool);
extern void rxrpc_process_call(struct work_struct *);

/*
 * ar-call.c
 */
extern struct kmem_cache *rxrpc_call_jar;
extern struct list_head rxrpc_calls;
extern rwlock_t rxrpc_call_lock;

extern struct rxrpc_call *rxrpc_get_client_call(struct rxrpc_sock *,
						struct rxrpc_transport *,
						struct rxrpc_conn_bundle *,
						unsigned long, int, gfp_t);
extern struct rxrpc_call *rxrpc_incoming_call(struct rxrpc_sock *,
					      struct rxrpc_connection *,
					      struct rxrpc_header *, gfp_t);
extern struct rxrpc_call *rxrpc_find_server_call(struct rxrpc_sock *,
						 unsigned long);
extern void rxrpc_release_call(struct rxrpc_call *);
extern void rxrpc_release_calls_on_socket(struct rxrpc_sock *);
extern void __rxrpc_put_call(struct rxrpc_call *);
extern void __exit rxrpc_destroy_all_calls(void);

/*
 * ar-connection.c
 */
extern struct list_head rxrpc_connections;
extern rwlock_t rxrpc_connection_lock;

extern struct rxrpc_conn_bundle *rxrpc_get_bundle(struct rxrpc_sock *,
						  struct rxrpc_transport *,
						  struct key *,
						  __be16, gfp_t);
extern void rxrpc_put_bundle(struct rxrpc_transport *,
			     struct rxrpc_conn_bundle *);
extern int rxrpc_connect_call(struct rxrpc_sock *, struct rxrpc_transport *,
			      struct rxrpc_conn_bundle *, struct rxrpc_call *,
			      gfp_t);
extern void rxrpc_put_connection(struct rxrpc_connection *);
extern void __exit rxrpc_destroy_all_connections(void);
extern struct rxrpc_connection *rxrpc_find_connection(struct rxrpc_transport *,
						      struct rxrpc_header *);
extern struct rxrpc_connection *
rxrpc_incoming_connection(struct rxrpc_transport *, struct rxrpc_header *,
			  gfp_t);

/*
 * ar-connevent.c
 */
extern void rxrpc_process_connection(struct work_struct *);
extern void rxrpc_reject_packet(struct rxrpc_local *, struct sk_buff *);
extern void rxrpc_reject_packets(struct work_struct *);

/*
 * ar-error.c
 */
extern void rxrpc_UDP_error_report(struct sock *);
extern void rxrpc_UDP_error_handler(struct work_struct *);

/*
 * ar-input.c
 */
extern unsigned long rxrpc_ack_timeout;
extern const char *rxrpc_pkts[];

extern void rxrpc_data_ready(struct sock *, int);
extern int rxrpc_queue_rcv_skb(struct rxrpc_call *, struct sk_buff *, bool,
			       bool);
extern void rxrpc_fast_process_packet(struct rxrpc_call *, struct sk_buff *);

/*
 * ar-local.c
 */
extern rwlock_t rxrpc_local_lock;
extern struct rxrpc_local *rxrpc_lookup_local(struct sockaddr_rxrpc *);
extern void rxrpc_put_local(struct rxrpc_local *);
extern void __exit rxrpc_destroy_all_locals(void);

/*
 * ar-key.c
 */
extern struct key_type key_type_rxrpc;
extern struct key_type key_type_rxrpc_s;

extern int rxrpc_request_key(struct rxrpc_sock *, char __user *, int);
extern int rxrpc_server_keyring(struct rxrpc_sock *, char __user *, int);
extern int rxrpc_get_server_data_key(struct rxrpc_connection *, const void *,
				     time_t, u32);

/*
 * ar-output.c
 */
extern int rxrpc_resend_timeout;

extern int rxrpc_send_packet(struct rxrpc_transport *, struct sk_buff *);
extern int rxrpc_client_sendmsg(struct kiocb *, struct rxrpc_sock *,
				struct rxrpc_transport *, struct msghdr *,
				size_t);
extern int rxrpc_server_sendmsg(struct kiocb *, struct rxrpc_sock *,
				struct msghdr *, size_t);

/*
 * ar-peer.c
 */
extern struct rxrpc_peer *rxrpc_get_peer(struct sockaddr_rxrpc *, gfp_t);
extern void rxrpc_put_peer(struct rxrpc_peer *);
extern struct rxrpc_peer *rxrpc_find_peer(struct rxrpc_local *,
					  __be32, __be16);
extern void __exit rxrpc_destroy_all_peers(void);

/*
 * ar-proc.c
 */
extern const char *rxrpc_call_states[];
extern struct file_operations rxrpc_call_seq_fops;
extern struct file_operations rxrpc_connection_seq_fops;

/*
 * ar-recvmsg.c
 */
extern void rxrpc_remove_user_ID(struct rxrpc_sock *, struct rxrpc_call *);
extern int rxrpc_recvmsg(struct kiocb *, struct socket *, struct msghdr *,
			 size_t, int);

/*
 * ar-security.c
 */
extern int rxrpc_register_security(struct rxrpc_security *);
extern void rxrpc_unregister_security(struct rxrpc_security *);
extern int rxrpc_init_client_conn_security(struct rxrpc_connection *);
extern int rxrpc_init_server_conn_security(struct rxrpc_connection *);
extern int rxrpc_secure_packet(const struct rxrpc_call *, struct sk_buff *,
			       size_t, void *);
extern int rxrpc_verify_packet(const struct rxrpc_call *, struct sk_buff *,
			       u32 *);
extern void rxrpc_clear_conn_security(struct rxrpc_connection *);

/*
 * ar-skbuff.c
 */
extern void rxrpc_packet_destructor(struct sk_buff *);

/*
 * ar-transport.c
 */
extern struct rxrpc_transport *rxrpc_get_transport(struct rxrpc_local *,
						   struct rxrpc_peer *,
						   gfp_t);
extern void rxrpc_put_transport(struct rxrpc_transport *);
extern void __exit rxrpc_destroy_all_transports(void);
extern struct rxrpc_transport *rxrpc_find_transport(struct rxrpc_local *,
						    struct rxrpc_peer *);

/*
 * debug tracing
 */
extern unsigned rxrpc_debug;

#define dbgprintk(FMT,...) \
	printk("[%x%-6.6s] "FMT"\n", smp_processor_id(), current->comm ,##__VA_ARGS__)

/* make sure we maintain the format strings, even when debugging is disabled */
static inline __attribute__((format(printf,1,2)))
void _dbprintk(const char *fmt, ...)
{
}

#define kenter(FMT,...)	dbgprintk("==> %s("FMT")",__FUNCTION__ ,##__VA_ARGS__)
#define kleave(FMT,...)	dbgprintk("<== %s()"FMT"",__FUNCTION__ ,##__VA_ARGS__)
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
#define _enter(FMT,...)	_dbprintk("==> %s("FMT")",__FUNCTION__ ,##__VA_ARGS__)
#define _leave(FMT,...)	_dbprintk("<== %s()"FMT"",__FUNCTION__ ,##__VA_ARGS__)
#define _debug(FMT,...)	_dbprintk("    "FMT ,##__VA_ARGS__)
#define _proto(FMT,...)	_dbprintk("### "FMT ,##__VA_ARGS__)
#define _net(FMT,...)	_dbprintk("@@@ "FMT ,##__VA_ARGS__)
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
} while(0)

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
} while(0)

#define ASSERTIF(C, X)						\
do {								\
	if (unlikely((C) && !(X))) {				\
		printk(KERN_ERR "\n");				\
		printk(KERN_ERR "RxRPC: Assertion failed\n");	\
		BUG();						\
	}							\
} while(0)

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
} while(0)

#else

#define ASSERT(X)				\
do {						\
} while(0)

#define ASSERTCMP(X, OP, Y)			\
do {						\
} while(0)

#define ASSERTIF(C, X)				\
do {						\
} while(0)

#define ASSERTIFCMP(C, X, OP, Y)		\
do {						\
} while(0)

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
} while(0)

#define rxrpc_put_call(CALL)				\
do {							\
	__rxrpc_put_call(CALL);				\
} while(0)
