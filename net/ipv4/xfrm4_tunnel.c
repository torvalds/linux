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
	struct iphdr *iph;
	
	iph = skb->nh.iph;
	iph->tot_len = htons(skb->len);
	ip_send_check(iph);

	return 0;
}

static int ipip_xfrm_rcv(struct xfrm_state *x, struct sk_buff *skb)
{
	return 0;
}

static int ipip_init_state(struct xfrm_state *x)
{
	if (!x->props.mode)
		return -EINVAL;

	if (x->encap)
		return -EINVAL;

	x->props.header_len = sizeof(struct iphdr);

	return 0;
}

static void ipip_destroy(struct xfrm_state *x)
{
}

static struct xfrm_type ipip_type = {
	.description	= "IPIP",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_IPIP,
	.init_state	= ipip_init_state,
	.destructor	= ipip_destroy,
	.input		= ipip_xfrm_rcv,
	.output		= ipip_output
};

static int xfrm_tunnel_err(struct sk_buff *skb, u32 info)
{
	return -ENOENT;
}

static struct xfrm_tunnel xfrm_tunnel_handler = {
	.handler	=	xfrm4_rcv,
	.err_handler	=	xfrm_tunnel_err,
	.priority	=	2,
};

static int __init ipip_init(void)
{
	if (xfrm_register_type(&ipip_type, AF_INET) < 0) {
		printk(KERN_INFO "ipip init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (xfrm4_tunnel_register(&xfrm_tunnel_handler)) {
		printk(KERN_INFO "ipip init: can't add xfrm handler\n");
		xfrm_unregister_type(&ipip_type, AF_INET);
		return -EAGAIN;
	}
	return 0;
}

static void __exit ipip_fini(void)
{
	if (xfrm4_tunnel_deregister(&xfrm_tunnel_handler))
		printk(KERN_INFO "ipip close: can't remove xfrm handler\n");
	if (xfrm_unregister_type(&ipip_type, AF_INET) < 0)
		printk(KERN_INFO "ipip close: can't remove xfrm type\n");
}

module_init(ipip_init);
module_exit(ipip_fini);
MODULE_LICENSE("GPL");
