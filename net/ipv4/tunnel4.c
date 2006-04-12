/* tunnel4.c: Generic IP tunnel transformer.
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/xfrm.h>

static struct xfrm_tunnel *tunnel4_handlers;
static DEFINE_MUTEX(tunnel4_mutex);

int xfrm4_tunnel_register(struct xfrm_tunnel *handler)
{
	struct xfrm_tunnel **pprev;
	int ret = -EEXIST;
	int priority = handler->priority;

	mutex_lock(&tunnel4_mutex);

	for (pprev = &tunnel4_handlers; *pprev; pprev = &(*pprev)->next) {
		if ((*pprev)->priority > priority)
			break;
		if ((*pprev)->priority == priority)
			goto err;
	}

	handler->next = *pprev;
	*pprev = handler;

	ret = 0;

err:
	mutex_unlock(&tunnel4_mutex);

	return ret;
}

EXPORT_SYMBOL(xfrm4_tunnel_register);

int xfrm4_tunnel_deregister(struct xfrm_tunnel *handler)
{
	struct xfrm_tunnel **pprev;
	int ret = -ENOENT;

	mutex_lock(&tunnel4_mutex);

	for (pprev = &tunnel4_handlers; *pprev; pprev = &(*pprev)->next) {
		if (*pprev == handler) {
			*pprev = handler->next;
			ret = 0;
			break;
		}
	}

	mutex_unlock(&tunnel4_mutex);

	synchronize_net();

	return ret;
}

EXPORT_SYMBOL(xfrm4_tunnel_deregister);

static int tunnel4_rcv(struct sk_buff *skb)
{
	struct xfrm_tunnel *handler;

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto drop;

	for (handler = tunnel4_handlers; handler; handler = handler->next)
		if (!handler->handler(skb))
			return 0;

	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);

drop:
	kfree_skb(skb);
	return 0;
}

static void tunnel4_err(struct sk_buff *skb, u32 info)
{
	struct xfrm_tunnel *handler;

	for (handler = tunnel4_handlers; handler; handler = handler->next)
		if (!handler->err_handler(skb, info))
			break;
}

static struct net_protocol tunnel4_protocol = {
	.handler	=	tunnel4_rcv,
	.err_handler	=	tunnel4_err,
	.no_policy	=	1,
};

static int __init tunnel4_init(void)
{
	if (inet_add_protocol(&tunnel4_protocol, IPPROTO_IPIP)) {
		printk(KERN_ERR "tunnel4 init: can't add protocol\n");
		return -EAGAIN;
	}
	return 0;
}

static void __exit tunnel4_fini(void)
{
	if (inet_del_protocol(&tunnel4_protocol, IPPROTO_IPIP))
		printk(KERN_ERR "tunnel4 close: can't remove protocol\n");
}

module_init(tunnel4_init);
module_exit(tunnel4_fini);
MODULE_LICENSE("GPL");
