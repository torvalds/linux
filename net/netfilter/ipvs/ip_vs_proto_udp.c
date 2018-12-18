/*
 * ip_vs_proto_udp.c:	UDP load balancing support for IPVS
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *              Julian Anastasov <ja@ssi.bg>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:     Hans Schillstrom <hans.schillstrom@ericsson.com>
 *              Network name space (netns) aware.
 *
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/in.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/udp.h>

#include <net/ip_vs.h>
#include <net/ip.h>
#include <net/ip6_checksum.h>

static int
udp_conn_schedule(struct netns_ipvs *ipvs, int af, struct sk_buff *skb,
		  struct ip_vs_proto_data *pd,
		  int *verdict, struct ip_vs_conn **cpp,
		  struct ip_vs_iphdr *iph)
{
	struct ip_vs_service *svc;
	struct udphdr _udph, *uh;
	__be16 _ports[2], *ports = NULL;

	if (likely(!ip_vs_iph_icmp(iph))) {
		/* IPv6 fragments, only first fragment will hit this */
		uh = skb_header_pointer(skb, iph->len, sizeof(_udph), &_udph);
		if (uh)
			ports = &uh->source;
	} else {
		ports = skb_header_pointer(
			skb, iph->len, sizeof(_ports), &_ports);
	}

	if (!ports) {
		*verdict = NF_DROP;
		return 0;
	}

	if (likely(!ip_vs_iph_inverse(iph)))
		svc = ip_vs_service_find(ipvs, af, skb->mark, iph->protocol,
					 &iph->daddr, ports[1]);
	else
		svc = ip_vs_service_find(ipvs, af, skb->mark, iph->protocol,
					 &iph->saddr, ports[0]);

	if (svc) {
		int ignored;

		if (ip_vs_todrop(ipvs)) {
			/*
			 * It seems that we are very loaded.
			 * We have to drop this packet :(
			 */
			*verdict = NF_DROP;
			return 0;
		}

		/*
		 * Let the virtual server select a real server for the
		 * incoming connection, and create a connection entry.
		 */
		*cpp = ip_vs_schedule(svc, skb, pd, &ignored, iph);
		if (!*cpp && ignored <= 0) {
			if (!ignored)
				*verdict = ip_vs_leave(svc, skb, pd, iph);
			else
				*verdict = NF_DROP;
			return 0;
		}
	}
	/* NF_ACCEPT */
	return 1;
}


static inline void
udp_fast_csum_update(int af, struct udphdr *uhdr,
		     const union nf_inet_addr *oldip,
		     const union nf_inet_addr *newip,
		     __be16 oldport, __be16 newport)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		uhdr->check =
			csum_fold(ip_vs_check_diff16(oldip->ip6, newip->ip6,
					 ip_vs_check_diff2(oldport, newport,
						~csum_unfold(uhdr->check))));
	else
#endif
		uhdr->check =
			csum_fold(ip_vs_check_diff4(oldip->ip, newip->ip,
					 ip_vs_check_diff2(oldport, newport,
						~csum_unfold(uhdr->check))));
	if (!uhdr->check)
		uhdr->check = CSUM_MANGLED_0;
}

static inline void
udp_partial_csum_update(int af, struct udphdr *uhdr,
		     const union nf_inet_addr *oldip,
		     const union nf_inet_addr *newip,
		     __be16 oldlen, __be16 newlen)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		uhdr->check =
			~csum_fold(ip_vs_check_diff16(oldip->ip6, newip->ip6,
					 ip_vs_check_diff2(oldlen, newlen,
						csum_unfold(uhdr->check))));
	else
#endif
	uhdr->check =
		~csum_fold(ip_vs_check_diff4(oldip->ip, newip->ip,
				ip_vs_check_diff2(oldlen, newlen,
						csum_unfold(uhdr->check))));
}


static int
udp_snat_handler(struct sk_buff *skb, struct ip_vs_protocol *pp,
		 struct ip_vs_conn *cp, struct ip_vs_iphdr *iph)
{
	struct udphdr *udph;
	unsigned int udphoff = iph->len;
	int oldlen;
	int payload_csum = 0;

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6 && iph->fragoffs)
		return 1;
#endif
	oldlen = skb->len - udphoff;

	/* csum_check requires unshared skb */
	if (!skb_make_writable(skb, udphoff+sizeof(*udph)))
		return 0;

	if (unlikely(cp->app != NULL)) {
		int ret;

		/* Some checks before mangling */
		if (pp->csum_check && !pp->csum_check(cp->af, skb, pp))
			return 0;

		/*
		 *	Call application helper if needed
		 */
		if (!(ret = ip_vs_app_pkt_out(cp, skb, iph)))
			return 0;
		/* ret=2: csum update is needed after payload mangling */
		if (ret == 1)
			oldlen = skb->len - udphoff;
		else
			payload_csum = 1;
	}

	udph = (void *)skb_network_header(skb) + udphoff;
	udph->source = cp->vport;

	/*
	 *	Adjust UDP checksums
	 */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		udp_partial_csum_update(cp->af, udph, &cp->daddr, &cp->vaddr,
					htons(oldlen),
					htons(skb->len - udphoff));
	} else if (!payload_csum && (udph->check != 0)) {
		/* Only port and addr are changed, do fast csum update */
		udp_fast_csum_update(cp->af, udph, &cp->daddr, &cp->vaddr,
				     cp->dport, cp->vport);
		if (skb->ip_summed == CHECKSUM_COMPLETE)
			skb->ip_summed = (cp->app && pp->csum_check) ?
					 CHECKSUM_UNNECESSARY : CHECKSUM_NONE;
	} else {
		/* full checksum calculation */
		udph->check = 0;
		skb->csum = skb_checksum(skb, udphoff, skb->len - udphoff, 0);
#ifdef CONFIG_IP_VS_IPV6
		if (cp->af == AF_INET6)
			udph->check = csum_ipv6_magic(&cp->vaddr.in6,
						      &cp->caddr.in6,
						      skb->len - udphoff,
						      cp->protocol, skb->csum);
		else
#endif
			udph->check = csum_tcpudp_magic(cp->vaddr.ip,
							cp->caddr.ip,
							skb->len - udphoff,
							cp->protocol,
							skb->csum);
		if (udph->check == 0)
			udph->check = CSUM_MANGLED_0;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		IP_VS_DBG(11, "O-pkt: %s O-csum=%d (+%zd)\n",
			  pp->name, udph->check,
			  (char*)&(udph->check) - (char*)udph);
	}
	return 1;
}


static int
udp_dnat_handler(struct sk_buff *skb, struct ip_vs_protocol *pp,
		 struct ip_vs_conn *cp, struct ip_vs_iphdr *iph)
{
	struct udphdr *udph;
	unsigned int udphoff = iph->len;
	int oldlen;
	int payload_csum = 0;

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6 && iph->fragoffs)
		return 1;
#endif
	oldlen = skb->len - udphoff;

	/* csum_check requires unshared skb */
	if (!skb_make_writable(skb, udphoff+sizeof(*udph)))
		return 0;

	if (unlikely(cp->app != NULL)) {
		int ret;

		/* Some checks before mangling */
		if (pp->csum_check && !pp->csum_check(cp->af, skb, pp))
			return 0;

		/*
		 *	Attempt ip_vs_app call.
		 *	It will fix ip_vs_conn
		 */
		if (!(ret = ip_vs_app_pkt_in(cp, skb, iph)))
			return 0;
		/* ret=2: csum update is needed after payload mangling */
		if (ret == 1)
			oldlen = skb->len - udphoff;
		else
			payload_csum = 1;
	}

	udph = (void *)skb_network_header(skb) + udphoff;
	udph->dest = cp->dport;

	/*
	 *	Adjust UDP checksums
	 */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		udp_partial_csum_update(cp->af, udph, &cp->vaddr, &cp->daddr,
					htons(oldlen),
					htons(skb->len - udphoff));
	} else if (!payload_csum && (udph->check != 0)) {
		/* Only port and addr are changed, do fast csum update */
		udp_fast_csum_update(cp->af, udph, &cp->vaddr, &cp->daddr,
				     cp->vport, cp->dport);
		if (skb->ip_summed == CHECKSUM_COMPLETE)
			skb->ip_summed = (cp->app && pp->csum_check) ?
					 CHECKSUM_UNNECESSARY : CHECKSUM_NONE;
	} else {
		/* full checksum calculation */
		udph->check = 0;
		skb->csum = skb_checksum(skb, udphoff, skb->len - udphoff, 0);
#ifdef CONFIG_IP_VS_IPV6
		if (cp->af == AF_INET6)
			udph->check = csum_ipv6_magic(&cp->caddr.in6,
						      &cp->daddr.in6,
						      skb->len - udphoff,
						      cp->protocol, skb->csum);
		else
#endif
			udph->check = csum_tcpudp_magic(cp->caddr.ip,
							cp->daddr.ip,
							skb->len - udphoff,
							cp->protocol,
							skb->csum);
		if (udph->check == 0)
			udph->check = CSUM_MANGLED_0;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
	return 1;
}


static int
udp_csum_check(int af, struct sk_buff *skb, struct ip_vs_protocol *pp)
{
	struct udphdr _udph, *uh;
	unsigned int udphoff;

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		udphoff = sizeof(struct ipv6hdr);
	else
#endif
		udphoff = ip_hdrlen(skb);

	uh = skb_header_pointer(skb, udphoff, sizeof(_udph), &_udph);
	if (uh == NULL)
		return 0;

	if (uh->check != 0) {
		switch (skb->ip_summed) {
		case CHECKSUM_NONE:
			skb->csum = skb_checksum(skb, udphoff,
						 skb->len - udphoff, 0);
			/* fall through */
		case CHECKSUM_COMPLETE:
#ifdef CONFIG_IP_VS_IPV6
			if (af == AF_INET6) {
				if (csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						    &ipv6_hdr(skb)->daddr,
						    skb->len - udphoff,
						    ipv6_hdr(skb)->nexthdr,
						    skb->csum)) {
					IP_VS_DBG_RL_PKT(0, af, pp, skb, 0,
							 "Failed checksum for");
					return 0;
				}
			} else
#endif
				if (csum_tcpudp_magic(ip_hdr(skb)->saddr,
						      ip_hdr(skb)->daddr,
						      skb->len - udphoff,
						      ip_hdr(skb)->protocol,
						      skb->csum)) {
					IP_VS_DBG_RL_PKT(0, af, pp, skb, 0,
							 "Failed checksum for");
					return 0;
				}
			break;
		default:
			/* No need to checksum. */
			break;
		}
	}
	return 1;
}

static inline __u16 udp_app_hashkey(__be16 port)
{
	return (((__force u16)port >> UDP_APP_TAB_BITS) ^ (__force u16)port)
		& UDP_APP_TAB_MASK;
}


static int udp_register_app(struct netns_ipvs *ipvs, struct ip_vs_app *inc)
{
	struct ip_vs_app *i;
	__u16 hash;
	__be16 port = inc->port;
	int ret = 0;
	struct ip_vs_proto_data *pd = ip_vs_proto_data_get(ipvs, IPPROTO_UDP);

	hash = udp_app_hashkey(port);

	list_for_each_entry(i, &ipvs->udp_apps[hash], p_list) {
		if (i->port == port) {
			ret = -EEXIST;
			goto out;
		}
	}
	list_add_rcu(&inc->p_list, &ipvs->udp_apps[hash]);
	atomic_inc(&pd->appcnt);

  out:
	return ret;
}


static void
udp_unregister_app(struct netns_ipvs *ipvs, struct ip_vs_app *inc)
{
	struct ip_vs_proto_data *pd = ip_vs_proto_data_get(ipvs, IPPROTO_UDP);

	atomic_dec(&pd->appcnt);
	list_del_rcu(&inc->p_list);
}


static int udp_app_conn_bind(struct ip_vs_conn *cp)
{
	struct netns_ipvs *ipvs = cp->ipvs;
	int hash;
	struct ip_vs_app *inc;
	int result = 0;

	/* Default binding: bind app only for NAT */
	if (IP_VS_FWD_METHOD(cp) != IP_VS_CONN_F_MASQ)
		return 0;

	/* Lookup application incarnations and bind the right one */
	hash = udp_app_hashkey(cp->vport);

	list_for_each_entry_rcu(inc, &ipvs->udp_apps[hash], p_list) {
		if (inc->port == cp->vport) {
			if (unlikely(!ip_vs_app_inc_get(inc)))
				break;

			IP_VS_DBG_BUF(9, "%s(): Binding conn %s:%u->"
				      "%s:%u to app %s on port %u\n",
				      __func__,
				      IP_VS_DBG_ADDR(cp->af, &cp->caddr),
				      ntohs(cp->cport),
				      IP_VS_DBG_ADDR(cp->af, &cp->vaddr),
				      ntohs(cp->vport),
				      inc->name, ntohs(inc->port));

			cp->app = inc;
			if (inc->init_conn)
				result = inc->init_conn(inc, cp);
			break;
		}
	}

	return result;
}


static const int udp_timeouts[IP_VS_UDP_S_LAST+1] = {
	[IP_VS_UDP_S_NORMAL]		=	5*60*HZ,
	[IP_VS_UDP_S_LAST]		=	2*HZ,
};

static const char *const udp_state_name_table[IP_VS_UDP_S_LAST+1] = {
	[IP_VS_UDP_S_NORMAL]		=	"UDP",
	[IP_VS_UDP_S_LAST]		=	"BUG!",
};

static const char * udp_state_name(int state)
{
	if (state >= IP_VS_UDP_S_LAST)
		return "ERR!";
	return udp_state_name_table[state] ? udp_state_name_table[state] : "?";
}

static void
udp_state_transition(struct ip_vs_conn *cp, int direction,
		     const struct sk_buff *skb,
		     struct ip_vs_proto_data *pd)
{
	if (unlikely(!pd)) {
		pr_err("UDP no ns data\n");
		return;
	}

	cp->timeout = pd->timeout_table[IP_VS_UDP_S_NORMAL];
	if (direction == IP_VS_DIR_OUTPUT)
		ip_vs_control_assure_ct(cp);
}

static int __udp_init(struct netns_ipvs *ipvs, struct ip_vs_proto_data *pd)
{
	ip_vs_init_hash_table(ipvs->udp_apps, UDP_APP_TAB_SIZE);
	pd->timeout_table = ip_vs_create_timeout_table((int *)udp_timeouts,
							sizeof(udp_timeouts));
	if (!pd->timeout_table)
		return -ENOMEM;
	return 0;
}

static void __udp_exit(struct netns_ipvs *ipvs, struct ip_vs_proto_data *pd)
{
	kfree(pd->timeout_table);
}


struct ip_vs_protocol ip_vs_protocol_udp = {
	.name =			"UDP",
	.protocol =		IPPROTO_UDP,
	.num_states =		IP_VS_UDP_S_LAST,
	.dont_defrag =		0,
	.init =			NULL,
	.exit =			NULL,
	.init_netns =		__udp_init,
	.exit_netns =		__udp_exit,
	.conn_schedule =	udp_conn_schedule,
	.conn_in_get =		ip_vs_conn_in_get_proto,
	.conn_out_get =		ip_vs_conn_out_get_proto,
	.snat_handler =		udp_snat_handler,
	.dnat_handler =		udp_dnat_handler,
	.csum_check =		udp_csum_check,
	.state_transition =	udp_state_transition,
	.state_name =		udp_state_name,
	.register_app =		udp_register_app,
	.unregister_app =	udp_unregister_app,
	.app_conn_bind =	udp_app_conn_bind,
	.debug_packet =		ip_vs_tcpudp_debug_packet,
	.timeout_change =	NULL,
};
