#ifndef _RDS_RDS_H
#define _RDS_RDS_H

#include <net/sock.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <rdma/rdma_cm.h>
#include <linux/mutex.h>
#include <linux/rds.h>
#include <linux/rhashtable.h>

#include "info.h"

/*
 * RDS Network protocol version
 */
#define RDS_PROTOCOL_3_0	0x0300
#define RDS_PROTOCOL_3_1	0x0301
#define RDS_PROTOCOL_VERSION	RDS_PROTOCOL_3_1
#define RDS_PROTOCOL_MAJOR(v)	((v) >> 8)
#define RDS_PROTOCOL_MINOR(v)	((v) & 255)
#define RDS_PROTOCOL(maj, min)	(((maj) << 8) | min)

/*
 * XXX randomly chosen, but at least seems to be unused:
 * #               18464-18768 Unassigned
 * We should do better.  We want a reserved port to discourage unpriv'ed
 * userspace from listening.
 */
#define RDS_PORT	18634

#ifdef ATOMIC64_INIT
#define KERNEL_HAS_ATOMIC64
#endif

#ifdef RDS_DEBUG
#define rdsdebug(fmt, args...) pr_debug("%s(): " fmt, __func__ , ##args)
#else
/* sigh, pr_debug() causes unused variable warnings */
static inline __printf(1, 2)
void rdsdebug(char *fmt, ...)
{
}
#endif

/* XXX is there one of these somewhere? */
#define ceil(x, y) \
	({ unsigned long __x = (x), __y = (y); (__x + __y - 1) / __y; })

#define RDS_FRAG_SHIFT	12
#define RDS_FRAG_SIZE	((unsigned int)(1 << RDS_FRAG_SHIFT))

/* Used to limit both RDMA and non-RDMA RDS message to 1MB */
#define RDS_MAX_MSG_SIZE	((unsigned int)(1 << 20))

#define RDS_CONG_MAP_BYTES	(65536 / 8)
#define RDS_CONG_MAP_PAGES	(PAGE_ALIGN(RDS_CONG_MAP_BYTES) / PAGE_SIZE)
#define RDS_CONG_MAP_PAGE_BITS	(PAGE_SIZE * 8)

struct rds_cong_map {
	struct rb_node		m_rb_node;
	__be32			m_addr;
	wait_queue_head_t	m_waitq;
	struct list_head	m_conn_list;
	unsigned long		m_page_addrs[RDS_CONG_MAP_PAGES];
};


/*
 * This is how we will track the connection state:
 * A connection is always in one of the following
 * states. Updates to the state are atomic and imply
 * a memory barrier.
 */
enum {
	RDS_CONN_DOWN = 0,
	RDS_CONN_CONNECTING,
	RDS_CONN_DISCONNECTING,
	RDS_CONN_UP,
	RDS_CONN_RESETTING,
	RDS_CONN_ERROR,
};

/* Bits for c_flags */
#define RDS_LL_SEND_FULL	0
#define RDS_RECONNECT_PENDING	1
#define RDS_IN_XMIT		2
#define RDS_RECV_REFILL		3

/* Max number of multipaths per RDS connection. Must be a power of 2 */
#define	RDS_MPATH_WORKERS	8
#define	RDS_MPATH_HASH(rs, n) (jhash_1word((rs)->rs_bound_port, \
			       (rs)->rs_hash_initval) & ((n) - 1))

/* Per mpath connection state */
struct rds_conn_path {
	struct rds_connection	*cp_conn;
	struct rds_message	*cp_xmit_rm;
	unsigned long		cp_xmit_sg;
	unsigned int		cp_xmit_hdr_off;
	unsigned int		cp_xmit_data_off;
	unsigned int		cp_xmit_atomic_sent;
	unsigned int		cp_xmit_rdma_sent;
	unsigned int		cp_xmit_data_sent;

	spinlock_t		cp_lock;		/* protect msg queues */
	u64			cp_next_tx_seq;
	struct list_head	cp_send_queue;
	struct list_head	cp_retrans;

	u64			cp_next_rx_seq;

	void			*cp_transport_data;

	atomic_t		cp_state;
	unsigned long		cp_send_gen;
	unsigned long		cp_flags;
	unsigned long		cp_reconnect_jiffies;
	struct delayed_work	cp_send_w;
	struct delayed_work	cp_recv_w;
	struct delayed_work	cp_conn_w;
	struct work_struct	cp_down_w;
	struct mutex		cp_cm_lock;	/* protect cp_state & cm */
	wait_queue_head_t	cp_waitq;

	unsigned int		cp_unacked_packets;
	unsigned int		cp_unacked_bytes;
	unsigned int		cp_outgoing:1,
				cp_pad_to_32:31;
	unsigned int		cp_index;
};

/* One rds_connection per RDS address pair */
struct rds_connection {
	struct hlist_node	c_hash_node;
	__be32			c_laddr;
	__be32			c_faddr;
	unsigned int		c_loopback:1,
				c_ping_triggered:1,
				c_pad_to_32:30;
	int			c_npaths;
	struct rds_connection	*c_passive;
	struct rds_transport	*c_trans;

	struct rds_cong_map	*c_lcong;
	struct rds_cong_map	*c_fcong;

	/* Protocol version */
	unsigned int		c_version;
	struct net		*c_net;

	struct list_head	c_map_item;
	unsigned long		c_map_queued;

	struct rds_conn_path	c_path[RDS_MPATH_WORKERS];
	wait_queue_head_t	c_hs_waitq; /* handshake waitq */

	u32			c_my_gen_num;
	u32			c_peer_gen_num;
};

static inline
struct net *rds_conn_net(struct rds_connection *conn)
{
	return conn->c_net;
}

static inline
void rds_conn_net_set(struct rds_connection *conn, struct net *net)
{
	conn->c_net = get_net(net);
}

#define RDS_FLAG_CONG_BITMAP	0x01
#define RDS_FLAG_ACK_REQUIRED	0x02
#define RDS_FLAG_RETRANSMITTED	0x04
#define RDS_MAX_ADV_CREDIT	255

/* RDS_FLAG_PROBE_PORT is the reserved sport used for sending a ping
 * probe to exchange control information before establishing a connection.
 * Currently the control information that is exchanged is the number of
 * supported paths. If the peer is a legacy (older kernel revision) peer,
 * it would return a pong message without additional control information
 * that would then alert the sender that the peer was an older rev.
 */
#define RDS_FLAG_PROBE_PORT	1
#define	RDS_HS_PROBE(sport, dport) \
		((sport == RDS_FLAG_PROBE_PORT && dport == 0) || \
		 (sport == 0 && dport == RDS_FLAG_PROBE_PORT))
/*
 * Maximum space available for extension headers.
 */
#define RDS_HEADER_EXT_SPACE	16

struct rds_header {
	__be64	h_sequence;
	__be64	h_ack;
	__be32	h_len;
	__be16	h_sport;
	__be16	h_dport;
	u8	h_flags;
	u8	h_credit;
	u8	h_padding[4];
	__sum16	h_csum;

	u8	h_exthdr[RDS_HEADER_EXT_SPACE];
};

/*
 * Reserved - indicates end of extensions
 */
#define RDS_EXTHDR_NONE		0

/*
 * This extension header is included in the very
 * first message that is sent on a new connection,
 * and identifies the protocol level. This will help
 * rolling updates if a future change requires breaking
 * the protocol.
 * NB: This is no longer true for IB, where we do a version
 * negotiation during the connection setup phase (protocol
 * version information is included in the RDMA CM private data).
 */
#define RDS_EXTHDR_VERSION	1
struct rds_ext_header_version {
	__be32			h_version;
};

/*
 * This extension header is included in the RDS message
 * chasing an RDMA operation.
 */
#define RDS_EXTHDR_RDMA		2
struct rds_ext_header_rdma {
	__be32			h_rdma_rkey;
};

/*
 * This extension header tells the peer about the
 * destination <R_Key,offset> of the requested RDMA
 * operation.
 */
#define RDS_EXTHDR_RDMA_DEST	3
struct rds_ext_header_rdma_dest {
	__be32			h_rdma_rkey;
	__be32			h_rdma_offset;
};

/* Extension header announcing number of paths.
 * Implicit length = 2 bytes.
 */
#define RDS_EXTHDR_NPATHS	5
#define RDS_EXTHDR_GEN_NUM	6

#define __RDS_EXTHDR_MAX	16 /* for now */
#define RDS_RX_MAX_TRACES	(RDS_MSG_RX_DGRAM_TRACE_MAX + 1)
#define	RDS_MSG_RX_HDR		0
#define	RDS_MSG_RX_START	1
#define	RDS_MSG_RX_END		2
#define	RDS_MSG_RX_CMSG		3

struct rds_incoming {
	atomic_t		i_refcount;
	struct list_head	i_item;
	struct rds_connection	*i_conn;
	struct rds_conn_path	*i_conn_path;
	struct rds_header	i_hdr;
	unsigned long		i_rx_jiffies;
	__be32			i_saddr;

	rds_rdma_cookie_t	i_rdma_cookie;
	struct timeval		i_rx_tstamp;
	u64			i_rx_lat_trace[RDS_RX_MAX_TRACES];
};

struct rds_mr {
	struct rb_node		r_rb_node;
	atomic_t		r_refcount;
	u32			r_key;

	/* A copy of the creation flags */
	unsigned int		r_use_once:1;
	unsigned int		r_invalidate:1;
	unsigned int		r_write:1;

	/* This is for RDS_MR_DEAD.
	 * It would be nice & consistent to make this part of the above
	 * bit field here, but we need to use test_and_set_bit.
	 */
	unsigned long		r_state;
	struct rds_sock		*r_sock; /* back pointer to the socket that owns us */
	struct rds_transport	*r_trans;
	void			*r_trans_private;
};

/* Flags for mr->r_state */
#define RDS_MR_DEAD		0

static inline rds_rdma_cookie_t rds_rdma_make_cookie(u32 r_key, u32 offset)
{
	return r_key | (((u64) offset) << 32);
}

static inline u32 rds_rdma_cookie_key(rds_rdma_cookie_t cookie)
{
	return cookie;
}

static inline u32 rds_rdma_cookie_offset(rds_rdma_cookie_t cookie)
{
	return cookie >> 32;
}

/* atomic operation types */
#define RDS_ATOMIC_TYPE_CSWP		0
#define RDS_ATOMIC_TYPE_FADD		1

/*
 * m_sock_item and m_conn_item are on lists that are serialized under
 * conn->c_lock.  m_sock_item has additional meaning in that once it is empty
 * the message will not be put back on the retransmit list after being sent.
 * messages that are canceled while being sent rely on this.
 *
 * m_inc is used by loopback so that it can pass an incoming message straight
 * back up into the rx path.  It embeds a wire header which is also used by
 * the send path, which is kind of awkward.
 *
 * m_sock_item indicates the message's presence on a socket's send or receive
 * queue.  m_rs will point to that socket.
 *
 * m_daddr is used by cancellation to prune messages to a given destination.
 *
 * The RDS_MSG_ON_SOCK and RDS_MSG_ON_CONN flags are used to avoid lock
 * nesting.  As paths iterate over messages on a sock, or conn, they must
 * also lock the conn, or sock, to remove the message from those lists too.
 * Testing the flag to determine if the message is still on the lists lets
 * us avoid testing the list_head directly.  That means each path can use
 * the message's list_head to keep it on a local list while juggling locks
 * without confusing the other path.
 *
 * m_ack_seq is an optional field set by transports who need a different
 * sequence number range to invalidate.  They can use this in a callback
 * that they pass to rds_send_drop_acked() to see if each message has been
 * acked.  The HAS_ACK_SEQ flag can be used to detect messages which haven't
 * had ack_seq set yet.
 */
#define RDS_MSG_ON_SOCK		1
#define RDS_MSG_ON_CONN		2
#define RDS_MSG_HAS_ACK_SEQ	3
#define RDS_MSG_ACK_REQUIRED	4
#define RDS_MSG_RETRANSMITTED	5
#define RDS_MSG_MAPPED		6
#define RDS_MSG_PAGEVEC		7
#define RDS_MSG_FLUSH		8

struct rds_message {
	atomic_t		m_refcount;
	struct list_head	m_sock_item;
	struct list_head	m_conn_item;
	struct rds_incoming	m_inc;
	u64			m_ack_seq;
	__be32			m_daddr;
	unsigned long		m_flags;

	/* Never access m_rs without holding m_rs_lock.
	 * Lock nesting is
	 *  rm->m_rs_lock
	 *   -> rs->rs_lock
	 */
	spinlock_t		m_rs_lock;
	wait_queue_head_t	m_flush_wait;

	struct rds_sock		*m_rs;

	/* cookie to send to remote, in rds header */
	rds_rdma_cookie_t	m_rdma_cookie;

	unsigned int		m_used_sgs;
	unsigned int		m_total_sgs;

	void			*m_final_op;

	struct {
		struct rm_atomic_op {
			int			op_type;
			union {
				struct {
					uint64_t	compare;
					uint64_t	swap;
					uint64_t	compare_mask;
					uint64_t	swap_mask;
				} op_m_cswp;
				struct {
					uint64_t	add;
					uint64_t	nocarry_mask;
				} op_m_fadd;
			};

			u32			op_rkey;
			u64			op_remote_addr;
			unsigned int		op_notify:1;
			unsigned int		op_recverr:1;
			unsigned int		op_mapped:1;
			unsigned int		op_silent:1;
			unsigned int		op_active:1;
			struct scatterlist	*op_sg;
			struct rds_notifier	*op_notifier;

			struct rds_mr		*op_rdma_mr;
		} atomic;
		struct rm_rdma_op {
			u32			op_rkey;
			u64			op_remote_addr;
			unsigned int		op_write:1;
			unsigned int		op_fence:1;
			unsigned int		op_notify:1;
			unsigned int		op_recverr:1;
			unsigned int		op_mapped:1;
			unsigned int		op_silent:1;
			unsigned int		op_active:1;
			unsigned int		op_bytes;
			unsigned int		op_nents;
			unsigned int		op_count;
			struct scatterlist	*op_sg;
			struct rds_notifier	*op_notifier;

			struct rds_mr		*op_rdma_mr;
		} rdma;
		struct rm_data_op {
			unsigned int		op_active:1;
			unsigned int		op_notify:1;
			unsigned int		op_nents;
			unsigned int		op_count;
			unsigned int		op_dmasg;
			unsigned int		op_dmaoff;
			struct scatterlist	*op_sg;
		} data;
	};
};

/*
 * The RDS notifier is used (optionally) to tell the application about
 * completed RDMA operations. Rather than keeping the whole rds message
 * around on the queue, we allocate a small notifier that is put on the
 * socket's notifier_list. Notifications are delivered to the application
 * through control messages.
 */
struct rds_notifier {
	struct list_head	n_list;
	uint64_t		n_user_token;
	int			n_status;
};

/**
 * struct rds_transport -  transport specific behavioural hooks
 *
 * @xmit: .xmit is called by rds_send_xmit() to tell the transport to send
 *        part of a message.  The caller serializes on the send_sem so this
 *        doesn't need to be reentrant for a given conn.  The header must be
 *        sent before the data payload.  .xmit must be prepared to send a
 *        message with no data payload.  .xmit should return the number of
 *        bytes that were sent down the connection, including header bytes.
 *        Returning 0 tells the caller that it doesn't need to perform any
 *        additional work now.  This is usually the case when the transport has
 *        filled the sending queue for its connection and will handle
 *        triggering the rds thread to continue the send when space becomes
 *        available.  Returning -EAGAIN tells the caller to retry the send
 *        immediately.  Returning -ENOMEM tells the caller to retry the send at
 *        some point in the future.
 *
 * @conn_shutdown: conn_shutdown stops traffic on the given connection.  Once
 *                 it returns the connection can not call rds_recv_incoming().
 *                 This will only be called once after conn_connect returns
 *                 non-zero success and will The caller serializes this with
 *                 the send and connecting paths (xmit_* and conn_*).  The
 *                 transport is responsible for other serialization, including
 *                 rds_recv_incoming().  This is called in process context but
 *                 should try hard not to block.
 */

struct rds_transport {
	char			t_name[TRANSNAMSIZ];
	struct list_head	t_item;
	struct module		*t_owner;
	unsigned int		t_prefer_loopback:1,
				t_mp_capable:1;
	unsigned int		t_type;

	int (*laddr_check)(struct net *net, __be32 addr);
	int (*conn_alloc)(struct rds_connection *conn, gfp_t gfp);
	void (*conn_free)(void *data);
	int (*conn_path_connect)(struct rds_conn_path *cp);
	void (*conn_path_shutdown)(struct rds_conn_path *conn);
	void (*xmit_path_prepare)(struct rds_conn_path *cp);
	void (*xmit_path_complete)(struct rds_conn_path *cp);
	int (*xmit)(struct rds_connection *conn, struct rds_message *rm,
		    unsigned int hdr_off, unsigned int sg, unsigned int off);
	int (*xmit_rdma)(struct rds_connection *conn, struct rm_rdma_op *op);
	int (*xmit_atomic)(struct rds_connection *conn, struct rm_atomic_op *op);
	int (*recv_path)(struct rds_conn_path *cp);
	int (*inc_copy_to_user)(struct rds_incoming *inc, struct iov_iter *to);
	void (*inc_free)(struct rds_incoming *inc);

	int (*cm_handle_connect)(struct rdma_cm_id *cm_id,
				 struct rdma_cm_event *event);
	int (*cm_initiate_connect)(struct rdma_cm_id *cm_id);
	void (*cm_connect_complete)(struct rds_connection *conn,
				    struct rdma_cm_event *event);

	unsigned int (*stats_info_copy)(struct rds_info_iterator *iter,
					unsigned int avail);
	void (*exit)(void);
	void *(*get_mr)(struct scatterlist *sg, unsigned long nr_sg,
			struct rds_sock *rs, u32 *key_ret);
	void (*sync_mr)(void *trans_private, int direction);
	void (*free_mr)(void *trans_private, int invalidate);
	void (*flush_mrs)(void);
};

struct rds_sock {
	struct sock		rs_sk;

	u64			rs_user_addr;
	u64			rs_user_bytes;

	/*
	 * bound_addr used for both incoming and outgoing, no INADDR_ANY
	 * support.
	 */
	struct rhash_head	rs_bound_node;
	u64			rs_bound_key;
	__be32			rs_bound_addr;
	__be32			rs_conn_addr;
	__be16			rs_bound_port;
	__be16			rs_conn_port;
	struct rds_transport    *rs_transport;

	/*
	 * rds_sendmsg caches the conn it used the last time around.
	 * This helps avoid costly lookups.
	 */
	struct rds_connection	*rs_conn;

	/* flag indicating we were congested or not */
	int			rs_congested;
	/* seen congestion (ENOBUFS) when sending? */
	int			rs_seen_congestion;

	/* rs_lock protects all these adjacent members before the newline */
	spinlock_t		rs_lock;
	struct list_head	rs_send_queue;
	u32			rs_snd_bytes;
	int			rs_rcv_bytes;
	struct list_head	rs_notify_queue;	/* currently used for failed RDMAs */

	/* Congestion wake_up. If rs_cong_monitor is set, we use cong_mask
	 * to decide whether the application should be woken up.
	 * If not set, we use rs_cong_track to find out whether a cong map
	 * update arrived.
	 */
	uint64_t		rs_cong_mask;
	uint64_t		rs_cong_notify;
	struct list_head	rs_cong_list;
	unsigned long		rs_cong_track;

	/*
	 * rs_recv_lock protects the receive queue, and is
	 * used to serialize with rds_release.
	 */
	rwlock_t		rs_recv_lock;
	struct list_head	rs_recv_queue;

	/* just for stats reporting */
	struct list_head	rs_item;

	/* these have their own lock */
	spinlock_t		rs_rdma_lock;
	struct rb_root		rs_rdma_keys;

	/* Socket options - in case there will be more */
	unsigned char		rs_recverr,
				rs_cong_monitor;
	u32			rs_hash_initval;

	/* Socket receive path trace points*/
	u8			rs_rx_traces;
	u8			rs_rx_trace[RDS_MSG_RX_DGRAM_TRACE_MAX];
};

static inline struct rds_sock *rds_sk_to_rs(const struct sock *sk)
{
	return container_of(sk, struct rds_sock, rs_sk);
}
static inline struct sock *rds_rs_to_sk(struct rds_sock *rs)
{
	return &rs->rs_sk;
}

/*
 * The stack assigns sk_sndbuf and sk_rcvbuf to twice the specified value
 * to account for overhead.  We don't account for overhead, we just apply
 * the number of payload bytes to the specified value.
 */
static inline int rds_sk_sndbuf(struct rds_sock *rs)
{
	return rds_rs_to_sk(rs)->sk_sndbuf / 2;
}
static inline int rds_sk_rcvbuf(struct rds_sock *rs)
{
	return rds_rs_to_sk(rs)->sk_rcvbuf / 2;
}

struct rds_statistics {
	uint64_t	s_conn_reset;
	uint64_t	s_recv_drop_bad_checksum;
	uint64_t	s_recv_drop_old_seq;
	uint64_t	s_recv_drop_no_sock;
	uint64_t	s_recv_drop_dead_sock;
	uint64_t	s_recv_deliver_raced;
	uint64_t	s_recv_delivered;
	uint64_t	s_recv_queued;
	uint64_t	s_recv_immediate_retry;
	uint64_t	s_recv_delayed_retry;
	uint64_t	s_recv_ack_required;
	uint64_t	s_recv_rdma_bytes;
	uint64_t	s_recv_ping;
	uint64_t	s_send_queue_empty;
	uint64_t	s_send_queue_full;
	uint64_t	s_send_lock_contention;
	uint64_t	s_send_lock_queue_raced;
	uint64_t	s_send_immediate_retry;
	uint64_t	s_send_delayed_retry;
	uint64_t	s_send_drop_acked;
	uint64_t	s_send_ack_required;
	uint64_t	s_send_queued;
	uint64_t	s_send_rdma;
	uint64_t	s_send_rdma_bytes;
	uint64_t	s_send_pong;
	uint64_t	s_page_remainder_hit;
	uint64_t	s_page_remainder_miss;
	uint64_t	s_copy_to_user;
	uint64_t	s_copy_from_user;
	uint64_t	s_cong_update_queued;
	uint64_t	s_cong_update_received;
	uint64_t	s_cong_send_error;
	uint64_t	s_cong_send_blocked;
	uint64_t	s_recv_bytes_added_to_socket;
	uint64_t	s_recv_bytes_removed_from_socket;

};

/* af_rds.c */
void rds_sock_addref(struct rds_sock *rs);
void rds_sock_put(struct rds_sock *rs);
void rds_wake_sk_sleep(struct rds_sock *rs);
static inline void __rds_wake_sk_sleep(struct sock *sk)
{
	wait_queue_head_t *waitq = sk_sleep(sk);

	if (!sock_flag(sk, SOCK_DEAD) && waitq)
		wake_up(waitq);
}
extern wait_queue_head_t rds_poll_waitq;


/* bind.c */
int rds_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len);
void rds_remove_bound(struct rds_sock *rs);
struct rds_sock *rds_find_bound(__be32 addr, __be16 port);
int rds_bind_lock_init(void);
void rds_bind_lock_destroy(void);

/* cong.c */
int rds_cong_get_maps(struct rds_connection *conn);
void rds_cong_add_conn(struct rds_connection *conn);
void rds_cong_remove_conn(struct rds_connection *conn);
void rds_cong_set_bit(struct rds_cong_map *map, __be16 port);
void rds_cong_clear_bit(struct rds_cong_map *map, __be16 port);
int rds_cong_wait(struct rds_cong_map *map, __be16 port, int nonblock, struct rds_sock *rs);
void rds_cong_queue_updates(struct rds_cong_map *map);
void rds_cong_map_updated(struct rds_cong_map *map, uint64_t);
int rds_cong_updated_since(unsigned long *recent);
void rds_cong_add_socket(struct rds_sock *);
void rds_cong_remove_socket(struct rds_sock *);
void rds_cong_exit(void);
struct rds_message *rds_cong_update_alloc(struct rds_connection *conn);

/* conn.c */
extern u32 rds_gen_num;
int rds_conn_init(void);
void rds_conn_exit(void);
struct rds_connection *rds_conn_create(struct net *net,
				       __be32 laddr, __be32 faddr,
				       struct rds_transport *trans, gfp_t gfp);
struct rds_connection *rds_conn_create_outgoing(struct net *net,
						__be32 laddr, __be32 faddr,
			       struct rds_transport *trans, gfp_t gfp);
void rds_conn_shutdown(struct rds_conn_path *cpath);
void rds_conn_destroy(struct rds_connection *conn);
void rds_conn_drop(struct rds_connection *conn);
void rds_conn_path_drop(struct rds_conn_path *cpath);
void rds_conn_connect_if_down(struct rds_connection *conn);
void rds_conn_path_connect_if_down(struct rds_conn_path *cp);
void rds_for_each_conn_info(struct socket *sock, unsigned int len,
			  struct rds_info_iterator *iter,
			  struct rds_info_lengths *lens,
			  int (*visitor)(struct rds_connection *, void *),
			  size_t item_len);

__printf(2, 3)
void __rds_conn_path_error(struct rds_conn_path *cp, const char *, ...);
#define rds_conn_path_error(cp, fmt...) \
	__rds_conn_path_error(cp, KERN_WARNING "RDS: " fmt)

static inline int
rds_conn_path_transition(struct rds_conn_path *cp, int old, int new)
{
	return atomic_cmpxchg(&cp->cp_state, old, new) == old;
}

static inline int
rds_conn_transition(struct rds_connection *conn, int old, int new)
{
	WARN_ON(conn->c_trans->t_mp_capable);
	return rds_conn_path_transition(&conn->c_path[0], old, new);
}

static inline int
rds_conn_path_state(struct rds_conn_path *cp)
{
	return atomic_read(&cp->cp_state);
}

static inline int
rds_conn_state(struct rds_connection *conn)
{
	WARN_ON(conn->c_trans->t_mp_capable);
	return rds_conn_path_state(&conn->c_path[0]);
}

static inline int
rds_conn_path_up(struct rds_conn_path *cp)
{
	return atomic_read(&cp->cp_state) == RDS_CONN_UP;
}

static inline int
rds_conn_up(struct rds_connection *conn)
{
	WARN_ON(conn->c_trans->t_mp_capable);
	return rds_conn_path_up(&conn->c_path[0]);
}

static inline int
rds_conn_path_connecting(struct rds_conn_path *cp)
{
	return atomic_read(&cp->cp_state) == RDS_CONN_CONNECTING;
}

static inline int
rds_conn_connecting(struct rds_connection *conn)
{
	WARN_ON(conn->c_trans->t_mp_capable);
	return rds_conn_path_connecting(&conn->c_path[0]);
}

/* message.c */
struct rds_message *rds_message_alloc(unsigned int nents, gfp_t gfp);
struct scatterlist *rds_message_alloc_sgs(struct rds_message *rm, int nents);
int rds_message_copy_from_user(struct rds_message *rm, struct iov_iter *from);
struct rds_message *rds_message_map_pages(unsigned long *page_addrs, unsigned int total_len);
void rds_message_populate_header(struct rds_header *hdr, __be16 sport,
				 __be16 dport, u64 seq);
int rds_message_add_extension(struct rds_header *hdr,
			      unsigned int type, const void *data, unsigned int len);
int rds_message_next_extension(struct rds_header *hdr,
			       unsigned int *pos, void *buf, unsigned int *buflen);
int rds_message_add_rdma_dest_extension(struct rds_header *hdr, u32 r_key, u32 offset);
int rds_message_inc_copy_to_user(struct rds_incoming *inc, struct iov_iter *to);
void rds_message_inc_free(struct rds_incoming *inc);
void rds_message_addref(struct rds_message *rm);
void rds_message_put(struct rds_message *rm);
void rds_message_wait(struct rds_message *rm);
void rds_message_unmapped(struct rds_message *rm);

static inline void rds_message_make_checksum(struct rds_header *hdr)
{
	hdr->h_csum = 0;
	hdr->h_csum = ip_fast_csum((void *) hdr, sizeof(*hdr) >> 2);
}

static inline int rds_message_verify_checksum(const struct rds_header *hdr)
{
	return !hdr->h_csum || ip_fast_csum((void *) hdr, sizeof(*hdr) >> 2) == 0;
}


/* page.c */
int rds_page_remainder_alloc(struct scatterlist *scat, unsigned long bytes,
			     gfp_t gfp);
void rds_page_exit(void);

/* recv.c */
void rds_inc_init(struct rds_incoming *inc, struct rds_connection *conn,
		  __be32 saddr);
void rds_inc_path_init(struct rds_incoming *inc, struct rds_conn_path *conn,
		       __be32 saddr);
void rds_inc_put(struct rds_incoming *inc);
void rds_recv_incoming(struct rds_connection *conn, __be32 saddr, __be32 daddr,
		       struct rds_incoming *inc, gfp_t gfp);
int rds_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
		int msg_flags);
void rds_clear_recv_queue(struct rds_sock *rs);
int rds_notify_queue_get(struct rds_sock *rs, struct msghdr *msg);
void rds_inc_info_copy(struct rds_incoming *inc,
		       struct rds_info_iterator *iter,
		       __be32 saddr, __be32 daddr, int flip);

/* send.c */
int rds_sendmsg(struct socket *sock, struct msghdr *msg, size_t payload_len);
void rds_send_path_reset(struct rds_conn_path *conn);
int rds_send_xmit(struct rds_conn_path *cp);
struct sockaddr_in;
void rds_send_drop_to(struct rds_sock *rs, struct sockaddr_in *dest);
typedef int (*is_acked_func)(struct rds_message *rm, uint64_t ack);
void rds_send_drop_acked(struct rds_connection *conn, u64 ack,
			 is_acked_func is_acked);
void rds_send_path_drop_acked(struct rds_conn_path *cp, u64 ack,
			      is_acked_func is_acked);
int rds_send_pong(struct rds_conn_path *cp, __be16 dport);

/* rdma.c */
void rds_rdma_unuse(struct rds_sock *rs, u32 r_key, int force);
int rds_get_mr(struct rds_sock *rs, char __user *optval, int optlen);
int rds_get_mr_for_dest(struct rds_sock *rs, char __user *optval, int optlen);
int rds_free_mr(struct rds_sock *rs, char __user *optval, int optlen);
void rds_rdma_drop_keys(struct rds_sock *rs);
int rds_rdma_extra_size(struct rds_rdma_args *args);
int rds_cmsg_rdma_args(struct rds_sock *rs, struct rds_message *rm,
			  struct cmsghdr *cmsg);
int rds_cmsg_rdma_dest(struct rds_sock *rs, struct rds_message *rm,
			  struct cmsghdr *cmsg);
int rds_cmsg_rdma_args(struct rds_sock *rs, struct rds_message *rm,
			  struct cmsghdr *cmsg);
int rds_cmsg_rdma_map(struct rds_sock *rs, struct rds_message *rm,
			  struct cmsghdr *cmsg);
void rds_rdma_free_op(struct rm_rdma_op *ro);
void rds_atomic_free_op(struct rm_atomic_op *ao);
void rds_rdma_send_complete(struct rds_message *rm, int wc_status);
void rds_atomic_send_complete(struct rds_message *rm, int wc_status);
int rds_cmsg_atomic(struct rds_sock *rs, struct rds_message *rm,
		    struct cmsghdr *cmsg);

void __rds_put_mr_final(struct rds_mr *mr);
static inline void rds_mr_put(struct rds_mr *mr)
{
	if (atomic_dec_and_test(&mr->r_refcount))
		__rds_put_mr_final(mr);
}

/* stats.c */
DECLARE_PER_CPU_SHARED_ALIGNED(struct rds_statistics, rds_stats);
#define rds_stats_inc_which(which, member) do {		\
	per_cpu(which, get_cpu()).member++;		\
	put_cpu();					\
} while (0)
#define rds_stats_inc(member) rds_stats_inc_which(rds_stats, member)
#define rds_stats_add_which(which, member, count) do {		\
	per_cpu(which, get_cpu()).member += count;	\
	put_cpu();					\
} while (0)
#define rds_stats_add(member, count) rds_stats_add_which(rds_stats, member, count)
int rds_stats_init(void);
void rds_stats_exit(void);
void rds_stats_info_copy(struct rds_info_iterator *iter,
			 uint64_t *values, const char *const *names,
			 size_t nr);

/* sysctl.c */
int rds_sysctl_init(void);
void rds_sysctl_exit(void);
extern unsigned long rds_sysctl_sndbuf_min;
extern unsigned long rds_sysctl_sndbuf_default;
extern unsigned long rds_sysctl_sndbuf_max;
extern unsigned long rds_sysctl_reconnect_min_jiffies;
extern unsigned long rds_sysctl_reconnect_max_jiffies;
extern unsigned int  rds_sysctl_max_unacked_packets;
extern unsigned int  rds_sysctl_max_unacked_bytes;
extern unsigned int  rds_sysctl_ping_enable;
extern unsigned long rds_sysctl_trace_flags;
extern unsigned int  rds_sysctl_trace_level;

/* threads.c */
int rds_threads_init(void);
void rds_threads_exit(void);
extern struct workqueue_struct *rds_wq;
void rds_queue_reconnect(struct rds_conn_path *cp);
void rds_connect_worker(struct work_struct *);
void rds_shutdown_worker(struct work_struct *);
void rds_send_worker(struct work_struct *);
void rds_recv_worker(struct work_struct *);
void rds_connect_path_complete(struct rds_conn_path *conn, int curr);
void rds_connect_complete(struct rds_connection *conn);

/* transport.c */
void rds_trans_register(struct rds_transport *trans);
void rds_trans_unregister(struct rds_transport *trans);
struct rds_transport *rds_trans_get_preferred(struct net *net, __be32 addr);
void rds_trans_put(struct rds_transport *trans);
unsigned int rds_trans_stats_info_copy(struct rds_info_iterator *iter,
				       unsigned int avail);
struct rds_transport *rds_trans_get(int t_type);
int rds_trans_init(void);
void rds_trans_exit(void);

#endif
