/*
 *	IPv6 over IPv4 tunnel device - Simple Internet Transition (SIT)
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Alexey Kuznetsov	<kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	Changes:
 * Roger Venning <r.venning@telstra.com>:	6to4 support
 * Nate Thompson <nate@thebog.net>:		6to4 support
 * Fred Templin <fred.l.templin@boeing.com>:	isatap support
 * Sascha Hlusiak <mail@saschahlusiak.de>:	stateless autoconf for isatap
 */

#include <linux/module.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/icmp.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/netfilter_ipv4.h>
#include <linux/if_ether.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <net/ipip.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>
#include <net/dsfield.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

/*
   This version of net/ipv6/sit.c is cloned of net/ipv4/ip_gre.c

   For comments look at net/ipv4/ip_gre.c --ANK
 */

#define HASH_SIZE  16
#define HASH(addr) (((__force u32)addr^((__force u32)addr>>4))&0xF)

static void ipip6_fb_tunnel_init(struct net_device *dev);
static void ipip6_tunnel_init(struct net_device *dev);
static void ipip6_tunnel_setup(struct net_device *dev);

static int sit_net_id;
struct sit_net {
	struct ip_tunnel *tunnels_r_l[HASH_SIZE];
	struct ip_tunnel *tunnels_r[HASH_SIZE];
	struct ip_tunnel *tunnels_l[HASH_SIZE];
	struct ip_tunnel *tunnels_wc[1];
	struct ip_tunnel **tunnels[4];

	struct net_device *fb_tunnel_dev;
};

static DEFINE_RWLOCK(ipip6_lock);

static struct ip_tunnel * ipip6_tunnel_lookup(struct net *net,
		struct net_device *dev, __be32 remote, __be32 local)
{
	unsigned h0 = HASH(remote);
	unsigned h1 = HASH(local);
	struct ip_tunnel *t;
	struct sit_net *sitn = net_generic(net, sit_net_id);

	for (t = sitn->tunnels_r_l[h0^h1]; t; t = t->next) {
		if (local == t->parms.iph.saddr &&
		    remote == t->parms.iph.daddr &&
		    (!dev || !t->parms.link || dev->iflink == t->parms.link) &&
		    (t->dev->flags & IFF_UP))
			return t;
	}
	for (t = sitn->tunnels_r[h0]; t; t = t->next) {
		if (remote == t->parms.iph.daddr &&
		    (!dev || !t->parms.link || dev->iflink == t->parms.link) &&
		    (t->dev->flags & IFF_UP))
			return t;
	}
	for (t = sitn->tunnels_l[h1]; t; t = t->next) {
		if (local == t->parms.iph.saddr &&
		    (!dev || !t->parms.link || dev->iflink == t->parms.link) &&
		    (t->dev->flags & IFF_UP))
			return t;
	}
	t = sitn->tunnels_wc[0];
	if ((t != NULL) && (t->dev->flags & IFF_UP))
		return t;
	return NULL;
}

static struct ip_tunnel **__ipip6_bucket(struct sit_net *sitn,
		struct ip_tunnel_parm *parms)
{
	__be32 remote = parms->iph.daddr;
	__be32 local = parms->iph.saddr;
	unsigned h = 0;
	int prio = 0;

	if (remote) {
		prio |= 2;
		h ^= HASH(remote);
	}
	if (local) {
		prio |= 1;
		h ^= HASH(local);
	}
	return &sitn->tunnels[prio][h];
}

static inline struct ip_tunnel **ipip6_bucket(struct sit_net *sitn,
		struct ip_tunnel *t)
{
	return __ipip6_bucket(sitn, &t->parms);
}

static void ipip6_tunnel_unlink(struct sit_net *sitn, struct ip_tunnel *t)
{
	struct ip_tunnel **tp;

	for (tp = ipip6_bucket(sitn, t); *tp; tp = &(*tp)->next) {
		if (t == *tp) {
			write_lock_bh(&ipip6_lock);
			*tp = t->next;
			write_unlock_bh(&ipip6_lock);
			break;
		}
	}
}

static void ipip6_tunnel_link(struct sit_net *sitn, struct ip_tunnel *t)
{
	struct ip_tunnel **tp = ipip6_bucket(sitn, t);

	t->next = *tp;
	write_lock_bh(&ipip6_lock);
	*tp = t;
	write_unlock_bh(&ipip6_lock);
}

static struct ip_tunnel * ipip6_tunnel_locate(struct net *net,
		struct ip_tunnel_parm *parms, int create)
{
	__be32 remote = parms->iph.daddr;
	__be32 local = parms->iph.saddr;
	struct ip_tunnel *t, **tp, *nt;
	struct net_device *dev;
	char name[IFNAMSIZ];
	struct sit_net *sitn = net_generic(net, sit_net_id);

	for (tp = __ipip6_bucket(sitn, parms); (t = *tp) != NULL; tp = &t->next) {
		if (local == t->parms.iph.saddr &&
		    remote == t->parms.iph.daddr &&
		    parms->link == t->parms.link) {
			if (create)
				return NULL;
			else
				return t;
		}
	}
	if (!create)
		goto failed;

	if (parms->name[0])
		strlcpy(name, parms->name, IFNAMSIZ);
	else
		sprintf(name, "sit%%d");

	dev = alloc_netdev(sizeof(*t), name, ipip6_tunnel_setup);
	if (dev == NULL)
		return NULL;

	dev_net_set(dev, net);

	if (strchr(name, '%')) {
		if (dev_alloc_name(dev, name) < 0)
			goto failed_free;
	}

	nt = netdev_priv(dev);

	nt->parms = *parms;
	ipip6_tunnel_init(dev);

	if (parms->i_flags & SIT_ISATAP)
		dev->priv_flags |= IFF_ISATAP;

	if (register_netdevice(dev) < 0)
		goto failed_free;

	dev_hold(dev);

	ipip6_tunnel_link(sitn, nt);
	return nt;

failed_free:
	free_netdev(dev);
failed:
	return NULL;
}

static void ipip6_tunnel_rs_timer(unsigned long data)
{
	struct ip_tunnel_prl_entry *p = (struct ip_tunnel_prl_entry *) data;
	struct inet6_dev *ifp;
	struct inet6_ifaddr *addr;

	spin_lock(&p->lock);
	ifp = __in6_dev_get(p->tunnel->dev);

	read_lock_bh(&ifp->lock);
	for (addr = ifp->addr_list; addr; addr = addr->if_next) {
		struct in6_addr rtr;

		if (!(ipv6_addr_type(&addr->addr) & IPV6_ADDR_LINKLOCAL))
			continue;

		/* Send RS to guessed linklocal address of router
		 *
		 * Better: send to ff02::2 encapsuled in unicast directly
		 * to router-v4 instead of guessing the v6 address.
		 *
		 * Cisco/Windows seem to not set the u/l bit correctly,
		 * so we won't guess right.
		 */
		ipv6_addr_set(&rtr,  htonl(0xFE800000), 0, 0, 0);
		if (!__ipv6_isatap_ifid(rtr.s6_addr + 8,
					p->addr)) {
			ndisc_send_rs(p->tunnel->dev, &addr->addr, &rtr);
		}
	}
	read_unlock_bh(&ifp->lock);

	mod_timer(&p->rs_timer, jiffies + HZ * p->rs_delay);
	spin_unlock(&p->lock);

	return;
}

static struct ip_tunnel_prl_entry *
__ipip6_tunnel_locate_prl(struct ip_tunnel *t, __be32 addr)
{
	struct ip_tunnel_prl_entry *p = (struct ip_tunnel_prl_entry *)NULL;

	for (p = t->prl; p; p = p->next)
		if (p->addr == addr)
			break;
	return p;

}

static int ipip6_tunnel_get_prl(struct ip_tunnel *t,
				struct ip_tunnel_prl __user *a)
{
	struct ip_tunnel_prl kprl, *kp;
	struct ip_tunnel_prl_entry *prl;
	unsigned int cmax, c = 0, ca, len;
	int ret = 0;

	if (copy_from_user(&kprl, a, sizeof(kprl)))
		return -EFAULT;
	cmax = kprl.datalen / sizeof(kprl);
	if (cmax > 1 && kprl.addr != htonl(INADDR_ANY))
		cmax = 1;

	/* For simple GET or for root users,
	 * we try harder to allocate.
	 */
	kp = (cmax <= 1 || capable(CAP_NET_ADMIN)) ?
		kcalloc(cmax, sizeof(*kp), GFP_KERNEL) :
		NULL;

	read_lock(&ipip6_lock);

	ca = t->prl_count < cmax ? t->prl_count : cmax;

	if (!kp) {
		/* We don't try hard to allocate much memory for
		 * non-root users.
		 * For root users, retry allocating enough memory for
		 * the answer.
		 */
		kp = kcalloc(ca, sizeof(*kp), GFP_ATOMIC);
		if (!kp) {
			ret = -ENOMEM;
			goto out;
		}
	}

	c = 0;
	for (prl = t->prl; prl; prl = prl->next) {
		if (c > cmax)
			break;
		if (kprl.addr != htonl(INADDR_ANY) && prl->addr != kprl.addr)
			continue;
		kp[c].addr = prl->addr;
		kp[c].flags = prl->flags;
		kp[c].rs_delay = prl->rs_delay;
		c++;
		if (kprl.addr != htonl(INADDR_ANY))
			break;
	}
out:
	read_unlock(&ipip6_lock);

	len = sizeof(*kp) * c;
	ret = 0;
	if ((len && copy_to_user(a + 1, kp, len)) || put_user(len, &a->datalen))
		ret = -EFAULT;

	kfree(kp);

	return ret;
}

static int
ipip6_tunnel_add_prl(struct ip_tunnel *t, struct ip_tunnel_prl *a, int chg)
{
	struct ip_tunnel_prl_entry *p;
	int err = 0;

	if (a->addr == htonl(INADDR_ANY))
		return -EINVAL;

	write_lock(&ipip6_lock);

	for (p = t->prl; p; p = p->next) {
		if (p->addr == a->addr) {
			if (chg)
				goto update;
			err = -EEXIST;
			goto out;
		}
	}

	if (chg) {
		err = -ENXIO;
		goto out;
	}

	p = kzalloc(sizeof(struct ip_tunnel_prl_entry), GFP_KERNEL);
	if (!p) {
		err = -ENOBUFS;
		goto out;
	}

	p->next = t->prl;
	p->tunnel = t;
	t->prl = p;
	t->prl_count++;

	spin_lock_init(&p->lock);
	setup_timer(&p->rs_timer, ipip6_tunnel_rs_timer, (unsigned long) p);
update:
	p->addr = a->addr;
	p->flags = a->flags;
	p->rs_delay = a->rs_delay;
	if (p->rs_delay == 0)
		p->rs_delay = IPTUNNEL_RS_DEFAULT_DELAY;
	spin_lock(&p->lock);
	del_timer(&p->rs_timer);
	if (p->flags & PRL_DEFAULT)
		mod_timer(&p->rs_timer, jiffies + 1);
	spin_unlock(&p->lock);
out:
	write_unlock(&ipip6_lock);
	return err;
}

static int
ipip6_tunnel_del_prl(struct ip_tunnel *t, struct ip_tunnel_prl *a)
{
	struct ip_tunnel_prl_entry *x, **p;
	int err = 0;

	write_lock(&ipip6_lock);

	if (a && a->addr != htonl(INADDR_ANY)) {
		for (p = &t->prl; *p; p = &(*p)->next) {
			if ((*p)->addr == a->addr) {
				x = *p;
				*p = x->next;
				spin_lock(&x->lock);
				del_timer(&x->rs_timer);
				spin_unlock(&x->lock);
				kfree(x);
				t->prl_count--;
				goto out;
			}
		}
		err = -ENXIO;
	} else {
		while (t->prl) {
			x = t->prl;
			t->prl = t->prl->next;
			spin_lock(&x->lock);
			del_timer(&x->rs_timer);
			spin_unlock(&x->lock);
			kfree(x);
			t->prl_count--;
		}
	}
out:
	write_unlock(&ipip6_lock);
	return err;
}

static int
isatap_chksrc(struct sk_buff *skb, struct iphdr *iph, struct ip_tunnel *t)
{
	struct ip_tunnel_prl_entry *p;
	int ok = 1;

	read_lock(&ipip6_lock);
	p = __ipip6_tunnel_locate_prl(t, iph->saddr);
	if (p) {
		if (p->flags & PRL_DEFAULT)
			skb->ndisc_nodetype = NDISC_NODETYPE_DEFAULT;
		else
			skb->ndisc_nodetype = NDISC_NODETYPE_NODEFAULT;
	} else {
		struct in6_addr *addr6 = &ipv6_hdr(skb)->saddr;
		if (ipv6_addr_is_isatap(addr6) &&
		    (addr6->s6_addr32[3] == iph->saddr) &&
		    ipv6_chk_prefix(addr6, t->dev))
			skb->ndisc_nodetype = NDISC_NODETYPE_HOST;
		else
			ok = 0;
	}
	read_unlock(&ipip6_lock);
	return ok;
}

static void ipip6_tunnel_uninit(struct net_device *dev)
{
	struct net *net = dev_net(dev);
	struct sit_net *sitn = net_generic(net, sit_net_id);

	if (dev == sitn->fb_tunnel_dev) {
		write_lock_bh(&ipip6_lock);
		sitn->tunnels_wc[0] = NULL;
		write_unlock_bh(&ipip6_lock);
		dev_put(dev);
	} else {
		ipip6_tunnel_unlink(sitn, netdev_priv(dev));
		ipip6_tunnel_del_prl(netdev_priv(dev), NULL);
		dev_put(dev);
	}
}


static int ipip6_err(struct sk_buff *skb, u32 info)
{

/* All the routers (except for Linux) return only
   8 bytes of packet payload. It means, that precise relaying of
   ICMP in the real Internet is absolutely infeasible.
 */
	struct iphdr *iph = (struct iphdr*)skb->data;
	const int type = icmp_hdr(skb)->type;
	const int code = icmp_hdr(skb)->code;
	struct ip_tunnel *t;
	int err;

	switch (type) {
	default:
	case ICMP_PARAMETERPROB:
		return 0;

	case ICMP_DEST_UNREACH:
		switch (code) {
		case ICMP_SR_FAILED:
		case ICMP_PORT_UNREACH:
			/* Impossible event. */
			return 0;
		case ICMP_FRAG_NEEDED:
			/* Soft state for pmtu is maintained by IP core. */
			return 0;
		default:
			/* All others are translated to HOST_UNREACH.
			   rfc2003 contains "deep thoughts" about NET_UNREACH,
			   I believe they are just ether pollution. --ANK
			 */
			break;
		}
		break;
	case ICMP_TIME_EXCEEDED:
		if (code != ICMP_EXC_TTL)
			return 0;
		break;
	}

	err = -ENOENT;

	read_lock(&ipip6_lock);
	t = ipip6_tunnel_lookup(dev_net(skb->dev),
				skb->dev,
				iph->daddr,
				iph->saddr);
	if (t == NULL || t->parms.iph.daddr == 0)
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
	read_unlock(&ipip6_lock);
	return err;
}

static inline void ipip6_ecn_decapsulate(struct iphdr *iph, struct sk_buff *skb)
{
	if (INET_ECN_is_ce(iph->tos))
		IP6_ECN_set_ce(ipv6_hdr(skb));
}

static int ipip6_rcv(struct sk_buff *skb)
{
	struct iphdr *iph;
	struct ip_tunnel *tunnel;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto out;

	iph = ip_hdr(skb);

	read_lock(&ipip6_lock);
	tunnel = ipip6_tunnel_lookup(dev_net(skb->dev), skb->dev,
				     iph->saddr, iph->daddr);
	if (tunnel != NULL) {
		secpath_reset(skb);
		skb->mac_header = skb->network_header;
		skb_reset_network_header(skb);
		IPCB(skb)->flags = 0;
		skb->protocol = htons(ETH_P_IPV6);
		skb->pkt_type = PACKET_HOST;

		if ((tunnel->dev->priv_flags & IFF_ISATAP) &&
		    !isatap_chksrc(skb, iph, tunnel)) {
			tunnel->dev->stats.rx_errors++;
			read_unlock(&ipip6_lock);
			kfree_skb(skb);
			return 0;
		}
		tunnel->dev->stats.rx_packets++;
		tunnel->dev->stats.rx_bytes += skb->len;
		skb->dev = tunnel->dev;
		skb_dst_drop(skb);
		nf_reset(skb);
		ipip6_ecn_decapsulate(iph, skb);
		netif_rx(skb);
		read_unlock(&ipip6_lock);
		return 0;
	}

	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);
	read_unlock(&ipip6_lock);
out:
	kfree_skb(skb);
	return 0;
}

/* Returns the embedded IPv4 address if the IPv6 address
   comes from 6to4 (RFC 3056) addr space */

static inline __be32 try_6to4(struct in6_addr *v6dst)
{
	__be32 dst = 0;

	if (v6dst->s6_addr16[0] == htons(0x2002)) {
		/* 6to4 v6 addr has 16 bits prefix, 32 v4addr, 16 SLA, ... */
		memcpy(&dst, &v6dst->s6_addr16[1], 4);
	}
	return dst;
}

/*
 *	This function assumes it is being called from dev_queue_xmit()
 *	and that skb is filled properly by that function.
 */

static int ipip6_tunnel_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct net_device_stats *stats = &tunnel->dev->stats;
	struct iphdr  *tiph = &tunnel->parms.iph;
	struct ipv6hdr *iph6 = ipv6_hdr(skb);
	u8     tos = tunnel->parms.iph.tos;
	struct rtable *rt;     			/* Route to the other host */
	struct net_device *tdev;			/* Device to other host */
	struct iphdr  *iph;			/* Our new IP header */
	unsigned int max_headroom;		/* The extra header space needed */
	__be32 dst = tiph->daddr;
	int    mtu;
	struct in6_addr *addr6;
	int addr_type;

	if (tunnel->recursion++) {
		stats->collisions++;
		goto tx_error;
	}

	if (skb->protocol != htons(ETH_P_IPV6))
		goto tx_error;

	/* ISATAP (RFC4214) - must come before 6to4 */
	if (dev->priv_flags & IFF_ISATAP) {
		struct neighbour *neigh = NULL;

		if (skb_dst(skb))
			neigh = skb_dst(skb)->neighbour;

		if (neigh == NULL) {
			if (net_ratelimit())
				printk(KERN_DEBUG "sit: nexthop == NULL\n");
			goto tx_error;
		}

		addr6 = (struct in6_addr*)&neigh->primary_key;
		addr_type = ipv6_addr_type(addr6);

		if ((addr_type & IPV6_ADDR_UNICAST) &&
		     ipv6_addr_is_isatap(addr6))
			dst = addr6->s6_addr32[3];
		else
			goto tx_error;
	}

	if (!dst)
		dst = try_6to4(&iph6->daddr);

	if (!dst) {
		struct neighbour *neigh = NULL;

		if (skb_dst(skb))
			neigh = skb_dst(skb)->neighbour;

		if (neigh == NULL) {
			if (net_ratelimit())
				printk(KERN_DEBUG "sit: nexthop == NULL\n");
			goto tx_error;
		}

		addr6 = (struct in6_addr*)&neigh->primary_key;
		addr_type = ipv6_addr_type(addr6);

		if (addr_type == IPV6_ADDR_ANY) {
			addr6 = &ipv6_hdr(skb)->daddr;
			addr_type = ipv6_addr_type(addr6);
		}

		if ((addr_type & IPV6_ADDR_COMPATv4) == 0)
			goto tx_error_icmp;

		dst = addr6->s6_addr32[3];
	}

	{
		struct flowi fl = { .nl_u = { .ip4_u =
					      { .daddr = dst,
						.saddr = tiph->saddr,
						.tos = RT_TOS(tos) } },
				    .oif = tunnel->parms.link,
				    .proto = IPPROTO_IPV6 };
		if (ip_route_output_key(dev_net(dev), &rt, &fl)) {
			stats->tx_carrier_errors++;
			goto tx_error_icmp;
		}
	}
	if (rt->rt_type != RTN_UNICAST) {
		ip_rt_put(rt);
		stats->tx_carrier_errors++;
		goto tx_error_icmp;
	}
	tdev = rt->u.dst.dev;

	if (tdev == dev) {
		ip_rt_put(rt);
		stats->collisions++;
		goto tx_error;
	}

	if (tiph->frag_off)
		mtu = dst_mtu(&rt->u.dst) - sizeof(struct iphdr);
	else
		mtu = skb_dst(skb) ? dst_mtu(skb_dst(skb)) : dev->mtu;

	if (mtu < 68) {
		stats->collisions++;
		ip_rt_put(rt);
		goto tx_error;
	}
	if (mtu < IPV6_MIN_MTU)
		mtu = IPV6_MIN_MTU;
	if (tunnel->parms.iph.daddr && skb_dst(skb))
		skb_dst(skb)->ops->update_pmtu(skb_dst(skb), mtu);

	if (skb->len > mtu) {
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu, dev);
		ip_rt_put(rt);
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

	/*
	 * Okay, now see if we can stuff it in the buffer as-is.
	 */
	max_headroom = LL_RESERVED_SPACE(tdev)+sizeof(struct iphdr);

	if (skb_headroom(skb) < max_headroom || skb_shared(skb) ||
	    (skb_cloned(skb) && !skb_clone_writable(skb, 0))) {
		struct sk_buff *new_skb = skb_realloc_headroom(skb, max_headroom);
		if (!new_skb) {
			ip_rt_put(rt);
			stats->tx_dropped++;
			dev_kfree_skb(skb);
			tunnel->recursion--;
			return 0;
		}
		if (skb->sk)
			skb_set_owner_w(new_skb, skb->sk);
		dev_kfree_skb(skb);
		skb = new_skb;
		iph6 = ipv6_hdr(skb);
	}

	skb->transport_header = skb->network_header;
	skb_push(skb, sizeof(struct iphdr));
	skb_reset_network_header(skb);
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
	IPCB(skb)->flags = 0;
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->u.dst);

	/*
	 *	Push down and install the IPIP header.
	 */

	iph 			=	ip_hdr(skb);
	iph->version		=	4;
	iph->ihl		=	sizeof(struct iphdr)>>2;
	if (mtu > IPV6_MIN_MTU)
		iph->frag_off	=	htons(IP_DF);
	else
		iph->frag_off	=	0;

	iph->protocol		=	IPPROTO_IPV6;
	iph->tos		=	INET_ECN_encapsulate(tos, ipv6_get_dsfield(iph6));
	iph->daddr		=	rt->rt_dst;
	iph->saddr		=	rt->rt_src;

	if ((iph->ttl = tiph->ttl) == 0)
		iph->ttl	=	iph6->hop_limit;

	nf_reset(skb);

	IPTUNNEL_XMIT();
	tunnel->recursion--;
	return 0;

tx_error_icmp:
	dst_link_failure(skb);
tx_error:
	stats->tx_errors++;
	dev_kfree_skb(skb);
	tunnel->recursion--;
	return 0;
}

static void ipip6_tunnel_bind_dev(struct net_device *dev)
{
	struct net_device *tdev = NULL;
	struct ip_tunnel *tunnel;
	struct iphdr *iph;

	tunnel = netdev_priv(dev);
	iph = &tunnel->parms.iph;

	if (iph->daddr) {
		struct flowi fl = { .nl_u = { .ip4_u =
					      { .daddr = iph->daddr,
						.saddr = iph->saddr,
						.tos = RT_TOS(iph->tos) } },
				    .oif = tunnel->parms.link,
				    .proto = IPPROTO_IPV6 };
		struct rtable *rt;
		if (!ip_route_output_key(dev_net(dev), &rt, &fl)) {
			tdev = rt->u.dst.dev;
			ip_rt_put(rt);
		}
		dev->flags |= IFF_POINTOPOINT;
	}

	if (!tdev && tunnel->parms.link)
		tdev = __dev_get_by_index(dev_net(dev), tunnel->parms.link);

	if (tdev) {
		dev->hard_header_len = tdev->hard_header_len + sizeof(struct iphdr);
		dev->mtu = tdev->mtu - sizeof(struct iphdr);
		if (dev->mtu < IPV6_MIN_MTU)
			dev->mtu = IPV6_MIN_MTU;
	}
	dev->iflink = tunnel->parms.link;
}

static int
ipip6_tunnel_ioctl (struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int err = 0;
	struct ip_tunnel_parm p;
	struct ip_tunnel_prl prl;
	struct ip_tunnel *t;
	struct net *net = dev_net(dev);
	struct sit_net *sitn = net_generic(net, sit_net_id);

	switch (cmd) {
	case SIOCGETTUNNEL:
		t = NULL;
		if (dev == sitn->fb_tunnel_dev) {
			if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p))) {
				err = -EFAULT;
				break;
			}
			t = ipip6_tunnel_locate(net, &p, 0);
		}
		if (t == NULL)
			t = netdev_priv(dev);
		memcpy(&p, &t->parms, sizeof(p));
		if (copy_to_user(ifr->ifr_ifru.ifru_data, &p, sizeof(p)))
			err = -EFAULT;
		break;

	case SIOCADDTUNNEL:
	case SIOCCHGTUNNEL:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			goto done;

		err = -EFAULT;
		if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p)))
			goto done;

		err = -EINVAL;
		if (p.iph.version != 4 || p.iph.protocol != IPPROTO_IPV6 ||
		    p.iph.ihl != 5 || (p.iph.frag_off&htons(~IP_DF)))
			goto done;
		if (p.iph.ttl)
			p.iph.frag_off |= htons(IP_DF);

		t = ipip6_tunnel_locate(net, &p, cmd == SIOCADDTUNNEL);

		if (dev != sitn->fb_tunnel_dev && cmd == SIOCCHGTUNNEL) {
			if (t != NULL) {
				if (t->dev != dev) {
					err = -EEXIST;
					break;
				}
			} else {
				if (((dev->flags&IFF_POINTOPOINT) && !p.iph.daddr) ||
				    (!(dev->flags&IFF_POINTOPOINT) && p.iph.daddr)) {
					err = -EINVAL;
					break;
				}
				t = netdev_priv(dev);
				ipip6_tunnel_unlink(sitn, t);
				t->parms.iph.saddr = p.iph.saddr;
				t->parms.iph.daddr = p.iph.daddr;
				memcpy(dev->dev_addr, &p.iph.saddr, 4);
				memcpy(dev->broadcast, &p.iph.daddr, 4);
				ipip6_tunnel_link(sitn, t);
				netdev_state_change(dev);
			}
		}

		if (t) {
			err = 0;
			if (cmd == SIOCCHGTUNNEL) {
				t->parms.iph.ttl = p.iph.ttl;
				t->parms.iph.tos = p.iph.tos;
				if (t->parms.link != p.link) {
					t->parms.link = p.link;
					ipip6_tunnel_bind_dev(dev);
					netdev_state_change(dev);
				}
			}
			if (copy_to_user(ifr->ifr_ifru.ifru_data, &t->parms, sizeof(p)))
				err = -EFAULT;
		} else
			err = (cmd == SIOCADDTUNNEL ? -ENOBUFS : -ENOENT);
		break;

	case SIOCDELTUNNEL:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			goto done;

		if (dev == sitn->fb_tunnel_dev) {
			err = -EFAULT;
			if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p)))
				goto done;
			err = -ENOENT;
			if ((t = ipip6_tunnel_locate(net, &p, 0)) == NULL)
				goto done;
			err = -EPERM;
			if (t == netdev_priv(sitn->fb_tunnel_dev))
				goto done;
			dev = t->dev;
		}
		unregister_netdevice(dev);
		err = 0;
		break;

	case SIOCGETPRL:
		err = -EINVAL;
		if (dev == sitn->fb_tunnel_dev)
			goto done;
		err = -ENOENT;
		if (!(t = netdev_priv(dev)))
			goto done;
		err = ipip6_tunnel_get_prl(t, ifr->ifr_ifru.ifru_data);
		break;

	case SIOCADDPRL:
	case SIOCDELPRL:
	case SIOCCHGPRL:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			goto done;
		err = -EINVAL;
		if (dev == sitn->fb_tunnel_dev)
			goto done;
		err = -EFAULT;
		if (copy_from_user(&prl, ifr->ifr_ifru.ifru_data, sizeof(prl)))
			goto done;
		err = -ENOENT;
		if (!(t = netdev_priv(dev)))
			goto done;

		switch (cmd) {
		case SIOCDELPRL:
			err = ipip6_tunnel_del_prl(t, &prl);
			break;
		case SIOCADDPRL:
		case SIOCCHGPRL:
			err = ipip6_tunnel_add_prl(t, &prl, cmd == SIOCCHGPRL);
			break;
		}
		netdev_state_change(dev);
		break;

	default:
		err = -EINVAL;
	}

done:
	return err;
}

static int ipip6_tunnel_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < IPV6_MIN_MTU || new_mtu > 0xFFF8 - sizeof(struct iphdr))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops ipip6_netdev_ops = {
	.ndo_uninit	= ipip6_tunnel_uninit,
	.ndo_start_xmit	= ipip6_tunnel_xmit,
	.ndo_do_ioctl	= ipip6_tunnel_ioctl,
	.ndo_change_mtu	= ipip6_tunnel_change_mtu,
};

static void ipip6_tunnel_setup(struct net_device *dev)
{
	dev->netdev_ops		= &ipip6_netdev_ops;
	dev->destructor 	= free_netdev;

	dev->type		= ARPHRD_SIT;
	dev->hard_header_len 	= LL_MAX_HEADER + sizeof(struct iphdr);
	dev->mtu		= ETH_DATA_LEN - sizeof(struct iphdr);
	dev->flags		= IFF_NOARP;
	dev->iflink		= 0;
	dev->addr_len		= 4;
	dev->features		|= NETIF_F_NETNS_LOCAL;
}

static void ipip6_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);

	tunnel->dev = dev;
	strcpy(tunnel->parms.name, dev->name);

	memcpy(dev->dev_addr, &tunnel->parms.iph.saddr, 4);
	memcpy(dev->broadcast, &tunnel->parms.iph.daddr, 4);

	ipip6_tunnel_bind_dev(dev);
}

static void ipip6_fb_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct iphdr *iph = &tunnel->parms.iph;
	struct net *net = dev_net(dev);
	struct sit_net *sitn = net_generic(net, sit_net_id);

	tunnel->dev = dev;
	strcpy(tunnel->parms.name, dev->name);

	iph->version		= 4;
	iph->protocol		= IPPROTO_IPV6;
	iph->ihl		= 5;
	iph->ttl		= 64;

	dev_hold(dev);
	sitn->tunnels_wc[0]	= tunnel;
}

static struct xfrm_tunnel sit_handler = {
	.handler	=	ipip6_rcv,
	.err_handler	=	ipip6_err,
	.priority	=	1,
};

static void sit_destroy_tunnels(struct sit_net *sitn)
{
	int prio;

	for (prio = 1; prio < 4; prio++) {
		int h;
		for (h = 0; h < HASH_SIZE; h++) {
			struct ip_tunnel *t;
			while ((t = sitn->tunnels[prio][h]) != NULL)
				unregister_netdevice(t->dev);
		}
	}
}

static int sit_init_net(struct net *net)
{
	int err;
	struct sit_net *sitn;

	err = -ENOMEM;
	sitn = kzalloc(sizeof(struct sit_net), GFP_KERNEL);
	if (sitn == NULL)
		goto err_alloc;

	err = net_assign_generic(net, sit_net_id, sitn);
	if (err < 0)
		goto err_assign;

	sitn->tunnels[0] = sitn->tunnels_wc;
	sitn->tunnels[1] = sitn->tunnels_l;
	sitn->tunnels[2] = sitn->tunnels_r;
	sitn->tunnels[3] = sitn->tunnels_r_l;

	sitn->fb_tunnel_dev = alloc_netdev(sizeof(struct ip_tunnel), "sit0",
					   ipip6_tunnel_setup);
	if (!sitn->fb_tunnel_dev) {
		err = -ENOMEM;
		goto err_alloc_dev;
	}
	dev_net_set(sitn->fb_tunnel_dev, net);

	ipip6_fb_tunnel_init(sitn->fb_tunnel_dev);

	if ((err = register_netdev(sitn->fb_tunnel_dev)))
		goto err_reg_dev;

	return 0;

err_reg_dev:
	dev_put(sitn->fb_tunnel_dev);
	free_netdev(sitn->fb_tunnel_dev);
err_alloc_dev:
	/* nothing */
err_assign:
	kfree(sitn);
err_alloc:
	return err;
}

static void sit_exit_net(struct net *net)
{
	struct sit_net *sitn;

	sitn = net_generic(net, sit_net_id);
	rtnl_lock();
	sit_destroy_tunnels(sitn);
	unregister_netdevice(sitn->fb_tunnel_dev);
	rtnl_unlock();
	kfree(sitn);
}

static struct pernet_operations sit_net_ops = {
	.init = sit_init_net,
	.exit = sit_exit_net,
};

static void __exit sit_cleanup(void)
{
	xfrm4_tunnel_deregister(&sit_handler, AF_INET6);

	unregister_pernet_gen_device(sit_net_id, &sit_net_ops);
}

static int __init sit_init(void)
{
	int err;

	printk(KERN_INFO "IPv6 over IPv4 tunneling driver\n");

	if (xfrm4_tunnel_register(&sit_handler, AF_INET6) < 0) {
		printk(KERN_INFO "sit init: Can't add protocol\n");
		return -EAGAIN;
	}

	err = register_pernet_gen_device(&sit_net_id, &sit_net_ops);
	if (err < 0)
		xfrm4_tunnel_deregister(&sit_handler, AF_INET6);

	return err;
}

module_init(sit_init);
module_exit(sit_cleanup);
MODULE_LICENSE("GPL");
MODULE_ALIAS("sit0");
