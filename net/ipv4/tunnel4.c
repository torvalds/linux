/* tunnel4.c: Generic IP tunnel transformer.
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/xfrm.h>

static struct xfrm_tunnel __rcu *tunnel4_handlers __read_mostly;
static struct xfrm_tunnel __rcu *tunnel64_handlers __read_mostly;
static DEFINE_MUTEX(tunnel4_mutex);

static inline struct xfrm_tunnel __rcu **fam_handlers(unsigned short family)
{
	return (family == AF_INET) ? &tunnel4_handlers : &tunnel64_handlers;
}

int xfrm4_tunnel_register(struct xfrm_tunnel *handler, unsigned short family)
{
	struct xfrm_tunnel __rcu **pprev;
	struct xfrm_tunnel *t;

	int ret = -EEXIST;
	int priority = handler->priority;

	mutex_lock(&tunnel4_mutex);

	for (pprev = fam_handlers(family);
	     (t = rcu_dereference_protected(*pprev,
			lockdep_is_held(&tunnel4_mutex))) != NULL;
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
	mutex_unlock(&tunnel4_mutex);

	return ret;
}
EXPORT_SYMBOL(xfrm4_tunnel_register);

int xfrm4_tunnel_deregister(struct xfrm_tunnel *handler, unsigned short family)
{
	struct xfrm_tunnel __rcu **pprev;
	struct xfrm_tunnel *t;
	int ret = -ENOENT;

	mutex_lock(&tunnel4_mutex);

	for (pprev = fam_handlers(family);
	     (t = rcu_dereference_protected(*pprev,
			lockdep_is_held(&tunnel4_mutex))) != NULL;
	     pprev = &t->next) {
		if (t == handler) {
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

#define for_each_tunnel_rcu(head, handler)		\
	for (handler = rcu_dereference(head);		\
	     handler != NULL;				\
	     handler = rcu_dereference(handler->next))	\
	
static int tunnel4_rcv(struct sk_buff *skb)
{
	struct xfrm_tunnel *handler;

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto drop;

	for_each_tunnel_rcu(tunnel4_handlers, handler)
		if (!handler->handler(skb))
			return 0;

	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);

drop:
	kfree_skb(skb);
	return 0;
}

#if IS_ENABLED(CONFIG_IPV6)
static int tunnel64_rcv(struct sk_buff *skb)
{
	struct xfrm_tunnel *handler;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto drop;

	for_each_tunnel_rcu(tunnel64_handlers, handler)
		if (!handler->handler(skb))
			return 0;

	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);

drop:
	kfree_skb(skb);
	return 0;
}
#endif

static void tunnel4_err(struct sk_buff *skb, u32 info)
{
	struct xfrm_tunnel *handler;

	for_each_tunnel_rcu(tunnel4_handlers, handler)
		if (!handler->err_handler(skb, info))
			break;
}

#if IS_ENABLED(CONFIG_IPV6)
static void tunnel64_err(struct sk_buff *skb, u32 info)
{
	struct xfrm_tunnel *handler;

	for_each_tunnel_rcu(tunnel64_handlers, handler)
		if (!handler->err_handler(skb, info))
			break;
}
#endif

static const struct net_protocol tunnel4_protocol = {
	.handler	=	tunnel4_rcv,
	.err_handler	=	tunnel4_err,
	.no_policy	=	1,
	.netns_ok	=	1,
};

#if IS_ENABLED(CONFIG_IPV6)
static const struct net_protocol tunnel64_protocol = {
	.handler	=	tunnel64_rcv,
	.err_handler	=	tunnel64_err,
	.no_policy	=	1,
	.netns_ok	=	1,
};
#endif

static int __init tunnel4_init(void)
{
	if (inet_add_protocol(&tunnel4_protocol, IPPROTO_IPIP)) {
		pr_err("%s: can't add protocol\n", __func__);
		return -EAGAIN;
	}
#if IS_ENABLED(CONFIG_IPV6)
	if (inet_add_protocol(&tunnel64_protocol, IPPROTO_IPV6)) {
		pr_err("tunnel64 init: can't add protocol\n");
		inet_del_protocol(&tunnel4_protocol, IPPROTO_IPIP);
		return -EAGAIN;
	}
#endif
	return 0;
}

static void __exit tunnel4_fini(void)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (inet_del_protocol(&tunnel64_protocol, IPPROTO_IPV6))
		pr_err("tunnel64 close: can't remove protocol\n");
#endif
	if (inet_del_protocol(&tunnel4_protocol, IPPROTO_IPIP))
		pr_err("tunnel4 close: can't remove protocol\n");
}

module_init(tunnel4_init);
module_exit(tunnel4_fini);
MODULE_LICENSE("GPL");
