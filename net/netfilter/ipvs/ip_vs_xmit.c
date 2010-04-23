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
#include <linux/icmpv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <net/ip_vs.h>


/*
 *      Destination cache to speed up outgoing route lookup
 */
static inline void
__ip_vs_dst_set(struct ip_vs_dest *dest, u32 rtos, struct dst_entry *dst)
{
	struct dst_entry *old_dst;

	old_dst = dest->dst_cache;
	dest->dst_cache = dst;
	dest->dst_rtos = rtos;
	dst_release(old_dst);
}

static inline struct dst_entry *
__ip_vs_dst_check(struct ip_vs_dest *dest, u32 rtos, u32 cookie)
{
	struct dst_entry *dst = dest->dst_cache;

	if (!dst)
		return NULL;
	if ((dst->obsolete
	     || (dest->af == AF_INET && rtos != dest->dst_rtos)) &&
	    dst->ops->check(dst, cookie) == NULL) {
		dest->dst_cache = NULL;
		dst_release(dst);
		return NULL;
	}
	dst_hold(dst);
	return dst;
}

static struct rtable *
__ip_vs_get_out_rt(struct ip_vs_conn *cp, u32 rtos)
{
	struct rtable *rt;			/* Route to the other host */
	struct ip_vs_dest *dest = cp->dest;

	if (dest) {
		spin_lock(&dest->dst_lock);
		if (!(rt = (struct rtable *)
		      __ip_vs_dst_check(dest, rtos, 0))) {
			struct flowi fl = {
				.oif = 0,
				.nl_u = {
					.ip4_u = {
						.daddr = dest->addr.ip,
						.saddr = 0,
						.tos = rtos, } },
			};

			if (ip_route_output_key(&init_net, &rt, &fl)) {
				spin_unlock(&dest->dst_lock);
				IP_VS_DBG_RL("ip_route_output error, dest: %pI4\n",
					     &dest->addr.ip);
				return NULL;
			}
			__ip_vs_dst_set(dest, rtos, dst_clone(&rt->u.dst));
			IP_VS_DBG(10, "new dst %pI4, refcnt=%d, rtos=%X\n",
				  &dest->addr.ip,
				  atomic_read(&rt->u.dst.__refcnt), rtos);
		}
		spin_unlock(&dest->dst_lock);
	} else {
		struct flowi fl = {
			.oif = 0,
			.nl_u = {
				.ip4_u = {
					.daddr = cp->daddr.ip,
					.saddr = 0,
					.tos = rtos, } },
		};

		if (ip_route_output_key(&init_net, &rt, &fl)) {
			IP_VS_DBG_RL("ip_route_output error, dest: %pI4\n",
				     &cp->daddr.ip);
			return NULL;
		}
	}

	return rt;
}

#ifdef CONFIG_IP_VS_IPV6
static struct rt6_info *
__ip_vs_get_out_rt_v6(struct ip_vs_conn *cp)
{
	struct rt6_info *rt;			/* Route to the other host */
	struct ip_vs_dest *dest = cp->dest;

	if (dest) {
		spin_lock(&dest->dst_lock);
		rt = (struct rt6_info *)__ip_vs_dst_check(dest, 0, 0);
		if (!rt) {
			struct flowi fl = {
				.oif = 0,
				.nl_u = {
					.ip6_u = {
						.daddr = dest->addr.in6,
						.saddr = {
							.s6_addr32 =
								{ 0, 0, 0, 0 },
						},
					},
				},
			};

			rt = (struct rt6_info *)ip6_route_output(&init_net,
								 NULL, &fl);
			if (!rt) {
				spin_unlock(&dest->dst_lock);
				IP_VS_DBG_RL("ip6_route_output error, dest: %pI6\n",
					     &dest->addr.in6);
				return NULL;
			}
			__ip_vs_dst_set(dest, 0, dst_clone(&rt->u.dst));
			IP_VS_DBG(10, "new dst %pI6, refcnt=%d\n",
				  &dest->addr.in6,
				  atomic_read(&rt->u.dst.__refcnt));
		}
		spin_unlock(&dest->dst_lock);
	} else {
		struct flowi fl = {
			.oif = 0,
			.nl_u = {
				.ip6_u = {
					.daddr = cp->daddr.in6,
					.saddr = {
						.s6_addr32 = { 0, 0, 0, 0 },
					},
				},
			},
		};

		rt = (struct rt6_info *)ip6_route_output(&init_net, NULL, &fl);
		if (!rt) {
			IP_VS_DBG_RL("ip6_route_output error, dest: %pI6\n",
				     &cp->daddr.in6);
			return NULL;
		}
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
}

#define IP_VS_XMIT(pf, skb, rt)				\
do {							\
	(skb)->ipvs_property = 1;			\
	skb_forward_csum(skb);				\
	NF_HOOK(pf, NF_INET_LOCAL_OUT, (skb), NULL,	\
		(rt)->u.dst.dev, dst_output);		\
} while (0)


/*
 *      NULL transmitter (do nothing except return NF_ACCEPT)
 */
int
ip_vs_null_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		struct ip_vs_protocol *pp)
{
	/* we do not touch skb and do not need pskb ptr */
	return NF_ACCEPT;
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
	u8     tos = iph->tos;
	int    mtu;
	struct flowi fl = {
		.oif = 0,
		.nl_u = {
			.ip4_u = {
				.daddr = iph->daddr,
				.saddr = 0,
				.tos = RT_TOS(tos), } },
	};

	EnterFunction(10);

	if (ip_route_output_key(&init_net, &rt, &fl)) {
		IP_VS_DBG_RL("%s(): ip_route_output error, dest: %pI4\n",
			     __func__, &iph->daddr);
		goto tx_error_icmp;
	}

	/* MTU checking */
	mtu = dst_mtu(&rt->u.dst);
	if ((skb->len > mtu) && (iph->frag_off & htons(IP_DF))) {
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
	skb_dst_set(skb, &rt->u.dst);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(PF_INET, skb, rt);

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
	struct flowi fl = {
		.oif = 0,
		.nl_u = {
			.ip6_u = {
				.daddr = iph->daddr,
				.saddr = { .s6_addr32 = {0, 0, 0, 0} }, } },
	};

	EnterFunction(10);

	rt = (struct rt6_info *)ip6_route_output(&init_net, NULL, &fl);
	if (!rt) {
		IP_VS_DBG_RL("%s(): ip6_route_output error, dest: %pI6\n",
			     __func__, &iph->daddr);
		goto tx_error_icmp;
	}

	/* MTU checking */
	mtu = dst_mtu(&rt->u.dst);
	if (skb->len > mtu) {
		dst_release(&rt->u.dst);
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error;
	}

	/*
	 * Call ip_send_check because we are not sure it is called
	 * after ip_defrag. Is copy-on-write needed?
	 */
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(skb == NULL)) {
		dst_release(&rt->u.dst);
		return NF_STOLEN;
	}

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->u.dst);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(PF_INET6, skb, rt);

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

	if (!(rt = __ip_vs_get_out_rt(cp, RT_TOS(iph->tos))))
		goto tx_error_icmp;

	/* MTU checking */
	mtu = dst_mtu(&rt->u.dst);
	if ((skb->len > mtu) && (iph->frag_off & htons(IP_DF))) {
		ip_rt_put(rt);
		icmp_send(skb, ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED, htonl(mtu));
		IP_VS_DBG_RL_PKT(0, pp, skb, 0, "ip_vs_nat_xmit(): frag needed for");
		goto tx_error;
	}

	/* copy-on-write the packet before mangling it */
	if (!skb_make_writable(skb, sizeof(struct iphdr)))
		goto tx_error_put;

	if (skb_cow(skb, rt->u.dst.dev->hard_header_len))
		goto tx_error_put;

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->u.dst);

	/* mangle the packet */
	if (pp->dnat_handler && !pp->dnat_handler(skb, pp, cp))
		goto tx_error;
	ip_hdr(skb)->daddr = cp->daddr.ip;
	ip_send_check(ip_hdr(skb));

	IP_VS_DBG_PKT(10, pp, skb, 0, "After DNAT");

	/* FIXME: when application helper enlarges the packet and the length
	   is larger than the MTU of outgoing device, there will be still
	   MTU problem. */

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(PF_INET, skb, rt);

	LeaveFunction(10);
	return NF_STOLEN;

  tx_error_icmp:
	dst_link_failure(skb);
  tx_error:
	LeaveFunction(10);
	kfree_skb(skb);
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

	rt = __ip_vs_get_out_rt_v6(cp);
	if (!rt)
		goto tx_error_icmp;

	/* MTU checking */
	mtu = dst_mtu(&rt->u.dst);
	if (skb->len > mtu) {
		dst_release(&rt->u.dst);
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		IP_VS_DBG_RL_PKT(0, pp, skb, 0,
				 "ip_vs_nat_xmit_v6(): frag needed for");
		goto tx_error;
	}

	/* copy-on-write the packet before mangling it */
	if (!skb_make_writable(skb, sizeof(struct ipv6hdr)))
		goto tx_error_put;

	if (skb_cow(skb, rt->u.dst.dev->hard_header_len))
		goto tx_error_put;

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->u.dst);

	/* mangle the packet */
	if (pp->dnat_handler && !pp->dnat_handler(skb, pp, cp))
		goto tx_error;
	ipv6_hdr(skb)->daddr = cp->daddr.in6;

	IP_VS_DBG_PKT(10, pp, skb, 0, "After DNAT");

	/* FIXME: when application helper enlarges the packet and the length
	   is larger than the MTU of outgoing device, there will be still
	   MTU problem. */

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(PF_INET6, skb, rt);

	LeaveFunction(10);
	return NF_STOLEN;

tx_error_icmp:
	dst_link_failure(skb);
tx_error:
	LeaveFunction(10);
	kfree_skb(skb);
	return NF_STOLEN;
tx_error_put:
	dst_release(&rt->u.dst);
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
	struct rtable *rt;			/* Route to the other host */
	struct net_device *tdev;		/* Device to other host */
	struct iphdr  *old_iph = ip_hdr(skb);
	u8     tos = old_iph->tos;
	__be16 df = old_iph->frag_off;
	sk_buff_data_t old_transport_header = skb->transport_header;
	struct iphdr  *iph;			/* Our new IP header */
	unsigned int max_headroom;		/* The extra header space needed */
	int    mtu;

	EnterFunction(10);

	if (skb->protocol != htons(ETH_P_IP)) {
		IP_VS_DBG_RL("%s(): protocol error, "
			     "ETH_P_IP: %d, skb protocol: %d\n",
			     __func__, htons(ETH_P_IP), skb->protocol);
		goto tx_error;
	}

	if (!(rt = __ip_vs_get_out_rt(cp, RT_TOS(tos))))
		goto tx_error_icmp;

	tdev = rt->u.dst.dev;

	mtu = dst_mtu(&rt->u.dst) - sizeof(struct iphdr);
	if (mtu < 68) {
		ip_rt_put(rt);
		IP_VS_DBG_RL("%s(): mtu less than 68\n", __func__);
		goto tx_error;
	}
	if (skb_dst(skb))
		skb_dst(skb)->ops->update_pmtu(skb_dst(skb), mtu);

	df |= (old_iph->frag_off & htons(IP_DF));

	if ((old_iph->frag_off & htons(IP_DF))
	    && mtu < ntohs(old_iph->tot_len)) {
		icmp_send(skb, ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED, htonl(mtu));
		ip_rt_put(rt);
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error;
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
		kfree_skb(skb);
		skb = new_skb;
		old_iph = ip_hdr(skb);
	}

	skb->transport_header = old_transport_header;

	/* fix old IP header checksum */
	ip_send_check(old_iph);

	skb_push(skb, sizeof(struct iphdr));
	skb_reset_network_header(skb);
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->u.dst);

	/*
	 *	Push down and install the IPIP header.
	 */
	iph			=	ip_hdr(skb);
	iph->version		=	4;
	iph->ihl		=	sizeof(struct iphdr)>>2;
	iph->frag_off		=	df;
	iph->protocol		=	IPPROTO_IPIP;
	iph->tos		=	tos;
	iph->daddr		=	rt->rt_dst;
	iph->saddr		=	rt->rt_src;
	iph->ttl		=	old_iph->ttl;
	ip_select_ident(iph, &rt->u.dst, NULL);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	ip_local_out(skb);

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
ip_vs_tunnel_xmit_v6(struct sk_buff *skb, struct ip_vs_conn *cp,
		     struct ip_vs_protocol *pp)
{
	struct rt6_info *rt;		/* Route to the other host */
	struct net_device *tdev;	/* Device to other host */
	struct ipv6hdr  *old_iph = ipv6_hdr(skb);
	sk_buff_data_t old_transport_header = skb->transport_header;
	struct ipv6hdr  *iph;		/* Our new IP header */
	unsigned int max_headroom;	/* The extra header space needed */
	int    mtu;

	EnterFunction(10);

	if (skb->protocol != htons(ETH_P_IPV6)) {
		IP_VS_DBG_RL("%s(): protocol error, "
			     "ETH_P_IPV6: %d, skb protocol: %d\n",
			     __func__, htons(ETH_P_IPV6), skb->protocol);
		goto tx_error;
	}

	rt = __ip_vs_get_out_rt_v6(cp);
	if (!rt)
		goto tx_error_icmp;

	tdev = rt->u.dst.dev;

	mtu = dst_mtu(&rt->u.dst) - sizeof(struct ipv6hdr);
	/* TODO IPv6: do we need this check in IPv6? */
	if (mtu < 1280) {
		dst_release(&rt->u.dst);
		IP_VS_DBG_RL("%s(): mtu less than 1280\n", __func__);
		goto tx_error;
	}
	if (skb_dst(skb))
		skb_dst(skb)->ops->update_pmtu(skb_dst(skb), mtu);

	if (mtu < ntohs(old_iph->payload_len) + sizeof(struct ipv6hdr)) {
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		dst_release(&rt->u.dst);
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error;
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
			dst_release(&rt->u.dst);
			kfree_skb(skb);
			IP_VS_ERR_RL("%s(): no memory\n", __func__);
			return NF_STOLEN;
		}
		kfree_skb(skb);
		skb = new_skb;
		old_iph = ipv6_hdr(skb);
	}

	skb->transport_header = old_transport_header;

	skb_push(skb, sizeof(struct ipv6hdr));
	skb_reset_network_header(skb);
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->u.dst);

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
	iph->daddr		=	rt->rt6i_dst.addr;
	iph->saddr		=	cp->vaddr.in6; /* rt->rt6i_src.addr; */
	iph->hop_limit		=	old_iph->hop_limit;

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	ip6_local_out(skb);

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

	if (!(rt = __ip_vs_get_out_rt(cp, RT_TOS(iph->tos))))
		goto tx_error_icmp;

	/* MTU checking */
	mtu = dst_mtu(&rt->u.dst);
	if ((iph->frag_off & htons(IP_DF)) && skb->len > mtu) {
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
	skb_dst_set(skb, &rt->u.dst);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(PF_INET, skb, rt);

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

	rt = __ip_vs_get_out_rt_v6(cp);
	if (!rt)
		goto tx_error_icmp;

	/* MTU checking */
	mtu = dst_mtu(&rt->u.dst);
	if (skb->len > mtu) {
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		dst_release(&rt->u.dst);
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error;
	}

	/*
	 * Call ip_send_check because we are not sure it is called
	 * after ip_defrag. Is copy-on-write needed?
	 */
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(skb == NULL)) {
		dst_release(&rt->u.dst);
		return NF_STOLEN;
	}

	/* drop old route */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->u.dst);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(PF_INET6, skb, rt);

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
		struct ip_vs_protocol *pp, int offset)
{
	struct rtable	*rt;	/* Route to the other host */
	int mtu;
	int rc;

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

	if (!(rt = __ip_vs_get_out_rt(cp, RT_TOS(ip_hdr(skb)->tos))))
		goto tx_error_icmp;

	/* MTU checking */
	mtu = dst_mtu(&rt->u.dst);
	if ((skb->len > mtu) && (ip_hdr(skb)->frag_off & htons(IP_DF))) {
		ip_rt_put(rt);
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error;
	}

	/* copy-on-write the packet before mangling it */
	if (!skb_make_writable(skb, offset))
		goto tx_error_put;

	if (skb_cow(skb, rt->u.dst.dev->hard_header_len))
		goto tx_error_put;

	/* drop the old route when skb is not shared */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->u.dst);

	ip_vs_nat_icmp(skb, pp, cp, 0);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(PF_INET, skb, rt);

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
		struct ip_vs_protocol *pp, int offset)
{
	struct rt6_info	*rt;	/* Route to the other host */
	int mtu;
	int rc;

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

	rt = __ip_vs_get_out_rt_v6(cp);
	if (!rt)
		goto tx_error_icmp;

	/* MTU checking */
	mtu = dst_mtu(&rt->u.dst);
	if (skb->len > mtu) {
		dst_release(&rt->u.dst);
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		IP_VS_DBG_RL("%s(): frag needed\n", __func__);
		goto tx_error;
	}

	/* copy-on-write the packet before mangling it */
	if (!skb_make_writable(skb, offset))
		goto tx_error_put;

	if (skb_cow(skb, rt->u.dst.dev->hard_header_len))
		goto tx_error_put;

	/* drop the old route when skb is not shared */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->u.dst);

	ip_vs_nat_icmp_v6(skb, pp, cp, 0);

	/* Another hack: avoid icmp_send in ip_fragment */
	skb->local_df = 1;

	IP_VS_XMIT(PF_INET6, skb, rt);

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
	dst_release(&rt->u.dst);
	goto tx_error;
}
#endif
