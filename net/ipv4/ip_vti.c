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

/* We dont digest the packet therefore let the packet pass */
static int vti_rcv(struct sk_buff *skb)
{
	struct ip_tunnel *tunnel;
	const struct iphdr *iph = ip_hdr(skb);
	struct net *net = dev_net(skb->dev);
	struct ip_tunnel_net *itn = net_generic(net, vti_net_id);

	tunnel = ip_tunnel_lookup(itn, skb->dev->ifindex, TUNNEL_NO_KEY,
				  iph->saddr, iph->daddr, 0);
	if (tunnel != NULL) {
		struct pcpu_sw_netstats *tstats;
		u32 oldmark = skb->mark;
		int ret;


		/* temporarily mark the skb with the tunnel o_key, to
		 * only match policies with this mark.
		 */
		skb->mark = be32_to_cpu(tunnel->parms.o_key);
		ret = xfrm4_policy_check(NULL, XFRM_POLICY_IN, skb);
		skb->mark = oldmark;
		if (!ret)
			return -1;

		tstats = this_cpu_ptr(tunnel->dev->tstats);
		u64_stats_update_begin(&tstats->syncp);
		tstats->rx_packets++;
		tstats->rx_bytes += skb->len;
		u64_stats_update_end(&tstats->syncp);

		secpath_reset(skb);
		skb->dev = tunnel->dev;
		return 1;
	}

	return -1;
}

/* This function assumes it is being called from dev_queue_xmit()
 * and that skb is filled properly by that function.
 */

static netdev_tx_t vti_tunnel_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct iphdr  *tiph = &tunnel->parms.iph;
	u8     tos;
	struct rtable *rt;		/* Route to the other host */
	struct net_device *tdev;	/* Device to other host */
	struct iphdr  *old_iph = ip_hdr(skb);
	__be32 dst = tiph->daddr;
	struct flowi4 fl4;
	int err;

	if (skb->protocol != htons(ETH_P_IP))
		goto tx_error;

	tos = old_iph->tos;

	memset(&fl4, 0, sizeof(fl4));
	flowi4_init_output(&fl4, tunnel->parms.link,
			   be32_to_cpu(tunnel->parms.o_key), RT_TOS(tos),
			   RT_SCOPE_UNIVERSE,
			   IPPROTO_IPIP, 0,
			   dst, tiph->saddr, 0, 0);
	rt = ip_route_output_key(dev_net(dev), &fl4);
	if (IS_ERR(rt)) {
		dev->stats.tx_carrier_errors++;
		goto tx_error_icmp;
	}
	/* if there is no transform then this tunnel is not functional.
	 * Or if the xfrm is not mode tunnel.
	 */
	if (!rt->dst.xfrm ||
	    rt->dst.xfrm->props.mode != XFRM_MODE_TUNNEL) {
		dev->stats.tx_carrier_errors++;
		ip_rt_put(rt);
		goto tx_error_icmp;
	}
	tdev = rt->dst.dev;

	if (tdev == dev) {
		ip_rt_put(rt);
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

	memset(IPCB(skb), 0, sizeof(*IPCB(skb)));
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);
	nf_reset(skb);
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

	err = ip_tunnel_ioctl(dev, &p, cmd);
	if (err)
		return err;

	if (cmd != SIOCDELTUNNEL) {
		p.i_flags |= GRE_KEY | VTI_ISVTI;
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
	ip_tunnel_setup(dev, vti_net_id);
}

static int vti_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct iphdr *iph = &tunnel->parms.iph;

	memcpy(dev->dev_addr, &iph->saddr, 4);
	memcpy(dev->broadcast, &iph->daddr, 4);

	dev->type		= ARPHRD_TUNNEL;
	dev->hard_header_len	= LL_MAX_HEADER + sizeof(struct iphdr);
	dev->mtu		= ETH_DATA_LEN;
	dev->flags		= IFF_NOARP;
	dev->iflink		= 0;
	dev->addr_len		= 4;
	dev->features		|= NETIF_F_NETNS_LOCAL;
	dev->features		|= NETIF_F_LLTX;
	dev->priv_flags		&= ~IFF_XMIT_DST_RELEASE;

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

static struct xfrm_tunnel_notifier vti_handler __read_mostly = {
	.handler	=	vti_rcv,
	.priority	=	1,
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
	int err;

	pr_info("IPv4 over IPSec tunneling driver\n");

	err = register_pernet_device(&vti_net_ops);
	if (err < 0)
		return err;
	err = xfrm4_mode_tunnel_input_register(&vti_handler);
	if (err < 0) {
		unregister_pernet_device(&vti_net_ops);
		pr_info("vti init: can't register tunnel\n");
	}

	err = rtnl_link_register(&vti_link_ops);
	if (err < 0)
		goto rtnl_link_failed;

	return err;

rtnl_link_failed:
	xfrm4_mode_tunnel_input_deregister(&vti_handler);
	unregister_pernet_device(&vti_net_ops);
	return err;
}

static void __exit vti_fini(void)
{
	rtnl_link_unregister(&vti_link_ops);
	if (xfrm4_mode_tunnel_input_deregister(&vti_handler))
		pr_info("vti close: can't deregister tunnel\n");

	unregister_pernet_device(&vti_net_ops);
}

module_init(vti_init);
module_exit(vti_fini);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK("vti");
MODULE_ALIAS_NETDEV("ip_vti0");
