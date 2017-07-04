/*
 * L2TP internal definitions.
 *
 * Copyright (c) 2008,2009 Katalix Systems Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/refcount.h>

#ifndef _L2TP_CORE_H_
#define _L2TP_CORE_H_

/* Just some random numbers */
#define L2TP_TUNNEL_MAGIC	0x42114DDA
#define L2TP_SESSION_MAGIC	0x0C04EB7D

/* Per tunnel, session hash table size */
#define L2TP_HASH_BITS	4
#define L2TP_HASH_SIZE	(1 << L2TP_HASH_BITS)

/* System-wide, session hash table size */
#define L2TP_HASH_BITS_2	8
#define L2TP_HASH_SIZE_2	(1 << L2TP_HASH_BITS_2)

struct sk_buff;

struct l2tp_stats {
	atomic_long_t		tx_packets;
	atomic_long_t		tx_bytes;
	atomic_long_t		tx_errors;
	atomic_long_t		rx_packets;
	atomic_long_t		rx_bytes;
	atomic_long_t		rx_seq_discards;
	atomic_long_t		rx_oos_packets;
	atomic_long_t		rx_errors;
	atomic_long_t		rx_cookie_discards;
};

struct l2tp_tunnel;

/* Describes a session. Contains information to determine incoming
 * packets and transmit outgoing ones.
 */
struct l2tp_session_cfg {
	enum l2tp_pwtype	pw_type;
	unsigned int		data_seq:2;	/* data sequencing level
						 * 0 => none, 1 => IP only,
						 * 2 => all
						 */
	unsigned int		recv_seq:1;	/* expect receive packets with
						 * sequence numbers? */
	unsigned int		send_seq:1;	/* send packets with sequence
						 * numbers? */
	unsigned int		lns_mode:1;	/* behave as LNS? LAC enables
						 * sequence numbers under
						 * control of LNS. */
	int			debug;		/* bitmask of debug message
						 * categories */
	u16			vlan_id;	/* VLAN pseudowire only */
	u16			offset;		/* offset to payload */
	u16			l2specific_len;	/* Layer 2 specific length */
	u16			l2specific_type; /* Layer 2 specific type */
	u8			cookie[8];	/* optional cookie */
	int			cookie_len;	/* 0, 4 or 8 bytes */
	u8			peer_cookie[8];	/* peer's cookie */
	int			peer_cookie_len; /* 0, 4 or 8 bytes */
	int			reorder_timeout; /* configured reorder timeout
						  * (in jiffies) */
	int			mtu;
	int			mru;
	char			*ifname;
};

struct l2tp_session {
	int			magic;		/* should be
						 * L2TP_SESSION_MAGIC */

	struct l2tp_tunnel	*tunnel;	/* back pointer to tunnel
						 * context */
	u32			session_id;
	u32			peer_session_id;
	u8			cookie[8];
	int			cookie_len;
	u8			peer_cookie[8];
	int			peer_cookie_len;
	u16			offset;		/* offset from end of L2TP header
						   to beginning of data */
	u16			l2specific_len;
	u16			l2specific_type;
	u16			hdr_len;
	u32			nr;		/* session NR state (receive) */
	u32			ns;		/* session NR state (send) */
	struct sk_buff_head	reorder_q;	/* receive reorder queue */
	u32			nr_max;		/* max NR. Depends on tunnel */
	u32			nr_window_size;	/* NR window size */
	u32			nr_oos;		/* NR of last OOS packet */
	int			nr_oos_count;	/* For OOS recovery */
	int			nr_oos_count_max;
	struct hlist_node	hlist;		/* Hash list node */
	refcount_t		ref_count;

	char			name[32];	/* for logging */
	char			ifname[IFNAMSIZ];
	unsigned int		data_seq:2;	/* data sequencing level
						 * 0 => none, 1 => IP only,
						 * 2 => all
						 */
	unsigned int		recv_seq:1;	/* expect receive packets with
						 * sequence numbers? */
	unsigned int		send_seq:1;	/* send packets with sequence
						 * numbers? */
	unsigned int		lns_mode:1;	/* behave as LNS? LAC enables
						 * sequence numbers under
						 * control of LNS. */
	int			debug;		/* bitmask of debug message
						 * categories */
	int			reorder_timeout; /* configured reorder timeout
						  * (in jiffies) */
	int			reorder_skip;	/* set if skip to next nr */
	int			mtu;
	int			mru;
	enum l2tp_pwtype	pwtype;
	struct l2tp_stats	stats;
	struct hlist_node	global_hlist;	/* Global hash list node */

	int (*build_header)(struct l2tp_session *session, void *buf);
	void (*recv_skb)(struct l2tp_session *session, struct sk_buff *skb, int data_len);
	void (*session_close)(struct l2tp_session *session);
	void (*ref)(struct l2tp_session *session);
	void (*deref)(struct l2tp_session *session);
#if IS_ENABLED(CONFIG_L2TP_DEBUGFS)
	void (*show)(struct seq_file *m, void *priv);
#endif
	uint8_t			priv[0];	/* private data */
};

/* Describes the tunnel. It contains info to track all the associated
 * sessions so incoming packets can be sorted out
 */
struct l2tp_tunnel_cfg {
	int			debug;		/* bitmask of debug message
						 * categories */
	enum l2tp_encap_type	encap;

	/* Used only for kernel-created sockets */
	struct in_addr		local_ip;
	struct in_addr		peer_ip;
#if IS_ENABLED(CONFIG_IPV6)
	struct in6_addr		*local_ip6;
	struct in6_addr		*peer_ip6;
#endif
	u16			local_udp_port;
	u16			peer_udp_port;
	unsigned int		use_udp_checksums:1,
				udp6_zero_tx_checksums:1,
				udp6_zero_rx_checksums:1;
};

struct l2tp_tunnel {
	int			magic;		/* Should be L2TP_TUNNEL_MAGIC */
	struct rcu_head rcu;
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
	enum l2tp_encap_type	encap;
	struct l2tp_stats	stats;

	struct list_head	list;		/* Keep a list of all tunnels */
	struct net		*l2tp_net;	/* the net we belong to */

	refcount_t		ref_count;
#ifdef CONFIG_DEBUG_FS
	void (*show)(struct seq_file *m, void *arg);
#endif
	int (*recv_payload_hook)(struct sk_buff *skb);
	void (*old_sk_destruct)(struct sock *);
	struct sock		*sock;		/* Parent socket */
	int			fd;		/* Parent fd, if tunnel socket
						 * was created by userspace */
#if IS_ENABLED(CONFIG_IPV6)
	bool			v4mapped;
#endif

	struct work_struct	del_work;

	uint8_t			priv[0];	/* private data */
};

struct l2tp_nl_cmd_ops {
	int (*session_create)(struct net *net, u32 tunnel_id, u32 session_id, u32 peer_session_id, struct l2tp_session_cfg *cfg);
	int (*session_delete)(struct l2tp_session *session);
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

struct l2tp_session *l2tp_session_get(const struct net *net,
				      struct l2tp_tunnel *tunnel,
				      u32 session_id, bool do_ref);
struct l2tp_session *l2tp_session_get_nth(struct l2tp_tunnel *tunnel, int nth,
					  bool do_ref);
struct l2tp_session *l2tp_session_get_by_ifname(const struct net *net,
						const char *ifname,
						bool do_ref);
struct l2tp_tunnel *l2tp_tunnel_find(const struct net *net, u32 tunnel_id);
struct l2tp_tunnel *l2tp_tunnel_find_nth(const struct net *net, int nth);

int l2tp_tunnel_create(struct net *net, int fd, int version, u32 tunnel_id,
		       u32 peer_tunnel_id, struct l2tp_tunnel_cfg *cfg,
		       struct l2tp_tunnel **tunnelp);
void l2tp_tunnel_closeall(struct l2tp_tunnel *tunnel);
int l2tp_tunnel_delete(struct l2tp_tunnel *tunnel);
struct l2tp_session *l2tp_session_create(int priv_size,
					 struct l2tp_tunnel *tunnel,
					 u32 session_id, u32 peer_session_id,
					 struct l2tp_session_cfg *cfg);
void __l2tp_session_unhash(struct l2tp_session *session);
int l2tp_session_delete(struct l2tp_session *session);
void l2tp_session_free(struct l2tp_session *session);
void l2tp_recv_common(struct l2tp_session *session, struct sk_buff *skb,
		      unsigned char *ptr, unsigned char *optr, u16 hdrflags,
		      int length, int (*payload_hook)(struct sk_buff *skb));
int l2tp_session_queue_purge(struct l2tp_session *session);
int l2tp_udp_encap_recv(struct sock *sk, struct sk_buff *skb);
void l2tp_session_set_header_len(struct l2tp_session *session, int version);

int l2tp_xmit_skb(struct l2tp_session *session, struct sk_buff *skb,
		  int hdr_len);

int l2tp_nl_register_ops(enum l2tp_pwtype pw_type,
			 const struct l2tp_nl_cmd_ops *ops);
void l2tp_nl_unregister_ops(enum l2tp_pwtype pw_type);
int l2tp_ioctl(struct sock *sk, int cmd, unsigned long arg);

/* Session reference counts. Incremented when code obtains a reference
 * to a session.
 */
static inline void l2tp_session_inc_refcount_1(struct l2tp_session *session)
{
	refcount_inc(&session->ref_count);
}

static inline void l2tp_session_dec_refcount_1(struct l2tp_session *session)
{
	if (refcount_dec_and_test(&session->ref_count))
		l2tp_session_free(session);
}

#ifdef L2TP_REFCNT_DEBUG
#define l2tp_session_inc_refcount(_s)					\
do {									\
	pr_debug("l2tp_session_inc_refcount: %s:%d %s: cnt=%d\n",	\
		 __func__, __LINE__, (_s)->name,			\
		 refcount_read(&_s->ref_count));			\
	l2tp_session_inc_refcount_1(_s);				\
} while (0)
#define l2tp_session_dec_refcount(_s)					\
do {									\
	pr_debug("l2tp_session_dec_refcount: %s:%d %s: cnt=%d\n",	\
		 __func__, __LINE__, (_s)->name,			\
		 refcount_read(&_s->ref_count));			\
	l2tp_session_dec_refcount_1(_s);				\
} while (0)
#else
#define l2tp_session_inc_refcount(s) l2tp_session_inc_refcount_1(s)
#define l2tp_session_dec_refcount(s) l2tp_session_dec_refcount_1(s)
#endif

#define l2tp_printk(ptr, type, func, fmt, ...)				\
do {									\
	if (((ptr)->debug) & (type))					\
		func(fmt, ##__VA_ARGS__);				\
} while (0)

#define l2tp_warn(ptr, type, fmt, ...)					\
	l2tp_printk(ptr, type, pr_warn, fmt, ##__VA_ARGS__)
#define l2tp_info(ptr, type, fmt, ...)					\
	l2tp_printk(ptr, type, pr_info, fmt, ##__VA_ARGS__)
#define l2tp_dbg(ptr, type, fmt, ...)					\
	l2tp_printk(ptr, type, pr_debug, fmt, ##__VA_ARGS__)

#define MODULE_ALIAS_L2TP_PWTYPE(type) \
	MODULE_ALIAS("net-l2tp-type-" __stringify(type))

#endif /* _L2TP_CORE_H_ */
