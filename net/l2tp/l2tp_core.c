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

static void l2tp_session_set_header_len(struct l2tp_session *session, int version);
static void l2tp_tunnel_free(struct l2tp_tunnel *tunnel);
static void l2tp_tunnel_closeall(struct l2tp_tunnel *tunnel);

static inline struct l2tp_net *l2tp_pernet(struct net *net)
{
	BUG_ON(!net);

	return net_generic(net, l2tp_net_id);
}

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
#define l2tp_tunnel_inc_refcount(_t)					\
do {									\
	pr_debug("l2tp_tunnel_inc_refcount: %s:%d %s: cnt=%d\n",	\
		 __func__, __LINE__, (_t)->name,			\
		 atomic_read(&_t->ref_count));				\
	l2tp_tunnel_inc_refcount_1(_t);					\
} while (0)
#define l2tp_tunnel_dec_refcount(_t)
do {									\
	pr_debug("l2tp_tunnel_dec_refcount: %s:%d %s: cnt=%d\n",	\
		 __func__, __LINE__, (_t)->name,			\
		 atomic_read(&_t->ref_count));				\
	l2tp_tunnel_dec_refcount_1(_t);					\
} while (0)
#else
#define l2tp_tunnel_inc_refcount(t) l2tp_tunnel_inc_refcount_1(t)
#define l2tp_tunnel_dec_refcount(t) l2tp_tunnel_dec_refcount_1(t)
#endif

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
struct sock *l2tp_tunnel_sock_lookup(struct l2tp_tunnel *tunnel)
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
	}

out:
	return sk;
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_sock_lookup);

/* Drop a reference to a tunnel socket obtained via. l2tp_tunnel_sock_put */
void l2tp_tunnel_sock_put(struct sock *sk)
{
	struct l2tp_tunnel *tunnel = l2tp_sock_to_tunnel(sk);
	if (tunnel) {
		if (tunnel->fd >= 0) {
			/* Socket is owned by userspace */
			sockfd_put(sk->sk_socket);
		}
		sock_put(sk);
	}
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_sock_put);

/* Lookup a session by id in the global session list
 */
static struct l2tp_session *l2tp_session_find_2(struct net *net, u32 session_id)
{
	struct l2tp_net *pn = l2tp_pernet(net);
	struct hlist_head *session_list =
		l2tp_session_id_hash_2(pn, session_id);
	struct l2tp_session *session;

	rcu_read_lock_bh();
	hlist_for_each_entry_rcu(session, session_list, global_hlist) {
		if (session->session_id == session_id) {
			rcu_read_unlock_bh();
			return session;
		}
	}
	rcu_read_unlock_bh();

	return NULL;
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

/* Lookup a session by id
 */
struct l2tp_session *l2tp_session_find(struct net *net, struct l2tp_tunnel *tunnel, u32 session_id)
{
	struct hlist_head *session_list;
	struct l2tp_session *session;

	/* In L2TPv3, session_ids are unique over all tunnels and we
	 * sometimes need to look them up before we know the
	 * tunnel.
	 */
	if (tunnel == NULL)
		return l2tp_session_find_2(net, session_id);

	session_list = l2tp_session_id_hash(tunnel, session_id);
	read_lock_bh(&tunnel->hlist_lock);
	hlist_for_each_entry(session, session_list, hlist) {
		if (session->session_id == session_id) {
			read_unlock_bh(&tunnel->hlist_lock);
			return session;
		}
	}
	read_unlock_bh(&tunnel->hlist_lock);

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_session_find);

struct l2tp_session *l2tp_session_find_nth(struct l2tp_tunnel *tunnel, int nth)
{
	int hash;
	struct l2tp_session *session;
	int count = 0;

	read_lock_bh(&tunnel->hlist_lock);
	for (hash = 0; hash < L2TP_HASH_SIZE; hash++) {
		hlist_for_each_entry(session, &tunnel->session_hlist[hash], hlist) {
			if (++count > nth) {
				read_unlock_bh(&tunnel->hlist_lock);
				return session;
			}
		}
	}

	read_unlock_bh(&tunnel->hlist_lock);

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_session_find_nth);

/* Lookup a session by interface name.
 * This is very inefficient but is only used by management interfaces.
 */
struct l2tp_session *l2tp_session_find_by_ifname(struct net *net, char *ifname)
{
	struct l2tp_net *pn = l2tp_pernet(net);
	int hash;
	struct l2tp_session *session;

	rcu_read_lock_bh();
	for (hash = 0; hash < L2TP_HASH_SIZE_2; hash++) {
		hlist_for_each_entry_rcu(session, &pn->l2tp_session_hlist[hash], global_hlist) {
			if (!strcmp(session->ifname, ifname)) {
				rcu_read_unlock_bh();
				return session;
			}
		}
	}

	rcu_read_unlock_bh();

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_session_find_by_ifname);

/* Lookup a tunnel by id
 */
struct l2tp_tunnel *l2tp_tunnel_find(struct net *net, u32 tunnel_id)
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

struct l2tp_tunnel *l2tp_tunnel_find_nth(struct net *net, int nth)
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
	struct l2tp_stats *sstats;

	spin_lock_bh(&session->reorder_q.lock);
	sstats = &session->stats;
	skb_queue_walk_safe(&session->reorder_q, skbp, tmp) {
		if (L2TP_SKB_CB(skbp)->ns > ns) {
			__skb_queue_before(&session->reorder_q, skbp, skb);
			l2tp_dbg(session, L2TP_MSG_SEQ,
				 "%s: pkt %hu, inserted before %hu, reorder_q len=%d\n",
				 session->name, ns, L2TP_SKB_CB(skbp)->ns,
				 skb_queue_len(&session->reorder_q));
			u64_stats_update_begin(&sstats->syncp);
			sstats->rx_oos_packets++;
			u64_stats_update_end(&sstats->syncp);
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
	struct l2tp_stats *tstats, *sstats;

	/* We're about to requeue the skb, so return resources
	 * to its current owner (a socket receive buffer).
	 */
	skb_orphan(skb);

	tstats = &tunnel->stats;
	u64_stats_update_begin(&tstats->syncp);
	sstats = &session->stats;
	u64_stats_update_begin(&sstats->syncp);
	tstats->rx_packets++;
	tstats->rx_bytes += length;
	sstats->rx_packets++;
	sstats->rx_bytes += length;
	u64_stats_update_end(&tstats->syncp);
	u64_stats_update_end(&sstats->syncp);

	if (L2TP_SKB_CB(skb)->has_seq) {
		/* Bump our Nr */
		session->nr++;
		if (tunnel->version == L2TP_HDR_VER_2)
			session->nr &= 0xffff;
		else
			session->nr &= 0xffffff;

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
	struct l2tp_stats *sstats;

	/* If the pkt at the head of the queue has the nr that we
	 * expect to send up next, dequeue it and any other
	 * in-sequence packets behind it.
	 */
start:
	spin_lock_bh(&session->reorder_q.lock);
	sstats = &session->stats;
	skb_queue_walk_safe(&session->reorder_q, skb, tmp) {
		if (time_after(jiffies, L2TP_SKB_CB(skb)->expires)) {
			u64_stats_update_begin(&sstats->syncp);
			sstats->rx_seq_discards++;
			sstats->rx_errors++;
			u64_stats_update_end(&sstats->syncp);
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

static inline int l2tp_verify_udp_checksum(struct sock *sk,
					   struct sk_buff *skb)
{
	struct udphdr *uh = udp_hdr(skb);
	u16 ulen = ntohs(uh->len);
	__wsum psum;

	if (sk->sk_no_check || skb_csum_unnecessary(skb))
		return 0;

#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == PF_INET6) {
		if (!uh->check) {
			LIMIT_NETDEBUG(KERN_INFO "L2TP: IPv6: checksum is 0\n");
			return 1;
		}
		if ((skb->ip_summed == CHECKSUM_COMPLETE) &&
		    !csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
				     &ipv6_hdr(skb)->daddr, ulen,
				     IPPROTO_UDP, skb->csum)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			return 0;
		}
		skb->csum = ~csum_unfold(csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
							 &ipv6_hdr(skb)->daddr,
							 skb->len, IPPROTO_UDP,
							 0));
	} else
#endif
	{
		struct inet_sock *inet;
		if (!uh->check)
			return 0;
		inet = inet_sk(sk);
		psum = csum_tcpudp_nofold(inet->inet_saddr, inet->inet_daddr,
					  ulen, IPPROTO_UDP, 0);

		if ((skb->ip_summed == CHECKSUM_COMPLETE) &&
		    !csum_fold(csum_add(psum, skb->csum)))
			return 0;
		skb->csum = psum;
	}

	return __skb_checksum_complete(skb);
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
 */
void l2tp_recv_common(struct l2tp_session *session, struct sk_buff *skb,
		      unsigned char *ptr, unsigned char *optr, u16 hdrflags,
		      int length, int (*payload_hook)(struct sk_buff *skb))
{
	struct l2tp_tunnel *tunnel = session->tunnel;
	int offset;
	u32 ns, nr;
	struct l2tp_stats *sstats = &session->stats;

	/* The ref count is increased since we now hold a pointer to
	 * the session. Take care to decrement the refcnt when exiting
	 * this function from now on...
	 */
	l2tp_session_inc_refcount(session);
	if (session->ref)
		(*session->ref)(session);

	/* Parse and check optional cookie */
	if (session->peer_cookie_len > 0) {
		if (memcmp(ptr, &session->peer_cookie[0], session->peer_cookie_len)) {
			l2tp_info(tunnel, L2TP_MSG_DATA,
				  "%s: cookie mismatch (%u/%u). Discarding.\n",
				  tunnel->name, tunnel->tunnel_id,
				  session->session_id);
			u64_stats_update_begin(&sstats->syncp);
			sstats->rx_cookie_discards++;
			u64_stats_update_end(&sstats->syncp);
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
			session->send_seq = -1;
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
			u64_stats_update_begin(&sstats->syncp);
			sstats->rx_seq_discards++;
			u64_stats_update_end(&sstats->syncp);
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
			u64_stats_update_begin(&sstats->syncp);
			sstats->rx_seq_discards++;
			u64_stats_update_end(&sstats->syncp);
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
		if (session->reorder_timeout != 0) {
			/* Packet reordering enabled. Add skb to session's
			 * reorder queue, in order of ns.
			 */
			l2tp_recv_queue_skb(session, skb);
		} else {
			/* Packet reordering disabled. Discard out-of-sequence
			 * packets
			 */
			if (L2TP_SKB_CB(skb)->ns != session->nr) {
				u64_stats_update_begin(&sstats->syncp);
				sstats->rx_seq_discards++;
				u64_stats_update_end(&sstats->syncp);
				l2tp_dbg(session, L2TP_MSG_SEQ,
					 "%s: oos pkt %u len %d discarded, waiting for %u, reorder_q_len=%d\n",
					 session->name, L2TP_SKB_CB(skb)->ns,
					 L2TP_SKB_CB(skb)->length, session->nr,
					 skb_queue_len(&session->reorder_q));
				goto discard;
			}
			skb_queue_tail(&session->reorder_q, skb);
		}
	} else {
		/* No sequence numbers. Add the skb to the tail of the
		 * reorder queue. This ensures that it will be
		 * delivered after all previous sequenced skbs.
		 */
		skb_queue_tail(&session->reorder_q, skb);
	}

	/* Try to dequeue as many skbs from reorder_q as we can. */
	l2tp_recv_dequeue(session);

	l2tp_session_dec_refcount(session);

	return;

discard:
	u64_stats_update_begin(&sstats->syncp);
	sstats->rx_errors++;
	u64_stats_update_end(&sstats->syncp);
	kfree_skb(skb);

	if (session->deref)
		(*session->deref)(session);

	l2tp_session_dec_refcount(session);
}
EXPORT_SYMBOL(l2tp_recv_common);

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
	struct l2tp_stats *tstats;

	if (tunnel->sock && l2tp_verify_udp_checksum(tunnel->sock, skb))
		goto discard_bad_csum;

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
	session = l2tp_session_find(tunnel->l2tp_net, tunnel, session_id);
	if (!session || !session->recv_skb) {
		/* Not found? Pass to userspace to deal with */
		l2tp_info(tunnel, L2TP_MSG_DATA,
			  "%s: no session found (%u/%u). Passing up.\n",
			  tunnel->name, tunnel_id, session_id);
		goto error;
	}

	l2tp_recv_common(session, skb, ptr, optr, hdrflags, length, payload_hook);

	return 0;

discard_bad_csum:
	LIMIT_NETDEBUG("%s: UDP: bad checksum\n", tunnel->name);
	UDP_INC_STATS_USER(tunnel->l2tp_net, UDP_MIB_INERRORS, 0);
	tstats = &tunnel->stats;
	u64_stats_update_begin(&tstats->syncp);
	tstats->rx_errors++;
	u64_stats_update_end(&tstats->syncp);
	kfree_skb(skb);

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
	struct l2tp_stats *tstats, *sstats;

	/* Debug */
	if (session->send_seq)
		l2tp_dbg(session, L2TP_MSG_DATA, "%s: send %Zd bytes, ns=%u\n",
			 session->name, data_len, session->ns - 1);
	else
		l2tp_dbg(session, L2TP_MSG_DATA, "%s: send %Zd bytes\n",
			 session->name, data_len);

	if (session->debug & L2TP_MSG_DATA) {
		int uhlen = (tunnel->encap == L2TP_ENCAPTYPE_UDP) ? sizeof(struct udphdr) : 0;
		unsigned char *datap = skb->data + uhlen;

		pr_debug("%s: xmit\n", session->name);
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
				     datap, min_t(size_t, 32, len - uhlen));
	}

	/* Queue the packet to IP for output */
	skb->local_df = 1;
#if IS_ENABLED(CONFIG_IPV6)
	if (skb->sk->sk_family == PF_INET6)
		error = inet6_csk_xmit(skb, NULL);
	else
#endif
		error = ip_queue_xmit(skb, fl);

	/* Update stats */
	tstats = &tunnel->stats;
	u64_stats_update_begin(&tstats->syncp);
	sstats = &session->stats;
	u64_stats_update_begin(&sstats->syncp);
	if (error >= 0) {
		tstats->tx_packets++;
		tstats->tx_bytes += len;
		sstats->tx_packets++;
		sstats->tx_bytes += len;
	} else {
		tstats->tx_errors++;
		sstats->tx_errors++;
	}
	u64_stats_update_end(&tstats->syncp);
	u64_stats_update_end(&sstats->syncp);

	return 0;
}

/* Automatically called when the skb is freed.
 */
static void l2tp_sock_wfree(struct sk_buff *skb)
{
	sock_put(skb->sk);
}

/* For data skbs that we transmit, we associate with the tunnel socket
 * but don't do accounting.
 */
static inline void l2tp_skb_set_owner_w(struct sk_buff *skb, struct sock *sk)
{
	sock_hold(sk);
	skb->sk = sk;
	skb->destructor = l2tp_sock_wfree;
}

#if IS_ENABLED(CONFIG_IPV6)
static void l2tp_xmit_ipv6_csum(struct sock *sk, struct sk_buff *skb,
				int udp_len)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct udphdr *uh = udp_hdr(skb);

	if (!skb_dst(skb) || !skb_dst(skb)->dev ||
	    !(skb_dst(skb)->dev->features & NETIF_F_IPV6_CSUM)) {
		__wsum csum = skb_checksum(skb, 0, udp_len, 0);
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		uh->check = csum_ipv6_magic(&np->saddr, &np->daddr, udp_len,
					    IPPROTO_UDP, csum);
		if (uh->check == 0)
			uh->check = CSUM_MANGLED_0;
	} else {
		skb->ip_summed = CHECKSUM_PARTIAL;
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct udphdr, check);
		uh->check = ~csum_ipv6_magic(&np->saddr, &np->daddr,
					     udp_len, IPPROTO_UDP, 0);
	}
}
#endif

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
	__wsum csum;
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

	skb_orphan(skb);
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

	/* Get routing info from the tunnel socket */
	skb_dst_drop(skb);
	skb_dst_set(skb, dst_clone(__sk_dst_check(sk, 0)));

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
		uh->check = 0;

		/* Calculate UDP checksum if configured to do so */
#if IS_ENABLED(CONFIG_IPV6)
		if (sk->sk_family == PF_INET6)
			l2tp_xmit_ipv6_csum(sk, skb, udp_len);
		else
#endif
		if (sk->sk_no_check == UDP_CSUM_NOXMIT)
			skb->ip_summed = CHECKSUM_NONE;
		else if ((skb_dst(skb) && skb_dst(skb)->dev) &&
			 (!(skb_dst(skb)->dev->features & NETIF_F_V4_CSUM))) {
			skb->ip_summed = CHECKSUM_COMPLETE;
			csum = skb_checksum(skb, 0, udp_len, 0);
			uh->check = csum_tcpudp_magic(inet->inet_saddr,
						      inet->inet_daddr,
						      udp_len, IPPROTO_UDP, csum);
			if (uh->check == 0)
				uh->check = CSUM_MANGLED_0;
		} else {
			skb->ip_summed = CHECKSUM_PARTIAL;
			skb->csum_start = skb_transport_header(skb) - skb->head;
			skb->csum_offset = offsetof(struct udphdr, check);
			uh->check = ~csum_tcpudp_magic(inet->inet_saddr,
						       inet->inet_daddr,
						       udp_len, IPPROTO_UDP, 0);
		}
		break;

	case L2TP_ENCAPTYPE_IP:
		break;
	}

	l2tp_skb_set_owner_w(skb, sk);

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
	struct l2tp_tunnel *tunnel;
	struct l2tp_net *pn;

	tunnel = sk->sk_user_data;
	if (tunnel == NULL)
		goto end;

	l2tp_info(tunnel, L2TP_MSG_CONTROL, "%s: closing...\n", tunnel->name);


	/* Disable udp encapsulation */
	switch (tunnel->encap) {
	case L2TP_ENCAPTYPE_UDP:
		/* No longer an encapsulation socket. See net/ipv4/udp.c */
		(udp_sk(sk))->encap_type = 0;
		(udp_sk(sk))->encap_rcv = NULL;
		break;
	case L2TP_ENCAPTYPE_IP:
		break;
	}

	/* Remove hooks into tunnel socket */
	sk->sk_destruct = tunnel->old_sk_destruct;
	sk->sk_user_data = NULL;
	tunnel->sock = NULL;

	/* Remove the tunnel struct from the tunnel list */
	pn = l2tp_pernet(tunnel->l2tp_net);
	spin_lock_bh(&pn->l2tp_tunnel_list_lock);
	list_del_rcu(&tunnel->list);
	spin_unlock_bh(&pn->l2tp_tunnel_list_lock);
	atomic_dec(&l2tp_tunnel_count);

	l2tp_tunnel_closeall(tunnel);
	l2tp_tunnel_dec_refcount(tunnel);

	/* Call the original destructor */
	if (sk->sk_destruct)
		(*sk->sk_destruct)(sk);
end:
	return;
}

/* When the tunnel is closed, all the attached sessions need to go too.
 */
static void l2tp_tunnel_closeall(struct l2tp_tunnel *tunnel)
{
	int hash;
	struct hlist_node *walk;
	struct hlist_node *tmp;
	struct l2tp_session *session;

	BUG_ON(tunnel == NULL);

	l2tp_info(tunnel, L2TP_MSG_CONTROL, "%s: closing all sessions...\n",
		  tunnel->name);

	write_lock_bh(&tunnel->hlist_lock);
	for (hash = 0; hash < L2TP_HASH_SIZE; hash++) {
again:
		hlist_for_each_safe(walk, tmp, &tunnel->session_hlist[hash]) {
			session = hlist_entry(walk, struct l2tp_session, hlist);

			l2tp_info(session, L2TP_MSG_CONTROL,
				  "%s: closing session\n", session->name);

			hlist_del_init(&session->hlist);

			/* Since we should hold the sock lock while
			 * doing any unbinding, we need to release the
			 * lock we're holding before taking that lock.
			 * Hold a reference to the sock so it doesn't
			 * disappear as we're jumping between locks.
			 */
			if (session->ref != NULL)
				(*session->ref)(session);

			write_unlock_bh(&tunnel->hlist_lock);

			if (tunnel->version != L2TP_HDR_VER_2) {
				struct l2tp_net *pn = l2tp_pernet(tunnel->l2tp_net);

				spin_lock_bh(&pn->l2tp_session_hlist_lock);
				hlist_del_init_rcu(&session->global_hlist);
				spin_unlock_bh(&pn->l2tp_session_hlist_lock);
				synchronize_rcu();
			}

			if (session->session_close != NULL)
				(*session->session_close)(session);

			if (session->deref != NULL)
				(*session->deref)(session);

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

/* Really kill the tunnel.
 * Come here only when all sessions have been cleared from the tunnel.
 */
static void l2tp_tunnel_free(struct l2tp_tunnel *tunnel)
{
	BUG_ON(atomic_read(&tunnel->ref_count) != 0);
	BUG_ON(tunnel->sock != NULL);
	l2tp_info(tunnel, L2TP_MSG_CONTROL, "%s: free...\n", tunnel->name);
	kfree_rcu(tunnel, rcu);
}

/* Workqueue tunnel deletion function */
static void l2tp_tunnel_del_work(struct work_struct *work)
{
	struct l2tp_tunnel *tunnel = NULL;
	struct socket *sock = NULL;
	struct sock *sk = NULL;

	tunnel = container_of(work, struct l2tp_tunnel, del_work);
	sk = l2tp_tunnel_sock_lookup(tunnel);
	if (!sk)
		return;

	sock = sk->sk_socket;
	BUG_ON(!sock);

	/* If the tunnel socket was created directly by the kernel, use the
	 * sk_* API to release the socket now.  Otherwise go through the
	 * inet_* layer to shut the socket down, and let userspace close it.
	 * In either case the tunnel resources are freed in the socket
	 * destructor when the tunnel socket goes away.
	 */
	if (sock->file == NULL) {
		kernel_sock_shutdown(sock, SHUT_RDWR);
		sk_release_kernel(sk);
	} else {
		inet_shutdown(sock, 2);
	}

	l2tp_tunnel_sock_put(sk);
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
	struct sockaddr_in udp_addr = {0};
	struct sockaddr_l2tpip ip_addr = {0};
#if IS_ENABLED(CONFIG_IPV6)
	struct sockaddr_in6 udp6_addr = {0};
	struct sockaddr_l2tpip6 ip6_addr = {0};
#endif

	switch (cfg->encap) {
	case L2TP_ENCAPTYPE_UDP:
#if IS_ENABLED(CONFIG_IPV6)
		if (cfg->local_ip6 && cfg->peer_ip6) {
			err = sock_create_kern(AF_INET6, SOCK_DGRAM, 0, &sock);
			if (err < 0)
				goto out;

			sk_change_net(sock->sk, net);

			udp6_addr.sin6_family = AF_INET6;
			memcpy(&udp6_addr.sin6_addr, cfg->local_ip6,
			       sizeof(udp6_addr.sin6_addr));
			udp6_addr.sin6_port = htons(cfg->local_udp_port);
			err = kernel_bind(sock, (struct sockaddr *) &udp6_addr,
					  sizeof(udp6_addr));
			if (err < 0)
				goto out;

			udp6_addr.sin6_family = AF_INET6;
			memcpy(&udp6_addr.sin6_addr, cfg->peer_ip6,
			       sizeof(udp6_addr.sin6_addr));
			udp6_addr.sin6_port = htons(cfg->peer_udp_port);
			err = kernel_connect(sock,
					     (struct sockaddr *) &udp6_addr,
					     sizeof(udp6_addr), 0);
			if (err < 0)
				goto out;
		} else
#endif
		{
			err = sock_create_kern(AF_INET, SOCK_DGRAM, 0, &sock);
			if (err < 0)
				goto out;

			sk_change_net(sock->sk, net);

			udp_addr.sin_family = AF_INET;
			udp_addr.sin_addr = cfg->local_ip;
			udp_addr.sin_port = htons(cfg->local_udp_port);
			err = kernel_bind(sock, (struct sockaddr *) &udp_addr,
					  sizeof(udp_addr));
			if (err < 0)
				goto out;

			udp_addr.sin_family = AF_INET;
			udp_addr.sin_addr = cfg->peer_ip;
			udp_addr.sin_port = htons(cfg->peer_udp_port);
			err = kernel_connect(sock,
					     (struct sockaddr *) &udp_addr,
					     sizeof(udp_addr), 0);
			if (err < 0)
				goto out;
		}

		if (!cfg->use_udp_checksums)
			sock->sk->sk_no_check = UDP_CSUM_NOXMIT;

		break;

	case L2TP_ENCAPTYPE_IP:
#if IS_ENABLED(CONFIG_IPV6)
		if (cfg->local_ip6 && cfg->peer_ip6) {
			err = sock_create_kern(AF_INET6, SOCK_DGRAM,
					  IPPROTO_L2TP, &sock);
			if (err < 0)
				goto out;

			sk_change_net(sock->sk, net);

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
			err = sock_create_kern(AF_INET, SOCK_DGRAM,
					  IPPROTO_L2TP, &sock);
			if (err < 0)
				goto out;

			sk_change_net(sock->sk, net);

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
		sk_release_kernel(sock->sk);
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
	switch (encap) {
	case L2TP_ENCAPTYPE_UDP:
		err = -EPROTONOSUPPORT;
		if (sk->sk_protocol != IPPROTO_UDP) {
			pr_err("tunl %hu: fd %d wrong protocol, got %d, expected %d\n",
			       tunnel_id, fd, sk->sk_protocol, IPPROTO_UDP);
			goto err;
		}
		break;
	case L2TP_ENCAPTYPE_IP:
		err = -EPROTONOSUPPORT;
		if (sk->sk_protocol != IPPROTO_L2TP) {
			pr_err("tunl %hu: fd %d wrong protocol, got %d, expected %d\n",
			       tunnel_id, fd, sk->sk_protocol, IPPROTO_L2TP);
			goto err;
		}
		break;
	}

	/* Check if this socket has already been prepped */
	tunnel = (struct l2tp_tunnel *)sk->sk_user_data;
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

	/* The net we belong to */
	tunnel->l2tp_net = net;
	pn = l2tp_pernet(net);

	if (cfg != NULL)
		tunnel->debug = cfg->debug;

	/* Mark socket as an encapsulation socket. See net/ipv4/udp.c */
	tunnel->encap = encap;
	if (encap == L2TP_ENCAPTYPE_UDP) {
		/* Mark socket as an encapsulation socket. See net/ipv4/udp.c */
		udp_sk(sk)->encap_type = UDP_ENCAP_L2TPINUDP;
		udp_sk(sk)->encap_rcv = l2tp_udp_encap_recv;
#if IS_ENABLED(CONFIG_IPV6)
		if (sk->sk_family == PF_INET6)
			udpv6_encap_enable();
		else
#endif
		udp_encap_enable();
	}

	sk->sk_user_data = tunnel;

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
	l2tp_tunnel_inc_refcount(tunnel);
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
int l2tp_tunnel_delete(struct l2tp_tunnel *tunnel)
{
	return (false == queue_work(l2tp_wq, &tunnel->del_work));
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_delete);

/* Really kill the session.
 */
void l2tp_session_free(struct l2tp_session *session)
{
	struct l2tp_tunnel *tunnel;

	BUG_ON(atomic_read(&session->ref_count) != 0);

	tunnel = session->tunnel;
	if (tunnel != NULL) {
		BUG_ON(tunnel->magic != L2TP_TUNNEL_MAGIC);

		/* Delete the session from the hash */
		write_lock_bh(&tunnel->hlist_lock);
		hlist_del_init(&session->hlist);
		write_unlock_bh(&tunnel->hlist_lock);

		/* Unlink from the global hash if not L2TPv2 */
		if (tunnel->version != L2TP_HDR_VER_2) {
			struct l2tp_net *pn = l2tp_pernet(tunnel->l2tp_net);

			spin_lock_bh(&pn->l2tp_session_hlist_lock);
			hlist_del_init_rcu(&session->global_hlist);
			spin_unlock_bh(&pn->l2tp_session_hlist_lock);
			synchronize_rcu();
		}

		if (session->session_id != 0)
			atomic_dec(&l2tp_session_count);

		sock_put(tunnel->sock);

		/* This will delete the tunnel context if this
		 * is the last session on the tunnel.
		 */
		session->tunnel = NULL;
		l2tp_tunnel_dec_refcount(tunnel);
	}

	kfree(session);

	return;
}
EXPORT_SYMBOL_GPL(l2tp_session_free);

/* This function is used by the netlink SESSION_DELETE command and by
   pseudowire modules.
 */
int l2tp_session_delete(struct l2tp_session *session)
{
	if (session->session_close != NULL)
		(*session->session_close)(session);

	l2tp_session_dec_refcount(session);

	return 0;
}
EXPORT_SYMBOL_GPL(l2tp_session_delete);


/* We come here whenever a session's send_seq, cookie_len or
 * l2specific_len parameters are set.
 */
static void l2tp_session_set_header_len(struct l2tp_session *session, int version)
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

struct l2tp_session *l2tp_session_create(int priv_size, struct l2tp_tunnel *tunnel, u32 session_id, u32 peer_session_id, struct l2tp_session_cfg *cfg)
{
	struct l2tp_session *session;

	session = kzalloc(sizeof(struct l2tp_session) + priv_size, GFP_KERNEL);
	if (session != NULL) {
		session->magic = L2TP_SESSION_MAGIC;
		session->tunnel = tunnel;

		session->session_id = session_id;
		session->peer_session_id = peer_session_id;
		session->nr = 0;

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

		/* Bump the reference count. The session context is deleted
		 * only when this drops to zero.
		 */
		l2tp_session_inc_refcount(session);
		l2tp_tunnel_inc_refcount(tunnel);

		/* Ensure tunnel socket isn't deleted */
		sock_hold(tunnel->sock);

		/* Add session to the tunnel's hash list */
		write_lock_bh(&tunnel->hlist_lock);
		hlist_add_head(&session->hlist,
			       l2tp_session_id_hash(tunnel, session_id));
		write_unlock_bh(&tunnel->hlist_lock);

		/* And to the global session list if L2TPv3 */
		if (tunnel->version != L2TP_HDR_VER_2) {
			struct l2tp_net *pn = l2tp_pernet(tunnel->l2tp_net);

			spin_lock_bh(&pn->l2tp_session_hlist_lock);
			hlist_add_head_rcu(&session->global_hlist,
					   l2tp_session_id_hash_2(pn, session_id));
			spin_unlock_bh(&pn->l2tp_session_hlist_lock);
		}

		/* Ignore management session in session count value */
		if (session->session_id != 0)
			atomic_inc(&l2tp_session_count);
	}

	return session;
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
		(void)l2tp_tunnel_delete(tunnel);
	}
	rcu_read_unlock_bh();
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

	l2tp_wq = alloc_workqueue("l2tp", WQ_NON_REENTRANT | WQ_UNBOUND, 0);
	if (!l2tp_wq) {
		pr_err("alloc_workqueue failed\n");
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

