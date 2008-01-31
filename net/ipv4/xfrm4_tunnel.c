/* xfrm4_tunnel.c: Generic IP tunnel transformer.
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <net/xfrm.h>
#include <net/ip.h>
#include <net/protocol.h>

static int ipip_output(struct xfrm_state *x, struct sk_buff *skb)
{
	skb_push(skb, -skb_network_offset(skb));
	return 0;
}

static int ipip_xfrm_rcv(struct xfrm_state *x, struct sk_buff *skb)
{
	return ip_hdr(skb)->protocol;
}

static int ipip_init_state(struct xfrm_state *x)
{
	if (x->props.mode != XFRM_MODE_TUNNEL)
		return -EINVAL;

	if (x->encap)
		return -EINVAL;

	x->props.header_len = sizeof(struct iphdr);

	return 0;
}

static void ipip_destroy(struct xfrm_state *x)
{
}

static const struct xfrm_type ipip_type = {
	.description	= "IPIP",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_IPIP,
	.init_state	= ipip_init_state,
	.destructor	= ipip_destroy,
	.input		= ipip_xfrm_rcv,
	.output		= ipip_output
};

static int xfrm_tunnel_rcv(struct sk_buff *skb)
{
	return xfrm4_rcv_spi(skb, IPPROTO_IPIP, ip_hdr(skb)->saddr);
}

static int xfrm_tunnel_err(struct sk_buff *skb, u32 info)
{
	return -ENOENT;
}

static struct xfrm_tunnel xfrm_tunnel_handler = {
	.handler	=	xfrm_tunnel_rcv,
	.err_handler	=	xfrm_tunnel_err,
	.priority	=	2,
};

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static struct xfrm_tunnel xfrm64_tunnel_handler = {
	.handler	=	xfrm_tunnel_rcv,
	.err_handler	=	xfrm_tunnel_err,
	.priority	=	2,
};
#endif

static int __init ipip_init(void)
{
	if (xfrm_register_type(&ipip_type, AF_INET) < 0) {
		printk(KERN_INFO "ipip init: can't add xfrm type\n");
		return -EAGAIN;
	}

	if (xfrm4_tunnel_register(&xfrm_tunnel_handler, AF_INET)) {
		printk(KERN_INFO "ipip init: can't add xfrm handler for AF_INET\n");
		xfrm_unregister_type(&ipip_type, AF_INET);
		return -EAGAIN;
	}
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	if (xfrm4_tunnel_register(&xfrm64_tunnel_handler, AF_INET6)) {
		printk(KERN_INFO "ipip init: can't add xfrm handler for AF_INET6\n");
		xfrm4_tunnel_deregister(&xfrm_tunnel_handler, AF_INET);
		xfrm_unregister_type(&ipip_type, AF_INET);
		return -EAGAIN;
	}
#endif
	return 0;
}

static void __exit ipip_fini(void)
{
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	if (xfrm4_tunnel_deregister(&xfrm64_tunnel_handler, AF_INET6))
		printk(KERN_INFO "ipip close: can't remove xfrm handler for AF_INET6\n");
#endif
	if (xfrm4_tunnel_deregister(&xfrm_tunnel_handler, AF_INET))
		printk(KERN_INFO "ipip close: can't remove xfrm handler for AF_INET\n");
	if (xfrm_unregister_type(&ipip_type, AF_INET) < 0)
		printk(KERN_INFO "ipip close: can't remove xfrm type\n");
}

module_init(ipip_init);
module_exit(ipip_fini);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_TYPE(AF_INET, XFRM_PROTO_IPIP);
