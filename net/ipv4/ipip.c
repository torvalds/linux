/*
 *	Linux NET3:	IP/IP protocol decoder.
 *
 *	Authors:
 *		Sam Lantinga (slouken@cs.ucdavis.edu)  02/01/95
 *
 *	Fixes:
 *		Alan Cox	:	Merged and made usable non modular (its so tiny its silly as
 *					a module taking up 2 pages).
 *		Alan Cox	: 	Fixed bug with 1.3.18 and IPIP not working (now needs to set skb->h.iph)
 *					to keep ip_forward happy.
 *		Alan Cox	:	More fixes for 1.3.21, and firewall fix. Maybe this will work soon 8).
 *		Kai Schulte	:	Fixed #defines for IP_FIREWALL->FIREWALL
 *              David Woodhouse :       Perform some basic ICMP handling.
 *                                      IPIP Routing without decapsulation.
 *              Carlos Picoto   :       GRE over IP support
 *		Alexey Kuznetsov:	Reworked. Really, now it is truncated version of ipv4/ip_gre.c.
 *					I do not want to merge them together.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

/* tunnel.c: an IP tunnel driver

	The purpose of this driver is to provide an IP tunnel through
	which you can tunnel network traffic transparently across subnets.

	This was written by looking at Nick Holloway's dummy driver
	Thanks for the great code!

		-Sam Lantinga	(slouken@cs.ucdavis.edu)  02/01/95

	Minor tweaks:
		Cleaned up the code a little and added some pre-1.3.0 tweaks.
		dev->hard_header/hard_header_len changed to use no headers.
		Comments/bracketing tweaked.
		Made the tunnels use dev->name not tunnel: when error reporting.
		Added tx_dropped stat

		-Alan Cox	(alan@lxorguk.ukuu.org.uk) 21 March 95

	Reworked:
		Changed to tunnel to destination gateway in addition to the
			tunnel's pointopoint address
		Almost completely rewritten
		Note:  There is currently no firewall or ICMP handling done.

		-Sam Lantinga	(slouken@cs.ucdavis.edu) 02/13/96

*/

/* Things I wish I had known when writing the tunnel driver:

	When the tunnel_xmit() function is called, the skb contains the
	packet to be sent (plus a great deal of extra info), and dev
	contains the tunnel device that _we_ are.

	When we are passed a packet, we are expected to fill in the
	source address with our source IP address.

	What is the proper way to allocate, copy and free a buffer?
	After you allocate it, it is a "0 length" chunk of memory
	starting at zero.  If you want to add headers to the buffer
	later, you'll have to call "skb_reserve(skb, amount)" with
	the amount of memory you want reserved.  Then, you call
	"skb_put(skb, amount)" with the amount of space you want in
	the buffer.  skb_put() returns a pointer to the top (#0) of
	that buffer.  skb->len is set to the amount of space you have
	"allocated" with skb_put().  You can then write up to skb->len
	bytes to that buffer.  If you need more, you can call skb_put()
	again with the additional amount of space you need.  You can
	find out how much more space you can allocate by calling
	"skb_tailroom(skb)".
	Now, to add header space, call "skb_push(skb, header_len)".
	This creates space at the beginning of the buffer and returns
	a pointer to this new space.  If later you need to strip a
	header from a buffer, call "skb_pull(skb, header_len)".
	skb_headroom() will return how much space is left at the top
	of the buffer (before the main data).  Remember, this headroom
	space must be reserved before the skb_put() function is called.
	*/

/*
   This version of net/ipv4/ipip.c is cloned of net/ipv4/ip_gre.c

   For comments look at net/ipv4/ip_gre.c --ANK
 */


#include <linux/capability.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
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

static bool log_ecn_error = true;
module_param(log_ecn_error, bool, 0644);
MODULE_PARM_DESC(log_ecn_error, "Log packets received with corrupted ECN");

static int ipip_net_id __read_mostly;

static int ipip_tunnel_init(struct net_device *dev);
static struct rtnl_link_ops ipip_link_ops __read_mostly;

static int ipip_err(struct sk_buff *skb, u32 info)
{

/* All the routers (except for Linux) return only
   8 bytes of packet payload. It means, that precise relaying of
   ICMP in the real Internet is absolutely infeasible.
 */
	struct net *net = dev_net(skb->dev);
	struct ip_tunnel_net *itn = net_generic(net, ipip_net_id);
	const struct iphdr *iph = (const struct iphdr *)skb->data;
	struct ip_tunnel *t;
	int err;
	const int type = icmp_hdr(skb)->type;
	const int code = icmp_hdr(skb)->code;

	err = -ENOENT;
	t = ip_tunnel_lookup(itn, skb->dev->ifindex, TUNNEL_NO_KEY,
			     iph->daddr, iph->saddr, 0);
	if (t == NULL)
		goto out;

	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED) {
		ipv4_update_pmtu(skb, dev_net(skb->dev), info,
				 t->dev->ifindex, 0, IPPROTO_IPIP, 0);
		err = 0;
		goto out;
	}

	if (type == ICMP_REDIRECT) {
		ipv4_redirect(skb, dev_net(skb->dev), t->dev->ifindex, 0,
			      IPPROTO_IPIP, 0);
		err = 0;
		goto out;
	}

	if (t->parms.iph.daddr == 0)
		goto out;

	err = 0;
	if (t->parms.iph.ttl == 0 && type == ICMP_TIME_EXCEEDED)
		goto out;

	if (time_before(jiffies, t->err_time + IPTUNNEL_ERR_TIMEO))
		t->err_count++;
	else
		t->err_count = 1;
	t->err_time = jiffies;

out:
	return err;
}

static const struct tnl_ptk_info tpi = {
	/* no tunnel info required for ipip. */
	.proto = htons(ETH_P_IP),
};

static int ipip_rcv(struct sk_buff *skb)
{
	struct net *net = dev_net(skb->dev);
	struct ip_tunnel_net *itn = net_generic(net, ipip_net_id);
	struct ip_tunnel *tunnel;
	const struct iphdr *iph = ip_hdr(skb);

	tunnel = ip_tunnel_lookup(itn, skb->dev->ifindex, TUNNEL_NO_KEY,
			iph->saddr, iph->daddr, 0);
	if (tunnel) {
		if (!xfrm4_policy_check(NULL, XFRM_POLICY_IN, skb))
			goto drop;
		return ip_tunnel_rcv(tunnel, skb, &tpi, 0, log_ecn_error);
	}

	return -1;

drop:
	kfree_skb(skb);
	return 0;
}

/*
 *	This function assumes it is being called from dev_queue_xmit()
 *	and that skb is filled properly by that function.
 */
static netdev_tx_t ipip_tunnel_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	const struct iphdr  *tiph = &tunnel->parms.iph;

	if (unlikely(skb->protocol != htons(ETH_P_IP)))
		goto tx_error;

	if (likely(!skb->encapsulation)) {
		skb_reset_inner_headers(skb);
		skb->encapsulation = 1;
	}

	ip_tunnel_xmit(skb, dev, tiph);
	return NETDEV_TX_OK;

tx_error:
	dev->stats.tx_errors++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int
ipip_tunnel_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int err = 0;
	struct ip_tunnel_parm p;

	if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p)))
		return -EFAULT;

	if (cmd == SIOCADDTUNNEL || cmd == SIOCCHGTUNNEL) {
		if (p.iph.version != 4 || p.iph.protocol != IPPROTO_IPIP ||
		    p.iph.ihl != 5 || (p.iph.frag_off&htons(~IP_DF)))
			return -EINVAL;
	}

	p.i_key = p.o_key = p.i_flags = p.o_flags = 0;
	if (p.iph.ttl)
		p.iph.frag_off |= htons(IP_DF);

	err = ip_tunnel_ioctl(dev, &p, cmd);
	if (err)
		return err;

	if (copy_to_user(ifr->ifr_ifru.ifru_data, &p, sizeof(p)))
		return -EFAULT;

	return 0;
}

static const struct net_device_ops ipip_netdev_ops = {
	.ndo_init       = ipip_tunnel_init,
	.ndo_uninit     = ip_tunnel_uninit,
	.ndo_start_xmit	= ipip_tunnel_xmit,
	.ndo_do_ioctl	= ipip_tunnel_ioctl,
	.ndo_change_mtu = ip_tunnel_change_mtu,
	.ndo_get_stats64 = ip_tunnel_get_stats64,
};

#define IPIP_FEATURES (NETIF_F_SG |		\
		       NETIF_F_FRAGLIST |	\
		       NETIF_F_HIGHDMA |	\
		       NETIF_F_HW_CSUM)

static void ipip_tunnel_setup(struct net_device *dev)
{
	dev->netdev_ops		= &ipip_netdev_ops;

	dev->type		= ARPHRD_TUNNEL;
	dev->flags		= IFF_NOARP;
	dev->iflink		= 0;
	dev->addr_len		= 4;
	dev->features		|= NETIF_F_NETNS_LOCAL;
	dev->features		|= NETIF_F_LLTX;
	dev->priv_flags		&= ~IFF_XMIT_DST_RELEASE;

	dev->features		|= IPIP_FEATURES;
	dev->hw_features	|= IPIP_FEATURES;
	ip_tunnel_setup(dev, ipip_net_id);
}

static int ipip_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);

	memcpy(dev->dev_addr, &tunnel->parms.iph.saddr, 4);
	memcpy(dev->broadcast, &tunnel->parms.iph.daddr, 4);

	tunnel->hlen = 0;
	tunnel->parms.iph.protocol = IPPROTO_IPIP;
	return ip_tunnel_init(dev);
}

static void ipip_netlink_parms(struct nlattr *data[],
			       struct ip_tunnel_parm *parms)
{
	memset(parms, 0, sizeof(*parms));

	parms->iph.version = 4;
	parms->iph.protocol = IPPROTO_IPIP;
	parms->iph.ihl = 5;

	if (!data)
		return;

	if (data[IFLA_IPTUN_LINK])
		parms->link = nla_get_u32(data[IFLA_IPTUN_LINK]);

	if (data[IFLA_IPTUN_LOCAL])
		parms->iph.saddr = nla_get_be32(data[IFLA_IPTUN_LOCAL]);

	if (data[IFLA_IPTUN_REMOTE])
		parms->iph.daddr = nla_get_be32(data[IFLA_IPTUN_REMOTE]);

	if (data[IFLA_IPTUN_TTL]) {
		parms->iph.ttl = nla_get_u8(data[IFLA_IPTUN_TTL]);
		if (parms->iph.ttl)
			parms->iph.frag_off = htons(IP_DF);
	}

	if (data[IFLA_IPTUN_TOS])
		parms->iph.tos = nla_get_u8(data[IFLA_IPTUN_TOS]);

	if (!data[IFLA_IPTUN_PMTUDISC] || nla_get_u8(data[IFLA_IPTUN_PMTUDISC]))
		parms->iph.frag_off = htons(IP_DF);
}

static int ipip_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[])
{
	struct ip_tunnel_parm p;

	ipip_netlink_parms(data, &p);
	return ip_tunnel_newlink(dev, tb, &p);
}

static int ipip_changelink(struct net_device *dev, struct nlattr *tb[],
			   struct nlattr *data[])
{
	struct ip_tunnel_parm p;

	ipip_netlink_parms(data, &p);

	if (((dev->flags & IFF_POINTOPOINT) && !p.iph.daddr) ||
	    (!(dev->flags & IFF_POINTOPOINT) && p.iph.daddr))
		return -EINVAL;

	return ip_tunnel_changelink(dev, tb, &p);
}

static size_t ipip_get_size(const struct net_device *dev)
{
	return
		/* IFLA_IPTUN_LINK */
		nla_total_size(4) +
		/* IFLA_IPTUN_LOCAL */
		nla_total_size(4) +
		/* IFLA_IPTUN_REMOTE */
		nla_total_size(4) +
		/* IFLA_IPTUN_TTL */
		nla_total_size(1) +
		/* IFLA_IPTUN_TOS */
		nla_total_size(1) +
		/* IFLA_IPTUN_PMTUDISC */
		nla_total_size(1) +
		0;
}

static int ipip_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct ip_tunnel_parm *parm = &tunnel->parms;

	if (nla_put_u32(skb, IFLA_IPTUN_LINK, parm->link) ||
	    nla_put_be32(skb, IFLA_IPTUN_LOCAL, parm->iph.saddr) ||
	    nla_put_be32(skb, IFLA_IPTUN_REMOTE, parm->iph.daddr) ||
	    nla_put_u8(skb, IFLA_IPTUN_TTL, parm->iph.ttl) ||
	    nla_put_u8(skb, IFLA_IPTUN_TOS, parm->iph.tos) ||
	    nla_put_u8(skb, IFLA_IPTUN_PMTUDISC,
		       !!(parm->iph.frag_off & htons(IP_DF))))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static const struct nla_policy ipip_policy[IFLA_IPTUN_MAX + 1] = {
	[IFLA_IPTUN_LINK]		= { .type = NLA_U32 },
	[IFLA_IPTUN_LOCAL]		= { .type = NLA_U32 },
	[IFLA_IPTUN_REMOTE]		= { .type = NLA_U32 },
	[IFLA_IPTUN_TTL]		= { .type = NLA_U8 },
	[IFLA_IPTUN_TOS]		= { .type = NLA_U8 },
	[IFLA_IPTUN_PMTUDISC]		= { .type = NLA_U8 },
};

static struct rtnl_link_ops ipip_link_ops __read_mostly = {
	.kind		= "ipip",
	.maxtype	= IFLA_IPTUN_MAX,
	.policy		= ipip_policy,
	.priv_size	= sizeof(struct ip_tunnel),
	.setup		= ipip_tunnel_setup,
	.newlink	= ipip_newlink,
	.changelink	= ipip_changelink,
	.dellink	= ip_tunnel_dellink,
	.get_size	= ipip_get_size,
	.fill_info	= ipip_fill_info,
};

static struct xfrm_tunnel ipip_handler __read_mostly = {
	.handler	=	ipip_rcv,
	.err_handler	=	ipip_err,
	.priority	=	1,
};

static int __net_init ipip_init_net(struct net *net)
{
	return ip_tunnel_init_net(net, ipip_net_id, &ipip_link_ops, "tunl0");
}

static void __net_exit ipip_exit_net(struct net *net)
{
	struct ip_tunnel_net *itn = net_generic(net, ipip_net_id);
	ip_tunnel_delete_net(itn);
}

static struct pernet_operations ipip_net_ops = {
	.init = ipip_init_net,
	.exit = ipip_exit_net,
	.id   = &ipip_net_id,
	.size = sizeof(struct ip_tunnel_net),
};

static int __init ipip_init(void)
{
	int err;

	pr_info("ipip: IPv4 over IPv4 tunneling driver\n");

	err = register_pernet_device(&ipip_net_ops);
	if (err < 0)
		return err;
	err = xfrm4_tunnel_register(&ipip_handler, AF_INET);
	if (err < 0) {
		pr_info("%s: can't register tunnel\n", __func__);
		goto xfrm_tunnel_failed;
	}
	err = rtnl_link_register(&ipip_link_ops);
	if (err < 0)
		goto rtnl_link_failed;

out:
	return err;

rtnl_link_failed:
	xfrm4_tunnel_deregister(&ipip_handler, AF_INET);
xfrm_tunnel_failed:
	unregister_pernet_device(&ipip_net_ops);
	goto out;
}

static void __exit ipip_fini(void)
{
	rtnl_link_unregister(&ipip_link_ops);
	if (xfrm4_tunnel_deregister(&ipip_handler, AF_INET))
		pr_info("%s: can't deregister tunnel\n", __func__);

	unregister_pernet_device(&ipip_net_ops);
}

module_init(ipip_init);
module_exit(ipip_fini);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETDEV("tunl0");
