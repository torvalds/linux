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
 *	@(#)ip_icmp.c	8.2 (Berkeley) 1/4/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/rmlock.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_fib.h>
#include <netinet/in_pcb.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_var.h>
#include <netinet/ip_options.h>
#include <netinet/sctp.h>
#include <netinet/tcp.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#include <netinet/icmp_var.h>


#ifdef INET

#include <machine/in_cksum.h>

#include <security/mac/mac_framework.h>
#endif /* INET */

/*
 * ICMP routines: error generation, receive packet processing, and
 * routines to turnaround packets back to the originator, and
 * host table maintenance routines.
 */
VNET_DEFINE_STATIC(int, icmplim) = 200;
#define	V_icmplim			VNET(icmplim)
SYSCTL_INT(_net_inet_icmp, ICMPCTL_ICMPLIM, icmplim, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(icmplim), 0,
	"Maximum number of ICMP responses per second");

VNET_DEFINE_STATIC(int, icmplim_output) = 1;
#define	V_icmplim_output		VNET(icmplim_output)
SYSCTL_INT(_net_inet_icmp, OID_AUTO, icmplim_output, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(icmplim_output), 0,
	"Enable logging of ICMP response rate limiting");

#ifdef INET
VNET_PCPUSTAT_DEFINE(struct icmpstat, icmpstat);
VNET_PCPUSTAT_SYSINIT(icmpstat);
SYSCTL_VNET_PCPUSTAT(_net_inet_icmp, ICMPCTL_STATS, stats, struct icmpstat,
    icmpstat, "ICMP statistics (struct icmpstat, netinet/icmp_var.h)");

#ifdef VIMAGE
VNET_PCPUSTAT_SYSUNINIT(icmpstat);
#endif /* VIMAGE */

VNET_DEFINE_STATIC(int, icmpmaskrepl) = 0;
#define	V_icmpmaskrepl			VNET(icmpmaskrepl)
SYSCTL_INT(_net_inet_icmp, ICMPCTL_MASKREPL, maskrepl, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(icmpmaskrepl), 0,
	"Reply to ICMP Address Mask Request packets");

VNET_DEFINE_STATIC(u_int, icmpmaskfake) = 0;
#define	V_icmpmaskfake			VNET(icmpmaskfake)
SYSCTL_UINT(_net_inet_icmp, OID_AUTO, maskfake, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(icmpmaskfake), 0,
	"Fake reply to ICMP Address Mask Request packets");

VNET_DEFINE(int, drop_redirect) = 0;
#define	V_drop_redirect			VNET(drop_redirect)
SYSCTL_INT(_net_inet_icmp, OID_AUTO, drop_redirect, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(drop_redirect), 0,
	"Ignore ICMP redirects");

VNET_DEFINE_STATIC(int, log_redirect) = 0;
#define	V_log_redirect			VNET(log_redirect)
SYSCTL_INT(_net_inet_icmp, OID_AUTO, log_redirect, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(log_redirect), 0,
	"Log ICMP redirects to the console");

VNET_DEFINE_STATIC(char, reply_src[IFNAMSIZ]);
#define	V_reply_src			VNET(reply_src)
SYSCTL_STRING(_net_inet_icmp, OID_AUTO, reply_src, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(reply_src), IFNAMSIZ,
	"ICMP reply source for non-local packets");

VNET_DEFINE_STATIC(int, icmp_rfi) = 0;
#define	V_icmp_rfi			VNET(icmp_rfi)
SYSCTL_INT(_net_inet_icmp, OID_AUTO, reply_from_interface, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(icmp_rfi), 0,
	"ICMP reply from incoming interface for non-local packets");
/* Router requirements RFC 1812 section 4.3.2.3 requires 576 - 28. */
VNET_DEFINE_STATIC(int, icmp_quotelen) = 548;
#define	V_icmp_quotelen			VNET(icmp_quotelen)
SYSCTL_INT(_net_inet_icmp, OID_AUTO, quotelen, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(icmp_quotelen), 0,
	"Number of bytes from original packet to quote in ICMP reply");

VNET_DEFINE_STATIC(int, icmpbmcastecho) = 0;
#define	V_icmpbmcastecho		VNET(icmpbmcastecho)
SYSCTL_INT(_net_inet_icmp, OID_AUTO, bmcastecho, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(icmpbmcastecho), 0,
	"Reply to multicast ICMP Echo Request and Timestamp packets");

VNET_DEFINE_STATIC(int, icmptstamprepl) = 1;
#define	V_icmptstamprepl		VNET(icmptstamprepl)
SYSCTL_INT(_net_inet_icmp, OID_AUTO, tstamprepl, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(icmptstamprepl), 0,
	"Respond to ICMP Timestamp packets");

VNET_DEFINE_STATIC(int, error_keeptags) = 0;
#define	V_error_keeptags		VNET(error_keeptags)
SYSCTL_INT(_net_inet_icmp, OID_AUTO, error_keeptags, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(error_keeptags), 0,
	"ICMP error response keeps copy of mbuf_tags of original packet");

#ifdef ICMPPRINTFS
int	icmpprintfs = 0;
#endif

static void	icmp_reflect(struct mbuf *);
static void	icmp_send(struct mbuf *, struct mbuf *);

extern	struct protosw inetsw[];

/*
 * Kernel module interface for updating icmpstat.  The argument is an index
 * into icmpstat treated as an array of u_long.  While this encodes the
 * general layout of icmpstat into the caller, it doesn't encode its
 * location, so that future changes to add, for example, per-CPU stats
 * support won't cause binary compatibility problems for kernel modules.
 */
void
kmod_icmpstat_inc(int statnum)
{

	counter_u64_add(VNET(icmpstat)[statnum], 1);
}

/*
 * Generate an error packet of type error
 * in response to bad packet ip.
 */
void
icmp_error(struct mbuf *n, int type, int code, uint32_t dest, int mtu)
{
	struct ip *oip, *nip;
	struct icmp *icp;
	struct mbuf *m;
	unsigned icmplen, icmpelen, nlen, oiphlen;

	KASSERT((u_int)type <= ICMP_MAXTYPE, ("%s: illegal ICMP type",
	    __func__));

	if (type != ICMP_REDIRECT)
		ICMPSTAT_INC(icps_error);
	/*
	 * Don't send error:
	 *  if the original packet was encrypted.
	 *  if not the first fragment of message.
	 *  in response to a multicast or broadcast packet.
	 *  if the old packet protocol was an ICMP error message.
	 */
	if (n->m_flags & M_DECRYPTED)
		goto freeit;
	if (n->m_flags & (M_BCAST|M_MCAST))
		goto freeit;

	/* Drop if IP header plus 8 bytes is not contiguous in first mbuf. */
	if (n->m_len < sizeof(struct ip) + ICMP_MINLEN)
		goto freeit;
	oip = mtod(n, struct ip *);
	oiphlen = oip->ip_hl << 2;
	if (n->m_len < oiphlen + ICMP_MINLEN)
		goto freeit;
#ifdef ICMPPRINTFS
	if (icmpprintfs)
		printf("icmp_error(%p, %x, %d)\n", oip, type, code);
#endif
	if (oip->ip_off & htons(~(IP_MF|IP_DF)))
		goto freeit;
	if (oip->ip_p == IPPROTO_ICMP && type != ICMP_REDIRECT &&
	    !ICMP_INFOTYPE(((struct icmp *)((caddr_t)oip +
		oiphlen))->icmp_type)) {
		ICMPSTAT_INC(icps_oldicmp);
		goto freeit;
	}
	/*
	 * Calculate length to quote from original packet and
	 * prevent the ICMP mbuf from overflowing.
	 * Unfortunately this is non-trivial since ip_forward()
	 * sends us truncated packets.
	 */
	nlen = m_length(n, NULL);
	if (oip->ip_p == IPPROTO_TCP) {
		struct tcphdr *th;
		int tcphlen;

		if (oiphlen + sizeof(struct tcphdr) > n->m_len &&
		    n->m_next == NULL)
			goto stdreply;
		if (n->m_len < oiphlen + sizeof(struct tcphdr) &&
		    (n = m_pullup(n, oiphlen + sizeof(struct tcphdr))) == NULL)
			goto freeit;
		oip = mtod(n, struct ip *);
		th = mtodo(n, oiphlen);
		tcphlen = th->th_off << 2;
		if (tcphlen < sizeof(struct tcphdr))
			goto freeit;
		if (ntohs(oip->ip_len) < oiphlen + tcphlen)
			goto freeit;
		if (oiphlen + tcphlen > n->m_len && n->m_next == NULL)
			goto stdreply;
		if (n->m_len < oiphlen + tcphlen &&
		    (n = m_pullup(n, oiphlen + tcphlen)) == NULL)
			goto freeit;
		oip = mtod(n, struct ip *);
		icmpelen = max(tcphlen, min(V_icmp_quotelen,
		    ntohs(oip->ip_len) - oiphlen));
	} else if (oip->ip_p == IPPROTO_SCTP) {
		struct sctphdr *sh;
		struct sctp_chunkhdr *ch;

		if (ntohs(oip->ip_len) < oiphlen + sizeof(struct sctphdr))
			goto stdreply;
		if (oiphlen + sizeof(struct sctphdr) > n->m_len &&
		    n->m_next == NULL)
			goto stdreply;
		if (n->m_len < oiphlen + sizeof(struct sctphdr) &&
		    (n = m_pullup(n, oiphlen + sizeof(struct sctphdr))) == NULL)
			goto freeit;
		oip = mtod(n, struct ip *);
		icmpelen = max(sizeof(struct sctphdr),
		    min(V_icmp_quotelen, ntohs(oip->ip_len) - oiphlen));
		sh = mtodo(n, oiphlen);
		if (ntohl(sh->v_tag) == 0 &&
		    ntohs(oip->ip_len) >= oiphlen +
		    sizeof(struct sctphdr) + 8 &&
		    (n->m_len >= oiphlen + sizeof(struct sctphdr) + 8 ||
		     n->m_next != NULL)) {
			if (n->m_len < oiphlen + sizeof(struct sctphdr) + 8 &&
			    (n = m_pullup(n, oiphlen +
			    sizeof(struct sctphdr) + 8)) == NULL)
				goto freeit;
			oip = mtod(n, struct ip *);
			sh = mtodo(n, oiphlen);
			ch = (struct sctp_chunkhdr *)(sh + 1);
			if (ch->chunk_type == SCTP_INITIATION) {
				icmpelen = max(sizeof(struct sctphdr) + 8,
				    min(V_icmp_quotelen, ntohs(oip->ip_len) -
				    oiphlen));
			}
		}
	} else
stdreply:	icmpelen = max(8, min(V_icmp_quotelen, ntohs(oip->ip_len) -
		    oiphlen));

	icmplen = min(oiphlen + icmpelen, nlen);
	if (icmplen < sizeof(struct ip))
		goto freeit;

	if (MHLEN > sizeof(struct ip) + ICMP_MINLEN + icmplen)
		m = m_gethdr(M_NOWAIT, MT_DATA);
	else
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		goto freeit;
#ifdef MAC
	mac_netinet_icmp_reply(n, m);
#endif
	icmplen = min(icmplen, M_TRAILINGSPACE(m) -
	    sizeof(struct ip) - ICMP_MINLEN);
	m_align(m, sizeof(struct ip) + ICMP_MINLEN + icmplen);
	m->m_data += sizeof(struct ip);
	m->m_len = ICMP_MINLEN + icmplen;

	/* XXX MRT  make the outgoing packet use the same FIB
	 * that was associated with the incoming packet
	 */
	M_SETFIB(m, M_GETFIB(n));
	icp = mtod(m, struct icmp *);
	ICMPSTAT_INC(icps_outhist[type]);
	icp->icmp_type = type;
	if (type == ICMP_REDIRECT)
		icp->icmp_gwaddr.s_addr = dest;
	else {
		icp->icmp_void = 0;
		/*
		 * The following assignments assume an overlay with the
		 * just zeroed icmp_void field.
		 */
		if (type == ICMP_PARAMPROB) {
			icp->icmp_pptr = code;
			code = 0;
		} else if (type == ICMP_UNREACH &&
			code == ICMP_UNREACH_NEEDFRAG && mtu) {
			icp->icmp_nextmtu = htons(mtu);
		}
	}
	icp->icmp_code = code;

	/*
	 * Copy the quotation into ICMP message and
	 * convert quoted IP header back to network representation.
	 */
	m_copydata(n, 0, icmplen, (caddr_t)&icp->icmp_ip);
	nip = &icp->icmp_ip;

	/*
	 * Set up ICMP message mbuf and copy old IP header (without options
	 * in front of ICMP message.
	 * If the original mbuf was meant to bypass the firewall, the error
	 * reply should bypass as well.
	 */
	m->m_flags |= n->m_flags & M_SKIP_FIREWALL;
	KASSERT(M_LEADINGSPACE(m) >= sizeof(struct ip),
	    ("insufficient space for ip header"));
	m->m_data -= sizeof(struct ip);
	m->m_len += sizeof(struct ip);
	m->m_pkthdr.len = m->m_len;
	m->m_pkthdr.rcvif = n->m_pkthdr.rcvif;
	nip = mtod(m, struct ip *);
	bcopy((caddr_t)oip, (caddr_t)nip, sizeof(struct ip));
	nip->ip_len = htons(m->m_len);
	nip->ip_v = IPVERSION;
	nip->ip_hl = 5;
	nip->ip_p = IPPROTO_ICMP;
	nip->ip_tos = 0;
	nip->ip_off = 0;

	if (V_error_keeptags)
		m_tag_copy_chain(m, n, M_NOWAIT);

	icmp_reflect(m);

freeit:
	m_freem(n);
}

/*
 * Process a received ICMP message.
 */
int
icmp_input(struct mbuf **mp, int *offp, int proto)
{
	struct epoch_tracker et;
	struct icmp *icp;
	struct in_ifaddr *ia;
	struct mbuf *m = *mp;
	struct ip *ip = mtod(m, struct ip *);
	struct sockaddr_in icmpsrc, icmpdst, icmpgw;
	int hlen = *offp;
	int icmplen = ntohs(ip->ip_len) - *offp;
	int i, code;
	void (*ctlfunc)(int, struct sockaddr *, void *);
	int fibnum;

	*mp = NULL;

	/*
	 * Locate icmp structure in mbuf, and check
	 * that not corrupted and of at least minimum length.
	 */
#ifdef ICMPPRINTFS
	if (icmpprintfs) {
		char srcbuf[INET_ADDRSTRLEN];
		char dstbuf[INET_ADDRSTRLEN];

		printf("icmp_input from %s to %s, len %d\n",
		    inet_ntoa_r(ip->ip_src, srcbuf),
		    inet_ntoa_r(ip->ip_dst, dstbuf), icmplen);
	}
#endif
	NET_EPOCH_ENTER(et);
	if (icmplen < ICMP_MINLEN) {
		ICMPSTAT_INC(icps_tooshort);
		goto freeit;
	}
	i = hlen + min(icmplen, ICMP_ADVLENMIN);
	if (m->m_len < i && (m = m_pullup(m, i)) == NULL)  {
		ICMPSTAT_INC(icps_tooshort);
		NET_EPOCH_EXIT(et);
		return (IPPROTO_DONE);
	}
	ip = mtod(m, struct ip *);
	m->m_len -= hlen;
	m->m_data += hlen;
	icp = mtod(m, struct icmp *);
	if (in_cksum(m, icmplen)) {
		ICMPSTAT_INC(icps_checksum);
		goto freeit;
	}
	m->m_len += hlen;
	m->m_data -= hlen;

#ifdef ICMPPRINTFS
	if (icmpprintfs)
		printf("icmp_input, type %d code %d\n", icp->icmp_type,
		    icp->icmp_code);
#endif

	/*
	 * Message type specific processing.
	 */
	if (icp->icmp_type > ICMP_MAXTYPE)
		goto raw;

	/* Initialize */
	bzero(&icmpsrc, sizeof(icmpsrc));
	icmpsrc.sin_len = sizeof(struct sockaddr_in);
	icmpsrc.sin_family = AF_INET;
	bzero(&icmpdst, sizeof(icmpdst));
	icmpdst.sin_len = sizeof(struct sockaddr_in);
	icmpdst.sin_family = AF_INET;
	bzero(&icmpgw, sizeof(icmpgw));
	icmpgw.sin_len = sizeof(struct sockaddr_in);
	icmpgw.sin_family = AF_INET;

	ICMPSTAT_INC(icps_inhist[icp->icmp_type]);
	code = icp->icmp_code;
	switch (icp->icmp_type) {

	case ICMP_UNREACH:
		switch (code) {
			case ICMP_UNREACH_NET:
			case ICMP_UNREACH_HOST:
			case ICMP_UNREACH_SRCFAIL:
			case ICMP_UNREACH_NET_UNKNOWN:
			case ICMP_UNREACH_HOST_UNKNOWN:
			case ICMP_UNREACH_ISOLATED:
			case ICMP_UNREACH_TOSNET:
			case ICMP_UNREACH_TOSHOST:
			case ICMP_UNREACH_HOST_PRECEDENCE:
			case ICMP_UNREACH_PRECEDENCE_CUTOFF:
				code = PRC_UNREACH_NET;
				break;

			case ICMP_UNREACH_NEEDFRAG:
				code = PRC_MSGSIZE;
				break;

			/*
			 * RFC 1122, Sections 3.2.2.1 and 4.2.3.9.
			 * Treat subcodes 2,3 as immediate RST
			 */
			case ICMP_UNREACH_PROTOCOL:
				code = PRC_UNREACH_PROTOCOL;
				break;
			case ICMP_UNREACH_PORT:
				code = PRC_UNREACH_PORT;
				break;

			case ICMP_UNREACH_NET_PROHIB:
			case ICMP_UNREACH_HOST_PROHIB:
			case ICMP_UNREACH_FILTER_PROHIB:
				code = PRC_UNREACH_ADMIN_PROHIB;
				break;

			default:
				goto badcode;
		}
		goto deliver;

	case ICMP_TIMXCEED:
		if (code > 1)
			goto badcode;
		code += PRC_TIMXCEED_INTRANS;
		goto deliver;

	case ICMP_PARAMPROB:
		if (code > 1)
			goto badcode;
		code = PRC_PARAMPROB;
	deliver:
		/*
		 * Problem with datagram; advise higher level routines.
		 */
		if (icmplen < ICMP_ADVLENMIN || icmplen < ICMP_ADVLEN(icp) ||
		    icp->icmp_ip.ip_hl < (sizeof(struct ip) >> 2)) {
			ICMPSTAT_INC(icps_badlen);
			goto freeit;
		}
		/* Discard ICMP's in response to multicast packets */
		if (IN_MULTICAST(ntohl(icp->icmp_ip.ip_dst.s_addr)))
			goto badcode;
#ifdef ICMPPRINTFS
		if (icmpprintfs)
			printf("deliver to protocol %d\n", icp->icmp_ip.ip_p);
#endif
		icmpsrc.sin_addr = icp->icmp_ip.ip_dst;
		/*
		 * XXX if the packet contains [IPv4 AH TCP], we can't make a
		 * notification to TCP layer.
		 */
		i = sizeof(struct ip) + min(icmplen, ICMP_ADVLENPREF(icp));
		ip_stripoptions(m);
		if (m->m_len < i && (m = m_pullup(m, i)) == NULL) {
			/* This should actually not happen */
			ICMPSTAT_INC(icps_tooshort);
			NET_EPOCH_EXIT(et);
			return (IPPROTO_DONE);
		}
		ip = mtod(m, struct ip *);
		icp = (struct icmp *)(ip + 1);
		/*
		 * The upper layer handler can rely on:
		 * - The outer IP header has no options.
		 * - The outer IP header, the ICMP header, the inner IP header,
		 *   and the first n bytes of the inner payload are contiguous.
		 *   n is at least 8, but might be larger based on 
		 *   ICMP_ADVLENPREF. See its definition in ip_icmp.h.
		 */
		ctlfunc = inetsw[ip_protox[icp->icmp_ip.ip_p]].pr_ctlinput;
		if (ctlfunc)
			(*ctlfunc)(code, (struct sockaddr *)&icmpsrc,
				   (void *)&icp->icmp_ip);
		break;

	badcode:
		ICMPSTAT_INC(icps_badcode);
		break;

	case ICMP_ECHO:
		if (!V_icmpbmcastecho
		    && (m->m_flags & (M_MCAST | M_BCAST)) != 0) {
			ICMPSTAT_INC(icps_bmcastecho);
			break;
		}
		if (badport_bandlim(BANDLIM_ICMP_ECHO) < 0)
			goto freeit;
		icp->icmp_type = ICMP_ECHOREPLY;
		goto reflect;

	case ICMP_TSTAMP:
		if (V_icmptstamprepl == 0)
			break;
		if (!V_icmpbmcastecho
		    && (m->m_flags & (M_MCAST | M_BCAST)) != 0) {
			ICMPSTAT_INC(icps_bmcasttstamp);
			break;
		}
		if (icmplen < ICMP_TSLEN) {
			ICMPSTAT_INC(icps_badlen);
			break;
		}
		if (badport_bandlim(BANDLIM_ICMP_TSTAMP) < 0)
			goto freeit;
		icp->icmp_type = ICMP_TSTAMPREPLY;
		icp->icmp_rtime = iptime();
		icp->icmp_ttime = icp->icmp_rtime;	/* bogus, do later! */
		goto reflect;

	case ICMP_MASKREQ:
		if (V_icmpmaskrepl == 0)
			break;
		/*
		 * We are not able to respond with all ones broadcast
		 * unless we receive it over a point-to-point interface.
		 */
		if (icmplen < ICMP_MASKLEN)
			break;
		switch (ip->ip_dst.s_addr) {

		case INADDR_BROADCAST:
		case INADDR_ANY:
			icmpdst.sin_addr = ip->ip_src;
			break;

		default:
			icmpdst.sin_addr = ip->ip_dst;
		}
		ia = (struct in_ifaddr *)ifaof_ifpforaddr(
			    (struct sockaddr *)&icmpdst, m->m_pkthdr.rcvif);
		if (ia == NULL)
			break;
		if (ia->ia_ifp == NULL) 
			break;
		icp->icmp_type = ICMP_MASKREPLY;
		if (V_icmpmaskfake == 0)
			icp->icmp_mask = ia->ia_sockmask.sin_addr.s_addr;
		else
			icp->icmp_mask = V_icmpmaskfake;
		if (ip->ip_src.s_addr == 0) {
			if (ia->ia_ifp->if_flags & IFF_BROADCAST)
			    ip->ip_src = satosin(&ia->ia_broadaddr)->sin_addr;
			else if (ia->ia_ifp->if_flags & IFF_POINTOPOINT)
			    ip->ip_src = satosin(&ia->ia_dstaddr)->sin_addr;
		}
reflect:
		ICMPSTAT_INC(icps_reflect);
		ICMPSTAT_INC(icps_outhist[icp->icmp_type]);
		icmp_reflect(m);
		NET_EPOCH_EXIT(et);
		return (IPPROTO_DONE);

	case ICMP_REDIRECT:
		if (V_log_redirect) {
			u_long src, dst, gw;

			src = ntohl(ip->ip_src.s_addr);
			dst = ntohl(icp->icmp_ip.ip_dst.s_addr);
			gw = ntohl(icp->icmp_gwaddr.s_addr);
			printf("icmp redirect from %d.%d.%d.%d: "
			       "%d.%d.%d.%d => %d.%d.%d.%d\n",
			       (int)(src >> 24), (int)((src >> 16) & 0xff),
			       (int)((src >> 8) & 0xff), (int)(src & 0xff),
			       (int)(dst >> 24), (int)((dst >> 16) & 0xff),
			       (int)((dst >> 8) & 0xff), (int)(dst & 0xff),
			       (int)(gw >> 24), (int)((gw >> 16) & 0xff),
			       (int)((gw >> 8) & 0xff), (int)(gw & 0xff));
		}
		/*
		 * RFC1812 says we must ignore ICMP redirects if we
		 * are acting as router.
		 */
		if (V_drop_redirect || V_ipforwarding)
			break;
		if (code > 3)
			goto badcode;
		if (icmplen < ICMP_ADVLENMIN || icmplen < ICMP_ADVLEN(icp) ||
		    icp->icmp_ip.ip_hl < (sizeof(struct ip) >> 2)) {
			ICMPSTAT_INC(icps_badlen);
			break;
		}
		/*
		 * Short circuit routing redirects to force
		 * immediate change in the kernel's routing
		 * tables.  The message is also handed to anyone
		 * listening on a raw socket (e.g. the routing
		 * daemon for use in updating its tables).
		 */
		icmpgw.sin_addr = ip->ip_src;
		icmpdst.sin_addr = icp->icmp_gwaddr;
#ifdef	ICMPPRINTFS
		if (icmpprintfs) {
			char dstbuf[INET_ADDRSTRLEN];
			char gwbuf[INET_ADDRSTRLEN];

			printf("redirect dst %s to %s\n",
			       inet_ntoa_r(icp->icmp_ip.ip_dst, dstbuf),
			       inet_ntoa_r(icp->icmp_gwaddr, gwbuf));
		}
#endif
		icmpsrc.sin_addr = icp->icmp_ip.ip_dst;
		for ( fibnum = 0; fibnum < rt_numfibs; fibnum++) {
			in_rtredirect((struct sockaddr *)&icmpsrc,
			  (struct sockaddr *)&icmpdst,
			  (struct sockaddr *)0, RTF_GATEWAY | RTF_HOST,
			  (struct sockaddr *)&icmpgw, fibnum);
		}
		pfctlinput(PRC_REDIRECT_HOST, (struct sockaddr *)&icmpsrc);
		break;

	/*
	 * No kernel processing for the following;
	 * just fall through to send to raw listener.
	 */
	case ICMP_ECHOREPLY:
	case ICMP_ROUTERADVERT:
	case ICMP_ROUTERSOLICIT:
	case ICMP_TSTAMPREPLY:
	case ICMP_IREQREPLY:
	case ICMP_MASKREPLY:
	case ICMP_SOURCEQUENCH:
	default:
		break;
	}

raw:
	NET_EPOCH_EXIT(et);
	*mp = m;
	rip_input(mp, offp, proto);
	return (IPPROTO_DONE);

freeit:
	NET_EPOCH_EXIT(et);
	m_freem(m);
	return (IPPROTO_DONE);
}

/*
 * Reflect the ip packet back to the source
 */
static void
icmp_reflect(struct mbuf *m)
{
	struct rm_priotracker in_ifa_tracker;
	struct epoch_tracker et;
	struct ip *ip = mtod(m, struct ip *);
	struct ifaddr *ifa;
	struct ifnet *ifp;
	struct in_ifaddr *ia;
	struct in_addr t;
	struct nhop4_extended nh_ext;
	struct mbuf *opts = NULL;
	int optlen = (ip->ip_hl << 2) - sizeof(struct ip);

	if (IN_MULTICAST(ntohl(ip->ip_src.s_addr)) ||
	    IN_EXPERIMENTAL(ntohl(ip->ip_src.s_addr)) ||
	    IN_ZERONET(ntohl(ip->ip_src.s_addr)) ) {
		m_freem(m);	/* Bad return address */
		ICMPSTAT_INC(icps_badaddr);
		goto done;	/* Ip_output() will check for broadcast */
	}

	t = ip->ip_dst;
	ip->ip_dst = ip->ip_src;

	/*
	 * Source selection for ICMP replies:
	 *
	 * If the incoming packet was addressed directly to one of our
	 * own addresses, use dst as the src for the reply.
	 */
	IN_IFADDR_RLOCK(&in_ifa_tracker);
	LIST_FOREACH(ia, INADDR_HASH(t.s_addr), ia_hash) {
		if (t.s_addr == IA_SIN(ia)->sin_addr.s_addr) {
			t = IA_SIN(ia)->sin_addr;
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			goto match;
		}
	}
	IN_IFADDR_RUNLOCK(&in_ifa_tracker);

	/*
	 * If the incoming packet was addressed to one of our broadcast
	 * addresses, use the first non-broadcast address which corresponds
	 * to the incoming interface.
	 */
	ifp = m->m_pkthdr.rcvif;
	if (ifp != NULL && ifp->if_flags & IFF_BROADCAST) {
		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			ia = ifatoia(ifa);
			if (satosin(&ia->ia_broadaddr)->sin_addr.s_addr ==
			    t.s_addr) {
				t = IA_SIN(ia)->sin_addr;
				NET_EPOCH_EXIT(et);
				goto match;
			}
		}
		NET_EPOCH_EXIT(et);
	}
	/*
	 * If the packet was transiting through us, use the address of
	 * the interface the packet came through in.  If that interface
	 * doesn't have a suitable IP address, the normal selection
	 * criteria apply.
	 */
	if (V_icmp_rfi && ifp != NULL) {
		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			ia = ifatoia(ifa);
			t = IA_SIN(ia)->sin_addr;
			NET_EPOCH_EXIT(et);
			goto match;
		}
		NET_EPOCH_EXIT(et);
	}
	/*
	 * If the incoming packet was not addressed directly to us, use
	 * designated interface for icmp replies specified by sysctl
	 * net.inet.icmp.reply_src (default not set). Otherwise continue
	 * with normal source selection.
	 */
	if (V_reply_src[0] != '\0' && (ifp = ifunit(V_reply_src))) {
		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			ia = ifatoia(ifa);
			t = IA_SIN(ia)->sin_addr;
			NET_EPOCH_EXIT(et);
			goto match;
		}
		NET_EPOCH_EXIT(et);
	}
	/*
	 * If the packet was transiting through us, use the address of
	 * the interface that is the closest to the packet source.
	 * When we don't have a route back to the packet source, stop here
	 * and drop the packet.
	 */
	if (fib4_lookup_nh_ext(M_GETFIB(m), ip->ip_dst, 0, 0, &nh_ext) != 0) {
		m_freem(m);
		ICMPSTAT_INC(icps_noroute);
		goto done;
	}
	t = nh_ext.nh_src;
match:
#ifdef MAC
	mac_netinet_icmp_replyinplace(m);
#endif
	ip->ip_src = t;
	ip->ip_ttl = V_ip_defttl;

	if (optlen > 0) {
		u_char *cp;
		int opt, cnt;
		u_int len;

		/*
		 * Retrieve any source routing from the incoming packet;
		 * add on any record-route or timestamp options.
		 */
		cp = (u_char *) (ip + 1);
		if ((opts = ip_srcroute(m)) == NULL &&
		    (opts = m_gethdr(M_NOWAIT, MT_DATA))) {
			opts->m_len = sizeof(struct in_addr);
			mtod(opts, struct in_addr *)->s_addr = 0;
		}
		if (opts) {
#ifdef ICMPPRINTFS
		    if (icmpprintfs)
			    printf("icmp_reflect optlen %d rt %d => ",
				optlen, opts->m_len);
#endif
		    for (cnt = optlen; cnt > 0; cnt -= len, cp += len) {
			    opt = cp[IPOPT_OPTVAL];
			    if (opt == IPOPT_EOL)
				    break;
			    if (opt == IPOPT_NOP)
				    len = 1;
			    else {
				    if (cnt < IPOPT_OLEN + sizeof(*cp))
					    break;
				    len = cp[IPOPT_OLEN];
				    if (len < IPOPT_OLEN + sizeof(*cp) ||
				        len > cnt)
					    break;
			    }
			    /*
			     * Should check for overflow, but it "can't happen"
			     */
			    if (opt == IPOPT_RR || opt == IPOPT_TS ||
				opt == IPOPT_SECURITY) {
				    bcopy((caddr_t)cp,
					mtod(opts, caddr_t) + opts->m_len, len);
				    opts->m_len += len;
			    }
		    }
		    /* Terminate & pad, if necessary */
		    cnt = opts->m_len % 4;
		    if (cnt) {
			    for (; cnt < 4; cnt++) {
				    *(mtod(opts, caddr_t) + opts->m_len) =
					IPOPT_EOL;
				    opts->m_len++;
			    }
		    }
#ifdef ICMPPRINTFS
		    if (icmpprintfs)
			    printf("%d\n", opts->m_len);
#endif
		}
		ip_stripoptions(m);
	}
	m_tag_delete_nonpersistent(m);
	m->m_flags &= ~(M_BCAST|M_MCAST);
	icmp_send(m, opts);
done:
	if (opts)
		(void)m_free(opts);
}

/*
 * Send an icmp packet back to the ip level,
 * after supplying a checksum.
 */
static void
icmp_send(struct mbuf *m, struct mbuf *opts)
{
	struct ip *ip = mtod(m, struct ip *);
	int hlen;
	struct icmp *icp;

	hlen = ip->ip_hl << 2;
	m->m_data += hlen;
	m->m_len -= hlen;
	icp = mtod(m, struct icmp *);
	icp->icmp_cksum = 0;
	icp->icmp_cksum = in_cksum(m, ntohs(ip->ip_len) - hlen);
	m->m_data -= hlen;
	m->m_len += hlen;
	m->m_pkthdr.rcvif = (struct ifnet *)0;
#ifdef ICMPPRINTFS
	if (icmpprintfs) {
		char dstbuf[INET_ADDRSTRLEN];
		char srcbuf[INET_ADDRSTRLEN];

		printf("icmp_send dst %s src %s\n",
		    inet_ntoa_r(ip->ip_dst, dstbuf),
		    inet_ntoa_r(ip->ip_src, srcbuf));
	}
#endif
	(void) ip_output(m, opts, NULL, 0, NULL, NULL);
}

/*
 * Return milliseconds since 00:00 UTC in network format.
 */
uint32_t
iptime(void)
{
	struct timeval atv;
	u_long t;

	getmicrotime(&atv);
	t = (atv.tv_sec % (24*60*60)) * 1000 + atv.tv_usec / 1000;
	return (htonl(t));
}

/*
 * Return the next larger or smaller MTU plateau (table from RFC 1191)
 * given current value MTU.  If DIR is less than zero, a larger plateau
 * is returned; otherwise, a smaller value is returned.
 */
int
ip_next_mtu(int mtu, int dir)
{
	static int mtutab[] = {
		65535, 32000, 17914, 8166, 4352, 2002, 1492, 1280, 1006, 508,
		296, 68, 0
	};
	int i, size;

	size = (sizeof mtutab) / (sizeof mtutab[0]);
	if (dir >= 0) {
		for (i = 0; i < size; i++)
			if (mtu > mtutab[i])
				return mtutab[i];
	} else {
		for (i = size - 1; i >= 0; i--)
			if (mtu < mtutab[i])
				return mtutab[i];
		if (mtu == mtutab[0])
			return mtutab[0];
	}
	return 0;
}
#endif /* INET */


/*
 * badport_bandlim() - check for ICMP bandwidth limit
 *
 *	Return 0 if it is ok to send an ICMP error response, -1 if we have
 *	hit our bandwidth limit and it is not ok.
 *
 *	If icmplim is <= 0, the feature is disabled and 0 is returned.
 *
 *	For now we separate the TCP and UDP subsystems w/ different 'which'
 *	values.  We may eventually remove this separation (and simplify the
 *	code further).
 *
 *	Note that the printing of the error message is delayed so we can
 *	properly print the icmp error rate that the system was trying to do
 *	(i.e. 22000/100 pps, etc...).  This can cause long delays in printing
 *	the 'final' error, but it doesn't make sense to solve the printing
 *	delay with more complex code.
 */
struct icmp_rate {
	const char *descr;
	struct counter_rate cr;
};
VNET_DEFINE_STATIC(struct icmp_rate, icmp_rates[BANDLIM_MAX]) = {
	{ "icmp unreach response" },
	{ "icmp ping response" },
	{ "icmp tstamp response" },
	{ "closed port RST response" },
	{ "open port RST response" },
	{ "icmp6 unreach response" },
	{ "sctp ootb response" }
};
#define	V_icmp_rates	VNET(icmp_rates)

static void
icmp_bandlimit_init(void)
{

	for (int i = 0; i < BANDLIM_MAX; i++) {
		V_icmp_rates[i].cr.cr_rate = counter_u64_alloc(M_WAITOK);
		V_icmp_rates[i].cr.cr_ticks = ticks;
	}
}
VNET_SYSINIT(icmp_bandlimit, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY,
    icmp_bandlimit_init, NULL);

static void
icmp_bandlimit_uninit(void)
{

	for (int i = 0; i < BANDLIM_MAX; i++)
		counter_u64_free(V_icmp_rates[i].cr.cr_rate);
}
VNET_SYSUNINIT(icmp_bandlimit, SI_SUB_PROTO_DOMAIN, SI_ORDER_THIRD,
    icmp_bandlimit_uninit, NULL);

int
badport_bandlim(int which)
{
	int64_t pps;

	if (V_icmplim == 0 || which == BANDLIM_UNLIMITED)
		return (0);

	KASSERT(which >= 0 && which < BANDLIM_MAX,
	    ("%s: which %d", __func__, which));

	pps = counter_ratecheck(&V_icmp_rates[which].cr, V_icmplim);
	if (pps == -1)
		return (-1);
	if (pps > 0 && V_icmplim_output)
		log(LOG_NOTICE, "Limiting %s from %jd to %d packets/sec\n",
			V_icmp_rates[which].descr, (intmax_t )pps, V_icmplim);
	return (0);
}
