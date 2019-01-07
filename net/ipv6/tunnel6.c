/*
 * Copyright (C)2003,2004 USAGI/WIDE Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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
static DEFINE_MUTEX(tunnel6_mutex);

int xfrm6_tunnel_register(struct xfrm6_tunnel *handler, unsigned short family)
{
	struct xfrm6_tunnel __rcu **pprev;
	struct xfrm6_tunnel *t;
	int ret = -EEXIST;
	int priority = handler->priority;

	mutex_lock(&tunnel6_mutex);

	for (pprev = (family == AF_INET6) ? &tunnel6_handlers : &tunnel46_handlers;
	     (t = rcu_dereference_protected(*pprev,
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

	for (pprev = (family == AF_INET6) ? &tunnel6_handlers : &tunnel46_handlers;
	     (t = rcu_dereference_protected(*pprev,
			lockdep_is_held(&tunnel6_mutex))) != NULL;
	     pprev = &t->next) {
		if (t == handler) {
			*pprev = handler->next;
			ret = 0;
			break;
		}
	}

	mutex_unlock(&tunnel6_mutex);

	synchronize_net();

	return ret;
}
EXPORT_SYMBOL(xfrm6_tunnel_deregister);

#define for_each_tunnel_rcu(head, handler)		\
	for (handler = rcu_dereference(head);		\
	     handler != NULL;				\
	     handler = rcu_dereference(handler->next))	\

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
	return 0;
}

static void __exit tunnel6_fini(void)
{
	if (inet6_del_protocol(&tunnel46_protocol, IPPROTO_IPIP))
		pr_err("%s: can't remove protocol\n", __func__);
	if (inet6_del_protocol(&tunnel6_protocol, IPPROTO_IPV6))
		pr_err("%s: can't remove protocol\n", __func__);
}

module_init(tunnel6_init);
module_exit(tunnel6_fini);
MODULE_LICENSE("GPL");
