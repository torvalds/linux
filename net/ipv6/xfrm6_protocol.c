// SPDX-License-Identifier: GPL-2.0-or-later
/* xfrm6_protocol.c - Generic xfrm protocol multiplexer for ipv6.
 *
 * Copyright (C) 2013 secunet Security Networks AG
 *
 * Author:
 * Steffen Klassert <steffen.klassert@secunet.com>
 *
 * Based on:
 * net/ipv4/xfrm4_protocol.c
 */

#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/skbuff.h>
#include <linux/icmpv6.h>
#include <net/ip6_route.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/xfrm.h>

static struct xfrm6_protocol __rcu *esp6_handlers __read_mostly;
static struct xfrm6_protocol __rcu *ah6_handlers __read_mostly;
static struct xfrm6_protocol __rcu *ipcomp6_handlers __read_mostly;
static DEFINE_MUTEX(xfrm6_protocol_mutex);

static inline struct xfrm6_protocol __rcu **proto_handlers(u8 protocol)
{
	switch (protocol) {
	case IPPROTO_ESP:
		return &esp6_handlers;
	case IPPROTO_AH:
		return &ah6_handlers;
	case IPPROTO_COMP:
		return &ipcomp6_handlers;
	}

	return NULL;
}

#define for_each_protocol_rcu(head, handler)		\
	for (handler = rcu_dereference(head);		\
	     handler != NULL;				\
	     handler = rcu_dereference(handler->next))	\

static int xfrm6_rcv_cb(struct sk_buff *skb, u8 protocol, int err)
{
	int ret;
	struct xfrm6_protocol *handler;
	struct xfrm6_protocol __rcu **head = proto_handlers(protocol);

	if (!head)
		return 0;

	for_each_protocol_rcu(*proto_handlers(protocol), handler)
		if ((ret = handler->cb_handler(skb, err)) <= 0)
			return ret;

	return 0;
}

int xfrm6_rcv_encap(struct sk_buff *skb, int nexthdr, __be32 spi,
		    int encap_type)
{
	int ret;
	struct xfrm6_protocol *handler;
	struct xfrm6_protocol __rcu **head = proto_handlers(nexthdr);

	XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip6 = NULL;
	XFRM_SPI_SKB_CB(skb)->family = AF_INET6;
	XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct ipv6hdr, daddr);

	if (!head)
		goto out;

	if (!skb_dst(skb)) {
		const struct ipv6hdr *ip6h = ipv6_hdr(skb);
		int flags = RT6_LOOKUP_F_HAS_SADDR;
		struct dst_entry *dst;
		struct flowi6 fl6 = {
			.flowi6_iif   = skb->dev->ifindex,
			.daddr        = ip6h->daddr,
			.saddr        = ip6h->saddr,
			.flowlabel    = ip6_flowinfo(ip6h),
			.flowi6_mark  = skb->mark,
			.flowi6_proto = ip6h->nexthdr,
		};

		dst = ip6_route_input_lookup(dev_net(skb->dev), skb->dev, &fl6,
					     skb, flags);
		if (dst->error)
			goto drop;
		skb_dst_set(skb, dst);
	}

	for_each_protocol_rcu(*head, handler)
		if ((ret = handler->input_handler(skb, nexthdr, spi, encap_type)) != -EINVAL)
			return ret;

out:
	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0);

drop:
	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL(xfrm6_rcv_encap);

static int xfrm6_esp_rcv(struct sk_buff *skb)
{
	int ret;
	struct xfrm6_protocol *handler;

	XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip6 = NULL;

	for_each_protocol_rcu(esp6_handlers, handler)
		if ((ret = handler->handler(skb)) != -EINVAL)
			return ret;

	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0);

	kfree_skb(skb);
	return 0;
}

static int xfrm6_esp_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			  u8 type, u8 code, int offset, __be32 info)
{
	struct xfrm6_protocol *handler;

	for_each_protocol_rcu(esp6_handlers, handler)
		if (!handler->err_handler(skb, opt, type, code, offset, info))
			return 0;

	return -ENOENT;
}

static int xfrm6_ah_rcv(struct sk_buff *skb)
{
	int ret;
	struct xfrm6_protocol *handler;

	XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip6 = NULL;

	for_each_protocol_rcu(ah6_handlers, handler)
		if ((ret = handler->handler(skb)) != -EINVAL)
			return ret;

	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0);

	kfree_skb(skb);
	return 0;
}

static int xfrm6_ah_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			 u8 type, u8 code, int offset, __be32 info)
{
	struct xfrm6_protocol *handler;

	for_each_protocol_rcu(ah6_handlers, handler)
		if (!handler->err_handler(skb, opt, type, code, offset, info))
			return 0;

	return -ENOENT;
}

static int xfrm6_ipcomp_rcv(struct sk_buff *skb)
{
	int ret;
	struct xfrm6_protocol *handler;

	XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip6 = NULL;

	for_each_protocol_rcu(ipcomp6_handlers, handler)
		if ((ret = handler->handler(skb)) != -EINVAL)
			return ret;

	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0);

	kfree_skb(skb);
	return 0;
}

static int xfrm6_ipcomp_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			     u8 type, u8 code, int offset, __be32 info)
{
	struct xfrm6_protocol *handler;

	for_each_protocol_rcu(ipcomp6_handlers, handler)
		if (!handler->err_handler(skb, opt, type, code, offset, info))
			return 0;

	return -ENOENT;
}

static const struct inet6_protocol esp6_protocol = {
	.handler	=	xfrm6_esp_rcv,
	.err_handler	=	xfrm6_esp_err,
	.flags		=	INET6_PROTO_NOPOLICY,
};

static const struct inet6_protocol ah6_protocol = {
	.handler	=	xfrm6_ah_rcv,
	.err_handler	=	xfrm6_ah_err,
	.flags		=	INET6_PROTO_NOPOLICY,
};

static const struct inet6_protocol ipcomp6_protocol = {
	.handler	=	xfrm6_ipcomp_rcv,
	.err_handler	=	xfrm6_ipcomp_err,
	.flags		=	INET6_PROTO_NOPOLICY,
};

static const struct xfrm_input_afinfo xfrm6_input_afinfo = {
	.family		=	AF_INET6,
	.callback	=	xfrm6_rcv_cb,
};

static inline const struct inet6_protocol *netproto(unsigned char protocol)
{
	switch (protocol) {
	case IPPROTO_ESP:
		return &esp6_protocol;
	case IPPROTO_AH:
		return &ah6_protocol;
	case IPPROTO_COMP:
		return &ipcomp6_protocol;
	}

	return NULL;
}

int xfrm6_protocol_register(struct xfrm6_protocol *handler,
			    unsigned char protocol)
{
	struct xfrm6_protocol __rcu **pprev;
	struct xfrm6_protocol *t;
	bool add_netproto = false;
	int ret = -EEXIST;
	int priority = handler->priority;

	if (!proto_handlers(protocol) || !netproto(protocol))
		return -EINVAL;

	mutex_lock(&xfrm6_protocol_mutex);

	if (!rcu_dereference_protected(*proto_handlers(protocol),
				       lockdep_is_held(&xfrm6_protocol_mutex)))
		add_netproto = true;

	for (pprev = proto_handlers(protocol);
	     (t = rcu_dereference_protected(*pprev,
			lockdep_is_held(&xfrm6_protocol_mutex))) != NULL;
	     pprev = &t->next) {
		if (t->priority < priority)
			break;
		if (t->priority == priority)
			goto err;
	}

	handler->next = *pprev;
	rcu_assign_pointer(*pprev, handler);

	ret = 0;

err:
	mutex_unlock(&xfrm6_protocol_mutex);

	if (add_netproto) {
		if (inet6_add_protocol(netproto(protocol), protocol)) {
			pr_err("%s: can't add protocol\n", __func__);
			ret = -EAGAIN;
		}
	}

	return ret;
}
EXPORT_SYMBOL(xfrm6_protocol_register);

int xfrm6_protocol_deregister(struct xfrm6_protocol *handler,
			      unsigned char protocol)
{
	struct xfrm6_protocol __rcu **pprev;
	struct xfrm6_protocol *t;
	int ret = -ENOENT;

	if (!proto_handlers(protocol) || !netproto(protocol))
		return -EINVAL;

	mutex_lock(&xfrm6_protocol_mutex);

	for (pprev = proto_handlers(protocol);
	     (t = rcu_dereference_protected(*pprev,
			lockdep_is_held(&xfrm6_protocol_mutex))) != NULL;
	     pprev = &t->next) {
		if (t == handler) {
			*pprev = handler->next;
			ret = 0;
			break;
		}
	}

	if (!rcu_dereference_protected(*proto_handlers(protocol),
				       lockdep_is_held(&xfrm6_protocol_mutex))) {
		if (inet6_del_protocol(netproto(protocol), protocol) < 0) {
			pr_err("%s: can't remove protocol\n", __func__);
			ret = -EAGAIN;
		}
	}

	mutex_unlock(&xfrm6_protocol_mutex);

	synchronize_net();

	return ret;
}
EXPORT_SYMBOL(xfrm6_protocol_deregister);

int __init xfrm6_protocol_init(void)
{
	return xfrm_input_register_afinfo(&xfrm6_input_afinfo);
}

void xfrm6_protocol_fini(void)
{
	xfrm_input_unregister_afinfo(&xfrm6_input_afinfo);
}
