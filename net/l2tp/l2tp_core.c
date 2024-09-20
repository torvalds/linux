// SPDX-License-Identifier: GPL-2.0-only
/* L2TP core.
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

#define CREATE_TRACE_POINTS
#include "trace.h"

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

#define L2TP_HDR_SIZE_MAX		14

/* Default trace flags */
#define L2TP_DEFAULT_DEBUG_FLAGS	0

#define L2TP_DEPTH_NESTING		2
#if L2TP_DEPTH_NESTING == SINGLE_DEPTH_NESTING
#error "L2TP requires its own lockdep subclass"
#endif

/* Private data stored for received packets in the skb.
 */
struct l2tp_skb_cb {
	u32			ns;
	u16			has_seq;
	u16			length;
	unsigned long		expires;
};

#define L2TP_SKB_CB(skb)	((struct l2tp_skb_cb *)&(skb)->cb[sizeof(struct inet_skb_parm)])

static struct workqueue_struct *l2tp_wq;

/* per-net private data for this module */
static unsigned int l2tp_net_id;
struct l2tp_net {
	/* Lock for write access to l2tp_tunnel_idr */
	spinlock_t l2tp_tunnel_idr_lock;
	struct idr l2tp_tunnel_idr;
	/* Lock for write access to l2tp_v[23]_session_idr/htable */
	spinlock_t l2tp_session_idr_lock;
	struct idr l2tp_v2_session_idr;
	struct idr l2tp_v3_session_idr;
	struct hlist_head l2tp_v3_session_htable[16];
};

static inline u32 l2tp_v2_session_key(u16 tunnel_id, u16 session_id)
{
	return ((u32)tunnel_id) << 16 | session_id;
}

static inline unsigned long l2tp_v3_session_hashkey(struct sock *sk, u32 session_id)
{
	return ((unsigned long)sk) + session_id;
}

#if IS_ENABLED(CONFIG_IPV6)
static bool l2tp_sk_is_v6(struct sock *sk)
{
	return sk->sk_family == PF_INET6 &&
	       !ipv6_addr_v4mapped(&sk->sk_v6_daddr);
}
#endif

static inline struct l2tp_net *l2tp_pernet(const struct net *net)
{
	return net_generic(net, l2tp_net_id);
}

static void l2tp_tunnel_free(struct l2tp_tunnel *tunnel)
{
	trace_free_tunnel(tunnel);
	sock_put(tunnel->sock);
	/* the tunnel is freed in the socket destructor */
}

static void l2tp_session_free(struct l2tp_session *session)
{
	trace_free_session(session);
	if (session->tunnel)
		l2tp_tunnel_dec_refcount(session->tunnel);
	kfree(session);
}

struct l2tp_tunnel *l2tp_sk_to_tunnel(struct sock *sk)
{
	struct l2tp_tunnel *tunnel = sk->sk_user_data;

	if (tunnel)
		if (WARN_ON(tunnel->magic != L2TP_TUNNEL_MAGIC))
			return NULL;

	return tunnel;
}
EXPORT_SYMBOL_GPL(l2tp_sk_to_tunnel);

void l2tp_tunnel_inc_refcount(struct l2tp_tunnel *tunnel)
{
	refcount_inc(&tunnel->ref_count);
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_inc_refcount);

void l2tp_tunnel_dec_refcount(struct l2tp_tunnel *tunnel)
{
	if (refcount_dec_and_test(&tunnel->ref_count))
		l2tp_tunnel_free(tunnel);
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_dec_refcount);

void l2tp_session_inc_refcount(struct l2tp_session *session)
{
	refcount_inc(&session->ref_count);
}
EXPORT_SYMBOL_GPL(l2tp_session_inc_refcount);

void l2tp_session_dec_refcount(struct l2tp_session *session)
{
	if (refcount_dec_and_test(&session->ref_count))
		l2tp_session_free(session);
}
EXPORT_SYMBOL_GPL(l2tp_session_dec_refcount);

/* Lookup a tunnel. A new reference is held on the returned tunnel. */
struct l2tp_tunnel *l2tp_tunnel_get(const struct net *net, u32 tunnel_id)
{
	const struct l2tp_net *pn = l2tp_pernet(net);
	struct l2tp_tunnel *tunnel;

	rcu_read_lock_bh();
	tunnel = idr_find(&pn->l2tp_tunnel_idr, tunnel_id);
	if (tunnel && refcount_inc_not_zero(&tunnel->ref_count)) {
		rcu_read_unlock_bh();
		return tunnel;
	}
	rcu_read_unlock_bh();

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_get);

struct l2tp_tunnel *l2tp_tunnel_get_nth(const struct net *net, int nth)
{
	struct l2tp_net *pn = l2tp_pernet(net);
	unsigned long tunnel_id, tmp;
	struct l2tp_tunnel *tunnel;
	int count = 0;

	rcu_read_lock_bh();
	idr_for_each_entry_ul(&pn->l2tp_tunnel_idr, tunnel, tmp, tunnel_id) {
		if (tunnel && ++count > nth &&
		    refcount_inc_not_zero(&tunnel->ref_count)) {
			rcu_read_unlock_bh();
			return tunnel;
		}
	}
	rcu_read_unlock_bh();

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_get_nth);

struct l2tp_session *l2tp_v3_session_get(const struct net *net, struct sock *sk, u32 session_id)
{
	const struct l2tp_net *pn = l2tp_pernet(net);
	struct l2tp_session *session;

	rcu_read_lock_bh();
	session = idr_find(&pn->l2tp_v3_session_idr, session_id);
	if (session && !hash_hashed(&session->hlist) &&
	    refcount_inc_not_zero(&session->ref_count)) {
		rcu_read_unlock_bh();
		return session;
	}

	/* If we get here and session is non-NULL, the session_id
	 * collides with one in another tunnel. If sk is non-NULL,
	 * find the session matching sk.
	 */
	if (session && sk) {
		unsigned long key = l2tp_v3_session_hashkey(sk, session->session_id);

		hash_for_each_possible_rcu(pn->l2tp_v3_session_htable, session,
					   hlist, key) {
			if (session->tunnel->sock == sk &&
			    refcount_inc_not_zero(&session->ref_count)) {
				rcu_read_unlock_bh();
				return session;
			}
		}
	}
	rcu_read_unlock_bh();

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_v3_session_get);

struct l2tp_session *l2tp_v2_session_get(const struct net *net, u16 tunnel_id, u16 session_id)
{
	u32 session_key = l2tp_v2_session_key(tunnel_id, session_id);
	const struct l2tp_net *pn = l2tp_pernet(net);
	struct l2tp_session *session;

	rcu_read_lock_bh();
	session = idr_find(&pn->l2tp_v2_session_idr, session_key);
	if (session && refcount_inc_not_zero(&session->ref_count)) {
		rcu_read_unlock_bh();
		return session;
	}
	rcu_read_unlock_bh();

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_v2_session_get);

struct l2tp_session *l2tp_session_get(const struct net *net, struct sock *sk, int pver,
				      u32 tunnel_id, u32 session_id)
{
	if (pver == L2TP_HDR_VER_2)
		return l2tp_v2_session_get(net, tunnel_id, session_id);
	else
		return l2tp_v3_session_get(net, sk, session_id);
}
EXPORT_SYMBOL_GPL(l2tp_session_get);

struct l2tp_session *l2tp_session_get_nth(struct l2tp_tunnel *tunnel, int nth)
{
	struct l2tp_session *session;
	int count = 0;

	rcu_read_lock_bh();
	list_for_each_entry_rcu(session, &tunnel->session_list, list) {
		if (++count > nth) {
			l2tp_session_inc_refcount(session);
			rcu_read_unlock_bh();
			return session;
		}
	}
	rcu_read_unlock_bh();

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_session_get_nth);

/* Lookup a session by interface name.
 * This is very inefficient but is only used by management interfaces.
 */
struct l2tp_session *l2tp_session_get_by_ifname(const struct net *net,
						const char *ifname)
{
	struct l2tp_net *pn = l2tp_pernet(net);
	unsigned long tunnel_id, tmp;
	struct l2tp_session *session;
	struct l2tp_tunnel *tunnel;

	rcu_read_lock_bh();
	idr_for_each_entry_ul(&pn->l2tp_tunnel_idr, tunnel, tmp, tunnel_id) {
		if (tunnel) {
			list_for_each_entry_rcu(session, &tunnel->session_list, list) {
				if (!strcmp(session->ifname, ifname)) {
					l2tp_session_inc_refcount(session);
					rcu_read_unlock_bh();

					return session;
				}
			}
		}
	}
	rcu_read_unlock_bh();

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_session_get_by_ifname);

static void l2tp_session_coll_list_add(struct l2tp_session_coll_list *clist,
				       struct l2tp_session *session)
{
	l2tp_session_inc_refcount(session);
	WARN_ON_ONCE(session->coll_list);
	session->coll_list = clist;
	spin_lock(&clist->lock);
	list_add(&session->clist, &clist->list);
	spin_unlock(&clist->lock);
}

static int l2tp_session_collision_add(struct l2tp_net *pn,
				      struct l2tp_session *session1,
				      struct l2tp_session *session2)
{
	struct l2tp_session_coll_list *clist;

	lockdep_assert_held(&pn->l2tp_session_idr_lock);

	if (!session2)
		return -EEXIST;

	/* If existing session is in IP-encap tunnel, refuse new session */
	if (session2->tunnel->encap == L2TP_ENCAPTYPE_IP)
		return -EEXIST;

	clist = session2->coll_list;
	if (!clist) {
		/* First collision. Allocate list to manage the collided sessions
		 * and add the existing session to the list.
		 */
		clist = kmalloc(sizeof(*clist), GFP_ATOMIC);
		if (!clist)
			return -ENOMEM;

		spin_lock_init(&clist->lock);
		INIT_LIST_HEAD(&clist->list);
		refcount_set(&clist->ref_count, 1);
		l2tp_session_coll_list_add(clist, session2);
	}

	/* If existing session isn't already in the session hlist, add it. */
	if (!hash_hashed(&session2->hlist))
		hash_add(pn->l2tp_v3_session_htable, &session2->hlist,
			 session2->hlist_key);

	/* Add new session to the hlist and collision list */
	hash_add(pn->l2tp_v3_session_htable, &session1->hlist,
		 session1->hlist_key);
	refcount_inc(&clist->ref_count);
	l2tp_session_coll_list_add(clist, session1);

	return 0;
}

static void l2tp_session_collision_del(struct l2tp_net *pn,
				       struct l2tp_session *session)
{
	struct l2tp_session_coll_list *clist = session->coll_list;
	unsigned long session_key = session->session_id;
	struct l2tp_session *session2;

	lockdep_assert_held(&pn->l2tp_session_idr_lock);

	hash_del(&session->hlist);

	if (clist) {
		/* Remove session from its collision list. If there
		 * are other sessions with the same ID, replace this
		 * session's IDR entry with that session, otherwise
		 * remove the IDR entry. If this is the last session,
		 * the collision list data is freed.
		 */
		spin_lock(&clist->lock);
		list_del_init(&session->clist);
		session2 = list_first_entry_or_null(&clist->list, struct l2tp_session, clist);
		if (session2) {
			void *old = idr_replace(&pn->l2tp_v3_session_idr, session2, session_key);

			WARN_ON_ONCE(IS_ERR_VALUE(old));
		} else {
			void *removed = idr_remove(&pn->l2tp_v3_session_idr, session_key);

			WARN_ON_ONCE(removed != session);
		}
		session->coll_list = NULL;
		spin_unlock(&clist->lock);
		if (refcount_dec_and_test(&clist->ref_count))
			kfree(clist);
		l2tp_session_dec_refcount(session);
	}
}

int l2tp_session_register(struct l2tp_session *session,
			  struct l2tp_tunnel *tunnel)
{
	struct l2tp_net *pn = l2tp_pernet(tunnel->l2tp_net);
	struct l2tp_session *other_session = NULL;
	u32 session_key;
	int err;

	spin_lock_bh(&tunnel->list_lock);
	spin_lock_bh(&pn->l2tp_session_idr_lock);

	if (!tunnel->acpt_newsess) {
		err = -ENODEV;
		goto out;
	}

	if (tunnel->version == L2TP_HDR_VER_3) {
		session_key = session->session_id;
		err = idr_alloc_u32(&pn->l2tp_v3_session_idr, NULL,
				    &session_key, session_key, GFP_ATOMIC);
		/* IP encap expects session IDs to be globally unique, while
		 * UDP encap doesn't. This isn't per the RFC, which says that
		 * sessions are identified only by the session ID, but is to
		 * support existing userspace which depends on it.
		 */
		if (err == -ENOSPC && tunnel->encap == L2TP_ENCAPTYPE_UDP) {
			other_session = idr_find(&pn->l2tp_v3_session_idr,
						 session_key);
			err = l2tp_session_collision_add(pn, session,
							 other_session);
		}
	} else {
		session_key = l2tp_v2_session_key(tunnel->tunnel_id,
						  session->session_id);
		err = idr_alloc_u32(&pn->l2tp_v2_session_idr, NULL,
				    &session_key, session_key, GFP_ATOMIC);
	}

	if (err) {
		if (err == -ENOSPC)
			err = -EEXIST;
		goto out;
	}

	l2tp_tunnel_inc_refcount(tunnel);
	list_add(&session->list, &tunnel->session_list);

	if (tunnel->version == L2TP_HDR_VER_3) {
		if (!other_session)
			idr_replace(&pn->l2tp_v3_session_idr, session, session_key);
	} else {
		idr_replace(&pn->l2tp_v2_session_idr, session, session_key);
	}

out:
	spin_unlock_bh(&pn->l2tp_session_idr_lock);
	spin_unlock_bh(&tunnel->list_lock);

	if (!err)
		trace_register_session(session);

	return err;
}
EXPORT_SYMBOL_GPL(l2tp_session_register);

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
		trace_session_seqnum_update(session);
	}

	/* call private receive handler */
	if (session->recv_skb)
		(*session->recv_skb)(session, skb, L2TP_SKB_CB(skb)->length);
	else
		kfree_skb(skb);
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
		struct l2tp_skb_cb *cb = L2TP_SKB_CB(skb);

		/* If the packet has been pending on the queue for too long, discard it */
		if (time_after(jiffies, cb->expires)) {
			atomic_long_inc(&session->stats.rx_seq_discards);
			atomic_long_inc(&session->stats.rx_errors);
			trace_session_pkt_expired(session, cb->ns);
			session->reorder_skip = 1;
			__skb_unlink(skb, &session->reorder_q);
			kfree_skb(skb);
			continue;
		}

		if (cb->has_seq) {
			if (session->reorder_skip) {
				session->reorder_skip = 0;
				session->nr = cb->ns;
				trace_session_seqnum_reset(session);
			}
			if (cb->ns != session->nr)
				goto out;
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
	struct l2tp_skb_cb *cb = L2TP_SKB_CB(skb);

	if (!l2tp_seq_check_rx_window(session, cb->ns)) {
		/* Packet sequence number is outside allowed window.
		 * Discard it.
		 */
		trace_session_pkt_outside_rx_window(session, cb->ns);
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
	if (cb->ns == session->nr) {
		skb_queue_tail(&session->reorder_q, skb);
	} else {
		u32 nr_oos = cb->ns;
		u32 nr_next = (session->nr_oos + 1) & session->nr_max;

		if (nr_oos == nr_next)
			session->nr_oos_count++;
		else
			session->nr_oos_count = 0;

		session->nr_oos = nr_oos;
		if (session->nr_oos_count > session->nr_oos_count_max) {
			session->reorder_skip = 1;
		}
		if (!session->reorder_skip) {
			atomic_long_inc(&session->stats.rx_seq_discards);
			trace_session_pkt_oos(session, cb->ns);
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
 * Cookie value and sublayer format are negotiated with the peer when
 * the session is set up. Unlike L2TPv2, we do not need to parse the
 * packet header to determine if optional fields are present.
 *
 * Caller must already have parsed the frame and determined that it is
 * a data (not control) frame before coming here. Fields up to the
 * session-id have already been parsed and ptr points to the data
 * after the session-id.
 */
void l2tp_recv_common(struct l2tp_session *session, struct sk_buff *skb,
		      unsigned char *ptr, unsigned char *optr, u16 hdrflags,
		      int length)
{
	struct l2tp_tunnel *tunnel = session->tunnel;
	int offset;

	/* Parse and check optional cookie */
	if (session->peer_cookie_len > 0) {
		if (memcmp(ptr, &session->peer_cookie[0], session->peer_cookie_len)) {
			pr_debug_ratelimited("%s: cookie mismatch (%u/%u). Discarding.\n",
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
	L2TP_SKB_CB(skb)->has_seq = 0;
	if (tunnel->version == L2TP_HDR_VER_2) {
		if (hdrflags & L2TP_HDRFLAG_S) {
			/* Store L2TP info in the skb */
			L2TP_SKB_CB(skb)->ns = ntohs(*(__be16 *)ptr);
			L2TP_SKB_CB(skb)->has_seq = 1;
			ptr += 2;
			/* Skip past nr in the header */
			ptr += 2;

		}
	} else if (session->l2specific_type == L2TP_L2SPECTYPE_DEFAULT) {
		u32 l2h = ntohl(*(__be32 *)ptr);

		if (l2h & 0x40000000) {
			/* Store L2TP info in the skb */
			L2TP_SKB_CB(skb)->ns = l2h & 0x00ffffff;
			L2TP_SKB_CB(skb)->has_seq = 1;
		}
		ptr += 4;
	}

	if (L2TP_SKB_CB(skb)->has_seq) {
		/* Received a packet with sequence numbers. If we're the LAC,
		 * check if we sre sending sequence numbers and if not,
		 * configure it so.
		 */
		if (!session->lns_mode && !session->send_seq) {
			trace_session_seqnum_lns_enable(session);
			session->send_seq = 1;
			l2tp_session_set_header_len(session, tunnel->version);
		}
	} else {
		/* No sequence numbers.
		 * If user has configured mandatory sequence numbers, discard.
		 */
		if (session->recv_seq) {
			pr_debug_ratelimited("%s: recv data has no seq numbers when required. Discarding.\n",
					     session->name);
			atomic_long_inc(&session->stats.rx_seq_discards);
			goto discard;
		}

		/* If we're the LAC and we're sending sequence numbers, the
		 * LNS has requested that we no longer send sequence numbers.
		 * If we're the LNS and we're sending sequence numbers, the
		 * LAC is broken. Discard the frame.
		 */
		if (!session->lns_mode && session->send_seq) {
			trace_session_seqnum_lns_disable(session);
			session->send_seq = 0;
			l2tp_session_set_header_len(session, tunnel->version);
		} else if (session->send_seq) {
			pr_debug_ratelimited("%s: recv data has no seq numbers when required. Discarding.\n",
					     session->name);
			atomic_long_inc(&session->stats.rx_seq_discards);
			goto discard;
		}
	}

	/* Session data offset is defined only for L2TPv2 and is
	 * indicated by an optional 16-bit value in the header.
	 */
	if (tunnel->version == L2TP_HDR_VER_2) {
		/* If offset bit set, skip it. */
		if (hdrflags & L2TP_HDRFLAG_O) {
			offset = ntohs(*(__be16 *)ptr);
			ptr += 2 + offset;
		}
	}

	offset = ptr - optr;
	if (!pskb_may_pull(skb, offset))
		goto discard;

	__skb_pull(skb, offset);

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
}
EXPORT_SYMBOL_GPL(l2tp_recv_common);

/* Drop skbs from the session's reorder_q
 */
static void l2tp_session_queue_purge(struct l2tp_session *session)
{
	struct sk_buff *skb = NULL;

	while ((skb = skb_dequeue(&session->reorder_q))) {
		atomic_long_inc(&session->stats.rx_errors);
		kfree_skb(skb);
	}
}

/* UDP encapsulation receive handler. See net/ipv4/udp.c for details. */
int l2tp_udp_encap_recv(struct sock *sk, struct sk_buff *skb)
{
	struct l2tp_session *session = NULL;
	struct l2tp_tunnel *tunnel = NULL;
	struct net *net = sock_net(sk);
	unsigned char *ptr, *optr;
	u16 hdrflags;
	u16 version;
	int length;

	/* UDP has verified checksum */

	/* UDP always verifies the packet length. */
	__skb_pull(skb, sizeof(struct udphdr));

	/* Short packet? */
	if (!pskb_may_pull(skb, L2TP_HDR_SIZE_MAX))
		goto pass;

	/* Point to L2TP header */
	optr = skb->data;
	ptr = skb->data;

	/* Get L2TP header flags */
	hdrflags = ntohs(*(__be16 *)ptr);

	/* Get protocol version */
	version = hdrflags & L2TP_HDR_VER_MASK;

	/* Get length of L2TP packet */
	length = skb->len;

	/* If type is control packet, it is handled by userspace. */
	if (hdrflags & L2TP_HDRFLAG_T)
		goto pass;

	/* Skip flags */
	ptr += 2;

	if (version == L2TP_HDR_VER_2) {
		u16 tunnel_id, session_id;

		/* If length is present, skip it */
		if (hdrflags & L2TP_HDRFLAG_L)
			ptr += 2;

		/* Extract tunnel and session ID */
		tunnel_id = ntohs(*(__be16 *)ptr);
		ptr += 2;
		session_id = ntohs(*(__be16 *)ptr);
		ptr += 2;

		session = l2tp_v2_session_get(net, tunnel_id, session_id);
	} else {
		u32 session_id;

		ptr += 2;	/* skip reserved bits */
		session_id = ntohl(*(__be32 *)ptr);
		ptr += 4;

		session = l2tp_v3_session_get(net, sk, session_id);
	}

	if (!session || !session->recv_skb) {
		if (session)
			l2tp_session_dec_refcount(session);

		/* Not found? Pass to userspace to deal with */
		goto pass;
	}

	tunnel = session->tunnel;

	/* Check protocol version */
	if (version != tunnel->version)
		goto invalid;

	if (version == L2TP_HDR_VER_3 &&
	    l2tp_v3_ensure_opt_in_linear(session, skb, &ptr, &optr)) {
		l2tp_session_dec_refcount(session);
		goto invalid;
	}

	l2tp_recv_common(session, skb, ptr, optr, hdrflags, length);
	l2tp_session_dec_refcount(session);

	return 0;

invalid:
	atomic_long_inc(&tunnel->stats.rx_invalid);

pass:
	/* Put UDP header back */
	__skb_push(skb, sizeof(struct udphdr));

	return 1;
}
EXPORT_SYMBOL_GPL(l2tp_udp_encap_recv);

/* UDP encapsulation receive error handler. See net/ipv4/udp.c for details. */
static void l2tp_udp_encap_err_recv(struct sock *sk, struct sk_buff *skb, int err,
				    __be16 port, u32 info, u8 *payload)
{
	sk->sk_err = err;
	sk_error_report(sk);

	if (ip_hdr(skb)->version == IPVERSION) {
		if (inet_test_bit(RECVERR, sk))
			return ip_icmp_error(sk, skb, err, port, info, payload);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		if (inet6_test_bit(RECVERR6, sk))
			return ipv6_icmp_error(sk, skb, err, port, info, payload);
#endif
	}
}

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
		trace_session_seqnum_update(session);
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
		*((__be16 *)bufp) = htons(flags);
		bufp += 2;
		*((__be16 *)bufp) = 0;
		bufp += 2;
	}

	*((__be32 *)bufp) = htonl(session->peer_session_id);
	bufp += 4;
	if (session->cookie_len) {
		memcpy(bufp, &session->cookie[0], session->cookie_len);
		bufp += session->cookie_len;
	}
	if (session->l2specific_type == L2TP_L2SPECTYPE_DEFAULT) {
		u32 l2h = 0;

		if (session->send_seq) {
			l2h = 0x40000000 | session->ns;
			session->ns++;
			session->ns &= 0xffffff;
			trace_session_seqnum_update(session);
		}

		*((__be32 *)bufp) = htonl(l2h);
		bufp += 4;
	}

	return bufp - optr;
}

/* Queue the packet to IP for output: tunnel socket lock must be held */
static int l2tp_xmit_queue(struct l2tp_tunnel *tunnel, struct sk_buff *skb, struct flowi *fl)
{
	int err;

	skb->ignore_df = 1;
	skb_dst_drop(skb);
#if IS_ENABLED(CONFIG_IPV6)
	if (l2tp_sk_is_v6(tunnel->sock))
		err = inet6_csk_xmit(tunnel->sock, skb, NULL);
	else
#endif
		err = ip_queue_xmit(tunnel->sock, skb, fl);

	return err >= 0 ? NET_XMIT_SUCCESS : NET_XMIT_DROP;
}

static int l2tp_xmit_core(struct l2tp_session *session, struct sk_buff *skb, unsigned int *len)
{
	struct l2tp_tunnel *tunnel = session->tunnel;
	unsigned int data_len = skb->len;
	struct sock *sk = tunnel->sock;
	int headroom, uhlen, udp_len;
	int ret = NET_XMIT_SUCCESS;
	struct inet_sock *inet;
	struct udphdr *uh;

	/* Check that there's enough headroom in the skb to insert IP,
	 * UDP and L2TP headers. If not enough, expand it to
	 * make room. Adjust truesize.
	 */
	uhlen = (tunnel->encap == L2TP_ENCAPTYPE_UDP) ? sizeof(*uh) : 0;
	headroom = NET_SKB_PAD + sizeof(struct iphdr) + uhlen + session->hdr_len;
	if (skb_cow_head(skb, headroom)) {
		kfree_skb(skb);
		return NET_XMIT_DROP;
	}

	/* Setup L2TP header */
	if (tunnel->version == L2TP_HDR_VER_2)
		l2tp_build_l2tpv2_header(session, __skb_push(skb, session->hdr_len));
	else
		l2tp_build_l2tpv3_header(session, __skb_push(skb, session->hdr_len));

	/* Reset skb netfilter state */
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
	IPCB(skb)->flags &= ~(IPSKB_XFRM_TUNNEL_SIZE | IPSKB_XFRM_TRANSFORMED | IPSKB_REROUTED);
	nf_reset_ct(skb);

	/* L2TP uses its own lockdep subclass to avoid lockdep splats caused by
	 * nested socket calls on the same lockdep socket class. This can
	 * happen when data from a user socket is routed over l2tp, which uses
	 * another userspace socket.
	 */
	spin_lock_nested(&sk->sk_lock.slock, L2TP_DEPTH_NESTING);

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

	/* Report transmitted length before we add encap header, which keeps
	 * statistics consistent for both UDP and IP encap tx/rx paths.
	 */
	*len = skb->len;

	inet = inet_sk(sk);
	switch (tunnel->encap) {
	case L2TP_ENCAPTYPE_UDP:
		/* Setup UDP header */
		__skb_push(skb, sizeof(*uh));
		skb_reset_transport_header(skb);
		uh = udp_hdr(skb);
		uh->source = inet->inet_sport;
		uh->dest = inet->inet_dport;
		udp_len = uhlen + session->hdr_len + data_len;
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

	ret = l2tp_xmit_queue(tunnel, skb, &inet->cork.fl);

out_unlock:
	spin_unlock(&sk->sk_lock.slock);

	return ret;
}

/* If caller requires the skb to have a ppp header, the header must be
 * inserted in the skb data before calling this function.
 */
int l2tp_xmit_skb(struct l2tp_session *session, struct sk_buff *skb)
{
	unsigned int len = 0;
	int ret;

	ret = l2tp_xmit_core(session, skb, &len);
	if (ret == NET_XMIT_SUCCESS) {
		atomic_long_inc(&session->tunnel->stats.tx_packets);
		atomic_long_add(len, &session->tunnel->stats.tx_bytes);
		atomic_long_inc(&session->stats.tx_packets);
		atomic_long_add(len, &session->stats.tx_bytes);
	} else {
		atomic_long_inc(&session->tunnel->stats.tx_errors);
		atomic_long_inc(&session->stats.tx_errors);
	}
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
	struct l2tp_tunnel *tunnel = l2tp_sk_to_tunnel(sk);

	if (!tunnel)
		goto end;

	/* Disable udp encapsulation */
	switch (tunnel->encap) {
	case L2TP_ENCAPTYPE_UDP:
		/* No longer an encapsulation socket. See net/ipv4/udp.c */
		WRITE_ONCE(udp_sk(sk)->encap_type, 0);
		udp_sk(sk)->encap_rcv = NULL;
		udp_sk(sk)->encap_destroy = NULL;
		break;
	case L2TP_ENCAPTYPE_IP:
		break;
	}

	/* Remove hooks into tunnel socket */
	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_destruct = tunnel->old_sk_destruct;
	sk->sk_user_data = NULL;
	write_unlock_bh(&sk->sk_callback_lock);

	/* Call the original destructor */
	if (sk->sk_destruct)
		(*sk->sk_destruct)(sk);

	kfree_rcu(tunnel, rcu);
end:
	return;
}

/* Remove an l2tp session from l2tp_core's lists. */
static void l2tp_session_unhash(struct l2tp_session *session)
{
	struct l2tp_tunnel *tunnel = session->tunnel;

	if (tunnel) {
		struct l2tp_net *pn = l2tp_pernet(tunnel->l2tp_net);
		struct l2tp_session *removed = session;

		spin_lock_bh(&tunnel->list_lock);
		spin_lock_bh(&pn->l2tp_session_idr_lock);

		/* Remove from the per-tunnel list */
		list_del_init(&session->list);

		/* Remove from per-net IDR */
		if (tunnel->version == L2TP_HDR_VER_3) {
			if (hash_hashed(&session->hlist))
				l2tp_session_collision_del(pn, session);
			else
				removed = idr_remove(&pn->l2tp_v3_session_idr,
						     session->session_id);
		} else {
			u32 session_key = l2tp_v2_session_key(tunnel->tunnel_id,
							      session->session_id);
			removed = idr_remove(&pn->l2tp_v2_session_idr,
					     session_key);
		}
		WARN_ON_ONCE(removed && removed != session);

		spin_unlock_bh(&pn->l2tp_session_idr_lock);
		spin_unlock_bh(&tunnel->list_lock);

		synchronize_rcu();
	}
}

/* When the tunnel is closed, all the attached sessions need to go too.
 */
static void l2tp_tunnel_closeall(struct l2tp_tunnel *tunnel)
{
	struct l2tp_session *session;

	spin_lock_bh(&tunnel->list_lock);
	tunnel->acpt_newsess = false;
	for (;;) {
		session = list_first_entry_or_null(&tunnel->session_list,
						   struct l2tp_session, list);
		if (!session)
			break;
		l2tp_session_inc_refcount(session);
		list_del_init(&session->list);
		spin_unlock_bh(&tunnel->list_lock);
		l2tp_session_delete(session);
		spin_lock_bh(&tunnel->list_lock);
		l2tp_session_dec_refcount(session);
	}
	spin_unlock_bh(&tunnel->list_lock);
}

/* Tunnel socket destroy hook for UDP encapsulation */
static void l2tp_udp_encap_destroy(struct sock *sk)
{
	struct l2tp_tunnel *tunnel = l2tp_sk_to_tunnel(sk);

	if (tunnel)
		l2tp_tunnel_delete(tunnel);
}

static void l2tp_tunnel_remove(struct net *net, struct l2tp_tunnel *tunnel)
{
	struct l2tp_net *pn = l2tp_pernet(net);

	spin_lock_bh(&pn->l2tp_tunnel_idr_lock);
	idr_remove(&pn->l2tp_tunnel_idr, tunnel->tunnel_id);
	spin_unlock_bh(&pn->l2tp_tunnel_idr_lock);
}

/* Workqueue tunnel deletion function */
static void l2tp_tunnel_del_work(struct work_struct *work)
{
	struct l2tp_tunnel *tunnel = container_of(work, struct l2tp_tunnel,
						  del_work);
	struct sock *sk = tunnel->sock;
	struct socket *sock = sk->sk_socket;

	l2tp_tunnel_closeall(tunnel);

	/* If the tunnel socket was created within the kernel, use
	 * the sk API to release it here.
	 */
	if (tunnel->fd < 0) {
		if (sock) {
			kernel_sock_shutdown(sock, SHUT_RDWR);
			sock_release(sock);
		}
	}

	l2tp_tunnel_remove(tunnel->l2tp_net, tunnel);
	/* drop initial ref */
	l2tp_tunnel_dec_refcount(tunnel);

	/* drop workqueue ref */
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
			  !cfg->udp6_zero_tx_checksums;
			udp_conf.use_udp6_rx_checksums =
			  !cfg->udp6_zero_rx_checksums;
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
			err = kernel_bind(sock, (struct sockaddr *)&ip6_addr,
					  sizeof(ip6_addr));
			if (err < 0)
				goto out;

			ip6_addr.l2tp_family = AF_INET6;
			memcpy(&ip6_addr.l2tp_addr, cfg->peer_ip6,
			       sizeof(ip6_addr.l2tp_addr));
			ip6_addr.l2tp_conn_id = peer_tunnel_id;
			err = kernel_connect(sock,
					     (struct sockaddr *)&ip6_addr,
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
			err = kernel_bind(sock, (struct sockaddr *)&ip_addr,
					  sizeof(ip_addr));
			if (err < 0)
				goto out;

			ip_addr.l2tp_family = AF_INET;
			ip_addr.l2tp_addr = cfg->peer_ip;
			ip_addr.l2tp_conn_id = peer_tunnel_id;
			err = kernel_connect(sock, (struct sockaddr *)&ip_addr,
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
	if (err < 0 && sock) {
		kernel_sock_shutdown(sock, SHUT_RDWR);
		sock_release(sock);
		*sockp = NULL;
	}

	return err;
}

int l2tp_tunnel_create(int fd, int version, u32 tunnel_id, u32 peer_tunnel_id,
		       struct l2tp_tunnel_cfg *cfg, struct l2tp_tunnel **tunnelp)
{
	struct l2tp_tunnel *tunnel = NULL;
	int err;
	enum l2tp_encap_type encap = L2TP_ENCAPTYPE_UDP;

	if (cfg)
		encap = cfg->encap;

	tunnel = kzalloc(sizeof(*tunnel), GFP_KERNEL);
	if (!tunnel) {
		err = -ENOMEM;
		goto err;
	}

	tunnel->version = version;
	tunnel->tunnel_id = tunnel_id;
	tunnel->peer_tunnel_id = peer_tunnel_id;

	tunnel->magic = L2TP_TUNNEL_MAGIC;
	sprintf(&tunnel->name[0], "tunl %u", tunnel_id);
	spin_lock_init(&tunnel->list_lock);
	tunnel->acpt_newsess = true;
	INIT_LIST_HEAD(&tunnel->session_list);

	tunnel->encap = encap;

	refcount_set(&tunnel->ref_count, 1);
	tunnel->fd = fd;

	/* Init delete workqueue struct */
	INIT_WORK(&tunnel->del_work, l2tp_tunnel_del_work);

	err = 0;
err:
	if (tunnelp)
		*tunnelp = tunnel;

	return err;
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_create);

static int l2tp_validate_socket(const struct sock *sk, const struct net *net,
				enum l2tp_encap_type encap)
{
	if (!net_eq(sock_net(sk), net))
		return -EINVAL;

	if (sk->sk_type != SOCK_DGRAM)
		return -EPROTONOSUPPORT;

	if (sk->sk_family != PF_INET && sk->sk_family != PF_INET6)
		return -EPROTONOSUPPORT;

	if ((encap == L2TP_ENCAPTYPE_UDP && sk->sk_protocol != IPPROTO_UDP) ||
	    (encap == L2TP_ENCAPTYPE_IP && sk->sk_protocol != IPPROTO_L2TP))
		return -EPROTONOSUPPORT;

	if (sk->sk_user_data)
		return -EBUSY;

	return 0;
}

int l2tp_tunnel_register(struct l2tp_tunnel *tunnel, struct net *net,
			 struct l2tp_tunnel_cfg *cfg)
{
	struct l2tp_net *pn = l2tp_pernet(net);
	u32 tunnel_id = tunnel->tunnel_id;
	struct socket *sock;
	struct sock *sk;
	int ret;

	spin_lock_bh(&pn->l2tp_tunnel_idr_lock);
	ret = idr_alloc_u32(&pn->l2tp_tunnel_idr, NULL, &tunnel_id, tunnel_id,
			    GFP_ATOMIC);
	spin_unlock_bh(&pn->l2tp_tunnel_idr_lock);
	if (ret)
		return ret == -ENOSPC ? -EEXIST : ret;

	if (tunnel->fd < 0) {
		ret = l2tp_tunnel_sock_create(net, tunnel->tunnel_id,
					      tunnel->peer_tunnel_id, cfg,
					      &sock);
		if (ret < 0)
			goto err;
	} else {
		sock = sockfd_lookup(tunnel->fd, &ret);
		if (!sock)
			goto err;
	}

	sk = sock->sk;
	lock_sock(sk);
	write_lock_bh(&sk->sk_callback_lock);
	ret = l2tp_validate_socket(sk, net, tunnel->encap);
	if (ret < 0)
		goto err_inval_sock;
	rcu_assign_sk_user_data(sk, tunnel);
	write_unlock_bh(&sk->sk_callback_lock);

	if (tunnel->encap == L2TP_ENCAPTYPE_UDP) {
		struct udp_tunnel_sock_cfg udp_cfg = {
			.sk_user_data = tunnel,
			.encap_type = UDP_ENCAP_L2TPINUDP,
			.encap_rcv = l2tp_udp_encap_recv,
			.encap_err_rcv = l2tp_udp_encap_err_recv,
			.encap_destroy = l2tp_udp_encap_destroy,
		};

		setup_udp_tunnel_sock(net, sock, &udp_cfg);
	}

	tunnel->old_sk_destruct = sk->sk_destruct;
	sk->sk_destruct = &l2tp_tunnel_destruct;
	sk->sk_allocation = GFP_ATOMIC;
	release_sock(sk);

	sock_hold(sk);
	tunnel->sock = sk;
	tunnel->l2tp_net = net;

	spin_lock_bh(&pn->l2tp_tunnel_idr_lock);
	idr_replace(&pn->l2tp_tunnel_idr, tunnel, tunnel->tunnel_id);
	spin_unlock_bh(&pn->l2tp_tunnel_idr_lock);

	trace_register_tunnel(tunnel);

	if (tunnel->fd >= 0)
		sockfd_put(sock);

	return 0;

err_inval_sock:
	write_unlock_bh(&sk->sk_callback_lock);
	release_sock(sk);

	if (tunnel->fd < 0)
		sock_release(sock);
	else
		sockfd_put(sock);
err:
	l2tp_tunnel_remove(net, tunnel);
	return ret;
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_register);

/* This function is used by the netlink TUNNEL_DELETE command.
 */
void l2tp_tunnel_delete(struct l2tp_tunnel *tunnel)
{
	if (!test_and_set_bit(0, &tunnel->dead)) {
		trace_delete_tunnel(tunnel);
		l2tp_tunnel_inc_refcount(tunnel);
		queue_work(l2tp_wq, &tunnel->del_work);
	}
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_delete);

void l2tp_session_delete(struct l2tp_session *session)
{
	if (test_and_set_bit(0, &session->dead))
		return;

	trace_delete_session(session);
	l2tp_session_unhash(session);
	l2tp_session_queue_purge(session);
	if (session->session_close)
		(*session->session_close)(session);

	l2tp_session_dec_refcount(session);
}
EXPORT_SYMBOL_GPL(l2tp_session_delete);

/* We come here whenever a session's send_seq, cookie_len or
 * l2specific_type parameters are set.
 */
void l2tp_session_set_header_len(struct l2tp_session *session, int version)
{
	if (version == L2TP_HDR_VER_2) {
		session->hdr_len = 6;
		if (session->send_seq)
			session->hdr_len += 4;
	} else {
		session->hdr_len = 4 + session->cookie_len;
		session->hdr_len += l2tp_get_l2specific_len(session);
		if (session->tunnel->encap == L2TP_ENCAPTYPE_UDP)
			session->hdr_len += 4;
	}
}
EXPORT_SYMBOL_GPL(l2tp_session_set_header_len);

struct l2tp_session *l2tp_session_create(int priv_size, struct l2tp_tunnel *tunnel, u32 session_id,
					 u32 peer_session_id, struct l2tp_session_cfg *cfg)
{
	struct l2tp_session *session;

	session = kzalloc(sizeof(*session) + priv_size, GFP_KERNEL);
	if (session) {
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

		session->hlist_key = l2tp_v3_session_hashkey(tunnel->sock, session->session_id);
		INIT_HLIST_NODE(&session->hlist);
		INIT_LIST_HEAD(&session->clist);
		INIT_LIST_HEAD(&session->list);

		if (cfg) {
			session->pwtype = cfg->pw_type;
			session->send_seq = cfg->send_seq;
			session->recv_seq = cfg->recv_seq;
			session->lns_mode = cfg->lns_mode;
			session->reorder_timeout = cfg->reorder_timeout;
			session->l2specific_type = cfg->l2specific_type;
			session->cookie_len = cfg->cookie_len;
			memcpy(&session->cookie[0], &cfg->cookie[0], cfg->cookie_len);
			session->peer_cookie_len = cfg->peer_cookie_len;
			memcpy(&session->peer_cookie[0], &cfg->peer_cookie[0], cfg->peer_cookie_len);
		}

		l2tp_session_set_header_len(session, tunnel->version);

		refcount_set(&session->ref_count, 1);

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

	idr_init(&pn->l2tp_tunnel_idr);
	spin_lock_init(&pn->l2tp_tunnel_idr_lock);

	idr_init(&pn->l2tp_v2_session_idr);
	idr_init(&pn->l2tp_v3_session_idr);
	spin_lock_init(&pn->l2tp_session_idr_lock);

	return 0;
}

static __net_exit void l2tp_exit_net(struct net *net)
{
	struct l2tp_net *pn = l2tp_pernet(net);
	struct l2tp_tunnel *tunnel = NULL;
	unsigned long tunnel_id, tmp;

	rcu_read_lock_bh();
	idr_for_each_entry_ul(&pn->l2tp_tunnel_idr, tunnel, tmp, tunnel_id) {
		if (tunnel)
			l2tp_tunnel_delete(tunnel);
	}
	rcu_read_unlock_bh();

	if (l2tp_wq)
		flush_workqueue(l2tp_wq);
	rcu_barrier();

	idr_destroy(&pn->l2tp_v2_session_idr);
	idr_destroy(&pn->l2tp_v3_session_idr);
	idr_destroy(&pn->l2tp_tunnel_idr);
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
