/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)in_proto.c	8.2 (Berkeley) 2/9/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_mrouting.h"
#include "opt_ipsec.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_sctp.h"
#include "opt_mpath.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/domain.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

/*
 * While this file provides the domain and protocol switch tables for IPv4, it
 * also provides the sysctl node declarations for net.inet.* often shared with
 * IPv6 for common features or by upper layer protocols.  In case of no IPv4
 * support compile out everything but these sysctl nodes.
 */
#ifdef INET
#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#ifdef RADIX_MPATH
#include <net/radix_mpath.h>
#endif
#include <net/vnet.h>
#endif /* INET */

#if defined(INET) || defined(INET6)
#include <netinet/in.h>
#endif

#ifdef INET
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/igmp_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/ip_encap.h>

/*
 * TCP/IP protocol family: IP, ICMP, UDP, TCP.
 */

static struct pr_usrreqs nousrreqs;

#ifdef SCTP
#include <netinet/in_pcb.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctp.h>
#include <netinet/sctp_var.h>
#endif /* SCTP */

FEATURE(inet, "Internet Protocol version 4");

extern	struct domain inetdomain;

/* Spacer for loadable protocols. */
#define IPPROTOSPACER   			\
{						\
	.pr_domain =		&inetdomain,	\
	.pr_protocol =		PROTO_SPACER,	\
	.pr_usrreqs =		&nousrreqs	\
}

struct protosw inetsw[] = {
{
	.pr_type =		0,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_IP,
	.pr_init =		ip_init,
	.pr_slowtimo =		ip_slowtimo,
	.pr_drain =		ip_drain,
	.pr_usrreqs =		&nousrreqs
},
{
	.pr_type =		SOCK_DGRAM,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_UDP,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		udp_input,
	.pr_ctlinput =		udp_ctlinput,
	.pr_ctloutput =		udp_ctloutput,
	.pr_init =		udp_init,
	.pr_usrreqs =		&udp_usrreqs
},
{
	.pr_type =		SOCK_STREAM,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_TCP,
	.pr_flags =		PR_CONNREQUIRED|PR_IMPLOPCL|PR_WANTRCVD,
	.pr_input =		tcp_input,
	.pr_ctlinput =		tcp_ctlinput,
	.pr_ctloutput =		tcp_ctloutput,
	.pr_init =		tcp_init,
	.pr_slowtimo =		tcp_slowtimo,
	.pr_drain =		tcp_drain,
	.pr_usrreqs =		&tcp_usrreqs
},
#ifdef SCTP
{ 
	.pr_type =		SOCK_SEQPACKET,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_SCTP,
	.pr_flags =		PR_WANTRCVD|PR_LASTHDR,
	.pr_input =		sctp_input,
	.pr_ctlinput =		sctp_ctlinput,
	.pr_ctloutput =		sctp_ctloutput,
	.pr_init =		sctp_init,
	.pr_drain =		sctp_drain,
	.pr_usrreqs =		&sctp_usrreqs
},
{ 
	.pr_type =		SOCK_STREAM,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_SCTP,
	.pr_flags =		PR_CONNREQUIRED|PR_WANTRCVD|PR_LASTHDR,
	.pr_input =		sctp_input,
	.pr_ctlinput =		sctp_ctlinput,
	.pr_ctloutput =		sctp_ctloutput,
	.pr_drain =		sctp_drain,
	.pr_usrreqs =		&sctp_usrreqs
},
#endif /* SCTP */
{
	.pr_type =		SOCK_DGRAM,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_UDPLITE,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		udp_input,
	.pr_ctlinput =		udplite_ctlinput,
	.pr_ctloutput =		udp_ctloutput,
	.pr_init =		udplite_init,
	.pr_usrreqs =		&udp_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_RAW,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		rip_input,
	.pr_ctlinput =		rip_ctlinput,
	.pr_ctloutput =		rip_ctloutput,
	.pr_usrreqs =		&rip_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_ICMP,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		icmp_input,
	.pr_ctloutput =		rip_ctloutput,
	.pr_usrreqs =		&rip_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_IGMP,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		igmp_input,
	.pr_ctloutput =		rip_ctloutput,
	.pr_fasttimo =		igmp_fasttimo,
	.pr_slowtimo =		igmp_slowtimo,
	.pr_usrreqs =		&rip_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_RSVP,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		rsvp_input,
	.pr_ctloutput =		rip_ctloutput,
	.pr_usrreqs =		&rip_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_IPV4,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		encap4_input,
	.pr_ctloutput =		rip_ctloutput,
	.pr_usrreqs =		&rip_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_MOBILE,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		encap4_input,
	.pr_ctloutput =		rip_ctloutput,
	.pr_usrreqs =		&rip_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_ETHERIP,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		encap4_input,
	.pr_ctloutput =		rip_ctloutput,
	.pr_usrreqs =		&rip_usrreqs
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_GRE,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		encap4_input,
	.pr_ctloutput =		rip_ctloutput,
	.pr_usrreqs =		&rip_usrreqs
},
# ifdef INET6
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_IPV6,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		encap4_input,
	.pr_ctloutput =		rip_ctloutput,
	.pr_usrreqs =		&rip_usrreqs
},
#endif
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_PIM,
	.pr_flags =		PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input =		encap4_input,
	.pr_ctloutput =		rip_ctloutput,
	.pr_usrreqs =		&rip_usrreqs
},
/* Spacer n-times for loadable protocols. */
IPPROTOSPACER,
IPPROTOSPACER,
IPPROTOSPACER,
IPPROTOSPACER,
IPPROTOSPACER,
IPPROTOSPACER,
IPPROTOSPACER,
IPPROTOSPACER,
/* raw wildcard */
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		rip_input,
	.pr_ctloutput =		rip_ctloutput,
	.pr_init =		rip_init,
	.pr_usrreqs =		&rip_usrreqs
},
};

extern int in_inithead(void **, int);
extern int in_detachhead(void **, int);

struct domain inetdomain = {
	.dom_family =		AF_INET,
	.dom_name =		"internet",
	.dom_protosw =		inetsw,
	.dom_protoswNPROTOSW =	&inetsw[nitems(inetsw)],
#ifdef RADIX_MPATH
	.dom_rtattach =		rn4_mpath_inithead,
#else
	.dom_rtattach =		in_inithead,
#endif
#ifdef VIMAGE
	.dom_rtdetach =		in_detachhead,
#endif
	.dom_ifattach =		in_domifattach,
	.dom_ifdetach =		in_domifdetach
};

VNET_DOMAIN_SET(inet);
#endif /* INET */

SYSCTL_NODE(_net,      PF_INET,		inet,	CTLFLAG_RW, 0,
	"Internet Family");

SYSCTL_NODE(_net_inet, IPPROTO_IP,	ip,	CTLFLAG_RW, 0,	"IP");
SYSCTL_NODE(_net_inet, IPPROTO_ICMP,	icmp,	CTLFLAG_RW, 0,	"ICMP");
SYSCTL_NODE(_net_inet, IPPROTO_UDP,	udp,	CTLFLAG_RW, 0,	"UDP");
SYSCTL_NODE(_net_inet, IPPROTO_TCP,	tcp,	CTLFLAG_RW, 0,	"TCP");
#ifdef SCTP
SYSCTL_NODE(_net_inet, IPPROTO_SCTP,	sctp,	CTLFLAG_RW, 0,	"SCTP");
#endif
SYSCTL_NODE(_net_inet, IPPROTO_IGMP,	igmp,	CTLFLAG_RW, 0,	"IGMP");
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
/* XXX no protocol # to use, pick something "reserved" */
SYSCTL_NODE(_net_inet, 253,		ipsec,	CTLFLAG_RW, 0,	"IPSEC");
SYSCTL_NODE(_net_inet, IPPROTO_AH,	ah,	CTLFLAG_RW, 0,	"AH");
SYSCTL_NODE(_net_inet, IPPROTO_ESP,	esp,	CTLFLAG_RW, 0,	"ESP");
SYSCTL_NODE(_net_inet, IPPROTO_IPCOMP,	ipcomp,	CTLFLAG_RW, 0,	"IPCOMP");
SYSCTL_NODE(_net_inet, IPPROTO_IPIP,	ipip,	CTLFLAG_RW, 0,	"IPIP");
#endif /* IPSEC */
SYSCTL_NODE(_net_inet, IPPROTO_RAW,	raw,	CTLFLAG_RW, 0,	"RAW");
SYSCTL_NODE(_net_inet, OID_AUTO,	accf,	CTLFLAG_RW, 0,
    "Accept filters");
