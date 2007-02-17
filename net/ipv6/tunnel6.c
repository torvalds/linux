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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors	Mitsuru KANDA  <mk@linux-ipv6.org>
 * 		YOSHIFUJI Hideaki <yoshfuji@linux-ipv6.org>
 */

#include <linux/icmpv6.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/xfrm.h>

static struct xfrm6_tunnel *tunnel6_handlers;
static struct xfrm6_tunnel *tunnel46_handlers;
static DEFINE_MUTEX(tunnel6_mutex);

int xfrm6_tunnel_register(struct xfrm6_tunnel *handler, unsigned short family)
{
	struct xfrm6_tunnel **pprev;
	int ret = -EEXIST;
	int priority = handler->priority;

	mutex_lock(&tunnel6_mutex);

	for (pprev = (family == AF_INET6) ? &tunnel6_handlers : &tunnel46_handlers;
	     *pprev; pprev = &(*pprev)->next) {
		if ((*pprev)->priority > priority)
			break;
		if ((*pprev)->priority == priority)
			goto err;
	}

	handler->next = *pprev;
	*pprev = handler;

	ret = 0;

err:
	mutex_unlock(&tunnel6_mutex);

	return ret;
}

EXPORT_SYMBOL(xfrm6_tunnel_register);

int xfrm6_tunnel_deregister(struct xfrm6_tunnel *handler, unsigned short family)
{
	struct xfrm6_tunnel **pprev;
	int ret = -ENOENT;

	mutex_lock(&tunnel6_mutex);

	for (pprev = (family == AF_INET6) ? &tunnel6_handlers : &tunnel46_handlers;
	     *pprev; pprev = &(*pprev)->next) {
		if (*pprev == handler) {
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

static int tunnel6_rcv(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct xfrm6_tunnel *handler;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto drop;

	for (handler = tunnel6_handlers; handler; handler = handler->next)
		if (!handler->handler(skb))
			return 0;

	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0, skb->dev);

drop:
	kfree_skb(skb);
	return 0;
}

static int tunnel46_rcv(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct xfrm6_tunnel *handler;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto drop;

	for (handler = tunnel46_handlers; handler; handler = handler->next)
		if (!handler->handler(skb))
			return 0;

	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0, skb->dev);

drop:
	kfree_skb(skb);
	return 0;
}

static void tunnel6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			int type, int code, int offset, __be32 info)
{
	struct xfrm6_tunnel *handler;

	for (handler = tunnel6_handlers; handler; handler = handler->next)
		if (!handler->err_handler(skb, opt, type, code, offset, info))
			break;
}

static struct inet6_protocol tunnel6_protocol = {
	.handler	= tunnel6_rcv,
	.err_handler	= tunnel6_err,
	.flags          = INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

static struct inet6_protocol tunnel46_protocol = {
	.handler	= tunnel46_rcv,
	.err_handler	= tunnel6_err,
	.flags          = INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

static int __init tunnel6_init(void)
{
	if (inet6_add_protocol(&tunnel6_protocol, IPPROTO_IPV6)) {
		printk(KERN_ERR "tunnel6 init(): can't add protocol\n");
		return -EAGAIN;
	}
	if (inet6_add_protocol(&tunnel46_protocol, IPPROTO_IPIP)) {
		printk(KERN_ERR "tunnel6 init(): can't add protocol\n");
		inet6_del_protocol(&tunnel6_protocol, IPPROTO_IPV6);
		return -EAGAIN;
	}
	return 0;
}

static void __exit tunnel6_fini(void)
{
	if (inet6_del_protocol(&tunnel46_protocol, IPPROTO_IPIP))
		printk(KERN_ERR "tunnel6 close: can't remove protocol\n");
	if (inet6_del_protocol(&tunnel6_protocol, IPPROTO_IPV6))
		printk(KERN_ERR "tunnel6 close: can't remove protocol\n");
}

module_init(tunnel6_init);
module_exit(tunnel6_fini);
MODULE_LICENSE("GPL");
