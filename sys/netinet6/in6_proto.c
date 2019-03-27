/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$KAME: in6_proto.c,v 1.91 2001/05/27 13:28:35 itojun Exp $
 */

/*-
 * Copyright (c) 1982, 1986, 1993
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
 *	@(#)in_proto.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"
#include "opt_ipstealth.h"
#include "opt_sctp.h"
#include "opt_mpath.h"
#include "opt_route.h"

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/domain.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/radix.h>
#include <net/route.h>
#ifdef RADIX_MPATH
#include <net/radix_mpath.h>
#endif

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip_encap.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>

#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet6/tcp6_var.h>
#include <netinet6/raw_ip6.h>
#include <netinet6/udp6_var.h>
#include <netinet6/pim6_var.h>
#include <netinet6/nd6.h>

#ifdef SCTP
#include <netinet/in_pcb.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctp.h>
#include <netinet/sctp_var.h>
#include <netinet6/sctp6_var.h>
#endif /* SCTP */

#include <netinet6/ip6protosw.h>

/*
 * TCP/IP protocol family: IP6, ICMP6, UDP, TCP.
 */
FEATURE(inet6, "Internet Protocol version 6");

extern	struct domain inet6domain;
static	struct pr_usrreqs nousrreqs;

#define PR_LISTEN	0
#define PR_ABRTACPTDIS	0

/* Spacer for loadable protocols. */
#define IP6PROTOSPACER   			\
{						\
	.pr_domain =		&inet6domain,	\
	.pr_protocol =		PROTO_SPACER,	\
	.pr_usrreqs =		&nousrreqs	\
}

struct protosw inet6sw[] = {
{
	.pr_type =		0,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_IPV6,
	.pr_init =		ip6_init,
	.pr_slowtimo =		frag6_slowtimo,
	.pr_drain =		frag6_drain,
	.pr_usrreqs =		&nousrreqs,
},
{
	.pr_type =		SOCK_DGRAM,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_UDP,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		udp6_input,
	.pr_ctlinput =		udp6_ctlinput,
	.pr_ctloutput =		ip6_ctloutput,
#ifndef INET	/* Do not call initialization twice. */
	.pr_init =		udp_init,
#endif
	.pr_usrreqs =		&udp6_usrreqs,
},
{
	.pr_type =		SOCK_STREAM,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_TCP,
	.pr_flags =		PR_CONNREQUIRED|PR_IMPLOPCL|PR_WANTRCVD|PR_LISTEN,
	.pr_input =		tcp6_input,
	.pr_ctlinput =		tcp6_ctlinput,
	.pr_ctloutput =		tcp_ctloutput,
#ifndef INET	/* don't call initialization and timeout routines twice */
	.pr_init =		tcp_init,
	.pr_slowtimo =		tcp_slowtimo,
#endif
	.pr_drain =		tcp_drain,
	.pr_usrreqs =		&tcp6_usrreqs,
},
#ifdef SCTP
{
	.pr_type =		SOCK_SEQPACKET,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_SCTP,
	.pr_flags =		PR_WANTRCVD|PR_LASTHDR,
	.pr_input =		sctp6_input,
	.pr_ctlinput =		sctp6_ctlinput,
	.pr_ctloutput =	sctp_ctloutput,
	.pr_drain =		sctp_drain,
#ifndef INET	/* Do not call initialization twice. */
	.pr_init =		sctp_init,
#endif
	.pr_usrreqs =		&sctp6_usrreqs
},
{
	.pr_type =		SOCK_STREAM,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_SCTP,
	.pr_flags =		PR_CONNREQUIRED|PR_WANTRCVD|PR_LASTHDR,
	.pr_input =		sctp6_input,
	.pr_ctlinput =		sctp6_ctlinput,
	.pr_ctloutput =		sctp_ctloutput,
	.pr_drain =		sctp_drain,
	.pr_usrreqs =		&sctp6_usrreqs
},
#endif /* SCTP */
{
	.pr_type =		SOCK_DGRAM,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_UDPLITE,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		udp6_input,
	.pr_ctlinput =		udplite6_ctlinput,
	.pr_ctloutput =		udp_ctloutput,
#ifndef INET	/* Do not call initialization twice. */
	.pr_init =		udplite_init,
#endif
	.pr_usrreqs =		&udp6_usrreqs,
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_RAW,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		rip6_input,
	.pr_output =		rip6_output,
	.pr_ctlinput =		rip6_ctlinput,
	.pr_ctloutput =		rip6_ctloutput,
#ifndef INET	/* Do not call initialization twice. */
	.pr_init =		rip_init,
#endif
	.pr_usrreqs =		&rip6_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_ICMPV6,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		icmp6_input,
	.pr_output =		rip6_output,
	.pr_ctlinput =		rip6_ctlinput,
	.pr_ctloutput =		rip6_ctloutput,
	.pr_fasttimo =		icmp6_fasttimo,
	.pr_slowtimo =		icmp6_slowtimo,
	.pr_usrreqs =		&rip6_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_DSTOPTS,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		dest6_input,
	.pr_usrreqs =		&nousrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_ROUTING,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		route6_input,
	.pr_usrreqs =		&nousrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_FRAGMENT,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		frag6_input,
	.pr_usrreqs =		&nousrreqs
},
#ifdef INET
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_IPV4,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		encap6_input,
	.pr_output =		rip6_output,
	.pr_ctloutput =		rip6_ctloutput,
	.pr_usrreqs =		&rip6_usrreqs
},
#endif /* INET */
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_IPV6,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		encap6_input,
	.pr_output =		rip6_output,
	.pr_ctloutput =		rip6_ctloutput,
	.pr_usrreqs =		&rip6_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_GRE,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		encap6_input,
	.pr_output =		rip6_output,
	.pr_ctloutput =		rip6_ctloutput,
	.pr_usrreqs =		&rip6_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_PIM,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		encap6_input,
	.pr_output =		rip6_output,
	.pr_ctloutput =		rip6_ctloutput,
	.pr_usrreqs =		&rip6_usrreqs
},
/* Spacer n-times for loadable protocols. */
IP6PROTOSPACER,
IP6PROTOSPACER,
IP6PROTOSPACER,
IP6PROTOSPACER,
IP6PROTOSPACER,
IP6PROTOSPACER,
IP6PROTOSPACER,
IP6PROTOSPACER,
/* raw wildcard */
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inet6domain,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		rip6_input,
	.pr_output =		rip6_output,
	.pr_ctloutput =		rip6_ctloutput,
	.pr_usrreqs =		&rip6_usrreqs
},
};

extern int in6_inithead(void **, int);
#ifdef VIMAGE
extern int in6_detachhead(void **, int);
#endif

struct domain inet6domain = {
	.dom_family =		AF_INET6,
	.dom_name =		"internet6",
	.dom_protosw =		(struct protosw *)inet6sw,
	.dom_protoswNPROTOSW =	(struct protosw *)&inet6sw[nitems(inet6sw)],
#ifdef RADIX_MPATH
	.dom_rtattach =		rn6_mpath_inithead,
#else
	.dom_rtattach =		in6_inithead,
#endif
#ifdef VIMAGE
	.dom_rtdetach =		in6_detachhead,
#endif
	.dom_ifattach =		in6_domifattach,
	.dom_ifdetach =		in6_domifdetach,
	.dom_ifmtu    =		in6_domifmtu
};

VNET_DOMAIN_SET(inet6);

/*
 * Internet configuration info
 */
#ifndef	IPV6FORWARDING
#ifdef GATEWAY6
#define	IPV6FORWARDING	1	/* forward IP6 packets not for us */
#else
#define	IPV6FORWARDING	0	/* don't forward IP6 packets not for us */
#endif /* GATEWAY6 */
#endif /* !IPV6FORWARDING */

#ifndef	IPV6_SENDREDIRECTS
#define	IPV6_SENDREDIRECTS	1
#endif

VNET_DEFINE(int, ip6_forwarding) = IPV6FORWARDING;	/* act as router? */
VNET_DEFINE(int, ip6_sendredirects) = IPV6_SENDREDIRECTS;
VNET_DEFINE(int, ip6_defhlim) = IPV6_DEFHLIM;
VNET_DEFINE(int, ip6_defmcasthlim) = IPV6_DEFAULT_MULTICAST_HOPS;
VNET_DEFINE(int, ip6_accept_rtadv) = 0;
VNET_DEFINE(int, ip6_no_radr) = 0;
VNET_DEFINE(int, ip6_norbit_raif) = 0;
VNET_DEFINE(int, ip6_rfc6204w3) = 0;
VNET_DEFINE(int, ip6_maxfragpackets);	/* initialized in frag6.c:frag6_init() */
int ip6_maxfrags;		/* initialized in frag6.c:frag6_init() */
VNET_DEFINE(int, ip6_maxfragbucketsize);/* initialized in frag6.c:frag6_init() */
VNET_DEFINE(int, ip6_maxfragsperpacket); /* initialized in frag6.c:frag6_init() */
VNET_DEFINE(int, ip6_log_interval) = 5;
VNET_DEFINE(int, ip6_hdrnestlimit) = 15;/* How many header options will we
					 * process? */
VNET_DEFINE(int, ip6_dad_count) = 1;	/* DupAddrDetectionTransmits */
VNET_DEFINE(int, ip6_auto_flowlabel) = 1;
VNET_DEFINE(int, ip6_use_deprecated) = 1;/* allow deprecated addr
					 * (RFC2462 5.5.4) */
VNET_DEFINE(int, ip6_rr_prune) = 5;	/* router renumbering prefix
					 * walk list every 5 sec. */
VNET_DEFINE(int, ip6_mcast_pmtu) = 0;	/* enable pMTU discovery for multicast? */
VNET_DEFINE(int, ip6_v6only) = 1;

VNET_DEFINE(time_t, ip6_log_time) = (time_t)0L;
#ifdef IPSTEALTH
VNET_DEFINE(int, ip6stealth) = 0;
#endif
VNET_DEFINE(int, nd6_onlink_ns_rfc4861) = 0;/* allow 'on-link' nd6 NS
					     * (RFC 4861) */

/* icmp6 */
/*
 * BSDI4 defines these variables in in_proto.c...
 * XXX: what if we don't define INET? Should we define pmtu6_expire
 * or so? (jinmei@kame.net 19990310)
 */
VNET_DEFINE(int, pmtu_expire) = 60*10;
VNET_DEFINE(int, pmtu_probe) = 60*2;

/* ICMPV6 parameters */
VNET_DEFINE(int, icmp6_rediraccept) = 1;/* accept and process redirects */
VNET_DEFINE(int, icmp6_redirtimeout) = 10 * 60;	/* 10 minutes */
VNET_DEFINE(int, icmp6errppslim) = 100;		/* 100pps */
/* control how to respond to NI queries */
VNET_DEFINE(int, icmp6_nodeinfo) =
    (ICMP6_NODEINFO_FQDNOK|ICMP6_NODEINFO_NODEADDROK);
VNET_DEFINE(int, icmp6_nodeinfo_oldmcprefix) = 1;

/*
 * sysctl related items.
 */
SYSCTL_NODE(_net,	PF_INET6,	inet6,	CTLFLAG_RW,	0,
	"Internet6 Family");

/* net.inet6 */
SYSCTL_NODE(_net_inet6,	IPPROTO_IPV6,	ip6,	CTLFLAG_RW, 0,	"IP6");
SYSCTL_NODE(_net_inet6,	IPPROTO_ICMPV6,	icmp6,	CTLFLAG_RW, 0,	"ICMP6");
SYSCTL_NODE(_net_inet6,	IPPROTO_UDP,	udp6,	CTLFLAG_RW, 0,	"UDP6");
SYSCTL_NODE(_net_inet6,	IPPROTO_TCP,	tcp6,	CTLFLAG_RW, 0,	"TCP6");
#ifdef SCTP
SYSCTL_NODE(_net_inet6,	IPPROTO_SCTP,	sctp6,	CTLFLAG_RW, 0,	"SCTP6");
#endif
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
SYSCTL_NODE(_net_inet6,	IPPROTO_ESP,	ipsec6,	CTLFLAG_RW, 0,	"IPSEC6");
#endif /* IPSEC */

/* net.inet6.ip6 */
static int
sysctl_ip6_temppltime(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = V_ip6_temp_preferred_lifetime;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || !req->newptr)
		return (error);
	if (val < V_ip6_desync_factor + V_ip6_temp_regen_advance)
		return (EINVAL);
	V_ip6_temp_preferred_lifetime = val;
	return (0);
}

static int
sysctl_ip6_tempvltime(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = V_ip6_temp_valid_lifetime;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || !req->newptr)
		return (error);
	if (val < V_ip6_temp_preferred_lifetime)
		return (EINVAL);
	V_ip6_temp_valid_lifetime = val;
	return (0);
}

static int
sysctl_ip6_maxfragpackets(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = V_ip6_maxfragpackets;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || !req->newptr)
		return (error);
	V_ip6_maxfragpackets = val;
	frag6_set_bucketsize();
	return (0);
}

SYSCTL_INT(_net_inet6_ip6, IPV6CTL_FORWARDING, forwarding,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_forwarding), 0,
	"Enable forwarding of IPv6 packets between interfaces");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_SENDREDIRECTS, redirect,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_sendredirects), 0,
	"Send ICMPv6 redirects for unforwardable IPv6 packets");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_DEFHLIM, hlim,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_defhlim), 0,
	"Default hop limit to use for outgoing IPv6 packets");
SYSCTL_VNET_PCPUSTAT(_net_inet6_ip6, IPV6CTL_STATS, stats, struct ip6stat,
	ip6stat,
	"IP6 statistics (struct ip6stat, netinet6/ip6_var.h)");
SYSCTL_PROC(_net_inet6_ip6, IPV6CTL_MAXFRAGPACKETS, maxfragpackets,
	CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW, NULL, 0,
	sysctl_ip6_maxfragpackets, "I",
	"Default maximum number of outstanding fragmented IPv6 packets. "
	"A value of 0 means no fragmented packets will be accepted, while a "
	"a value of -1 means no limit");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_ACCEPT_RTADV, accept_rtadv,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_accept_rtadv), 0,
	"Default value of per-interface flag for accepting ICMPv6 RA messages");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_NO_RADR, no_radr,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_no_radr), 0,
	"Default value of per-interface flag to control whether routers "
	"sending ICMPv6 RA messages on that interface are added into the "
	"default router list");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_NORBIT_RAIF, norbit_raif,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_norbit_raif), 0,
	"Always set clear the R flag in ICMPv6 NA messages when accepting RA "
	"on the interface");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_RFC6204W3, rfc6204w3,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_rfc6204w3), 0,
	"Accept the default router list from ICMPv6 RA messages even "
	"when packet forwarding is enabled");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_LOG_INTERVAL, log_interval,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_log_interval), 0,
	"Frequency in seconds at which to log IPv6 forwarding errors");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_HDRNESTLIMIT, hdrnestlimit,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_hdrnestlimit), 0,
	"Default maximum number of IPv6 extension headers permitted on "
	"incoming IPv6 packets, 0 for no artificial limit");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_DAD_COUNT, dad_count,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_dad_count), 0,
	"Number of ICMPv6 NS messages sent during duplicate address detection");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_AUTO_FLOWLABEL, auto_flowlabel,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_auto_flowlabel), 0,
	"Provide an IPv6 flowlabel in outbound packets");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_DEFMCASTHLIM, defmcasthlim,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_defmcasthlim), 0,
	"Default hop limit for IPv6 multicast packets originating from this "
	"node");
SYSCTL_STRING(_net_inet6_ip6, IPV6CTL_KAME_VERSION, kame_version,
	CTLFLAG_RD, __KAME_VERSION, 0,
	"KAME version string");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_USE_DEPRECATED, use_deprecated,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_use_deprecated), 0,
	"Allow the use of addresses whose preferred lifetimes have expired");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_RR_PRUNE, rr_prune,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_rr_prune), 0,
	""); /* XXX unused */
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_USETEMPADDR, use_tempaddr,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_use_tempaddr), 0,
	"Create RFC3041 temporary addresses for autoconfigured addresses");
SYSCTL_PROC(_net_inet6_ip6, IPV6CTL_TEMPPLTIME, temppltime,
	CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW,
	NULL, 0, sysctl_ip6_temppltime, "I",
	"Maximum preferred lifetime for temporary addresses");
SYSCTL_PROC(_net_inet6_ip6, IPV6CTL_TEMPVLTIME, tempvltime,
	CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW,
	NULL, 0, sysctl_ip6_tempvltime, "I",
	"Maximum valid lifetime for temporary addresses");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_V6ONLY, v6only,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_v6only), 0,
	"Restrict AF_INET6 sockets to IPv6 addresses only");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_AUTO_LINKLOCAL, auto_linklocal,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_auto_linklocal), 0,
	"Default value of per-interface flag for automatically adding an IPv6 "
	"link-local address to interfaces when attached");
SYSCTL_VNET_PCPUSTAT(_net_inet6_ip6, IPV6CTL_RIP6STATS, rip6stats,
	struct rip6stat, rip6stat,
	"Raw IP6 statistics (struct rip6stat, netinet6/raw_ip6.h)");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_PREFER_TEMPADDR, prefer_tempaddr,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_prefer_tempaddr), 0,
	"Prefer RFC3041 temporary addresses in source address selection");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_USE_DEFAULTZONE, use_defaultzone,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_use_defzone), 0,
	"Use the default scope zone when none is specified");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_MAXFRAGS, maxfrags,
	CTLFLAG_RW, &ip6_maxfrags, 0,
	"Maximum allowed number of outstanding IPv6 packet fragments. "
	"A value of 0 means no fragmented packets will be accepted, while a "
	"a value of -1 means no limit");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_MAXFRAGBUCKETSIZE, maxfragbucketsize,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_maxfragbucketsize), 0,
	"Maximum number of reassembly queues per hash bucket");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_MAXFRAGSPERPACKET, maxfragsperpacket,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_maxfragsperpacket), 0,
	"Maximum allowed number of fragments per packet");
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_MCAST_PMTU, mcast_pmtu,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_mcast_pmtu), 0,
	"Enable path MTU discovery for multicast packets");
#ifdef IPSTEALTH
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_STEALTH, stealth, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(ip6stealth), 0,
	"Forward IPv6 packets without decrementing their TTL");
#endif

/* net.inet6.icmp6 */
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_REDIRACCEPT, rediraccept,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(icmp6_rediraccept), 0,
	"Accept ICMPv6 redirect messages");
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_REDIRTIMEOUT, redirtimeout,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(icmp6_redirtimeout), 0,
	""); /* XXX unused */
SYSCTL_VNET_PCPUSTAT(_net_inet6_icmp6, ICMPV6CTL_STATS, stats,
	struct icmp6stat, icmp6stat,
	"ICMPv6 statistics (struct icmp6stat, netinet/icmp6.h)");
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_ND6_PRUNE, nd6_prune,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(nd6_prune), 0,
	"Frequency in seconds of checks for expired prefixes and routers");
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_ND6_DELAY, nd6_delay,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(nd6_delay), 0,
	"Delay in seconds before probing for reachability");
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_ND6_UMAXTRIES, nd6_umaxtries,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(nd6_umaxtries), 0,
	"Number of ICMPv6 NS messages sent during reachability detection");
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_ND6_MMAXTRIES, nd6_mmaxtries,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(nd6_mmaxtries), 0,
	"Number of ICMPv6 NS messages sent during address resolution");
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_ND6_USELOOPBACK, nd6_useloopback,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(nd6_useloopback), 0,
	"Create a loopback route when configuring an IPv6 address");
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_NODEINFO, nodeinfo,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(icmp6_nodeinfo), 0,
	"Mask of enabled RF4620 node information query types");
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_NODEINFO_OLDMCPREFIX,
	nodeinfo_oldmcprefix, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(icmp6_nodeinfo_oldmcprefix), 0,
	"Join old IPv6 NI group address in draft-ietf-ipngwg-icmp-name-lookup "
	"for compatibility with KAME implementation");
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_ERRPPSLIMIT, errppslimit,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(icmp6errppslim), 0,
	"Maximum number of ICMPv6 error messages per second");
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_ND6_MAXNUDHINT, nd6_maxnudhint,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(nd6_maxnudhint), 0,
	""); /* XXX unused */
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_ND6_DEBUG, nd6_debug,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(nd6_debug), 0,
	"Log NDP debug messages");
SYSCTL_INT(_net_inet6_icmp6, ICMPV6CTL_ND6_ONLINKNSRFC4861,
	nd6_onlink_ns_rfc4861, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(nd6_onlink_ns_rfc4861), 0,
	"Accept 'on-link' ICMPv6 NS messages in compliance with RFC 4861");
#ifdef EXPERIMENTAL
SYSCTL_INT(_net_inet6_icmp6, OID_AUTO,
	nd6_ignore_ipv6_only_ra, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(nd6_ignore_ipv6_only_ra), 0,
	"Ignore the 'IPv6-Only flag' in RA messages in compliance with "
	"draft-ietf-6man-ipv6only-flag");
#endif
