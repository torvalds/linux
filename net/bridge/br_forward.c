// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Forwarding decision
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/netpoll.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/netfilter_bridge.h>
#include "br_private.h"

struct br_fwd_dst {
	struct net_bridge_port *port;
	struct net_bridge_vlan *vlan;
};

static bool should_deliver_vlan(const struct br_fwd_dst *fwd,
				const struct sk_buff *skb)
{
	if (fwd->vlan)
		return br_vlan_state_allowed(br_vlan_get_state(fwd->vlan),
					     false);

	return br_allowed_egress(nbp_vlan_group_rcu(fwd->port), skb);
}

/* Don't forward packets to originating port or forwarding disabled */
static __always_inline bool should_deliver(const struct br_fwd_dst *fwd,
					   const struct sk_buff *skb)
{
	const struct net_bridge_port *p = fwd->port;

	return (test_bit(BR_HAIRPIN_MODE_BIT, &p->flags) || skb->dev != p->dev) &&
		(br_mst_is_enabled(p) || p->state == BR_STATE_FORWARDING) &&
		should_deliver_vlan(fwd, skb) &&
		nbp_switchdev_allowed_egress(p, skb) &&
		!br_skb_isolated(p, skb);
}

int br_dev_queue_push_xmit(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	skb_push(skb, ETH_HLEN);
	if (!is_skb_forwardable(skb->dev, skb))
		goto drop;

	br_drop_fake_rtable(skb);

	if (skb->ip_summed == CHECKSUM_PARTIAL &&
	    eth_type_vlan(skb->protocol)) {
		int depth;

		if (!vlan_get_protocol_and_depth(skb, skb->protocol, &depth))
			goto drop;

		skb_set_network_header(skb, depth);
	}

	br_switchdev_frame_set_offload_fwd_mark(skb);

	dev_queue_xmit(skb);

	return 0;

drop:
	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL_GPL(br_dev_queue_push_xmit);

int br_forward_finish(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	skb_clear_tstamp(skb);
	return NF_HOOK(NFPROTO_BRIDGE, NF_BR_POST_ROUTING,
		       net, sk, skb, NULL, skb->dev,
		       br_dev_queue_push_xmit);

}
EXPORT_SYMBOL_GPL(br_forward_finish);

static void __br_forward(const struct br_fwd_dst *fwd,
			 struct sk_buff *skb, bool local_orig)
{
	const struct net_bridge_port *to = fwd->port;
	struct net_bridge_vlan_group *vg;
	struct net_device *indev;
	struct net *net;
	int br_hook;

	/* Mark the skb for forwarding offload early so that br_handle_vlan()
	 * can know whether to pop the VLAN header on egress or keep it.
	 */
	nbp_switchdev_frame_mark_tx_fwd_offload(to, skb);

	vg = fwd->vlan ? NULL : nbp_vlan_group_rcu(to);
	skb = br_handle_vlan(to->br, to, vg, fwd->vlan, skb);
	if (!skb)
		return;

	indev = skb->dev;
	skb->dev = to->dev;
	if (!local_orig) {
		if (skb_warn_if_lro(skb)) {
			kfree_skb(skb);
			return;
		}
		br_hook = NF_BR_FORWARD;
		skb_forward_csum(skb);
		net = dev_net(indev);
	} else {
		if (unlikely(netpoll_tx_running(to->br->dev))) {
			skb_push(skb, ETH_HLEN);
			if (!is_skb_forwardable(skb->dev, skb))
				kfree_skb(skb);
			else
				br_netpoll_send_skb(to, skb);
			return;
		}
		br_hook = NF_BR_LOCAL_OUT;
		net = dev_net(skb->dev);
		indev = NULL;
	}

	NF_HOOK(NFPROTO_BRIDGE, br_hook,
		net, NULL, skb, indev, skb->dev,
		br_forward_finish);
}

static int deliver_clone(const struct br_fwd_dst *fwd,
			 struct sk_buff *skb, bool local_orig)
{
	struct net_device *dev = BR_INPUT_SKB_CB(skb)->brdev;

	skb = skb_clone(skb, GFP_ATOMIC);
	if (!skb) {
		DEV_STATS_INC(dev, tx_dropped);
		return -ENOMEM;
	}

	__br_forward(fwd, skb, local_orig);
	return 0;
}

/**
 * br_forward - forward a packet to a specific port
 * @dst: bridge destination
 * @skb: packet being forwarded
 * @local_rcv: packet will be received locally after forwarding
 * @local_orig: packet is locally originated
 *
 * Should be called with rcu_read_lock.
 */
void br_forward(struct net_bridge_dst dst,
		struct sk_buff *skb, bool local_rcv, bool local_orig)
{
	struct br_fwd_dst fwd;

	br_dst_decode(dst, &fwd.port, &fwd.vlan);

	if (unlikely(!fwd.port))
		goto out;

	/* redirect to backup link if the destination port is down */
	if (rcu_access_pointer(fwd.port->backup_port) &&
	    (!netif_carrier_ok(fwd.port->dev) ||
	     !netif_running(fwd.port->dev))) {
		struct net_bridge_port *backup_port;

		backup_port = rcu_dereference(fwd.port->backup_port);
		if (unlikely(!backup_port))
			goto out;
		BR_INPUT_SKB_CB(skb)->backup_nhid = READ_ONCE(fwd.port->backup_nhid);
		fwd.port = backup_port;
		fwd.vlan = NULL;
	}

	if (should_deliver(&fwd, skb)) {
		if (local_rcv)
			deliver_clone(&fwd, skb, local_orig);
		else
			__br_forward(&fwd, skb, local_orig);
		return;
	}

out:
	if (!local_rcv)
		kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(br_forward);

static int maybe_deliver(struct br_fwd_dst *prev, const struct br_fwd_dst *fwd,
			 struct sk_buff *skb, bool local_orig)
{
	const struct net_bridge_port *p = fwd->port;
	u8 igmp_type = br_multicast_igmp_type(skb);
	int err;

	if (!should_deliver(fwd, skb))
		return 0;

	nbp_switchdev_frame_mark_tx_fwd_to_hwdom(p, skb);

	if (!prev->port)
		goto out;

	err = deliver_clone(prev, skb, local_orig);
	if (err)
		return err;
out:
	br_multicast_count(p->br, p, skb, igmp_type, BR_MCAST_DIR_TX);
	*prev = *fwd;

	return 0;
}

static void br_flood_finish(const struct br_fwd_dst *fwd, int err,
			    struct sk_buff *skb,
			    bool local_rcv, bool local_orig)
{
	enum skb_drop_reason reason = SKB_DROP_REASON_NO_TX_TARGET;

	if (err || !fwd->port) {
		if (err)
			reason = err == -ENOMEM ? SKB_DROP_REASON_NOMEM :
						  SKB_DROP_REASON_NOT_SPECIFIED;

		if (!local_rcv)
			kfree_skb_reason(skb, reason);
		return;
	}

	if (local_rcv)
		deliver_clone(fwd, skb, local_orig);
	else
		__br_forward(fwd, skb, local_orig);
}

static int br_flood_port(struct br_fwd_dst *prev,
			 const struct br_fwd_dst *fwd, struct sk_buff *skb,
			 enum br_pkt_type pkt_type, bool local_orig)
{
	const struct net_bridge_port *p = fwd->port;

	/* Do not flood unicast traffic to ports that turn it off, nor
	 * other traffic if flood off, except for traffic we originate
	 */
	switch (pkt_type) {
	case BR_PKT_UNICAST:
		if (!test_bit(BR_FLOOD_BIT, &p->flags))
			return 0;
		break;
	case BR_PKT_MULTICAST:
		if (!test_bit(BR_MCAST_FLOOD_BIT, &p->flags) &&
		    skb->dev != p->br->dev)
			return 0;
		break;
	case BR_PKT_BROADCAST:
		if (!test_bit(BR_BCAST_FLOOD_BIT, &p->flags) &&
		    skb->dev != p->br->dev)
			return 0;
		break;
	}

	/* Do not flood to ports that enable proxy ARP */
	if (test_bit(BR_PROXYARP_BIT, &p->flags))
		return 0;
	if (BR_INPUT_SKB_CB(skb)->proxyarp_replied) {
		if (test_bit(BR_PROXYARP_WIFI_BIT, &p->flags))
			return 0;
		/* For gratuitous ARPs/NAs, check neigh_forward_grat.
		 * For regular ARPs/NDs, check only neigh_suppress.
		 */
		if (br_is_neigh_suppress_enabled(p, fwd->vlan) &&
		    (!BR_INPUT_SKB_CB(skb)->grat_arp ||
		     !br_is_neigh_forward_grat_enabled(p, fwd->vlan)))
			return 0;
	}

	return maybe_deliver(prev, fwd, skb, local_orig);
}

static int br_flood_vlan(struct br_fwd_dst *prev,
			 struct net_bridge_vlan *v, struct sk_buff *skb,
			 enum br_pkt_type pkt_type, bool local_orig)
{
	struct net_bridge_vlan_port_array *array;
	struct net_bridge_vlan *masterv, *pv;
	struct br_fwd_dst fwd;
	int err;

	masterv = br_vlan_is_master(v) ? v : v->brvlan;
	array = rcu_dereference(masterv->port_array);
	if (array) {
		unsigned int i;

		for (i = 0; i < array->count; i++) {
			pv = array->vlans[i];
			fwd.port = pv->port;
			fwd.vlan = pv;
			err = br_flood_port(prev, &fwd, skb, pkt_type,
					    local_orig);
			if (err)
				return err;
		}
	} else {
		list_for_each_entry_rcu(pv, &masterv->port_vlist, port_vlist) {
			fwd.port = pv->port;
			fwd.vlan = pv;
			err = br_flood_port(prev, &fwd, skb, pkt_type,
					    local_orig);
			if (err)
				return err;
		}
	}

	return 0;
}

/* called under rcu_read_lock */
void br_flood(struct net_bridge *br, struct net_bridge_vlan *v,
	      struct sk_buff *skb, enum br_pkt_type pkt_type,
	      bool local_rcv, bool local_orig)
{
	struct br_fwd_dst prev = {};
	int err = 0;

	br_tc_skb_miss_set(skb, pkt_type != BR_PKT_BROADCAST);

	if (v) {
		err = br_flood_vlan(&prev, v, skb, pkt_type, local_orig);
	} else {
		struct net_bridge_port *p;

		list_for_each_entry_rcu(p, &br->port_list, list) {
			struct br_fwd_dst fwd = {
				.port = p,
			};

			err = br_flood_port(&prev, &fwd, skb, pkt_type,
					    local_orig);
			if (err)
				break;
		}
	}

	br_flood_finish(&prev, err, skb, local_rcv, local_orig);
}

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
static void maybe_deliver_addr(struct net_bridge_port *p, struct sk_buff *skb,
			       const unsigned char *addr, bool local_orig)
{
	struct net_device *dev = BR_INPUT_SKB_CB(skb)->brdev;
	const unsigned char *src = eth_hdr(skb)->h_source;
	struct br_fwd_dst fwd = {
		.port = p,
	};
	struct sk_buff *nskb;

	if (!should_deliver(&fwd, skb))
		return;

	/* Even with hairpin, no soliloquies - prevent breaking IPv6 DAD */
	if (skb->dev == p->dev && ether_addr_equal(src, addr))
		return;

	__skb_push(skb, ETH_HLEN);
	nskb = pskb_copy(skb, GFP_ATOMIC);
	__skb_pull(skb, ETH_HLEN);
	if (!nskb) {
		DEV_STATS_INC(dev, tx_dropped);
		return;
	}

	skb = nskb;
	__skb_pull(skb, ETH_HLEN);
	if (!is_broadcast_ether_addr(addr))
		memcpy(eth_hdr(skb)->h_dest, addr, ETH_ALEN);

	__br_forward(&fwd, skb, local_orig);
}

/* called with rcu_read_lock */
void br_multicast_flood(struct net_bridge_mdb_entry *mdst,
			struct sk_buff *skb,
			struct net_bridge_mcast *brmctx,
			bool local_rcv, bool local_orig)
{
	struct net_bridge_port_group *p;
	bool allow_mode_include = true;
	struct br_fwd_dst prev = {};
	struct hlist_node *rp;
	int err = 0;

	rp = br_multicast_get_first_rport_node(brmctx, skb);

	if (mdst) {
		p = rcu_dereference(mdst->ports);
		if (br_multicast_should_handle_mode(brmctx, mdst->addr.proto) &&
		    br_multicast_is_star_g(&mdst->addr))
			allow_mode_include = false;
	} else {
		p = NULL;
		br_tc_skb_miss_set(skb, true);
	}

	while (p || rp) {
		struct net_bridge_port *port, *lport, *rport;
		struct br_fwd_dst fwd = {};

		lport = p ? p->key.port : NULL;
		rport = br_multicast_rport_from_node_skb(rp, skb);

		if ((unsigned long)lport > (unsigned long)rport) {
			port = lport;

			if (test_bit(BR_MULTICAST_TO_UNICAST_BIT,
				     &port->flags)) {
				maybe_deliver_addr(lport, skb, p->eth_addr,
						   local_orig);
				goto delivered;
			}
			if ((!allow_mode_include &&
			     p->filter_mode == MCAST_INCLUDE) ||
			    (p->flags & MDB_PG_FLAGS_BLOCKED))
				goto delivered;
		} else {
			port = rport;
		}

		fwd.port = port;
		err = maybe_deliver(&prev, &fwd, skb, local_orig);
		if (err)
			break;

delivered:
		if ((unsigned long)lport >= (unsigned long)port)
			p = rcu_dereference(p->next);
		if ((unsigned long)rport >= (unsigned long)port)
			rp = rcu_dereference(hlist_next_rcu(rp));
	}

	br_flood_finish(&prev, err, skb, local_rcv, local_orig);
}
#endif
