// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)2003,2004 USAGI/WIDE Project
 *
 * Authors	Mitsuru KANDA  <mk@linux-ipv6.org>
 *		YOSHIFUJI Hideaki <yoshfuji@linux-ipv6.org>
 */

#define pr_fmt(fmt) "IPv6: " fmt

#include <linux/icmpv6.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/xfrm.h>

static struct xfrm6_tunnel __rcu *tunnel6_handlers __read_mostly;
static struct xfrm6_tunnel __rcu *tunnel46_handlers __read_mostly;
static struct xfrm6_tunnel __rcu *tunnelmpls6_handlers __read_mostly;
static DEFINE_MUTEX(tunnel6_mutex);

static inline int xfrm6_tunnel_mpls_supported(void)
{
	return IS_ENABLED(CONFIG_MPLS);
}

int xfrm6_tunnel_register(struct xfrm6_tunnel *handler, unsigned short family)
{
	struct xfrm6_tunnel __rcu **pprev;
	struct xfrm6_tunnel *t;
	int ret = -EEXIST;
	int priority = handler->priority;

	mutex_lock(&tunnel6_mutex);

	switch (family) {
	case AF_INET6:
		pprev = &tunnel6_handlers;
		break;
	case AF_INET:
		pprev = &tunnel46_handlers;
		break;
	case AF_MPLS:
		pprev = &tunnelmpls6_handlers;
		break;
	default:
		goto err;
	}

	for (; (t = rcu_dereference_protected(*pprev,
			lockdep_is_held(&tunnel6_mutex))) != NULL;
	     pprev = &t->next) {
		if (t->priority > priority)
			break;
		if (t->priority == priority)
			goto err;
	}

	handler->next = *pprev;
	rcu_assign_pointer(*pprev, handler);

	ret = 0;

err:
	mutex_unlock(&tunnel6_mutex);

	return ret;
}
EXPORT_SYMBOL(xfrm6_tunnel_register);

int xfrm6_tunnel_deregister(struct xfrm6_tunnel *handler, unsigned short family)
{
	struct xfrm6_tunnel __rcu **pprev;
	struct xfrm6_tunnel *t;
	int ret = -ENOENT;

	mutex_lock(&tunnel6_mutex);

	switch (family) {
	case AF_INET6:
		pprev = &tunnel6_handlers;
		break;
	case AF_INET:
		pprev = &tunnel46_handlers;
		break;
	case AF_MPLS:
		pprev = &tunnelmpls6_handlers;
		break;
	default:
		goto err;
	}

	for (; (t = rcu_dereference_protected(*pprev,
			lockdep_is_held(&tunnel6_mutex))) != NULL;
	     pprev = &t->next) {
		if (t == handler) {
			*pprev = handler->next;
			ret = 0;
			break;
		}
	}

err:
	mutex_unlock(&tunnel6_mutex);

	synchronize_net();

	return ret;
}
EXPORT_SYMBOL(xfrm6_tunnel_deregister);

#define for_each_tunnel_rcu(head, handler)		\
	for (handler = rcu_dereference(head);		\
	     handler != NULL;				\
	     handler = rcu_dereference(handler->next))	\

static int tunnelmpls6_rcv(struct sk_buff *skb)
{
	struct xfrm6_tunnel *handler;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto drop;

	for_each_tunnel_rcu(tunnelmpls6_handlers, handler)
		if (!handler->handler(skb))
			return 0;

	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0);

drop:
	kfree_skb(skb);
	return 0;
}

static int tunnel6_rcv(struct sk_buff *skb)
{
	struct xfrm6_tunnel *handler;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto drop;

	for_each_tunnel_rcu(tunnel6_handlers, handler)
		if (!handler->handler(skb))
			return 0;

	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0);

drop:
	kfree_skb(skb);
	return 0;
}

#if IS_ENABLED(CONFIG_INET6_XFRM_TUNNEL)
static int tunnel6_rcv_cb(struct sk_buff *skb, u8 proto, int err)
{
	struct xfrm6_tunnel __rcu *head;
	struct xfrm6_tunnel *handler;
	int ret;

	head = (proto == IPPROTO_IPV6) ? tunnel6_handlers : tunnel46_handlers;

	for_each_tunnel_rcu(head, handler) {
		if (handler->cb_handler) {
			ret = handler->cb_handler(skb, err);
			if (ret <= 0)
				return ret;
		}
	}

	return 0;
}

static const struct xfrm_input_afinfo tunnel6_input_afinfo = {
	.family		=	AF_INET6,
	.is_ipip	=	true,
	.callback	=	tunnel6_rcv_cb,
};
#endif

static int tunnel46_rcv(struct sk_buff *skb)
{
	struct xfrm6_tunnel *handler;

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto drop;

	for_each_tunnel_rcu(tunnel46_handlers, handler)
		if (!handler->handler(skb))
			return 0;

	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0);

drop:
	kfree_skb(skb);
	return 0;
}

static int tunnel6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			u8 type, u8 code, int offset, __be32 info)
{
	struct xfrm6_tunnel *handler;

	for_each_tunnel_rcu(tunnel6_handlers, handler)
		if (!handler->err_handler(skb, opt, type, code, offset, info))
			return 0;

	return -ENOENT;
}

static int tunnel46_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			 u8 type, u8 code, int offset, __be32 info)
{
	struct xfrm6_tunnel *handler;

	for_each_tunnel_rcu(tunnel46_handlers, handler)
		if (!handler->err_handler(skb, opt, type, code, offset, info))
			return 0;

	return -ENOENT;
}

static int tunnelmpls6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			   u8 type, u8 code, int offset, __be32 info)
{
	struct xfrm6_tunnel *handler;

	for_each_tunnel_rcu(tunnelmpls6_handlers, handler)
		if (!handler->err_handler(skb, opt, type, code, offset, info))
			return 0;

	return -ENOENT;
}

static const struct inet6_protocol tunnel6_protocol = {
	.handler	= tunnel6_rcv,
	.err_handler	= tunnel6_err,
	.flags          = INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

static const struct inet6_protocol tunnel46_protocol = {
	.handler	= tunnel46_rcv,
	.err_handler	= tunnel46_err,
	.flags          = INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

static const struct inet6_protocol tunnelmpls6_protocol = {
	.handler	= tunnelmpls6_rcv,
	.err_handler	= tunnelmpls6_err,
	.flags          = INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

static int __init tunnel6_init(void)
{
	if (inet6_add_protocol(&tunnel6_protocol, IPPROTO_IPV6)) {
		pr_err("%s: can't add protocol\n", __func__);
		return -EAGAIN;
	}
	if (inet6_add_protocol(&tunnel46_protocol, IPPROTO_IPIP)) {
		pr_err("%s: can't add protocol\n", __func__);
		inet6_del_protocol(&tunnel6_protocol, IPPROTO_IPV6);
		return -EAGAIN;
	}
	if (xfrm6_tunnel_mpls_supported() &&
	    inet6_add_protocol(&tunnelmpls6_protocol, IPPROTO_MPLS)) {
		pr_err("%s: can't add protocol\n", __func__);
		inet6_del_protocol(&tunnel6_protocol, IPPROTO_IPV6);
		inet6_del_protocol(&tunnel46_protocol, IPPROTO_IPIP);
		return -EAGAIN;
	}
#if IS_ENABLED(CONFIG_INET6_XFRM_TUNNEL)
	if (xfrm_input_register_afinfo(&tunnel6_input_afinfo)) {
		pr_err("%s: can't add input afinfo\n", __func__);
		inet6_del_protocol(&tunnel6_protocol, IPPROTO_IPV6);
		inet6_del_protocol(&tunnel46_protocol, IPPROTO_IPIP);
		if (xfrm6_tunnel_mpls_supported())
			inet6_del_protocol(&tunnelmpls6_protocol, IPPROTO_MPLS);
		return -EAGAIN;
	}
#endif
	return 0;
}

static void __exit tunnel6_fini(void)
{
#if IS_ENABLED(CONFIG_INET6_XFRM_TUNNEL)
	if (xfrm_input_unregister_afinfo(&tunnel6_input_afinfo))
		pr_err("%s: can't remove input afinfo\n", __func__);
#endif
	if (inet6_del_protocol(&tunnel46_protocol, IPPROTO_IPIP))
		pr_err("%s: can't remove protocol\n", __func__);
	if (inet6_del_protocol(&tunnel6_protocol, IPPROTO_IPV6))
		pr_err("%s: can't remove protocol\n", __func__);
	if (xfrm6_tunnel_mpls_supported() &&
	    inet6_del_protocol(&tunnelmpls6_protocol, IPPROTO_MPLS))
		pr_err("%s: can't remove protocol\n", __func__);
}

module_init(tunnel6_init);
module_exit(tunnel6_fini);
MODULE_LICENSE("GPL");
