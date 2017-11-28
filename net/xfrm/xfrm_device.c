/*
 * xfrm_device.c - IPsec device offloading code.
 *
 * Copyright (c) 2015 secunet Security Networks AG
 *
 * Author:
 * Steffen Klassert <steffen.klassert@secunet.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <net/dst.h>
#include <net/xfrm.h>
#include <linux/notifier.h>

#ifdef CONFIG_XFRM_OFFLOAD
int validate_xmit_xfrm(struct sk_buff *skb, netdev_features_t features)
{
	int err;
	struct xfrm_state *x;
	struct xfrm_offload *xo = xfrm_offload(skb);

	if (skb_is_gso(skb))
		return 0;

	if (xo) {
		x = skb->sp->xvec[skb->sp->len - 1];
		if (xo->flags & XFRM_GRO || x->xso.flags & XFRM_OFFLOAD_INBOUND)
			return 0;

		x->outer_mode->xmit(x, skb);

		err = x->type_offload->xmit(x, skb, features);
		if (err) {
			XFRM_INC_STATS(xs_net(x), LINUX_MIB_XFRMOUTSTATEPROTOERROR);
			return err;
		}

		skb_push(skb, skb->data - skb_mac_header(skb));
	}

	return 0;
}
EXPORT_SYMBOL_GPL(validate_xmit_xfrm);

int xfrm_dev_state_add(struct net *net, struct xfrm_state *x,
		       struct xfrm_user_offload *xuo)
{
	int err;
	struct dst_entry *dst;
	struct net_device *dev;
	struct xfrm_state_offload *xso = &x->xso;
	xfrm_address_t *saddr;
	xfrm_address_t *daddr;

	if (!x->type_offload)
		return -EINVAL;

	/* We don't yet support UDP encapsulation, TFC padding and ESN. */
	if (x->encap || x->tfcpad || (x->props.flags & XFRM_STATE_ESN))
		return 0;

	dev = dev_get_by_index(net, xuo->ifindex);
	if (!dev) {
		if (!(xuo->flags & XFRM_OFFLOAD_INBOUND)) {
			saddr = &x->props.saddr;
			daddr = &x->id.daddr;
		} else {
			saddr = &x->id.daddr;
			daddr = &x->props.saddr;
		}

		dst = __xfrm_dst_lookup(net, 0, 0, saddr, daddr,
					x->props.family, x->props.output_mark);
		if (IS_ERR(dst))
			return 0;

		dev = dst->dev;

		dev_hold(dev);
		dst_release(dst);
	}

	if (!dev->xfrmdev_ops || !dev->xfrmdev_ops->xdo_dev_state_add) {
		xso->dev = NULL;
		dev_put(dev);
		return 0;
	}

	xso->dev = dev;
	xso->num_exthdrs = 1;
	xso->flags = xuo->flags;

	err = dev->xfrmdev_ops->xdo_dev_state_add(x);
	if (err) {
		dev_put(dev);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xfrm_dev_state_add);

bool xfrm_dev_offload_ok(struct sk_buff *skb, struct xfrm_state *x)
{
	int mtu;
	struct dst_entry *dst = skb_dst(skb);
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	struct net_device *dev = x->xso.dev;

	if (!x->type_offload || x->encap)
		return false;

	if ((x->xso.offload_handle && (dev == dst->path->dev)) &&
	     !xdst->child->xfrm && x->type->get_mtu) {
		mtu = x->type->get_mtu(x, xdst->child_mtu_cached);

		if (skb->len <= mtu)
			goto ok;

		if (skb_is_gso(skb) && skb_gso_validate_mtu(skb, mtu))
			goto ok;
	}

	return false;

ok:
	if (dev && dev->xfrmdev_ops && dev->xfrmdev_ops->xdo_dev_offload_ok)
		return x->xso.dev->xfrmdev_ops->xdo_dev_offload_ok(skb, x);

	return true;
}
EXPORT_SYMBOL_GPL(xfrm_dev_offload_ok);
#endif

static int xfrm_dev_register(struct net_device *dev)
{
	if ((dev->features & NETIF_F_HW_ESP) && !dev->xfrmdev_ops)
		return NOTIFY_BAD;
	if ((dev->features & NETIF_F_HW_ESP_TX_CSUM) &&
	    !(dev->features & NETIF_F_HW_ESP))
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

static int xfrm_dev_unregister(struct net_device *dev)
{
	xfrm_policy_cache_flush();
	return NOTIFY_DONE;
}

static int xfrm_dev_feat_change(struct net_device *dev)
{
	if ((dev->features & NETIF_F_HW_ESP) && !dev->xfrmdev_ops)
		return NOTIFY_BAD;
	else if (!(dev->features & NETIF_F_HW_ESP))
		dev->xfrmdev_ops = NULL;

	if ((dev->features & NETIF_F_HW_ESP_TX_CSUM) &&
	    !(dev->features & NETIF_F_HW_ESP))
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

static int xfrm_dev_down(struct net_device *dev)
{
	if (dev->features & NETIF_F_HW_ESP)
		xfrm_dev_state_flush(dev_net(dev), dev, true);

	xfrm_policy_cache_flush();
	return NOTIFY_DONE;
}

static int xfrm_dev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_REGISTER:
		return xfrm_dev_register(dev);

	case NETDEV_UNREGISTER:
		return xfrm_dev_unregister(dev);

	case NETDEV_FEAT_CHANGE:
		return xfrm_dev_feat_change(dev);

	case NETDEV_DOWN:
		return xfrm_dev_down(dev);
	}
	return NOTIFY_DONE;
}

static struct notifier_block xfrm_dev_notifier = {
	.notifier_call	= xfrm_dev_event,
};

void __net_init xfrm_dev_init(void)
{
	register_netdevice_notifier(&xfrm_dev_notifier);
}
