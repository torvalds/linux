/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a module
 *              over the Netfilter framework. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *              Peter Kese <peter.kese@ijs.si>
 *              Julian Anastasov <ja@ssi.bg>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * The IPVS code for kernel 2.2 was done by Wensong Zhang and Peter Kese,
 * with changes/fixes from Julian Anastasov, Lars Marowsky-Bree, Horms
 * and others.
 *
 * Changes:
 *	Paul `Rusty' Russell		properly handle non-linear skbs
 *	Harald Welte			don't use nfcache
 *
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sctp.h>
#include <linux/icmp.h>
#include <linux/slab.h>

#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>                   /* for icmp_send */
#include <net/route.h>
#include <net/ip6_checksum.h>
#include <net/netns/generic.h>		/* net_generic() */

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#ifdef CONFIG_IP_VS_IPV6
#include <net/ipv6.h>
#include <linux/netfilter_ipv6.h>
#include <net/ip6_route.h>
#endif

#include <net/ip_vs.h>


EXPORT_SYMBOL(register_ip_vs_scheduler);
EXPORT_SYMBOL(unregister_ip_vs_scheduler);
EXPORT_SYMBOL(ip_vs_proto_name);
EXPORT_SYMBOL(ip_vs_conn_new);
EXPORT_SYMBOL(ip_vs_conn_in_get);
EXPORT_SYMBOL(ip_vs_conn_out_get);
#ifdef CONFIG_IP_VS_PROTO_TCP
EXPORT_SYMBOL(ip_vs_tcp_conn_listen);
#endif
EXPORT_SYMBOL(ip_vs_conn_put);
#ifdef CONFIG_IP_VS_DEBUG
EXPORT_SYMBOL(ip_vs_get_debug_level);
#endif

int ip_vs_net_id __read_mostly;
#ifdef IP_VS_GENERIC_NETNS
EXPORT_SYMBOL(ip_vs_net_id);
#endif
/* netns cnt used for uniqueness */
static atomic_t ipvs_netns_cnt = ATOMIC_INIT(0);

/* ID used in ICMP lookups */
#define icmp_id(icmph)          (((icmph)->un).echo.id)
#define icmpv6_id(icmph)        (icmph->icmp6_dataun.u_echo.identifier)

const char *ip_vs_proto_name(unsigned proto)
{
	static char buf[20];

	switch (proto) {
	case IPPROTO_IP:
		return "IP";
	case IPPROTO_UDP:
		return "UDP";
	case IPPROTO_TCP:
		return "TCP";
	case IPPROTO_SCTP:
		return "SCTP";
	case IPPROTO_ICMP:
		return "ICMP";
#ifdef CONFIG_IP_VS_IPV6
	case IPPROTO_ICMPV6:
		return "ICMPv6";
#endif
	default:
		sprintf(buf, "IP_%d", proto);
		return buf;
	}
}

void ip_vs_init_hash_table(struct list_head *table, int rows)
{
	while (--rows >= 0)
		INIT_LIST_HEAD(&table[rows]);
}

static inline void
ip_vs_in_stats(struct ip_vs_conn *cp, struct sk_buff *skb)
{
	struct ip_vs_dest *dest = cp->dest;
	struct netns_ipvs *ipvs = net_ipvs(skb_net(skb));

	if (dest && (dest->flags & IP_VS_DEST_F_AVAILABLE)) {
		struct ip_vs_cpu_stats *s;

		s = this_cpu_ptr(dest->stats.cpustats);
		s->ustats.inpkts++;
		u64_stats_update_begin(&s->syncp);
		s->ustats.inbytes += skb->len;
		u64_stats_update_end(&s->syncp);

		s = this_cpu_ptr(dest->svc->stats.cpustats);
		s->ustats.inpkts++;
		u64_stats_update_begin(&s->syncp);
		s->ustats.inbytes += skb->len;
		u64_stats_update_end(&s->syncp);

		s = this_cpu_ptr(ipvs->tot_stats.cpustats);
		s->ustats.inpkts++;
		u64_stats_update_begin(&s->syncp);
		s->ustats.inbytes += skb->len;
		u64_stats_update_end(&s->syncp);
	}
}


static inline void
ip_vs_out_stats(struct ip_vs_conn *cp, struct sk_buff *skb)
{
	struct ip_vs_dest *dest = cp->dest;
	struct netns_ipvs *ipvs = net_ipvs(skb_net(skb));

	if (dest && (dest->flags & IP_VS_DEST_F_AVAILABLE)) {
		struct ip_vs_cpu_stats *s;

		s = this_cpu_ptr(dest->stats.cpustats);
		s->ustats.outpkts++;
		u64_stats_update_begin(&s->syncp);
		s->ustats.outbytes += skb->len;
		u64_stats_update_end(&s->syncp);

		s = this_cpu_ptr(dest->svc->stats.cpustats);
		s->ustats.outpkts++;
		u64_stats_update_begin(&s->syncp);
		s->ustats.outbytes += skb->len;
		u64_stats_update_end(&s->syncp);

		s = this_cpu_ptr(ipvs->tot_stats.cpustats);
		s->ustats.outpkts++;
		u64_stats_update_begin(&s->syncp);
		s->ustats.outbytes += skb->len;
		u64_stats_update_end(&s->syncp);
	}
}


static inline void
ip_vs_conn_stats(struct ip_vs_conn *cp, struct ip_vs_service *svc)
{
	struct netns_ipvs *ipvs = net_ipvs(svc->net);
	struct ip_vs_cpu_stats *s;

	s = this_cpu_ptr(cp->dest->stats.cpustats);
	s->ustats.conns++;

	s = this_cpu_ptr(svc->stats.cpustats);
	s->ustats.conns++;

	s = this_cpu_ptr(ipvs->tot_stats.cpustats);
	s->ustats.conns++;
}


static inline void
ip_vs_set_state(struct ip_vs_conn *cp, int direction,
		const struct sk_buff *skb,
		struct ip_vs_proto_data *pd)
{
	if (likely(pd->pp->state_transition))
		pd->pp->state_transition(cp, direction, skb, pd);
}

static inline int
ip_vs_conn_fill_param_persist(const struct ip_vs_service *svc,
			      struct sk_buff *skb, int protocol,
			      const union nf_inet_addr *caddr, __be16 cport,
			      const union nf_inet_addr *vaddr, __be16 vport,
			      struct ip_vs_conn_param *p)
{
	ip_vs_conn_fill_param(svc->net, svc->af, protocol, caddr, cport, vaddr,
			      vport, p);
	p->pe = svc->pe;
	if (p->pe && p->pe->fill_param)
		return p->pe->fill_param(p, skb);

	return 0;
}

/*
 *  IPVS persistent scheduling function
 *  It creates a connection entry according to its template if exists,
 *  or selects a server and creates a connection entry plus a template.
 *  Locking: we are svc user (svc->refcnt), so we hold all dests too
 *  Protocols supported: TCP, UDP
 */
static struct ip_vs_conn *
ip_vs_sched_persist(struct ip_vs_service *svc,
		    struct sk_buff *skb,
		    __be16 src_port, __be16 dst_port, int *ignored)
{
	struct ip_vs_conn *cp = NULL;
	struct ip_vs_iphdr iph;
	struct ip_vs_dest *dest;
	struct ip_vs_conn *ct;
	__be16 dport = 0;		/* destination port to forward */
	unsigned int flags;
	struct ip_vs_conn_param param;
	const union nf_inet_addr fwmark = { .ip = htonl(svc->fwmark) };
	union nf_inet_addr snet;	/* source network of the client,
					   after masking */

	ip_vs_fill_iphdr(svc->af, skb_network_header(skb), &iph);

	/* Mask saddr with the netmask to adjust template granularity */
#ifdef CONFIG_IP_VS_IPV6
	if (svc->af == AF_INET6)
		ipv6_addr_prefix(&snet.in6, &iph.saddr.in6, svc->netmask);
	else
#endif
		snet.ip = iph.saddr.ip & svc->netmask;

	IP_VS_DBG_BUF(6, "p-schedule: src %s:%u dest %s:%u "
		      "mnet %s\n",
		      IP_VS_DBG_ADDR(svc->af, &iph.saddr), ntohs(src_port),
		      IP_VS_DBG_ADDR(svc->af, &iph.daddr), ntohs(dst_port),
		      IP_VS_DBG_ADDR(svc->af, &snet));

	/*
	 * As far as we know, FTP is a very complicated network protocol, and
	 * it uses control connection and data connections. For active FTP,
	 * FTP server initialize data connection to the client, its source port
	 * is often 20. For passive FTP, FTP server tells the clients the port
	 * that it passively listens to,  and the client issues the data
	 * connection. In the tunneling or direct routing mode, the load
	 * balancer is on the client-to-server half of connection, the port
	 * number is unknown to the load balancer. So, a conn template like
	 * <caddr, 0, vaddr, 0, daddr, 0> is created for persistent FTP
	 * service, and a template like <caddr, 0, vaddr, vport, daddr, dport>
	 * is created for other persistent services.
	 */
	{
		int protocol = iph.protocol;
		const union nf_inet_addr *vaddr = &iph.daddr;
		__be16 vport = 0;

		if (dst_port == svc->port) {
			/* non-FTP template:
			 * <protocol, caddr, 0, vaddr, vport, daddr, dport>
			 * FTP template:
			 * <protocol, caddr, 0, vaddr, 0, daddr, 0>
			 */
			if (svc->port != FTPPORT)
				vport = dst_port;
		} else {
			/* Note: persistent fwmark-based services and
			 * persistent port zero service are handled here.
			 * fwmark template:
			 * <IPPROTO_IP,caddr,0,fwmark,0,daddr,0>
			 * port zero template:
			 * <protocol,caddr,0,vaddr,0,daddr,0>
			 */
			if (svc->fwmark) {
				protocol = IPPROTO_IP;
				vaddr = &fwmark;
			}
		}
		/* return *ignored = -1 so NF_DROP can be used */
		if (ip_vs_conn_fill_param_persist(svc, skb, protocol, &snet, 0,
						  vaddr, vport, &param) < 0) {
			*ignored = -1;
			return NULL;
		}
	}

	/* Check if a template already exists */
	ct = ip_vs_ct_in_get(&param);
	if (!ct || !ip_vs_check_template(ct)) {
		/*
		 * No template found or the dest of the connection
		 * template is not available.
		 * return *ignored=0 i.e. ICMP and NF_DROP
		 */
		dest = svc->scheduler->schedule(svc, skb);
		if (!dest) {
			IP_VS_DBG(1, "p-schedule: no dest found.\n");
			kfree(param.pe_data);
			*ignored = 0;
			return NULL;
		}

		if (dst_port == svc->port && svc->port != FTPPORT)
			dport = dest->port;

		/* Create a template
		 * This adds param.pe_data to the template,
		 * and thus param.pe_data will be destroyed
		 * when the template expires */
		ct = ip_vs_conn_new(&param, &dest->addr, dport,
				    IP_VS_CONN_F_TEMPLATE, dest, skb->mark);
		if (ct == NULL) {
			kfree(param.pe_data);
			*ignored = -1;
			return NULL;
		}

		ct->timeout = svc->timeout;
	} else {
		/* set destination with the found template */
		dest = ct->dest;
		kfree(param.pe_data);
	}

	dport = dst_port;
	if (dport == svc->port && dest->port)
		dport = dest->port;

	flags = (svc->flags & IP_VS_SVC_F_ONEPACKET
		 && iph.protocol == IPPROTO_UDP)?
		IP_VS_CONN_F_ONE_PACKET : 0;

	/*
	 *    Create a new connection according to the template
	 */
	ip_vs_conn_fill_param(svc->net, svc->af, iph.protocol, &iph.saddr,
			      src_port, &iph.daddr, dst_port, &param);

	cp = ip_vs_conn_new(&param, &dest->addr, dport, flags, dest, skb->mark);
	if (cp == NULL) {
		ip_vs_conn_put(ct);
		*ignored = -1;
		return NULL;
	}

	/*
	 *    Add its control
	 */
	ip_vs_control_add(cp, ct);
	ip_vs_conn_put(ct);

	ip_vs_conn_stats(cp, svc);
	return cp;
}


/*
 *  IPVS main scheduling function
 *  It selects a server according to the virtual service, and
 *  creates a connection entry.
 *  Protocols supported: TCP, UDP
 *
 *  Usage of *ignored
 *
 * 1 :   protocol tried to schedule (eg. on SYN), found svc but the
 *       svc/scheduler decides that this packet should be accepted with
 *       NF_ACCEPT because it must not be scheduled.
 *
 * 0 :   scheduler can not find destination, so try bypass or
 *       return ICMP and then NF_DROP (ip_vs_leave).
 *
 * -1 :  scheduler tried to schedule but fatal error occurred, eg.
 *       ip_vs_conn_new failure (ENOMEM) or ip_vs_sip_fill_param
 *       failure such as missing Call-ID, ENOMEM on skb_linearize
 *       or pe_data. In this case we should return NF_DROP without
 *       any attempts to send ICMP with ip_vs_leave.
 */
struct ip_vs_conn *
ip_vs_schedule(struct ip_vs_service *svc, struct sk_buff *skb,
	       struct ip_vs_proto_data *pd, int *ignored)
{
	struct ip_vs_protocol *pp = pd->pp;
	struct ip_vs_conn *cp = NULL;
	struct ip_vs_iphdr iph;
	struct ip_vs_dest *dest;
	__be16 _ports[2], *pptr;
	unsigned int flags;

	*ignored = 1;
	ip_vs_fill_iphdr(svc->af, skb_network_header(skb), &iph);
	pptr = skb_header_pointer(skb, iph.len, sizeof(_ports), _ports);
	if (pptr == NULL)
		return NULL;

	/*
	 * FTPDATA needs this check when using local real server.
	 * Never schedule Active FTPDATA connections from real server.
	 * For LVS-NAT they must be already created. For other methods
	 * with persistence the connection is created on SYN+ACK.
	 */
	if (pptr[0] == FTPDATA) {
		IP_VS_DBG_PKT(12, svc->af, pp, skb, 0,
			      "Not scheduling FTPDATA");
		return NULL;
	}

	/*
	 *    Do not schedule replies from local real server.
	 */
	if ((!skb->dev || skb->dev->flags & IFF_LOOPBACK) &&
	    (cp = pp->conn_in_get(svc->af, skb, &iph, iph.len, 1))) {
		IP_VS_DBG_PKT(12, svc->af, pp, skb, 0,
			      "Not scheduling reply for existing connection");
		__ip_vs_conn_put(cp);
		return NULL;
	}

	/*
	 *    Persistent service
	 */
	if (svc->flags & IP_VS_SVC_F_PERSISTENT)
		return ip_vs_sched_persist(svc, skb, pptr[0], pptr[1], ignored);

	*ignored = 0;

	/*
	 *    Non-persistent service
	 */
	if (!svc->fwmark && pptr[1] != svc->port) {
		if (!svc->port)
			pr_err("Schedule: port zero only supported "
			       "in persistent services, "
			       "check your ipvs configuration\n");
		return NULL;
	}

	dest = svc->scheduler->schedule(svc, skb);
	if (dest == NULL) {
		IP_VS_DBG(1, "Schedule: no dest found.\n");
		return NULL;
	}

	flags = (svc->flags & IP_VS_SVC_F_ONEPACKET
		 && iph.protocol == IPPROTO_UDP)?
		IP_VS_CONN_F_ONE_PACKET : 0;

	/*
	 *    Create a connection entry.
	 */
	{
		struct ip_vs_conn_param p;

		ip_vs_conn_fill_param(svc->net, svc->af, iph.protocol,
				      &iph.saddr, pptr[0], &iph.daddr, pptr[1],
				      &p);
		cp = ip_vs_conn_new(&p, &dest->addr,
				    dest->port ? dest->port : pptr[1],
				    flags, dest, skb->mark);
		if (!cp) {
			*ignored = -1;
			return NULL;
		}
	}

	IP_VS_DBG_BUF(6, "Schedule fwd:%c c:%s:%u v:%s:%u "
		      "d:%s:%u conn->flags:%X conn->refcnt:%d\n",
		      ip_vs_fwd_tag(cp),
		      IP_VS_DBG_ADDR(svc->af, &cp->caddr), ntohs(cp->cport),
		      IP_VS_DBG_ADDR(svc->af, &cp->vaddr), ntohs(cp->vport),
		      IP_VS_DBG_ADDR(svc->af, &cp->daddr), ntohs(cp->dport),
		      cp->flags, atomic_read(&cp->refcnt));

	ip_vs_conn_stats(cp, svc);
	return cp;
}


/*
 *  Pass or drop the packet.
 *  Called by ip_vs_in, when the virtual service is available but
 *  no destination is available for a new connection.
 */
int ip_vs_leave(struct ip_vs_service *svc, struct sk_buff *skb,
		struct ip_vs_proto_data *pd)
{
	__be16 _ports[2], *pptr;
	struct ip_vs_iphdr iph;
#ifdef CONFIG_SYSCTL
	struct net *net;
	struct netns_ipvs *ipvs;
	int unicast;
#endif

	ip_vs_fill_iphdr(svc->af, skb_network_header(skb), &iph);

	pptr = skb_header_pointer(skb, iph.len, sizeof(_ports), _ports);
	if (pptr == NULL) {
		ip_vs_service_put(svc);
		return NF_DROP;
	}

#ifdef CONFIG_SYSCTL
	net = skb_net(skb);

#ifdef CONFIG_IP_VS_IPV6
	if (svc->af == AF_INET6)
		unicast = ipv6_addr_type(&iph.daddr.in6) & IPV6_ADDR_UNICAST;
	else
#endif
		unicast = (inet_addr_type(net, iph.daddr.ip) == RTN_UNICAST);

	/* if it is fwmark-based service, the cache_bypass sysctl is up
	   and the destination is a non-local unicast, then create
	   a cache_bypass connection entry */
	ipvs = net_ipvs(net);
	if (ipvs->sysctl_cache_bypass && svc->fwmark && unicast) {
		int ret;
		struct ip_vs_conn *cp;
		unsigned int flags = (svc->flags & IP_VS_SVC_F_ONEPACKET &&
				      iph.protocol == IPPROTO_UDP)?
				      IP_VS_CONN_F_ONE_PACKET : 0;
		union nf_inet_addr daddr =  { .all = { 0, 0, 0, 0 } };

		ip_vs_service_put(svc);

		/* create a new connection entry */
		IP_VS_DBG(6, "%s(): create a cache_bypass entry\n", __func__);
		{
			struct ip_vs_conn_param p;
			ip_vs_conn_fill_param(svc->net, svc->af, iph.protocol,
					      &iph.saddr, pptr[0],
					      &iph.daddr, pptr[1], &p);
			cp = ip_vs_conn_new(&p, &daddr, 0,
					    IP_VS_CONN_F_BYPASS | flags,
					    NULL, skb->mark);
			if (!cp)
				return NF_DROP;
		}

		/* statistics */
		ip_vs_in_stats(cp, skb);

		/* set state */
		ip_vs_set_state(cp, IP_VS_DIR_INPUT, skb, pd);

		/* transmit the first SYN packet */
		ret = cp->packet_xmit(skb, cp, pd->pp);
		/* do not touch skb anymore */

		atomic_inc(&cp->in_pkts);
		ip_vs_conn_put(cp);
		return ret;
	}
#endif

	/*
	 * When the virtual ftp service is presented, packets destined
	 * for other services on the VIP may get here (except services
	 * listed in the ipvs table), pass the packets, because it is
	 * not ipvs job to decide to drop the packets.
	 */
	if ((svc->port == FTPPORT) && (pptr[1] != FTPPORT)) {
		ip_vs_service_put(svc);
		return NF_ACCEPT;
	}

	ip_vs_service_put(svc);

	/*
	 * Notify the client that the destination is unreachable, and
	 * release the socket buffer.
	 * Since it is in IP layer, the TCP socket is not actually
	 * created, the TCP RST packet cannot be sent, instead that
	 * ICMP_PORT_UNREACH is sent here no matter it is TCP/UDP. --WZ
	 */
#ifdef CONFIG_IP_VS_IPV6
	if (svc->af == AF_INET6) {
		if (!skb->dev) {
			struct net *net = dev_net(skb_dst(skb)->dev);

			skb->dev = net->loopback_dev;
		}
		icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0);
	} else
#endif
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);

	return NF_DROP;
}

#ifdef CONFIG_SYSCTL

static int sysctl_snat_reroute(struct sk_buff *skb)
{
	struct netns_ipvs *ipvs = net_ipvs(skb_net(skb));
	return ipvs->sysctl_snat_reroute;
}

static int sysctl_nat_icmp_send(struct net *net)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	return ipvs->sysctl_nat_icmp_send;
}

static int sysctl_expire_nodest_conn(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_expire_nodest_conn;
}

#else

static int sysctl_snat_reroute(struct sk_buff *skb) { return 0; }
static int sysctl_nat_icmp_send(struct net *net) { return 0; }
static int sysctl_expire_nodest_conn(struct netns_ipvs *ipvs) { return 0; }

#endif

__sum16 ip_vs_checksum_complete(struct sk_buff *skb, int offset)
{
	return csum_fold(skb_checksum(skb, offset, skb->len - offset, 0));
}

static inline enum ip_defrag_users ip_vs_defrag_user(unsigned int hooknum)
{
	if (NF_INET_LOCAL_IN == hooknum)
		return IP_DEFRAG_VS_IN;
	if (NF_INET_FORWARD == hooknum)
		return IP_DEFRAG_VS_FWD;
	return IP_DEFRAG_VS_OUT;
}

static inline int ip_vs_gather_frags(struct sk_buff *skb, u_int32_t user)
{
	int err = ip_defrag(skb, user);

	if (!err)
		ip_send_check(ip_hdr(skb));

	return err;
}

#ifdef CONFIG_IP_VS_IPV6
static inline int ip_vs_gather_frags_v6(struct sk_buff *skb, u_int32_t user)
{
	/* TODO IPv6: Find out what to do here for IPv6 */
	return 0;
}
#endif

static int ip_vs_route_me_harder(int af, struct sk_buff *skb)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6) {
		if (sysctl_snat_reroute(skb) && ip6_route_me_harder(skb) != 0)
			return 1;
	} else
#endif
		if ((sysctl_snat_reroute(skb) ||
		     skb_rtable(skb)->rt_flags & RTCF_LOCAL) &&
		    ip_route_me_harder(skb, RTN_LOCAL) != 0)
			return 1;

	return 0;
}

/*
 * Packet has been made sufficiently writable in caller
 * - inout: 1=in->out, 0=out->in
 */
void ip_vs_nat_icmp(struct sk_buff *skb, struct ip_vs_protocol *pp,
		    struct ip_vs_conn *cp, int inout)
{
	struct iphdr *iph	 = ip_hdr(skb);
	unsigned int icmp_offset = iph->ihl*4;
	struct icmphdr *icmph	 = (struct icmphdr *)(skb_network_header(skb) +
						      icmp_offset);
	struct iphdr *ciph	 = (struct iphdr *)(icmph + 1);

	if (inout) {
		iph->saddr = cp->vaddr.ip;
		ip_send_check(iph);
		ciph->daddr = cp->vaddr.ip;
		ip_send_check(ciph);
	} else {
		iph->daddr = cp->daddr.ip;
		ip_send_check(iph);
		ciph->saddr = cp->daddr.ip;
		ip_send_check(ciph);
	}

	/* the TCP/UDP/SCTP port */
	if (IPPROTO_TCP == ciph->protocol || IPPROTO_UDP == ciph->protocol ||
	    IPPROTO_SCTP == ciph->protocol) {
		__be16 *ports = (void *)ciph + ciph->ihl*4;

		if (inout)
			ports[1] = cp->vport;
		else
			ports[0] = cp->dport;
	}

	/* And finally the ICMP checksum */
	icmph->checksum = 0;
	icmph->checksum = ip_vs_checksum_complete(skb, icmp_offset);
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (inout)
		IP_VS_DBG_PKT(11, AF_INET, pp, skb, (void *)ciph - (void *)iph,
			"Forwarding altered outgoing ICMP");
	else
		IP_VS_DBG_PKT(11, AF_INET, pp, skb, (void *)ciph - (void *)iph,
			"Forwarding altered incoming ICMP");
}

#ifdef CONFIG_IP_VS_IPV6
void ip_vs_nat_icmp_v6(struct sk_buff *skb, struct ip_vs_protocol *pp,
		    struct ip_vs_conn *cp, int inout)
{
	struct ipv6hdr *iph	 = ipv6_hdr(skb);
	unsigned int icmp_offset = sizeof(struct ipv6hdr);
	struct icmp6hdr *icmph	 = (struct icmp6hdr *)(skb_network_header(skb) +
						      icmp_offset);
	struct ipv6hdr *ciph	 = (struct ipv6hdr *)(icmph + 1);

	if (inout) {
		iph->saddr = cp->vaddr.in6;
		ciph->daddr = cp->vaddr.in6;
	} else {
		iph->daddr = cp->daddr.in6;
		ciph->saddr = cp->daddr.in6;
	}

	/* the TCP/UDP/SCTP port */
	if (IPPROTO_TCP == ciph->nexthdr || IPPROTO_UDP == ciph->nexthdr ||
	    IPPROTO_SCTP == ciph->nexthdr) {
		__be16 *ports = (void *)ciph + sizeof(struct ipv6hdr);

		if (inout)
			ports[1] = cp->vport;
		else
			ports[0] = cp->dport;
	}

	/* And finally the ICMP checksum */
	icmph->icmp6_cksum = ~csum_ipv6_magic(&iph->saddr, &iph->daddr,
					      skb->len - icmp_offset,
					      IPPROTO_ICMPV6, 0);
	skb->csum_start = skb_network_header(skb) - skb->head + icmp_offset;
	skb->csum_offset = offsetof(struct icmp6hdr, icmp6_cksum);
	skb->ip_summed = CHECKSUM_PARTIAL;

	if (inout)
		IP_VS_DBG_PKT(11, AF_INET6, pp, skb,
			      (void *)ciph - (void *)iph,
			      "Forwarding altered outgoing ICMPv6");
	else
		IP_VS_DBG_PKT(11, AF_INET6, pp, skb,
			      (void *)ciph - (void *)iph,
			      "Forwarding altered incoming ICMPv6");
}
#endif

/* Handle relevant response ICMP messages - forward to the right
 * destination host.
 */
static int handle_response_icmp(int af, struct sk_buff *skb,
				union nf_inet_addr *snet,
				__u8 protocol, struct ip_vs_conn *cp,
				struct ip_vs_protocol *pp,
				unsigned int offset, unsigned int ihl)
{
	unsigned int verdict = NF_DROP;

	if (IP_VS_FWD_METHOD(cp) != 0) {
		pr_err("shouldn't reach here, because the box is on the "
		       "half connection in the tun/dr module.\n");
	}

	/* Ensure the checksum is correct */
	if (!skb_csum_unnecessary(skb) && ip_vs_checksum_complete(skb, ihl)) {
		/* Failed checksum! */
		IP_VS_DBG_BUF(1, "Forward ICMP: failed checksum from %s!\n",
			      IP_VS_DBG_ADDR(af, snet));
		goto out;
	}

	if (IPPROTO_TCP == protocol || IPPROTO_UDP == protocol ||
	    IPPROTO_SCTP == protocol)
		offset += 2 * sizeof(__u16);
	if (!skb_make_writable(skb, offset))
		goto out;

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		ip_vs_nat_icmp_v6(skb, pp, cp, 1);
	else
#endif
		ip_vs_nat_icmp(skb, pp, cp, 1);

	if (ip_vs_route_me_harder(af, skb))
		goto out;

	/* do the statistics and put it back */
	ip_vs_out_stats(cp, skb);

	skb->ipvs_property = 1;
	if (!(cp->flags & IP_VS_CONN_F_NFCT))
		ip_vs_notrack(skb);
	else
		ip_vs_update_conntrack(skb, cp, 0);
	verdict = NF_ACCEPT;

out:
	__ip_vs_conn_put(cp);

	return verdict;
}

/*
 *	Handle ICMP messages in the inside-to-outside direction (outgoing).
 *	Find any that might be relevant, check against existing connections.
 *	Currently handles error types - unreachable, quench, ttl exceeded.
 */
static int ip_vs_out_icmp(struct sk_buff *skb, int *related,
			  unsigned int hooknum)
{
	struct iphdr *iph;
	struct icmphdr	_icmph, *ic;
	struct iphdr	_ciph, *cih;	/* The ip header contained within the ICMP */
	struct ip_vs_iphdr ciph;
	struct ip_vs_conn *cp;
	struct ip_vs_protocol *pp;
	unsigned int offset, ihl;
	union nf_inet_addr snet;

	*related = 1;

	/* reassemble IP fragments */
	if (ip_is_fragment(ip_hdr(skb))) {
		if (ip_vs_gather_frags(skb, ip_vs_defrag_user(hooknum)))
			return NF_STOLEN;
	}

	iph = ip_hdr(skb);
	offset = ihl = iph->ihl * 4;
	ic = skb_header_pointer(skb, offset, sizeof(_icmph), &_icmph);
	if (ic == NULL)
		return NF_DROP;

	IP_VS_DBG(12, "Outgoing ICMP (%d,%d) %pI4->%pI4\n",
		  ic->type, ntohs(icmp_id(ic)),
		  &iph->saddr, &iph->daddr);

	/*
	 * Work through seeing if this is for us.
	 * These checks are supposed to be in an order that means easy
	 * things are checked first to speed up processing.... however
	 * this means that some packets will manage to get a long way
	 * down this stack and then be rejected, but that's life.
	 */
	if ((ic->type != ICMP_DEST_UNREACH) &&
	    (ic->type != ICMP_SOURCE_QUENCH) &&
	    (ic->type != ICMP_TIME_EXCEEDED)) {
		*related = 0;
		return NF_ACCEPT;
	}

	/* Now find the contained IP header */
	offset += sizeof(_icmph);
	cih = skb_header_pointer(skb, offset, sizeof(_ciph), &_ciph);
	if (cih == NULL)
		return NF_ACCEPT; /* The packet looks wrong, ignore */

	pp = ip_vs_proto_get(cih->protocol);
	if (!pp)
		return NF_ACCEPT;

	/* Is the embedded protocol header present? */
	if (unlikely(cih->frag_off & htons(IP_OFFSET) &&
		     pp->dont_defrag))
		return NF_ACCEPT;

	IP_VS_DBG_PKT(11, AF_INET, pp, skb, offset,
		      "Checking outgoing ICMP for");

	offset += cih->ihl * 4;

	ip_vs_fill_iphdr(AF_INET, cih, &ciph);
	/* The embedded headers contain source and dest in reverse order */
	cp = pp->conn_out_get(AF_INET, skb, &ciph, offset, 1);
	if (!cp)
		return NF_ACCEPT;

	snet.ip = iph->saddr;
	return handle_response_icmp(AF_INET, skb, &snet, cih->protocol, cp,
				    pp, offset, ihl);
}

#ifdef CONFIG_IP_VS_IPV6
static int ip_vs_out_icmp_v6(struct sk_buff *skb, int *related,
			     unsigned int hooknum)
{
	struct ipv6hdr *iph;
	struct icmp6hdr	_icmph, *ic;
	struct ipv6hdr	_ciph, *cih;	/* The ip header contained
					   within the ICMP */
	struct ip_vs_iphdr ciph;
	struct ip_vs_conn *cp;
	struct ip_vs_protocol *pp;
	unsigned int offset;
	union nf_inet_addr snet;

	*related = 1;

	/* reassemble IP fragments */
	if (ipv6_hdr(skb)->nexthdr == IPPROTO_FRAGMENT) {
		if (ip_vs_gather_frags_v6(skb, ip_vs_defrag_user(hooknum)))
			return NF_STOLEN;
	}

	iph = ipv6_hdr(skb);
	offset = sizeof(struct ipv6hdr);
	ic = skb_header_pointer(skb, offset, sizeof(_icmph), &_icmph);
	if (ic == NULL)
		return NF_DROP;

	IP_VS_DBG(12, "Outgoing ICMPv6 (%d,%d) %pI6->%pI6\n",
		  ic->icmp6_type, ntohs(icmpv6_id(ic)),
		  &iph->saddr, &iph->daddr);

	/*
	 * Work through seeing if this is for us.
	 * These checks are supposed to be in an order that means easy
	 * things are checked first to speed up processing.... however
	 * this means that some packets will manage to get a long way
	 * down this stack and then be rejected, but that's life.
	 */
	if ((ic->icmp6_type != ICMPV6_DEST_UNREACH) &&
	    (ic->icmp6_type != ICMPV6_PKT_TOOBIG) &&
	    (ic->icmp6_type != ICMPV6_TIME_EXCEED)) {
		*related = 0;
		return NF_ACCEPT;
	}

	/* Now find the contained IP header */
	offset += sizeof(_icmph);
	cih = skb_header_pointer(skb, offset, sizeof(_ciph), &_ciph);
	if (cih == NULL)
		return NF_ACCEPT; /* The packet looks wrong, ignore */

	pp = ip_vs_proto_get(cih->nexthdr);
	if (!pp)
		return NF_ACCEPT;

	/* Is the embedded protocol header present? */
	/* TODO: we don't support fragmentation at the moment anyways */
	if (unlikely(cih->nexthdr == IPPROTO_FRAGMENT && pp->dont_defrag))
		return NF_ACCEPT;

	IP_VS_DBG_PKT(11, AF_INET6, pp, skb, offset,
		      "Checking outgoing ICMPv6 for");

	offset += sizeof(struct ipv6hdr);

	ip_vs_fill_iphdr(AF_INET6, cih, &ciph);
	/* The embedded headers contain source and dest in reverse order */
	cp = pp->conn_out_get(AF_INET6, skb, &ciph, offset, 1);
	if (!cp)
		return NF_ACCEPT;

	snet.in6 = iph->saddr;
	return handle_response_icmp(AF_INET6, skb, &snet, cih->nexthdr, cp,
				    pp, offset, sizeof(struct ipv6hdr));
}
#endif

/*
 * Check if sctp chunc is ABORT chunk
 */
static inline int is_sctp_abort(const struct sk_buff *skb, int nh_len)
{
	sctp_chunkhdr_t *sch, schunk;
	sch = skb_header_pointer(skb, nh_len + sizeof(sctp_sctphdr_t),
			sizeof(schunk), &schunk);
	if (sch == NULL)
		return 0;
	if (sch->type == SCTP_CID_ABORT)
		return 1;
	return 0;
}

static inline int is_tcp_reset(const struct sk_buff *skb, int nh_len)
{
	struct tcphdr _tcph, *th;

	th = skb_header_pointer(skb, nh_len, sizeof(_tcph), &_tcph);
	if (th == NULL)
		return 0;
	return th->rst;
}

/* Handle response packets: rewrite addresses and send away...
 */
static unsigned int
handle_response(int af, struct sk_buff *skb, struct ip_vs_proto_data *pd,
		struct ip_vs_conn *cp, int ihl)
{
	struct ip_vs_protocol *pp = pd->pp;

	IP_VS_DBG_PKT(11, af, pp, skb, 0, "Outgoing packet");

	if (!skb_make_writable(skb, ihl))
		goto drop;

	/* mangle the packet */
	if (pp->snat_handler && !pp->snat_handler(skb, pp, cp))
		goto drop;

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		ipv6_hdr(skb)->saddr = cp->vaddr.in6;
	else
#endif
	{
		ip_hdr(skb)->saddr = cp->vaddr.ip;
		ip_send_check(ip_hdr(skb));
	}

	/*
	 * nf_iterate does not expect change in the skb->dst->dev.
	 * It looks like it is not fatal to enable this code for hooks
	 * where our handlers are at the end of the chain list and
	 * when all next handlers use skb->dst->dev and not outdev.
	 * It will definitely route properly the inout NAT traffic
	 * when multiple paths are used.
	 */

	/* For policy routing, packets originating from this
	 * machine itself may be routed differently to packets
	 * passing through.  We want this packet to be routed as
	 * if it came from this machine itself.  So re-compute
	 * the routing information.
	 */
	if (ip_vs_route_me_harder(af, skb))
		goto drop;

	IP_VS_DBG_PKT(10, af, pp, skb, 0, "After SNAT");

	ip_vs_out_stats(cp, skb);
	ip_vs_set_state(cp, IP_VS_DIR_OUTPUT, skb, pd);
	skb->ipvs_property = 1;
	if (!(cp->flags & IP_VS_CONN_F_NFCT))
		ip_vs_notrack(skb);
	else
		ip_vs_update_conntrack(skb, cp, 0);
	ip_vs_conn_put(cp);

	LeaveFunction(11);
	return NF_ACCEPT;

drop:
	ip_vs_conn_put(cp);
	kfree_skb(skb);
	LeaveFunction(11);
	return NF_STOLEN;
}

/*
 *	Check if outgoing packet belongs to the established ip_vs_conn.
 */
static unsigned int
ip_vs_out(unsigned int hooknum, struct sk_buff *skb, int af)
{
	struct net *net = NULL;
	struct ip_vs_iphdr iph;
	struct ip_vs_protocol *pp;
	struct ip_vs_proto_data *pd;
	struct ip_vs_conn *cp;

	EnterFunction(11);

	/* Already marked as IPVS request or reply? */
	if (skb->ipvs_property)
		return NF_ACCEPT;

	/* Bad... Do not break raw sockets */
	if (unlikely(skb->sk != NULL && hooknum == NF_INET_LOCAL_OUT &&
		     af == AF_INET)) {
		struct sock *sk = skb->sk;
		struct inet_sock *inet = inet_sk(skb->sk);

		if (inet && sk->sk_family == PF_INET && inet->nodefrag)
			return NF_ACCEPT;
	}

	if (unlikely(!skb_dst(skb)))
		return NF_ACCEPT;

	net = skb_net(skb);
	if (!net_ipvs(net)->enable)
		return NF_ACCEPT;

	ip_vs_fill_iphdr(af, skb_network_header(skb), &iph);
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6) {
		if (unlikely(iph.protocol == IPPROTO_ICMPV6)) {
			int related;
			int verdict = ip_vs_out_icmp_v6(skb, &related,
							hooknum);

			if (related)
				return verdict;
			ip_vs_fill_iphdr(af, skb_network_header(skb), &iph);
		}
	} else
#endif
		if (unlikely(iph.protocol == IPPROTO_ICMP)) {
			int related;
			int verdict = ip_vs_out_icmp(skb, &related, hooknum);

			if (related)
				return verdict;
			ip_vs_fill_iphdr(af, skb_network_header(skb), &iph);
		}

	pd = ip_vs_proto_data_get(net, iph.protocol);
	if (unlikely(!pd))
		return NF_ACCEPT;
	pp = pd->pp;

	/* reassemble IP fragments */
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6) {
		if (ipv6_hdr(skb)->nexthdr == IPPROTO_FRAGMENT) {
			if (ip_vs_gather_frags_v6(skb,
						  ip_vs_defrag_user(hooknum)))
				return NF_STOLEN;
		}

		ip_vs_fill_iphdr(af, skb_network_header(skb), &iph);
	} else
#endif
		if (unlikely(ip_is_fragment(ip_hdr(skb)) && !pp->dont_defrag)) {
			if (ip_vs_gather_frags(skb,
					       ip_vs_defrag_user(hooknum)))
				return NF_STOLEN;

			ip_vs_fill_iphdr(af, skb_network_header(skb), &iph);
		}

	/*
	 * Check if the packet belongs to an existing entry
	 */
	cp = pp->conn_out_get(af, skb, &iph, iph.len, 0);

	if (likely(cp))
		return handle_response(af, skb, pd, cp, iph.len);
	if (sysctl_nat_icmp_send(net) &&
	    (pp->protocol == IPPROTO_TCP ||
	     pp->protocol == IPPROTO_UDP ||
	     pp->protocol == IPPROTO_SCTP)) {
		__be16 _ports[2], *pptr;

		pptr = skb_header_pointer(skb, iph.len,
					  sizeof(_ports), _ports);
		if (pptr == NULL)
			return NF_ACCEPT;	/* Not for me */
		if (ip_vs_lookup_real_service(net, af, iph.protocol,
					      &iph.saddr,
					      pptr[0])) {
			/*
			 * Notify the real server: there is no
			 * existing entry if it is not RST
			 * packet or not TCP packet.
			 */
			if ((iph.protocol != IPPROTO_TCP &&
			     iph.protocol != IPPROTO_SCTP)
			     || ((iph.protocol == IPPROTO_TCP
				  && !is_tcp_reset(skb, iph.len))
				 || (iph.protocol == IPPROTO_SCTP
					&& !is_sctp_abort(skb,
						iph.len)))) {
#ifdef CONFIG_IP_VS_IPV6
				if (af == AF_INET6) {
					struct net *net =
						dev_net(skb_dst(skb)->dev);

					if (!skb->dev)
						skb->dev = net->loopback_dev;
					icmpv6_send(skb,
						    ICMPV6_DEST_UNREACH,
						    ICMPV6_PORT_UNREACH,
						    0);
				} else
#endif
					icmp_send(skb,
						  ICMP_DEST_UNREACH,
						  ICMP_PORT_UNREACH, 0);
				return NF_DROP;
			}
		}
	}
	IP_VS_DBG_PKT(12, af, pp, skb, 0,
		      "ip_vs_out: packet continues traversal as normal");
	return NF_ACCEPT;
}

/*
 *	It is hooked at the NF_INET_FORWARD and NF_INET_LOCAL_IN chain,
 *	used only for VS/NAT.
 *	Check if packet is reply for established ip_vs_conn.
 */
static unsigned int
ip_vs_reply4(unsigned int hooknum, struct sk_buff *skb,
	     const struct net_device *in, const struct net_device *out,
	     int (*okfn)(struct sk_buff *))
{
	return ip_vs_out(hooknum, skb, AF_INET);
}

/*
 *	It is hooked at the NF_INET_LOCAL_OUT chain, used only for VS/NAT.
 *	Check if packet is reply for established ip_vs_conn.
 */
static unsigned int
ip_vs_local_reply4(unsigned int hooknum, struct sk_buff *skb,
		   const struct net_device *in, const struct net_device *out,
		   int (*okfn)(struct sk_buff *))
{
	unsigned int verdict;

	/* Disable BH in LOCAL_OUT until all places are fixed */
	local_bh_disable();
	verdict = ip_vs_out(hooknum, skb, AF_INET);
	local_bh_enable();
	return verdict;
}

#ifdef CONFIG_IP_VS_IPV6

/*
 *	It is hooked at the NF_INET_FORWARD and NF_INET_LOCAL_IN chain,
 *	used only for VS/NAT.
 *	Check if packet is reply for established ip_vs_conn.
 */
static unsigned int
ip_vs_reply6(unsigned int hooknum, struct sk_buff *skb,
	     const struct net_device *in, const struct net_device *out,
	     int (*okfn)(struct sk_buff *))
{
	return ip_vs_out(hooknum, skb, AF_INET6);
}

/*
 *	It is hooked at the NF_INET_LOCAL_OUT chain, used only for VS/NAT.
 *	Check if packet is reply for established ip_vs_conn.
 */
static unsigned int
ip_vs_local_reply6(unsigned int hooknum, struct sk_buff *skb,
		   const struct net_device *in, const struct net_device *out,
		   int (*okfn)(struct sk_buff *))
{
	unsigned int verdict;

	/* Disable BH in LOCAL_OUT until all places are fixed */
	local_bh_disable();
	verdict = ip_vs_out(hooknum, skb, AF_INET6);
	local_bh_enable();
	return verdict;
}

#endif

/*
 *	Handle ICMP messages in the outside-to-inside direction (incoming).
 *	Find any that might be relevant, check against existing connections,
 *	forward to the right destination host if relevant.
 *	Currently handles error types - unreachable, quench, ttl exceeded.
 */
static int
ip_vs_in_icmp(struct sk_buff *skb, int *related, unsigned int hooknum)
{
	struct net *net = NULL;
	struct iphdr *iph;
	struct icmphdr	_icmph, *ic;
	struct iphdr	_ciph, *cih;	/* The ip header contained within the ICMP */
	struct ip_vs_iphdr ciph;
	struct ip_vs_conn *cp;
	struct ip_vs_protocol *pp;
	struct ip_vs_proto_data *pd;
	unsigned int offset, ihl, verdict;

	*related = 1;

	/* reassemble IP fragments */
	if (ip_is_fragment(ip_hdr(skb))) {
		if (ip_vs_gather_frags(skb, ip_vs_defrag_user(hooknum)))
			return NF_STOLEN;
	}

	iph = ip_hdr(skb);
	offset = ihl = iph->ihl * 4;
	ic = skb_header_pointer(skb, offset, sizeof(_icmph), &_icmph);
	if (ic == NULL)
		return NF_DROP;

	IP_VS_DBG(12, "Incoming ICMP (%d,%d) %pI4->%pI4\n",
		  ic->type, ntohs(icmp_id(ic)),
		  &iph->saddr, &iph->daddr);

	/*
	 * Work through seeing if this is for us.
	 * These checks are supposed to be in an order that means easy
	 * things are checked first to speed up processing.... however
	 * this means that some packets will manage to get a long way
	 * down this stack and then be rejected, but that's life.
	 */
	if ((ic->type != ICMP_DEST_UNREACH) &&
	    (ic->type != ICMP_SOURCE_QUENCH) &&
	    (ic->type != ICMP_TIME_EXCEEDED)) {
		*related = 0;
		return NF_ACCEPT;
	}

	/* Now find the contained IP header */
	offset += sizeof(_icmph);
	cih = skb_header_pointer(skb, offset, sizeof(_ciph), &_ciph);
	if (cih == NULL)
		return NF_ACCEPT; /* The packet looks wrong, ignore */

	net = skb_net(skb);

	pd = ip_vs_proto_data_get(net, cih->protocol);
	if (!pd)
		return NF_ACCEPT;
	pp = pd->pp;

	/* Is the embedded protocol header present? */
	if (unlikely(cih->frag_off & htons(IP_OFFSET) &&
		     pp->dont_defrag))
		return NF_ACCEPT;

	IP_VS_DBG_PKT(11, AF_INET, pp, skb, offset,
		      "Checking incoming ICMP for");

	offset += cih->ihl * 4;

	ip_vs_fill_iphdr(AF_INET, cih, &ciph);
	/* The embedded headers contain source and dest in reverse order */
	cp = pp->conn_in_get(AF_INET, skb, &ciph, offset, 1);
	if (!cp)
		return NF_ACCEPT;

	verdict = NF_DROP;

	/* Ensure the checksum is correct */
	if (!skb_csum_unnecessary(skb) && ip_vs_checksum_complete(skb, ihl)) {
		/* Failed checksum! */
		IP_VS_DBG(1, "Incoming ICMP: failed checksum from %pI4!\n",
			  &iph->saddr);
		goto out;
	}

	/* do the statistics and put it back */
	ip_vs_in_stats(cp, skb);
	if (IPPROTO_TCP == cih->protocol || IPPROTO_UDP == cih->protocol)
		offset += 2 * sizeof(__u16);
	verdict = ip_vs_icmp_xmit(skb, cp, pp, offset, hooknum);

out:
	__ip_vs_conn_put(cp);

	return verdict;
}

#ifdef CONFIG_IP_VS_IPV6
static int
ip_vs_in_icmp_v6(struct sk_buff *skb, int *related, unsigned int hooknum)
{
	struct net *net = NULL;
	struct ipv6hdr *iph;
	struct icmp6hdr	_icmph, *ic;
	struct ipv6hdr	_ciph, *cih;	/* The ip header contained
					   within the ICMP */
	struct ip_vs_iphdr ciph;
	struct ip_vs_conn *cp;
	struct ip_vs_protocol *pp;
	struct ip_vs_proto_data *pd;
	unsigned int offset, verdict;

	*related = 1;

	/* reassemble IP fragments */
	if (ipv6_hdr(skb)->nexthdr == IPPROTO_FRAGMENT) {
		if (ip_vs_gather_frags_v6(skb, ip_vs_defrag_user(hooknum)))
			return NF_STOLEN;
	}

	iph = ipv6_hdr(skb);
	offset = sizeof(struct ipv6hdr);
	ic = skb_header_pointer(skb, offset, sizeof(_icmph), &_icmph);
	if (ic == NULL)
		return NF_DROP;

	IP_VS_DBG(12, "Incoming ICMPv6 (%d,%d) %pI6->%pI6\n",
		  ic->icmp6_type, ntohs(icmpv6_id(ic)),
		  &iph->saddr, &iph->daddr);

	/*
	 * Work through seeing if this is for us.
	 * These checks are supposed to be in an order that means easy
	 * things are checked first to speed up processing.... however
	 * this means that some packets will manage to get a long way
	 * down this stack and then be rejected, but that's life.
	 */
	if ((ic->icmp6_type != ICMPV6_DEST_UNREACH) &&
	    (ic->icmp6_type != ICMPV6_PKT_TOOBIG) &&
	    (ic->icmp6_type != ICMPV6_TIME_EXCEED)) {
		*related = 0;
		return NF_ACCEPT;
	}

	/* Now find the contained IP header */
	offset += sizeof(_icmph);
	cih = skb_header_pointer(skb, offset, sizeof(_ciph), &_ciph);
	if (cih == NULL)
		return NF_ACCEPT; /* The packet looks wrong, ignore */

	net = skb_net(skb);
	pd = ip_vs_proto_data_get(net, cih->nexthdr);
	if (!pd)
		return NF_ACCEPT;
	pp = pd->pp;

	/* Is the embedded protocol header present? */
	/* TODO: we don't support fragmentation at the moment anyways */
	if (unlikely(cih->nexthdr == IPPROTO_FRAGMENT && pp->dont_defrag))
		return NF_ACCEPT;

	IP_VS_DBG_PKT(11, AF_INET6, pp, skb, offset,
		      "Checking incoming ICMPv6 for");

	offset += sizeof(struct ipv6hdr);

	ip_vs_fill_iphdr(AF_INET6, cih, &ciph);
	/* The embedded headers contain source and dest in reverse order */
	cp = pp->conn_in_get(AF_INET6, skb, &ciph, offset, 1);
	if (!cp)
		return NF_ACCEPT;

	/* do the statistics and put it back */
	ip_vs_in_stats(cp, skb);
	if (IPPROTO_TCP == cih->nexthdr || IPPROTO_UDP == cih->nexthdr ||
	    IPPROTO_SCTP == cih->nexthdr)
		offset += 2 * sizeof(__u16);
	verdict = ip_vs_icmp_xmit_v6(skb, cp, pp, offset, hooknum);

	__ip_vs_conn_put(cp);

	return verdict;
}
#endif


/*
 *	Check if it's for virtual services, look it up,
 *	and send it on its way...
 */
static unsigned int
ip_vs_in(unsigned int hooknum, struct sk_buff *skb, int af)
{
	struct net *net;
	struct ip_vs_iphdr iph;
	struct ip_vs_protocol *pp;
	struct ip_vs_proto_data *pd;
	struct ip_vs_conn *cp;
	int ret, pkts;
	struct netns_ipvs *ipvs;

	/* Already marked as IPVS request or reply? */
	if (skb->ipvs_property)
		return NF_ACCEPT;

	/*
	 *	Big tappo:
	 *	- remote client: only PACKET_HOST
	 *	- route: used for struct net when skb->dev is unset
	 */
	if (unlikely((skb->pkt_type != PACKET_HOST &&
		      hooknum != NF_INET_LOCAL_OUT) ||
		     !skb_dst(skb))) {
		ip_vs_fill_iphdr(af, skb_network_header(skb), &iph);
		IP_VS_DBG_BUF(12, "packet type=%d proto=%d daddr=%s"
			      " ignored in hook %u\n",
			      skb->pkt_type, iph.protocol,
			      IP_VS_DBG_ADDR(af, &iph.daddr), hooknum);
		return NF_ACCEPT;
	}
	/* ipvs enabled in this netns ? */
	net = skb_net(skb);
	if (!net_ipvs(net)->enable)
		return NF_ACCEPT;

	ip_vs_fill_iphdr(af, skb_network_header(skb), &iph);

	/* Bad... Do not break raw sockets */
	if (unlikely(skb->sk != NULL && hooknum == NF_INET_LOCAL_OUT &&
		     af == AF_INET)) {
		struct sock *sk = skb->sk;
		struct inet_sock *inet = inet_sk(skb->sk);

		if (inet && sk->sk_family == PF_INET && inet->nodefrag)
			return NF_ACCEPT;
	}

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6) {
		if (unlikely(iph.protocol == IPPROTO_ICMPV6)) {
			int related;
			int verdict = ip_vs_in_icmp_v6(skb, &related, hooknum);

			if (related)
				return verdict;
			ip_vs_fill_iphdr(af, skb_network_header(skb), &iph);
		}
	} else
#endif
		if (unlikely(iph.protocol == IPPROTO_ICMP)) {
			int related;
			int verdict = ip_vs_in_icmp(skb, &related, hooknum);

			if (related)
				return verdict;
			ip_vs_fill_iphdr(af, skb_network_header(skb), &iph);
		}

	/* Protocol supported? */
	pd = ip_vs_proto_data_get(net, iph.protocol);
	if (unlikely(!pd))
		return NF_ACCEPT;
	pp = pd->pp;
	/*
	 * Check if the packet belongs to an existing connection entry
	 */
	cp = pp->conn_in_get(af, skb, &iph, iph.len, 0);

	if (unlikely(!cp)) {
		int v;

		if (!pp->conn_schedule(af, skb, pd, &v, &cp))
			return v;
	}

	if (unlikely(!cp)) {
		/* sorry, all this trouble for a no-hit :) */
		IP_VS_DBG_PKT(12, af, pp, skb, 0,
			      "ip_vs_in: packet continues traversal as normal");
		return NF_ACCEPT;
	}

	IP_VS_DBG_PKT(11, af, pp, skb, 0, "Incoming packet");
	ipvs = net_ipvs(net);
	/* Check the server status */
	if (cp->dest && !(cp->dest->flags & IP_VS_DEST_F_AVAILABLE)) {
		/* the destination server is not available */

		if (sysctl_expire_nodest_conn(ipvs)) {
			/* try to expire the connection immediately */
			ip_vs_conn_expire_now(cp);
		}
		/* don't restart its timer, and silently
		   drop the packet. */
		__ip_vs_conn_put(cp);
		return NF_DROP;
	}

	ip_vs_in_stats(cp, skb);
	ip_vs_set_state(cp, IP_VS_DIR_INPUT, skb, pd);
	if (cp->packet_xmit)
		ret = cp->packet_xmit(skb, cp, pp);
		/* do not touch skb anymore */
	else {
		IP_VS_DBG_RL("warning: packet_xmit is null");
		ret = NF_ACCEPT;
	}

	/* Increase its packet counter and check if it is needed
	 * to be synchronized
	 *
	 * Sync connection if it is about to close to
	 * encorage the standby servers to update the connections timeout
	 *
	 * For ONE_PKT let ip_vs_sync_conn() do the filter work.
	 */

	if (cp->flags & IP_VS_CONN_F_ONE_PACKET)
		pkts = sysctl_sync_threshold(ipvs);
	else
		pkts = atomic_add_return(1, &cp->in_pkts);

	if ((ipvs->sync_state & IP_VS_STATE_MASTER) &&
	    cp->protocol == IPPROTO_SCTP) {
		if ((cp->state == IP_VS_SCTP_S_ESTABLISHED &&
			(pkts % sysctl_sync_period(ipvs)
			 == sysctl_sync_threshold(ipvs))) ||
				(cp->old_state != cp->state &&
				 ((cp->state == IP_VS_SCTP_S_CLOSED) ||
				  (cp->state == IP_VS_SCTP_S_SHUT_ACK_CLI) ||
				  (cp->state == IP_VS_SCTP_S_SHUT_ACK_SER)))) {
			ip_vs_sync_conn(net, cp);
			goto out;
		}
	}

	/* Keep this block last: TCP and others with pp->num_states <= 1 */
	else if ((ipvs->sync_state & IP_VS_STATE_MASTER) &&
	    (((cp->protocol != IPPROTO_TCP ||
	       cp->state == IP_VS_TCP_S_ESTABLISHED) &&
	      (pkts % sysctl_sync_period(ipvs)
	       == sysctl_sync_threshold(ipvs))) ||
	     ((cp->protocol == IPPROTO_TCP) && (cp->old_state != cp->state) &&
	      ((cp->state == IP_VS_TCP_S_FIN_WAIT) ||
	       (cp->state == IP_VS_TCP_S_CLOSE) ||
	       (cp->state == IP_VS_TCP_S_CLOSE_WAIT) ||
	       (cp->state == IP_VS_TCP_S_TIME_WAIT)))))
		ip_vs_sync_conn(net, cp);
out:
	cp->old_state = cp->state;

	ip_vs_conn_put(cp);
	return ret;
}

/*
 *	AF_INET handler in NF_INET_LOCAL_IN chain
 *	Schedule and forward packets from remote clients
 */
static unsigned int
ip_vs_remote_request4(unsigned int hooknum, struct sk_buff *skb,
		      const struct net_device *in,
		      const struct net_device *out,
		      int (*okfn)(struct sk_buff *))
{
	return ip_vs_in(hooknum, skb, AF_INET);
}

/*
 *	AF_INET handler in NF_INET_LOCAL_OUT chain
 *	Schedule and forward packets from local clients
 */
static unsigned int
ip_vs_local_request4(unsigned int hooknum, struct sk_buff *skb,
		     const struct net_device *in, const struct net_device *out,
		     int (*okfn)(struct sk_buff *))
{
	unsigned int verdict;

	/* Disable BH in LOCAL_OUT until all places are fixed */
	local_bh_disable();
	verdict = ip_vs_in(hooknum, skb, AF_INET);
	local_bh_enable();
	return verdict;
}

#ifdef CONFIG_IP_VS_IPV6

/*
 *	AF_INET6 handler in NF_INET_LOCAL_IN chain
 *	Schedule and forward packets from remote clients
 */
static unsigned int
ip_vs_remote_request6(unsigned int hooknum, struct sk_buff *skb,
		      const struct net_device *in,
		      const struct net_device *out,
		      int (*okfn)(struct sk_buff *))
{
	return ip_vs_in(hooknum, skb, AF_INET6);
}

/*
 *	AF_INET6 handler in NF_INET_LOCAL_OUT chain
 *	Schedule and forward packets from local clients
 */
static unsigned int
ip_vs_local_request6(unsigned int hooknum, struct sk_buff *skb,
		     const struct net_device *in, const struct net_device *out,
		     int (*okfn)(struct sk_buff *))
{
	unsigned int verdict;

	/* Disable BH in LOCAL_OUT until all places are fixed */
	local_bh_disable();
	verdict = ip_vs_in(hooknum, skb, AF_INET6);
	local_bh_enable();
	return verdict;
}

#endif


/*
 *	It is hooked at the NF_INET_FORWARD chain, in order to catch ICMP
 *      related packets destined for 0.0.0.0/0.
 *      When fwmark-based virtual service is used, such as transparent
 *      cache cluster, TCP packets can be marked and routed to ip_vs_in,
 *      but ICMP destined for 0.0.0.0/0 cannot not be easily marked and
 *      sent to ip_vs_in_icmp. So, catch them at the NF_INET_FORWARD chain
 *      and send them to ip_vs_in_icmp.
 */
static unsigned int
ip_vs_forward_icmp(unsigned int hooknum, struct sk_buff *skb,
		   const struct net_device *in, const struct net_device *out,
		   int (*okfn)(struct sk_buff *))
{
	int r;
	struct net *net;

	if (ip_hdr(skb)->protocol != IPPROTO_ICMP)
		return NF_ACCEPT;

	/* ipvs enabled in this netns ? */
	net = skb_net(skb);
	if (!net_ipvs(net)->enable)
		return NF_ACCEPT;

	return ip_vs_in_icmp(skb, &r, hooknum);
}

#ifdef CONFIG_IP_VS_IPV6
static unsigned int
ip_vs_forward_icmp_v6(unsigned int hooknum, struct sk_buff *skb,
		      const struct net_device *in, const struct net_device *out,
		      int (*okfn)(struct sk_buff *))
{
	int r;
	struct net *net;

	if (ipv6_hdr(skb)->nexthdr != IPPROTO_ICMPV6)
		return NF_ACCEPT;

	/* ipvs enabled in this netns ? */
	net = skb_net(skb);
	if (!net_ipvs(net)->enable)
		return NF_ACCEPT;

	return ip_vs_in_icmp_v6(skb, &r, hooknum);
}
#endif


static struct nf_hook_ops ip_vs_ops[] __read_mostly = {
	/* After packet filtering, change source only for VS/NAT */
	{
		.hook		= ip_vs_reply4,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_NAT_SRC - 2,
	},
	/* After packet filtering, forward packet through VS/DR, VS/TUN,
	 * or VS/NAT(change destination), so that filtering rules can be
	 * applied to IPVS. */
	{
		.hook		= ip_vs_remote_request4,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_NAT_SRC - 1,
	},
	/* Before ip_vs_in, change source only for VS/NAT */
	{
		.hook		= ip_vs_local_reply4,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_NAT_DST + 1,
	},
	/* After mangle, schedule and forward local requests */
	{
		.hook		= ip_vs_local_request4,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_NAT_DST + 2,
	},
	/* After packet filtering (but before ip_vs_out_icmp), catch icmp
	 * destined for 0.0.0.0/0, which is for incoming IPVS connections */
	{
		.hook		= ip_vs_forward_icmp,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_FORWARD,
		.priority	= 99,
	},
	/* After packet filtering, change source only for VS/NAT */
	{
		.hook		= ip_vs_reply4,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_FORWARD,
		.priority	= 100,
	},
#ifdef CONFIG_IP_VS_IPV6
	/* After packet filtering, change source only for VS/NAT */
	{
		.hook		= ip_vs_reply6,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_NAT_SRC - 2,
	},
	/* After packet filtering, forward packet through VS/DR, VS/TUN,
	 * or VS/NAT(change destination), so that filtering rules can be
	 * applied to IPVS. */
	{
		.hook		= ip_vs_remote_request6,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_NAT_SRC - 1,
	},
	/* Before ip_vs_in, change source only for VS/NAT */
	{
		.hook		= ip_vs_local_reply6,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_NAT_DST + 1,
	},
	/* After mangle, schedule and forward local requests */
	{
		.hook		= ip_vs_local_request6,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_NAT_DST + 2,
	},
	/* After packet filtering (but before ip_vs_out_icmp), catch icmp
	 * destined for 0.0.0.0/0, which is for incoming IPVS connections */
	{
		.hook		= ip_vs_forward_icmp_v6,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_INET_FORWARD,
		.priority	= 99,
	},
	/* After packet filtering, change source only for VS/NAT */
	{
		.hook		= ip_vs_reply6,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_INET_FORWARD,
		.priority	= 100,
	},
#endif
};
/*
 *	Initialize IP Virtual Server netns mem.
 */
static int __net_init __ip_vs_init(struct net *net)
{
	struct netns_ipvs *ipvs;

	ipvs = net_generic(net, ip_vs_net_id);
	if (ipvs == NULL)
		return -ENOMEM;

	/* Hold the beast until a service is registerd */
	ipvs->enable = 0;
	ipvs->net = net;
	/* Counters used for creating unique names */
	ipvs->gen = atomic_read(&ipvs_netns_cnt);
	atomic_inc(&ipvs_netns_cnt);
	net->ipvs = ipvs;

	if (ip_vs_estimator_net_init(net) < 0)
		goto estimator_fail;

	if (ip_vs_control_net_init(net) < 0)
		goto control_fail;

	if (ip_vs_protocol_net_init(net) < 0)
		goto protocol_fail;

	if (ip_vs_app_net_init(net) < 0)
		goto app_fail;

	if (ip_vs_conn_net_init(net) < 0)
		goto conn_fail;

	if (ip_vs_sync_net_init(net) < 0)
		goto sync_fail;

	printk(KERN_INFO "IPVS: Creating netns size=%zu id=%d\n",
			 sizeof(struct netns_ipvs), ipvs->gen);
	return 0;
/*
 * Error handling
 */

sync_fail:
	ip_vs_conn_net_cleanup(net);
conn_fail:
	ip_vs_app_net_cleanup(net);
app_fail:
	ip_vs_protocol_net_cleanup(net);
protocol_fail:
	ip_vs_control_net_cleanup(net);
control_fail:
	ip_vs_estimator_net_cleanup(net);
estimator_fail:
	net->ipvs = NULL;
	return -ENOMEM;
}

static void __net_exit __ip_vs_cleanup(struct net *net)
{
	ip_vs_service_net_cleanup(net);	/* ip_vs_flush() with locks */
	ip_vs_conn_net_cleanup(net);
	ip_vs_app_net_cleanup(net);
	ip_vs_protocol_net_cleanup(net);
	ip_vs_control_net_cleanup(net);
	ip_vs_estimator_net_cleanup(net);
	IP_VS_DBG(2, "ipvs netns %d released\n", net_ipvs(net)->gen);
	net->ipvs = NULL;
}

static void __net_exit __ip_vs_dev_cleanup(struct net *net)
{
	EnterFunction(2);
	net_ipvs(net)->enable = 0;	/* Disable packet reception */
	smp_wmb();
	ip_vs_sync_net_cleanup(net);
	LeaveFunction(2);
}

static struct pernet_operations ipvs_core_ops = {
	.init = __ip_vs_init,
	.exit = __ip_vs_cleanup,
	.id   = &ip_vs_net_id,
	.size = sizeof(struct netns_ipvs),
};

static struct pernet_operations ipvs_core_dev_ops = {
	.exit = __ip_vs_dev_cleanup,
};

/*
 *	Initialize IP Virtual Server
 */
static int __init ip_vs_init(void)
{
	int ret;

	ret = ip_vs_control_init();
	if (ret < 0) {
		pr_err("can't setup control.\n");
		goto exit;
	}

	ip_vs_protocol_init();

	ret = ip_vs_conn_init();
	if (ret < 0) {
		pr_err("can't setup connection table.\n");
		goto cleanup_protocol;
	}

	ret = register_pernet_subsys(&ipvs_core_ops);	/* Alloc ip_vs struct */
	if (ret < 0)
		goto cleanup_conn;

	ret = register_pernet_device(&ipvs_core_dev_ops);
	if (ret < 0)
		goto cleanup_sub;

	ret = nf_register_hooks(ip_vs_ops, ARRAY_SIZE(ip_vs_ops));
	if (ret < 0) {
		pr_err("can't register hooks.\n");
		goto cleanup_dev;
	}

	ret = ip_vs_register_nl_ioctl();
	if (ret < 0) {
		pr_err("can't register netlink/ioctl.\n");
		goto cleanup_hooks;
	}

	pr_info("ipvs loaded.\n");

	return ret;

cleanup_hooks:
	nf_unregister_hooks(ip_vs_ops, ARRAY_SIZE(ip_vs_ops));
cleanup_dev:
	unregister_pernet_device(&ipvs_core_dev_ops);
cleanup_sub:
	unregister_pernet_subsys(&ipvs_core_ops);
cleanup_conn:
	ip_vs_conn_cleanup();
cleanup_protocol:
	ip_vs_protocol_cleanup();
	ip_vs_control_cleanup();
exit:
	return ret;
}

static void __exit ip_vs_cleanup(void)
{
	ip_vs_unregister_nl_ioctl();
	nf_unregister_hooks(ip_vs_ops, ARRAY_SIZE(ip_vs_ops));
	unregister_pernet_device(&ipvs_core_dev_ops);
	unregister_pernet_subsys(&ipvs_core_ops);	/* free ip_vs struct */
	ip_vs_conn_cleanup();
	ip_vs_protocol_cleanup();
	ip_vs_control_cleanup();
	pr_info("ipvs unloaded.\n");
}

module_init(ip_vs_init);
module_exit(ip_vs_cleanup);
MODULE_LICENSE("GPL");
