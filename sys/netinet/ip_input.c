/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ip_input.c	8.2 (Berkeley) 1/4/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bootp.h"
#include "opt_ipstealth.h"
#include "opt_ipsec.h"
#include "opt_route.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/hhook.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/rmlock.h>
#include <sys/rwlock.h>
#include <sys/sdt.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/pfil.h>
#include <net/route.h>
#include <net/netisr.h>
#include <net/rss_config.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_kdtrace.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/ip_fw.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_options.h>
#include <machine/in_cksum.h>
#include <netinet/ip_carp.h>
#include <netinet/in_rss.h>

#include <netipsec/ipsec_support.h>

#include <sys/socketvar.h>

#include <security/mac/mac_framework.h>

#ifdef CTASSERT
CTASSERT(sizeof(struct ip) == 20);
#endif

/* IP reassembly functions are defined in ip_reass.c. */
extern void ipreass_init(void);
extern void ipreass_drain(void);
extern void ipreass_slowtimo(void);
#ifdef VIMAGE
extern void ipreass_destroy(void);
#endif

struct rmlock in_ifaddr_lock;
RM_SYSINIT(in_ifaddr_lock, &in_ifaddr_lock, "in_ifaddr_lock");

VNET_DEFINE(int, rsvp_on);

VNET_DEFINE(int, ipforwarding);
SYSCTL_INT(_net_inet_ip, IPCTL_FORWARDING, forwarding, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(ipforwarding), 0,
    "Enable IP forwarding between interfaces");

VNET_DEFINE_STATIC(int, ipsendredirects) = 1;	/* XXX */
#define	V_ipsendredirects	VNET(ipsendredirects)
SYSCTL_INT(_net_inet_ip, IPCTL_SENDREDIRECTS, redirect, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(ipsendredirects), 0,
    "Enable sending IP redirects");

/*
 * XXX - Setting ip_checkinterface mostly implements the receive side of
 * the Strong ES model described in RFC 1122, but since the routing table
 * and transmit implementation do not implement the Strong ES model,
 * setting this to 1 results in an odd hybrid.
 *
 * XXX - ip_checkinterface currently must be disabled if you use ipnat
 * to translate the destination address to another local interface.
 *
 * XXX - ip_checkinterface must be disabled if you add IP aliases
 * to the loopback interface instead of the interface where the
 * packets for those addresses are received.
 */
VNET_DEFINE_STATIC(int, ip_checkinterface);
#define	V_ip_checkinterface	VNET(ip_checkinterface)
SYSCTL_INT(_net_inet_ip, OID_AUTO, check_interface, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(ip_checkinterface), 0,
    "Verify packet arrives on correct interface");

VNET_DEFINE(pfil_head_t, inet_pfil_head);	/* Packet filter hooks */

static struct netisr_handler ip_nh = {
	.nh_name = "ip",
	.nh_handler = ip_input,
	.nh_proto = NETISR_IP,
#ifdef	RSS
	.nh_m2cpuid = rss_soft_m2cpuid_v4,
	.nh_policy = NETISR_POLICY_CPU,
	.nh_dispatch = NETISR_DISPATCH_HYBRID,
#else
	.nh_policy = NETISR_POLICY_FLOW,
#endif
};

#ifdef	RSS
/*
 * Directly dispatched frames are currently assumed
 * to have a flowid already calculated.
 *
 * It should likely have something that assert it
 * actually has valid flow details.
 */
static struct netisr_handler ip_direct_nh = {
	.nh_name = "ip_direct",
	.nh_handler = ip_direct_input,
	.nh_proto = NETISR_IP_DIRECT,
	.nh_m2cpuid = rss_soft_m2cpuid_v4,
	.nh_policy = NETISR_POLICY_CPU,
	.nh_dispatch = NETISR_DISPATCH_HYBRID,
};
#endif

extern	struct domain inetdomain;
extern	struct protosw inetsw[];
u_char	ip_protox[IPPROTO_MAX];
VNET_DEFINE(struct in_ifaddrhead, in_ifaddrhead);  /* first inet address */
VNET_DEFINE(struct in_ifaddrhashhead *, in_ifaddrhashtbl); /* inet addr hash table  */
VNET_DEFINE(u_long, in_ifaddrhmask);		/* mask for hash table */

#ifdef IPCTL_DEFMTU
SYSCTL_INT(_net_inet_ip, IPCTL_DEFMTU, mtu, CTLFLAG_RW,
    &ip_mtu, 0, "Default MTU");
#endif

#ifdef IPSTEALTH
VNET_DEFINE(int, ipstealth);
SYSCTL_INT(_net_inet_ip, OID_AUTO, stealth, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(ipstealth), 0,
    "IP stealth mode, no TTL decrementation on forwarding");
#endif

/*
 * IP statistics are stored in the "array" of counter(9)s.
 */
VNET_PCPUSTAT_DEFINE(struct ipstat, ipstat);
VNET_PCPUSTAT_SYSINIT(ipstat);
SYSCTL_VNET_PCPUSTAT(_net_inet_ip, IPCTL_STATS, stats, struct ipstat, ipstat,
    "IP statistics (struct ipstat, netinet/ip_var.h)");

#ifdef VIMAGE
VNET_PCPUSTAT_SYSUNINIT(ipstat);
#endif /* VIMAGE */

/*
 * Kernel module interface for updating ipstat.  The argument is an index
 * into ipstat treated as an array.
 */
void
kmod_ipstat_inc(int statnum)
{

	counter_u64_add(VNET(ipstat)[statnum], 1);
}

void
kmod_ipstat_dec(int statnum)
{

	counter_u64_add(VNET(ipstat)[statnum], -1);
}

static int
sysctl_netinet_intr_queue_maxlen(SYSCTL_HANDLER_ARGS)
{
	int error, qlimit;

	netisr_getqlimit(&ip_nh, &qlimit);
	error = sysctl_handle_int(oidp, &qlimit, 0, req);
	if (error || !req->newptr)
		return (error);
	if (qlimit < 1)
		return (EINVAL);
	return (netisr_setqlimit(&ip_nh, qlimit));
}
SYSCTL_PROC(_net_inet_ip, IPCTL_INTRQMAXLEN, intr_queue_maxlen,
    CTLTYPE_INT|CTLFLAG_RW, 0, 0, sysctl_netinet_intr_queue_maxlen, "I",
    "Maximum size of the IP input queue");

static int
sysctl_netinet_intr_queue_drops(SYSCTL_HANDLER_ARGS)
{
	u_int64_t qdrops_long;
	int error, qdrops;

	netisr_getqdrops(&ip_nh, &qdrops_long);
	qdrops = qdrops_long;
	error = sysctl_handle_int(oidp, &qdrops, 0, req);
	if (error || !req->newptr)
		return (error);
	if (qdrops != 0)
		return (EINVAL);
	netisr_clearqdrops(&ip_nh);
	return (0);
}

SYSCTL_PROC(_net_inet_ip, IPCTL_INTRQDROPS, intr_queue_drops,
    CTLTYPE_INT|CTLFLAG_RD, 0, 0, sysctl_netinet_intr_queue_drops, "I",
    "Number of packets dropped from the IP input queue");

#ifdef	RSS
static int
sysctl_netinet_intr_direct_queue_maxlen(SYSCTL_HANDLER_ARGS)
{
	int error, qlimit;

	netisr_getqlimit(&ip_direct_nh, &qlimit);
	error = sysctl_handle_int(oidp, &qlimit, 0, req);
	if (error || !req->newptr)
		return (error);
	if (qlimit < 1)
		return (EINVAL);
	return (netisr_setqlimit(&ip_direct_nh, qlimit));
}
SYSCTL_PROC(_net_inet_ip, IPCTL_INTRDQMAXLEN, intr_direct_queue_maxlen,
    CTLTYPE_INT|CTLFLAG_RW, 0, 0, sysctl_netinet_intr_direct_queue_maxlen,
    "I", "Maximum size of the IP direct input queue");

static int
sysctl_netinet_intr_direct_queue_drops(SYSCTL_HANDLER_ARGS)
{
	u_int64_t qdrops_long;
	int error, qdrops;

	netisr_getqdrops(&ip_direct_nh, &qdrops_long);
	qdrops = qdrops_long;
	error = sysctl_handle_int(oidp, &qdrops, 0, req);
	if (error || !req->newptr)
		return (error);
	if (qdrops != 0)
		return (EINVAL);
	netisr_clearqdrops(&ip_direct_nh);
	return (0);
}

SYSCTL_PROC(_net_inet_ip, IPCTL_INTRDQDROPS, intr_direct_queue_drops,
    CTLTYPE_INT|CTLFLAG_RD, 0, 0, sysctl_netinet_intr_direct_queue_drops, "I",
    "Number of packets dropped from the IP direct input queue");
#endif	/* RSS */

/*
 * IP initialization: fill in IP protocol switch table.
 * All protocols not implemented in kernel go to raw IP protocol handler.
 */
void
ip_init(void)
{
	struct pfil_head_args args;
	struct protosw *pr;
	int i;

	CK_STAILQ_INIT(&V_in_ifaddrhead);
	V_in_ifaddrhashtbl = hashinit(INADDR_NHASH, M_IFADDR, &V_in_ifaddrhmask);

	/* Initialize IP reassembly queue. */
	ipreass_init();

	/* Initialize packet filter hooks. */
	args.pa_version = PFIL_VERSION;
	args.pa_flags = PFIL_IN | PFIL_OUT;
	args.pa_type = PFIL_TYPE_IP4;
	args.pa_headname = PFIL_INET_NAME;
	V_inet_pfil_head = pfil_head_register(&args);

	if (hhook_head_register(HHOOK_TYPE_IPSEC_IN, AF_INET,
	    &V_ipsec_hhh_in[HHOOK_IPSEC_INET],
	    HHOOK_WAITOK | HHOOK_HEADISINVNET) != 0)
		printf("%s: WARNING: unable to register input helper hook\n",
		    __func__);
	if (hhook_head_register(HHOOK_TYPE_IPSEC_OUT, AF_INET,
	    &V_ipsec_hhh_out[HHOOK_IPSEC_INET],
	    HHOOK_WAITOK | HHOOK_HEADISINVNET) != 0)
		printf("%s: WARNING: unable to register output helper hook\n",
		    __func__);

	/* Skip initialization of globals for non-default instances. */
#ifdef VIMAGE
	if (!IS_DEFAULT_VNET(curvnet)) {
		netisr_register_vnet(&ip_nh);
#ifdef	RSS
		netisr_register_vnet(&ip_direct_nh);
#endif
		return;
	}
#endif

	pr = pffindproto(PF_INET, IPPROTO_RAW, SOCK_RAW);
	if (pr == NULL)
		panic("ip_init: PF_INET not found");

	/* Initialize the entire ip_protox[] array to IPPROTO_RAW. */
	for (i = 0; i < IPPROTO_MAX; i++)
		ip_protox[i] = pr - inetsw;
	/*
	 * Cycle through IP protocols and put them into the appropriate place
	 * in ip_protox[].
	 */
	for (pr = inetdomain.dom_protosw;
	    pr < inetdomain.dom_protoswNPROTOSW; pr++)
		if (pr->pr_domain->dom_family == PF_INET &&
		    pr->pr_protocol && pr->pr_protocol != IPPROTO_RAW) {
			/* Be careful to only index valid IP protocols. */
			if (pr->pr_protocol < IPPROTO_MAX)
				ip_protox[pr->pr_protocol] = pr - inetsw;
		}

	netisr_register(&ip_nh);
#ifdef	RSS
	netisr_register(&ip_direct_nh);
#endif
}

#ifdef VIMAGE
static void
ip_destroy(void *unused __unused)
{
	struct ifnet *ifp;
	int error;

#ifdef	RSS
	netisr_unregister_vnet(&ip_direct_nh);
#endif
	netisr_unregister_vnet(&ip_nh);

	pfil_head_unregister(V_inet_pfil_head);
	error = hhook_head_deregister(V_ipsec_hhh_in[HHOOK_IPSEC_INET]);
	if (error != 0) {
		printf("%s: WARNING: unable to deregister input helper hook "
		    "type HHOOK_TYPE_IPSEC_IN, id HHOOK_IPSEC_INET: "
		    "error %d returned\n", __func__, error);
	}
	error = hhook_head_deregister(V_ipsec_hhh_out[HHOOK_IPSEC_INET]);
	if (error != 0) {
		printf("%s: WARNING: unable to deregister output helper hook "
		    "type HHOOK_TYPE_IPSEC_OUT, id HHOOK_IPSEC_INET: "
		    "error %d returned\n", __func__, error);
	}

	/* Remove the IPv4 addresses from all interfaces. */
	in_ifscrub_all();

	/* Make sure the IPv4 routes are gone as well. */
	IFNET_RLOCK();
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link)
		rt_flushifroutes_af(ifp, AF_INET);
	IFNET_RUNLOCK();

	/* Destroy IP reassembly queue. */
	ipreass_destroy();

	/* Cleanup in_ifaddr hash table; should be empty. */
	hashdestroy(V_in_ifaddrhashtbl, M_IFADDR, V_in_ifaddrhmask);
}

VNET_SYSUNINIT(ip, SI_SUB_PROTO_DOMAIN, SI_ORDER_THIRD, ip_destroy, NULL);
#endif

#ifdef	RSS
/*
 * IP direct input routine.
 *
 * This is called when reinjecting completed fragments where
 * all of the previous checking and book-keeping has been done.
 */
void
ip_direct_input(struct mbuf *m)
{
	struct ip *ip;
	int hlen;

	ip = mtod(m, struct ip *);
	hlen = ip->ip_hl << 2;

#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	if (IPSEC_ENABLED(ipv4)) {
		if (IPSEC_INPUT(ipv4, m, hlen, ip->ip_p) != 0)
			return;
	}
#endif /* IPSEC */
	IPSTAT_INC(ips_delivered);
	(*inetsw[ip_protox[ip->ip_p]].pr_input)(&m, &hlen, ip->ip_p);
	return;
}
#endif

/*
 * Ip input routine.  Checksum and byte swap header.  If fragmented
 * try to reassemble.  Process options.  Pass to next level.
 */
void
ip_input(struct mbuf *m)
{
	struct rm_priotracker in_ifa_tracker;
	struct ip *ip = NULL;
	struct in_ifaddr *ia = NULL;
	struct ifaddr *ifa;
	struct ifnet *ifp;
	int    checkif, hlen = 0;
	uint16_t sum, ip_len;
	int dchg = 0;				/* dest changed after fw */
	struct in_addr odst;			/* original dst address */

	M_ASSERTPKTHDR(m);

	if (m->m_flags & M_FASTFWD_OURS) {
		m->m_flags &= ~M_FASTFWD_OURS;
		/* Set up some basics that will be used later. */
		ip = mtod(m, struct ip *);
		hlen = ip->ip_hl << 2;
		ip_len = ntohs(ip->ip_len);
		goto ours;
	}

	IPSTAT_INC(ips_total);

	if (m->m_pkthdr.len < sizeof(struct ip))
		goto tooshort;

	if (m->m_len < sizeof (struct ip) &&
	    (m = m_pullup(m, sizeof (struct ip))) == NULL) {
		IPSTAT_INC(ips_toosmall);
		return;
	}
	ip = mtod(m, struct ip *);

	if (ip->ip_v != IPVERSION) {
		IPSTAT_INC(ips_badvers);
		goto bad;
	}

	hlen = ip->ip_hl << 2;
	if (hlen < sizeof(struct ip)) {	/* minimum header length */
		IPSTAT_INC(ips_badhlen);
		goto bad;
	}
	if (hlen > m->m_len) {
		if ((m = m_pullup(m, hlen)) == NULL) {
			IPSTAT_INC(ips_badhlen);
			return;
		}
		ip = mtod(m, struct ip *);
	}

	IP_PROBE(receive, NULL, NULL, ip, m->m_pkthdr.rcvif, ip, NULL);

	/* 127/8 must not appear on wire - RFC1122 */
	ifp = m->m_pkthdr.rcvif;
	if ((ntohl(ip->ip_dst.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET ||
	    (ntohl(ip->ip_src.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET) {
		if ((ifp->if_flags & IFF_LOOPBACK) == 0) {
			IPSTAT_INC(ips_badaddr);
			goto bad;
		}
	}

	if (m->m_pkthdr.csum_flags & CSUM_IP_CHECKED) {
		sum = !(m->m_pkthdr.csum_flags & CSUM_IP_VALID);
	} else {
		if (hlen == sizeof(struct ip)) {
			sum = in_cksum_hdr(ip);
		} else {
			sum = in_cksum(m, hlen);
		}
	}
	if (sum) {
		IPSTAT_INC(ips_badsum);
		goto bad;
	}

#ifdef ALTQ
	if (altq_input != NULL && (*altq_input)(m, AF_INET) == 0)
		/* packet is dropped by traffic conditioner */
		return;
#endif

	ip_len = ntohs(ip->ip_len);
	if (ip_len < hlen) {
		IPSTAT_INC(ips_badlen);
		goto bad;
	}

	/*
	 * Check that the amount of data in the buffers
	 * is as at least much as the IP header would have us expect.
	 * Trim mbufs if longer than we expect.
	 * Drop packet if shorter than we expect.
	 */
	if (m->m_pkthdr.len < ip_len) {
tooshort:
		IPSTAT_INC(ips_tooshort);
		goto bad;
	}
	if (m->m_pkthdr.len > ip_len) {
		if (m->m_len == m->m_pkthdr.len) {
			m->m_len = ip_len;
			m->m_pkthdr.len = ip_len;
		} else
			m_adj(m, ip_len - m->m_pkthdr.len);
	}

	/*
	 * Try to forward the packet, but if we fail continue.
	 * ip_tryforward() does not generate redirects, so fall
	 * through to normal processing if redirects are required.
	 * ip_tryforward() does inbound and outbound packet firewall
	 * processing. If firewall has decided that destination becomes
	 * our local address, it sets M_FASTFWD_OURS flag. In this
	 * case skip another inbound firewall processing and update
	 * ip pointer.
	 */
	if (V_ipforwarding != 0 && V_ipsendredirects == 0
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	    && (!IPSEC_ENABLED(ipv4) ||
	    IPSEC_CAPS(ipv4, m, IPSEC_CAP_OPERABLE) == 0)
#endif
	    ) {
		if ((m = ip_tryforward(m)) == NULL)
			return;
		if (m->m_flags & M_FASTFWD_OURS) {
			m->m_flags &= ~M_FASTFWD_OURS;
			ip = mtod(m, struct ip *);
			goto ours;
		}
	}

#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	/*
	 * Bypass packet filtering for packets previously handled by IPsec.
	 */
	if (IPSEC_ENABLED(ipv4) &&
	    IPSEC_CAPS(ipv4, m, IPSEC_CAP_BYPASS_FILTER) != 0)
			goto passin;
#endif

	/*
	 * Run through list of hooks for input packets.
	 *
	 * NB: Beware of the destination address changing (e.g.
	 *     by NAT rewriting).  When this happens, tell
	 *     ip_forward to do the right thing.
	 */

	/* Jump over all PFIL processing if hooks are not active. */
	if (!PFIL_HOOKED_IN(V_inet_pfil_head))
		goto passin;

	odst = ip->ip_dst;
	if (pfil_run_hooks(V_inet_pfil_head, &m, ifp, PFIL_IN, NULL) !=
	    PFIL_PASS)
		return;
	if (m == NULL)			/* consumed by filter */
		return;

	ip = mtod(m, struct ip *);
	dchg = (odst.s_addr != ip->ip_dst.s_addr);
	ifp = m->m_pkthdr.rcvif;

	if (m->m_flags & M_FASTFWD_OURS) {
		m->m_flags &= ~M_FASTFWD_OURS;
		goto ours;
	}
	if (m->m_flags & M_IP_NEXTHOP) {
		if (m_tag_find(m, PACKET_TAG_IPFORWARD, NULL) != NULL) {
			/*
			 * Directly ship the packet on.  This allows
			 * forwarding packets originally destined to us
			 * to some other directly connected host.
			 */
			ip_forward(m, 1);
			return;
		}
	}
passin:

	/*
	 * Process options and, if not destined for us,
	 * ship it on.  ip_dooptions returns 1 when an
	 * error was detected (causing an icmp message
	 * to be sent and the original packet to be freed).
	 */
	if (hlen > sizeof (struct ip) && ip_dooptions(m, 0))
		return;

        /* greedy RSVP, snatches any PATH packet of the RSVP protocol and no
         * matter if it is destined to another node, or whether it is 
         * a multicast one, RSVP wants it! and prevents it from being forwarded
         * anywhere else. Also checks if the rsvp daemon is running before
	 * grabbing the packet.
         */
	if (V_rsvp_on && ip->ip_p==IPPROTO_RSVP) 
		goto ours;

	/*
	 * Check our list of addresses, to see if the packet is for us.
	 * If we don't have any addresses, assume any unicast packet
	 * we receive might be for us (and let the upper layers deal
	 * with it).
	 */
	if (CK_STAILQ_EMPTY(&V_in_ifaddrhead) &&
	    (m->m_flags & (M_MCAST|M_BCAST)) == 0)
		goto ours;

	/*
	 * Enable a consistency check between the destination address
	 * and the arrival interface for a unicast packet (the RFC 1122
	 * strong ES model) if IP forwarding is disabled and the packet
	 * is not locally generated and the packet is not subject to
	 * 'ipfw fwd'.
	 *
	 * XXX - Checking also should be disabled if the destination
	 * address is ipnat'ed to a different interface.
	 *
	 * XXX - Checking is incompatible with IP aliases added
	 * to the loopback interface instead of the interface where
	 * the packets are received.
	 *
	 * XXX - This is the case for carp vhost IPs as well so we
	 * insert a workaround. If the packet got here, we already
	 * checked with carp_iamatch() and carp_forus().
	 */
	checkif = V_ip_checkinterface && (V_ipforwarding == 0) && 
	    ifp != NULL && ((ifp->if_flags & IFF_LOOPBACK) == 0) &&
	    ifp->if_carp == NULL && (dchg == 0);

	/*
	 * Check for exact addresses in the hash bucket.
	 */
	IN_IFADDR_RLOCK(&in_ifa_tracker);
	LIST_FOREACH(ia, INADDR_HASH(ip->ip_dst.s_addr), ia_hash) {
		/*
		 * If the address matches, verify that the packet
		 * arrived via the correct interface if checking is
		 * enabled.
		 */
		if (IA_SIN(ia)->sin_addr.s_addr == ip->ip_dst.s_addr && 
		    (!checkif || ia->ia_ifp == ifp)) {
			counter_u64_add(ia->ia_ifa.ifa_ipackets, 1);
			counter_u64_add(ia->ia_ifa.ifa_ibytes,
			    m->m_pkthdr.len);
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			goto ours;
		}
	}
	IN_IFADDR_RUNLOCK(&in_ifa_tracker);

	/*
	 * Check for broadcast addresses.
	 *
	 * Only accept broadcast packets that arrive via the matching
	 * interface.  Reception of forwarded directed broadcasts would
	 * be handled via ip_forward() and ether_output() with the loopback
	 * into the stack for SIMPLEX interfaces handled by ether_output().
	 */
	if (ifp != NULL && ifp->if_flags & IFF_BROADCAST) {
		struct epoch_tracker et;

		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			ia = ifatoia(ifa);
			if (satosin(&ia->ia_broadaddr)->sin_addr.s_addr ==
			    ip->ip_dst.s_addr) {
				counter_u64_add(ia->ia_ifa.ifa_ipackets, 1);
				counter_u64_add(ia->ia_ifa.ifa_ibytes,
				    m->m_pkthdr.len);
				NET_EPOCH_EXIT(et);
				goto ours;
			}
#ifdef BOOTP_COMPAT
			if (IA_SIN(ia)->sin_addr.s_addr == INADDR_ANY) {
				counter_u64_add(ia->ia_ifa.ifa_ipackets, 1);
				counter_u64_add(ia->ia_ifa.ifa_ibytes,
				    m->m_pkthdr.len);
				NET_EPOCH_EXIT(et);
				goto ours;
			}
#endif
		}
		NET_EPOCH_EXIT(et);
		ia = NULL;
	}
	/* RFC 3927 2.7: Do not forward datagrams for 169.254.0.0/16. */
	if (IN_LINKLOCAL(ntohl(ip->ip_dst.s_addr))) {
		IPSTAT_INC(ips_cantforward);
		m_freem(m);
		return;
	}
	if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr))) {
		if (V_ip_mrouter) {
			/*
			 * If we are acting as a multicast router, all
			 * incoming multicast packets are passed to the
			 * kernel-level multicast forwarding function.
			 * The packet is returned (relatively) intact; if
			 * ip_mforward() returns a non-zero value, the packet
			 * must be discarded, else it may be accepted below.
			 */
			if (ip_mforward && ip_mforward(ip, ifp, m, 0) != 0) {
				IPSTAT_INC(ips_cantforward);
				m_freem(m);
				return;
			}

			/*
			 * The process-level routing daemon needs to receive
			 * all multicast IGMP packets, whether or not this
			 * host belongs to their destination groups.
			 */
			if (ip->ip_p == IPPROTO_IGMP)
				goto ours;
			IPSTAT_INC(ips_forward);
		}
		/*
		 * Assume the packet is for us, to avoid prematurely taking
		 * a lock on the in_multi hash. Protocols must perform
		 * their own filtering and update statistics accordingly.
		 */
		goto ours;
	}
	if (ip->ip_dst.s_addr == (u_long)INADDR_BROADCAST)
		goto ours;
	if (ip->ip_dst.s_addr == INADDR_ANY)
		goto ours;

	/*
	 * Not for us; forward if possible and desirable.
	 */
	if (V_ipforwarding == 0) {
		IPSTAT_INC(ips_cantforward);
		m_freem(m);
	} else {
		ip_forward(m, dchg);
	}
	return;

ours:
#ifdef IPSTEALTH
	/*
	 * IPSTEALTH: Process non-routing options only
	 * if the packet is destined for us.
	 */
	if (V_ipstealth && hlen > sizeof (struct ip) && ip_dooptions(m, 1))
		return;
#endif /* IPSTEALTH */

	/*
	 * Attempt reassembly; if it succeeds, proceed.
	 * ip_reass() will return a different mbuf.
	 */
	if (ip->ip_off & htons(IP_MF | IP_OFFMASK)) {
		/* XXXGL: shouldn't we save & set m_flags? */
		m = ip_reass(m);
		if (m == NULL)
			return;
		ip = mtod(m, struct ip *);
		/* Get the header length of the reassembled packet */
		hlen = ip->ip_hl << 2;
	}

#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	if (IPSEC_ENABLED(ipv4)) {
		if (IPSEC_INPUT(ipv4, m, hlen, ip->ip_p) != 0)
			return;
	}
#endif /* IPSEC */

	/*
	 * Switch out to protocol's input routine.
	 */
	IPSTAT_INC(ips_delivered);

	(*inetsw[ip_protox[ip->ip_p]].pr_input)(&m, &hlen, ip->ip_p);
	return;
bad:
	m_freem(m);
}

/*
 * IP timer processing;
 * if a timer expires on a reassembly
 * queue, discard it.
 */
void
ip_slowtimo(void)
{
	VNET_ITERATOR_DECL(vnet_iter);

	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		ipreass_slowtimo();
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
}

void
ip_drain(void)
{
	VNET_ITERATOR_DECL(vnet_iter);

	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		ipreass_drain();
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
}

/*
 * The protocol to be inserted into ip_protox[] must be already registered
 * in inetsw[], either statically or through pf_proto_register().
 */
int
ipproto_register(short ipproto)
{
	struct protosw *pr;

	/* Sanity checks. */
	if (ipproto <= 0 || ipproto >= IPPROTO_MAX)
		return (EPROTONOSUPPORT);

	/*
	 * The protocol slot must not be occupied by another protocol
	 * already.  An index pointing to IPPROTO_RAW is unused.
	 */
	pr = pffindproto(PF_INET, IPPROTO_RAW, SOCK_RAW);
	if (pr == NULL)
		return (EPFNOSUPPORT);
	if (ip_protox[ipproto] != pr - inetsw)	/* IPPROTO_RAW */
		return (EEXIST);

	/* Find the protocol position in inetsw[] and set the index. */
	for (pr = inetdomain.dom_protosw;
	     pr < inetdomain.dom_protoswNPROTOSW; pr++) {
		if (pr->pr_domain->dom_family == PF_INET &&
		    pr->pr_protocol && pr->pr_protocol == ipproto) {
			ip_protox[pr->pr_protocol] = pr - inetsw;
			return (0);
		}
	}
	return (EPROTONOSUPPORT);
}

int
ipproto_unregister(short ipproto)
{
	struct protosw *pr;

	/* Sanity checks. */
	if (ipproto <= 0 || ipproto >= IPPROTO_MAX)
		return (EPROTONOSUPPORT);

	/* Check if the protocol was indeed registered. */
	pr = pffindproto(PF_INET, IPPROTO_RAW, SOCK_RAW);
	if (pr == NULL)
		return (EPFNOSUPPORT);
	if (ip_protox[ipproto] == pr - inetsw)  /* IPPROTO_RAW */
		return (ENOENT);

	/* Reset the protocol slot to IPPROTO_RAW. */
	ip_protox[ipproto] = pr - inetsw;
	return (0);
}

u_char inetctlerrmap[PRC_NCMDS] = {
	0,		0,		0,		0,
	0,		EMSGSIZE,	EHOSTDOWN,	EHOSTUNREACH,
	EHOSTUNREACH,	EHOSTUNREACH,	ECONNREFUSED,	ECONNREFUSED,
	EMSGSIZE,	EHOSTUNREACH,	0,		0,
	0,		0,		EHOSTUNREACH,	0,
	ENOPROTOOPT,	ECONNREFUSED
};

/*
 * Forward a packet.  If some error occurs return the sender
 * an icmp packet.  Note we can't always generate a meaningful
 * icmp message because icmp doesn't have a large enough repertoire
 * of codes and types.
 *
 * If not forwarding, just drop the packet.  This could be confusing
 * if ipforwarding was zero but some routing protocol was advancing
 * us as a gateway to somewhere.  However, we must let the routing
 * protocol deal with that.
 *
 * The srcrt parameter indicates whether the packet is being forwarded
 * via a source route.
 */
void
ip_forward(struct mbuf *m, int srcrt)
{
	struct ip *ip = mtod(m, struct ip *);
	struct in_ifaddr *ia;
	struct mbuf *mcopy;
	struct sockaddr_in *sin;
	struct in_addr dest;
	struct route ro;
	struct epoch_tracker et;
	int error, type = 0, code = 0, mtu = 0;

	if (m->m_flags & (M_BCAST|M_MCAST) || in_canforward(ip->ip_dst) == 0) {
		IPSTAT_INC(ips_cantforward);
		m_freem(m);
		return;
	}
	if (
#ifdef IPSTEALTH
	    V_ipstealth == 0 &&
#endif
	    ip->ip_ttl <= IPTTLDEC) {
		icmp_error(m, ICMP_TIMXCEED, ICMP_TIMXCEED_INTRANS, 0, 0);
		return;
	}

	bzero(&ro, sizeof(ro));
	sin = (struct sockaddr_in *)&ro.ro_dst;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	sin->sin_addr = ip->ip_dst;
#ifdef RADIX_MPATH
	rtalloc_mpath_fib(&ro,
	    ntohl(ip->ip_src.s_addr ^ ip->ip_dst.s_addr),
	    M_GETFIB(m));
#else
	in_rtalloc_ign(&ro, 0, M_GETFIB(m));
#endif
	NET_EPOCH_ENTER(et);
	if (ro.ro_rt != NULL) {
		ia = ifatoia(ro.ro_rt->rt_ifa);
	} else
		ia = NULL;
	/*
	 * Save the IP header and at most 8 bytes of the payload,
	 * in case we need to generate an ICMP message to the src.
	 *
	 * XXX this can be optimized a lot by saving the data in a local
	 * buffer on the stack (72 bytes at most), and only allocating the
	 * mbuf if really necessary. The vast majority of the packets
	 * are forwarded without having to send an ICMP back (either
	 * because unnecessary, or because rate limited), so we are
	 * really we are wasting a lot of work here.
	 *
	 * We don't use m_copym() because it might return a reference
	 * to a shared cluster. Both this function and ip_output()
	 * assume exclusive access to the IP header in `m', so any
	 * data in a cluster may change before we reach icmp_error().
	 */
	mcopy = m_gethdr(M_NOWAIT, m->m_type);
	if (mcopy != NULL && !m_dup_pkthdr(mcopy, m, M_NOWAIT)) {
		/*
		 * It's probably ok if the pkthdr dup fails (because
		 * the deep copy of the tag chain failed), but for now
		 * be conservative and just discard the copy since
		 * code below may some day want the tags.
		 */
		m_free(mcopy);
		mcopy = NULL;
	}
	if (mcopy != NULL) {
		mcopy->m_len = min(ntohs(ip->ip_len), M_TRAILINGSPACE(mcopy));
		mcopy->m_pkthdr.len = mcopy->m_len;
		m_copydata(m, 0, mcopy->m_len, mtod(mcopy, caddr_t));
	}
#ifdef IPSTEALTH
	if (V_ipstealth == 0)
#endif
		ip->ip_ttl -= IPTTLDEC;
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	if (IPSEC_ENABLED(ipv4)) {
		if ((error = IPSEC_FORWARD(ipv4, m)) != 0) {
			/* mbuf consumed by IPsec */
			m_freem(mcopy);
			if (error != EINPROGRESS)
				IPSTAT_INC(ips_cantforward);
			goto out;
		}
		/* No IPsec processing required */
	}
#endif /* IPSEC */
	/*
	 * If forwarding packet using same interface that it came in on,
	 * perhaps should send a redirect to sender to shortcut a hop.
	 * Only send redirect if source is sending directly to us,
	 * and if packet was not source routed (or has any options).
	 * Also, don't send redirect if forwarding using a default route
	 * or a route modified by a redirect.
	 */
	dest.s_addr = 0;
	if (!srcrt && V_ipsendredirects &&
	    ia != NULL && ia->ia_ifp == m->m_pkthdr.rcvif) {
		struct rtentry *rt;

		rt = ro.ro_rt;

		if (rt && (rt->rt_flags & (RTF_DYNAMIC|RTF_MODIFIED)) == 0 &&
		    satosin(rt_key(rt))->sin_addr.s_addr != 0) {
#define	RTA(rt)	((struct in_ifaddr *)(rt->rt_ifa))
			u_long src = ntohl(ip->ip_src.s_addr);

			if (RTA(rt) &&
			    (src & RTA(rt)->ia_subnetmask) == RTA(rt)->ia_subnet) {
				if (rt->rt_flags & RTF_GATEWAY)
					dest.s_addr = satosin(rt->rt_gateway)->sin_addr.s_addr;
				else
					dest.s_addr = ip->ip_dst.s_addr;
				/* Router requirements says to only send host redirects */
				type = ICMP_REDIRECT;
				code = ICMP_REDIRECT_HOST;
			}
		}
	}

	error = ip_output(m, NULL, &ro, IP_FORWARDING, NULL, NULL);

	if (error == EMSGSIZE && ro.ro_rt)
		mtu = ro.ro_rt->rt_mtu;
	RO_RTFREE(&ro);

	if (error)
		IPSTAT_INC(ips_cantforward);
	else {
		IPSTAT_INC(ips_forward);
		if (type)
			IPSTAT_INC(ips_redirectsent);
		else {
			if (mcopy)
				m_freem(mcopy);
			goto out;
		}
	}
	if (mcopy == NULL)
		goto out;


	switch (error) {

	case 0:				/* forwarded, but need redirect */
		/* type, code set above */
		break;

	case ENETUNREACH:
	case EHOSTUNREACH:
	case ENETDOWN:
	case EHOSTDOWN:
	default:
		type = ICMP_UNREACH;
		code = ICMP_UNREACH_HOST;
		break;

	case EMSGSIZE:
		type = ICMP_UNREACH;
		code = ICMP_UNREACH_NEEDFRAG;
		/*
		 * If the MTU was set before make sure we are below the
		 * interface MTU.
		 * If the MTU wasn't set before use the interface mtu or
		 * fall back to the next smaller mtu step compared to the
		 * current packet size.
		 */
		if (mtu != 0) {
			if (ia != NULL)
				mtu = min(mtu, ia->ia_ifp->if_mtu);
		} else {
			if (ia != NULL)
				mtu = ia->ia_ifp->if_mtu;
			else
				mtu = ip_next_mtu(ntohs(ip->ip_len), 0);
		}
		IPSTAT_INC(ips_cantfrag);
		break;

	case ENOBUFS:
	case EACCES:			/* ipfw denied packet */
		m_freem(mcopy);
		goto out;
	}
	icmp_error(mcopy, type, code, dest.s_addr, mtu);
 out:
	NET_EPOCH_EXIT(et);
}

#define	CHECK_SO_CT(sp, ct) \
    (((sp->so_options & SO_TIMESTAMP) && (sp->so_ts_clock == ct)) ? 1 : 0)

void
ip_savecontrol(struct inpcb *inp, struct mbuf **mp, struct ip *ip,
    struct mbuf *m)
{
	bool stamped;

	stamped = false;
	if ((inp->inp_socket->so_options & SO_BINTIME) ||
	    CHECK_SO_CT(inp->inp_socket, SO_TS_BINTIME)) {
		struct bintime boottimebin, bt;
		struct timespec ts1;

		if ((m->m_flags & (M_PKTHDR | M_TSTMP)) == (M_PKTHDR |
		    M_TSTMP)) {
			mbuf_tstmp2timespec(m, &ts1);
			timespec2bintime(&ts1, &bt);
			getboottimebin(&boottimebin);
			bintime_add(&bt, &boottimebin);
		} else {
			bintime(&bt);
		}
		*mp = sbcreatecontrol((caddr_t)&bt, sizeof(bt),
		    SCM_BINTIME, SOL_SOCKET);
		if (*mp != NULL) {
			mp = &(*mp)->m_next;
			stamped = true;
		}
	}
	if (CHECK_SO_CT(inp->inp_socket, SO_TS_REALTIME_MICRO)) {
		struct bintime boottimebin, bt1;
		struct timespec ts1;;
		struct timeval tv;

		if ((m->m_flags & (M_PKTHDR | M_TSTMP)) == (M_PKTHDR |
		    M_TSTMP)) {
			mbuf_tstmp2timespec(m, &ts1);
			timespec2bintime(&ts1, &bt1);
			getboottimebin(&boottimebin);
			bintime_add(&bt1, &boottimebin);
			bintime2timeval(&bt1, &tv);
		} else {
			microtime(&tv);
		}
		*mp = sbcreatecontrol((caddr_t)&tv, sizeof(tv),
		    SCM_TIMESTAMP, SOL_SOCKET);
		if (*mp != NULL) {
			mp = &(*mp)->m_next;
			stamped = true;
		}
	} else if (CHECK_SO_CT(inp->inp_socket, SO_TS_REALTIME)) {
		struct bintime boottimebin;
		struct timespec ts, ts1;

		if ((m->m_flags & (M_PKTHDR | M_TSTMP)) == (M_PKTHDR |
		    M_TSTMP)) {
			mbuf_tstmp2timespec(m, &ts);
			getboottimebin(&boottimebin);
			bintime2timespec(&boottimebin, &ts1);
			timespecadd(&ts, &ts1, &ts);
		} else {
			nanotime(&ts);
		}
		*mp = sbcreatecontrol((caddr_t)&ts, sizeof(ts),
		    SCM_REALTIME, SOL_SOCKET);
		if (*mp != NULL) {
			mp = &(*mp)->m_next;
			stamped = true;
		}
	} else if (CHECK_SO_CT(inp->inp_socket, SO_TS_MONOTONIC)) {
		struct timespec ts;

		if ((m->m_flags & (M_PKTHDR | M_TSTMP)) == (M_PKTHDR |
		    M_TSTMP))
			mbuf_tstmp2timespec(m, &ts);
		else
			nanouptime(&ts);
		*mp = sbcreatecontrol((caddr_t)&ts, sizeof(ts),
		    SCM_MONOTONIC, SOL_SOCKET);
		if (*mp != NULL) {
			mp = &(*mp)->m_next;
			stamped = true;
		}
	}
	if (stamped && (m->m_flags & (M_PKTHDR | M_TSTMP)) == (M_PKTHDR |
	    M_TSTMP)) {
		struct sock_timestamp_info sti;

		bzero(&sti, sizeof(sti));
		sti.st_info_flags = ST_INFO_HW;
		if ((m->m_flags & M_TSTMP_HPREC) != 0)
			sti.st_info_flags |= ST_INFO_HW_HPREC;
		*mp = sbcreatecontrol((caddr_t)&sti, sizeof(sti), SCM_TIME_INFO,
		    SOL_SOCKET);
		if (*mp != NULL)
			mp = &(*mp)->m_next;
	}
	if (inp->inp_flags & INP_RECVDSTADDR) {
		*mp = sbcreatecontrol((caddr_t)&ip->ip_dst,
		    sizeof(struct in_addr), IP_RECVDSTADDR, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
	if (inp->inp_flags & INP_RECVTTL) {
		*mp = sbcreatecontrol((caddr_t)&ip->ip_ttl,
		    sizeof(u_char), IP_RECVTTL, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
#ifdef notyet
	/* XXX
	 * Moving these out of udp_input() made them even more broken
	 * than they already were.
	 */
	/* options were tossed already */
	if (inp->inp_flags & INP_RECVOPTS) {
		*mp = sbcreatecontrol((caddr_t)opts_deleted_above,
		    sizeof(struct in_addr), IP_RECVOPTS, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
	/* ip_srcroute doesn't do what we want here, need to fix */
	if (inp->inp_flags & INP_RECVRETOPTS) {
		*mp = sbcreatecontrol((caddr_t)ip_srcroute(m),
		    sizeof(struct in_addr), IP_RECVRETOPTS, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
#endif
	if (inp->inp_flags & INP_RECVIF) {
		struct ifnet *ifp;
		struct sdlbuf {
			struct sockaddr_dl sdl;
			u_char	pad[32];
		} sdlbuf;
		struct sockaddr_dl *sdp;
		struct sockaddr_dl *sdl2 = &sdlbuf.sdl;

		if ((ifp = m->m_pkthdr.rcvif) &&
		    ifp->if_index && ifp->if_index <= V_if_index) {
			sdp = (struct sockaddr_dl *)ifp->if_addr->ifa_addr;
			/*
			 * Change our mind and don't try copy.
			 */
			if (sdp->sdl_family != AF_LINK ||
			    sdp->sdl_len > sizeof(sdlbuf)) {
				goto makedummy;
			}
			bcopy(sdp, sdl2, sdp->sdl_len);
		} else {
makedummy:	
			sdl2->sdl_len =
			    offsetof(struct sockaddr_dl, sdl_data[0]);
			sdl2->sdl_family = AF_LINK;
			sdl2->sdl_index = 0;
			sdl2->sdl_nlen = sdl2->sdl_alen = sdl2->sdl_slen = 0;
		}
		*mp = sbcreatecontrol((caddr_t)sdl2, sdl2->sdl_len,
		    IP_RECVIF, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
	if (inp->inp_flags & INP_RECVTOS) {
		*mp = sbcreatecontrol((caddr_t)&ip->ip_tos,
		    sizeof(u_char), IP_RECVTOS, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}

	if (inp->inp_flags2 & INP_RECVFLOWID) {
		uint32_t flowid, flow_type;

		flowid = m->m_pkthdr.flowid;
		flow_type = M_HASHTYPE_GET(m);

		/*
		 * XXX should handle the failure of one or the
		 * other - don't populate both?
		 */
		*mp = sbcreatecontrol((caddr_t) &flowid,
		    sizeof(uint32_t), IP_FLOWID, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
		*mp = sbcreatecontrol((caddr_t) &flow_type,
		    sizeof(uint32_t), IP_FLOWTYPE, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}

#ifdef	RSS
	if (inp->inp_flags2 & INP_RECVRSSBUCKETID) {
		uint32_t flowid, flow_type;
		uint32_t rss_bucketid;

		flowid = m->m_pkthdr.flowid;
		flow_type = M_HASHTYPE_GET(m);

		if (rss_hash2bucket(flowid, flow_type, &rss_bucketid) == 0) {
			*mp = sbcreatecontrol((caddr_t) &rss_bucketid,
			   sizeof(uint32_t), IP_RSSBUCKETID, IPPROTO_IP);
			if (*mp)
				mp = &(*mp)->m_next;
		}
	}
#endif
}

/*
 * XXXRW: Multicast routing code in ip_mroute.c is generally MPSAFE, but the
 * ip_rsvp and ip_rsvp_on variables need to be interlocked with rsvp_on
 * locking.  This code remains in ip_input.c as ip_mroute.c is optionally
 * compiled.
 */
VNET_DEFINE_STATIC(int, ip_rsvp_on);
VNET_DEFINE(struct socket *, ip_rsvpd);

#define	V_ip_rsvp_on		VNET(ip_rsvp_on)

int
ip_rsvp_init(struct socket *so)
{

	if (so->so_type != SOCK_RAW ||
	    so->so_proto->pr_protocol != IPPROTO_RSVP)
		return EOPNOTSUPP;

	if (V_ip_rsvpd != NULL)
		return EADDRINUSE;

	V_ip_rsvpd = so;
	/*
	 * This may seem silly, but we need to be sure we don't over-increment
	 * the RSVP counter, in case something slips up.
	 */
	if (!V_ip_rsvp_on) {
		V_ip_rsvp_on = 1;
		V_rsvp_on++;
	}

	return 0;
}

int
ip_rsvp_done(void)
{

	V_ip_rsvpd = NULL;
	/*
	 * This may seem silly, but we need to be sure we don't over-decrement
	 * the RSVP counter, in case something slips up.
	 */
	if (V_ip_rsvp_on) {
		V_ip_rsvp_on = 0;
		V_rsvp_on--;
	}
	return 0;
}

int
rsvp_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m;

	m = *mp;
	*mp = NULL;

	if (rsvp_input_p) { /* call the real one if loaded */
		*mp = m;
		rsvp_input_p(mp, offp, proto);
		return (IPPROTO_DONE);
	}

	/* Can still get packets with rsvp_on = 0 if there is a local member
	 * of the group to which the RSVP packet is addressed.  But in this
	 * case we want to throw the packet away.
	 */
	
	if (!V_rsvp_on) {
		m_freem(m);
		return (IPPROTO_DONE);
	}

	if (V_ip_rsvpd != NULL) { 
		*mp = m;
		rip_input(mp, offp, proto);
		return (IPPROTO_DONE);
	}
	/* Drop the packet */
	m_freem(m);
	return (IPPROTO_DONE);
}
