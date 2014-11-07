/*
 *	Linux NET3: IP/IP protocol decoder modified to support
 *		    virtual tunnel interface
 *
 *	Authors:
 *		Saurabh Mohan (saurabh.mohan@vyatta.com) 05/07/2012
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

/*
   This version of net/ipv4/ip_vti.c is cloned of net/ipv4/ipip.c

   For comments look at net/ipv4/ip_gre.c --ANK
 */


#include <linux/capability.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if_arp.h>
#include <linux/mroute.h>
#include <linux/init.h>
#include <linux/netfilter_ipv4.h>
#include <linux/if_ether.h>
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/ip_tunnels.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

static struct rtnl_link_ops vti_link_ops __read_mostly;

static int vti_net_id __read_mostly;
static int vti_tunnel_init(struct net_device *dev);

static int vti_input(struct sk_buff *skb, int nexthdr, __be32 spi,
		     int encap_type)
{
	struct ip_tunnel *tunnel;
	const struct iphdr *iph = ip_hdr(skb);
	struct net *net = dev_net(skb->dev);
	struct ip_tunnel_net *itn = net_generic(net, vti_net_id);

	tunnel = ip_tunnel_lookup(itn, skb->dev->ifindex, TUNNEL_NO_KEY,
				  iph->saddr, iph->daddr, 0);
	if (tunnel != NULL) {
		if (!xfrm4_policy_check(NULL, XFRM_POLICY_IN, skb))
			goto drop;

		XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip4 = tunnel;
		skb->mark = be32_to_cpu(tunnel->parms.i_key);

		return xfrm_input(skb, nexthdr, spi, encap_type);
	}

	return -EINVAL;
drop:
	kfree_skb(skb);
	return 0;
}

static int vti_rcv(struct sk_buff *skb)
{
	XFRM_SPI_SKB_CB(skb)->family = AF_INET;
	XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct iphdr, daddr);

	return vti_input(skb, ip_hdr(skb)->protocol, 0, 0);
}

static int vti_rcv_cb(struct sk_buff *skb, int err)
{
	unsigned short family;
	struct net_device *dev;
	struct pcpu_sw_netstats *tstats;
	struct xfrm_state *x;
	struct ip_tunnel *tunnel = XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip4;

	if (!tunnel)
		return 1;

	dev = tunnel->dev;

	if (err) {
		dev->stats.rx_errors++;
		dev->stats.rx_dropped++;

		return 0;
	}

	x = xfrm_input_state(skb);
	family = x->inner_mode->afinfo->family;

	if (!xfrm_policy_check(NULL, XFRM_POLICY_IN, skb, family))
		return -EPERM;

	skb_scrub_packet(skb, !net_eq(tunnel->net, dev_net(skb->dev)));
	skb->dev = dev;

	tstats = this_cpu_ptr(dev->tstats);

	u64_stats_update_begin(&tstats->syncp);
	tstats->rx_packets++;
	tstats->rx_bytes += skb->len;
	u64_stats_update_end(&tstats->syncp);

	return 0;
}

static bool vti_state_check(const struct xfrm_state *x, __be32 dst, __be32 src)
{
	xfrm_address_t *daddr = (xfrm_address_t *)&dst;
	xfrm_address_t *saddr = (xfrm_address_t *)&src;

	/* if there is no transform then this tunnel is not functional.
	 * Or if the xfrm is not mode tunnel.
	 */
	if (!x || x->props.mode != XFRM_MODE_TUNNEL ||
	    x->props.family != AF_INET)
		return false;

	if (!dst)
		return xfrm_addr_equal(saddr, &x->props.saddr, AF_INET);

	if (!xfrm_state_addr_check(x, daddr, saddr, AF_INET))
		return false;

	return true;
}

static netdev_tx_t vti_xmit(struct sk_buff *skb, struct net_device *dev,
			    struct flowi *fl)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct ip_tunnel_parm *parms = &tunnel->parms;
	struct dst_entry *dst = skb_dst(skb);
	struct net_device *tdev;	/* Device to other host */
	int err;

	if (!dst) {
		dev->stats.tx_carrier_errors++;
		goto tx_error_icmp;
	}

	dst_hold(dst);
	dst = xfrm_lookup(tunnel->net, dst, fl, NULL, 0);
	if (IS_ERR(dst)) {
		dev->stats.tx_carrier_errors++;
		goto tx_error_icmp;
	}

	if (!vti_state_check(dst->xfrm, parms->iph.daddr, parms->iph.saddr)) {
		dev->stats.tx_carrier_errors++;
		dst_release(dst);
		goto tx_error_icmp;
	}

	tdev = dst->dev;

	if (tdev == dev) {
		dst_release(dst);
		dev->stats.collisions++;
		goto tx_error;
	}

	if (tunnel->err_count > 0) {
		if (time_before(jiffies,
				tunnel->err_time + IPTUNNEL_ERR_TIMEO)) {
			tunnel->err_count--;
			dst_link_failure(skb);
		} else
			tunnel->err_count = 0;
	}

	skb_scrub_packet(skb, !net_eq(tunnel->net, dev_net(dev)));
	skb_dst_set(skb, dst);
	skb->dev = skb_dst(skb)->dev;

	err = dst_output(skb);
	if (net_xmit_eval(err) == 0)
		err = skb->len;
	iptunnel_xmit_stats(err, &dev->stats, dev->tstats);
	return NETDEV_TX_OK;

tx_error_icmp:
	dst_link_failure(skb);
tx_error:
	dev->stats.tx_errors++;
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

/* This function assumes it is being called from dev_queue_xmit()
 * and that skb is filled properly by that function.
 */
static netdev_tx_t vti_tunnel_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct flowi fl;

	memset(&fl, 0, sizeof(fl));

	skb->mark = be32_to_cpu(tunnel->parms.o_key);

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		xfrm_decode_session(skb, &fl, AF_INET);
		memset(IPCB(skb), 0, sizeof(*IPCB(skb)));
		break;
	case htons(ETH_P_IPV6):
		xfrm_decode_session(skb, &fl, AF_INET6);
		memset(IP6CB(skb), 0, sizeof(*IP6CB(skb)));
		break;
	default:
		dev->stats.tx_errors++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	return vti_xmit(skb, dev, &fl);
}

static int vti4_err(struct sk_buff *skb, u32 info)
{
	__be32 spi;
	__u32 mark;
	struct xfrm_state *x;
	struct ip_tunnel *tunnel;
	struct ip_esp_hdr *esph;
	struct ip_auth_hdr *ah ;
	struct ip_comp_hdr *ipch;
	struct net *net = dev_net(skb->dev);
	const struct iphdr *iph = (const struct iphdr *)skb->data;
	int protocol = iph->protocol;
	struct ip_tunnel_net *itn = net_generic(net, vti_net_id);

	tunnel = ip_tunnel_lookup(itn, skb->dev->ifindex, TUNNEL_NO_KEY,
				  iph->daddr, iph->saddr, 0);
	if (!tunnel)
		return -1;

	mark = be32_to_cpu(tunnel->parms.o_key);

	switch (protocol) {
	case IPPROTO_ESP:
		esph = (struct ip_esp_hdr *)(skb->data+(iph->ihl<<2));
		spi = esph->spi;
		break;
	case IPPROTO_AH:
		ah = (struct ip_auth_hdr *)(skb->data+(iph->ihl<<2));
		spi = ah->spi;
		break;
	case IPPROTO_COMP:
		ipch = (struct ip_comp_hdr *)(skb->data+(iph->ihl<<2));
		spi = htonl(ntohs(ipch->cpi));
		break;
	default:
		return 0;
	}

	switch (icmp_hdr(skb)->type) {
	case ICMP_DEST_UNREACH:
		if (icmp_hdr(skb)->code != ICMP_FRAG_NEEDED)
			return 0;
	case ICMP_REDIRECT:
		break;
	default:
		return 0;
	}

	x = xfrm_state_lookup(net, mark, (const xfrm_address_t *)&iph->daddr,
			      spi, protocol, AF_INET);
	if (!x)
		return 0;

	if (icmp_hdr(skb)->type == ICMP_DEST_UNREACH)
		ipv4_update_pmtu(skb, net, info, 0, 0, protocol, 0);
	else
		ipv4_redirect(skb, net, 0, 0, protocol, 0);
	xfrm_state_put(x);

	return 0;
}

static int
vti_tunnel_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int err = 0;
	struct ip_tunnel_parm p;

	if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p)))
		return -EFAULT;

	if (cmd == SIOCADDTUNNEL || cmd == SIOCCHGTUNNEL) {
		if (p.iph.version != 4 || p.iph.protocol != IPPROTO_IPIP ||
		    p.iph.ihl != 5)
			return -EINVAL;
	}

	if (!(p.i_flags & GRE_KEY))
		p.i_key = 0;
	if (!(p.o_flags & GRE_KEY))
		p.o_key = 0;

	p.i_flags = VTI_ISVTI;

	err = ip_tunnel_ioctl(dev, &p, cmd);
	if (err)
		return err;

	if (cmd != SIOCDELTUNNEL) {
		p.i_flags |= GRE_KEY;
		p.o_flags |= GRE_KEY;
	}

	if (copy_to_user(ifr->ifr_ifru.ifru_data, &p, sizeof(p)))
		return -EFAULT;
	return 0;
}

static const struct net_device_ops vti_netdev_ops = {
	.ndo_init	= vti_tunnel_init,
	.ndo_uninit	= ip_tunnel_uninit,
	.ndo_start_xmit	= vti_tunnel_xmit,
	.ndo_do_ioctl	= vti_tunnel_ioctl,
	.ndo_change_mtu	= ip_tunnel_change_mtu,
	.ndo_get_stats64 = ip_tunnel_get_stats64,
};

static void vti_tunnel_setup(struct net_device *dev)
{
	dev->netdev_ops		= &vti_netdev_ops;
	dev->type		= ARPHRD_TUNNEL;
	ip_tunnel_setup(dev, vti_net_id);
}

static int vti_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct iphdr *iph = &tunnel->parms.iph;

	memcpy(dev->dev_addr, &iph->saddr, 4);
	memcpy(dev->broadcast, &iph->daddr, 4);

	dev->hard_header_len	= LL_MAX_HEADER + sizeof(struct iphdr);
	dev->mtu		= ETH_DATA_LEN;
	dev->flags		= IFF_NOARP;
	dev->iflink		= 0;
	dev->addr_len		= 4;
	dev->features		|= NETIF_F_LLTX;
	netif_keep_dst(dev);

	return ip_tunnel_init(dev);
}

static void __net_init vti_fb_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct iphdr *iph = &tunnel->parms.iph;

	iph->version		= 4;
	iph->protocol		= IPPROTO_IPIP;
	iph->ihl		= 5;
}

static struct xfrm4_protocol vti_esp4_protocol __read_mostly = {
	.handler	=	vti_rcv,
	.input_handler	=	vti_input,
	.cb_handler	=	vti_rcv_cb,
	.err_handler	=	vti4_err,
	.priority	=	100,
};

static struct xfrm4_protocol vti_ah4_protocol __read_mostly = {
	.handler	=	vti_rcv,
	.input_handler	=	vti_input,
	.cb_handler	=	vti_rcv_cb,
	.err_handler	=	vti4_err,
	.priority	=	100,
};

static struct xfrm4_protocol vti_ipcomp4_protocol __read_mostly = {
	.handler	=	vti_rcv,
	.input_handler	=	vti_input,
	.cb_handler	=	vti_rcv_cb,
	.err_handler	=	vti4_err,
	.priority	=	100,
};

static int __net_init vti_init_net(struct net *net)
{
	int err;
	struct ip_tunnel_net *itn;

	err = ip_tunnel_init_net(net, vti_net_id, &vti_link_ops, "ip_vti0");
	if (err)
		return err;
	itn = net_generic(net, vti_net_id);
	vti_fb_tunnel_init(itn->fb_tunnel_dev);
	return 0;
}

static void __net_exit vti_exit_net(struct net *net)
{
	struct ip_tunnel_net *itn = net_generic(net, vti_net_id);
	ip_tunnel_delete_net(itn, &vti_link_ops);
}

static struct pernet_operations vti_net_ops = {
	.init = vti_init_net,
	.exit = vti_exit_net,
	.id   = &vti_net_id,
	.size = sizeof(struct ip_tunnel_net),
};

static int vti_tunnel_validate(struct nlattr *tb[], struct nlattr *data[])
{
	return 0;
}

static void vti_netlink_parms(struct nlattr *data[],
			      struct ip_tunnel_parm *parms)
{
	memset(parms, 0, sizeof(*parms));

	parms->iph.protocol = IPPROTO_IPIP;

	if (!data)
		return;

	parms->i_flags = VTI_ISVTI;

	if (data[IFLA_VTI_LINK])
		parms->link = nla_get_u32(data[IFLA_VTI_LINK]);

	if (data[IFLA_VTI_IKEY])
		parms->i_key = nla_get_be32(data[IFLA_VTI_IKEY]);

	if (data[IFLA_VTI_OKEY])
		parms->o_key = nla_get_be32(data[IFLA_VTI_OKEY]);

	if (data[IFLA_VTI_LOCAL])
		parms->iph.saddr = nla_get_be32(data[IFLA_VTI_LOCAL]);

	if (data[IFLA_VTI_REMOTE])
		parms->iph.daddr = nla_get_be32(data[IFLA_VTI_REMOTE]);

}

static int vti_newlink(struct net *src_net, struct net_device *dev,
		       struct nlattr *tb[], struct nlattr *data[])
{
	struct ip_tunnel_parm parms;

	vti_netlink_parms(data, &parms);
	return ip_tunnel_newlink(dev, tb, &parms);
}

static int vti_changelink(struct net_device *dev, struct nlattr *tb[],
			  struct nlattr *data[])
{
	struct ip_tunnel_parm p;

	vti_netlink_parms(data, &p);
	return ip_tunnel_changelink(dev, tb, &p);
}

static size_t vti_get_size(const struct net_device *dev)
{
	return
		/* IFLA_VTI_LINK */
		nla_total_size(4) +
		/* IFLA_VTI_IKEY */
		nla_total_size(4) +
		/* IFLA_VTI_OKEY */
		nla_total_size(4) +
		/* IFLA_VTI_LOCAL */
		nla_total_size(4) +
		/* IFLA_VTI_REMOTE */
		nla_total_size(4) +
		0;
}

static int vti_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct ip_tunnel *t = netdev_priv(dev);
	struct ip_tunnel_parm *p = &t->parms;

	nla_put_u32(skb, IFLA_VTI_LINK, p->link);
	nla_put_be32(skb, IFLA_VTI_IKEY, p->i_key);
	nla_put_be32(skb, IFLA_VTI_OKEY, p->o_key);
	nla_put_be32(skb, IFLA_VTI_LOCAL, p->iph.saddr);
	nla_put_be32(skb, IFLA_VTI_REMOTE, p->iph.daddr);

	return 0;
}

static const struct nla_policy vti_policy[IFLA_VTI_MAX + 1] = {
	[IFLA_VTI_LINK]		= { .type = NLA_U32 },
	[IFLA_VTI_IKEY]		= { .type = NLA_U32 },
	[IFLA_VTI_OKEY]		= { .type = NLA_U32 },
	[IFLA_VTI_LOCAL]	= { .len = FIELD_SIZEOF(struct iphdr, saddr) },
	[IFLA_VTI_REMOTE]	= { .len = FIELD_SIZEOF(struct iphdr, daddr) },
};

static struct rtnl_link_ops vti_link_ops __read_mostly = {
	.kind		= "vti",
	.maxtype	= IFLA_VTI_MAX,
	.policy		= vti_policy,
	.priv_size	= sizeof(struct ip_tunnel),
	.setup		= vti_tunnel_setup,
	.validate	= vti_tunnel_validate,
	.newlink	= vti_newlink,
	.changelink	= vti_changelink,
	.get_size	= vti_get_size,
	.fill_info	= vti_fill_info,
};

static int __init vti_init(void)
{
	const char *msg;
	int err;

	pr_info("IPv4 over IPsec tunneling driver\n");

	msg = "tunnel device";
	err = register_pernet_device(&vti_net_ops);
	if (err < 0)
		goto pernet_dev_failed;

	msg = "tunnel protocols";
	err = xfrm4_protocol_register(&vti_esp4_protocol, IPPROTO_ESP);
	if (err < 0)
		goto xfrm_proto_esp_failed;
	err = xfrm4_protocol_register(&vti_ah4_protocol, IPPROTO_AH);
	if (err < 0)
		goto xfrm_proto_ah_failed;
	err = xfrm4_protocol_register(&vti_ipcomp4_protocol, IPPROTO_COMP);
	if (err < 0)
		goto xfrm_proto_comp_failed;

	msg = "netlink interface";
	err = rtnl_link_register(&vti_link_ops);
	if (err < 0)
		goto rtnl_link_failed;

	return err;

rtnl_link_failed:
	xfrm4_protocol_deregister(&vti_ipcomp4_protocol, IPPROTO_COMP);
xfrm_proto_comp_failed:
	xfrm4_protocol_deregister(&vti_ah4_protocol, IPPROTO_AH);
xfrm_proto_ah_failed:
	xfrm4_protocol_deregister(&vti_esp4_protocol, IPPROTO_ESP);
xfrm_proto_esp_failed:
	unregister_pernet_device(&vti_net_ops);
pernet_dev_failed:
	pr_err("vti init: failed to register %s\n", msg);
	return err;
}

static void __exit vti_fini(void)
{
	rtnl_link_unregister(&vti_link_ops);
	xfrm4_protocol_deregister(&vti_ipcomp4_protocol, IPPROTO_COMP);
	xfrm4_protocol_deregister(&vti_ah4_protocol, IPPROTO_AH);
	xfrm4_protocol_deregister(&vti_esp4_protocol, IPPROTO_ESP);
	unregister_pernet_device(&vti_net_ops);
}

module_init(vti_init);
module_exit(vti_fini);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK("vti");
MODULE_ALIAS_NETDEV("ip_vti0");
