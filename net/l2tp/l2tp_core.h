/*
 * L2TP internal definitions.
 *
 * Copyright (c) 2008,2009 Katalix Systems Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _L2TP_CORE_H_
#define _L2TP_CORE_H_

/* Just some random numbers */
#define L2TP_TUNNEL_MAGIC	0x42114DDA
#define L2TP_SESSION_MAGIC	0x0C04EB7D

#define L2TP_HASH_BITS	4
#define L2TP_HASH_SIZE	(1 << L2TP_HASH_BITS)

/* Debug message categories for the DEBUG socket option */
enum {
	L2TP_MSG_DEBUG		= (1 << 0),	/* verbose debug (if
						 * compiled in) */
	L2TP_MSG_CONTROL	= (1 << 1),	/* userspace - kernel
						 * interface */
	L2TP_MSG_SEQ		= (1 << 2),	/* sequence numbers */
	L2TP_MSG_DATA		= (1 << 3),	/* data packets */
};

struct sk_buff;

struct l2tp_stats {
	u64			tx_packets;
	u64			tx_bytes;
	u64			tx_errors;
	u64			rx_packets;
	u64			rx_bytes;
	u64			rx_seq_discards;
	u64			rx_oos_packets;
	u64			rx_errors;
};

struct l2tp_tunnel;

/* Describes a session. Contains information to determine incoming
 * packets and transmit outgoing ones.
 */
struct l2tp_session_cfg {
	unsigned		data_seq:2;	/* data sequencing level
						 * 0 => none, 1 => IP only,
						 * 2 => all
						 */
	unsigned		recv_seq:1;	/* expect receive packets with
						 * sequence numbers? */
	unsigned		send_seq:1;	/* send packets with sequence
						 * numbers? */
	unsigned		lns_mode:1;	/* behave as LNS? LAC enables
						 * sequence numbers under
						 * control of LNS. */
	int			debug;		/* bitmask of debug message
						 * categories */
	int			offset;		/* offset to payload */
	int			reorder_timeout; /* configured reorder timeout
						  * (in jiffies) */
	int			mtu;
	int			mru;
	int			hdr_len;
};

struct l2tp_session {
	int			magic;		/* should be
						 * L2TP_SESSION_MAGIC */

	struct l2tp_tunnel	*tunnel;	/* back pointer to tunnel
						 * context */
	u32			session_id;
	u32			peer_session_id;
	u16			nr;		/* session NR state (receive) */
	u16			ns;		/* session NR state (send) */
	struct sk_buff_head	reorder_q;	/* receive reorder queue */
	struct hlist_node	hlist;		/* Hash list node */
	atomic_t		ref_count;

	char			name[32];	/* for logging */
	unsigned		data_seq:2;	/* data sequencing level
						 * 0 => none, 1 => IP only,
						 * 2 => all
						 */
	unsigned		recv_seq:1;	/* expect receive packets with
						 * sequence numbers? */
	unsigned		send_seq:1;	/* send packets with sequence
						 * numbers? */
	unsigned		lns_mode:1;	/* behave as LNS? LAC enables
						 * sequence numbers under
						 * control of LNS. */
	int			debug;		/* bitmask of debug message
						 * categories */
	int			reorder_timeout; /* configured reorder timeout
						  * (in jiffies) */
	int			mtu;
	int			mru;
	int			hdr_len;
	struct l2tp_stats	stats;

	void (*recv_skb)(struct l2tp_session *session, struct sk_buff *skb, int data_len);
	void (*session_close)(struct l2tp_session *session);
	void (*ref)(struct l2tp_session *session);
	void (*deref)(struct l2tp_session *session);

	uint8_t			priv[0];	/* private data */
};

/* Describes the tunnel. It contains info to track all the associated
 * sessions so incoming packets can be sorted out
 */
struct l2tp_tunnel_cfg {
	int			debug;		/* bitmask of debug message
						 * categories */
};

struct l2tp_tunnel {
	int			magic;		/* Should be L2TP_TUNNEL_MAGIC */
	rwlock_t		hlist_lock;	/* protect session_hlist */
	struct hlist_head	session_hlist[L2TP_HASH_SIZE];
						/* hashed list of sessions,
						 * hashed by id */
	u32			tunnel_id;
	u32			peer_tunnel_id;
	int			version;	/* 2=>L2TPv2, 3=>L2TPv3 */

	char			name[20];	/* for logging */
	int			debug;		/* bitmask of debug message
						 * categories */
	int			hdr_len;
	struct l2tp_stats	stats;

	struct list_head	list;		/* Keep a list of all tunnels */
	struct net		*l2tp_net;	/* the net we belong to */

	atomic_t		ref_count;

	int (*recv_payload_hook)(struct sk_buff *skb);
	void (*old_sk_destruct)(struct sock *);
	struct sock		*sock;		/* Parent socket */
	int			fd;

	uint8_t			priv[0];	/* private data */
};

static inline void *l2tp_tunnel_priv(struct l2tp_tunnel *tunnel)
{
	return &tunnel->priv[0];
}

static inline void *l2tp_session_priv(struct l2tp_session *session)
{
	return &session->priv[0];
}

static inline struct l2tp_tunnel *l2tp_sock_to_tunnel(struct sock *sk)
{
	struct l2tp_tunnel *tunnel;

	if (sk == NULL)
		return NULL;

	sock_hold(sk);
	tunnel = (struct l2tp_tunnel *)(sk->sk_user_data);
	if (tunnel == NULL) {
		sock_put(sk);
		goto out;
	}

	BUG_ON(tunnel->magic != L2TP_TUNNEL_MAGIC);

out:
	return tunnel;
}

extern struct l2tp_session *l2tp_session_find(struct l2tp_tunnel *tunnel, u32 session_id);
extern struct l2tp_session *l2tp_session_find_nth(struct l2tp_tunnel *tunnel, int nth);
extern struct l2tp_tunnel *l2tp_tunnel_find(struct net *net, u32 tunnel_id);
extern struct l2tp_tunnel *l2tp_tunnel_find_nth(struct net *net, int nth);

extern int l2tp_tunnel_create(struct net *net, int fd, int version, u32 tunnel_id, u32 peer_tunnel_id, struct l2tp_tunnel_cfg *cfg, struct l2tp_tunnel **tunnelp);
extern struct l2tp_session *l2tp_session_create(int priv_size, struct l2tp_tunnel *tunnel, u32 session_id, u32 peer_session_id, struct l2tp_session_cfg *cfg);
extern void l2tp_tunnel_free(struct l2tp_tunnel *tunnel);
extern void l2tp_session_free(struct l2tp_session *session);
extern int l2tp_udp_recv_core(struct l2tp_tunnel *tunnel, struct sk_buff *skb, int (*payload_hook)(struct sk_buff *skb));
extern int l2tp_udp_encap_recv(struct sock *sk, struct sk_buff *skb);

extern void l2tp_build_l2tp_header(struct l2tp_session *session, void *buf);
extern int l2tp_xmit_core(struct l2tp_session *session, struct sk_buff *skb, size_t data_len);
extern int l2tp_xmit_skb(struct l2tp_session *session, struct sk_buff *skb, int hdr_len);
extern void l2tp_tunnel_destruct(struct sock *sk);
extern void l2tp_tunnel_closeall(struct l2tp_tunnel *tunnel);

/* Tunnel reference counts. Incremented per session that is added to
 * the tunnel.
 */
static inline void l2tp_tunnel_inc_refcount_1(struct l2tp_tunnel *tunnel)
{
	atomic_inc(&tunnel->ref_count);
}

static inline void l2tp_tunnel_dec_refcount_1(struct l2tp_tunnel *tunnel)
{
	if (atomic_dec_and_test(&tunnel->ref_count))
		l2tp_tunnel_free(tunnel);
}
#ifdef L2TP_REFCNT_DEBUG
#define l2tp_tunnel_inc_refcount(_t) do { \
		printk(KERN_DEBUG "l2tp_tunnel_inc_refcount: %s:%d %s: cnt=%d\n", __func__, __LINE__, (_t)->name, atomic_read(&_t->ref_count)); \
		l2tp_tunnel_inc_refcount_1(_t);				\
	} while (0)
#define l2tp_tunnel_dec_refcount(_t) do { \
		printk(KERN_DEBUG "l2tp_tunnel_dec_refcount: %s:%d %s: cnt=%d\n", __func__, __LINE__, (_t)->name, atomic_read(&_t->ref_count)); \
		l2tp_tunnel_dec_refcount_1(_t);				\
	} while (0)
#else
#define l2tp_tunnel_inc_refcount(t) l2tp_tunnel_inc_refcount_1(t)
#define l2tp_tunnel_dec_refcount(t) l2tp_tunnel_dec_refcount_1(t)
#endif

/* Session reference counts. Incremented when code obtains a reference
 * to a session.
 */
static inline void l2tp_session_inc_refcount_1(struct l2tp_session *session)
{
	atomic_inc(&session->ref_count);
}

static inline void l2tp_session_dec_refcount_1(struct l2tp_session *session)
{
	if (atomic_dec_and_test(&session->ref_count))
		l2tp_session_free(session);
}

#ifdef L2TP_REFCNT_DEBUG
#define l2tp_session_inc_refcount(_s) do { \
		printk(KERN_DEBUG "l2tp_session_inc_refcount: %s:%d %s: cnt=%d\n", __func__, __LINE__, (_s)->name, atomic_read(&_s->ref_count)); \
		l2tp_session_inc_refcount_1(_s);				\
	} while (0)
#define l2tp_session_dec_refcount(_s) do { \
		printk(KERN_DEBUG "l2tp_session_dec_refcount: %s:%d %s: cnt=%d\n", __func__, __LINE__, (_s)->name, atomic_read(&_s->ref_count)); \
		l2tp_session_dec_refcount_1(_s);				\
	} while (0)
#else
#define l2tp_session_inc_refcount(s) l2tp_session_inc_refcount_1(s)
#define l2tp_session_dec_refcount(s) l2tp_session_dec_refcount_1(s)
#endif

#endif /* _L2TP_CORE_H_ */
