/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Routing Functions (Endnode and Router)
 *
 * Authors:     Steve Whitehouse <SteveW@ACM.org>
 *              Eduardo Marcelo Serrat <emserrat@geocities.com>
 *
 * Changes:
 *              Steve Whitehouse : Fixes to allow "intra-ethernet" and
 *                                 "return-to-sender" bits on outgoing
 *                                 packets.
 *		Steve Whitehouse : Timeouts for cached routes.
 *              Steve Whitehouse : Use dst cache for input routes too.
 *              Steve Whitehouse : Fixed error values in dn_send_skb.
 *              Steve Whitehouse : Rework routing functions to better fit
 *                                 DECnet routing design
 *              Alexey Kuznetsov : New SMP locking
 *              Steve Whitehouse : More SMP locking changes & dn_cache_dump()
 *              Steve Whitehouse : Prerouting NF hook, now really is prerouting.
 *				   Fixed possible skb leak in rtnetlink funcs.
 *              Steve Whitehouse : Dave Miller's dynamic hash table sizing and
 *                                 Alexey Kuznetsov's finer grained locking
 *                                 from ipv4/route.c.
 *              Steve Whitehouse : Routing is now starting to look like a
 *                                 sensible set of code now, mainly due to
 *                                 my copying the IPv4 routing code. The
 *                                 hooks here are modified and will continue
 *                                 to evolve for a while.
 *              Steve Whitehouse : Real SMP at last :-) Also new netfilter
 *                                 stuff. Look out raw sockets your days
 *                                 are numbered!
 *              Steve Whitehouse : Added return-to-sender functions. Added
 *                                 backlog congestion level return codes.
 *		Steve Whitehouse : Fixed bug where routes were set up with
 *                                 no ref count on net devices.
 *              Steve Whitehouse : RCU for the route cache
 *              Steve Whitehouse : Preparations for the flow cache
 *              Steve Whitehouse : Prepare for nonlinear skbs
 */

/******************************************************************************
    (c) 1995-1998 E.M. Serrat		emserrat@geocities.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*******************************************************************************/

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/inet.h>
#include <linux/route.h>
#include <linux/in_route.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>
#include <linux/string.h>
#include <linux/netfilter_decnet.h>
#include <linux/rcupdate.h>
#include <linux/times.h>
#include <linux/export.h>
#include <asm/errno.h>
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/fib_rules.h>
#include <net/dn.h>
#include <net/dn_dev.h>
#include <net/dn_nsp.h>
#include <net/dn_route.h>
#include <net/dn_neigh.h>
#include <net/dn_fib.h>

struct dn_rt_hash_bucket
{
	struct dn_route __rcu *chain;
	spinlock_t lock;
};

extern struct neigh_table dn_neigh_table;


static unsigned char dn_hiord_addr[6] = {0xAA,0x00,0x04,0x00,0x00,0x00};

static const int dn_rt_min_delay = 2 * HZ;
static const int dn_rt_max_delay = 10 * HZ;
static const int dn_rt_mtu_expires = 10 * 60 * HZ;

static unsigned long dn_rt_deadline;

static int dn_dst_gc(struct dst_ops *ops);
static struct dst_entry *dn_dst_check(struct dst_entry *, __u32);
static unsigned int dn_dst_default_advmss(const struct dst_entry *dst);
static unsigned int dn_dst_mtu(const struct dst_entry *dst);
static void dn_dst_destroy(struct dst_entry *);
static void dn_dst_ifdown(struct dst_entry *, struct net_device *dev, int how);
static struct dst_entry *dn_dst_negative_advice(struct dst_entry *);
static void dn_dst_link_failure(struct sk_buff *);
static void dn_dst_update_pmtu(struct dst_entry *dst, u32 mtu);
static struct neighbour *dn_dst_neigh_lookup(const struct dst_entry *dst,
					     struct sk_buff *skb,
					     const void *daddr);
static int dn_route_input(struct sk_buff *);
static void dn_run_flush(unsigned long dummy);

static struct dn_rt_hash_bucket *dn_rt_hash_table;
static unsigned int dn_rt_hash_mask;

static struct timer_list dn_route_timer;
static DEFINE_TIMER(dn_rt_flush_timer, dn_run_flush, 0, 0);
int decnet_dst_gc_interval = 2;

static struct dst_ops dn_dst_ops = {
	.family =		PF_DECnet,
	.protocol =		cpu_to_be16(ETH_P_DNA_RT),
	.gc_thresh =		128,
	.gc =			dn_dst_gc,
	.check =		dn_dst_check,
	.default_advmss =	dn_dst_default_advmss,
	.mtu =			dn_dst_mtu,
	.cow_metrics =		dst_cow_metrics_generic,
	.destroy =		dn_dst_destroy,
	.ifdown =		dn_dst_ifdown,
	.negative_advice =	dn_dst_negative_advice,
	.link_failure =		dn_dst_link_failure,
	.update_pmtu =		dn_dst_update_pmtu,
	.neigh_lookup =		dn_dst_neigh_lookup,
};

static void dn_dst_destroy(struct dst_entry *dst)
{
	struct dn_route *rt = (struct dn_route *) dst;

	if (rt->n)
		neigh_release(rt->n);
	dst_destroy_metrics_generic(dst);
}

static void dn_dst_ifdown(struct dst_entry *dst, struct net_device *dev, int how)
{
	if (how) {
		struct dn_route *rt = (struct dn_route *) dst;
		struct neighbour *n = rt->n;

		if (n && n->dev == dev) {
			n->dev = dev_net(dev)->loopback_dev;
			dev_hold(n->dev);
			dev_put(dev);
		}
	}
}

static __inline__ unsigned int dn_hash(__le16 src, __le16 dst)
{
	__u16 tmp = (__u16 __force)(src ^ dst);
	tmp ^= (tmp >> 3);
	tmp ^= (tmp >> 5);
	tmp ^= (tmp >> 10);
	return dn_rt_hash_mask & (unsigned int)tmp;
}

static inline void dnrt_free(struct dn_route *rt)
{
	call_rcu_bh(&rt->dst.rcu_head, dst_rcu_free);
}

static inline void dnrt_drop(struct dn_route *rt)
{
	dst_release(&rt->dst);
	call_rcu_bh(&rt->dst.rcu_head, dst_rcu_free);
}

static void dn_dst_check_expire(unsigned long dummy)
{
	int i;
	struct dn_route *rt;
	struct dn_route __rcu **rtp;
	unsigned long now = jiffies;
	unsigned long expire = 120 * HZ;

	for (i = 0; i <= dn_rt_hash_mask; i++) {
		rtp = &dn_rt_hash_table[i].chain;

		spin_lock(&dn_rt_hash_table[i].lock);
		while ((rt = rcu_dereference_protected(*rtp,
						lockdep_is_held(&dn_rt_hash_table[i].lock))) != NULL) {
			if (atomic_read(&rt->dst.__refcnt) ||
					(now - rt->dst.lastuse) < expire) {
				rtp = &rt->dst.dn_next;
				continue;
			}
			*rtp = rt->dst.dn_next;
			rt->dst.dn_next = NULL;
			dnrt_free(rt);
		}
		spin_unlock(&dn_rt_hash_table[i].lock);

		if ((jiffies - now) > 0)
			break;
	}

	mod_timer(&dn_route_timer, now + decnet_dst_gc_interval * HZ);
}

static int dn_dst_gc(struct dst_ops *ops)
{
	struct dn_route *rt;
	struct dn_route __rcu **rtp;
	int i;
	unsigned long now = jiffies;
	unsigned long expire = 10 * HZ;

	for (i = 0; i <= dn_rt_hash_mask; i++) {

		spin_lock_bh(&dn_rt_hash_table[i].lock);
		rtp = &dn_rt_hash_table[i].chain;

		while ((rt = rcu_dereference_protected(*rtp,
						lockdep_is_held(&dn_rt_hash_table[i].lock))) != NULL) {
			if (atomic_read(&rt->dst.__refcnt) ||
					(now - rt->dst.lastuse) < expire) {
				rtp = &rt->dst.dn_next;
				continue;
			}
			*rtp = rt->dst.dn_next;
			rt->dst.dn_next = NULL;
			dnrt_drop(rt);
			break;
		}
		spin_unlock_bh(&dn_rt_hash_table[i].lock);
	}

	return 0;
}

/*
 * The decnet standards don't impose a particular minimum mtu, what they
 * do insist on is that the routing layer accepts a datagram of at least
 * 230 bytes long. Here we have to subtract the routing header length from
 * 230 to get the minimum acceptable mtu. If there is no neighbour, then we
 * assume the worst and use a long header size.
 *
 * We update both the mtu and the advertised mss (i.e. the segment size we
 * advertise to the other end).
 */
static void dn_dst_update_pmtu(struct dst_entry *dst, u32 mtu)
{
	struct dn_route *rt = (struct dn_route *) dst;
	struct neighbour *n = rt->n;
	u32 min_mtu = 230;
	struct dn_dev *dn;

	dn = n ? rcu_dereference_raw(n->dev->dn_ptr) : NULL;

	if (dn && dn->use_long == 0)
		min_mtu -= 6;
	else
		min_mtu -= 21;

	if (dst_metric(dst, RTAX_MTU) > mtu && mtu >= min_mtu) {
		if (!(dst_metric_locked(dst, RTAX_MTU))) {
			dst_metric_set(dst, RTAX_MTU, mtu);
			dst_set_expires(dst, dn_rt_mtu_expires);
		}
		if (!(dst_metric_locked(dst, RTAX_ADVMSS))) {
			u32 mss = mtu - DN_MAX_NSP_DATA_HEADER;
			u32 existing_mss = dst_metric_raw(dst, RTAX_ADVMSS);
			if (!existing_mss || existing_mss > mss)
				dst_metric_set(dst, RTAX_ADVMSS, mss);
		}
	}
}

/*
 * When a route has been marked obsolete. (e.g. routing cache flush)
 */
static struct dst_entry *dn_dst_check(struct dst_entry *dst, __u32 cookie)
{
	return NULL;
}

static struct dst_entry *dn_dst_negative_advice(struct dst_entry *dst)
{
	dst_release(dst);
	return NULL;
}

static void dn_dst_link_failure(struct sk_buff *skb)
{
}

static inline int compare_keys(struct flowidn *fl1, struct flowidn *fl2)
{
	return ((fl1->daddr ^ fl2->daddr) |
		(fl1->saddr ^ fl2->saddr) |
		(fl1->flowidn_mark ^ fl2->flowidn_mark) |
		(fl1->flowidn_scope ^ fl2->flowidn_scope) |
		(fl1->flowidn_oif ^ fl2->flowidn_oif) |
		(fl1->flowidn_iif ^ fl2->flowidn_iif)) == 0;
}

static int dn_insert_route(struct dn_route *rt, unsigned int hash, struct dn_route **rp)
{
	struct dn_route *rth;
	struct dn_route __rcu **rthp;
	unsigned long now = jiffies;

	rthp = &dn_rt_hash_table[hash].chain;

	spin_lock_bh(&dn_rt_hash_table[hash].lock);
	while ((rth = rcu_dereference_protected(*rthp,
						lockdep_is_held(&dn_rt_hash_table[hash].lock))) != NULL) {
		if (compare_keys(&rth->fld, &rt->fld)) {
			/* Put it first */
			*rthp = rth->dst.dn_next;
			rcu_assign_pointer(rth->dst.dn_next,
					   dn_rt_hash_table[hash].chain);
			rcu_assign_pointer(dn_rt_hash_table[hash].chain, rth);

			dst_use(&rth->dst, now);
			spin_unlock_bh(&dn_rt_hash_table[hash].lock);

			dnrt_drop(rt);
			*rp = rth;
			return 0;
		}
		rthp = &rth->dst.dn_next;
	}

	rcu_assign_pointer(rt->dst.dn_next, dn_rt_hash_table[hash].chain);
	rcu_assign_pointer(dn_rt_hash_table[hash].chain, rt);

	dst_use(&rt->dst, now);
	spin_unlock_bh(&dn_rt_hash_table[hash].lock);
	*rp = rt;
	return 0;
}

static void dn_run_flush(unsigned long dummy)
{
	int i;
	struct dn_route *rt, *next;

	for (i = 0; i < dn_rt_hash_mask; i++) {
		spin_lock_bh(&dn_rt_hash_table[i].lock);

		if ((rt = xchg((struct dn_route **)&dn_rt_hash_table[i].chain, NULL)) == NULL)
			goto nothing_to_declare;

		for(; rt; rt = next) {
			next = rcu_dereference_raw(rt->dst.dn_next);
			RCU_INIT_POINTER(rt->dst.dn_next, NULL);
			dst_free((struct dst_entry *)rt);
		}

nothing_to_declare:
		spin_unlock_bh(&dn_rt_hash_table[i].lock);
	}
}

static DEFINE_SPINLOCK(dn_rt_flush_lock);

void dn_rt_cache_flush(int delay)
{
	unsigned long now = jiffies;
	int user_mode = !in_interrupt();

	if (delay < 0)
		delay = dn_rt_min_delay;

	spin_lock_bh(&dn_rt_flush_lock);

	if (del_timer(&dn_rt_flush_timer) && delay > 0 && dn_rt_deadline) {
		long tmo = (long)(dn_rt_deadline - now);

		if (user_mode && tmo < dn_rt_max_delay - dn_rt_min_delay)
			tmo = 0;

		if (delay > tmo)
			delay = tmo;
	}

	if (delay <= 0) {
		spin_unlock_bh(&dn_rt_flush_lock);
		dn_run_flush(0);
		return;
	}

	if (dn_rt_deadline == 0)
		dn_rt_deadline = now + dn_rt_max_delay;

	dn_rt_flush_timer.expires = now + delay;
	add_timer(&dn_rt_flush_timer);
	spin_unlock_bh(&dn_rt_flush_lock);
}

/**
 * dn_return_short - Return a short packet to its sender
 * @skb: The packet to return
 *
 */
static int dn_return_short(struct sk_buff *skb)
{
	struct dn_skb_cb *cb;
	unsigned char *ptr;
	__le16 *src;
	__le16 *dst;

	/* Add back headers */
	skb_push(skb, skb->data - skb_network_header(skb));

	if ((skb = skb_unshare(skb, GFP_ATOMIC)) == NULL)
		return NET_RX_DROP;

	cb = DN_SKB_CB(skb);
	/* Skip packet length and point to flags */
	ptr = skb->data + 2;
	*ptr++ = (cb->rt_flags & ~DN_RT_F_RQR) | DN_RT_F_RTS;

	dst = (__le16 *)ptr;
	ptr += 2;
	src = (__le16 *)ptr;
	ptr += 2;
	*ptr = 0; /* Zero hop count */

	swap(*src, *dst);

	skb->pkt_type = PACKET_OUTGOING;
	dn_rt_finish_output(skb, NULL, NULL);
	return NET_RX_SUCCESS;
}

/**
 * dn_return_long - Return a long packet to its sender
 * @skb: The long format packet to return
 *
 */
static int dn_return_long(struct sk_buff *skb)
{
	struct dn_skb_cb *cb;
	unsigned char *ptr;
	unsigned char *src_addr, *dst_addr;
	unsigned char tmp[ETH_ALEN];

	/* Add back all headers */
	skb_push(skb, skb->data - skb_network_header(skb));

	if ((skb = skb_unshare(skb, GFP_ATOMIC)) == NULL)
		return NET_RX_DROP;

	cb = DN_SKB_CB(skb);
	/* Ignore packet length and point to flags */
	ptr = skb->data + 2;

	/* Skip padding */
	if (*ptr & DN_RT_F_PF) {
		char padlen = (*ptr & ~DN_RT_F_PF);
		ptr += padlen;
	}

	*ptr++ = (cb->rt_flags & ~DN_RT_F_RQR) | DN_RT_F_RTS;
	ptr += 2;
	dst_addr = ptr;
	ptr += 8;
	src_addr = ptr;
	ptr += 6;
	*ptr = 0; /* Zero hop count */

	/* Swap source and destination */
	memcpy(tmp, src_addr, ETH_ALEN);
	memcpy(src_addr, dst_addr, ETH_ALEN);
	memcpy(dst_addr, tmp, ETH_ALEN);

	skb->pkt_type = PACKET_OUTGOING;
	dn_rt_finish_output(skb, dst_addr, src_addr);
	return NET_RX_SUCCESS;
}

/**
 * dn_route_rx_packet - Try and find a route for an incoming packet
 * @skb: The packet to find a route for
 *
 * Returns: result of input function if route is found, error code otherwise
 */
static int dn_route_rx_packet(struct sk_buff *skb)
{
	struct dn_skb_cb *cb;
	int err;

	if ((err = dn_route_input(skb)) == 0)
		return dst_input(skb);

	cb = DN_SKB_CB(skb);
	if (decnet_debug_level & 4) {
		char *devname = skb->dev ? skb->dev->name : "???";

		printk(KERN_DEBUG
			"DECnet: dn_route_rx_packet: rt_flags=0x%02x dev=%s len=%d src=0x%04hx dst=0x%04hx err=%d type=%d\n",
			(int)cb->rt_flags, devname, skb->len,
			le16_to_cpu(cb->src), le16_to_cpu(cb->dst),
			err, skb->pkt_type);
	}

	if ((skb->pkt_type == PACKET_HOST) && (cb->rt_flags & DN_RT_F_RQR)) {
		switch (cb->rt_flags & DN_RT_PKT_MSK) {
		case DN_RT_PKT_SHORT:
			return dn_return_short(skb);
		case DN_RT_PKT_LONG:
			return dn_return_long(skb);
		}
	}

	kfree_skb(skb);
	return NET_RX_DROP;
}

static int dn_route_rx_long(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = DN_SKB_CB(skb);
	unsigned char *ptr = skb->data;

	if (!pskb_may_pull(skb, 21)) /* 20 for long header, 1 for shortest nsp */
		goto drop_it;

	skb_pull(skb, 20);
	skb_reset_transport_header(skb);

	/* Destination info */
	ptr += 2;
	cb->dst = dn_eth2dn(ptr);
	if (memcmp(ptr, dn_hiord_addr, 4) != 0)
		goto drop_it;
	ptr += 6;


	/* Source info */
	ptr += 2;
	cb->src = dn_eth2dn(ptr);
	if (memcmp(ptr, dn_hiord_addr, 4) != 0)
		goto drop_it;
	ptr += 6;
	/* Other junk */
	ptr++;
	cb->hops = *ptr++; /* Visit Count */

	return NF_HOOK(NFPROTO_DECNET, NF_DN_PRE_ROUTING, skb, skb->dev, NULL,
		       dn_route_rx_packet);

drop_it:
	kfree_skb(skb);
	return NET_RX_DROP;
}



static int dn_route_rx_short(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = DN_SKB_CB(skb);
	unsigned char *ptr = skb->data;

	if (!pskb_may_pull(skb, 6)) /* 5 for short header + 1 for shortest nsp */
		goto drop_it;

	skb_pull(skb, 5);
	skb_reset_transport_header(skb);

	cb->dst = *(__le16 *)ptr;
	ptr += 2;
	cb->src = *(__le16 *)ptr;
	ptr += 2;
	cb->hops = *ptr & 0x3f;

	return NF_HOOK(NFPROTO_DECNET, NF_DN_PRE_ROUTING, skb, skb->dev, NULL,
		       dn_route_rx_packet);

drop_it:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static int dn_route_discard(struct sk_buff *skb)
{
	/*
	 * I know we drop the packet here, but thats considered success in
	 * this case
	 */
	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

static int dn_route_ptp_hello(struct sk_buff *skb)
{
	dn_dev_hello(skb);
	dn_neigh_pointopoint_hello(skb);
	return NET_RX_SUCCESS;
}

int dn_route_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
{
	struct dn_skb_cb *cb;
	unsigned char flags = 0;
	__u16 len = le16_to_cpu(*(__le16 *)skb->data);
	struct dn_dev *dn = rcu_dereference(dev->dn_ptr);
	unsigned char padlen = 0;

	if (!net_eq(dev_net(dev), &init_net))
		goto dump_it;

	if (dn == NULL)
		goto dump_it;

	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)
		goto out;

	if (!pskb_may_pull(skb, 3))
		goto dump_it;

	skb_pull(skb, 2);

	if (len > skb->len)
		goto dump_it;

	skb_trim(skb, len);

	flags = *skb->data;

	cb = DN_SKB_CB(skb);
	cb->stamp = jiffies;
	cb->iif = dev->ifindex;

	/*
	 * If we have padding, remove it.
	 */
	if (flags & DN_RT_F_PF) {
		padlen = flags & ~DN_RT_F_PF;
		if (!pskb_may_pull(skb, padlen + 1))
			goto dump_it;
		skb_pull(skb, padlen);
		flags = *skb->data;
	}

	skb_reset_network_header(skb);

	/*
	 * Weed out future version DECnet
	 */
	if (flags & DN_RT_F_VER)
		goto dump_it;

	cb->rt_flags = flags;

	if (decnet_debug_level & 1)
		printk(KERN_DEBUG
			"dn_route_rcv: got 0x%02x from %s [%d %d %d]\n",
			(int)flags, (dev) ? dev->name : "???", len, skb->len,
			padlen);

	if (flags & DN_RT_PKT_CNTL) {
		if (unlikely(skb_linearize(skb)))
			goto dump_it;

		switch (flags & DN_RT_CNTL_MSK) {
		case DN_RT_PKT_INIT:
			dn_dev_init_pkt(skb);
			break;
		case DN_RT_PKT_VERI:
			dn_dev_veri_pkt(skb);
			break;
		}

		if (dn->parms.state != DN_DEV_S_RU)
			goto dump_it;

		switch (flags & DN_RT_CNTL_MSK) {
		case DN_RT_PKT_HELO:
			return NF_HOOK(NFPROTO_DECNET, NF_DN_HELLO,
				       skb, skb->dev, NULL,
				       dn_route_ptp_hello);

		case DN_RT_PKT_L1RT:
		case DN_RT_PKT_L2RT:
			return NF_HOOK(NFPROTO_DECNET, NF_DN_ROUTE,
				       skb, skb->dev, NULL,
				       dn_route_discard);
		case DN_RT_PKT_ERTH:
			return NF_HOOK(NFPROTO_DECNET, NF_DN_HELLO,
				       skb, skb->dev, NULL,
				       dn_neigh_router_hello);

		case DN_RT_PKT_EEDH:
			return NF_HOOK(NFPROTO_DECNET, NF_DN_HELLO,
				       skb, skb->dev, NULL,
				       dn_neigh_endnode_hello);
		}
	} else {
		if (dn->parms.state != DN_DEV_S_RU)
			goto dump_it;

		skb_pull(skb, 1); /* Pull flags */

		switch (flags & DN_RT_PKT_MSK) {
		case DN_RT_PKT_LONG:
			return dn_route_rx_long(skb);
		case DN_RT_PKT_SHORT:
			return dn_route_rx_short(skb);
		}
	}

dump_it:
	kfree_skb(skb);
out:
	return NET_RX_DROP;
}

static int dn_to_neigh_output(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct dn_route *rt = (struct dn_route *) dst;
	struct neighbour *n = rt->n;

	return n->output(n, skb);
}

static int dn_output(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct dn_route *rt = (struct dn_route *)dst;
	struct net_device *dev = dst->dev;
	struct dn_skb_cb *cb = DN_SKB_CB(skb);

	int err = -EINVAL;

	if (rt->n == NULL)
		goto error;

	skb->dev = dev;

	cb->src = rt->rt_saddr;
	cb->dst = rt->rt_daddr;

	/*
	 * Always set the Intra-Ethernet bit on all outgoing packets
	 * originated on this node. Only valid flag from upper layers
	 * is return-to-sender-requested. Set hop count to 0 too.
	 */
	cb->rt_flags &= ~DN_RT_F_RQR;
	cb->rt_flags |= DN_RT_F_IE;
	cb->hops = 0;

	return NF_HOOK(NFPROTO_DECNET, NF_DN_LOCAL_OUT, skb, NULL, dev,
		       dn_to_neigh_output);

error:
	net_dbg_ratelimited("dn_output: This should not happen\n");

	kfree_skb(skb);

	return err;
}

static int dn_forward(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = DN_SKB_CB(skb);
	struct dst_entry *dst = skb_dst(skb);
	struct dn_dev *dn_db = rcu_dereference(dst->dev->dn_ptr);
	struct dn_route *rt;
	int header_len;
#ifdef CONFIG_NETFILTER
	struct net_device *dev = skb->dev;
#endif

	if (skb->pkt_type != PACKET_HOST)
		goto drop;

	/* Ensure that we have enough space for headers */
	rt = (struct dn_route *)skb_dst(skb);
	header_len = dn_db->use_long ? 21 : 6;
	if (skb_cow(skb, LL_RESERVED_SPACE(rt->dst.dev)+header_len))
		goto drop;

	/*
	 * Hop count exceeded.
	 */
	if (++cb->hops > 30)
		goto drop;

	skb->dev = rt->dst.dev;

	/*
	 * If packet goes out same interface it came in on, then set
	 * the Intra-Ethernet bit. This has no effect for short
	 * packets, so we don't need to test for them here.
	 */
	cb->rt_flags &= ~DN_RT_F_IE;
	if (rt->rt_flags & RTCF_DOREDIRECT)
		cb->rt_flags |= DN_RT_F_IE;

	return NF_HOOK(NFPROTO_DECNET, NF_DN_FORWARD, skb, dev, skb->dev,
		       dn_to_neigh_output);

drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}

/*
 * Used to catch bugs. This should never normally get
 * called.
 */
static int dn_rt_bug(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = DN_SKB_CB(skb);

	net_dbg_ratelimited("dn_rt_bug: skb from:%04x to:%04x\n",
			    le16_to_cpu(cb->src), le16_to_cpu(cb->dst));

	kfree_skb(skb);

	return NET_RX_DROP;
}

static unsigned int dn_dst_default_advmss(const struct dst_entry *dst)
{
	return dn_mss_from_pmtu(dst->dev, dst_mtu(dst));
}

static unsigned int dn_dst_mtu(const struct dst_entry *dst)
{
	unsigned int mtu = dst_metric_raw(dst, RTAX_MTU);

	return mtu ? : dst->dev->mtu;
}

static struct neighbour *dn_dst_neigh_lookup(const struct dst_entry *dst,
					     struct sk_buff *skb,
					     const void *daddr)
{
	return __neigh_lookup_errno(&dn_neigh_table, daddr, dst->dev);
}

static int dn_rt_set_next_hop(struct dn_route *rt, struct dn_fib_res *res)
{
	struct dn_fib_info *fi = res->fi;
	struct net_device *dev = rt->dst.dev;
	unsigned int mss_metric;
	struct neighbour *n;

	if (fi) {
		if (DN_FIB_RES_GW(*res) &&
		    DN_FIB_RES_NH(*res).nh_scope == RT_SCOPE_LINK)
			rt->rt_gateway = DN_FIB_RES_GW(*res);
		dst_init_metrics(&rt->dst, fi->fib_metrics, true);
	}
	rt->rt_type = res->type;

	if (dev != NULL && rt->n == NULL) {
		n = __neigh_lookup_errno(&dn_neigh_table, &rt->rt_gateway, dev);
		if (IS_ERR(n))
			return PTR_ERR(n);
		rt->n = n;
	}

	if (dst_metric(&rt->dst, RTAX_MTU) > rt->dst.dev->mtu)
		dst_metric_set(&rt->dst, RTAX_MTU, rt->dst.dev->mtu);
	mss_metric = dst_metric_raw(&rt->dst, RTAX_ADVMSS);
	if (mss_metric) {
		unsigned int mss = dn_mss_from_pmtu(dev, dst_mtu(&rt->dst));
		if (mss_metric > mss)
			dst_metric_set(&rt->dst, RTAX_ADVMSS, mss);
	}
	return 0;
}

static inline int dn_match_addr(__le16 addr1, __le16 addr2)
{
	__u16 tmp = le16_to_cpu(addr1) ^ le16_to_cpu(addr2);
	int match = 16;
	while(tmp) {
		tmp >>= 1;
		match--;
	}
	return match;
}

static __le16 dnet_select_source(const struct net_device *dev, __le16 daddr, int scope)
{
	__le16 saddr = 0;
	struct dn_dev *dn_db;
	struct dn_ifaddr *ifa;
	int best_match = 0;
	int ret;

	rcu_read_lock();
	dn_db = rcu_dereference(dev->dn_ptr);
	for (ifa = rcu_dereference(dn_db->ifa_list);
	     ifa != NULL;
	     ifa = rcu_dereference(ifa->ifa_next)) {
		if (ifa->ifa_scope > scope)
			continue;
		if (!daddr) {
			saddr = ifa->ifa_local;
			break;
		}
		ret = dn_match_addr(daddr, ifa->ifa_local);
		if (ret > best_match)
			saddr = ifa->ifa_local;
		if (best_match == 0)
			saddr = ifa->ifa_local;
	}
	rcu_read_unlock();

	return saddr;
}

static inline __le16 __dn_fib_res_prefsrc(struct dn_fib_res *res)
{
	return dnet_select_source(DN_FIB_RES_DEV(*res), DN_FIB_RES_GW(*res), res->scope);
}

static inline __le16 dn_fib_rules_map_destination(__le16 daddr, struct dn_fib_res *res)
{
	__le16 mask = dnet_make_mask(res->prefixlen);
	return (daddr&~mask)|res->fi->fib_nh->nh_gw;
}

static int dn_route_output_slow(struct dst_entry **pprt, const struct flowidn *oldflp, int try_hard)
{
	struct flowidn fld = {
		.daddr = oldflp->daddr,
		.saddr = oldflp->saddr,
		.flowidn_scope = RT_SCOPE_UNIVERSE,
		.flowidn_mark = oldflp->flowidn_mark,
		.flowidn_iif = init_net.loopback_dev->ifindex,
		.flowidn_oif = oldflp->flowidn_oif,
	};
	struct dn_route *rt = NULL;
	struct net_device *dev_out = NULL, *dev;
	struct neighbour *neigh = NULL;
	unsigned int hash;
	unsigned int flags = 0;
	struct dn_fib_res res = { .fi = NULL, .type = RTN_UNICAST };
	int err;
	int free_res = 0;
	__le16 gateway = 0;

	if (decnet_debug_level & 16)
		printk(KERN_DEBUG
		       "dn_route_output_slow: dst=%04x src=%04x mark=%d"
		       " iif=%d oif=%d\n", le16_to_cpu(oldflp->daddr),
		       le16_to_cpu(oldflp->saddr),
		       oldflp->flowidn_mark, init_net.loopback_dev->ifindex,
		       oldflp->flowidn_oif);

	/* If we have an output interface, verify its a DECnet device */
	if (oldflp->flowidn_oif) {
		dev_out = dev_get_by_index(&init_net, oldflp->flowidn_oif);
		err = -ENODEV;
		if (dev_out && dev_out->dn_ptr == NULL) {
			dev_put(dev_out);
			dev_out = NULL;
		}
		if (dev_out == NULL)
			goto out;
	}

	/* If we have a source address, verify that its a local address */
	if (oldflp->saddr) {
		err = -EADDRNOTAVAIL;

		if (dev_out) {
			if (dn_dev_islocal(dev_out, oldflp->saddr))
				goto source_ok;
			dev_put(dev_out);
			goto out;
		}
		rcu_read_lock();
		for_each_netdev_rcu(&init_net, dev) {
			if (!dev->dn_ptr)
				continue;
			if (!dn_dev_islocal(dev, oldflp->saddr))
				continue;
			if ((dev->flags & IFF_LOOPBACK) &&
			    oldflp->daddr &&
			    !dn_dev_islocal(dev, oldflp->daddr))
				continue;

			dev_out = dev;
			break;
		}
		rcu_read_unlock();
		if (dev_out == NULL)
			goto out;
		dev_hold(dev_out);
source_ok:
		;
	}

	/* No destination? Assume its local */
	if (!fld.daddr) {
		fld.daddr = fld.saddr;

		err = -EADDRNOTAVAIL;
		if (dev_out)
			dev_put(dev_out);
		dev_out = init_net.loopback_dev;
		dev_hold(dev_out);
		if (!fld.daddr) {
			fld.daddr =
			fld.saddr = dnet_select_source(dev_out, 0,
						       RT_SCOPE_HOST);
			if (!fld.daddr)
				goto out;
		}
		fld.flowidn_oif = init_net.loopback_dev->ifindex;
		res.type = RTN_LOCAL;
		goto make_route;
	}

	if (decnet_debug_level & 16)
		printk(KERN_DEBUG
		       "dn_route_output_slow: initial checks complete."
		       " dst=%o4x src=%04x oif=%d try_hard=%d\n",
		       le16_to_cpu(fld.daddr), le16_to_cpu(fld.saddr),
		       fld.flowidn_oif, try_hard);

	/*
	 * N.B. If the kernel is compiled without router support then
	 * dn_fib_lookup() will evaluate to non-zero so this if () block
	 * will always be executed.
	 */
	err = -ESRCH;
	if (try_hard || (err = dn_fib_lookup(&fld, &res)) != 0) {
		struct dn_dev *dn_db;
		if (err != -ESRCH)
			goto out;
		/*
		 * Here the fallback is basically the standard algorithm for
		 * routing in endnodes which is described in the DECnet routing
		 * docs
		 *
		 * If we are not trying hard, look in neighbour cache.
		 * The result is tested to ensure that if a specific output
		 * device/source address was requested, then we honour that
		 * here
		 */
		if (!try_hard) {
			neigh = neigh_lookup_nodev(&dn_neigh_table, &init_net, &fld.daddr);
			if (neigh) {
				if ((oldflp->flowidn_oif &&
				    (neigh->dev->ifindex != oldflp->flowidn_oif)) ||
				    (oldflp->saddr &&
				    (!dn_dev_islocal(neigh->dev,
						     oldflp->saddr)))) {
					neigh_release(neigh);
					neigh = NULL;
				} else {
					if (dev_out)
						dev_put(dev_out);
					if (dn_dev_islocal(neigh->dev, fld.daddr)) {
						dev_out = init_net.loopback_dev;
						res.type = RTN_LOCAL;
					} else {
						dev_out = neigh->dev;
					}
					dev_hold(dev_out);
					goto select_source;
				}
			}
		}

		/* Not there? Perhaps its a local address */
		if (dev_out == NULL)
			dev_out = dn_dev_get_default();
		err = -ENODEV;
		if (dev_out == NULL)
			goto out;
		dn_db = rcu_dereference_raw(dev_out->dn_ptr);
		/* Possible improvement - check all devices for local addr */
		if (dn_dev_islocal(dev_out, fld.daddr)) {
			dev_put(dev_out);
			dev_out = init_net.loopback_dev;
			dev_hold(dev_out);
			res.type = RTN_LOCAL;
			goto select_source;
		}
		/* Not local either.... try sending it to the default router */
		neigh = neigh_clone(dn_db->router);
		BUG_ON(neigh && neigh->dev != dev_out);

		/* Ok then, we assume its directly connected and move on */
select_source:
		if (neigh)
			gateway = ((struct dn_neigh *)neigh)->addr;
		if (gateway == 0)
			gateway = fld.daddr;
		if (fld.saddr == 0) {
			fld.saddr = dnet_select_source(dev_out, gateway,
						       res.type == RTN_LOCAL ?
						       RT_SCOPE_HOST :
						       RT_SCOPE_LINK);
			if (fld.saddr == 0 && res.type != RTN_LOCAL)
				goto e_addr;
		}
		fld.flowidn_oif = dev_out->ifindex;
		goto make_route;
	}
	free_res = 1;

	if (res.type == RTN_NAT)
		goto e_inval;

	if (res.type == RTN_LOCAL) {
		if (!fld.saddr)
			fld.saddr = fld.daddr;
		if (dev_out)
			dev_put(dev_out);
		dev_out = init_net.loopback_dev;
		dev_hold(dev_out);
		fld.flowidn_oif = dev_out->ifindex;
		if (res.fi)
			dn_fib_info_put(res.fi);
		res.fi = NULL;
		goto make_route;
	}

	if (res.fi->fib_nhs > 1 && fld.flowidn_oif == 0)
		dn_fib_select_multipath(&fld, &res);

	/*
	 * We could add some logic to deal with default routes here and
	 * get rid of some of the special casing above.
	 */

	if (!fld.saddr)
		fld.saddr = DN_FIB_RES_PREFSRC(res);

	if (dev_out)
		dev_put(dev_out);
	dev_out = DN_FIB_RES_DEV(res);
	dev_hold(dev_out);
	fld.flowidn_oif = dev_out->ifindex;
	gateway = DN_FIB_RES_GW(res);

make_route:
	if (dev_out->flags & IFF_LOOPBACK)
		flags |= RTCF_LOCAL;

	rt = dst_alloc(&dn_dst_ops, dev_out, 1, 0, DST_HOST);
	if (rt == NULL)
		goto e_nobufs;

	memset(&rt->fld, 0, sizeof(rt->fld));
	rt->fld.saddr        = oldflp->saddr;
	rt->fld.daddr        = oldflp->daddr;
	rt->fld.flowidn_oif  = oldflp->flowidn_oif;
	rt->fld.flowidn_iif  = 0;
	rt->fld.flowidn_mark = oldflp->flowidn_mark;

	rt->rt_saddr      = fld.saddr;
	rt->rt_daddr      = fld.daddr;
	rt->rt_gateway    = gateway ? gateway : fld.daddr;
	rt->rt_local_src  = fld.saddr;

	rt->rt_dst_map    = fld.daddr;
	rt->rt_src_map    = fld.saddr;

	rt->n = neigh;
	neigh = NULL;

	rt->dst.lastuse = jiffies;
	rt->dst.output  = dn_output;
	rt->dst.input   = dn_rt_bug;
	rt->rt_flags      = flags;
	if (flags & RTCF_LOCAL)
		rt->dst.input = dn_nsp_rx;

	err = dn_rt_set_next_hop(rt, &res);
	if (err)
		goto e_neighbour;

	hash = dn_hash(rt->fld.saddr, rt->fld.daddr);
	dn_insert_route(rt, hash, (struct dn_route **)pprt);

done:
	if (neigh)
		neigh_release(neigh);
	if (free_res)
		dn_fib_res_put(&res);
	if (dev_out)
		dev_put(dev_out);
out:
	return err;

e_addr:
	err = -EADDRNOTAVAIL;
	goto done;
e_inval:
	err = -EINVAL;
	goto done;
e_nobufs:
	err = -ENOBUFS;
	goto done;
e_neighbour:
	dst_free(&rt->dst);
	goto e_nobufs;
}


/*
 * N.B. The flags may be moved into the flowi at some future stage.
 */
static int __dn_route_output_key(struct dst_entry **pprt, const struct flowidn *flp, int flags)
{
	unsigned int hash = dn_hash(flp->saddr, flp->daddr);
	struct dn_route *rt = NULL;

	if (!(flags & MSG_TRYHARD)) {
		rcu_read_lock_bh();
		for (rt = rcu_dereference_bh(dn_rt_hash_table[hash].chain); rt;
			rt = rcu_dereference_bh(rt->dst.dn_next)) {
			if ((flp->daddr == rt->fld.daddr) &&
			    (flp->saddr == rt->fld.saddr) &&
			    (flp->flowidn_mark == rt->fld.flowidn_mark) &&
			    dn_is_output_route(rt) &&
			    (rt->fld.flowidn_oif == flp->flowidn_oif)) {
				dst_use(&rt->dst, jiffies);
				rcu_read_unlock_bh();
				*pprt = &rt->dst;
				return 0;
			}
		}
		rcu_read_unlock_bh();
	}

	return dn_route_output_slow(pprt, flp, flags);
}

static int dn_route_output_key(struct dst_entry **pprt, struct flowidn *flp, int flags)
{
	int err;

	err = __dn_route_output_key(pprt, flp, flags);
	if (err == 0 && flp->flowidn_proto) {
		*pprt = xfrm_lookup(&init_net, *pprt,
				    flowidn_to_flowi(flp), NULL, 0);
		if (IS_ERR(*pprt)) {
			err = PTR_ERR(*pprt);
			*pprt = NULL;
		}
	}
	return err;
}

int dn_route_output_sock(struct dst_entry **pprt, struct flowidn *fl, struct sock *sk, int flags)
{
	int err;

	err = __dn_route_output_key(pprt, fl, flags & MSG_TRYHARD);
	if (err == 0 && fl->flowidn_proto) {
		if (!(flags & MSG_DONTWAIT))
			fl->flowidn_flags |= FLOWI_FLAG_CAN_SLEEP;
		*pprt = xfrm_lookup(&init_net, *pprt,
				    flowidn_to_flowi(fl), sk, 0);
		if (IS_ERR(*pprt)) {
			err = PTR_ERR(*pprt);
			*pprt = NULL;
		}
	}
	return err;
}

static int dn_route_input_slow(struct sk_buff *skb)
{
	struct dn_route *rt = NULL;
	struct dn_skb_cb *cb = DN_SKB_CB(skb);
	struct net_device *in_dev = skb->dev;
	struct net_device *out_dev = NULL;
	struct dn_dev *dn_db;
	struct neighbour *neigh = NULL;
	unsigned int hash;
	int flags = 0;
	__le16 gateway = 0;
	__le16 local_src = 0;
	struct flowidn fld = {
		.daddr = cb->dst,
		.saddr = cb->src,
		.flowidn_scope = RT_SCOPE_UNIVERSE,
		.flowidn_mark = skb->mark,
		.flowidn_iif = skb->dev->ifindex,
	};
	struct dn_fib_res res = { .fi = NULL, .type = RTN_UNREACHABLE };
	int err = -EINVAL;
	int free_res = 0;

	dev_hold(in_dev);

	if ((dn_db = rcu_dereference(in_dev->dn_ptr)) == NULL)
		goto out;

	/* Zero source addresses are not allowed */
	if (fld.saddr == 0)
		goto out;

	/*
	 * In this case we've just received a packet from a source
	 * outside ourselves pretending to come from us. We don't
	 * allow it any further to prevent routing loops, spoofing and
	 * other nasties. Loopback packets already have the dst attached
	 * so this only affects packets which have originated elsewhere.
	 */
	err  = -ENOTUNIQ;
	if (dn_dev_islocal(in_dev, cb->src))
		goto out;

	err = dn_fib_lookup(&fld, &res);
	if (err) {
		if (err != -ESRCH)
			goto out;
		/*
		 * Is the destination us ?
		 */
		if (!dn_dev_islocal(in_dev, cb->dst))
			goto e_inval;

		res.type = RTN_LOCAL;
	} else {
		__le16 src_map = fld.saddr;
		free_res = 1;

		out_dev = DN_FIB_RES_DEV(res);
		if (out_dev == NULL) {
			net_crit_ratelimited("Bug in dn_route_input_slow() No output device\n");
			goto e_inval;
		}
		dev_hold(out_dev);

		if (res.r)
			src_map = fld.saddr; /* no NAT support for now */

		gateway = DN_FIB_RES_GW(res);
		if (res.type == RTN_NAT) {
			fld.daddr = dn_fib_rules_map_destination(fld.daddr, &res);
			dn_fib_res_put(&res);
			free_res = 0;
			if (dn_fib_lookup(&fld, &res))
				goto e_inval;
			free_res = 1;
			if (res.type != RTN_UNICAST)
				goto e_inval;
			flags |= RTCF_DNAT;
			gateway = fld.daddr;
		}
		fld.saddr = src_map;
	}

	switch(res.type) {
	case RTN_UNICAST:
		/*
		 * Forwarding check here, we only check for forwarding
		 * being turned off, if you want to only forward intra
		 * area, its up to you to set the routing tables up
		 * correctly.
		 */
		if (dn_db->parms.forwarding == 0)
			goto e_inval;

		if (res.fi->fib_nhs > 1 && fld.flowidn_oif == 0)
			dn_fib_select_multipath(&fld, &res);

		/*
		 * Check for out_dev == in_dev. We use the RTCF_DOREDIRECT
		 * flag as a hint to set the intra-ethernet bit when
		 * forwarding. If we've got NAT in operation, we don't do
		 * this optimisation.
		 */
		if (out_dev == in_dev && !(flags & RTCF_NAT))
			flags |= RTCF_DOREDIRECT;

		local_src = DN_FIB_RES_PREFSRC(res);

	case RTN_BLACKHOLE:
	case RTN_UNREACHABLE:
		break;
	case RTN_LOCAL:
		flags |= RTCF_LOCAL;
		fld.saddr = cb->dst;
		fld.daddr = cb->src;

		/* Routing tables gave us a gateway */
		if (gateway)
			goto make_route;

		/* Packet was intra-ethernet, so we know its on-link */
		if (cb->rt_flags & DN_RT_F_IE) {
			gateway = cb->src;
			flags |= RTCF_DIRECTSRC;
			goto make_route;
		}

		/* Use the default router if there is one */
		neigh = neigh_clone(dn_db->router);
		if (neigh) {
			gateway = ((struct dn_neigh *)neigh)->addr;
			goto make_route;
		}

		/* Close eyes and pray */
		gateway = cb->src;
		flags |= RTCF_DIRECTSRC;
		goto make_route;
	default:
		goto e_inval;
	}

make_route:
	rt = dst_alloc(&dn_dst_ops, out_dev, 0, 0, DST_HOST);
	if (rt == NULL)
		goto e_nobufs;

	memset(&rt->fld, 0, sizeof(rt->fld));
	rt->rt_saddr      = fld.saddr;
	rt->rt_daddr      = fld.daddr;
	rt->rt_gateway    = fld.daddr;
	if (gateway)
		rt->rt_gateway = gateway;
	rt->rt_local_src  = local_src ? local_src : rt->rt_saddr;

	rt->rt_dst_map    = fld.daddr;
	rt->rt_src_map    = fld.saddr;

	rt->fld.saddr        = cb->src;
	rt->fld.daddr        = cb->dst;
	rt->fld.flowidn_oif  = 0;
	rt->fld.flowidn_iif  = in_dev->ifindex;
	rt->fld.flowidn_mark = fld.flowidn_mark;

	rt->n = neigh;
	rt->dst.lastuse = jiffies;
	rt->dst.output = dn_rt_bug;
	switch (res.type) {
	case RTN_UNICAST:
		rt->dst.input = dn_forward;
		break;
	case RTN_LOCAL:
		rt->dst.output = dn_output;
		rt->dst.input = dn_nsp_rx;
		rt->dst.dev = in_dev;
		flags |= RTCF_LOCAL;
		break;
	default:
	case RTN_UNREACHABLE:
	case RTN_BLACKHOLE:
		rt->dst.input = dst_discard;
	}
	rt->rt_flags = flags;

	err = dn_rt_set_next_hop(rt, &res);
	if (err)
		goto e_neighbour;

	hash = dn_hash(rt->fld.saddr, rt->fld.daddr);
	dn_insert_route(rt, hash, &rt);
	skb_dst_set(skb, &rt->dst);

done:
	if (neigh)
		neigh_release(neigh);
	if (free_res)
		dn_fib_res_put(&res);
	dev_put(in_dev);
	if (out_dev)
		dev_put(out_dev);
out:
	return err;

e_inval:
	err = -EINVAL;
	goto done;

e_nobufs:
	err = -ENOBUFS;
	goto done;

e_neighbour:
	dst_free(&rt->dst);
	goto done;
}

static int dn_route_input(struct sk_buff *skb)
{
	struct dn_route *rt;
	struct dn_skb_cb *cb = DN_SKB_CB(skb);
	unsigned int hash = dn_hash(cb->src, cb->dst);

	if (skb_dst(skb))
		return 0;

	rcu_read_lock();
	for(rt = rcu_dereference(dn_rt_hash_table[hash].chain); rt != NULL;
	    rt = rcu_dereference(rt->dst.dn_next)) {
		if ((rt->fld.saddr == cb->src) &&
		    (rt->fld.daddr == cb->dst) &&
		    (rt->fld.flowidn_oif == 0) &&
		    (rt->fld.flowidn_mark == skb->mark) &&
		    (rt->fld.flowidn_iif == cb->iif)) {
			dst_use(&rt->dst, jiffies);
			rcu_read_unlock();
			skb_dst_set(skb, (struct dst_entry *)rt);
			return 0;
		}
	}
	rcu_read_unlock();

	return dn_route_input_slow(skb);
}

static int dn_rt_fill_info(struct sk_buff *skb, u32 pid, u32 seq,
			   int event, int nowait, unsigned int flags)
{
	struct dn_route *rt = (struct dn_route *)skb_dst(skb);
	struct rtmsg *r;
	struct nlmsghdr *nlh;
	long expires;

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(*r), flags);
	if (!nlh)
		return -EMSGSIZE;

	r = nlmsg_data(nlh);
	r->rtm_family = AF_DECnet;
	r->rtm_dst_len = 16;
	r->rtm_src_len = 0;
	r->rtm_tos = 0;
	r->rtm_table = RT_TABLE_MAIN;
	r->rtm_type = rt->rt_type;
	r->rtm_flags = (rt->rt_flags & ~0xFFFF) | RTM_F_CLONED;
	r->rtm_scope = RT_SCOPE_UNIVERSE;
	r->rtm_protocol = RTPROT_UNSPEC;

	if (rt->rt_flags & RTCF_NOTIFY)
		r->rtm_flags |= RTM_F_NOTIFY;

	if (nla_put_u32(skb, RTA_TABLE, RT_TABLE_MAIN) < 0 ||
	    nla_put_le16(skb, RTA_DST, rt->rt_daddr) < 0)
		goto errout;

	if (rt->fld.saddr) {
		r->rtm_src_len = 16;
		if (nla_put_le16(skb, RTA_SRC, rt->fld.saddr) < 0)
			goto errout;
	}
	if (rt->dst.dev &&
	    nla_put_u32(skb, RTA_OIF, rt->dst.dev->ifindex) < 0)
		goto errout;

	/*
	 * Note to self - change this if input routes reverse direction when
	 * they deal only with inputs and not with replies like they do
	 * currently.
	 */
	if (nla_put_le16(skb, RTA_PREFSRC, rt->rt_local_src) < 0)
		goto errout;

	if (rt->rt_daddr != rt->rt_gateway &&
	    nla_put_le16(skb, RTA_GATEWAY, rt->rt_gateway) < 0)
		goto errout;

	if (rtnetlink_put_metrics(skb, dst_metrics_ptr(&rt->dst)) < 0)
		goto errout;

	expires = rt->dst.expires ? rt->dst.expires - jiffies : 0;
	if (rtnl_put_cacheinfo(skb, &rt->dst, 0, 0, 0, expires,
			       rt->dst.error) < 0)
		goto errout;

	if (dn_is_input_route(rt) &&
	    nla_put_u32(skb, RTA_IIF, rt->fld.flowidn_iif) < 0)
		goto errout;

	return nlmsg_end(skb, nlh);

errout:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

/*
 * This is called by both endnodes and routers now.
 */
static int dn_cache_getroute(struct sk_buff *in_skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = sock_net(in_skb->sk);
	struct rtattr **rta = arg;
	struct rtmsg *rtm = nlmsg_data(nlh);
	struct dn_route *rt = NULL;
	struct dn_skb_cb *cb;
	int err;
	struct sk_buff *skb;
	struct flowidn fld;

	if (!net_eq(net, &init_net))
		return -EINVAL;

	memset(&fld, 0, sizeof(fld));
	fld.flowidn_proto = DNPROTO_NSP;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb == NULL)
		return -ENOBUFS;
	skb_reset_mac_header(skb);
	cb = DN_SKB_CB(skb);

	if (rta[RTA_SRC-1])
		memcpy(&fld.saddr, RTA_DATA(rta[RTA_SRC-1]), 2);
	if (rta[RTA_DST-1])
		memcpy(&fld.daddr, RTA_DATA(rta[RTA_DST-1]), 2);
	if (rta[RTA_IIF-1])
		memcpy(&fld.flowidn_iif, RTA_DATA(rta[RTA_IIF-1]), sizeof(int));

	if (fld.flowidn_iif) {
		struct net_device *dev;
		if ((dev = dev_get_by_index(&init_net, fld.flowidn_iif)) == NULL) {
			kfree_skb(skb);
			return -ENODEV;
		}
		if (!dev->dn_ptr) {
			dev_put(dev);
			kfree_skb(skb);
			return -ENODEV;
		}
		skb->protocol = htons(ETH_P_DNA_RT);
		skb->dev = dev;
		cb->src = fld.saddr;
		cb->dst = fld.daddr;
		local_bh_disable();
		err = dn_route_input(skb);
		local_bh_enable();
		memset(cb, 0, sizeof(struct dn_skb_cb));
		rt = (struct dn_route *)skb_dst(skb);
		if (!err && -rt->dst.error)
			err = rt->dst.error;
	} else {
		int oif = 0;
		if (rta[RTA_OIF - 1])
			memcpy(&oif, RTA_DATA(rta[RTA_OIF - 1]), sizeof(int));
		fld.flowidn_oif = oif;
		err = dn_route_output_key((struct dst_entry **)&rt, &fld, 0);
	}

	if (skb->dev)
		dev_put(skb->dev);
	skb->dev = NULL;
	if (err)
		goto out_free;
	skb_dst_set(skb, &rt->dst);
	if (rtm->rtm_flags & RTM_F_NOTIFY)
		rt->rt_flags |= RTCF_NOTIFY;

	err = dn_rt_fill_info(skb, NETLINK_CB(in_skb).pid, nlh->nlmsg_seq, RTM_NEWROUTE, 0, 0);

	if (err == 0)
		goto out_free;
	if (err < 0) {
		err = -EMSGSIZE;
		goto out_free;
	}

	return rtnl_unicast(skb, &init_net, NETLINK_CB(in_skb).pid);

out_free:
	kfree_skb(skb);
	return err;
}

/*
 * For routers, this is called from dn_fib_dump, but for endnodes its
 * called directly from the rtnetlink dispatch table.
 */
int dn_cache_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct dn_route *rt;
	int h, s_h;
	int idx, s_idx;
	struct rtmsg *rtm;

	if (!net_eq(net, &init_net))
		return 0;

	if (nlmsg_len(cb->nlh) < sizeof(struct rtmsg))
		return -EINVAL;

	rtm = nlmsg_data(cb->nlh);
	if (!(rtm->rtm_flags & RTM_F_CLONED))
		return 0;

	s_h = cb->args[0];
	s_idx = idx = cb->args[1];
	for(h = 0; h <= dn_rt_hash_mask; h++) {
		if (h < s_h)
			continue;
		if (h > s_h)
			s_idx = 0;
		rcu_read_lock_bh();
		for(rt = rcu_dereference_bh(dn_rt_hash_table[h].chain), idx = 0;
			rt;
			rt = rcu_dereference_bh(rt->dst.dn_next), idx++) {
			if (idx < s_idx)
				continue;
			skb_dst_set(skb, dst_clone(&rt->dst));
			if (dn_rt_fill_info(skb, NETLINK_CB(cb->skb).pid,
					cb->nlh->nlmsg_seq, RTM_NEWROUTE,
					1, NLM_F_MULTI) <= 0) {
				skb_dst_drop(skb);
				rcu_read_unlock_bh();
				goto done;
			}
			skb_dst_drop(skb);
		}
		rcu_read_unlock_bh();
	}

done:
	cb->args[0] = h;
	cb->args[1] = idx;
	return skb->len;
}

#ifdef CONFIG_PROC_FS
struct dn_rt_cache_iter_state {
	int bucket;
};

static struct dn_route *dn_rt_cache_get_first(struct seq_file *seq)
{
	struct dn_route *rt = NULL;
	struct dn_rt_cache_iter_state *s = seq->private;

	for(s->bucket = dn_rt_hash_mask; s->bucket >= 0; --s->bucket) {
		rcu_read_lock_bh();
		rt = rcu_dereference_bh(dn_rt_hash_table[s->bucket].chain);
		if (rt)
			break;
		rcu_read_unlock_bh();
	}
	return rt;
}

static struct dn_route *dn_rt_cache_get_next(struct seq_file *seq, struct dn_route *rt)
{
	struct dn_rt_cache_iter_state *s = seq->private;

	rt = rcu_dereference_bh(rt->dst.dn_next);
	while (!rt) {
		rcu_read_unlock_bh();
		if (--s->bucket < 0)
			break;
		rcu_read_lock_bh();
		rt = rcu_dereference_bh(dn_rt_hash_table[s->bucket].chain);
	}
	return rt;
}

static void *dn_rt_cache_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct dn_route *rt = dn_rt_cache_get_first(seq);

	if (rt) {
		while(*pos && (rt = dn_rt_cache_get_next(seq, rt)))
			--*pos;
	}
	return *pos ? NULL : rt;
}

static void *dn_rt_cache_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct dn_route *rt = dn_rt_cache_get_next(seq, v);
	++*pos;
	return rt;
}

static void dn_rt_cache_seq_stop(struct seq_file *seq, void *v)
{
	if (v)
		rcu_read_unlock_bh();
}

static int dn_rt_cache_seq_show(struct seq_file *seq, void *v)
{
	struct dn_route *rt = v;
	char buf1[DN_ASCBUF_LEN], buf2[DN_ASCBUF_LEN];

	seq_printf(seq, "%-8s %-7s %-7s %04d %04d %04d\n",
		   rt->dst.dev ? rt->dst.dev->name : "*",
		   dn_addr2asc(le16_to_cpu(rt->rt_daddr), buf1),
		   dn_addr2asc(le16_to_cpu(rt->rt_saddr), buf2),
		   atomic_read(&rt->dst.__refcnt),
		   rt->dst.__use, 0);
	return 0;
}

static const struct seq_operations dn_rt_cache_seq_ops = {
	.start	= dn_rt_cache_seq_start,
	.next	= dn_rt_cache_seq_next,
	.stop	= dn_rt_cache_seq_stop,
	.show	= dn_rt_cache_seq_show,
};

static int dn_rt_cache_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_private(file, &dn_rt_cache_seq_ops,
			sizeof(struct dn_rt_cache_iter_state));
}

static const struct file_operations dn_rt_cache_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = dn_rt_cache_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_private,
};

#endif /* CONFIG_PROC_FS */

void __init dn_route_init(void)
{
	int i, goal, order;

	dn_dst_ops.kmem_cachep =
		kmem_cache_create("dn_dst_cache", sizeof(struct dn_route), 0,
				  SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);
	dst_entries_init(&dn_dst_ops);
	setup_timer(&dn_route_timer, dn_dst_check_expire, 0);
	dn_route_timer.expires = jiffies + decnet_dst_gc_interval * HZ;
	add_timer(&dn_route_timer);

	goal = totalram_pages >> (26 - PAGE_SHIFT);

	for(order = 0; (1UL << order) < goal; order++)
		/* NOTHING */;

	/*
	 * Only want 1024 entries max, since the table is very, very unlikely
	 * to be larger than that.
	 */
	while(order && ((((1UL << order) * PAGE_SIZE) /
				sizeof(struct dn_rt_hash_bucket)) >= 2048))
		order--;

	do {
		dn_rt_hash_mask = (1UL << order) * PAGE_SIZE /
			sizeof(struct dn_rt_hash_bucket);
		while(dn_rt_hash_mask & (dn_rt_hash_mask - 1))
			dn_rt_hash_mask--;
		dn_rt_hash_table = (struct dn_rt_hash_bucket *)
			__get_free_pages(GFP_ATOMIC, order);
	} while (dn_rt_hash_table == NULL && --order > 0);

	if (!dn_rt_hash_table)
		panic("Failed to allocate DECnet route cache hash table\n");

	printk(KERN_INFO
		"DECnet: Routing cache hash table of %u buckets, %ldKbytes\n",
		dn_rt_hash_mask,
		(long)(dn_rt_hash_mask*sizeof(struct dn_rt_hash_bucket))/1024);

	dn_rt_hash_mask--;
	for(i = 0; i <= dn_rt_hash_mask; i++) {
		spin_lock_init(&dn_rt_hash_table[i].lock);
		dn_rt_hash_table[i].chain = NULL;
	}

	dn_dst_ops.gc_thresh = (dn_rt_hash_mask + 1);

	proc_net_fops_create(&init_net, "decnet_cache", S_IRUGO, &dn_rt_cache_seq_fops);

#ifdef CONFIG_DECNET_ROUTER
	rtnl_register(PF_DECnet, RTM_GETROUTE, dn_cache_getroute,
		      dn_fib_dump, NULL);
#else
	rtnl_register(PF_DECnet, RTM_GETROUTE, dn_cache_getroute,
		      dn_cache_dump, NULL);
#endif
}

void __exit dn_route_cleanup(void)
{
	del_timer(&dn_route_timer);
	dn_run_flush(0);

	proc_net_remove(&init_net, "decnet_cache");
	dst_entries_destroy(&dn_dst_ops);
}

