// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * xfrm_device.c - IPsec device offloading code.
 *
 * Copyright (c) 2015 secunet Security Networks AG
 *
 * Author:
 * Steffen Klassert <steffen.klassert@secunet.com>
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <net/dst.h>
#include <net/gso.h>
#include <net/xfrm.h>
#include <linux/notifier.h>

#ifdef CONFIG_XFRM_OFFLOAD
static void __xfrm_transport_prep(struct xfrm_state *x, struct sk_buff *skb,
				  unsigned int hsize)
{
	struct xfrm_offload *xo = xfrm_offload(skb);

	skb_reset_mac_len(skb);
	if (xo->flags & XFRM_GSO_SEGMENT)
		skb->transport_header -= x->props.header_len;

	pskb_pull(skb, skb_transport_offset(skb) + x->props.header_len);
}

static void __xfrm_mode_tunnel_prep(struct xfrm_state *x, struct sk_buff *skb,
				    unsigned int hsize)

{
	struct xfrm_offload *xo = xfrm_offload(skb);

	if (xo->flags & XFRM_GSO_SEGMENT)
		skb->transport_header = skb->network_header + hsize;

	skb_reset_mac_len(skb);
	pskb_pull(skb,
		  skb->mac_len + x->props.header_len - x->props.enc_hdr_len);
}

static void __xfrm_mode_beet_prep(struct xfrm_state *x, struct sk_buff *skb,
				  unsigned int hsize)
{
	struct xfrm_offload *xo = xfrm_offload(skb);
	int phlen = 0;

	if (xo->flags & XFRM_GSO_SEGMENT)
		skb->transport_header = skb->network_header + hsize;

	skb_reset_mac_len(skb);
	if (x->sel.family != AF_INET6) {
		phlen = IPV4_BEET_PHMAXLEN;
		if (x->outer_mode.family == AF_INET6)
			phlen += sizeof(struct ipv6hdr) - sizeof(struct iphdr);
	}

	pskb_pull(skb, skb->mac_len + hsize + (x->props.header_len - phlen));
}

/* Adjust pointers into the packet when IPsec is done at layer2 */
static void xfrm_outer_mode_prep(struct xfrm_state *x, struct sk_buff *skb)
{
	switch (x->outer_mode.encap) {
	case XFRM_MODE_IPTFS:
	case XFRM_MODE_TUNNEL:
		if (x->outer_mode.family == AF_INET)
			return __xfrm_mode_tunnel_prep(x, skb,
						       sizeof(struct iphdr));
		if (x->outer_mode.family == AF_INET6)
			return __xfrm_mode_tunnel_prep(x, skb,
						       sizeof(struct ipv6hdr));
		break;
	case XFRM_MODE_TRANSPORT:
		if (x->outer_mode.family == AF_INET)
			return __xfrm_transport_prep(x, skb,
						     sizeof(struct iphdr));
		if (x->outer_mode.family == AF_INET6)
			return __xfrm_transport_prep(x, skb,
						     sizeof(struct ipv6hdr));
		break;
	case XFRM_MODE_BEET:
		if (x->outer_mode.family == AF_INET)
			return __xfrm_mode_beet_prep(x, skb,
						     sizeof(struct iphdr));
		if (x->outer_mode.family == AF_INET6)
			return __xfrm_mode_beet_prep(x, skb,
						     sizeof(struct ipv6hdr));
		break;
	case XFRM_MODE_ROUTEOPTIMIZATION:
	case XFRM_MODE_IN_TRIGGER:
		break;
	}
}

static inline bool xmit_xfrm_check_overflow(struct sk_buff *skb)
{
	struct xfrm_offload *xo = xfrm_offload(skb);
	__u32 seq = xo->seq.low;

	seq += skb_shinfo(skb)->gso_segs;
	if (unlikely(seq < xo->seq.low))
		return true;

	return false;
}

struct sk_buff *validate_xmit_xfrm(struct sk_buff *skb, netdev_features_t features, bool *again)
{
	int err;
	unsigned long flags;
	struct xfrm_state *x;
	struct softnet_data *sd;
	struct sk_buff *skb2, *nskb, *pskb = NULL;
	netdev_features_t esp_features = features;
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct net_device *dev = skb->dev;
	struct sec_path *sp;

	if (!xo || (xo->flags & XFRM_XMIT))
		return skb;

	if (!(features & NETIF_F_HW_ESP))
		esp_features = features & ~(NETIF_F_SG | NETIF_F_CSUM_MASK);

	sp = skb_sec_path(skb);
	x = sp->xvec[sp->len - 1];
	if (xo->flags & XFRM_GRO || x->xso.dir == XFRM_DEV_OFFLOAD_IN)
		return skb;

	/* The packet was sent to HW IPsec packet offload engine,
	 * but to wrong device. Drop the packet, so it won't skip
	 * XFRM stack.
	 */
	if (x->xso.type == XFRM_DEV_OFFLOAD_PACKET && x->xso.dev != dev) {
		kfree_skb(skb);
		dev_core_stats_tx_dropped_inc(dev);
		return NULL;
	}

	local_irq_save(flags);
	sd = this_cpu_ptr(&softnet_data);
	err = !skb_queue_empty(&sd->xfrm_backlog);
	local_irq_restore(flags);

	if (err) {
		*again = true;
		return skb;
	}

	if (skb_is_gso(skb) && (unlikely(x->xso.dev != dev) ||
				unlikely(xmit_xfrm_check_overflow(skb)))) {
		struct sk_buff *segs;

		/* Packet got rerouted, fixup features and segment it. */
		esp_features = esp_features & ~(NETIF_F_HW_ESP | NETIF_F_GSO_ESP);

		segs = skb_gso_segment(skb, esp_features);
		if (IS_ERR(segs)) {
			kfree_skb(skb);
			dev_core_stats_tx_dropped_inc(dev);
			return NULL;
		} else {
			consume_skb(skb);
			skb = segs;
		}
	}

	if (!skb->next) {
		esp_features |= skb->dev->gso_partial_features;
		xfrm_outer_mode_prep(x, skb);

		xo->flags |= XFRM_DEV_RESUME;

		err = x->type_offload->xmit(x, skb, esp_features);
		if (err) {
			if (err == -EINPROGRESS)
				return NULL;

			XFRM_INC_STATS(xs_net(x), LINUX_MIB_XFRMOUTSTATEPROTOERROR);
			kfree_skb(skb);
			return NULL;
		}

		skb_push(skb, skb->data - skb_mac_header(skb));

		return skb;
	}

	skb_list_walk_safe(skb, skb2, nskb) {
		esp_features |= skb->dev->gso_partial_features;
		skb_mark_not_on_list(skb2);

		xo = xfrm_offload(skb2);
		xo->flags |= XFRM_DEV_RESUME;

		xfrm_outer_mode_prep(x, skb2);

		err = x->type_offload->xmit(x, skb2, esp_features);
		if (!err) {
			skb2->next = nskb;
		} else if (err != -EINPROGRESS) {
			XFRM_INC_STATS(xs_net(x), LINUX_MIB_XFRMOUTSTATEPROTOERROR);
			skb2->next = nskb;
			kfree_skb_list(skb2);
			return NULL;
		} else {
			if (skb == skb2)
				skb = nskb;
			else
				pskb->next = nskb;

			continue;
		}

		skb_push(skb2, skb2->data - skb_mac_header(skb2));
		pskb = skb2;
	}

	return skb;
}
EXPORT_SYMBOL_GPL(validate_xmit_xfrm);

int xfrm_dev_state_add(struct net *net, struct xfrm_state *x,
		       struct xfrm_user_offload *xuo,
		       struct netlink_ext_ack *extack)
{
	int err;
	struct dst_entry *dst;
	struct net_device *dev;
	struct xfrm_dev_offload *xso = &x->xso;
	xfrm_address_t *saddr;
	xfrm_address_t *daddr;
	bool is_packet_offload;

	if (xuo->flags &
	    ~(XFRM_OFFLOAD_IPV6 | XFRM_OFFLOAD_INBOUND | XFRM_OFFLOAD_PACKET)) {
		NL_SET_ERR_MSG(extack, "Unrecognized flags in offload request");
		return -EINVAL;
	}

	if ((xuo->flags & XFRM_OFFLOAD_INBOUND && x->dir == XFRM_SA_DIR_OUT) ||
	    (!(xuo->flags & XFRM_OFFLOAD_INBOUND) && x->dir == XFRM_SA_DIR_IN)) {
		NL_SET_ERR_MSG(extack, "Mismatched SA and offload direction");
		return -EINVAL;
	}

	if (xuo->flags & XFRM_OFFLOAD_INBOUND && x->if_id) {
		NL_SET_ERR_MSG(extack, "XFRM if_id is not supported in RX path");
		return -EINVAL;
	}

	is_packet_offload = xuo->flags & XFRM_OFFLOAD_PACKET;

	/* We don't yet support TFC padding. */
	if (x->tfcpad) {
		NL_SET_ERR_MSG(extack, "TFC padding can't be offloaded");
		return -EINVAL;
	}

	dev = dev_get_by_index(net, xuo->ifindex);
	if (!dev) {
		struct xfrm_dst_lookup_params params;

		if (!(xuo->flags & XFRM_OFFLOAD_INBOUND)) {
			saddr = &x->props.saddr;
			daddr = &x->id.daddr;
		} else {
			saddr = &x->id.daddr;
			daddr = &x->props.saddr;
		}

		memset(&params, 0, sizeof(params));
		params.net = net;
		params.saddr = saddr;
		params.daddr = daddr;
		params.mark = xfrm_smark_get(0, x);
		dst = __xfrm_dst_lookup(x->props.family, &params);
		if (IS_ERR(dst))
			return (is_packet_offload) ? -EINVAL : 0;

		dev = dst->dev;

		dev_hold(dev);
		dst_release(dst);
	}

	if (!dev->xfrmdev_ops || !dev->xfrmdev_ops->xdo_dev_state_add) {
		xso->dev = NULL;
		dev_put(dev);
		return (is_packet_offload) ? -EINVAL : 0;
	}

	if (!is_packet_offload && x->props.flags & XFRM_STATE_ESN &&
	    !dev->xfrmdev_ops->xdo_dev_state_advance_esn) {
		NL_SET_ERR_MSG(extack, "Device doesn't support offload with ESN");
		xso->dev = NULL;
		dev_put(dev);
		return -EINVAL;
	}

	if (!x->type_offload) {
		NL_SET_ERR_MSG(extack, "Type doesn't support offload");
		dev_put(dev);
		return -EINVAL;
	}

	xso->dev = dev;
	netdev_tracker_alloc(dev, &xso->dev_tracker, GFP_ATOMIC);

	if (xuo->flags & XFRM_OFFLOAD_INBOUND)
		xso->dir = XFRM_DEV_OFFLOAD_IN;
	else
		xso->dir = XFRM_DEV_OFFLOAD_OUT;

	if (is_packet_offload)
		xso->type = XFRM_DEV_OFFLOAD_PACKET;
	else
		xso->type = XFRM_DEV_OFFLOAD_CRYPTO;

	err = dev->xfrmdev_ops->xdo_dev_state_add(dev, x, extack);
	if (err) {
		xso->dev = NULL;
		xso->dir = 0;
		netdev_put(dev, &xso->dev_tracker);
		xso->type = XFRM_DEV_OFFLOAD_UNSPECIFIED;

		xfrm_unset_type_offload(x);
		/* User explicitly requested packet offload mode and configured
		 * policy in addition to the XFRM state. So be civil to users,
		 * and return an error instead of taking fallback path.
		 */
		if ((err != -EOPNOTSUPP && !is_packet_offload) || is_packet_offload) {
			NL_SET_ERR_MSG_WEAK(extack, "Device failed to offload this state");
			return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xfrm_dev_state_add);

int xfrm_dev_policy_add(struct net *net, struct xfrm_policy *xp,
			struct xfrm_user_offload *xuo, u8 dir,
			struct netlink_ext_ack *extack)
{
	struct xfrm_dev_offload *xdo = &xp->xdo;
	struct net_device *dev;
	int err;

	if (!xuo->flags || xuo->flags & ~XFRM_OFFLOAD_PACKET) {
		/* We support only packet offload mode and it means
		 * that user must set XFRM_OFFLOAD_PACKET bit.
		 */
		NL_SET_ERR_MSG(extack, "Unrecognized flags in offload request");
		return -EINVAL;
	}

	dev = dev_get_by_index(net, xuo->ifindex);
	if (!dev)
		return -EINVAL;

	if (!dev->xfrmdev_ops || !dev->xfrmdev_ops->xdo_dev_policy_add) {
		xdo->dev = NULL;
		dev_put(dev);
		NL_SET_ERR_MSG(extack, "Policy offload is not supported");
		return -EINVAL;
	}

	xdo->dev = dev;
	netdev_tracker_alloc(dev, &xdo->dev_tracker, GFP_ATOMIC);
	xdo->type = XFRM_DEV_OFFLOAD_PACKET;
	switch (dir) {
	case XFRM_POLICY_IN:
		xdo->dir = XFRM_DEV_OFFLOAD_IN;
		break;
	case XFRM_POLICY_OUT:
		xdo->dir = XFRM_DEV_OFFLOAD_OUT;
		break;
	case XFRM_POLICY_FWD:
		xdo->dir = XFRM_DEV_OFFLOAD_FWD;
		break;
	default:
		xdo->dev = NULL;
		netdev_put(dev, &xdo->dev_tracker);
		NL_SET_ERR_MSG(extack, "Unrecognized offload direction");
		return -EINVAL;
	}

	err = dev->xfrmdev_ops->xdo_dev_policy_add(xp, extack);
	if (err) {
		xdo->dev = NULL;
		xdo->type = XFRM_DEV_OFFLOAD_UNSPECIFIED;
		xdo->dir = 0;
		netdev_put(dev, &xdo->dev_tracker);
		NL_SET_ERR_MSG_WEAK(extack, "Device failed to offload this policy");
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xfrm_dev_policy_add);

bool xfrm_dev_offload_ok(struct sk_buff *skb, struct xfrm_state *x)
{
	int mtu;
	struct dst_entry *dst = skb_dst(skb);
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	struct net_device *dev = x->xso.dev;
	bool check_tunnel_size;

	if (!x->type_offload ||
	    (x->xso.type == XFRM_DEV_OFFLOAD_UNSPECIFIED && x->encap))
		return false;

	if ((!dev || dev == xfrm_dst_path(dst)->dev) &&
	    !xdst->child->xfrm) {
		mtu = xfrm_state_mtu(x, xdst->child_mtu_cached);
		if (skb->len <= mtu)
			goto ok;

		if (skb_is_gso(skb) && skb_gso_validate_network_len(skb, mtu))
			goto ok;
	}

	return false;

ok:
	if (!dev)
		return true;

	check_tunnel_size = x->xso.type == XFRM_DEV_OFFLOAD_PACKET &&
			    x->props.mode == XFRM_MODE_TUNNEL;
	switch (skb_dst(skb)->ops->family) {
	case AF_INET:
		/* Check for IPv4 options */
		if (ip_hdr(skb)->ihl != 5)
			return false;
		if (check_tunnel_size && xfrm4_tunnel_check_size(skb))
			return false;
		break;
	case AF_INET6:
		/* Check for IPv6 extensions */
		if (ipv6_ext_hdr(ipv6_hdr(skb)->nexthdr))
			return false;
		if (check_tunnel_size && xfrm6_tunnel_check_size(skb))
			return false;
		break;
	default:
		break;
	}

	if (dev->xfrmdev_ops->xdo_dev_offload_ok)
		return dev->xfrmdev_ops->xdo_dev_offload_ok(skb, x);

	return true;
}
EXPORT_SYMBOL_GPL(xfrm_dev_offload_ok);

void xfrm_dev_resume(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	int ret = NETDEV_TX_BUSY;
	struct netdev_queue *txq;
	struct softnet_data *sd;
	unsigned long flags;

	rcu_read_lock();
	txq = netdev_core_pick_tx(dev, skb, NULL);

	HARD_TX_LOCK(dev, txq, smp_processor_id());
	if (!netif_xmit_frozen_or_stopped(txq))
		skb = dev_hard_start_xmit(skb, dev, txq, &ret);
	HARD_TX_UNLOCK(dev, txq);

	if (!dev_xmit_complete(ret)) {
		local_irq_save(flags);
		sd = this_cpu_ptr(&softnet_data);
		skb_queue_tail(&sd->xfrm_backlog, skb);
		raise_softirq_irqoff(NET_TX_SOFTIRQ);
		local_irq_restore(flags);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(xfrm_dev_resume);

void xfrm_dev_backlog(struct softnet_data *sd)
{
	struct sk_buff_head *xfrm_backlog = &sd->xfrm_backlog;
	struct sk_buff_head list;
	struct sk_buff *skb;

	if (skb_queue_empty(xfrm_backlog))
		return;

	__skb_queue_head_init(&list);

	spin_lock(&xfrm_backlog->lock);
	skb_queue_splice_init(xfrm_backlog, &list);
	spin_unlock(&xfrm_backlog->lock);

	while (!skb_queue_empty(&list)) {
		skb = __skb_dequeue(&list);
		xfrm_dev_resume(skb);
	}

}
#endif

static int xfrm_api_check(struct net_device *dev)
{
#ifdef CONFIG_XFRM_OFFLOAD
	if ((dev->features & NETIF_F_HW_ESP_TX_CSUM) &&
	    !(dev->features & NETIF_F_HW_ESP))
		return NOTIFY_BAD;

	if ((dev->features & NETIF_F_HW_ESP) &&
	    (!(dev->xfrmdev_ops &&
	       dev->xfrmdev_ops->xdo_dev_state_add &&
	       dev->xfrmdev_ops->xdo_dev_state_delete)))
		return NOTIFY_BAD;
#else
	if (dev->features & (NETIF_F_HW_ESP | NETIF_F_HW_ESP_TX_CSUM))
		return NOTIFY_BAD;
#endif

	return NOTIFY_DONE;
}

static int xfrm_dev_down(struct net_device *dev)
{
	if (dev->features & NETIF_F_HW_ESP) {
		xfrm_dev_state_flush(dev_net(dev), dev, true);
		xfrm_dev_policy_flush(dev_net(dev), dev, true);
	}

	return NOTIFY_DONE;
}

static int xfrm_dev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_REGISTER:
		return xfrm_api_check(dev);

	case NETDEV_FEAT_CHANGE:
		return xfrm_api_check(dev);

	case NETDEV_DOWN:
	case NETDEV_UNREGISTER:
		return xfrm_dev_down(dev);
	}
	return NOTIFY_DONE;
}

static struct notifier_block xfrm_dev_notifier = {
	.notifier_call	= xfrm_dev_event,
};

void __init xfrm_dev_init(void)
{
	register_netdevice_notifier(&xfrm_dev_notifier);
}
