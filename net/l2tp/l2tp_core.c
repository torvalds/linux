/*
 * L2TP core.
 *
 * Copyright (c) 2008,2009,2010 Katalix Systems Ltd
 *
 * This file contains some code of the original L2TPv2 pppol2tp
 * driver, which has the following copyright:
 *
 * Authors:	Martijn van Oosterhout <kleptog@svana.org>
 *		James Chapman (jchapman@katalix.com)
 * Contributors:
 *		Michal Ostrowski <mostrows@speakeasy.net>
 *		Arnaldo Carvalho de Melo <acme@xconectiva.com.br>
 *		David S. Miller (davem@redhat.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/uaccess.h>

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/jiffies.h>

#include <linux/netdevice.h>
#include <linux/net.h>
#include <linux/inetdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/l2tp.h>
#include <linux/hash.h>
#include <linux/sort.h>
#include <linux/file.h>
#include <linux/nsproxy.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>
#include <net/inet_common.h>
#include <net/xfrm.h>
#include <net/protocol.h>
#include <net/inet6_connection_sock.h>
#include <net/inet_ecn.h>
#include <net/ip6_route.h>
#include <net/ip6_checksum.h>

#include <asm/byteorder.h>
#include <linux/atomic.h>

#include "l2tp_core.h"

#define L2TP_DRV_VERSION	"V2.0"

/* L2TP header constants */
#define L2TP_HDRFLAG_T	   0x8000
#define L2TP_HDRFLAG_L	   0x4000
#define L2TP_HDRFLAG_S	   0x0800
#define L2TP_HDRFLAG_O	   0x0200
#define L2TP_HDRFLAG_P	   0x0100

#define L2TP_HDR_VER_MASK  0x000F
#define L2TP_HDR_VER_2	   0x0002
#define L2TP_HDR_VER_3	   0x0003

/* L2TPv3 default L2-specific sublayer */
#define L2TP_SLFLAG_S	   0x40000000
#define L2TP_SL_SEQ_MASK   0x00ffffff

#define L2TP_HDR_SIZE_SEQ		10
#define L2TP_HDR_SIZE_NOSEQ		6

/* Default trace flags */
#define L2TP_DEFAULT_DEBUG_FLAGS	0

/* Private data stored for received packets in the skb.
 */
struct l2tp_skb_cb {
	u32			ns;
	u16			has_seq;
	u16			length;
	unsigned long		expires;
};

#define L2TP_SKB_CB(skb)	((struct l2tp_skb_cb *) &skb->cb[sizeof(struct inet_skb_parm)])

static atomic_t l2tp_tunnel_count;
static atomic_t l2tp_session_count;
static struct workqueue_struct *l2tp_wq;

/* per-net private data for this module */
static unsigned int l2tp_net_id;
struct l2tp_net {
	struct list_head l2tp_tunnel_list;
	spinlock_t l2tp_tunnel_list_lock;
	struct hlist_head l2tp_session_hlist[L2TP_HASH_SIZE_2];
	spinlock_t l2tp_session_hlist_lock;
};

#if IS_ENABLED(CONFIG_IPV6)
static bool l2tp_sk_is_v6(struct sock *sk)
{
	return sk->sk_family == PF_INET6 &&
	       !ipv6_addr_v4mapped(&sk->sk_v6_daddr);
}
#endif

static inline struct l2tp_tunnel *l2tp_tunnel(struct sock *sk)
{
	return sk->sk_user_data;
}

static inline struct l2tp_net *l2tp_pernet(const struct net *net)
{
	BUG_ON(!net);

	return net_generic(net, l2tp_net_id);
}

/* Session hash global list for L2TPv3.
 * The session_id SHOULD be random according to RFC3931, but several
 * L2TP implementations use incrementing session_ids.  So we do a real
 * hash on the session_id, rather than a simple bitmask.
 */
static inline struct hlist_head *
l2tp_session_id_hash_2(struct l2tp_net *pn, u32 session_id)
{
	return &pn->l2tp_session_hlist[hash_32(session_id, L2TP_HASH_BITS_2)];

}

/* Lookup the tunnel socket, possibly involving the fs code if the socket is
 * owned by userspace.  A struct sock returned from this function must be
 * released using l2tp_tunnel_sock_put once you're done with it.
 */
static struct sock *l2tp_tunnel_sock_lookup(struct l2tp_tunnel *tunnel)
{
	int err = 0;
	struct socket *sock = NULL;
	struct sock *sk = NULL;

	if (!tunnel)
		goto out;

	if (tunnel->fd >= 0) {
		/* Socket is owned by userspace, who might be in the process
		 * of closing it.  Look the socket up using the fd to ensure
		 * consistency.
		 */
		sock = sockfd_lookup(tunnel->fd, &err);
		if (sock)
			sk = sock->sk;
	} else {
		/* Socket is owned by kernelspace */
		sk = tunnel->sock;
		sock_hold(sk);
	}

out:
	return sk;
}

/* Drop a reference to a tunnel socket obtained via. l2tp_tunnel_sock_put */
static void l2tp_tunnel_sock_put(struct sock *sk)
{
	struct l2tp_tunnel *tunnel = l2tp_sock_to_tunnel(sk);
	if (tunnel) {
		if (tunnel->fd >= 0) {
			/* Socket is owned by userspace */
			sockfd_put(sk->sk_socket);
		}
		sock_put(sk);
	}
	sock_put(sk);
}

/* Session hash list.
 * The session_id SHOULD be random according to RFC2661, but several
 * L2TP implementations (Cisco and Microsoft) use incrementing
 * session_ids.  So we do a real hash on the session_id, rather than a
 * simple bitmask.
 */
static inline struct hlist_head *
l2tp_session_id_hash(struct l2tp_tunnel *tunnel, u32 session_id)
{
	return &tunnel->session_hlist[hash_32(session_id, L2TP_HASH_BITS)];
}

/* Lookup a tunnel. A new reference is held on the returned tunnel. */
struct l2tp_tunnel *l2tp_tunnel_get(const struct net *net, u32 tunnel_id)
{
	const struct l2tp_net *pn = l2tp_pernet(net);
	struct l2tp_tunnel *tunnel;

	rcu_read_lock_bh();
	list_for_each_entry_rcu(tunnel, &pn->l2tp_tunnel_list, list) {
		if (tunnel->tunnel_id == tunnel_id) {
			l2tp_tunnel_inc_refcount(tunnel);
			rcu_read_unlock_bh();

			return tunnel;
		}
	}
	rcu_read_unlock_bh();

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_get);

/* Lookup a session. A new reference is held on the returned session.
 * Optionally calls session->ref() too if do_ref is true.
 */
struct l2tp_session *l2tp_session_get(const struct net *net,
				      struct l2tp_tunnel *tunnel,
				      u32 session_id, bool do_ref)
{
	struct hlist_head *session_list;
	struct l2tp_session *session;

	if (!tunnel) {
		struct l2tp_net *pn = l2tp_pernet(net);

		session_list = l2tp_session_id_hash_2(pn, session_id);

		rcu_read_lock_bh();
		hlist_for_each_entry_rcu(session, session_list, global_hlist) {
			if (session->session_id == session_id) {
				l2tp_session_inc_refcount(session);
				if (do_ref && session->ref)
					session->ref(session);
				rcu_read_unlock_bh();

				return session;
			}
		}
		rcu_read_unlock_bh();

		return NULL;
	}

	session_list = l2tp_session_id_hash(tunnel, session_id);
	read_lock_bh(&tunnel->hlist_lock);
	hlist_for_each_entry(session, session_list, hlist) {
		if (session->session_id == session_id) {
			l2tp_session_inc_refcount(session);
			if (do_ref && session->ref)
				session->ref(session);
			read_unlock_bh(&tunnel->hlist_lock);

			return session;
		}
	}
	read_unlock_bh(&tunnel->hlist_lock);

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_session_get);

struct l2tp_session *l2tp_session_get_nth(struct l2tp_tunnel *tunnel, int nth,
					  bool do_ref)
{
	int hash;
	struct l2tp_session *session;
	int count = 0;

	read_lock_bh(&tunnel->hlist_lock);
	for (hash = 0; hash < L2TP_HASH_SIZE; hash++) {
		hlist_for_each_entry(session, &tunnel->session_hlist[hash], hlist) {
			if (++count > nth) {
				l2tp_session_inc_refcount(session);
				if (do_ref && session->ref)
					session->ref(session);
				read_unlock_bh(&tunnel->hlist_lock);
				return session;
			}
		}
	}

	read_unlock_bh(&tunnel->hlist_lock);

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_session_get_nth);

/* Lookup a session by interface name.
 * This is very inefficient but is only used by management interfaces.
 */
struct l2tp_session *l2tp_session_get_by_ifname(const struct net *net,
						const char *ifname,
						bool do_ref)
{
	struct l2tp_net *pn = l2tp_pernet(net);
	int hash;
	struct l2tp_session *session;

	rcu_read_lock_bh();
	for (hash = 0; hash < L2TP_HASH_SIZE_2; hash++) {
		hlist_for_each_entry_rcu(session, &pn->l2tp_session_hlist[hash], global_hlist) {
			if (!strcmp(session->ifname, ifname)) {
				l2tp_session_inc_refcount(session);
				if (do_ref && session->ref)
					session->ref(session);
				rcu_read_unlock_bh();

				return session;
			}
		}
	}

	rcu_read_unlock_bh();

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_session_get_by_ifname);

static int l2tp_session_add_to_tunnel(struct l2tp_tunnel *tunnel,
				      struct l2tp_session *session)
{
	struct l2tp_session *session_walk;
	struct hlist_head *g_head;
	struct hlist_head *head;
	struct l2tp_net *pn;
	int err;

	head = l2tp_session_id_hash(tunnel, session->session_id);

	write_lock_bh(&tunnel->hlist_lock);
	if (!tunnel->acpt_newsess) {
		err = -ENODEV;
		goto err_tlock;
	}

	hlist_for_each_entry(session_walk, head, hlist)
		if (session_walk->session_id == session->session_id) {
			err = -EEXIST;
			goto err_tlock;
		}

	if (tunnel->version == L2TP_HDR_VER_3) {
		pn = l2tp_pernet(tunnel->l2tp_net);
		g_head = l2tp_session_id_hash_2(l2tp_pernet(tunnel->l2tp_net),
						session->session_id);

		spin_lock_bh(&pn->l2tp_session_hlist_lock);

		hlist_for_each_entry(session_walk, g_head, global_hlist)
			if (session_walk->session_id == session->session_id) {
				err = -EEXIST;
				goto err_tlock_pnlock;
			}

		l2tp_tunnel_inc_refcount(tunnel);
		sock_hold(tunnel->sock);
		hlist_add_head_rcu(&session->global_hlist, g_head);

		spin_unlock_bh(&pn->l2tp_session_hlist_lock);
	} else {
		l2tp_tunnel_inc_refcount(tunnel);
		sock_hold(tunnel->sock);
	}

	hlist_add_head(&session->hlist, head);
	write_unlock_bh(&tunnel->hlist_lock);

	return 0;

err_tlock_pnlock:
	spin_unlock_bh(&pn->l2tp_session_hlist_lock);
err_tlock:
	write_unlock_bh(&tunnel->hlist_lock);

	return err;
}

/* Lookup a tunnel by id
 */
struct l2tp_tunnel *l2tp_tunnel_find(const struct net *net, u32 tunnel_id)
{
	struct l2tp_tunnel *tunnel;
	struct l2tp_net *pn = l2tp_pernet(net);

	rcu_read_lock_bh();
	list_for_each_entry_rcu(tunnel, &pn->l2tp_tunnel_list, list) {
		if (tunnel->tunnel_id == tunnel_id) {
			rcu_read_unlock_bh();
			return tunnel;
		}
	}
	rcu_read_unlock_bh();

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_find);

struct l2tp_tunnel *l2tp_tunnel_find_nth(const struct net *net, int nth)
{
	struct l2tp_net *pn = l2tp_pernet(net);
	struct l2tp_tunnel *tunnel;
	int count = 0;

	rcu_read_lock_bh();
	list_for_each_entry_rcu(tunnel, &pn->l2tp_tunnel_list, list) {
		if (++count > nth) {
			rcu_read_unlock_bh();
			return tunnel;
		}
	}

	rcu_read_unlock_bh();

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_find_nth);

/*****************************************************************************
 * Receive data handling
 *****************************************************************************/

/* Queue a skb in order. We come here only if the skb has an L2TP sequence
 * number.
 */
static void l2tp_recv_queue_skb(struct l2tp_session *session, struct sk_buff *skb)
{
	struct sk_buff *skbp;
	struct sk_buff *tmp;
	u32 ns = L2TP_SKB_CB(skb)->ns;

	spin_lock_bh(&session->reorder_q.lock);
	skb_queue_walk_safe(&session->reorder_q, skbp, tmp) {
		if (L2TP_SKB_CB(skbp)->ns > ns) {
			__skb_queue_before(&session->reorder_q, skbp, skb);
			l2tp_dbg(session, L2TP_MSG_SEQ,
				 "%s: pkt %hu, inserted before %hu, reorder_q len=%d\n",
				 session->name, ns, L2TP_SKB_CB(skbp)->ns,
				 skb_queue_len(&session->reorder_q));
			atomic_long_inc(&session->stats.rx_oos_packets);
			goto out;
		}
	}

	__skb_queue_tail(&session->reorder_q, skb);

out:
	spin_unlock_bh(&session->reorder_q.lock);
}

/* Dequeue a single skb.
 */
static void l2tp_recv_dequeue_skb(struct l2tp_session *session, struct sk_buff *skb)
{
	struct l2tp_tunnel *tunnel = session->tunnel;
	int length = L2TP_SKB_CB(skb)->length;

	/* We're about to requeue the skb, so return resources
	 * to its current owner (a socket receive buffer).
	 */
	skb_orphan(skb);

	atomic_long_inc(&tunnel->stats.rx_packets);
	atomic_long_add(length, &tunnel->stats.rx_bytes);
	atomic_long_inc(&session->stats.rx_packets);
	atomic_long_add(length, &session->stats.rx_bytes);

	if (L2TP_SKB_CB(skb)->has_seq) {
		/* Bump our Nr */
		session->nr++;
		session->nr &= session->nr_max;

		l2tp_dbg(session, L2TP_MSG_SEQ, "%s: updated nr to %hu\n",
			 session->name, session->nr);
	}

	/* call private receive handler */
	if (session->recv_skb != NULL)
		(*session->recv_skb)(session, skb, L2TP_SKB_CB(skb)->length);
	else
		kfree_skb(skb);

	if (session->deref)
		(*session->deref)(session);
}

/* Dequeue skbs from the session's reorder_q, subject to packet order.
 * Skbs that have been in the queue for too long are simply discarded.
 */
static void l2tp_recv_dequeue(struct l2tp_session *session)
{
	struct sk_buff *skb;
	struct sk_buff *tmp;

	/* If the pkt at the head of the queue has the nr that we
	 * expect to send up next, dequeue it and any other
	 * in-sequence packets behind it.
	 */
start:
	spin_lock_bh(&session->reorder_q.lock);
	skb_queue_walk_safe(&session->reorder_q, skb, tmp) {
		if (time_after(jiffies, L2TP_SKB_CB(skb)->expires)) {
			atomic_long_inc(&session->stats.rx_seq_discards);
			atomic_long_inc(&session->stats.rx_errors);
			l2tp_dbg(session, L2TP_MSG_SEQ,
				 "%s: oos pkt %u len %d discarded (too old), waiting for %u, reorder_q_len=%d\n",
				 session->name, L2TP_SKB_CB(skb)->ns,
				 L2TP_SKB_CB(skb)->length, session->nr,
				 skb_queue_len(&session->reorder_q));
			session->reorder_skip = 1;
			__skb_unlink(skb, &session->reorder_q);
			kfree_skb(skb);
			if (session->deref)
				(*session->deref)(session);
			continue;
		}

		if (L2TP_SKB_CB(skb)->has_seq) {
			if (session->reorder_skip) {
				l2tp_dbg(session, L2TP_MSG_SEQ,
					 "%s: advancing nr to next pkt: %u -> %u",
					 session->name, session->nr,
					 L2TP_SKB_CB(skb)->ns);
				session->reorder_skip = 0;
				session->nr = L2TP_SKB_CB(skb)->ns;
			}
			if (L2TP_SKB_CB(skb)->ns != session->nr) {
				l2tp_dbg(session, L2TP_MSG_SEQ,
					 "%s: holding oos pkt %u len %d, waiting for %u, reorder_q_len=%d\n",
					 session->name, L2TP_SKB_CB(skb)->ns,
					 L2TP_SKB_CB(skb)->length, session->nr,
					 skb_queue_len(&session->reorder_q));
				goto out;
			}
		}
		__skb_unlink(skb, &session->reorder_q);

		/* Process the skb. We release the queue lock while we
		 * do so to let other contexts process the queue.
		 */
		spin_unlock_bh(&session->reorder_q.lock);
		l2tp_recv_dequeue_skb(session, skb);
		goto start;
	}

out:
	spin_unlock_bh(&session->reorder_q.lock);
}

static int l2tp_seq_check_rx_window(struct l2tp_session *session, u32 nr)
{
	u32 nws;

	if (nr >= session->nr)
		nws = nr - session->nr;
	else
		nws = (session->nr_max + 1) - (session->nr - nr);

	return nws < session->nr_window_size;
}

/* If packet has sequence numbers, queue it if acceptable. Returns 0 if
 * acceptable, else non-zero.
 */
static int l2tp_recv_data_seq(struct l2tp_session *session, struct sk_buff *skb)
{
	if (!l2tp_seq_check_rx_window(session, L2TP_SKB_CB(skb)->ns)) {
		/* Packet sequence number is outside allowed window.
		 * Discard it.
		 */
		l2tp_dbg(session, L2TP_MSG_SEQ,
			 "%s: pkt %u len %d discarded, outside window, nr=%u\n",
			 session->name, L2TP_SKB_CB(skb)->ns,
			 L2TP_SKB_CB(skb)->length, session->nr);
		goto discard;
	}

	if (session->reorder_timeout != 0) {
		/* Packet reordering enabled. Add skb to session's
		 * reorder queue, in order of ns.
		 */
		l2tp_recv_queue_skb(session, skb);
		goto out;
	}

	/* Packet reordering disabled. Discard out-of-sequence packets, while
	 * tracking the number if in-sequence packets after the first OOS packet
	 * is seen. After nr_oos_count_max in-sequence packets, reset the
	 * sequence number to re-enable packet reception.
	 */
	if (L2TP_SKB_CB(skb)->ns == session->nr) {
		skb_queue_tail(&session->reorder_q, skb);
	} else {
		u32 nr_oos = L2TP_SKB_CB(skb)->ns;
		u32 nr_next = (session->nr_oos + 1) & session->nr_max;

		if (nr_oos == nr_next)
			session->nr_oos_count++;
		else
			session->nr_oos_count = 0;

		session->nr_oos = nr_oos;
		if (session->nr_oos_count > session->nr_oos_count_max) {
			session->reorder_skip = 1;
			l2tp_dbg(session, L2TP_MSG_SEQ,
				 "%s: %d oos packets received. Resetting sequence numbers\n",
				 session->name, session->nr_oos_count);
		}
		if (!session->reorder_skip) {
			atomic_long_inc(&session->stats.rx_seq_discards);
			l2tp_dbg(session, L2TP_MSG_SEQ,
				 "%s: oos pkt %u len %d discarded, waiting for %u, reorder_q_len=%d\n",
				 session->name, L2TP_SKB_CB(skb)->ns,
				 L2TP_SKB_CB(skb)->length, session->nr,
				 skb_queue_len(&session->reorder_q));
			goto discard;
		}
		skb_queue_tail(&session->reorder_q, skb);
	}

out:
	return 0;

discard:
	return 1;
}

/* Do receive processing of L2TP data frames. We handle both L2TPv2
 * and L2TPv3 data frames here.
 *
 * L2TPv2 Data Message Header
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |T|L|x|x|S|x|O|P|x|x|x|x|  Ver  |          Length (opt)         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           Tunnel ID           |           Session ID          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |             Ns (opt)          |             Nr (opt)          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      Offset Size (opt)        |    Offset pad... (opt)
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Data frames are marked by T=0. All other fields are the same as
 * those in L2TP control frames.
 *
 * L2TPv3 Data Message Header
 *
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      L2TP Session Header                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      L2-Specific Sublayer                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Tunnel Payload                      ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * L2TPv3 Session Header Over IP
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           Session ID                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |               Cookie (optional, maximum 64 bits)...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *                                                                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * L2TPv3 L2-Specific Sublayer Format
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |x|S|x|x|x|x|x|x|              Sequence Number                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Cookie value, sublayer format and offset (pad) are negotiated with
 * the peer when the session is set up. Unlike L2TPv2, we do not need
 * to parse the packet header to determine if optional fields are
 * present.
 *
 * Caller must already have parsed the frame and determined that it is
 * a data (not control) frame before coming here. Fields up to the
 * session-id have already been parsed and ptr points to the data
 * after the session-id.
 *
 * session->ref() must have been called prior to l2tp_recv_common().
 * session->deref() will be called automatically after skb is processed.
 */
void l2tp_recv_common(struct l2tp_session *session, struct sk_buff *skb,
		      unsigned char *ptr, unsigned char *optr, u16 hdrflags,
		      int length, int (*payload_hook)(struct sk_buff *skb))
{
	struct l2tp_tunnel *tunnel = session->tunnel;
	int offset;
	u32 ns, nr;

	/* Parse and check optional cookie */
	if (session->peer_cookie_len > 0) {
		if (memcmp(ptr, &session->peer_cookie[0], session->peer_cookie_len)) {
			l2tp_info(tunnel, L2TP_MSG_DATA,
				  "%s: cookie mismatch (%u/%u). Discarding.\n",
				  tunnel->name, tunnel->tunnel_id,
				  session->session_id);
			atomic_long_inc(&session->stats.rx_cookie_discards);
			goto discard;
		}
		ptr += session->peer_cookie_len;
	}

	/* Handle the optional sequence numbers. Sequence numbers are
	 * in different places for L2TPv2 and L2TPv3.
	 *
	 * If we are the LAC, enable/disable sequence numbers under
	 * the control of the LNS.  If no sequence numbers present but
	 * we were expecting them, discard frame.
	 */
	ns = nr = 0;
	L2TP_SKB_CB(skb)->has_seq = 0;
	if (tunnel->version == L2TP_HDR_VER_2) {
		if (hdrflags & L2TP_HDRFLAG_S) {
			ns = ntohs(*(__be16 *) ptr);
			ptr += 2;
			nr = ntohs(*(__be16 *) ptr);
			ptr += 2;

			/* Store L2TP info in the skb */
			L2TP_SKB_CB(skb)->ns = ns;
			L2TP_SKB_CB(skb)->has_seq = 1;

			l2tp_dbg(session, L2TP_MSG_SEQ,
				 "%s: recv data ns=%u, nr=%u, session nr=%u\n",
				 session->name, ns, nr, session->nr);
		}
	} else if (session->l2specific_type == L2TP_L2SPECTYPE_DEFAULT) {
		u32 l2h = ntohl(*(__be32 *) ptr);

		if (l2h & 0x40000000) {
			ns = l2h & 0x00ffffff;

			/* Store L2TP info in the skb */
			L2TP_SKB_CB(skb)->ns = ns;
			L2TP_SKB_CB(skb)->has_seq = 1;

			l2tp_dbg(session, L2TP_MSG_SEQ,
				 "%s: recv data ns=%u, session nr=%u\n",
				 session->name, ns, session->nr);
		}
	}

	/* Advance past L2-specific header, if present */
	ptr += session->l2specific_len;

	if (L2TP_SKB_CB(skb)->has_seq) {
		/* Received a packet with sequence numbers. If we're the LNS,
		 * check if we sre sending sequence numbers and if not,
		 * configure it so.
		 */
		if ((!session->lns_mode) && (!session->send_seq)) {
			l2tp_info(session, L2TP_MSG_SEQ,
				  "%s: requested to enable seq numbers by LNS\n",
				  session->name);
			session->send_seq = 1;
			l2tp_session_set_header_len(session, tunnel->version);
		}
	} else {
		/* No sequence numbers.
		 * If user has configured mandatory sequence numbers, discard.
		 */
		if (session->recv_seq) {
			l2tp_warn(session, L2TP_MSG_SEQ,
				  "%s: recv data has no seq numbers when required. Discarding.\n",
				  session->name);
			atomic_long_inc(&session->stats.rx_seq_discards);
			goto discard;
		}

		/* If we're the LAC and we're sending sequence numbers, the
		 * LNS has requested that we no longer send sequence numbers.
		 * If we're the LNS and we're sending sequence numbers, the
		 * LAC is broken. Discard the frame.
		 */
		if ((!session->lns_mode) && (session->send_seq)) {
			l2tp_info(session, L2TP_MSG_SEQ,
				  "%s: requested to disable seq numbers by LNS\n",
				  session->name);
			session->send_seq = 0;
			l2tp_session_set_header_len(session, tunnel->version);
		} else if (session->send_seq) {
			l2tp_warn(session, L2TP_MSG_SEQ,
				  "%s: recv data has no seq numbers when required. Discarding.\n",
				  session->name);
			atomic_long_inc(&session->stats.rx_seq_discards);
			goto discard;
		}
	}

	/* Session data offset is handled differently for L2TPv2 and
	 * L2TPv3. For L2TPv2, there is an optional 16-bit value in
	 * the header. For L2TPv3, the offset is negotiated using AVPs
	 * in the session setup control protocol.
	 */
	if (tunnel->version == L2TP_HDR_VER_2) {
		/* If offset bit set, skip it. */
		if (hdrflags & L2TP_HDRFLAG_O) {
			offset = ntohs(*(__be16 *)ptr);
			ptr += 2 + offset;
		}
	} else
		ptr += session->offset;

	offset = ptr - optr;
	if (!pskb_may_pull(skb, offset))
		goto discard;

	__skb_pull(skb, offset);

	/* If caller wants to process the payload before we queue the
	 * packet, do so now.
	 */
	if (payload_hook)
		if ((*payload_hook)(skb))
			goto discard;

	/* Prepare skb for adding to the session's reorder_q.  Hold
	 * packets for max reorder_timeout or 1 second if not
	 * reordering.
	 */
	L2TP_SKB_CB(skb)->length = length;
	L2TP_SKB_CB(skb)->expires = jiffies +
		(session->reorder_timeout ? session->reorder_timeout : HZ);

	/* Add packet to the session's receive queue. Reordering is done here, if
	 * enabled. Saved L2TP protocol info is stored in skb->sb[].
	 */
	if (L2TP_SKB_CB(skb)->has_seq) {
		if (l2tp_recv_data_seq(session, skb))
			goto discard;
	} else {
		/* No sequence numbers. Add the skb to the tail of the
		 * reorder queue. This ensures that it will be
		 * delivered after all previous sequenced skbs.
		 */
		skb_queue_tail(&session->reorder_q, skb);
	}

	/* Try to dequeue as many skbs from reorder_q as we can. */
	l2tp_recv_dequeue(session);

	return;

discard:
	atomic_long_inc(&session->stats.rx_errors);
	kfree_skb(skb);

	if (session->deref)
		(*session->deref)(session);
}
EXPORT_SYMBOL(l2tp_recv_common);

/* Drop skbs from the session's reorder_q
 */
int l2tp_session_queue_purge(struct l2tp_session *session)
{
	struct sk_buff *skb = NULL;
	BUG_ON(!session);
	BUG_ON(session->magic != L2TP_SESSION_MAGIC);
	while ((skb = skb_dequeue(&session->reorder_q))) {
		atomic_long_inc(&session->stats.rx_errors);
		kfree_skb(skb);
		if (session->deref)
			(*session->deref)(session);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(l2tp_session_queue_purge);

/* Internal UDP receive frame. Do the real work of receiving an L2TP data frame
 * here. The skb is not on a list when we get here.
 * Returns 0 if the packet was a data packet and was successfully passed on.
 * Returns 1 if the packet was not a good data packet and could not be
 * forwarded.  All such packets are passed up to userspace to deal with.
 */
static int l2tp_udp_recv_core(struct l2tp_tunnel *tunnel, struct sk_buff *skb,
			      int (*payload_hook)(struct sk_buff *skb))
{
	struct l2tp_session *session = NULL;
	unsigned char *ptr, *optr;
	u16 hdrflags;
	u32 tunnel_id, session_id;
	u16 version;
	int length;

	/* UDP has verifed checksum */

	/* UDP always verifies the packet length. */
	__skb_pull(skb, sizeof(struct udphdr));

	/* Short packet? */
	if (!pskb_may_pull(skb, L2TP_HDR_SIZE_SEQ)) {
		l2tp_info(tunnel, L2TP_MSG_DATA,
			  "%s: recv short packet (len=%d)\n",
			  tunnel->name, skb->len);
		goto error;
	}

	/* Trace packet contents, if enabled */
	if (tunnel->debug & L2TP_MSG_DATA) {
		length = min(32u, skb->len);
		if (!pskb_may_pull(skb, length))
			goto error;

		pr_debug("%s: recv\n", tunnel->name);
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, skb->data, length);
	}

	/* Point to L2TP header */
	optr = ptr = skb->data;

	/* Get L2TP header flags */
	hdrflags = ntohs(*(__be16 *) ptr);

	/* Check protocol version */
	version = hdrflags & L2TP_HDR_VER_MASK;
	if (version != tunnel->version) {
		l2tp_info(tunnel, L2TP_MSG_DATA,
			  "%s: recv protocol version mismatch: got %d expected %d\n",
			  tunnel->name, version, tunnel->version);
		goto error;
	}

	/* Get length of L2TP packet */
	length = skb->len;

	/* If type is control packet, it is handled by userspace. */
	if (hdrflags & L2TP_HDRFLAG_T) {
		l2tp_dbg(tunnel, L2TP_MSG_DATA,
			 "%s: recv control packet, len=%d\n",
			 tunnel->name, length);
		goto error;
	}

	/* Skip flags */
	ptr += 2;

	if (tunnel->version == L2TP_HDR_VER_2) {
		/* If length is present, skip it */
		if (hdrflags & L2TP_HDRFLAG_L)
			ptr += 2;

		/* Extract tunnel and session ID */
		tunnel_id = ntohs(*(__be16 *) ptr);
		ptr += 2;
		session_id = ntohs(*(__be16 *) ptr);
		ptr += 2;
	} else {
		ptr += 2;	/* skip reserved bits */
		tunnel_id = tunnel->tunnel_id;
		session_id = ntohl(*(__be32 *) ptr);
		ptr += 4;
	}

	/* Find the session context */
	session = l2tp_session_get(tunnel->l2tp_net, tunnel, session_id, true);
	if (!session || !session->recv_skb) {
		if (session) {
			if (session->deref)
				session->deref(session);
			l2tp_session_dec_refcount(session);
		}

		/* Not found? Pass to userspace to deal with */
		l2tp_info(tunnel, L2TP_MSG_DATA,
			  "%s: no session found (%u/%u). Passing up.\n",
			  tunnel->name, tunnel_id, session_id);
		goto error;
	}

	l2tp_recv_common(session, skb, ptr, optr, hdrflags, length, payload_hook);
	l2tp_session_dec_refcount(session);

	return 0;

error:
	/* Put UDP header back */
	__skb_push(skb, sizeof(struct udphdr));

	return 1;
}

/* UDP encapsulation receive handler. See net/ipv4/udp.c.
 * Return codes:
 * 0 : success.
 * <0: error
 * >0: skb should be passed up to userspace as UDP.
 */
int l2tp_udp_encap_recv(struct sock *sk, struct sk_buff *skb)
{
	struct l2tp_tunnel *tunnel;

	tunnel = l2tp_sock_to_tunnel(sk);
	if (tunnel == NULL)
		goto pass_up;

	l2tp_dbg(tunnel, L2TP_MSG_DATA, "%s: received %d bytes\n",
		 tunnel->name, skb->len);

	if (l2tp_udp_recv_core(tunnel, skb, tunnel->recv_payload_hook))
		goto pass_up_put;

	sock_put(sk);
	return 0;

pass_up_put:
	sock_put(sk);
pass_up:
	return 1;
}
EXPORT_SYMBOL_GPL(l2tp_udp_encap_recv);

/************************************************************************
 * Transmit handling
 ***********************************************************************/

/* Build an L2TP header for the session into the buffer provided.
 */
static int l2tp_build_l2tpv2_header(struct l2tp_session *session, void *buf)
{
	struct l2tp_tunnel *tunnel = session->tunnel;
	__be16 *bufp = buf;
	__be16 *optr = buf;
	u16 flags = L2TP_HDR_VER_2;
	u32 tunnel_id = tunnel->peer_tunnel_id;
	u32 session_id = session->peer_session_id;

	if (session->send_seq)
		flags |= L2TP_HDRFLAG_S;

	/* Setup L2TP header. */
	*bufp++ = htons(flags);
	*bufp++ = htons(tunnel_id);
	*bufp++ = htons(session_id);
	if (session->send_seq) {
		*bufp++ = htons(session->ns);
		*bufp++ = 0;
		session->ns++;
		session->ns &= 0xffff;
		l2tp_dbg(session, L2TP_MSG_SEQ, "%s: updated ns to %u\n",
			 session->name, session->ns);
	}

	return bufp - optr;
}

static int l2tp_build_l2tpv3_header(struct l2tp_session *session, void *buf)
{
	struct l2tp_tunnel *tunnel = session->tunnel;
	char *bufp = buf;
	char *optr = bufp;

	/* Setup L2TP header. The header differs slightly for UDP and
	 * IP encapsulations. For UDP, there is 4 bytes of flags.
	 */
	if (tunnel->encap == L2TP_ENCAPTYPE_UDP) {
		u16 flags = L2TP_HDR_VER_3;
		*((__be16 *) bufp) = htons(flags);
		bufp += 2;
		*((__be16 *) bufp) = 0;
		bufp += 2;
	}

	*((__be32 *) bufp) = htonl(session->peer_session_id);
	bufp += 4;
	if (session->cookie_len) {
		memcpy(bufp, &session->cookie[0], session->cookie_len);
		bufp += session->cookie_len;
	}
	if (session->l2specific_len) {
		if (session->l2specific_type == L2TP_L2SPECTYPE_DEFAULT) {
			u32 l2h = 0;
			if (session->send_seq) {
				l2h = 0x40000000 | session->ns;
				session->ns++;
				session->ns &= 0xffffff;
				l2tp_dbg(session, L2TP_MSG_SEQ,
					 "%s: updated ns to %u\n",
					 session->name, session->ns);
			}

			*((__be32 *) bufp) = htonl(l2h);
		}
		bufp += session->l2specific_len;
	}
	if (session->offset)
		bufp += session->offset;

	return bufp - optr;
}

static int l2tp_xmit_core(struct l2tp_session *session, struct sk_buff *skb,
			  struct flowi *fl, size_t data_len)
{
	struct l2tp_tunnel *tunnel = session->tunnel;
	unsigned int len = skb->len;
	int error;

	/* Debug */
	if (session->send_seq)
		l2tp_dbg(session, L2TP_MSG_DATA, "%s: send %zd bytes, ns=%u\n",
			 session->name, data_len, session->ns - 1);
	else
		l2tp_dbg(session, L2TP_MSG_DATA, "%s: send %zd bytes\n",
			 session->name, data_len);

	if (session->debug & L2TP_MSG_DATA) {
		int uhlen = (tunnel->encap == L2TP_ENCAPTYPE_UDP) ? sizeof(struct udphdr) : 0;
		unsigned char *datap = skb->data + uhlen;

		pr_debug("%s: xmit\n", session->name);
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
				     datap, min_t(size_t, 32, len - uhlen));
	}

	/* Queue the packet to IP for output */
	skb->ignore_df = 1;
#if IS_ENABLED(CONFIG_IPV6)
	if (l2tp_sk_is_v6(tunnel->sock))
		error = inet6_csk_xmit(tunnel->sock, skb, NULL);
	else
#endif
		error = ip_queue_xmit(tunnel->sock, skb, fl);

	/* Update stats */
	if (error >= 0) {
		atomic_long_inc(&tunnel->stats.tx_packets);
		atomic_long_add(len, &tunnel->stats.tx_bytes);
		atomic_long_inc(&session->stats.tx_packets);
		atomic_long_add(len, &session->stats.tx_bytes);
	} else {
		atomic_long_inc(&tunnel->stats.tx_errors);
		atomic_long_inc(&session->stats.tx_errors);
	}

	return 0;
}

/* If caller requires the skb to have a ppp header, the header must be
 * inserted in the skb data before calling this function.
 */
int l2tp_xmit_skb(struct l2tp_session *session, struct sk_buff *skb, int hdr_len)
{
	int data_len = skb->len;
	struct l2tp_tunnel *tunnel = session->tunnel;
	struct sock *sk = tunnel->sock;
	struct flowi *fl;
	struct udphdr *uh;
	struct inet_sock *inet;
	int headroom;
	int uhlen = (tunnel->encap == L2TP_ENCAPTYPE_UDP) ? sizeof(struct udphdr) : 0;
	int udp_len;
	int ret = NET_XMIT_SUCCESS;

	/* Check that there's enough headroom in the skb to insert IP,
	 * UDP and L2TP headers. If not enough, expand it to
	 * make room. Adjust truesize.
	 */
	headroom = NET_SKB_PAD + sizeof(struct iphdr) +
		uhlen + hdr_len;
	if (skb_cow_head(skb, headroom)) {
		kfree_skb(skb);
		return NET_XMIT_DROP;
	}

	/* Setup L2TP header */
	session->build_header(session, __skb_push(skb, hdr_len));

	/* Reset skb netfilter state */
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
	IPCB(skb)->flags &= ~(IPSKB_XFRM_TUNNEL_SIZE | IPSKB_XFRM_TRANSFORMED |
			      IPSKB_REROUTED);
	nf_reset(skb);

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		kfree_skb(skb);
		ret = NET_XMIT_DROP;
		goto out_unlock;
	}

	/* The user-space may change the connection status for the user-space
	 * provided socket at run time: we must check it under the socket lock
	 */
	if (tunnel->fd >= 0 && sk->sk_state != TCP_ESTABLISHED) {
		kfree_skb(skb);
		ret = NET_XMIT_DROP;
		goto out_unlock;
	}

	/* Get routing info from the tunnel socket */
	skb_dst_drop(skb);
	skb_dst_set(skb, sk_dst_check(sk, 0));

	inet = inet_sk(sk);
	fl = &inet->cork.fl;
	switch (tunnel->encap) {
	case L2TP_ENCAPTYPE_UDP:
		/* Setup UDP header */
		__skb_push(skb, sizeof(*uh));
		skb_reset_transport_header(skb);
		uh = udp_hdr(skb);
		uh->source = inet->inet_sport;
		uh->dest = inet->inet_dport;
		udp_len = uhlen + hdr_len + data_len;
		uh->len = htons(udp_len);

		/* Calculate UDP checksum if configured to do so */
#if IS_ENABLED(CONFIG_IPV6)
		if (l2tp_sk_is_v6(sk))
			udp6_set_csum(udp_get_no_check6_tx(sk),
				      skb, &inet6_sk(sk)->saddr,
				      &sk->sk_v6_daddr, udp_len);
		else
#endif
		udp_set_csum(sk->sk_no_check_tx, skb, inet->inet_saddr,
			     inet->inet_daddr, udp_len);
		break;

	case L2TP_ENCAPTYPE_IP:
		break;
	}

	l2tp_xmit_core(session, skb, fl, data_len);
out_unlock:
	bh_unlock_sock(sk);

	return ret;
}
EXPORT_SYMBOL_GPL(l2tp_xmit_skb);

/*****************************************************************************
 * Tinnel and session create/destroy.
 *****************************************************************************/

/* Tunnel socket destruct hook.
 * The tunnel context is deleted only when all session sockets have been
 * closed.
 */
static void l2tp_tunnel_destruct(struct sock *sk)
{
	struct l2tp_tunnel *tunnel = l2tp_tunnel(sk);
	struct l2tp_net *pn;

	if (tunnel == NULL)
		goto end;

	l2tp_info(tunnel, L2TP_MSG_CONTROL, "%s: closing...\n", tunnel->name);


	/* Disable udp encapsulation */
	switch (tunnel->encap) {
	case L2TP_ENCAPTYPE_UDP:
		/* No longer an encapsulation socket. See net/ipv4/udp.c */
		(udp_sk(sk))->encap_type = 0;
		(udp_sk(sk))->encap_rcv = NULL;
		(udp_sk(sk))->encap_destroy = NULL;
		break;
	case L2TP_ENCAPTYPE_IP:
		break;
	}

	/* Remove hooks into tunnel socket */
	sk->sk_destruct = tunnel->old_sk_destruct;
	sk->sk_user_data = NULL;

	/* Remove the tunnel struct from the tunnel list */
	pn = l2tp_pernet(tunnel->l2tp_net);
	spin_lock_bh(&pn->l2tp_tunnel_list_lock);
	list_del_rcu(&tunnel->list);
	spin_unlock_bh(&pn->l2tp_tunnel_list_lock);
	atomic_dec(&l2tp_tunnel_count);

	l2tp_tunnel_closeall(tunnel);

	tunnel->sock = NULL;
	l2tp_tunnel_dec_refcount(tunnel);

	/* Call the original destructor */
	if (sk->sk_destruct)
		(*sk->sk_destruct)(sk);
end:
	return;
}

/* When the tunnel is closed, all the attached sessions need to go too.
 */
void l2tp_tunnel_closeall(struct l2tp_tunnel *tunnel)
{
	int hash;
	struct hlist_node *walk;
	struct hlist_node *tmp;
	struct l2tp_session *session;

	BUG_ON(tunnel == NULL);

	l2tp_info(tunnel, L2TP_MSG_CONTROL, "%s: closing all sessions...\n",
		  tunnel->name);

	write_lock_bh(&tunnel->hlist_lock);
	tunnel->acpt_newsess = false;
	for (hash = 0; hash < L2TP_HASH_SIZE; hash++) {
again:
		hlist_for_each_safe(walk, tmp, &tunnel->session_hlist[hash]) {
			session = hlist_entry(walk, struct l2tp_session, hlist);

			l2tp_info(session, L2TP_MSG_CONTROL,
				  "%s: closing session\n", session->name);

			hlist_del_init(&session->hlist);

			if (test_and_set_bit(0, &session->dead))
				goto again;

			if (session->ref != NULL)
				(*session->ref)(session);

			write_unlock_bh(&tunnel->hlist_lock);

			__l2tp_session_unhash(session);
			l2tp_session_queue_purge(session);

			if (session->session_close != NULL)
				(*session->session_close)(session);

			if (session->deref != NULL)
				(*session->deref)(session);

			l2tp_session_dec_refcount(session);

			write_lock_bh(&tunnel->hlist_lock);

			/* Now restart from the beginning of this hash
			 * chain.  We always remove a session from the
			 * list so we are guaranteed to make forward
			 * progress.
			 */
			goto again;
		}
	}
	write_unlock_bh(&tunnel->hlist_lock);
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_closeall);

/* Tunnel socket destroy hook for UDP encapsulation */
static void l2tp_udp_encap_destroy(struct sock *sk)
{
	struct l2tp_tunnel *tunnel = l2tp_sock_to_tunnel(sk);
	if (tunnel) {
		l2tp_tunnel_closeall(tunnel);
		sock_put(sk);
	}
}

/* Workqueue tunnel deletion function */
static void l2tp_tunnel_del_work(struct work_struct *work)
{
	struct l2tp_tunnel *tunnel = NULL;
	struct socket *sock = NULL;
	struct sock *sk = NULL;

	tunnel = container_of(work, struct l2tp_tunnel, del_work);

	l2tp_tunnel_closeall(tunnel);

	sk = l2tp_tunnel_sock_lookup(tunnel);
	if (!sk)
		goto out;

	sock = sk->sk_socket;

	/* If the tunnel socket was created by userspace, then go through the
	 * inet layer to shut the socket down, and let userspace close it.
	 * Otherwise, if we created the socket directly within the kernel, use
	 * the sk API to release it here.
	 * In either case the tunnel resources are freed in the socket
	 * destructor when the tunnel socket goes away.
	 */
	if (tunnel->fd >= 0) {
		if (sock)
			inet_shutdown(sock, 2);
	} else {
		if (sock) {
			kernel_sock_shutdown(sock, SHUT_RDWR);
			sock_release(sock);
		}
	}

	l2tp_tunnel_sock_put(sk);
out:
	l2tp_tunnel_dec_refcount(tunnel);
}

/* Create a socket for the tunnel, if one isn't set up by
 * userspace. This is used for static tunnels where there is no
 * managing L2TP daemon.
 *
 * Since we don't want these sockets to keep a namespace alive by
 * themselves, we drop the socket's namespace refcount after creation.
 * These sockets are freed when the namespace exits using the pernet
 * exit hook.
 */
static int l2tp_tunnel_sock_create(struct net *net,
				u32 tunnel_id,
				u32 peer_tunnel_id,
				struct l2tp_tunnel_cfg *cfg,
				struct socket **sockp)
{
	int err = -EINVAL;
	struct socket *sock = NULL;
	struct udp_port_cfg udp_conf;

	switch (cfg->encap) {
	case L2TP_ENCAPTYPE_UDP:
		memset(&udp_conf, 0, sizeof(udp_conf));

#if IS_ENABLED(CONFIG_IPV6)
		if (cfg->local_ip6 && cfg->peer_ip6) {
			udp_conf.family = AF_INET6;
			memcpy(&udp_conf.local_ip6, cfg->local_ip6,
			       sizeof(udp_conf.local_ip6));
			memcpy(&udp_conf.peer_ip6, cfg->peer_ip6,
			       sizeof(udp_conf.peer_ip6));
			udp_conf.use_udp6_tx_checksums =
			  ! cfg->udp6_zero_tx_checksums;
			udp_conf.use_udp6_rx_checksums =
			  ! cfg->udp6_zero_rx_checksums;
		} else
#endif
		{
			udp_conf.family = AF_INET;
			udp_conf.local_ip = cfg->local_ip;
			udp_conf.peer_ip = cfg->peer_ip;
			udp_conf.use_udp_checksums = cfg->use_udp_checksums;
		}

		udp_conf.local_udp_port = htons(cfg->local_udp_port);
		udp_conf.peer_udp_port = htons(cfg->peer_udp_port);

		err = udp_sock_create(net, &udp_conf, &sock);
		if (err < 0)
			goto out;

		break;

	case L2TP_ENCAPTYPE_IP:
#if IS_ENABLED(CONFIG_IPV6)
		if (cfg->local_ip6 && cfg->peer_ip6) {
			struct sockaddr_l2tpip6 ip6_addr = {0};

			err = sock_create_kern(net, AF_INET6, SOCK_DGRAM,
					  IPPROTO_L2TP, &sock);
			if (err < 0)
				goto out;

			ip6_addr.l2tp_family = AF_INET6;
			memcpy(&ip6_addr.l2tp_addr, cfg->local_ip6,
			       sizeof(ip6_addr.l2tp_addr));
			ip6_addr.l2tp_conn_id = tunnel_id;
			err = kernel_bind(sock, (struct sockaddr *) &ip6_addr,
					  sizeof(ip6_addr));
			if (err < 0)
				goto out;

			ip6_addr.l2tp_family = AF_INET6;
			memcpy(&ip6_addr.l2tp_addr, cfg->peer_ip6,
			       sizeof(ip6_addr.l2tp_addr));
			ip6_addr.l2tp_conn_id = peer_tunnel_id;
			err = kernel_connect(sock,
					     (struct sockaddr *) &ip6_addr,
					     sizeof(ip6_addr), 0);
			if (err < 0)
				goto out;
		} else
#endif
		{
			struct sockaddr_l2tpip ip_addr = {0};

			err = sock_create_kern(net, AF_INET, SOCK_DGRAM,
					  IPPROTO_L2TP, &sock);
			if (err < 0)
				goto out;

			ip_addr.l2tp_family = AF_INET;
			ip_addr.l2tp_addr = cfg->local_ip;
			ip_addr.l2tp_conn_id = tunnel_id;
			err = kernel_bind(sock, (struct sockaddr *) &ip_addr,
					  sizeof(ip_addr));
			if (err < 0)
				goto out;

			ip_addr.l2tp_family = AF_INET;
			ip_addr.l2tp_addr = cfg->peer_ip;
			ip_addr.l2tp_conn_id = peer_tunnel_id;
			err = kernel_connect(sock, (struct sockaddr *) &ip_addr,
					     sizeof(ip_addr), 0);
			if (err < 0)
				goto out;
		}
		break;

	default:
		goto out;
	}

out:
	*sockp = sock;
	if ((err < 0) && sock) {
		kernel_sock_shutdown(sock, SHUT_RDWR);
		sock_release(sock);
		*sockp = NULL;
	}

	return err;
}

static struct lock_class_key l2tp_socket_class;

int l2tp_tunnel_create(struct net *net, int fd, int version, u32 tunnel_id, u32 peer_tunnel_id, struct l2tp_tunnel_cfg *cfg, struct l2tp_tunnel **tunnelp)
{
	struct l2tp_tunnel *tunnel = NULL;
	int err;
	struct socket *sock = NULL;
	struct sock *sk = NULL;
	struct l2tp_net *pn;
	enum l2tp_encap_type encap = L2TP_ENCAPTYPE_UDP;

	/* Get the tunnel socket from the fd, which was opened by
	 * the userspace L2TP daemon. If not specified, create a
	 * kernel socket.
	 */
	if (fd < 0) {
		err = l2tp_tunnel_sock_create(net, tunnel_id, peer_tunnel_id,
				cfg, &sock);
		if (err < 0)
			goto err;
	} else {
		sock = sockfd_lookup(fd, &err);
		if (!sock) {
			pr_err("tunl %u: sockfd_lookup(fd=%d) returned %d\n",
			       tunnel_id, fd, err);
			err = -EBADF;
			goto err;
		}

		/* Reject namespace mismatches */
		if (!net_eq(sock_net(sock->sk), net)) {
			pr_err("tunl %u: netns mismatch\n", tunnel_id);
			err = -EINVAL;
			goto err;
		}
	}

	sk = sock->sk;

	if (cfg != NULL)
		encap = cfg->encap;

	/* Quick sanity checks */
	err = -EPROTONOSUPPORT;
	if (sk->sk_type != SOCK_DGRAM) {
		pr_debug("tunl %hu: fd %d wrong socket type\n",
			 tunnel_id, fd);
		goto err;
	}
	switch (encap) {
	case L2TP_ENCAPTYPE_UDP:
		if (sk->sk_protocol != IPPROTO_UDP) {
			pr_err("tunl %hu: fd %d wrong protocol, got %d, expected %d\n",
			       tunnel_id, fd, sk->sk_protocol, IPPROTO_UDP);
			goto err;
		}
		break;
	case L2TP_ENCAPTYPE_IP:
		if (sk->sk_protocol != IPPROTO_L2TP) {
			pr_err("tunl %hu: fd %d wrong protocol, got %d, expected %d\n",
			       tunnel_id, fd, sk->sk_protocol, IPPROTO_L2TP);
			goto err;
		}
		break;
	}

	/* Check if this socket has already been prepped */
	tunnel = l2tp_tunnel(sk);
	if (tunnel != NULL) {
		/* This socket has already been prepped */
		err = -EBUSY;
		goto err;
	}

	tunnel = kzalloc(sizeof(struct l2tp_tunnel), GFP_KERNEL);
	if (tunnel == NULL) {
		err = -ENOMEM;
		goto err;
	}

	tunnel->version = version;
	tunnel->tunnel_id = tunnel_id;
	tunnel->peer_tunnel_id = peer_tunnel_id;
	tunnel->debug = L2TP_DEFAULT_DEBUG_FLAGS;

	tunnel->magic = L2TP_TUNNEL_MAGIC;
	sprintf(&tunnel->name[0], "tunl %u", tunnel_id);
	rwlock_init(&tunnel->hlist_lock);
	tunnel->acpt_newsess = true;

	/* The net we belong to */
	tunnel->l2tp_net = net;
	pn = l2tp_pernet(net);

	if (cfg != NULL)
		tunnel->debug = cfg->debug;

	/* Mark socket as an encapsulation socket. See net/ipv4/udp.c */
	tunnel->encap = encap;
	if (encap == L2TP_ENCAPTYPE_UDP) {
		struct udp_tunnel_sock_cfg udp_cfg = { };

		udp_cfg.sk_user_data = tunnel;
		udp_cfg.encap_type = UDP_ENCAP_L2TPINUDP;
		udp_cfg.encap_rcv = l2tp_udp_encap_recv;
		udp_cfg.encap_destroy = l2tp_udp_encap_destroy;

		setup_udp_tunnel_sock(net, sock, &udp_cfg);
	} else {
		sk->sk_user_data = tunnel;
	}

	/* Hook on the tunnel socket destructor so that we can cleanup
	 * if the tunnel socket goes away.
	 */
	tunnel->old_sk_destruct = sk->sk_destruct;
	sk->sk_destruct = &l2tp_tunnel_destruct;
	tunnel->sock = sk;
	tunnel->fd = fd;
	lockdep_set_class_and_name(&sk->sk_lock.slock, &l2tp_socket_class, "l2tp_sock");

	sk->sk_allocation = GFP_ATOMIC;

	/* Init delete workqueue struct */
	INIT_WORK(&tunnel->del_work, l2tp_tunnel_del_work);

	/* Add tunnel to our list */
	INIT_LIST_HEAD(&tunnel->list);
	atomic_inc(&l2tp_tunnel_count);

	/* Bump the reference count. The tunnel context is deleted
	 * only when this drops to zero. Must be done before list insertion
	 */
	refcount_set(&tunnel->ref_count, 1);
	spin_lock_bh(&pn->l2tp_tunnel_list_lock);
	list_add_rcu(&tunnel->list, &pn->l2tp_tunnel_list);
	spin_unlock_bh(&pn->l2tp_tunnel_list_lock);

	err = 0;
err:
	if (tunnelp)
		*tunnelp = tunnel;

	/* If tunnel's socket was created by the kernel, it doesn't
	 *  have a file.
	 */
	if (sock && sock->file)
		sockfd_put(sock);

	return err;
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_create);

/* This function is used by the netlink TUNNEL_DELETE command.
 */
void l2tp_tunnel_delete(struct l2tp_tunnel *tunnel)
{
	if (!test_and_set_bit(0, &tunnel->dead)) {
		l2tp_tunnel_inc_refcount(tunnel);
		queue_work(l2tp_wq, &tunnel->del_work);
	}
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_delete);

/* Really kill the session.
 */
void l2tp_session_free(struct l2tp_session *session)
{
	struct l2tp_tunnel *tunnel = session->tunnel;

	BUG_ON(refcount_read(&session->ref_count) != 0);

	if (tunnel) {
		BUG_ON(tunnel->magic != L2TP_TUNNEL_MAGIC);
		if (session->session_id != 0)
			atomic_dec(&l2tp_session_count);
		sock_put(tunnel->sock);
		session->tunnel = NULL;
		l2tp_tunnel_dec_refcount(tunnel);
	}

	kfree(session);
}
EXPORT_SYMBOL_GPL(l2tp_session_free);

/* Remove an l2tp session from l2tp_core's hash lists.
 * Provides a tidyup interface for pseudowire code which can't just route all
 * shutdown via. l2tp_session_delete and a pseudowire-specific session_close
 * callback.
 */
void __l2tp_session_unhash(struct l2tp_session *session)
{
	struct l2tp_tunnel *tunnel = session->tunnel;

	/* Remove the session from core hashes */
	if (tunnel) {
		/* Remove from the per-tunnel hash */
		write_lock_bh(&tunnel->hlist_lock);
		hlist_del_init(&session->hlist);
		write_unlock_bh(&tunnel->hlist_lock);

		/* For L2TPv3 we have a per-net hash: remove from there, too */
		if (tunnel->version != L2TP_HDR_VER_2) {
			struct l2tp_net *pn = l2tp_pernet(tunnel->l2tp_net);
			spin_lock_bh(&pn->l2tp_session_hlist_lock);
			hlist_del_init_rcu(&session->global_hlist);
			spin_unlock_bh(&pn->l2tp_session_hlist_lock);
			synchronize_rcu();
		}
	}
}
EXPORT_SYMBOL_GPL(__l2tp_session_unhash);

/* This function is used by the netlink SESSION_DELETE command and by
   pseudowire modules.
 */
int l2tp_session_delete(struct l2tp_session *session)
{
	if (test_and_set_bit(0, &session->dead))
		return 0;

	if (session->ref)
		(*session->ref)(session);
	__l2tp_session_unhash(session);
	l2tp_session_queue_purge(session);
	if (session->session_close != NULL)
		(*session->session_close)(session);
	if (session->deref)
		(*session->deref)(session);
	l2tp_session_dec_refcount(session);
	return 0;
}
EXPORT_SYMBOL_GPL(l2tp_session_delete);

/* We come here whenever a session's send_seq, cookie_len or
 * l2specific_len parameters are set.
 */
void l2tp_session_set_header_len(struct l2tp_session *session, int version)
{
	if (version == L2TP_HDR_VER_2) {
		session->hdr_len = 6;
		if (session->send_seq)
			session->hdr_len += 4;
	} else {
		session->hdr_len = 4 + session->cookie_len + session->l2specific_len + session->offset;
		if (session->tunnel->encap == L2TP_ENCAPTYPE_UDP)
			session->hdr_len += 4;
	}

}
EXPORT_SYMBOL_GPL(l2tp_session_set_header_len);

struct l2tp_session *l2tp_session_create(int priv_size, struct l2tp_tunnel *tunnel, u32 session_id, u32 peer_session_id, struct l2tp_session_cfg *cfg)
{
	struct l2tp_session *session;
	int err;

	session = kzalloc(sizeof(struct l2tp_session) + priv_size, GFP_KERNEL);
	if (session != NULL) {
		session->magic = L2TP_SESSION_MAGIC;
		session->tunnel = tunnel;

		session->session_id = session_id;
		session->peer_session_id = peer_session_id;
		session->nr = 0;
		if (tunnel->version == L2TP_HDR_VER_2)
			session->nr_max = 0xffff;
		else
			session->nr_max = 0xffffff;
		session->nr_window_size = session->nr_max / 2;
		session->nr_oos_count_max = 4;

		/* Use NR of first received packet */
		session->reorder_skip = 1;

		sprintf(&session->name[0], "sess %u/%u",
			tunnel->tunnel_id, session->session_id);

		skb_queue_head_init(&session->reorder_q);

		INIT_HLIST_NODE(&session->hlist);
		INIT_HLIST_NODE(&session->global_hlist);

		/* Inherit debug options from tunnel */
		session->debug = tunnel->debug;

		if (cfg) {
			session->pwtype = cfg->pw_type;
			session->debug = cfg->debug;
			session->mtu = cfg->mtu;
			session->mru = cfg->mru;
			session->send_seq = cfg->send_seq;
			session->recv_seq = cfg->recv_seq;
			session->lns_mode = cfg->lns_mode;
			session->reorder_timeout = cfg->reorder_timeout;
			session->offset = cfg->offset;
			session->l2specific_type = cfg->l2specific_type;
			session->l2specific_len = cfg->l2specific_len;
			session->cookie_len = cfg->cookie_len;
			memcpy(&session->cookie[0], &cfg->cookie[0], cfg->cookie_len);
			session->peer_cookie_len = cfg->peer_cookie_len;
			memcpy(&session->peer_cookie[0], &cfg->peer_cookie[0], cfg->peer_cookie_len);
		}

		if (tunnel->version == L2TP_HDR_VER_2)
			session->build_header = l2tp_build_l2tpv2_header;
		else
			session->build_header = l2tp_build_l2tpv3_header;

		l2tp_session_set_header_len(session, tunnel->version);

		refcount_set(&session->ref_count, 1);

		err = l2tp_session_add_to_tunnel(tunnel, session);
		if (err) {
			kfree(session);

			return ERR_PTR(err);
		}

		/* Ignore management session in session count value */
		if (session->session_id != 0)
			atomic_inc(&l2tp_session_count);

		return session;
	}

	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL_GPL(l2tp_session_create);

/*****************************************************************************
 * Init and cleanup
 *****************************************************************************/

static __net_init int l2tp_init_net(struct net *net)
{
	struct l2tp_net *pn = net_generic(net, l2tp_net_id);
	int hash;

	INIT_LIST_HEAD(&pn->l2tp_tunnel_list);
	spin_lock_init(&pn->l2tp_tunnel_list_lock);

	for (hash = 0; hash < L2TP_HASH_SIZE_2; hash++)
		INIT_HLIST_HEAD(&pn->l2tp_session_hlist[hash]);

	spin_lock_init(&pn->l2tp_session_hlist_lock);

	return 0;
}

static __net_exit void l2tp_exit_net(struct net *net)
{
	struct l2tp_net *pn = l2tp_pernet(net);
	struct l2tp_tunnel *tunnel = NULL;

	rcu_read_lock_bh();
	list_for_each_entry_rcu(tunnel, &pn->l2tp_tunnel_list, list) {
		l2tp_tunnel_delete(tunnel);
	}
	rcu_read_unlock_bh();

	flush_workqueue(l2tp_wq);
	rcu_barrier();
}

static struct pernet_operations l2tp_net_ops = {
	.init = l2tp_init_net,
	.exit = l2tp_exit_net,
	.id   = &l2tp_net_id,
	.size = sizeof(struct l2tp_net),
};

static int __init l2tp_init(void)
{
	int rc = 0;

	rc = register_pernet_device(&l2tp_net_ops);
	if (rc)
		goto out;

	l2tp_wq = alloc_workqueue("l2tp", WQ_UNBOUND, 0);
	if (!l2tp_wq) {
		pr_err("alloc_workqueue failed\n");
		unregister_pernet_device(&l2tp_net_ops);
		rc = -ENOMEM;
		goto out;
	}

	pr_info("L2TP core driver, %s\n", L2TP_DRV_VERSION);

out:
	return rc;
}

static void __exit l2tp_exit(void)
{
	unregister_pernet_device(&l2tp_net_ops);
	if (l2tp_wq) {
		destroy_workqueue(l2tp_wq);
		l2tp_wq = NULL;
	}
}

module_init(l2tp_init);
module_exit(l2tp_exit);

MODULE_AUTHOR("James Chapman <jchapman@katalix.com>");
MODULE_DESCRIPTION("L2TP core");
MODULE_LICENSE("GPL");
MODULE_VERSION(L2TP_DRV_VERSION);

