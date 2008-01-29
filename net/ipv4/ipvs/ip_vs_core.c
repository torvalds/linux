/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a module
 *              over the Netfilter framework. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Version:     $Id: ip_vs_core.c,v 1.34 2003/05/10 03:05:23 wensong Exp $
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/icmp.h>

#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>                   /* for icmp_send */
#include <net/route.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <net/ip_vs.h>


EXPORT_SYMBOL(register_ip_vs_scheduler);
EXPORT_SYMBOL(unregister_ip_vs_scheduler);
EXPORT_SYMBOL(ip_vs_skb_replace);
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


/* ID used in ICMP lookups */
#define icmp_id(icmph)          (((icmph)->un).echo.id)

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
	case IPPROTO_ICMP:
		return "ICMP";
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
	if (dest && (dest->flags & IP_VS_DEST_F_AVAILABLE)) {
		spin_lock(&dest->stats.lock);
		dest->stats.inpkts++;
		dest->stats.inbytes += skb->len;
		spin_unlock(&dest->stats.lock);

		spin_lock(&dest->svc->stats.lock);
		dest->svc->stats.inpkts++;
		dest->svc->stats.inbytes += skb->len;
		spin_unlock(&dest->svc->stats.lock);

		spin_lock(&ip_vs_stats.lock);
		ip_vs_stats.inpkts++;
		ip_vs_stats.inbytes += skb->len;
		spin_unlock(&ip_vs_stats.lock);
	}
}


static inline void
ip_vs_out_stats(struct ip_vs_conn *cp, struct sk_buff *skb)
{
	struct ip_vs_dest *dest = cp->dest;
	if (dest && (dest->flags & IP_VS_DEST_F_AVAILABLE)) {
		spin_lock(&dest->stats.lock);
		dest->stats.outpkts++;
		dest->stats.outbytes += skb->len;
		spin_unlock(&dest->stats.lock);

		spin_lock(&dest->svc->stats.lock);
		dest->svc->stats.outpkts++;
		dest->svc->stats.outbytes += skb->len;
		spin_unlock(&dest->svc->stats.lock);

		spin_lock(&ip_vs_stats.lock);
		ip_vs_stats.outpkts++;
		ip_vs_stats.outbytes += skb->len;
		spin_unlock(&ip_vs_stats.lock);
	}
}


static inline void
ip_vs_conn_stats(struct ip_vs_conn *cp, struct ip_vs_service *svc)
{
	spin_lock(&cp->dest->stats.lock);
	cp->dest->stats.conns++;
	spin_unlock(&cp->dest->stats.lock);

	spin_lock(&svc->stats.lock);
	svc->stats.conns++;
	spin_unlock(&svc->stats.lock);

	spin_lock(&ip_vs_stats.lock);
	ip_vs_stats.conns++;
	spin_unlock(&ip_vs_stats.lock);
}


static inline int
ip_vs_set_state(struct ip_vs_conn *cp, int direction,
		const struct sk_buff *skb,
		struct ip_vs_protocol *pp)
{
	if (unlikely(!pp->state_transition))
		return 0;
	return pp->state_transition(cp, direction, skb, pp);
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
		    const struct sk_buff *skb,
		    __be16 ports[2])
{
	struct ip_vs_conn *cp = NULL;
	struct iphdr *iph = ip_hdr(skb);
	struct ip_vs_dest *dest;
	struct ip_vs_conn *ct;
	__be16  dport;	 /* destination port to forward */
	__be32  snet;	 /* source network of the client, after masking */

	/* Mask saddr with the netmask to adjust template granularity */
	snet = iph->saddr & svc->netmask;

	IP_VS_DBG(6, "p-schedule: src %u.%u.%u.%u:%u dest %u.%u.%u.%u:%u "
		  "mnet %u.%u.%u.%u\n",
		  NIPQUAD(iph->saddr), ntohs(ports[0]),
		  NIPQUAD(iph->daddr), ntohs(ports[1]),
		  NIPQUAD(snet));

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
	if (ports[1] == svc->port) {
		/* Check if a template already exists */
		if (svc->port != FTPPORT)
			ct = ip_vs_ct_in_get(iph->protocol, snet, 0,
					       iph->daddr, ports[1]);
		else
			ct = ip_vs_ct_in_get(iph->protocol, snet, 0,
					       iph->daddr, 0);

		if (!ct || !ip_vs_check_template(ct)) {
			/*
			 * No template found or the dest of the connection
			 * template is not available.
			 */
			dest = svc->scheduler->schedule(svc, skb);
			if (dest == NULL) {
				IP_VS_DBG(1, "p-schedule: no dest found.\n");
				return NULL;
			}

			/*
			 * Create a template like <protocol,caddr,0,
			 * vaddr,vport,daddr,dport> for non-ftp service,
			 * and <protocol,caddr,0,vaddr,0,daddr,0>
			 * for ftp service.
			 */
			if (svc->port != FTPPORT)
				ct = ip_vs_conn_new(iph->protocol,
						    snet, 0,
						    iph->daddr,
						    ports[1],
						    dest->addr, dest->port,
						    IP_VS_CONN_F_TEMPLATE,
						    dest);
			else
				ct = ip_vs_conn_new(iph->protocol,
						    snet, 0,
						    iph->daddr, 0,
						    dest->addr, 0,
						    IP_VS_CONN_F_TEMPLATE,
						    dest);
			if (ct == NULL)
				return NULL;

			ct->timeout = svc->timeout;
		} else {
			/* set destination with the found template */
			dest = ct->dest;
		}
		dport = dest->port;
	} else {
		/*
		 * Note: persistent fwmark-based services and persistent
		 * port zero service are handled here.
		 * fwmark template: <IPPROTO_IP,caddr,0,fwmark,0,daddr,0>
		 * port zero template: <protocol,caddr,0,vaddr,0,daddr,0>
		 */
		if (svc->fwmark)
			ct = ip_vs_ct_in_get(IPPROTO_IP, snet, 0,
					       htonl(svc->fwmark), 0);
		else
			ct = ip_vs_ct_in_get(iph->protocol, snet, 0,
					       iph->daddr, 0);

		if (!ct || !ip_vs_check_template(ct)) {
			/*
			 * If it is not persistent port zero, return NULL,
			 * otherwise create a connection template.
			 */
			if (svc->port)
				return NULL;

			dest = svc->scheduler->schedule(svc, skb);
			if (dest == NULL) {
				IP_VS_DBG(1, "p-schedule: no dest found.\n");
				return NULL;
			}

			/*
			 * Create a template according to the service
			 */
			if (svc->fwmark)
				ct = ip_vs_conn_new(IPPROTO_IP,
						    snet, 0,
						    htonl(svc->fwmark), 0,
						    dest->addr, 0,
						    IP_VS_CONN_F_TEMPLATE,
						    dest);
			else
				ct = ip_vs_conn_new(iph->protocol,
						    snet, 0,
						    iph->daddr, 0,
						    dest->addr, 0,
						    IP_VS_CONN_F_TEMPLATE,
						    dest);
			if (ct == NULL)
				return NULL;

			ct->timeout = svc->timeout;
		} else {
			/* set destination with the found template */
			dest = ct->dest;
		}
		dport = ports[1];
	}

	/*
	 *    Create a new connection according to the template
	 */
	cp = ip_vs_conn_new(iph->protocol,
			    iph->saddr, ports[0],
			    iph->daddr, ports[1],
			    dest->addr, dport,
			    0,
			    dest);
	if (cp == NULL) {
		ip_vs_conn_put(ct);
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
 */
struct ip_vs_conn *
ip_vs_schedule(struct ip_vs_service *svc, const struct sk_buff *skb)
{
	struct ip_vs_conn *cp = NULL;
	struct iphdr *iph = ip_hdr(skb);
	struct ip_vs_dest *dest;
	__be16 _ports[2], *pptr;

	pptr = skb_header_pointer(skb, iph->ihl*4,
				  sizeof(_ports), _ports);
	if (pptr == NULL)
		return NULL;

	/*
	 *    Persistent service
	 */
	if (svc->flags & IP_VS_SVC_F_PERSISTENT)
		return ip_vs_sched_persist(svc, skb, pptr);

	/*
	 *    Non-persistent service
	 */
	if (!svc->fwmark && pptr[1] != svc->port) {
		if (!svc->port)
			IP_VS_ERR("Schedule: port zero only supported "
				  "in persistent services, "
				  "check your ipvs configuration\n");
		return NULL;
	}

	dest = svc->scheduler->schedule(svc, skb);
	if (dest == NULL) {
		IP_VS_DBG(1, "Schedule: no dest found.\n");
		return NULL;
	}

	/*
	 *    Create a connection entry.
	 */
	cp = ip_vs_conn_new(iph->protocol,
			    iph->saddr, pptr[0],
			    iph->daddr, pptr[1],
			    dest->addr, dest->port?dest->port:pptr[1],
			    0,
			    dest);
	if (cp == NULL)
		return NULL;

	IP_VS_DBG(6, "Schedule fwd:%c c:%u.%u.%u.%u:%u v:%u.%u.%u.%u:%u "
		  "d:%u.%u.%u.%u:%u conn->flags:%X conn->refcnt:%d\n",
		  ip_vs_fwd_tag(cp),
		  NIPQUAD(cp->caddr), ntohs(cp->cport),
		  NIPQUAD(cp->vaddr), ntohs(cp->vport),
		  NIPQUAD(cp->daddr), ntohs(cp->dport),
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
		struct ip_vs_protocol *pp)
{
	__be16 _ports[2], *pptr;
	struct iphdr *iph = ip_hdr(skb);

	pptr = skb_header_pointer(skb, iph->ihl*4,
				  sizeof(_ports), _ports);
	if (pptr == NULL) {
		ip_vs_service_put(svc);
		return NF_DROP;
	}

	/* if it is fwmark-based service, the cache_bypass sysctl is up
	   and the destination is RTN_UNICAST (and not local), then create
	   a cache_bypass connection entry */
	if (sysctl_ip_vs_cache_bypass && svc->fwmark
	    && (inet_addr_type(&init_net, iph->daddr) == RTN_UNICAST)) {
		int ret, cs;
		struct ip_vs_conn *cp;

		ip_vs_service_put(svc);

		/* create a new connection entry */
		IP_VS_DBG(6, "ip_vs_leave: create a cache_bypass entry\n");
		cp = ip_vs_conn_new(iph->protocol,
				    iph->saddr, pptr[0],
				    iph->daddr, pptr[1],
				    0, 0,
				    IP_VS_CONN_F_BYPASS,
				    NULL);
		if (cp == NULL)
			return NF_DROP;

		/* statistics */
		ip_vs_in_stats(cp, skb);

		/* set state */
		cs = ip_vs_set_state(cp, IP_VS_DIR_INPUT, skb, pp);

		/* transmit the first SYN packet */
		ret = cp->packet_xmit(skb, cp, pp);
		/* do not touch skb anymore */

		atomic_inc(&cp->in_pkts);
		ip_vs_conn_put(cp);
		return ret;
	}

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
	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);
	return NF_DROP;
}


/*
 *      It is hooked before NF_IP_PRI_NAT_SRC at the NF_INET_POST_ROUTING
 *      chain, and is used for VS/NAT.
 *      It detects packets for VS/NAT connections and sends the packets
 *      immediately. This can avoid that iptable_nat mangles the packets
 *      for VS/NAT.
 */
static unsigned int ip_vs_post_routing(unsigned int hooknum,
				       struct sk_buff *skb,
				       const struct net_device *in,
				       const struct net_device *out,
				       int (*okfn)(struct sk_buff *))
{
	if (!skb->ipvs_property)
		return NF_ACCEPT;
	/* The packet was sent from IPVS, exit this chain */
	return NF_STOP;
}

__sum16 ip_vs_checksum_complete(struct sk_buff *skb, int offset)
{
	return csum_fold(skb_checksum(skb, offset, skb->len - offset, 0));
}

static inline int ip_vs_gather_frags(struct sk_buff *skb, u_int32_t user)
{
	int err = ip_defrag(skb, user);

	if (!err)
		ip_send_check(ip_hdr(skb));

	return err;
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
		iph->saddr = cp->vaddr;
		ip_send_check(iph);
		ciph->daddr = cp->vaddr;
		ip_send_check(ciph);
	} else {
		iph->daddr = cp->daddr;
		ip_send_check(iph);
		ciph->saddr = cp->daddr;
		ip_send_check(ciph);
	}

	/* the TCP/UDP port */
	if (IPPROTO_TCP == ciph->protocol || IPPROTO_UDP == ciph->protocol) {
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
		IP_VS_DBG_PKT(11, pp, skb, (void *)ciph - (void *)iph,
			"Forwarding altered outgoing ICMP");
	else
		IP_VS_DBG_PKT(11, pp, skb, (void *)ciph - (void *)iph,
			"Forwarding altered incoming ICMP");
}

/*
 *	Handle ICMP messages in the inside-to-outside direction (outgoing).
 *	Find any that might be relevant, check against existing connections,
 *	forward to the right destination host if relevant.
 *	Currently handles error types - unreachable, quench, ttl exceeded.
 *	(Only used in VS/NAT)
 */
static int ip_vs_out_icmp(struct sk_buff *skb, int *related)
{
	struct iphdr *iph;
	struct icmphdr	_icmph, *ic;
	struct iphdr	_ciph, *cih;	/* The ip header contained within the ICMP */
	struct ip_vs_conn *cp;
	struct ip_vs_protocol *pp;
	unsigned int offset, ihl, verdict;

	*related = 1;

	/* reassemble IP fragments */
	if (ip_hdr(skb)->frag_off & htons(IP_MF | IP_OFFSET)) {
		if (ip_vs_gather_frags(skb, IP_DEFRAG_VS_OUT))
			return NF_STOLEN;
	}

	iph = ip_hdr(skb);
	offset = ihl = iph->ihl * 4;
	ic = skb_header_pointer(skb, offset, sizeof(_icmph), &_icmph);
	if (ic == NULL)
		return NF_DROP;

	IP_VS_DBG(12, "Outgoing ICMP (%d,%d) %u.%u.%u.%u->%u.%u.%u.%u\n",
		  ic->type, ntohs(icmp_id(ic)),
		  NIPQUAD(iph->saddr), NIPQUAD(iph->daddr));

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

	IP_VS_DBG_PKT(11, pp, skb, offset, "Checking outgoing ICMP for");

	offset += cih->ihl * 4;

	/* The embedded headers contain source and dest in reverse order */
	cp = pp->conn_out_get(skb, pp, cih, offset, 1);
	if (!cp)
		return NF_ACCEPT;

	verdict = NF_DROP;

	if (IP_VS_FWD_METHOD(cp) != 0) {
		IP_VS_ERR("shouldn't reach here, because the box is on the "
			  "half connection in the tun/dr module.\n");
	}

	/* Ensure the checksum is correct */
	if (!skb_csum_unnecessary(skb) && ip_vs_checksum_complete(skb, ihl)) {
		/* Failed checksum! */
		IP_VS_DBG(1, "Forward ICMP: failed checksum from %d.%d.%d.%d!\n",
			  NIPQUAD(iph->saddr));
		goto out;
	}

	if (IPPROTO_TCP == cih->protocol || IPPROTO_UDP == cih->protocol)
		offset += 2 * sizeof(__u16);
	if (!skb_make_writable(skb, offset))
		goto out;

	ip_vs_nat_icmp(skb, pp, cp, 1);

	/* do the statistics and put it back */
	ip_vs_out_stats(cp, skb);

	skb->ipvs_property = 1;
	verdict = NF_ACCEPT;

  out:
	__ip_vs_conn_put(cp);

	return verdict;
}

static inline int is_tcp_reset(const struct sk_buff *skb)
{
	struct tcphdr _tcph, *th;

	th = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_tcph), &_tcph);
	if (th == NULL)
		return 0;
	return th->rst;
}

/*
 *	It is hooked at the NF_INET_FORWARD chain, used only for VS/NAT.
 *	Check if outgoing packet belongs to the established ip_vs_conn,
 *      rewrite addresses of the packet and send it on its way...
 */
static unsigned int
ip_vs_out(unsigned int hooknum, struct sk_buff *skb,
	  const struct net_device *in, const struct net_device *out,
	  int (*okfn)(struct sk_buff *))
{
	struct iphdr	*iph;
	struct ip_vs_protocol *pp;
	struct ip_vs_conn *cp;
	int ihl;

	EnterFunction(11);

	if (skb->ipvs_property)
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	if (unlikely(iph->protocol == IPPROTO_ICMP)) {
		int related, verdict = ip_vs_out_icmp(skb, &related);

		if (related)
			return verdict;
		iph = ip_hdr(skb);
	}

	pp = ip_vs_proto_get(iph->protocol);
	if (unlikely(!pp))
		return NF_ACCEPT;

	/* reassemble IP fragments */
	if (unlikely(iph->frag_off & htons(IP_MF|IP_OFFSET) &&
		     !pp->dont_defrag)) {
		if (ip_vs_gather_frags(skb, IP_DEFRAG_VS_OUT))
			return NF_STOLEN;
		iph = ip_hdr(skb);
	}

	ihl = iph->ihl << 2;

	/*
	 * Check if the packet belongs to an existing entry
	 */
	cp = pp->conn_out_get(skb, pp, iph, ihl, 0);

	if (unlikely(!cp)) {
		if (sysctl_ip_vs_nat_icmp_send &&
		    (pp->protocol == IPPROTO_TCP ||
		     pp->protocol == IPPROTO_UDP)) {
			__be16 _ports[2], *pptr;

			pptr = skb_header_pointer(skb, ihl,
						  sizeof(_ports), _ports);
			if (pptr == NULL)
				return NF_ACCEPT;	/* Not for me */
			if (ip_vs_lookup_real_service(iph->protocol,
						      iph->saddr, pptr[0])) {
				/*
				 * Notify the real server: there is no
				 * existing entry if it is not RST
				 * packet or not TCP packet.
				 */
				if (iph->protocol != IPPROTO_TCP
				    || !is_tcp_reset(skb)) {
					icmp_send(skb,ICMP_DEST_UNREACH,
						  ICMP_PORT_UNREACH, 0);
					return NF_DROP;
				}
			}
		}
		IP_VS_DBG_PKT(12, pp, skb, 0,
			      "packet continues traversal as normal");
		return NF_ACCEPT;
	}

	IP_VS_DBG_PKT(11, pp, skb, 0, "Outgoing packet");

	if (!skb_make_writable(skb, ihl))
		goto drop;

	/* mangle the packet */
	if (pp->snat_handler && !pp->snat_handler(skb, pp, cp))
		goto drop;
	ip_hdr(skb)->saddr = cp->vaddr;
	ip_send_check(ip_hdr(skb));

	/* For policy routing, packets originating from this
	 * machine itself may be routed differently to packets
	 * passing through.  We want this packet to be routed as
	 * if it came from this machine itself.  So re-compute
	 * the routing information.
	 */
	if (ip_route_me_harder(skb, RTN_LOCAL) != 0)
		goto drop;

	IP_VS_DBG_PKT(10, pp, skb, 0, "After SNAT");

	ip_vs_out_stats(cp, skb);
	ip_vs_set_state(cp, IP_VS_DIR_OUTPUT, skb, pp);
	ip_vs_conn_put(cp);

	skb->ipvs_property = 1;

	LeaveFunction(11);
	return NF_ACCEPT;

  drop:
	ip_vs_conn_put(cp);
	kfree_skb(skb);
	return NF_STOLEN;
}


/*
 *	Handle ICMP messages in the outside-to-inside direction (incoming).
 *	Find any that might be relevant, check against existing connections,
 *	forward to the right destination host if relevant.
 *	Currently handles error types - unreachable, quench, ttl exceeded.
 */
static int
ip_vs_in_icmp(struct sk_buff *skb, int *related, unsigned int hooknum)
{
	struct iphdr *iph;
	struct icmphdr	_icmph, *ic;
	struct iphdr	_ciph, *cih;	/* The ip header contained within the ICMP */
	struct ip_vs_conn *cp;
	struct ip_vs_protocol *pp;
	unsigned int offset, ihl, verdict;

	*related = 1;

	/* reassemble IP fragments */
	if (ip_hdr(skb)->frag_off & htons(IP_MF | IP_OFFSET)) {
		if (ip_vs_gather_frags(skb, hooknum == NF_INET_LOCAL_IN ?
					    IP_DEFRAG_VS_IN : IP_DEFRAG_VS_FWD))
			return NF_STOLEN;
	}

	iph = ip_hdr(skb);
	offset = ihl = iph->ihl * 4;
	ic = skb_header_pointer(skb, offset, sizeof(_icmph), &_icmph);
	if (ic == NULL)
		return NF_DROP;

	IP_VS_DBG(12, "Incoming ICMP (%d,%d) %u.%u.%u.%u->%u.%u.%u.%u\n",
		  ic->type, ntohs(icmp_id(ic)),
		  NIPQUAD(iph->saddr), NIPQUAD(iph->daddr));

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

	IP_VS_DBG_PKT(11, pp, skb, offset, "Checking incoming ICMP for");

	offset += cih->ihl * 4;

	/* The embedded headers contain source and dest in reverse order */
	cp = pp->conn_in_get(skb, pp, cih, offset, 1);
	if (!cp)
		return NF_ACCEPT;

	verdict = NF_DROP;

	/* Ensure the checksum is correct */
	if (!skb_csum_unnecessary(skb) && ip_vs_checksum_complete(skb, ihl)) {
		/* Failed checksum! */
		IP_VS_DBG(1, "Incoming ICMP: failed checksum from %d.%d.%d.%d!\n",
			  NIPQUAD(iph->saddr));
		goto out;
	}

	/* do the statistics and put it back */
	ip_vs_in_stats(cp, skb);
	if (IPPROTO_TCP == cih->protocol || IPPROTO_UDP == cih->protocol)
		offset += 2 * sizeof(__u16);
	verdict = ip_vs_icmp_xmit(skb, cp, pp, offset);
	/* do not touch skb anymore */

  out:
	__ip_vs_conn_put(cp);

	return verdict;
}

/*
 *	Check if it's for virtual services, look it up,
 *	and send it on its way...
 */
static unsigned int
ip_vs_in(unsigned int hooknum, struct sk_buff *skb,
	 const struct net_device *in, const struct net_device *out,
	 int (*okfn)(struct sk_buff *))
{
	struct iphdr	*iph;
	struct ip_vs_protocol *pp;
	struct ip_vs_conn *cp;
	int ret, restart;
	int ihl;

	/*
	 *	Big tappo: only PACKET_HOST (neither loopback nor mcasts)
	 *	... don't know why 1st test DOES NOT include 2nd (?)
	 */
	if (unlikely(skb->pkt_type != PACKET_HOST
		     || skb->dev->flags & IFF_LOOPBACK || skb->sk)) {
		IP_VS_DBG(12, "packet type=%d proto=%d daddr=%d.%d.%d.%d ignored\n",
			  skb->pkt_type,
			  ip_hdr(skb)->protocol,
			  NIPQUAD(ip_hdr(skb)->daddr));
		return NF_ACCEPT;
	}

	iph = ip_hdr(skb);
	if (unlikely(iph->protocol == IPPROTO_ICMP)) {
		int related, verdict = ip_vs_in_icmp(skb, &related, hooknum);

		if (related)
			return verdict;
		iph = ip_hdr(skb);
	}

	/* Protocol supported? */
	pp = ip_vs_proto_get(iph->protocol);
	if (unlikely(!pp))
		return NF_ACCEPT;

	ihl = iph->ihl << 2;

	/*
	 * Check if the packet belongs to an existing connection entry
	 */
	cp = pp->conn_in_get(skb, pp, iph, ihl, 0);

	if (unlikely(!cp)) {
		int v;

		if (!pp->conn_schedule(skb, pp, &v, &cp))
			return v;
	}

	if (unlikely(!cp)) {
		/* sorry, all this trouble for a no-hit :) */
		IP_VS_DBG_PKT(12, pp, skb, 0,
			      "packet continues traversal as normal");
		return NF_ACCEPT;
	}

	IP_VS_DBG_PKT(11, pp, skb, 0, "Incoming packet");

	/* Check the server status */
	if (cp->dest && !(cp->dest->flags & IP_VS_DEST_F_AVAILABLE)) {
		/* the destination server is not available */

		if (sysctl_ip_vs_expire_nodest_conn) {
			/* try to expire the connection immediately */
			ip_vs_conn_expire_now(cp);
		}
		/* don't restart its timer, and silently
		   drop the packet. */
		__ip_vs_conn_put(cp);
		return NF_DROP;
	}

	ip_vs_in_stats(cp, skb);
	restart = ip_vs_set_state(cp, IP_VS_DIR_INPUT, skb, pp);
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
	 */
	atomic_inc(&cp->in_pkts);
	if ((ip_vs_sync_state & IP_VS_STATE_MASTER) &&
	    (((cp->protocol != IPPROTO_TCP ||
	       cp->state == IP_VS_TCP_S_ESTABLISHED) &&
	      (atomic_read(&cp->in_pkts) % sysctl_ip_vs_sync_threshold[1]
	       == sysctl_ip_vs_sync_threshold[0])) ||
	     ((cp->protocol == IPPROTO_TCP) && (cp->old_state != cp->state) &&
	      ((cp->state == IP_VS_TCP_S_FIN_WAIT) ||
	       (cp->state == IP_VS_TCP_S_CLOSE)))))
		ip_vs_sync_conn(cp);
	cp->old_state = cp->state;

	ip_vs_conn_put(cp);
	return ret;
}


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

	if (ip_hdr(skb)->protocol != IPPROTO_ICMP)
		return NF_ACCEPT;

	return ip_vs_in_icmp(skb, &r, hooknum);
}


static struct nf_hook_ops ip_vs_ops[] __read_mostly = {
	/* After packet filtering, forward packet through VS/DR, VS/TUN,
	 * or VS/NAT(change destination), so that filtering rules can be
	 * applied to IPVS. */
	{
		.hook		= ip_vs_in,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum        = NF_INET_LOCAL_IN,
		.priority       = 100,
	},
	/* After packet filtering, change source only for VS/NAT */
	{
		.hook		= ip_vs_out,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum        = NF_INET_FORWARD,
		.priority       = 100,
	},
	/* After packet filtering (but before ip_vs_out_icmp), catch icmp
	 * destined for 0.0.0.0/0, which is for incoming IPVS connections */
	{
		.hook		= ip_vs_forward_icmp,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum        = NF_INET_FORWARD,
		.priority       = 99,
	},
	/* Before the netfilter connection tracking, exit from POST_ROUTING */
	{
		.hook		= ip_vs_post_routing,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum        = NF_INET_POST_ROUTING,
		.priority       = NF_IP_PRI_NAT_SRC-1,
	},
};


/*
 *	Initialize IP Virtual Server
 */
static int __init ip_vs_init(void)
{
	int ret;

	ret = ip_vs_control_init();
	if (ret < 0) {
		IP_VS_ERR("can't setup control.\n");
		goto cleanup_nothing;
	}

	ip_vs_protocol_init();

	ret = ip_vs_app_init();
	if (ret < 0) {
		IP_VS_ERR("can't setup application helper.\n");
		goto cleanup_protocol;
	}

	ret = ip_vs_conn_init();
	if (ret < 0) {
		IP_VS_ERR("can't setup connection table.\n");
		goto cleanup_app;
	}

	ret = nf_register_hooks(ip_vs_ops, ARRAY_SIZE(ip_vs_ops));
	if (ret < 0) {
		IP_VS_ERR("can't register hooks.\n");
		goto cleanup_conn;
	}

	IP_VS_INFO("ipvs loaded.\n");
	return ret;

  cleanup_conn:
	ip_vs_conn_cleanup();
  cleanup_app:
	ip_vs_app_cleanup();
  cleanup_protocol:
	ip_vs_protocol_cleanup();
	ip_vs_control_cleanup();
  cleanup_nothing:
	return ret;
}

static void __exit ip_vs_cleanup(void)
{
	nf_unregister_hooks(ip_vs_ops, ARRAY_SIZE(ip_vs_ops));
	ip_vs_conn_cleanup();
	ip_vs_app_cleanup();
	ip_vs_protocol_cleanup();
	ip_vs_control_cleanup();
	IP_VS_INFO("ipvs unloaded.\n");
}

module_init(ip_vs_init);
module_exit(ip_vs_cleanup);
MODULE_LICENSE("GPL");
