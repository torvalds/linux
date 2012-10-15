/*
 * ip_vs_xmit.c: various packet transmitters for IPVS
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *              Julian Anastasov <ja@ssi.bg>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *
 * Description of forwarding methods:
 * - all transmitters are called from LOCAL_IN (remote clients) and
 * LOCAL_OUT (local clients) but for ICMP can be called from FORWARD
 * - not all connections have destination server, for example,
 * connections in backup server when fwmark is used
 * - bypass connections use daddr from packet
 * LOCAL_OUT rules:
 * - skb->dev is NULL, skb->protocol is not set (both are set in POST_ROUTING)
 * - skb->pkt_type is not set yet
 * - the only place where we can see skb->sk != NULL
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/tcp.h>                  /* for tcphdr */
#include <net/ip.h>
#include <net/tcp.h>                    /* for csum_tcpudp_magic */
#include <net/udp.h>
#include <net/icmp.h>                   /* for icmp_send */
#include <net/route.h>                  /* for ip_route_output */
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <linux/icmpv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <net/ip_vs.h>

enum {
	IP_VS_RT_MODE_LOCAL	= 1, /* Allow local dest */
	IP_VS_RT_MODE_NON_LOCAL	= 2, /* Allow non-local dest */
	IP_VS_RT_MODE_RDR	= 4, /* Allow redirect from remote daddr to
				      * local
				      */
	IP_VS_RT_MODE_CONNECT	= 8, /* Always bind route to saddr */
	IP_VS_RT_MODE_KNOWN_NH	= 16,/* Route via remote addr */
};

/*
 *      Destination cache to speed up outgoing route lookup
 */
static inline void
__ip_vs_dst_set(struct ip_vs_dest *dest, u32 rtos, struct dst_entry *dst,
		u32 dst_cookie)
{
	struct dst_entry *old_dst;

	old_dst = dest->dst_cache;
	dest->dst_cache = dst;
	dest->dst_rtos = rtos;
	dest->dst_cookie = dst_cookie;
	dst_release(old_dst);
}

static inline struct dst_entry *
__ip_vs_dst_check(struct ip_vs_dest *dest, u32 rtos)
{
	struct dst_entry *dst = dest->dst_cache;

	if (!dst)
		return NULL;
	if ((dst->obsolete || rtos != dest->dst_rtos) &&
	    dst->ops->check(dst, dest->dst_cookie) == NULL) {
		dest->dst_cache = NULL;
		dst_release(dst);
		return NULL;
	}
	dst_hold(dst);
	return dst;
}

static inline bool
__mtu_check_toobig_v6(const struct sk_buff *skb, u32 mtu)
{
	if (IP6CB(skb)->frag_max_size) {
		/* frag_max_size tell us that, this packet have been
		 * defragmented by netfilter IPv6 conntrack module.
		 */
		if (IP6CB(skb)->frag_max_size > mtu)
			return true; /* largest fragment violate MTU */
	}
	else if (skb->len > mtu && !skb_is_gso(skb)) {
		return true; /* Packet size violate MTU size */
	}
	return false;
}

/* Get route to daddr, update *saddr, optionally bind route to saddr */
static struct rtable *do_output_route4(struct net *net, __be32 daddr,
				       u32 rtos, int rt_mode, __be32 *saddr)
{
	struct flowi4 fl4;
	struct rtable *rt;
	int loop = 0;

	memset(&fl4, 0, sizeof(fl4));
	fl4.daddr = daddr;
	fl4.saddr = (rt_mode & IP_VS_RT_MODE_CONNECT) ? *saddr : 0;
	fl4.flowi4_tos = rtos;
	fl4.flowi4_flags = (rt_mode & IP_VS_RT_MODE_KNOWN_NH) ?
			   FLOWI_FLAG_KNOWN_NH : 0;

retry:
	rt = ip_route_output_key(net, &fl4);
	if (IS_ERR(rt)) {
		/* Invalid saddr ? */
		if (PTR_ERR(rt) == -EINVAL && *saddr &&
		    rt_mode & IP_VS_RT_MODE_CONNECT && !loop) {
			*saddr = 0;
			flowi4_update_output(&fl4, 0, rtos, daddr, 0);
			goto retry;
		}
		IP_VS_DBG_RL("ip_route_output error, dest: %pI4\n", &daddr);
		return NULL;
	} else if (!*saddr && rt_mode & IP_VS_RT_MODE_CONNECT && fl4.saddr) {
		ip_rt_put(rt);
		*saddr = fl4.saddr;
		flowi4_update_output(&fl4, 0, rtos, daddr, fl4.saddr);
		loop++;
		goto retry;
	}
	*saddr = fl4.saddr;
	return rt;
}

/* Get route to destination or remote server */
static struct rtable *
__ip_vs_get_out_rt(struct sk_buff *skb, struct ip_vs_dest *dest,
		   __be32 daddr, u32 rtos, int rt_mode, __be32 *ret_saddr)
{
	struct net *net = dev_net(skb_dst(skb)->dev);
	struct rtable *rt;			/* Route to the other host */
	struct rtable *ort;			/* Original route */
	int local;

	if (dest) {
		spin_lock(&dest->dst_lock);
		if (!(rt = (struct rtable *)
		      __ip_vs_dst_check(dest, rtos))) {
			rt = do_output_route4(net, dest->addr.ip, rtos,
					      rt_mode, &dest->dst_saddr.ip);
			if (!rt) {
				spin_unlock(&dest->dst_lock);
				return NULL;
			}
			__ip_vs_dst_set(dest, rtos, dst_clone(&rt->dst), 0);
			IP_VS_DBG(10, "new dst %pI4, src %pI4, refcnt=%d, "
				  "rtos=%X\n",
				  &dest->addr.ip, &dest->dst_saddr.ip,
				  atomic_read(&rt->dst.__refcnt), rtos);
		}
		daddr = dest->addr.ip;
		if (ret_saddr)
			*ret_saddr = dest->dst_saddr.ip;
		spin_unlock(&dest->dst_lock);
	} else {
		__be32 saddr = htonl(INADDR_ANY);

		/* For such unconfigured boxes avoid many route lookups
		 * for performance reasons because we do not remember saddr
		 */
		rt_mode &= ~IP_VS_RT_MODE_CONNECT;
		rt = do_output_route4(net, daddr, rtos, rt_mode, &saddr);
		if (!rt)
			return NULL;
		if (ret_saddr)
			*ret_saddr = saddr;
	}

	local = rt->rt_flags & RTCF_LOCAL;
	if (!((local ? IP_VS_RT_MODE_LOCAL : IP_VS_RT_MODE_NON_LOCAL) &
	      rt_mode)) {
		IP_VS_DBG_RL("Stopping traffic to %s address, dest: %pI4\n",
			     (rt->rt_flags & RTCF_LOCAL) ?
			     "local":"non-local", &daddr);
		ip_rt_put(rt);
		return NULL;
	}
	if (local && !(rt_mode & IP_VS_RT_MODE_RDR) &&
	    !((ort = skb_rtable(skb)) && ort->rt_flags & RTCF_LOCAL)) {
		IP_VS_DBG_RL("Redirect from non-local address %pI4 to local "
			     "requires NAT method, dest: %pI4\n",
			     &ip_hdr(skb)->daddr, &daddr);
		ip_rt_put(rt);
		return NULL;
	}
	if (unlikely(!local && ipv4_is_loopback(ip_hdr(skb)->saddr))) {
		IP_VS_DBG_RL("Stopping traffic from loopback address %pI4 "
			     "to non-local address, dest: %pI4\n",
			     &ip_hdr(skb)->saddr, &daddr);
		ip_rt_put(rt);
		return NULL;
	}

	return rt;
}

/* Reroute packet to local IPv4 stack after DNAT */
static int
__ip_vs_reroute_locally(struct sk_buff *skb)
{
	struct rtable *rt = skb_rtable(skb);
	struct net_device *dev = rt->dst.dev;
	struct net *net = dev_net(dev);
	struct iphdr *iph = ip_hdr(skb);

	if (rt_is_input_route(rt)) {
		unsigned long orefdst = skb->_skb_refdst;

		if (ip_route_input(skb, iph->daddr, iph->saddr,
				   iph->tos, skb->dev))
			return 0;
		refdst_drop(orefdst);
	} else {
		struct flowi4 fl4 = {
			.daddr = iph->daddr,
			.saddr = iph->saddr,
			.flowi4_tos = RT_TOS(iph->tos),
			.flowi4_mark = skb->mark,
		};

		rt = ip_route_output_key(net, &fl4);
		if (IS_ERR(rt))
			return 0;
		if (!(rt->rt_flags & RTCF_LOCAL)) {
			ip_rt_put(rt);
			return 0;
		}
		/* Drop old route. */
		skb_dst_drop(skb);
		skb_dst_set(skb, &rt->dst);
	}
	return 1;
}

#ifdef CONFIG_IP_VS_IPV6

static inline int __ip_vs_is_local_route6(struct rt6_info *rt)
{
	return rt->dst.dev && rt->dst.dev->flags & IFF_LOOPBACK;
}

static struct dst_entry *
__ip_vs_route_output_v6(struct net *net, struct in6_addr *daddr,
			struct in6_addr *ret_saddr, int do_xfrm)
{
	struct dst_entry *dst;
	struct flowi6 fl6 = {
		.daddr = *daddr,
	};

	dst = ip6_route_output(net, NULL, &fl6);
	if (dst->error)
		goto out_err;
	if (!ret_saddr)
		return dst;
	if (ipv6_addr_any(&fl6.saddr) &&
	    ipv6_dev_get_saddr(net, ip6_dst_idev(dst)->dev,
			       &fl6.daddr, 0, &fl6.saddr) < 0)
		goto out_err;
	if (do_xfrm) {
		dst = xfrm_lookup(net, dst, flowi6_to_flowi(&fl6), NULL, 0);
		if (IS_ERR(dst)) {
			dst = NULL;
			goto out_err;
		}
	}
	*ret_saddr = fl6.saddr;
	return dst;

out_err:
	dst_release(dst);
	IP_VS_DBG_RL("ip6_route_output error, dest: %pI6\n", daddr);
	return NULL;
}

/*
 * Get route to destination or remote server
 */
static struct rt6_info *
__ip_vs_get_out_rt_v6(struct sk_buff *skb, struct ip_vs_dest *dest,
		      struct in6_addr *daddr, struct in6_addr *ret_saddr,
		      int do_xfrm, int rt_mode)
{
	struct net *net = dev_net(skb_dst(skb)->dev);
	struct rt6_info *rt;			/* Route to the other host */
	struct rt6_info *ort;			/* Original route */
	struct dst_entry *dst;
	int local;

	if (dest) {
		spin_lock(&dest->dst_lock);
		rt = (struct rt6_info *)__ip_vs_dst_check(dest, 0);
		if (!rt) {
			u32 cookie;

			dst = __ip_vs_route_output_v6(net, &dest->addr.in6,
						      &dest->dst_saddr.in6,
						      do_xfrm);
			if (!dst) {
				spin_unlock(&dest->dst_lock);
				return NULL;
			}
			rt = (struct rt6_info *) dst;
			cookie = rt->rt6i_node ? rt->rt6i_node->fn_sernum : 0;
			__ip_vs_dst_set(dest, 0, dst_clone(&rt->dst), cookie);
			IP_VS_DBG(10, "new dst %pI6, src %pI6, refcnt=%d\n",
				  &dest->addr.in6, &dest->dst_saddr.in6,
				  atomic_read(&rt->dst.__refcnt));
		}
		if (ret_saddr)
			*ret_saddr = dest->dst_saddr.in6;
		spin_unlock(&dest->dst_lock);
	} else {
		dst = __ip_vs_route_output_v6(net, daddr, ret_saddr, do_xfrm);
		if (!dst)
			return NULL;
		rt = (struct rt6_info *) dst;
	}

	local = __ip_vs_is_local_route6(rt);
	if (!((local ? IP_VS_RT_MODE_LOCAL : IP_VS_RT_MODE_NON_LOCAL) &
	      rt_mode)) {
		IP_VS_DBG_RL("Stopping traffic to %s address, dest: %pI6\n",
			     local ? "local":"non-local", daddr);
		dst_release(&rt->dst);
		return NULL;
	}
	if (local && !(rt_mode & IP_VS_RT_MODE_RDR) &&
	    !((ort = (struct rt6_info *) skb_dst(skb)) &&
	      __ip_vs_is_local_route6(ort))) {
		IP_VS_DBG_RL("Redirect from non-local address %pI6 to local "
			     "requires NAT method, dest: %pI6\n",
			     &ipv6_hdr(skb)->daddr, daddr);
		dst_release(&rt->dst);
		return NULL;
	}
	if (unlikely(!local && (!skb->dev || skb->dev->flags & IFF_LOOPBACK) &&
		     ipv6_addr_type(&ipv6_hdr(skb)->saddr) &
				    IPV6_ADDR_LOOPBACK)) {
		IP_VS_DBG_RL("Stopping traffic from loopback address %pI6 "
			     "to non-local address, dest: %pI6\n",
			     &ipv6_hdr(skb)->saddr, daddr);
		dst_release(&rt->dst);
		return NULL;
	}

	return rt;
}
#endif


/*
 *	Release dest->dst_cache before a dest is removed
 */
void
ip_vs_dst_reset(struct ip_vs_dest *dest)
{
	struct dst_entry *old_dst;

	old_dst = dest->dst_cache;
	dest->dst_cache = NULL;
	dst_release(old_dst);
	dest->dst_saddr.ip = 0;
}

#define IP_VS_XMIT_TUNNEL(skb, cp)				\
({								\
	int __ret = NF_ACCEPT;					\
								\
	(skb)->ipvs_property = 1;				\
	if (unlikely((cp)->flags & IP_VS_CONN_F_NFCT))		\
		__ret = ip_vs_confirm_conntrack(skb);		\
	if (__ret == NF_ACCEPT) {				\
		nf_reset(skb);					\
		skb_forward_csum(skb);				\
	}							\
	__ret;							\
})

#define IP_VS_XMIT_NAT(pf, skb, cp, local)		\
do {							\
	(skb)->ipvs_property = 1;			\
	if (likely(!((cp)->flags & IP_VS_CONN_F_NFCT)))	\
		ip_vs_notrack(skb);			\
	else						\
		ip_vs_update_conntrack(skb, cp, 1);	\
	if (local)					\
		return NF_ACCEPT;			\
	skb_forward_csum(skb);				\
	NF_HOOK(pf, NF_INET_LOCAL_OUT, (skb), NULL,	\
		skb_dst(skb)->dev, dst_output);		\
} while (0)

#define IP_VS_XMIT(pf, skb, cp, local)			\
do {							\
	(skb)->ipvs_property = 1;			\
	if (likely(!((cp)->flags & IP_VS_CONN_F_NFCT)))	\
		ip_vs_notrack(skb);			\
	if (local)					\
		return NF_ACCEPT;			\
	skb_forward_csum(skb);				\
	NF_HOOK(pf, NF_INET_LOCAL_OUT, (skb), NULL,	\
		skb_dst(skb)->dev, dst_output);		\
} while (0)


/*
 *      NULL transmitter (do nothing except return NF_ACCEPT)
 */
int
ip_vs_null_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		struct ip_vs_protocol *pp)
{
	/* we do not touch skb and do not need pskb ptr */
	IP_VS_XMIT(NFPROTO_IPV4, skb, cp, 1);
}


/*
 *      Bypass transmitter
 *      Let packets bypass the destination when the destination is not
 *      available, it may be only used in transparent cache cluster.
 */
int
ip_vs_bypass_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		  struct ip_vs_protocol *pp)
{
	struct rtable *rt;			/* Route to the other host */
	struct iphdr  *iph = ip_hdr(skb);
	int    mtu;

	EnterFunction(10);

	if (!(rt = __ip_vs_get_out_rt(skb, NULL, iph->daddr, RT_TOS(iph->tos),
				      IP_VS_RT_MODE_NON_LOCAL, NULL)))
		goto tx_error_icmp;

	/* MTU checking */
	mtu = dst_mtu(&rt->dst);
	if ((skb->len > mtu) && (iph->frag_off & htons(IP_DF)) &&
	    !skb_is_gso(skb)) {
		ip_rt_put(rt);
		icmp_send(skb, ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED, htonl(mtu));
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error;
	}

	/*
	 * Call ip_send_check because we are not sure it is called
	 * after ip_defrag. Is copy-on-write needed?
	 */
	if (unlikely((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)) {
		ip_rt_put(rt);
		return NF_STOLEN;
	}
	ip_send_check(ip_hdr(skb));

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(NFPROTO_IPV4, skb, cp, 0);

	LeaveFunction(10);
	return NF_STOLEN;

 tx_error_icmp:
	dst_link_failure(skb);
 tx_error:
	kfree_skb(skb);
	LeaveFunction(10);
	return NF_STOLEN;
}

#ifdef CONFIG_IP_VS_IPV6
int
ip_vs_bypass_xmit_v6(struct sk_buff *skb, struct ip_vs_conn *cp,
		     struct ip_vs_protocol *pp)
{
	struct rt6_info *rt;			/* Route to the other host */
	struct ipv6hdr  *iph = ipv6_hdr(skb);
	int    mtu;

	EnterFunction(10);

	if (!(rt = __ip_vs_get_out_rt_v6(skb, NULL, &iph->daddr, NULL, 0,
					 IP_VS_RT_MODE_NON_LOCAL)))
		goto tx_error_icmp;

	/* MTU checking */
	mtu = dst_mtu(&rt->dst);
	if (__mtu_check_toobig_v6(skb, mtu)) {
		if (!skb->dev) {
			struct net *net = dev_net(skb_dst(skb)->dev);

			skb->dev = net->loopback_dev;
		}
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		dst_release(&rt->dst);
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error;
	}

	/*
	 * Call ip_send_check because we are not sure it is called
	 * after ip_defrag. Is copy-on-write needed?
	 */
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(skb == NULL)) {
		dst_release(&rt->dst);
		return NF_STOLEN;
	}

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(NFPROTO_IPV6, skb, cp, 0);

	LeaveFunction(10);
	return NF_STOLEN;

 tx_error_icmp:
	dst_link_failure(skb);
 tx_error:
	kfree_skb(skb);
	LeaveFunction(10);
	return NF_STOLEN;
}
#endif

/*
 *      NAT transmitter (only for outside-to-inside nat forwarding)
 *      Not used for related ICMP
 */
int
ip_vs_nat_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
	       struct ip_vs_protocol *pp)
{
	struct rtable *rt;		/* Route to the other host */
	int mtu;
	struct iphdr *iph = ip_hdr(skb);
	int local;

	EnterFunction(10);

	/* check if it is a connection of no-client-port */
	if (unlikely(cp->flags & IP_VS_CONN_F_NO_CPORT)) {
		__be16 _pt, *p;
		p = skb_header_pointer(skb, iph->ihl*4, sizeof(_pt), &_pt);
		if (p == NULL)
			goto tx_error;
		ip_vs_conn_fill_cport(cp, *p);
		IP_VS_DBG(10, "filled cport=%d\n", ntohs(*p));
	}

	if (!(rt = __ip_vs_get_out_rt(skb, cp->dest, cp->daddr.ip,
				      RT_TOS(iph->tos),
				      IP_VS_RT_MODE_LOCAL |
					IP_VS_RT_MODE_NON_LOCAL |
					IP_VS_RT_MODE_RDR, NULL)))
		goto tx_error_icmp;
	local = rt->rt_flags & RTCF_LOCAL;
	/*
	 * Avoid duplicate tuple in reply direction for NAT traffic
	 * to local address when connection is sync-ed
	 */
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	if (cp->flags & IP_VS_CONN_F_SYNC && local) {
		enum ip_conntrack_info ctinfo;
		struct nf_conn *ct = ct = nf_ct_get(skb, &ctinfo);

		if (ct && !nf_ct_is_untracked(ct)) {
			IP_VS_DBG_RL_PKT(10, AF_INET, pp, skb, 0,
					 "ip_vs_nat_xmit(): "
					 "stopping DNAT to local address");
			goto tx_error_put;
		}
	}
#endif

	/* From world but DNAT to loopback address? */
	if (local && ipv4_is_loopback(cp->daddr.ip) &&
	    rt_is_input_route(skb_rtable(skb))) {
		IP_VS_DBG_RL_PKT(1, AF_INET, pp, skb, 0, "ip_vs_nat_xmit(): "
				 "stopping DNAT to loopback address");
		goto tx_error_put;
	}

	/* MTU checking */
	mtu = dst_mtu(&rt->dst);
	if ((skb->len > mtu) && (iph->frag_off & htons(IP_DF)) &&
	    !skb_is_gso(skb)) {
		icmp_send(skb, ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED, htonl(mtu));
		IP_VS_DBG_RL_PKT(0, AF_INET, pp, skb, 0,
				 "ip_vs_nat_xmit(): frag needed for");
		goto tx_error_put;
	}

	/* copy-on-write the packet before mangling it */
	if (!skb_make_writable(skb, sizeof(struct iphdr)))
		goto tx_error_put;

	if (skb_cow(skb, rt->dst.dev->hard_header_len))
		goto tx_error_put;

	/* mangle the packet */
	if (pp->dnat_handler && !pp->dnat_handler(skb, pp, cp))
		goto tx_error_put;
	ip_hdr(skb)->daddr = cp->daddr.ip;
	ip_send_check(ip_hdr(skb));

	if (!local) {
		/* drop old route */
		skb_dst_drop(skb);
		skb_dst_set(skb, &rt->dst);
	} else {
		ip_rt_put(rt);
		/*
		 * Some IPv4 replies get local address from routes,
		 * not from iph, so while we DNAT after routing
		 * we need this second input/output route.
		 */
		if (!__ip_vs_reroute_locally(skb))
			goto tx_error;
	}

	IP_VS_DBG_PKT(10, AF_INET, pp, skb, 0, "After DNAT");

	/* FIXME: when application helper enlarges the packet and the length
	   is larger than the MTU of outgoing device, there will be still
	   MTU problem. */

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT_NAT(NFPROTO_IPV4, skb, cp, local);

	LeaveFunction(10);
	return NF_STOLEN;

  tx_error_icmp:
	dst_link_failure(skb);
  tx_error:
	kfree_skb(skb);
	LeaveFunction(10);
	return NF_STOLEN;
  tx_error_put:
	ip_rt_put(rt);
	goto tx_error;
}

#ifdef CONFIG_IP_VS_IPV6
int
ip_vs_nat_xmit_v6(struct sk_buff *skb, struct ip_vs_conn *cp,
		  struct ip_vs_protocol *pp)
{
	struct rt6_info *rt;		/* Route to the other host */
	int mtu;
	int local;

	EnterFunction(10);

	/* check if it is a connection of no-client-port */
	if (unlikely(cp->flags & IP_VS_CONN_F_NO_CPORT)) {
		__be16 _pt, *p;
		p = skb_header_pointer(skb, sizeof(struct ipv6hdr),
				       sizeof(_pt), &_pt);
		if (p == NULL)
			goto tx_error;
		ip_vs_conn_fill_cport(cp, *p);
		IP_VS_DBG(10, "filled cport=%d\n", ntohs(*p));
	}

	if (!(rt = __ip_vs_get_out_rt_v6(skb, cp->dest, &cp->daddr.in6, NULL,
					 0, (IP_VS_RT_MODE_LOCAL |
					     IP_VS_RT_MODE_NON_LOCAL |
					     IP_VS_RT_MODE_RDR))))
		goto tx_error_icmp;
	local = __ip_vs_is_local_route6(rt);
	/*
	 * Avoid duplicate tuple in reply direction for NAT traffic
	 * to local address when connection is sync-ed
	 */
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	if (cp->flags & IP_VS_CONN_F_SYNC && local) {
		enum ip_conntrack_info ctinfo;
		struct nf_conn *ct = ct = nf_ct_get(skb, &ctinfo);

		if (ct && !nf_ct_is_untracked(ct)) {
			IP_VS_DBG_RL_PKT(10, AF_INET6, pp, skb, 0,
					 "ip_vs_nat_xmit_v6(): "
					 "stopping DNAT to local address");
			goto tx_error_put;
		}
	}
#endif

	/* From world but DNAT to loopback address? */
	if (local && skb->dev && !(skb->dev->flags & IFF_LOOPBACK) &&
	    ipv6_addr_type(&rt->rt6i_dst.addr) & IPV6_ADDR_LOOPBACK) {
		IP_VS_DBG_RL_PKT(1, AF_INET6, pp, skb, 0,
				 "ip_vs_nat_xmit_v6(): "
				 "stopping DNAT to loopback address");
		goto tx_error_put;
	}

	/* MTU checking */
	mtu = dst_mtu(&rt->dst);
	if (__mtu_check_toobig_v6(skb, mtu)) {
		if (!skb->dev) {
			struct net *net = dev_net(skb_dst(skb)->dev);

			skb->dev = net->loopback_dev;
		}
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		IP_VS_DBG_RL_PKT(0, AF_INET6, pp, skb, 0,
				 "ip_vs_nat_xmit_v6(): frag needed for");
		goto tx_error_put;
	}

	/* copy-on-write the packet before mangling it */
	if (!skb_make_writable(skb, sizeof(struct ipv6hdr)))
		goto tx_error_put;

	if (skb_cow(skb, rt->dst.dev->hard_header_len))
		goto tx_error_put;

	/* mangle the packet */
	if (pp->dnat_handler && !pp->dnat_handler(skb, pp, cp))
		goto tx_error;
	ipv6_hdr(skb)->daddr = cp->daddr.in6;

	if (!local || !skb->dev) {
		/* drop the old route when skb is not shared */
		skb_dst_drop(skb);
		skb_dst_set(skb, &rt->dst);
	} else {
		/* destined to loopback, do we need to change route? */
		dst_release(&rt->dst);
	}

	IP_VS_DBG_PKT(10, AF_INET6, pp, skb, 0, "After DNAT");

	/* FIXME: when application helper enlarges the packet and the length
	   is larger than the MTU of outgoing device, there will be still
	   MTU problem. */

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT_NAT(NFPROTO_IPV6, skb, cp, local);

	LeaveFunction(10);
	return NF_STOLEN;

tx_error_icmp:
	dst_link_failure(skb);
tx_error:
	LeaveFunction(10);
	kfree_skb(skb);
	return NF_STOLEN;
tx_error_put:
	dst_release(&rt->dst);
	goto tx_error;
}
#endif


/*
 *   IP Tunneling transmitter
 *
 *   This function encapsulates the packet in a new IP packet, its
 *   destination will be set to cp->daddr. Most code of this function
 *   is taken from ipip.c.
 *
 *   It is used in VS/TUN cluster. The load balancer selects a real
 *   server from a cluster based on a scheduling algorithm,
 *   encapsulates the request packet and forwards it to the selected
 *   server. For example, all real servers are configured with
 *   "ifconfig tunl0 <Virtual IP Address> up". When the server receives
 *   the encapsulated packet, it will decapsulate the packet, processe
 *   the request and return the response packets directly to the client
 *   without passing the load balancer. This can greatly increase the
 *   scalability of virtual server.
 *
 *   Used for ANY protocol
 */
int
ip_vs_tunnel_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		  struct ip_vs_protocol *pp)
{
	struct netns_ipvs *ipvs = net_ipvs(skb_net(skb));
	struct rtable *rt;			/* Route to the other host */
	__be32 saddr;				/* Source for tunnel */
	struct net_device *tdev;		/* Device to other host */
	struct iphdr  *old_iph = ip_hdr(skb);
	u8     tos = old_iph->tos;
	__be16 df;
	struct iphdr  *iph;			/* Our new IP header */
	unsigned int max_headroom;		/* The extra header space needed */
	int    mtu;
	int ret;

	EnterFunction(10);

	if (!(rt = __ip_vs_get_out_rt(skb, cp->dest, cp->daddr.ip,
				      RT_TOS(tos), IP_VS_RT_MODE_LOCAL |
						   IP_VS_RT_MODE_NON_LOCAL |
						   IP_VS_RT_MODE_CONNECT,
						   &saddr)))
		goto tx_error_icmp;
	if (rt->rt_flags & RTCF_LOCAL) {
		ip_rt_put(rt);
		IP_VS_XMIT(NFPROTO_IPV4, skb, cp, 1);
	}

	tdev = rt->dst.dev;

	mtu = dst_mtu(&rt->dst) - sizeof(struct iphdr);
	if (mtu < 68) {
		IP_VS_DBG_RL("%s(): mtu less than 68\n", __func__);
		goto tx_error_put;
	}
	if (rt_is_output_route(skb_rtable(skb)))
		skb_dst(skb)->ops->update_pmtu(skb_dst(skb), NULL, skb, mtu);

	/* Copy DF, reset fragment offset and MF */
	df = sysctl_pmtu_disc(ipvs) ? old_iph->frag_off & htons(IP_DF) : 0;

	if (df && mtu < ntohs(old_iph->tot_len) && !skb_is_gso(skb)) {
		icmp_send(skb, ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED, htonl(mtu));
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error_put;
	}

	/*
	 * Okay, now see if we can stuff it in the buffer as-is.
	 */
	max_headroom = LL_RESERVED_SPACE(tdev) + sizeof(struct iphdr);

	if (skb_headroom(skb) < max_headroom
	    || skb_cloned(skb) || skb_shared(skb)) {
		struct sk_buff *new_skb =
			skb_realloc_headroom(skb, max_headroom);
		if (!new_skb) {
			ip_rt_put(rt);
			kfree_skb(skb);
			IP_VS_ERR_RL("%s(): no memory\n", __func__);
			return NF_STOLEN;
		}
		consume_skb(skb);
		skb = new_skb;
		old_iph = ip_hdr(skb);
	}

	skb->transport_header = skb->network_header;

	/* fix old IP header checksum */
	ip_send_check(old_iph);

	skb_push(skb, sizeof(struct iphdr));
	skb_reset_network_header(skb);
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);

	/*
	 *	Push down and install the IPIP header.
	 */
	iph			=	ip_hdr(skb);
	iph->version		=	4;
	iph->ihl		=	sizeof(struct iphdr)>>2;
	iph->frag_off		=	df;
	iph->protocol		=	IPPROTO_IPIP;
	iph->tos		=	tos;
	iph->daddr		=	cp->daddr.ip;
	iph->saddr		=	saddr;
	iph->ttl		=	old_iph->ttl;
	ip_select_ident(iph, &rt->dst, NULL);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	ret = IP_VS_XMIT_TUNNEL(skb, cp);
	if (ret == NF_ACCEPT)
		ip_local_out(skb);
	else if (ret == NF_DROP)
		kfree_skb(skb);

	LeaveFunction(10);

	return NF_STOLEN;

  tx_error_icmp:
	dst_link_failure(skb);
  tx_error:
	kfree_skb(skb);
	LeaveFunction(10);
	return NF_STOLEN;
tx_error_put:
	ip_rt_put(rt);
	goto tx_error;
}

#ifdef CONFIG_IP_VS_IPV6
int
ip_vs_tunnel_xmit_v6(struct sk_buff *skb, struct ip_vs_conn *cp,
		     struct ip_vs_protocol *pp)
{
	struct rt6_info *rt;		/* Route to the other host */
	struct in6_addr saddr;		/* Source for tunnel */
	struct net_device *tdev;	/* Device to other host */
	struct ipv6hdr  *old_iph = ipv6_hdr(skb);
	struct ipv6hdr  *iph;		/* Our new IP header */
	unsigned int max_headroom;	/* The extra header space needed */
	int    mtu;
	int ret;

	EnterFunction(10);

	if (!(rt = __ip_vs_get_out_rt_v6(skb, cp->dest, &cp->daddr.in6,
					 &saddr, 1, (IP_VS_RT_MODE_LOCAL |
						     IP_VS_RT_MODE_NON_LOCAL))))
		goto tx_error_icmp;
	if (__ip_vs_is_local_route6(rt)) {
		dst_release(&rt->dst);
		IP_VS_XMIT(NFPROTO_IPV6, skb, cp, 1);
	}

	tdev = rt->dst.dev;

	mtu = dst_mtu(&rt->dst) - sizeof(struct ipv6hdr);
	if (mtu < IPV6_MIN_MTU) {
		IP_VS_DBG_RL("%s(): mtu less than %d\n", __func__,
			     IPV6_MIN_MTU);
		goto tx_error_put;
	}
	if (skb_dst(skb))
		skb_dst(skb)->ops->update_pmtu(skb_dst(skb), NULL, skb, mtu);

	/* MTU checking: Notice that 'mtu' have been adjusted before hand */
	if (__mtu_check_toobig_v6(skb, mtu)) {
		if (!skb->dev) {
			struct net *net = dev_net(skb_dst(skb)->dev);

			skb->dev = net->loopback_dev;
		}
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error_put;
	}

	/*
	 * Okay, now see if we can stuff it in the buffer as-is.
	 */
	max_headroom = LL_RESERVED_SPACE(tdev) + sizeof(struct ipv6hdr);

	if (skb_headroom(skb) < max_headroom
	    || skb_cloned(skb) || skb_shared(skb)) {
		struct sk_buff *new_skb =
			skb_realloc_headroom(skb, max_headroom);
		if (!new_skb) {
			dst_release(&rt->dst);
			kfree_skb(skb);
			IP_VS_ERR_RL("%s(): no memory\n", __func__);
			return NF_STOLEN;
		}
		consume_skb(skb);
		skb = new_skb;
		old_iph = ipv6_hdr(skb);
	}

	skb->transport_header = skb->network_header;

	skb_push(skb, sizeof(struct ipv6hdr));
	skb_reset_network_header(skb);
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);

	/*
	 *	Push down and install the IPIP header.
	 */
	iph			=	ipv6_hdr(skb);
	iph->version		=	6;
	iph->nexthdr		=	IPPROTO_IPV6;
	iph->payload_len	=	old_iph->payload_len;
	be16_add_cpu(&iph->payload_len, sizeof(*old_iph));
	iph->priority		=	old_iph->priority;
	memset(&iph->flow_lbl, 0, sizeof(iph->flow_lbl));
	iph->daddr = cp->daddr.in6;
	iph->saddr = saddr;
	iph->hop_limit		=	old_iph->hop_limit;

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	ret = IP_VS_XMIT_TUNNEL(skb, cp);
	if (ret == NF_ACCEPT)
		ip6_local_out(skb);
	else if (ret == NF_DROP)
		kfree_skb(skb);

	LeaveFunction(10);

	return NF_STOLEN;

tx_error_icmp:
	dst_link_failure(skb);
tx_error:
	kfree_skb(skb);
	LeaveFunction(10);
	return NF_STOLEN;
tx_error_put:
	dst_release(&rt->dst);
	goto tx_error;
}
#endif


/*
 *      Direct Routing transmitter
 *      Used for ANY protocol
 */
int
ip_vs_dr_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
	      struct ip_vs_protocol *pp)
{
	struct rtable *rt;			/* Route to the other host */
	struct iphdr  *iph = ip_hdr(skb);
	int    mtu;

	EnterFunction(10);

	if (!(rt = __ip_vs_get_out_rt(skb, cp->dest, cp->daddr.ip,
				      RT_TOS(iph->tos),
				      IP_VS_RT_MODE_LOCAL |
				      IP_VS_RT_MODE_NON_LOCAL |
				      IP_VS_RT_MODE_KNOWN_NH, NULL)))
		goto tx_error_icmp;
	if (rt->rt_flags & RTCF_LOCAL) {
		ip_rt_put(rt);
		IP_VS_XMIT(NFPROTO_IPV4, skb, cp, 1);
	}

	/* MTU checking */
	mtu = dst_mtu(&rt->dst);
	if ((iph->frag_off & htons(IP_DF)) && skb->len > mtu &&
	    !skb_is_gso(skb)) {
		icmp_send(skb, ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED, htonl(mtu));
		ip_rt_put(rt);
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error;
	}

	/*
	 * Call ip_send_check because we are not sure it is called
	 * after ip_defrag. Is copy-on-write needed?
	 */
	if (unlikely((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)) {
		ip_rt_put(rt);
		return NF_STOLEN;
	}
	ip_send_check(ip_hdr(skb));

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(NFPROTO_IPV4, skb, cp, 0);

	LeaveFunction(10);
	return NF_STOLEN;

  tx_error_icmp:
	dst_link_failure(skb);
  tx_error:
	kfree_skb(skb);
	LeaveFunction(10);
	return NF_STOLEN;
}

#ifdef CONFIG_IP_VS_IPV6
int
ip_vs_dr_xmit_v6(struct sk_buff *skb, struct ip_vs_conn *cp,
		 struct ip_vs_protocol *pp)
{
	struct rt6_info *rt;			/* Route to the other host */
	int    mtu;

	EnterFunction(10);

	if (!(rt = __ip_vs_get_out_rt_v6(skb, cp->dest, &cp->daddr.in6, NULL,
					 0, (IP_VS_RT_MODE_LOCAL |
					     IP_VS_RT_MODE_NON_LOCAL))))
		goto tx_error_icmp;
	if (__ip_vs_is_local_route6(rt)) {
		dst_release(&rt->dst);
		IP_VS_XMIT(NFPROTO_IPV6, skb, cp, 1);
	}

	/* MTU checking */
	mtu = dst_mtu(&rt->dst);
	if (__mtu_check_toobig_v6(skb, mtu)) {
		if (!skb->dev) {
			struct net *net = dev_net(skb_dst(skb)->dev);

			skb->dev = net->loopback_dev;
		}
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		dst_release(&rt->dst);
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error;
	}

	/*
	 * Call ip_send_check because we are not sure it is called
	 * after ip_defrag. Is copy-on-write needed?
	 */
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(skb == NULL)) {
		dst_release(&rt->dst);
		return NF_STOLEN;
	}

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(NFPROTO_IPV6, skb, cp, 0);

	LeaveFunction(10);
	return NF_STOLEN;

tx_error_icmp:
	dst_link_failure(skb);
tx_error:
	kfree_skb(skb);
	LeaveFunction(10);
	return NF_STOLEN;
}
#endif


/*
 *	ICMP packet transmitter
 *	called by the ip_vs_in_icmp
 */
int
ip_vs_icmp_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		struct ip_vs_protocol *pp, int offset, unsigned int hooknum)
{
	struct rtable	*rt;	/* Route to the other host */
	int mtu;
	int rc;
	int local;
	int rt_mode;

	EnterFunction(10);

	/* The ICMP packet for VS/TUN, VS/DR and LOCALNODE will be
	   forwarded directly here, because there is no need to
	   translate address/port back */
	if (IP_VS_FWD_METHOD(cp) != IP_VS_CONN_F_MASQ) {
		if (cp->packet_xmit)
			rc = cp->packet_xmit(skb, cp, pp);
		else
			rc = NF_ACCEPT;
		/* do not touch skb anymore */
		atomic_inc(&cp->in_pkts);
		goto out;
	}

	/*
	 * mangle and send the packet here (only for VS/NAT)
	 */

	/* LOCALNODE from FORWARD hook is not supported */
	rt_mode = (hooknum != NF_INET_FORWARD) ?
		  IP_VS_RT_MODE_LOCAL | IP_VS_RT_MODE_NON_LOCAL |
		  IP_VS_RT_MODE_RDR : IP_VS_RT_MODE_NON_LOCAL;
	if (!(rt = __ip_vs_get_out_rt(skb, cp->dest, cp->daddr.ip,
				      RT_TOS(ip_hdr(skb)->tos),
				      rt_mode, NULL)))
		goto tx_error_icmp;
	local = rt->rt_flags & RTCF_LOCAL;

	/*
	 * Avoid duplicate tuple in reply direction for NAT traffic
	 * to local address when connection is sync-ed
	 */
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	if (cp->flags & IP_VS_CONN_F_SYNC && local) {
		enum ip_conntrack_info ctinfo;
		struct nf_conn *ct = ct = nf_ct_get(skb, &ctinfo);

		if (ct && !nf_ct_is_untracked(ct)) {
			IP_VS_DBG(10, "%s(): "
				  "stopping DNAT to local address %pI4\n",
				  __func__, &cp->daddr.ip);
			goto tx_error_put;
		}
	}
#endif

	/* From world but DNAT to loopback address? */
	if (local && ipv4_is_loopback(cp->daddr.ip) &&
	    rt_is_input_route(skb_rtable(skb))) {
		IP_VS_DBG(1, "%s(): "
			  "stopping DNAT to loopback %pI4\n",
			  __func__, &cp->daddr.ip);
		goto tx_error_put;
	}

	/* MTU checking */
	mtu = dst_mtu(&rt->dst);
	if ((skb->len > mtu) && (ip_hdr(skb)->frag_off & htons(IP_DF)) &&
	    !skb_is_gso(skb)) {
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error_put;
	}

	/* copy-on-write the packet before mangling it */
	if (!skb_make_writable(skb, offset))
		goto tx_error_put;

	if (skb_cow(skb, rt->dst.dev->hard_header_len))
		goto tx_error_put;

	ip_vs_nat_icmp(skb, pp, cp, 0);

	if (!local) {
		/* drop the old route when skb is not shared */
		skb_dst_drop(skb);
		skb_dst_set(skb, &rt->dst);
	} else {
		ip_rt_put(rt);
		/*
		 * Some IPv4 replies get local address from routes,
		 * not from iph, so while we DNAT after routing
		 * we need this second input/output route.
		 */
		if (!__ip_vs_reroute_locally(skb))
			goto tx_error;
	}

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT_NAT(NFPROTO_IPV4, skb, cp, local);

	rc = NF_STOLEN;
	goto out;

  tx_error_icmp:
	dst_link_failure(skb);
  tx_error:
	dev_kfree_skb(skb);
	rc = NF_STOLEN;
  out:
	LeaveFunction(10);
	return rc;
  tx_error_put:
	ip_rt_put(rt);
	goto tx_error;
}

#ifdef CONFIG_IP_VS_IPV6
int
ip_vs_icmp_xmit_v6(struct sk_buff *skb, struct ip_vs_conn *cp,
		struct ip_vs_protocol *pp, int offset, unsigned int hooknum)
{
	struct rt6_info	*rt;	/* Route to the other host */
	int mtu;
	int rc;
	int local;
	int rt_mode;

	EnterFunction(10);

	/* The ICMP packet for VS/TUN, VS/DR and LOCALNODE will be
	   forwarded directly here, because there is no need to
	   translate address/port back */
	if (IP_VS_FWD_METHOD(cp) != IP_VS_CONN_F_MASQ) {
		if (cp->packet_xmit)
			rc = cp->packet_xmit(skb, cp, pp);
		else
			rc = NF_ACCEPT;
		/* do not touch skb anymore */
		atomic_inc(&cp->in_pkts);
		goto out;
	}

	/*
	 * mangle and send the packet here (only for VS/NAT)
	 */

	/* LOCALNODE from FORWARD hook is not supported */
	rt_mode = (hooknum != NF_INET_FORWARD) ?
		  IP_VS_RT_MODE_LOCAL | IP_VS_RT_MODE_NON_LOCAL |
		  IP_VS_RT_MODE_RDR : IP_VS_RT_MODE_NON_LOCAL;
	if (!(rt = __ip_vs_get_out_rt_v6(skb, cp->dest, &cp->daddr.in6, NULL,
					 0, rt_mode)))
		goto tx_error_icmp;

	local = __ip_vs_is_local_route6(rt);
	/*
	 * Avoid duplicate tuple in reply direction for NAT traffic
	 * to local address when connection is sync-ed
	 */
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	if (cp->flags & IP_VS_CONN_F_SYNC && local) {
		enum ip_conntrack_info ctinfo;
		struct nf_conn *ct = ct = nf_ct_get(skb, &ctinfo);

		if (ct && !nf_ct_is_untracked(ct)) {
			IP_VS_DBG(10, "%s(): "
				  "stopping DNAT to local address %pI6\n",
				  __func__, &cp->daddr.in6);
			goto tx_error_put;
		}
	}
#endif

	/* From world but DNAT to loopback address? */
	if (local && skb->dev && !(skb->dev->flags & IFF_LOOPBACK) &&
	    ipv6_addr_type(&rt->rt6i_dst.addr) & IPV6_ADDR_LOOPBACK) {
		IP_VS_DBG(1, "%s(): "
			  "stopping DNAT to loopback %pI6\n",
			  __func__, &cp->daddr.in6);
		goto tx_error_put;
	}

	/* MTU checking */
	mtu = dst_mtu(&rt->dst);
	if (__mtu_check_toobig_v6(skb, mtu)) {
		if (!skb->dev) {
			struct net *net = dev_net(skb_dst(skb)->dev);

			skb->dev = net->loopback_dev;
		}
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error_put;
	}

	/* copy-on-write the packet before mangling it */
	if (!skb_make_writable(skb, offset))
		goto tx_error_put;

	if (skb_cow(skb, rt->dst.dev->hard_header_len))
		goto tx_error_put;

	ip_vs_nat_icmp_v6(skb, pp, cp, 0);

	if (!local || !skb->dev) {
		/* drop the old route when skb is not shared */
		skb_dst_drop(skb);
		skb_dst_set(skb, &rt->dst);
	} else {
		/* destined to loopback, do we need to change route? */
		dst_release(&rt->dst);
	}

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT_NAT(NFPROTO_IPV6, skb, cp, local);

	rc = NF_STOLEN;
	goto out;

tx_error_icmp:
	dst_link_failure(skb);
tx_error:
	dev_kfree_skb(skb);
	rc = NF_STOLEN;
out:
	LeaveFunction(10);
	return rc;
tx_error_put:
	dst_release(&rt->dst);
	goto tx_error;
}
#endif
