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

#include <linux/module.h>
#include <linux/string.h>
#include <linux/list.h>
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
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/hash.h>
#include <linux/sort.h>
#include <linux/file.h>
#include <linux/nsproxy.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/xfrm.h>

#include <asm/byteorder.h>
#include <asm/atomic.h>

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

/* L2TPv3 default L2-specific sublayer */
#define L2TP_SLFLAG_S	   0x40000000
#define L2TP_SL_SEQ_MASK   0x00ffffff

#define L2TP_HDR_SIZE_SEQ		10
#define L2TP_HDR_SIZE_NOSEQ		6

/* Default trace flags */
#define L2TP_DEFAULT_DEBUG_FLAGS	0

#define PRINTK(_mask, _type, _lvl, _fmt, args...)			\
	do {								\
		if ((_mask) & (_type))					\
			printk(_lvl "L2TP: " _fmt, ##args);		\
	} while (0)

/* Private data stored for received packets in the skb.
 */
struct l2tp_skb_cb {
	u16			ns;
	u16			has_seq;
	u16			length;
	unsigned long		expires;
};

#define L2TP_SKB_CB(skb)	((struct l2tp_skb_cb *) &skb->cb[sizeof(struct inet_skb_parm)])

static atomic_t l2tp_tunnel_count;
static atomic_t l2tp_session_count;

/* per-net private data for this module */
static unsigned int l2tp_net_id;
struct l2tp_net {
	struct list_head l2tp_tunnel_list;
	rwlock_t l2tp_tunnel_list_lock;
};

static inline struct l2tp_net *l2tp_pernet(struct net *net)
{
	BUG_ON(!net);

	return net_generic(net, l2tp_net_id);
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
struct l2tp_session *l2tp_session_find(struct l2tp_tunnel *tunnel, u32 session_id)
{
	struct hlist_head *session_list =
		l2tp_session_id_hash(tunnel, session_id);
	struct l2tp_session *session;
	struct hlist_node *walk;

	read_lock_bh(&tunnel->hlist_lock);
	hlist_for_each_entry(session, walk, session_list, hlist) {
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
	struct hlist_node *walk;
	struct l2tp_session *session;
	int count = 0;

	read_lock_bh(&tunnel->hlist_lock);
	for (hash = 0; hash < L2TP_HASH_SIZE; hash++) {
		hlist_for_each_entry(session, walk, &tunnel->session_hlist[hash], hlist) {
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

/* Lookup a tunnel by id
 */
struct l2tp_tunnel *l2tp_tunnel_find(struct net *net, u32 tunnel_id)
{
	struct l2tp_tunnel *tunnel;
	struct l2tp_net *pn = l2tp_pernet(net);

	read_lock_bh(&pn->l2tp_tunnel_list_lock);
	list_for_each_entry(tunnel, &pn->l2tp_tunnel_list, list) {
		if (tunnel->tunnel_id == tunnel_id) {
			read_unlock_bh(&pn->l2tp_tunnel_list_lock);
			return tunnel;
		}
	}
	read_unlock_bh(&pn->l2tp_tunnel_list_lock);

	return NULL;
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_find);

struct l2tp_tunnel *l2tp_tunnel_find_nth(struct net *net, int nth)
{
	struct l2tp_net *pn = l2tp_pernet(net);
	struct l2tp_tunnel *tunnel;
	int count = 0;

	read_lock_bh(&pn->l2tp_tunnel_list_lock);
	list_for_each_entry(tunnel, &pn->l2tp_tunnel_list, list) {
		if (++count > nth) {
			read_unlock_bh(&pn->l2tp_tunnel_list_lock);
			return tunnel;
		}
	}

	read_unlock_bh(&pn->l2tp_tunnel_list_lock);

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
	u16 ns = L2TP_SKB_CB(skb)->ns;

	spin_lock_bh(&session->reorder_q.lock);
	skb_queue_walk_safe(&session->reorder_q, skbp, tmp) {
		if (L2TP_SKB_CB(skbp)->ns > ns) {
			__skb_queue_before(&session->reorder_q, skbp, skb);
			PRINTK(session->debug, L2TP_MSG_SEQ, KERN_DEBUG,
			       "%s: pkt %hu, inserted before %hu, reorder_q len=%d\n",
			       session->name, ns, L2TP_SKB_CB(skbp)->ns,
			       skb_queue_len(&session->reorder_q));
			session->stats.rx_oos_packets++;
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

	tunnel->stats.rx_packets++;
	tunnel->stats.rx_bytes += length;
	session->stats.rx_packets++;
	session->stats.rx_bytes += length;

	if (L2TP_SKB_CB(skb)->has_seq) {
		/* Bump our Nr */
		session->nr++;
		PRINTK(session->debug, L2TP_MSG_SEQ, KERN_DEBUG,
		       "%s: updated nr to %hu\n", session->name, session->nr);
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
	spin_lock_bh(&session->reorder_q.lock);
	skb_queue_walk_safe(&session->reorder_q, skb, tmp) {
		if (time_after(jiffies, L2TP_SKB_CB(skb)->expires)) {
			session->stats.rx_seq_discards++;
			session->stats.rx_errors++;
			PRINTK(session->debug, L2TP_MSG_SEQ, KERN_DEBUG,
			       "%s: oos pkt %hu len %d discarded (too old), "
			       "waiting for %hu, reorder_q_len=%d\n",
			       session->name, L2TP_SKB_CB(skb)->ns,
			       L2TP_SKB_CB(skb)->length, session->nr,
			       skb_queue_len(&session->reorder_q));
			__skb_unlink(skb, &session->reorder_q);
			kfree_skb(skb);
			if (session->deref)
				(*session->deref)(session);
			continue;
		}

		if (L2TP_SKB_CB(skb)->has_seq) {
			if (L2TP_SKB_CB(skb)->ns != session->nr) {
				PRINTK(session->debug, L2TP_MSG_SEQ, KERN_DEBUG,
				       "%s: holding oos pkt %hu len %d, "
				       "waiting for %hu, reorder_q_len=%d\n",
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
		spin_lock_bh(&session->reorder_q.lock);
	}

out:
	spin_unlock_bh(&session->reorder_q.lock);
}

static inline int l2tp_verify_udp_checksum(struct sock *sk,
					   struct sk_buff *skb)
{
	struct udphdr *uh = udp_hdr(skb);
	u16 ulen = ntohs(uh->len);
	struct inet_sock *inet;
	__wsum psum;

	if (sk->sk_no_check || skb_csum_unnecessary(skb) || !uh->check)
		return 0;

	inet = inet_sk(sk);
	psum = csum_tcpudp_nofold(inet->inet_saddr, inet->inet_daddr, ulen,
				  IPPROTO_UDP, 0);

	if ((skb->ip_summed == CHECKSUM_COMPLETE) &&
	    !csum_fold(csum_add(psum, skb->csum)))
		return 0;

	skb->csum = psum;

	return __skb_checksum_complete(skb);
}

/* Internal UDP receive frame. Do the real work of receiving an L2TP data frame
 * here. The skb is not on a list when we get here.
 * Returns 0 if the packet was a data packet and was successfully passed on.
 * Returns 1 if the packet was not a good data packet and could not be
 * forwarded.  All such packets are passed up to userspace to deal with.
 */
int l2tp_udp_recv_core(struct l2tp_tunnel *tunnel, struct sk_buff *skb,
		       int (*payload_hook)(struct sk_buff *skb))
{
	struct l2tp_session *session = NULL;
	unsigned char *ptr, *optr;
	u16 hdrflags;
	u32 tunnel_id, session_id;
	int length;
	int offset;
	u16 version;
	u16 ns, nr;

	if (tunnel->sock && l2tp_verify_udp_checksum(tunnel->sock, skb))
		goto discard_bad_csum;

	/* UDP always verifies the packet length. */
	__skb_pull(skb, sizeof(struct udphdr));

	/* Short packet? */
	if (!pskb_may_pull(skb, L2TP_HDR_SIZE_SEQ)) {
		PRINTK(tunnel->debug, L2TP_MSG_DATA, KERN_INFO,
		       "%s: recv short packet (len=%d)\n", tunnel->name, skb->len);
		goto error;
	}

	/* Point to L2TP header */
	optr = ptr = skb->data;

	/* Trace packet contents, if enabled */
	if (tunnel->debug & L2TP_MSG_DATA) {
		length = min(32u, skb->len);
		if (!pskb_may_pull(skb, length))
			goto error;

		printk(KERN_DEBUG "%s: recv: ", tunnel->name);

		offset = 0;
		do {
			printk(" %02X", ptr[offset]);
		} while (++offset < length);

		printk("\n");
	}

	/* Get L2TP header flags */
	hdrflags = ntohs(*(__be16 *)ptr);

	/* Check protocol version */
	version = hdrflags & L2TP_HDR_VER_MASK;
	if (version != tunnel->version) {
		PRINTK(tunnel->debug, L2TP_MSG_DATA, KERN_INFO,
		       "%s: recv protocol version mismatch: got %d expected %d\n",
		       tunnel->name, version, tunnel->version);
		goto error;
	}

	/* Get length of L2TP packet */
	length = skb->len;

	/* If type is control packet, it is handled by userspace. */
	if (hdrflags & L2TP_HDRFLAG_T) {
		PRINTK(tunnel->debug, L2TP_MSG_DATA, KERN_DEBUG,
		       "%s: recv control packet, len=%d\n", tunnel->name, length);
		goto error;
	}

	/* Skip flags */
	ptr += 2;

	/* If length is present, skip it */
	if (hdrflags & L2TP_HDRFLAG_L)
		ptr += 2;

	/* Extract tunnel and session ID */
	tunnel_id = ntohs(*(__be16 *) ptr);
	ptr += 2;
	session_id = ntohs(*(__be16 *) ptr);
	ptr += 2;

	/* Find the session context */
	session = l2tp_session_find(tunnel, session_id);
	if (!session) {
		/* Not found? Pass to userspace to deal with */
		PRINTK(tunnel->debug, L2TP_MSG_DATA, KERN_INFO,
		       "%s: no session found (%hu/%hu). Passing up.\n",
		       tunnel->name, tunnel_id, session_id);
		goto error;
	}

	/* The ref count is increased since we now hold a pointer to
	 * the session. Take care to decrement the refcnt when exiting
	 * this function from now on...
	 */
	l2tp_session_inc_refcount(session);
	if (session->ref)
		(*session->ref)(session);

	/* Handle the optional sequence numbers. Sequence numbers are
	 * in different places for L2TPv2 and L2TPv3.
	 *
	 * If we are the LAC, enable/disable sequence numbers under
	 * the control of the LNS.  If no sequence numbers present but
	 * we were expecting them, discard frame.
	 */
	ns = nr = 0;
	L2TP_SKB_CB(skb)->has_seq = 0;
	if (hdrflags & L2TP_HDRFLAG_S) {
		ns = (u16) ntohs(*(__be16 *) ptr);
		ptr += 2;
		nr = ntohs(*(__be16 *) ptr);
		ptr += 2;

		/* Store L2TP info in the skb */
		L2TP_SKB_CB(skb)->ns = ns;
		L2TP_SKB_CB(skb)->has_seq = 1;

		PRINTK(session->debug, L2TP_MSG_SEQ, KERN_DEBUG,
		       "%s: recv data ns=%hu, nr=%hu, session nr=%hu\n",
		       session->name, ns, nr, session->nr);
	}

	if (L2TP_SKB_CB(skb)->has_seq) {
		/* Received a packet with sequence numbers. If we're the LNS,
		 * check if we sre sending sequence numbers and if not,
		 * configure it so.
		 */
		if ((!session->lns_mode) && (!session->send_seq)) {
			PRINTK(session->debug, L2TP_MSG_SEQ, KERN_INFO,
			       "%s: requested to enable seq numbers by LNS\n",
			       session->name);
			session->send_seq = -1;
		}
	} else {
		/* No sequence numbers.
		 * If user has configured mandatory sequence numbers, discard.
		 */
		if (session->recv_seq) {
			PRINTK(session->debug, L2TP_MSG_SEQ, KERN_WARNING,
			       "%s: recv data has no seq numbers when required. "
			       "Discarding\n", session->name);
			session->stats.rx_seq_discards++;
			goto discard;
		}

		/* If we're the LAC and we're sending sequence numbers, the
		 * LNS has requested that we no longer send sequence numbers.
		 * If we're the LNS and we're sending sequence numbers, the
		 * LAC is broken. Discard the frame.
		 */
		if ((!session->lns_mode) && (session->send_seq)) {
			PRINTK(session->debug, L2TP_MSG_SEQ, KERN_INFO,
			       "%s: requested to disable seq numbers by LNS\n",
			       session->name);
			session->send_seq = 0;
		} else if (session->send_seq) {
			PRINTK(session->debug, L2TP_MSG_SEQ, KERN_WARNING,
			       "%s: recv data has no seq numbers when required. "
			       "Discarding\n", session->name);
			session->stats.rx_seq_discards++;
			goto discard;
		}
	}

	/* If offset bit set, skip it. */
	if (hdrflags & L2TP_HDRFLAG_O) {
		offset = ntohs(*(__be16 *)ptr);
		ptr += 2 + offset;
	}

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
				session->stats.rx_seq_discards++;
				PRINTK(session->debug, L2TP_MSG_SEQ, KERN_DEBUG,
				       "%s: oos pkt %hu len %d discarded, "
				       "waiting for %hu, reorder_q_len=%d\n",
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

	return 0;

discard:
	session->stats.rx_errors++;
	kfree_skb(skb);

	if (session->deref)
		(*session->deref)(session);

	l2tp_session_dec_refcount(session);

	return 0;

discard_bad_csum:
	LIMIT_NETDEBUG("%s: UDP: bad checksum\n", tunnel->name);
	UDP_INC_STATS_USER(tunnel->l2tp_net, UDP_MIB_INERRORS, 0);
	tunnel->stats.rx_errors++;
	kfree_skb(skb);

	return 0;

error:
	/* Put UDP header back */
	__skb_push(skb, sizeof(struct udphdr));

	return 1;
}
EXPORT_SYMBOL_GPL(l2tp_udp_recv_core);

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

	PRINTK(tunnel->debug, L2TP_MSG_DATA, KERN_DEBUG,
	       "%s: received %d bytes\n", tunnel->name, skb->len);

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
static void l2tp_build_l2tpv2_header(struct l2tp_tunnel *tunnel,
				     struct l2tp_session *session,
				     void *buf)
{
	__be16 *bufp = buf;
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
		PRINTK(session->debug, L2TP_MSG_SEQ, KERN_DEBUG,
		       "%s: updated ns to %hu\n", session->name, session->ns);
	}
}

void l2tp_build_l2tp_header(struct l2tp_session *session, void *buf)
{
	struct l2tp_tunnel *tunnel = session->tunnel;

	BUG_ON(tunnel->version != L2TP_HDR_VER_2);
	l2tp_build_l2tpv2_header(tunnel, session, buf);
}
EXPORT_SYMBOL_GPL(l2tp_build_l2tp_header);

int l2tp_xmit_core(struct l2tp_session *session, struct sk_buff *skb, size_t data_len)
{
	struct l2tp_tunnel *tunnel = session->tunnel;
	unsigned int len = skb->len;
	int error;

	/* Debug */
	if (session->send_seq)
		PRINTK(session->debug, L2TP_MSG_DATA, KERN_DEBUG,
		       "%s: send %Zd bytes, ns=%hu\n", session->name,
		       data_len, session->ns - 1);
	else
		PRINTK(session->debug, L2TP_MSG_DATA, KERN_DEBUG,
		       "%s: send %Zd bytes\n", session->name, data_len);

	if (session->debug & L2TP_MSG_DATA) {
		int i;
		unsigned char *datap = skb->data + sizeof(struct udphdr);

		printk(KERN_DEBUG "%s: xmit:", session->name);
		for (i = 0; i < (len - sizeof(struct udphdr)); i++) {
			printk(" %02X", *datap++);
			if (i == 31) {
				printk(" ...");
				break;
			}
		}
		printk("\n");
	}

	/* Queue the packet to IP for output */
	error = ip_queue_xmit(skb, 1);

	/* Update stats */
	if (error >= 0) {
		tunnel->stats.tx_packets++;
		tunnel->stats.tx_bytes += len;
		session->stats.tx_packets++;
		session->stats.tx_bytes += len;
	} else {
		tunnel->stats.tx_errors++;
		session->stats.tx_errors++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(l2tp_xmit_core);

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

/* If caller requires the skb to have a ppp header, the header must be
 * inserted in the skb data before calling this function.
 */
int l2tp_xmit_skb(struct l2tp_session *session, struct sk_buff *skb, int hdr_len)
{
	int data_len = skb->len;
	struct sock *sk = session->tunnel->sock;
	struct udphdr *uh;
	unsigned int udp_len;
	struct inet_sock *inet;
	__wsum csum;
	int old_headroom;
	int new_headroom;
	int headroom;

	/* Check that there's enough headroom in the skb to insert IP,
	 * UDP and L2TP headers. If not enough, expand it to
	 * make room. Adjust truesize.
	 */
	headroom = NET_SKB_PAD + sizeof(struct iphdr) +
		sizeof(struct udphdr) + hdr_len;
	old_headroom = skb_headroom(skb);
	if (skb_cow_head(skb, headroom))
		goto abort;

	new_headroom = skb_headroom(skb);
	skb_orphan(skb);
	skb->truesize += new_headroom - old_headroom;

	/* Setup L2TP header */
	l2tp_build_l2tp_header(session, __skb_push(skb, hdr_len));
	udp_len = sizeof(struct udphdr) + hdr_len + data_len;

	/* Setup UDP header */
	inet = inet_sk(sk);
	__skb_push(skb, sizeof(*uh));
	skb_reset_transport_header(skb);
	uh = udp_hdr(skb);
	uh->source = inet->inet_sport;
	uh->dest = inet->inet_dport;
	uh->len = htons(udp_len);

	uh->check = 0;

	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
	IPCB(skb)->flags &= ~(IPSKB_XFRM_TUNNEL_SIZE | IPSKB_XFRM_TRANSFORMED |
			      IPSKB_REROUTED);
	nf_reset(skb);

	/* Get routing info from the tunnel socket */
	skb_dst_drop(skb);
	skb_dst_set(skb, dst_clone(__sk_dst_get(sk)));
	l2tp_skb_set_owner_w(skb, sk);

	/* Calculate UDP checksum if configured to do so */
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

	l2tp_xmit_core(session, skb, data_len);

abort:
	return 0;
}
EXPORT_SYMBOL_GPL(l2tp_xmit_skb);

/*****************************************************************************
 * Tinnel and session create/destroy.
 *****************************************************************************/

/* Tunnel socket destruct hook.
 * The tunnel context is deleted only when all session sockets have been
 * closed.
 */
void l2tp_tunnel_destruct(struct sock *sk)
{
	struct l2tp_tunnel *tunnel;

	tunnel = sk->sk_user_data;
	if (tunnel == NULL)
		goto end;

	PRINTK(tunnel->debug, L2TP_MSG_CONTROL, KERN_INFO,
	       "%s: closing...\n", tunnel->name);

	/* Close all sessions */
	l2tp_tunnel_closeall(tunnel);

	/* No longer an encapsulation socket. See net/ipv4/udp.c */
	(udp_sk(sk))->encap_type = 0;
	(udp_sk(sk))->encap_rcv = NULL;

	/* Remove hooks into tunnel socket */
	tunnel->sock = NULL;
	sk->sk_destruct = tunnel->old_sk_destruct;
	sk->sk_user_data = NULL;

	/* Call the original destructor */
	if (sk->sk_destruct)
		(*sk->sk_destruct)(sk);

	/* We're finished with the socket */
	l2tp_tunnel_dec_refcount(tunnel);

end:
	return;
}
EXPORT_SYMBOL(l2tp_tunnel_destruct);

/* When the tunnel is closed, all the attached sessions need to go too.
 */
void l2tp_tunnel_closeall(struct l2tp_tunnel *tunnel)
{
	int hash;
	struct hlist_node *walk;
	struct hlist_node *tmp;
	struct l2tp_session *session;

	BUG_ON(tunnel == NULL);

	PRINTK(tunnel->debug, L2TP_MSG_CONTROL, KERN_INFO,
	       "%s: closing all sessions...\n", tunnel->name);

	write_lock_bh(&tunnel->hlist_lock);
	for (hash = 0; hash < L2TP_HASH_SIZE; hash++) {
again:
		hlist_for_each_safe(walk, tmp, &tunnel->session_hlist[hash]) {
			session = hlist_entry(walk, struct l2tp_session, hlist);

			PRINTK(session->debug, L2TP_MSG_CONTROL, KERN_INFO,
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
EXPORT_SYMBOL_GPL(l2tp_tunnel_closeall);

/* Really kill the tunnel.
 * Come here only when all sessions have been cleared from the tunnel.
 */
void l2tp_tunnel_free(struct l2tp_tunnel *tunnel)
{
	struct l2tp_net *pn = l2tp_pernet(tunnel->l2tp_net);

	BUG_ON(atomic_read(&tunnel->ref_count) != 0);
	BUG_ON(tunnel->sock != NULL);

	PRINTK(tunnel->debug, L2TP_MSG_CONTROL, KERN_INFO,
	       "%s: free...\n", tunnel->name);

	/* Remove from tunnel list */
	write_lock_bh(&pn->l2tp_tunnel_list_lock);
	list_del_init(&tunnel->list);
	write_unlock_bh(&pn->l2tp_tunnel_list_lock);

	atomic_dec(&l2tp_tunnel_count);
	kfree(tunnel);
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_free);

int l2tp_tunnel_create(struct net *net, int fd, int version, u32 tunnel_id, u32 peer_tunnel_id, struct l2tp_tunnel_cfg *cfg, struct l2tp_tunnel **tunnelp)
{
	struct l2tp_tunnel *tunnel = NULL;
	int err;
	struct socket *sock = NULL;
	struct sock *sk = NULL;
	struct l2tp_net *pn;

	/* Get the tunnel socket from the fd, which was opened by
	 * the userspace L2TP daemon.
	 */
	err = -EBADF;
	sock = sockfd_lookup(fd, &err);
	if (!sock) {
		printk(KERN_ERR "tunl %hu: sockfd_lookup(fd=%d) returned %d\n",
		       tunnel_id, fd, err);
		goto err;
	}

	sk = sock->sk;

	/* Quick sanity checks */
	err = -EPROTONOSUPPORT;
	if (sk->sk_protocol != IPPROTO_UDP) {
		printk(KERN_ERR "tunl %hu: fd %d wrong protocol, got %d, expected %d\n",
		       tunnel_id, fd, sk->sk_protocol, IPPROTO_UDP);
		goto err;
	}
	err = -EAFNOSUPPORT;
	if (sock->ops->family != AF_INET) {
		printk(KERN_ERR "tunl %hu: fd %d wrong family, got %d, expected %d\n",
		       tunnel_id, fd, sock->ops->family, AF_INET);
		goto err;
	}

	/* Check if this socket has already been prepped */
	tunnel = (struct l2tp_tunnel *)sk->sk_user_data;
	if (tunnel != NULL) {
		/* This socket has already been prepped */
		err = -EBUSY;
		goto err;
	}

	if (version != L2TP_HDR_VER_2)
		goto err;

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

	if (cfg)
		tunnel->debug = cfg->debug;

	/* Mark socket as an encapsulation socket. See net/ipv4/udp.c */
	udp_sk(sk)->encap_type = UDP_ENCAP_L2TPINUDP;
	udp_sk(sk)->encap_rcv = l2tp_udp_encap_recv;

	sk->sk_user_data = tunnel;

	/* Hook on the tunnel socket destructor so that we can cleanup
	 * if the tunnel socket goes away.
	 */
	tunnel->old_sk_destruct = sk->sk_destruct;
	sk->sk_destruct = &l2tp_tunnel_destruct;
	tunnel->sock = sk;
	sk->sk_allocation = GFP_ATOMIC;

	/* Add tunnel to our list */
	INIT_LIST_HEAD(&tunnel->list);
	write_lock_bh(&pn->l2tp_tunnel_list_lock);
	list_add(&tunnel->list, &pn->l2tp_tunnel_list);
	write_unlock_bh(&pn->l2tp_tunnel_list_lock);
	atomic_inc(&l2tp_tunnel_count);

	/* Bump the reference count. The tunnel context is deleted
	 * only when this drops to zero.
	 */
	l2tp_tunnel_inc_refcount(tunnel);

	err = 0;
err:
	if (tunnelp)
		*tunnelp = tunnel;

	if (sock)
		sockfd_put(sock);

	return err;
}
EXPORT_SYMBOL_GPL(l2tp_tunnel_create);

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

struct l2tp_session *l2tp_session_create(int priv_size, struct l2tp_tunnel *tunnel, u32 session_id, u32 peer_session_id, struct l2tp_session_cfg *cfg)
{
	struct l2tp_session *session;

	session = kzalloc(sizeof(struct l2tp_session) + priv_size, GFP_KERNEL);
	if (session != NULL) {
		session->magic = L2TP_SESSION_MAGIC;
		session->tunnel = tunnel;

		session->session_id = session_id;
		session->peer_session_id = peer_session_id;

		sprintf(&session->name[0], "sess %u/%u",
			tunnel->tunnel_id, session->session_id);

		skb_queue_head_init(&session->reorder_q);

		INIT_HLIST_NODE(&session->hlist);

		/* Inherit debug options from tunnel */
		session->debug = tunnel->debug;

		if (cfg) {
			session->debug = cfg->debug;
			session->hdr_len = cfg->hdr_len;
			session->mtu = cfg->mtu;
			session->mru = cfg->mru;
			session->send_seq = cfg->send_seq;
			session->recv_seq = cfg->recv_seq;
			session->lns_mode = cfg->lns_mode;
		}

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
	struct l2tp_net *pn;
	int err;

	pn = kzalloc(sizeof(*pn), GFP_KERNEL);
	if (!pn)
		return -ENOMEM;

	INIT_LIST_HEAD(&pn->l2tp_tunnel_list);
	rwlock_init(&pn->l2tp_tunnel_list_lock);

	err = net_assign_generic(net, l2tp_net_id, pn);
	if (err)
		goto out;

	return 0;

out:
	kfree(pn);
	return err;
}

static __net_exit void l2tp_exit_net(struct net *net)
{
	struct l2tp_net *pn;

	pn = net_generic(net, l2tp_net_id);
	/*
	 * if someone has cached our net then
	 * further net_generic call will return NULL
	 */
	net_assign_generic(net, l2tp_net_id, NULL);
	kfree(pn);
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

	printk(KERN_INFO "L2TP core driver, %s\n", L2TP_DRV_VERSION);

out:
	return rc;
}

static void __exit l2tp_exit(void)
{
	unregister_pernet_device(&l2tp_net_ops);
}

module_init(l2tp_init);
module_exit(l2tp_exit);

MODULE_AUTHOR("James Chapman <jchapman@katalix.com>");
MODULE_DESCRIPTION("L2TP core");
MODULE_LICENSE("GPL");
MODULE_VERSION(L2TP_DRV_VERSION);

