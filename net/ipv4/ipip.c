/*
 *	Linux NET3:	IP/IP protocol decoder.
 *
 *	Version: $Id: ipip.c,v 1.50 2001/10/02 02:22:36 davem Exp $
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

		-Alan Cox	(Alan.Cox@linux.org) 21 March 95

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
#include <net/ipip.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>

#define HASH_SIZE  16
#define HASH(addr) (((__force u32)addr^((__force u32)addr>>4))&0xF)

static int ipip_fb_tunnel_init(struct net_device *dev);
static int ipip_tunnel_init(struct net_device *dev);
static void ipip_tunnel_setup(struct net_device *dev);

static struct net_device *ipip_fb_tunnel_dev;

static struct ip_tunnel *tunnels_r_l[HASH_SIZE];
static struct ip_tunnel *tunnels_r[HASH_SIZE];
static struct ip_tunnel *tunnels_l[HASH_SIZE];
static struct ip_tunnel *tunnels_wc[1];
static struct ip_tunnel **tunnels[4] = { tunnels_wc, tunnels_l, tunnels_r, tunnels_r_l };

static DEFINE_RWLOCK(ipip_lock);

static struct ip_tunnel * ipip_tunnel_lookup(__be32 remote, __be32 local)
{
	unsigned h0 = HASH(remote);
	unsigned h1 = HASH(local);
	struct ip_tunnel *t;

	for (t = tunnels_r_l[h0^h1]; t; t = t->next) {
		if (local == t->parms.iph.saddr &&
		    remote == t->parms.iph.daddr && (t->dev->flags&IFF_UP))
			return t;
	}
	for (t = tunnels_r[h0]; t; t = t->next) {
		if (remote == t->parms.iph.daddr && (t->dev->flags&IFF_UP))
			return t;
	}
	for (t = tunnels_l[h1]; t; t = t->next) {
		if (local == t->parms.iph.saddr && (t->dev->flags&IFF_UP))
			return t;
	}
	if ((t = tunnels_wc[0]) != NULL && (t->dev->flags&IFF_UP))
		return t;
	return NULL;
}

static struct ip_tunnel **__ipip_bucket(struct ip_tunnel_parm *parms)
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
	return &tunnels[prio][h];
}

static inline struct ip_tunnel **ipip_bucket(struct ip_tunnel *t)
{
	return __ipip_bucket(&t->parms);
}

static void ipip_tunnel_unlink(struct ip_tunnel *t)
{
	struct ip_tunnel **tp;

	for (tp = ipip_bucket(t); *tp; tp = &(*tp)->next) {
		if (t == *tp) {
			write_lock_bh(&ipip_lock);
			*tp = t->next;
			write_unlock_bh(&ipip_lock);
			break;
		}
	}
}

static void ipip_tunnel_link(struct ip_tunnel *t)
{
	struct ip_tunnel **tp = ipip_bucket(t);

	t->next = *tp;
	write_lock_bh(&ipip_lock);
	*tp = t;
	write_unlock_bh(&ipip_lock);
}

static struct ip_tunnel * ipip_tunnel_locate(struct ip_tunnel_parm *parms, int create)
{
	__be32 remote = parms->iph.daddr;
	__be32 local = parms->iph.saddr;
	struct ip_tunnel *t, **tp, *nt;
	struct net_device *dev;
	char name[IFNAMSIZ];

	for (tp = __ipip_bucket(parms); (t = *tp) != NULL; tp = &t->next) {
		if (local == t->parms.iph.saddr && remote == t->parms.iph.daddr)
			return t;
	}
	if (!create)
		return NULL;

	if (parms->name[0])
		strlcpy(name, parms->name, IFNAMSIZ);
	else
		sprintf(name, "tunl%%d");

	dev = alloc_netdev(sizeof(*t), name, ipip_tunnel_setup);
	if (dev == NULL)
		return NULL;

	nt = netdev_priv(dev);
	dev->init = ipip_tunnel_init;
	nt->parms = *parms;

	if (register_netdevice(dev) < 0) {
		free_netdev(dev);
		goto failed;
	}

	dev_hold(dev);
	ipip_tunnel_link(nt);
	return nt;

failed:
	return NULL;
}

static void ipip_tunnel_uninit(struct net_device *dev)
{
	if (dev == ipip_fb_tunnel_dev) {
		write_lock_bh(&ipip_lock);
		tunnels_wc[0] = NULL;
		write_unlock_bh(&ipip_lock);
	} else
		ipip_tunnel_unlink(netdev_priv(dev));
	dev_put(dev);
}

static int ipip_err(struct sk_buff *skb, u32 info)
{
#ifndef I_WISH_WORLD_WERE_PERFECT

/* It is not :-( All the routers (except for Linux) return only
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

	read_lock(&ipip_lock);
	t = ipip_tunnel_lookup(iph->daddr, iph->saddr);
	if (t == NULL || t->parms.iph.daddr == 0)
		goto out;

	err = 0;
	if (t->parms.iph.ttl == 0 && type == ICMP_TIME_EXCEEDED)
		goto out;

	if (jiffies - t->err_time < IPTUNNEL_ERR_TIMEO)
		t->err_count++;
	else
		t->err_count = 1;
	t->err_time = jiffies;
out:
	read_unlock(&ipip_lock);
	return err;
#else
	struct iphdr *iph = (struct iphdr*)dp;
	int hlen = iph->ihl<<2;
	struct iphdr *eiph;
	const int type = icmp_hdr(skb)->type;
	const int code = icmp_hdr(skb)->code;
	int rel_type = 0;
	int rel_code = 0;
	__be32 rel_info = 0;
	__u32 n = 0;
	struct sk_buff *skb2;
	struct flowi fl;
	struct rtable *rt;

	if (len < hlen + sizeof(struct iphdr))
		return 0;
	eiph = (struct iphdr*)(dp + hlen);

	switch (type) {
	default:
		return 0;
	case ICMP_PARAMETERPROB:
		n = ntohl(icmp_hdr(skb)->un.gateway) >> 24;
		if (n < hlen)
			return 0;

		/* So... This guy found something strange INSIDE encapsulated
		   packet. Well, he is fool, but what can we do ?
		 */
		rel_type = ICMP_PARAMETERPROB;
		rel_info = htonl((n - hlen) << 24);
		break;

	case ICMP_DEST_UNREACH:
		switch (code) {
		case ICMP_SR_FAILED:
		case ICMP_PORT_UNREACH:
			/* Impossible event. */
			return 0;
		case ICMP_FRAG_NEEDED:
			/* And it is the only really necessary thing :-) */
			n = ntohs(icmp_hdr(skb)->un.frag.mtu);
			if (n < hlen+68)
				return 0;
			n -= hlen;
			/* BSD 4.2 MORE DOES NOT EXIST IN NATURE. */
			if (n > ntohs(eiph->tot_len))
				return 0;
			rel_info = htonl(n);
			break;
		default:
			/* All others are translated to HOST_UNREACH.
			   rfc2003 contains "deep thoughts" about NET_UNREACH,
			   I believe, it is just ether pollution. --ANK
			 */
			rel_type = ICMP_DEST_UNREACH;
			rel_code = ICMP_HOST_UNREACH;
			break;
		}
		break;
	case ICMP_TIME_EXCEEDED:
		if (code != ICMP_EXC_TTL)
			return 0;
		break;
	}

	/* Prepare fake skb to feed it to icmp_send */
	skb2 = skb_clone(skb, GFP_ATOMIC);
	if (skb2 == NULL)
		return 0;
	dst_release(skb2->dst);
	skb2->dst = NULL;
	skb_pull(skb2, skb->data - (u8*)eiph);
	skb_reset_network_header(skb2);

	/* Try to guess incoming interface */
	memset(&fl, 0, sizeof(fl));
	fl.fl4_daddr = eiph->saddr;
	fl.fl4_tos = RT_TOS(eiph->tos);
	fl.proto = IPPROTO_IPIP;
	if (ip_route_output_key(&init_net, &rt, &key)) {
		kfree_skb(skb2);
		return 0;
	}
	skb2->dev = rt->u.dst.dev;

	/* route "incoming" packet */
	if (rt->rt_flags&RTCF_LOCAL) {
		ip_rt_put(rt);
		rt = NULL;
		fl.fl4_daddr = eiph->daddr;
		fl.fl4_src = eiph->saddr;
		fl.fl4_tos = eiph->tos;
		if (ip_route_output_key(&init_net, &rt, &fl) ||
		    rt->u.dst.dev->type != ARPHRD_TUNNEL) {
			ip_rt_put(rt);
			kfree_skb(skb2);
			return 0;
		}
	} else {
		ip_rt_put(rt);
		if (ip_route_input(skb2, eiph->daddr, eiph->saddr, eiph->tos, skb2->dev) ||
		    skb2->dst->dev->type != ARPHRD_TUNNEL) {
			kfree_skb(skb2);
			return 0;
		}
	}

	/* change mtu on this route */
	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED) {
		if (n > dst_mtu(skb2->dst)) {
			kfree_skb(skb2);
			return 0;
		}
		skb2->dst->ops->update_pmtu(skb2->dst, n);
	} else if (type == ICMP_TIME_EXCEEDED) {
		struct ip_tunnel *t = netdev_priv(skb2->dev);
		if (t->parms.iph.ttl) {
			rel_type = ICMP_DEST_UNREACH;
			rel_code = ICMP_HOST_UNREACH;
		}
	}

	icmp_send(skb2, rel_type, rel_code, rel_info);
	kfree_skb(skb2);
	return 0;
#endif
}

static inline void ipip_ecn_decapsulate(const struct iphdr *outer_iph,
					struct sk_buff *skb)
{
	struct iphdr *inner_iph = ip_hdr(skb);

	if (INET_ECN_is_ce(outer_iph->tos))
		IP_ECN_set_ce(inner_iph);
}

static int ipip_rcv(struct sk_buff *skb)
{
	struct ip_tunnel *tunnel;
	const struct iphdr *iph = ip_hdr(skb);

	read_lock(&ipip_lock);
	if ((tunnel = ipip_tunnel_lookup(iph->saddr, iph->daddr)) != NULL) {
		if (!xfrm4_policy_check(NULL, XFRM_POLICY_IN, skb)) {
			read_unlock(&ipip_lock);
			kfree_skb(skb);
			return 0;
		}

		secpath_reset(skb);

		skb->mac_header = skb->network_header;
		skb_reset_network_header(skb);
		skb->protocol = htons(ETH_P_IP);
		skb->pkt_type = PACKET_HOST;

		tunnel->stat.rx_packets++;
		tunnel->stat.rx_bytes += skb->len;
		skb->dev = tunnel->dev;
		dst_release(skb->dst);
		skb->dst = NULL;
		nf_reset(skb);
		ipip_ecn_decapsulate(iph, skb);
		netif_rx(skb);
		read_unlock(&ipip_lock);
		return 0;
	}
	read_unlock(&ipip_lock);

	return -1;
}

/*
 *	This function assumes it is being called from dev_queue_xmit()
 *	and that skb is filled properly by that function.
 */

static int ipip_tunnel_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct net_device_stats *stats = &tunnel->stat;
	struct iphdr  *tiph = &tunnel->parms.iph;
	u8     tos = tunnel->parms.iph.tos;
	__be16 df = tiph->frag_off;
	struct rtable *rt;     			/* Route to the other host */
	struct net_device *tdev;			/* Device to other host */
	struct iphdr  *old_iph = ip_hdr(skb);
	struct iphdr  *iph;			/* Our new IP header */
	unsigned int max_headroom;		/* The extra header space needed */
	__be32 dst = tiph->daddr;
	int    mtu;

	if (tunnel->recursion++) {
		tunnel->stat.collisions++;
		goto tx_error;
	}

	if (skb->protocol != htons(ETH_P_IP))
		goto tx_error;

	if (tos&1)
		tos = old_iph->tos;

	if (!dst) {
		/* NBMA tunnel */
		if ((rt = (struct rtable*)skb->dst) == NULL) {
			tunnel->stat.tx_fifo_errors++;
			goto tx_error;
		}
		if ((dst = rt->rt_gateway) == 0)
			goto tx_error_icmp;
	}

	{
		struct flowi fl = { .oif = tunnel->parms.link,
				    .nl_u = { .ip4_u =
					      { .daddr = dst,
						.saddr = tiph->saddr,
						.tos = RT_TOS(tos) } },
				    .proto = IPPROTO_IPIP };
		if (ip_route_output_key(&init_net, &rt, &fl)) {
			tunnel->stat.tx_carrier_errors++;
			goto tx_error_icmp;
		}
	}
	tdev = rt->u.dst.dev;

	if (tdev == dev) {
		ip_rt_put(rt);
		tunnel->stat.collisions++;
		goto tx_error;
	}

	if (tiph->frag_off)
		mtu = dst_mtu(&rt->u.dst) - sizeof(struct iphdr);
	else
		mtu = skb->dst ? dst_mtu(skb->dst) : dev->mtu;

	if (mtu < 68) {
		tunnel->stat.collisions++;
		ip_rt_put(rt);
		goto tx_error;
	}
	if (skb->dst)
		skb->dst->ops->update_pmtu(skb->dst, mtu);

	df |= (old_iph->frag_off&htons(IP_DF));

	if ((old_iph->frag_off&htons(IP_DF)) && mtu < ntohs(old_iph->tot_len)) {
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
		ip_rt_put(rt);
		goto tx_error;
	}

	if (tunnel->err_count > 0) {
		if (jiffies - tunnel->err_time < IPTUNNEL_ERR_TIMEO) {
			tunnel->err_count--;
			dst_link_failure(skb);
		} else
			tunnel->err_count = 0;
	}

	/*
	 * Okay, now see if we can stuff it in the buffer as-is.
	 */
	max_headroom = (LL_RESERVED_SPACE(tdev)+sizeof(struct iphdr));

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
		old_iph = ip_hdr(skb);
	}

	skb->transport_header = skb->network_header;
	skb_push(skb, sizeof(struct iphdr));
	skb_reset_network_header(skb);
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
	IPCB(skb)->flags &= ~(IPSKB_XFRM_TUNNEL_SIZE | IPSKB_XFRM_TRANSFORMED |
			      IPSKB_REROUTED);
	dst_release(skb->dst);
	skb->dst = &rt->u.dst;

	/*
	 *	Push down and install the IPIP header.
	 */

	iph 			=	ip_hdr(skb);
	iph->version		=	4;
	iph->ihl		=	sizeof(struct iphdr)>>2;
	iph->frag_off		=	df;
	iph->protocol		=	IPPROTO_IPIP;
	iph->tos		=	INET_ECN_encapsulate(tos, old_iph->tos);
	iph->daddr		=	rt->rt_dst;
	iph->saddr		=	rt->rt_src;

	if ((iph->ttl = tiph->ttl) == 0)
		iph->ttl	=	old_iph->ttl;

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

static void ipip_tunnel_bind_dev(struct net_device *dev)
{
	struct net_device *tdev = NULL;
	struct ip_tunnel *tunnel;
	struct iphdr *iph;

	tunnel = netdev_priv(dev);
	iph = &tunnel->parms.iph;

	if (iph->daddr) {
		struct flowi fl = { .oif = tunnel->parms.link,
				    .nl_u = { .ip4_u =
					      { .daddr = iph->daddr,
						.saddr = iph->saddr,
						.tos = RT_TOS(iph->tos) } },
				    .proto = IPPROTO_IPIP };
		struct rtable *rt;
		if (!ip_route_output_key(&init_net, &rt, &fl)) {
			tdev = rt->u.dst.dev;
			ip_rt_put(rt);
		}
		dev->flags |= IFF_POINTOPOINT;
	}

	if (!tdev && tunnel->parms.link)
		tdev = __dev_get_by_index(&init_net, tunnel->parms.link);

	if (tdev) {
		dev->hard_header_len = tdev->hard_header_len + sizeof(struct iphdr);
		dev->mtu = tdev->mtu - sizeof(struct iphdr);
	}
	dev->iflink = tunnel->parms.link;
}

static int
ipip_tunnel_ioctl (struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int err = 0;
	struct ip_tunnel_parm p;
	struct ip_tunnel *t;

	switch (cmd) {
	case SIOCGETTUNNEL:
		t = NULL;
		if (dev == ipip_fb_tunnel_dev) {
			if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p))) {
				err = -EFAULT;
				break;
			}
			t = ipip_tunnel_locate(&p, 0);
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
		if (p.iph.version != 4 || p.iph.protocol != IPPROTO_IPIP ||
		    p.iph.ihl != 5 || (p.iph.frag_off&htons(~IP_DF)))
			goto done;
		if (p.iph.ttl)
			p.iph.frag_off |= htons(IP_DF);

		t = ipip_tunnel_locate(&p, cmd == SIOCADDTUNNEL);

		if (dev != ipip_fb_tunnel_dev && cmd == SIOCCHGTUNNEL) {
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
				ipip_tunnel_unlink(t);
				t->parms.iph.saddr = p.iph.saddr;
				t->parms.iph.daddr = p.iph.daddr;
				memcpy(dev->dev_addr, &p.iph.saddr, 4);
				memcpy(dev->broadcast, &p.iph.daddr, 4);
				ipip_tunnel_link(t);
				netdev_state_change(dev);
			}
		}

		if (t) {
			err = 0;
			if (cmd == SIOCCHGTUNNEL) {
				t->parms.iph.ttl = p.iph.ttl;
				t->parms.iph.tos = p.iph.tos;
				t->parms.iph.frag_off = p.iph.frag_off;
				if (t->parms.link != p.link) {
					t->parms.link = p.link;
					ipip_tunnel_bind_dev(dev);
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

		if (dev == ipip_fb_tunnel_dev) {
			err = -EFAULT;
			if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p)))
				goto done;
			err = -ENOENT;
			if ((t = ipip_tunnel_locate(&p, 0)) == NULL)
				goto done;
			err = -EPERM;
			if (t->dev == ipip_fb_tunnel_dev)
				goto done;
			dev = t->dev;
		}
		unregister_netdevice(dev);
		err = 0;
		break;

	default:
		err = -EINVAL;
	}

done:
	return err;
}

static struct net_device_stats *ipip_tunnel_get_stats(struct net_device *dev)
{
	return &(((struct ip_tunnel*)netdev_priv(dev))->stat);
}

static int ipip_tunnel_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < 68 || new_mtu > 0xFFF8 - sizeof(struct iphdr))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static void ipip_tunnel_setup(struct net_device *dev)
{
	dev->uninit		= ipip_tunnel_uninit;
	dev->hard_start_xmit	= ipip_tunnel_xmit;
	dev->get_stats		= ipip_tunnel_get_stats;
	dev->do_ioctl		= ipip_tunnel_ioctl;
	dev->change_mtu		= ipip_tunnel_change_mtu;
	dev->destructor		= free_netdev;

	dev->type		= ARPHRD_TUNNEL;
	dev->hard_header_len 	= LL_MAX_HEADER + sizeof(struct iphdr);
	dev->mtu		= ETH_DATA_LEN - sizeof(struct iphdr);
	dev->flags		= IFF_NOARP;
	dev->iflink		= 0;
	dev->addr_len		= 4;
}

static int ipip_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel;

	tunnel = netdev_priv(dev);

	tunnel->dev = dev;
	strcpy(tunnel->parms.name, dev->name);

	memcpy(dev->dev_addr, &tunnel->parms.iph.saddr, 4);
	memcpy(dev->broadcast, &tunnel->parms.iph.daddr, 4);

	ipip_tunnel_bind_dev(dev);

	return 0;
}

static int __init ipip_fb_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct iphdr *iph = &tunnel->parms.iph;

	tunnel->dev = dev;
	strcpy(tunnel->parms.name, dev->name);

	iph->version		= 4;
	iph->protocol		= IPPROTO_IPIP;
	iph->ihl		= 5;

	dev_hold(dev);
	tunnels_wc[0]		= tunnel;
	return 0;
}

static struct xfrm_tunnel ipip_handler = {
	.handler	=	ipip_rcv,
	.err_handler	=	ipip_err,
	.priority	=	1,
};

static char banner[] __initdata =
	KERN_INFO "IPv4 over IPv4 tunneling driver\n";

static int __init ipip_init(void)
{
	int err;

	printk(banner);

	if (xfrm4_tunnel_register(&ipip_handler, AF_INET)) {
		printk(KERN_INFO "ipip init: can't register tunnel\n");
		return -EAGAIN;
	}

	ipip_fb_tunnel_dev = alloc_netdev(sizeof(struct ip_tunnel),
					   "tunl0",
					   ipip_tunnel_setup);
	if (!ipip_fb_tunnel_dev) {
		err = -ENOMEM;
		goto err1;
	}

	ipip_fb_tunnel_dev->init = ipip_fb_tunnel_init;

	if ((err = register_netdev(ipip_fb_tunnel_dev)))
		goto err2;
 out:
	return err;
 err2:
	free_netdev(ipip_fb_tunnel_dev);
 err1:
	xfrm4_tunnel_deregister(&ipip_handler, AF_INET);
	goto out;
}

static void __exit ipip_destroy_tunnels(void)
{
	int prio;

	for (prio = 1; prio < 4; prio++) {
		int h;
		for (h = 0; h < HASH_SIZE; h++) {
			struct ip_tunnel *t;
			while ((t = tunnels[prio][h]) != NULL)
				unregister_netdevice(t->dev);
		}
	}
}

static void __exit ipip_fini(void)
{
	if (xfrm4_tunnel_deregister(&ipip_handler, AF_INET))
		printk(KERN_INFO "ipip close: can't deregister tunnel\n");

	rtnl_lock();
	ipip_destroy_tunnels();
	unregister_netdevice(ipip_fb_tunnel_dev);
	rtnl_unlock();
}

module_init(ipip_init);
module_exit(ipip_fini);
MODULE_LICENSE("GPL");
