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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/slab.h>
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
#include <net/ip_tunnels.h>
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

static bool log_ecn_error = true;
module_param(log_ecn_error, bool, 0644);
MODULE_PARM_DESC(log_ecn_error, "Log packets received with corrupted ECN");

static int ipip6_tunnel_init(struct net_device *dev);
static void ipip6_tunnel_setup(struct net_device *dev);
static void ipip6_dev_free(struct net_device *dev);
static bool check_6rd(struct ip_tunnel *tunnel, const struct in6_addr *v6dst,
		      __be32 *v4dst);
static struct rtnl_link_ops sit_link_ops __read_mostly;

static int sit_net_id __read_mostly;
struct sit_net {
	struct ip_tunnel __rcu *tunnels_r_l[HASH_SIZE];
	struct ip_tunnel __rcu *tunnels_r[HASH_SIZE];
	struct ip_tunnel __rcu *tunnels_l[HASH_SIZE];
	struct ip_tunnel __rcu *tunnels_wc[1];
	struct ip_tunnel __rcu **tunnels[4];

	struct net_device *fb_tunnel_dev;
};

/*
 * Must be invoked with rcu_read_lock
 */
static struct ip_tunnel *ipip6_tunnel_lookup(struct net *net,
		struct net_device *dev, __be32 remote, __be32 local)
{
	unsigned int h0 = HASH(remote);
	unsigned int h1 = HASH(local);
	struct ip_tunnel *t;
	struct sit_net *sitn = net_generic(net, sit_net_id);

	for_each_ip_tunnel_rcu(t, sitn->tunnels_r_l[h0 ^ h1]) {
		if (local == t->parms.iph.saddr &&
		    remote == t->parms.iph.daddr &&
		    (!dev || !t->parms.link || dev->iflink == t->parms.link) &&
		    (t->dev->flags & IFF_UP))
			return t;
	}
	for_each_ip_tunnel_rcu(t, sitn->tunnels_r[h0]) {
		if (remote == t->parms.iph.daddr &&
		    (!dev || !t->parms.link || dev->iflink == t->parms.link) &&
		    (t->dev->flags & IFF_UP))
			return t;
	}
	for_each_ip_tunnel_rcu(t, sitn->tunnels_l[h1]) {
		if (local == t->parms.iph.saddr &&
		    (!dev || !t->parms.link || dev->iflink == t->parms.link) &&
		    (t->dev->flags & IFF_UP))
			return t;
	}
	t = rcu_dereference(sitn->tunnels_wc[0]);
	if ((t != NULL) && (t->dev->flags & IFF_UP))
		return t;
	return NULL;
}

static struct ip_tunnel __rcu **__ipip6_bucket(struct sit_net *sitn,
		struct ip_tunnel_parm *parms)
{
	__be32 remote = parms->iph.daddr;
	__be32 local = parms->iph.saddr;
	unsigned int h = 0;
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

static inline struct ip_tunnel __rcu **ipip6_bucket(struct sit_net *sitn,
		struct ip_tunnel *t)
{
	return __ipip6_bucket(sitn, &t->parms);
}

static void ipip6_tunnel_unlink(struct sit_net *sitn, struct ip_tunnel *t)
{
	struct ip_tunnel __rcu **tp;
	struct ip_tunnel *iter;

	for (tp = ipip6_bucket(sitn, t);
	     (iter = rtnl_dereference(*tp)) != NULL;
	     tp = &iter->next) {
		if (t == iter) {
			rcu_assign_pointer(*tp, t->next);
			break;
		}
	}
}

static void ipip6_tunnel_link(struct sit_net *sitn, struct ip_tunnel *t)
{
	struct ip_tunnel __rcu **tp = ipip6_bucket(sitn, t);

	rcu_assign_pointer(t->next, rtnl_dereference(*tp));
	rcu_assign_pointer(*tp, t);
}

static void ipip6_tunnel_clone_6rd(struct net_device *dev, struct sit_net *sitn)
{
#ifdef CONFIG_IPV6_SIT_6RD
	struct ip_tunnel *t = netdev_priv(dev);

	if (t->dev == sitn->fb_tunnel_dev) {
		ipv6_addr_set(&t->ip6rd.prefix, htonl(0x20020000), 0, 0, 0);
		t->ip6rd.relay_prefix = 0;
		t->ip6rd.prefixlen = 16;
		t->ip6rd.relay_prefixlen = 0;
	} else {
		struct ip_tunnel *t0 = netdev_priv(sitn->fb_tunnel_dev);
		memcpy(&t->ip6rd, &t0->ip6rd, sizeof(t->ip6rd));
	}
#endif
}

static int ipip6_tunnel_create(struct net_device *dev)
{
	struct ip_tunnel *t = netdev_priv(dev);
	struct net *net = dev_net(dev);
	struct sit_net *sitn = net_generic(net, sit_net_id);
	int err;

	err = ipip6_tunnel_init(dev);
	if (err < 0)
		goto out;
	ipip6_tunnel_clone_6rd(dev, sitn);

	if ((__force u16)t->parms.i_flags & SIT_ISATAP)
		dev->priv_flags |= IFF_ISATAP;

	err = register_netdevice(dev);
	if (err < 0)
		goto out;

	strcpy(t->parms.name, dev->name);
	dev->rtnl_link_ops = &sit_link_ops;

	dev_hold(dev);

	ipip6_tunnel_link(sitn, t);
	return 0;

out:
	return err;
}

static struct ip_tunnel *ipip6_tunnel_locate(struct net *net,
		struct ip_tunnel_parm *parms, int create)
{
	__be32 remote = parms->iph.daddr;
	__be32 local = parms->iph.saddr;
	struct ip_tunnel *t, *nt;
	struct ip_tunnel __rcu **tp;
	struct net_device *dev;
	char name[IFNAMSIZ];
	struct sit_net *sitn = net_generic(net, sit_net_id);

	for (tp = __ipip6_bucket(sitn, parms);
	    (t = rtnl_dereference(*tp)) != NULL;
	     tp = &t->next) {
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
		strcpy(name, "sit%d");

	dev = alloc_netdev(sizeof(*t), name, ipip6_tunnel_setup);
	if (dev == NULL)
		return NULL;

	dev_net_set(dev, net);

	nt = netdev_priv(dev);

	nt->parms = *parms;
	if (ipip6_tunnel_create(dev) < 0)
		goto failed_free;

	return nt;

failed_free:
	ipip6_dev_free(dev);
failed:
	return NULL;
}

#define for_each_prl_rcu(start)			\
	for (prl = rcu_dereference(start);	\
	     prl;				\
	     prl = rcu_dereference(prl->next))

static struct ip_tunnel_prl_entry *
__ipip6_tunnel_locate_prl(struct ip_tunnel *t, __be32 addr)
{
	struct ip_tunnel_prl_entry *prl;

	for_each_prl_rcu(t->prl)
		if (prl->addr == addr)
			break;
	return prl;

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

	rcu_read_lock();

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
	for_each_prl_rcu(t->prl) {
		if (c >= cmax)
			break;
		if (kprl.addr != htonl(INADDR_ANY) && prl->addr != kprl.addr)
			continue;
		kp[c].addr = prl->addr;
		kp[c].flags = prl->flags;
		c++;
		if (kprl.addr != htonl(INADDR_ANY))
			break;
	}
out:
	rcu_read_unlock();

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

	ASSERT_RTNL();

	for (p = rtnl_dereference(t->prl); p; p = rtnl_dereference(p->next)) {
		if (p->addr == a->addr) {
			if (chg) {
				p->flags = a->flags;
				goto out;
			}
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
	p->addr = a->addr;
	p->flags = a->flags;
	t->prl_count++;
	rcu_assign_pointer(t->prl, p);
out:
	return err;
}

static void prl_list_destroy_rcu(struct rcu_head *head)
{
	struct ip_tunnel_prl_entry *p, *n;

	p = container_of(head, struct ip_tunnel_prl_entry, rcu_head);
	do {
		n = rcu_dereference_protected(p->next, 1);
		kfree(p);
		p = n;
	} while (p);
}

static int
ipip6_tunnel_del_prl(struct ip_tunnel *t, struct ip_tunnel_prl *a)
{
	struct ip_tunnel_prl_entry *x;
	struct ip_tunnel_prl_entry __rcu **p;
	int err = 0;

	ASSERT_RTNL();

	if (a && a->addr != htonl(INADDR_ANY)) {
		for (p = &t->prl;
		     (x = rtnl_dereference(*p)) != NULL;
		     p = &x->next) {
			if (x->addr == a->addr) {
				*p = x->next;
				kfree_rcu(x, rcu_head);
				t->prl_count--;
				goto out;
			}
		}
		err = -ENXIO;
	} else {
		x = rtnl_dereference(t->prl);
		if (x) {
			t->prl_count = 0;
			call_rcu(&x->rcu_head, prl_list_destroy_rcu);
			t->prl = NULL;
		}
	}
out:
	return err;
}

static int
isatap_chksrc(struct sk_buff *skb, const struct iphdr *iph, struct ip_tunnel *t)
{
	struct ip_tunnel_prl_entry *p;
	int ok = 1;

	rcu_read_lock();
	p = __ipip6_tunnel_locate_prl(t, iph->saddr);
	if (p) {
		if (p->flags & PRL_DEFAULT)
			skb->ndisc_nodetype = NDISC_NODETYPE_DEFAULT;
		else
			skb->ndisc_nodetype = NDISC_NODETYPE_NODEFAULT;
	} else {
		const struct in6_addr *addr6 = &ipv6_hdr(skb)->saddr;

		if (ipv6_addr_is_isatap(addr6) &&
		    (addr6->s6_addr32[3] == iph->saddr) &&
		    ipv6_chk_prefix(addr6, t->dev))
			skb->ndisc_nodetype = NDISC_NODETYPE_HOST;
		else
			ok = 0;
	}
	rcu_read_unlock();
	return ok;
}

static void ipip6_tunnel_uninit(struct net_device *dev)
{
	struct net *net = dev_net(dev);
	struct sit_net *sitn = net_generic(net, sit_net_id);

	if (dev == sitn->fb_tunnel_dev) {
		RCU_INIT_POINTER(sitn->tunnels_wc[0], NULL);
	} else {
		ipip6_tunnel_unlink(sitn, netdev_priv(dev));
		ipip6_tunnel_del_prl(netdev_priv(dev), NULL);
	}
	dev_put(dev);
}


static int ipip6_err(struct sk_buff *skb, u32 info)
{

/* All the routers (except for Linux) return only
   8 bytes of packet payload. It means, that precise relaying of
   ICMP in the real Internet is absolutely infeasible.
 */
	const struct iphdr *iph = (const struct iphdr *)skb->data;
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
	case ICMP_REDIRECT:
		break;
	}

	err = -ENOENT;

	t = ipip6_tunnel_lookup(dev_net(skb->dev),
				skb->dev,
				iph->daddr,
				iph->saddr);
	if (t == NULL)
		goto out;

	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED) {
		ipv4_update_pmtu(skb, dev_net(skb->dev), info,
				 t->dev->ifindex, 0, IPPROTO_IPV6, 0);
		err = 0;
		goto out;
	}
	if (type == ICMP_REDIRECT) {
		ipv4_redirect(skb, dev_net(skb->dev), t->dev->ifindex, 0,
			      IPPROTO_IPV6, 0);
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

static inline bool is_spoofed_6rd(struct ip_tunnel *tunnel, const __be32 v4addr,
				  const struct in6_addr *v6addr)
{
	__be32 v4embed = 0;
	if (check_6rd(tunnel, v6addr, &v4embed) && v4addr != v4embed)
		return true;
	return false;
}

static int ipip6_rcv(struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct ip_tunnel *tunnel;
	int err;

	tunnel = ipip6_tunnel_lookup(dev_net(skb->dev), skb->dev,
				     iph->saddr, iph->daddr);
	if (tunnel != NULL) {
		struct pcpu_tstats *tstats;

		secpath_reset(skb);
		skb->mac_header = skb->network_header;
		skb_reset_network_header(skb);
		IPCB(skb)->flags = 0;
		skb->protocol = htons(ETH_P_IPV6);
		skb->pkt_type = PACKET_HOST;

		if (tunnel->dev->priv_flags & IFF_ISATAP) {
			if (!isatap_chksrc(skb, iph, tunnel)) {
				tunnel->dev->stats.rx_errors++;
				goto out;
			}
		} else if (!(tunnel->dev->flags&IFF_POINTOPOINT)) {
			if (is_spoofed_6rd(tunnel, iph->saddr,
					   &ipv6_hdr(skb)->saddr) ||
			    is_spoofed_6rd(tunnel, iph->daddr,
					   &ipv6_hdr(skb)->daddr)) {
				tunnel->dev->stats.rx_errors++;
				goto out;
			}
		}

		__skb_tunnel_rx(skb, tunnel->dev);

		err = IP_ECN_decapsulate(iph, skb);
		if (unlikely(err)) {
			if (log_ecn_error)
				net_info_ratelimited("non-ECT from %pI4 with TOS=%#x\n",
						     &iph->saddr, iph->tos);
			if (err > 1) {
				++tunnel->dev->stats.rx_frame_errors;
				++tunnel->dev->stats.rx_errors;
				goto out;
			}
		}

		tstats = this_cpu_ptr(tunnel->dev->tstats);
		tstats->rx_packets++;
		tstats->rx_bytes += skb->len;

		netif_rx(skb);

		return 0;
	}

	/* no tunnel matched,  let upstream know, ipsec may handle it */
	return 1;
out:
	kfree_skb(skb);
	return 0;
}

/*
 * If the IPv6 address comes from 6rd / 6to4 (RFC 3056) addr space this function
 * stores the embedded IPv4 address in v4dst and returns true.
 */
static bool check_6rd(struct ip_tunnel *tunnel, const struct in6_addr *v6dst,
		      __be32 *v4dst)
{
#ifdef CONFIG_IPV6_SIT_6RD
	if (ipv6_prefix_equal(v6dst, &tunnel->ip6rd.prefix,
			      tunnel->ip6rd.prefixlen)) {
		unsigned int pbw0, pbi0;
		int pbi1;
		u32 d;

		pbw0 = tunnel->ip6rd.prefixlen >> 5;
		pbi0 = tunnel->ip6rd.prefixlen & 0x1f;

		d = (ntohl(v6dst->s6_addr32[pbw0]) << pbi0) >>
		    tunnel->ip6rd.relay_prefixlen;

		pbi1 = pbi0 - tunnel->ip6rd.relay_prefixlen;
		if (pbi1 > 0)
			d |= ntohl(v6dst->s6_addr32[pbw0 + 1]) >>
			     (32 - pbi1);

		*v4dst = tunnel->ip6rd.relay_prefix | htonl(d);
		return true;
	}
#else
	if (v6dst->s6_addr16[0] == htons(0x2002)) {
		/* 6to4 v6 addr has 16 bits prefix, 32 v4addr, 16 SLA, ... */
		memcpy(v4dst, &v6dst->s6_addr16[1], 4);
		return true;
	}
#endif
	return false;
}

static inline __be32 try_6rd(struct ip_tunnel *tunnel,
			     const struct in6_addr *v6dst)
{
	__be32 dst = 0;
	check_6rd(tunnel, v6dst, &dst);
	return dst;
}

/*
 *	This function assumes it is being called from dev_queue_xmit()
 *	and that skb is filled properly by that function.
 */

static netdev_tx_t ipip6_tunnel_xmit(struct sk_buff *skb,
				     struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	const struct iphdr  *tiph = &tunnel->parms.iph;
	const struct ipv6hdr *iph6 = ipv6_hdr(skb);
	u8     tos = tunnel->parms.iph.tos;
	__be16 df = tiph->frag_off;
	struct rtable *rt;     			/* Route to the other host */
	struct net_device *tdev;		/* Device to other host */
	struct iphdr  *iph;			/* Our new IP header */
	unsigned int max_headroom;		/* The extra header space needed */
	__be32 dst = tiph->daddr;
	struct flowi4 fl4;
	int    mtu;
	const struct in6_addr *addr6;
	int addr_type;

	if (skb->protocol != htons(ETH_P_IPV6))
		goto tx_error;

	if (tos == 1)
		tos = ipv6_get_dsfield(iph6);

	/* ISATAP (RFC4214) - must come before 6to4 */
	if (dev->priv_flags & IFF_ISATAP) {
		struct neighbour *neigh = NULL;
		bool do_tx_error = false;

		if (skb_dst(skb))
			neigh = dst_neigh_lookup(skb_dst(skb), &iph6->daddr);

		if (neigh == NULL) {
			net_dbg_ratelimited("sit: nexthop == NULL\n");
			goto tx_error;
		}

		addr6 = (const struct in6_addr *)&neigh->primary_key;
		addr_type = ipv6_addr_type(addr6);

		if ((addr_type & IPV6_ADDR_UNICAST) &&
		     ipv6_addr_is_isatap(addr6))
			dst = addr6->s6_addr32[3];
		else
			do_tx_error = true;

		neigh_release(neigh);
		if (do_tx_error)
			goto tx_error;
	}

	if (!dst)
		dst = try_6rd(tunnel, &iph6->daddr);

	if (!dst) {
		struct neighbour *neigh = NULL;
		bool do_tx_error = false;

		if (skb_dst(skb))
			neigh = dst_neigh_lookup(skb_dst(skb), &iph6->daddr);

		if (neigh == NULL) {
			net_dbg_ratelimited("sit: nexthop == NULL\n");
			goto tx_error;
		}

		addr6 = (const struct in6_addr *)&neigh->primary_key;
		addr_type = ipv6_addr_type(addr6);

		if (addr_type == IPV6_ADDR_ANY) {
			addr6 = &ipv6_hdr(skb)->daddr;
			addr_type = ipv6_addr_type(addr6);
		}

		if ((addr_type & IPV6_ADDR_COMPATv4) != 0)
			dst = addr6->s6_addr32[3];
		else
			do_tx_error = true;

		neigh_release(neigh);
		if (do_tx_error)
			goto tx_error;
	}

	rt = ip_route_output_ports(dev_net(dev), &fl4, NULL,
				   dst, tiph->saddr,
				   0, 0,
				   IPPROTO_IPV6, RT_TOS(tos),
				   tunnel->parms.link);
	if (IS_ERR(rt)) {
		dev->stats.tx_carrier_errors++;
		goto tx_error_icmp;
	}
	if (rt->rt_type != RTN_UNICAST) {
		ip_rt_put(rt);
		dev->stats.tx_carrier_errors++;
		goto tx_error_icmp;
	}
	tdev = rt->dst.dev;

	if (tdev == dev) {
		ip_rt_put(rt);
		dev->stats.collisions++;
		goto tx_error;
	}

	if (df) {
		mtu = dst_mtu(&rt->dst) - sizeof(struct iphdr);

		if (mtu < 68) {
			dev->stats.collisions++;
			ip_rt_put(rt);
			goto tx_error;
		}

		if (mtu < IPV6_MIN_MTU) {
			mtu = IPV6_MIN_MTU;
			df = 0;
		}

		if (tunnel->parms.iph.daddr && skb_dst(skb))
			skb_dst(skb)->ops->update_pmtu(skb_dst(skb), NULL, skb, mtu);

		if (skb->len > mtu) {
			icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
			ip_rt_put(rt);
			goto tx_error;
		}
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
			dev->stats.tx_dropped++;
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
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
	skb_dst_set(skb, &rt->dst);

	/*
	 *	Push down and install the IPIP header.
	 */

	iph 			=	ip_hdr(skb);
	iph->version		=	4;
	iph->ihl		=	sizeof(struct iphdr)>>2;
	iph->frag_off		=	df;
	iph->protocol		=	IPPROTO_IPV6;
	iph->tos		=	INET_ECN_encapsulate(tos, ipv6_get_dsfield(iph6));
	iph->daddr		=	fl4.daddr;
	iph->saddr		=	fl4.saddr;

	if ((iph->ttl = tiph->ttl) == 0)
		iph->ttl	=	iph6->hop_limit;

	skb->ip_summed = CHECKSUM_NONE;
	ip_select_ident(skb, skb_dst(skb), NULL);
	iptunnel_xmit(skb, dev);
	return NETDEV_TX_OK;

tx_error_icmp:
	dst_link_failure(skb);
tx_error:
	dev->stats.tx_errors++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void ipip6_tunnel_bind_dev(struct net_device *dev)
{
	struct net_device *tdev = NULL;
	struct ip_tunnel *tunnel;
	const struct iphdr *iph;
	struct flowi4 fl4;

	tunnel = netdev_priv(dev);
	iph = &tunnel->parms.iph;

	if (iph->daddr) {
		struct rtable *rt = ip_route_output_ports(dev_net(dev), &fl4, NULL,
							  iph->daddr, iph->saddr,
							  0, 0,
							  IPPROTO_IPV6,
							  RT_TOS(iph->tos),
							  tunnel->parms.link);

		if (!IS_ERR(rt)) {
			tdev = rt->dst.dev;
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

static void ipip6_tunnel_update(struct ip_tunnel *t, struct ip_tunnel_parm *p)
{
	struct net *net = dev_net(t->dev);
	struct sit_net *sitn = net_generic(net, sit_net_id);

	ipip6_tunnel_unlink(sitn, t);
	synchronize_net();
	t->parms.iph.saddr = p->iph.saddr;
	t->parms.iph.daddr = p->iph.daddr;
	memcpy(t->dev->dev_addr, &p->iph.saddr, 4);
	memcpy(t->dev->broadcast, &p->iph.daddr, 4);
	ipip6_tunnel_link(sitn, t);
	t->parms.iph.ttl = p->iph.ttl;
	t->parms.iph.tos = p->iph.tos;
	if (t->parms.link != p->link) {
		t->parms.link = p->link;
		ipip6_tunnel_bind_dev(t->dev);
	}
	netdev_state_change(t->dev);
}

#ifdef CONFIG_IPV6_SIT_6RD
static int ipip6_tunnel_update_6rd(struct ip_tunnel *t,
				   struct ip_tunnel_6rd *ip6rd)
{
	struct in6_addr prefix;
	__be32 relay_prefix;

	if (ip6rd->relay_prefixlen > 32 ||
	    ip6rd->prefixlen + (32 - ip6rd->relay_prefixlen) > 64)
		return -EINVAL;

	ipv6_addr_prefix(&prefix, &ip6rd->prefix, ip6rd->prefixlen);
	if (!ipv6_addr_equal(&prefix, &ip6rd->prefix))
		return -EINVAL;
	if (ip6rd->relay_prefixlen)
		relay_prefix = ip6rd->relay_prefix &
			       htonl(0xffffffffUL <<
				     (32 - ip6rd->relay_prefixlen));
	else
		relay_prefix = 0;
	if (relay_prefix != ip6rd->relay_prefix)
		return -EINVAL;

	t->ip6rd.prefix = prefix;
	t->ip6rd.relay_prefix = relay_prefix;
	t->ip6rd.prefixlen = ip6rd->prefixlen;
	t->ip6rd.relay_prefixlen = ip6rd->relay_prefixlen;
	netdev_state_change(t->dev);
	return 0;
}
#endif

static int
ipip6_tunnel_ioctl (struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int err = 0;
	struct ip_tunnel_parm p;
	struct ip_tunnel_prl prl;
	struct ip_tunnel *t;
	struct net *net = dev_net(dev);
	struct sit_net *sitn = net_generic(net, sit_net_id);
#ifdef CONFIG_IPV6_SIT_6RD
	struct ip_tunnel_6rd ip6rd;
#endif

	switch (cmd) {
	case SIOCGETTUNNEL:
#ifdef CONFIG_IPV6_SIT_6RD
	case SIOCGET6RD:
#endif
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

		err = -EFAULT;
		if (cmd == SIOCGETTUNNEL) {
			memcpy(&p, &t->parms, sizeof(p));
			if (copy_to_user(ifr->ifr_ifru.ifru_data, &p,
					 sizeof(p)))
				goto done;
#ifdef CONFIG_IPV6_SIT_6RD
		} else {
			ip6rd.prefix = t->ip6rd.prefix;
			ip6rd.relay_prefix = t->ip6rd.relay_prefix;
			ip6rd.prefixlen = t->ip6rd.prefixlen;
			ip6rd.relay_prefixlen = t->ip6rd.relay_prefixlen;
			if (copy_to_user(ifr->ifr_ifru.ifru_data, &ip6rd,
					 sizeof(ip6rd)))
				goto done;
#endif
		}
		err = 0;
		break;

	case SIOCADDTUNNEL:
	case SIOCCHGTUNNEL:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
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
			}

			ipip6_tunnel_update(t, &p);
		}

		if (t) {
			err = 0;
			if (copy_to_user(ifr->ifr_ifru.ifru_data, &t->parms, sizeof(p)))
				err = -EFAULT;
		} else
			err = (cmd == SIOCADDTUNNEL ? -ENOBUFS : -ENOENT);
		break;

	case SIOCDELTUNNEL:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
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
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
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

#ifdef CONFIG_IPV6_SIT_6RD
	case SIOCADD6RD:
	case SIOCCHG6RD:
	case SIOCDEL6RD:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			goto done;

		err = -EFAULT;
		if (copy_from_user(&ip6rd, ifr->ifr_ifru.ifru_data,
				   sizeof(ip6rd)))
			goto done;

		t = netdev_priv(dev);

		if (cmd != SIOCDEL6RD) {
			err = ipip6_tunnel_update_6rd(t, &ip6rd);
			if (err < 0)
				goto done;
		} else
			ipip6_tunnel_clone_6rd(dev, sitn);

		err = 0;
		break;
#endif

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
	.ndo_get_stats64 = ip_tunnel_get_stats64,
};

static void ipip6_dev_free(struct net_device *dev)
{
	free_percpu(dev->tstats);
	free_netdev(dev);
}

static void ipip6_tunnel_setup(struct net_device *dev)
{
	dev->netdev_ops		= &ipip6_netdev_ops;
	dev->destructor 	= ipip6_dev_free;

	dev->type		= ARPHRD_SIT;
	dev->hard_header_len 	= LL_MAX_HEADER + sizeof(struct iphdr);
	dev->mtu		= ETH_DATA_LEN - sizeof(struct iphdr);
	dev->flags		= IFF_NOARP;
	dev->priv_flags	       &= ~IFF_XMIT_DST_RELEASE;
	dev->iflink		= 0;
	dev->addr_len		= 4;
	dev->features		|= NETIF_F_NETNS_LOCAL;
	dev->features		|= NETIF_F_LLTX;
}

static int ipip6_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);

	tunnel->dev = dev;

	memcpy(dev->dev_addr, &tunnel->parms.iph.saddr, 4);
	memcpy(dev->broadcast, &tunnel->parms.iph.daddr, 4);

	ipip6_tunnel_bind_dev(dev);
	dev->tstats = alloc_percpu(struct pcpu_tstats);
	if (!dev->tstats)
		return -ENOMEM;

	return 0;
}

static int __net_init ipip6_fb_tunnel_init(struct net_device *dev)
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

	dev->tstats = alloc_percpu(struct pcpu_tstats);
	if (!dev->tstats)
		return -ENOMEM;
	dev_hold(dev);
	rcu_assign_pointer(sitn->tunnels_wc[0], tunnel);
	return 0;
}

static void ipip6_netlink_parms(struct nlattr *data[],
				struct ip_tunnel_parm *parms)
{
	memset(parms, 0, sizeof(*parms));

	parms->iph.version = 4;
	parms->iph.protocol = IPPROTO_IPV6;
	parms->iph.ihl = 5;
	parms->iph.ttl = 64;

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

	if (data[IFLA_IPTUN_FLAGS])
		parms->i_flags = nla_get_be16(data[IFLA_IPTUN_FLAGS]);
}

#ifdef CONFIG_IPV6_SIT_6RD
/* This function returns true when 6RD attributes are present in the nl msg */
static bool ipip6_netlink_6rd_parms(struct nlattr *data[],
				    struct ip_tunnel_6rd *ip6rd)
{
	bool ret = false;
	memset(ip6rd, 0, sizeof(*ip6rd));

	if (!data)
		return ret;

	if (data[IFLA_IPTUN_6RD_PREFIX]) {
		ret = true;
		nla_memcpy(&ip6rd->prefix, data[IFLA_IPTUN_6RD_PREFIX],
			   sizeof(struct in6_addr));
	}

	if (data[IFLA_IPTUN_6RD_RELAY_PREFIX]) {
		ret = true;
		ip6rd->relay_prefix =
			nla_get_be32(data[IFLA_IPTUN_6RD_RELAY_PREFIX]);
	}

	if (data[IFLA_IPTUN_6RD_PREFIXLEN]) {
		ret = true;
		ip6rd->prefixlen = nla_get_u16(data[IFLA_IPTUN_6RD_PREFIXLEN]);
	}

	if (data[IFLA_IPTUN_6RD_RELAY_PREFIXLEN]) {
		ret = true;
		ip6rd->relay_prefixlen =
			nla_get_u16(data[IFLA_IPTUN_6RD_RELAY_PREFIXLEN]);
	}

	return ret;
}
#endif

static int ipip6_newlink(struct net *src_net, struct net_device *dev,
			 struct nlattr *tb[], struct nlattr *data[])
{
	struct net *net = dev_net(dev);
	struct ip_tunnel *nt;
#ifdef CONFIG_IPV6_SIT_6RD
	struct ip_tunnel_6rd ip6rd;
#endif
	int err;

	nt = netdev_priv(dev);
	ipip6_netlink_parms(data, &nt->parms);

	if (ipip6_tunnel_locate(net, &nt->parms, 0))
		return -EEXIST;

	err = ipip6_tunnel_create(dev);
	if (err < 0)
		return err;

#ifdef CONFIG_IPV6_SIT_6RD
	if (ipip6_netlink_6rd_parms(data, &ip6rd))
		err = ipip6_tunnel_update_6rd(nt, &ip6rd);
#endif

	return err;
}

static int ipip6_changelink(struct net_device *dev, struct nlattr *tb[],
			  struct nlattr *data[])
{
	struct ip_tunnel *t;
	struct ip_tunnel_parm p;
	struct net *net = dev_net(dev);
	struct sit_net *sitn = net_generic(net, sit_net_id);
#ifdef CONFIG_IPV6_SIT_6RD
	struct ip_tunnel_6rd ip6rd;
#endif

	if (dev == sitn->fb_tunnel_dev)
		return -EINVAL;

	ipip6_netlink_parms(data, &p);

	if (((dev->flags & IFF_POINTOPOINT) && !p.iph.daddr) ||
	    (!(dev->flags & IFF_POINTOPOINT) && p.iph.daddr))
		return -EINVAL;

	t = ipip6_tunnel_locate(net, &p, 0);

	if (t) {
		if (t->dev != dev)
			return -EEXIST;
	} else
		t = netdev_priv(dev);

	ipip6_tunnel_update(t, &p);

#ifdef CONFIG_IPV6_SIT_6RD
	if (ipip6_netlink_6rd_parms(data, &ip6rd))
		return ipip6_tunnel_update_6rd(t, &ip6rd);
#endif

	return 0;
}

static size_t ipip6_get_size(const struct net_device *dev)
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
		/* IFLA_IPTUN_FLAGS */
		nla_total_size(2) +
#ifdef CONFIG_IPV6_SIT_6RD
		/* IFLA_IPTUN_6RD_PREFIX */
		nla_total_size(sizeof(struct in6_addr)) +
		/* IFLA_IPTUN_6RD_RELAY_PREFIX */
		nla_total_size(4) +
		/* IFLA_IPTUN_6RD_PREFIXLEN */
		nla_total_size(2) +
		/* IFLA_IPTUN_6RD_RELAY_PREFIXLEN */
		nla_total_size(2) +
#endif
		0;
}

static int ipip6_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct ip_tunnel_parm *parm = &tunnel->parms;

	if (nla_put_u32(skb, IFLA_IPTUN_LINK, parm->link) ||
	    nla_put_be32(skb, IFLA_IPTUN_LOCAL, parm->iph.saddr) ||
	    nla_put_be32(skb, IFLA_IPTUN_REMOTE, parm->iph.daddr) ||
	    nla_put_u8(skb, IFLA_IPTUN_TTL, parm->iph.ttl) ||
	    nla_put_u8(skb, IFLA_IPTUN_TOS, parm->iph.tos) ||
	    nla_put_u8(skb, IFLA_IPTUN_PMTUDISC,
		       !!(parm->iph.frag_off & htons(IP_DF))) ||
	    nla_put_be16(skb, IFLA_IPTUN_FLAGS, parm->i_flags))
		goto nla_put_failure;

#ifdef CONFIG_IPV6_SIT_6RD
	if (nla_put(skb, IFLA_IPTUN_6RD_PREFIX, sizeof(struct in6_addr),
		    &tunnel->ip6rd.prefix) ||
	    nla_put_be32(skb, IFLA_IPTUN_6RD_RELAY_PREFIX,
			 tunnel->ip6rd.relay_prefix) ||
	    nla_put_u16(skb, IFLA_IPTUN_6RD_PREFIXLEN,
			tunnel->ip6rd.prefixlen) ||
	    nla_put_u16(skb, IFLA_IPTUN_6RD_RELAY_PREFIXLEN,
			tunnel->ip6rd.relay_prefixlen))
		goto nla_put_failure;
#endif

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static const struct nla_policy ipip6_policy[IFLA_IPTUN_MAX + 1] = {
	[IFLA_IPTUN_LINK]		= { .type = NLA_U32 },
	[IFLA_IPTUN_LOCAL]		= { .type = NLA_U32 },
	[IFLA_IPTUN_REMOTE]		= { .type = NLA_U32 },
	[IFLA_IPTUN_TTL]		= { .type = NLA_U8 },
	[IFLA_IPTUN_TOS]		= { .type = NLA_U8 },
	[IFLA_IPTUN_PMTUDISC]		= { .type = NLA_U8 },
	[IFLA_IPTUN_FLAGS]		= { .type = NLA_U16 },
#ifdef CONFIG_IPV6_SIT_6RD
	[IFLA_IPTUN_6RD_PREFIX]		= { .len = sizeof(struct in6_addr) },
	[IFLA_IPTUN_6RD_RELAY_PREFIX]	= { .type = NLA_U32 },
	[IFLA_IPTUN_6RD_PREFIXLEN]	= { .type = NLA_U16 },
	[IFLA_IPTUN_6RD_RELAY_PREFIXLEN] = { .type = NLA_U16 },
#endif
};

static struct rtnl_link_ops sit_link_ops __read_mostly = {
	.kind		= "sit",
	.maxtype	= IFLA_IPTUN_MAX,
	.policy		= ipip6_policy,
	.priv_size	= sizeof(struct ip_tunnel),
	.setup		= ipip6_tunnel_setup,
	.newlink	= ipip6_newlink,
	.changelink	= ipip6_changelink,
	.get_size	= ipip6_get_size,
	.fill_info	= ipip6_fill_info,
};

static struct xfrm_tunnel sit_handler __read_mostly = {
	.handler	=	ipip6_rcv,
	.err_handler	=	ipip6_err,
	.priority	=	1,
};

static void __net_exit sit_destroy_tunnels(struct sit_net *sitn, struct list_head *head)
{
	int prio;

	for (prio = 1; prio < 4; prio++) {
		int h;
		for (h = 0; h < HASH_SIZE; h++) {
			struct ip_tunnel *t;

			t = rtnl_dereference(sitn->tunnels[prio][h]);
			while (t != NULL) {
				unregister_netdevice_queue(t->dev, head);
				t = rtnl_dereference(t->next);
			}
		}
	}
}

static int __net_init sit_init_net(struct net *net)
{
	struct sit_net *sitn = net_generic(net, sit_net_id);
	struct ip_tunnel *t;
	int err;

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

	err = ipip6_fb_tunnel_init(sitn->fb_tunnel_dev);
	if (err)
		goto err_dev_free;

	ipip6_tunnel_clone_6rd(sitn->fb_tunnel_dev, sitn);

	if ((err = register_netdev(sitn->fb_tunnel_dev)))
		goto err_reg_dev;

	t = netdev_priv(sitn->fb_tunnel_dev);

	strcpy(t->parms.name, sitn->fb_tunnel_dev->name);
	return 0;

err_reg_dev:
	dev_put(sitn->fb_tunnel_dev);
err_dev_free:
	ipip6_dev_free(sitn->fb_tunnel_dev);
err_alloc_dev:
	return err;
}

static void __net_exit sit_exit_net(struct net *net)
{
	struct sit_net *sitn = net_generic(net, sit_net_id);
	LIST_HEAD(list);

	rtnl_lock();
	sit_destroy_tunnels(sitn, &list);
	unregister_netdevice_queue(sitn->fb_tunnel_dev, &list);
	unregister_netdevice_many(&list);
	rtnl_unlock();
}

static struct pernet_operations sit_net_ops = {
	.init = sit_init_net,
	.exit = sit_exit_net,
	.id   = &sit_net_id,
	.size = sizeof(struct sit_net),
};

static void __exit sit_cleanup(void)
{
	rtnl_link_unregister(&sit_link_ops);
	xfrm4_tunnel_deregister(&sit_handler, AF_INET6);

	unregister_pernet_device(&sit_net_ops);
	rcu_barrier(); /* Wait for completion of call_rcu()'s */
}

static int __init sit_init(void)
{
	int err;

	pr_info("IPv6 over IPv4 tunneling driver\n");

	err = register_pernet_device(&sit_net_ops);
	if (err < 0)
		return err;
	err = xfrm4_tunnel_register(&sit_handler, AF_INET6);
	if (err < 0) {
		pr_info("%s: can't add protocol\n", __func__);
		goto xfrm_tunnel_failed;
	}
	err = rtnl_link_register(&sit_link_ops);
	if (err < 0)
		goto rtnl_link_failed;

out:
	return err;

rtnl_link_failed:
	xfrm4_tunnel_deregister(&sit_handler, AF_INET6);
xfrm_tunnel_failed:
	unregister_pernet_device(&sit_net_ops);
	goto out;
}

module_init(sit_init);
module_exit(sit_cleanup);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETDEV("sit0");
