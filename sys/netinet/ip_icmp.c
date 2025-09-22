/*	$OpenBSD: ip_icmp.c,v 1.203 2025/07/08 00:47:41 jsg Exp $	*/
/*	$NetBSD: ip_icmp.c,v 1.19 1996/02/13 23:42:22 christos Exp $	*/

/*
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
 *	@(#)COPYRIGHT	1.1 (NRL) 17 January 1995
 *
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *	This product includes software developed at the Information
 *	Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

#include "carp.h"
#include "pf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_var.h>
#include <netinet/icmp_var.h>

#if NCARP > 0
#include <netinet/ip_carp.h>
#endif

/*
 * ICMP routines: error generation, receive packet processing, and
 * routines to turnaround packets back to the originator, and
 * host table maintenance routines.
 */

/*
 * Locks used to protect data:
 *	a	atomic
 */

#ifdef ICMPPRINTFS
int	icmpprintfs = 0;	/* Settable from ddb */
#endif

/* values controllable via sysctl */
int	icmpmaskrepl = 0;		/* [a] */
int	icmpbmcastecho = 0;		/* [a] */
int	icmptstamprepl = 1;		/* [a] */
int	icmperrppslim = 100;		/* [a] */
int	icmp_rediraccept = 0;		/* [a] */
int	icmp_redirtimeout = 10 * 60;

static int icmperrpps_count = 0;
static struct timeval icmperrppslim_last;

struct rttimer_queue ip_mtudisc_timeout_q;
struct rttimer_queue icmp_redirect_timeout_q;
struct cpumem *icmpcounters;

#ifndef SMALL_KERNEL
const struct sysctl_bounded_args icmpctl_vars[] =  {
	{ ICMPCTL_MASKREPL, &icmpmaskrepl, 0, 1 },
	{ ICMPCTL_BMCASTECHO, &icmpbmcastecho, 0, 1 },
	{ ICMPCTL_ERRPPSLIMIT, &icmperrppslim, -1, INT_MAX },
	{ ICMPCTL_REDIRACCEPT, &icmp_rediraccept, 0, 1 },
	{ ICMPCTL_TSTAMPREPL, &icmptstamprepl, 0, 1 },
};
#endif /* SMALL_KERNEL */

void icmp_mtudisc_timeout(struct rtentry *, u_int);
int icmp_ratelimit(const struct in_addr *, const int, const int);
int icmp_input_if(struct ifnet *, struct mbuf **, int *, int, int,
    struct netstack *);
int icmp_sysctl_icmpstat(void *, size_t *, void *);

void
icmp_init(void)
{
	rt_timer_queue_init(&ip_mtudisc_timeout_q, ip_mtudisc_timeout,
	    &icmp_mtudisc_timeout);
	rt_timer_queue_init(&icmp_redirect_timeout_q, icmp_redirtimeout,
	    NULL);
	icmpcounters = counters_alloc(icps_ncounters);
}

struct mbuf *
icmp_do_error(struct mbuf *n, int type, int code, u_int32_t dest, int destmtu)
{
	struct ip *oip = mtod(n, struct ip *), *nip;
	unsigned oiplen = oip->ip_hl << 2;
	struct icmp *icp;
	struct mbuf *m;
	unsigned icmplen, mblen;

#ifdef ICMPPRINTFS
	if (icmpprintfs)
		printf("icmp_error(%x, %d, %d)\n", oip, type, code);
#endif
	if (type != ICMP_REDIRECT)
		icmpstat_inc(icps_error);
	/*
	 * Don't send error if not the first fragment of message.
	 * Don't error if the old packet protocol was ICMP
	 * error message, only known informational types.
	 */
	if (oip->ip_off & htons(IP_OFFMASK))
		goto freeit;
	if (oip->ip_p == IPPROTO_ICMP && type != ICMP_REDIRECT &&
	    n->m_len >= oiplen + ICMP_MINLEN &&
	    !ICMP_INFOTYPE(((struct icmp *)
	    ((caddr_t)oip + oiplen))->icmp_type)) {
		icmpstat_inc(icps_oldicmp);
		goto freeit;
	}
	/* Don't send error in response to a multicast or broadcast packet */
	if (n->m_flags & (M_BCAST|M_MCAST))
		goto freeit;

	/*
	 * First, do a rate limitation check.
	 */
	if (icmp_ratelimit(&oip->ip_src, type, code)) {
		icmpstat_inc(icps_toofreq);
		goto freeit;
	}

	/*
	 * Now, formulate icmp message
	 */
	icmplen = oiplen + min(8, ntohs(oip->ip_len));
	/*
	 * Defend against mbuf chains shorter than oip->ip_len:
	 */
	mblen = 0;
	for (m = n; m && (mblen < icmplen); m = m->m_next)
		mblen += m->m_len;
	icmplen = min(mblen, icmplen);

	/*
	 * As we are not required to return everything we have,
	 * we return whatever we can return at ease.
	 *
	 * Note that ICMP datagrams longer than 576 octets are out of spec
	 * according to RFC1812;
	 */

	KASSERT(ICMP_MINLEN + sizeof (struct ip) <= MCLBYTES);

	if (sizeof (struct ip) + icmplen + ICMP_MINLEN > MCLBYTES)
		icmplen = MCLBYTES - ICMP_MINLEN - sizeof (struct ip);

	m = m_gethdr(M_DONTWAIT, MT_HEADER);
	if (m && ((sizeof (struct ip) + icmplen + ICMP_MINLEN +
	    sizeof(long) - 1) &~ (sizeof(long) - 1)) > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_freem(m);
			m = NULL;
		}
	}
	if (m == NULL)
		goto freeit;
	/* keep in same rtable and preserve other pkthdr bits */
	m->m_pkthdr.ph_rtableid = n->m_pkthdr.ph_rtableid;
	m->m_pkthdr.ph_ifidx = n->m_pkthdr.ph_ifidx;
	/* move PF_GENERATED to new packet, if existent XXX preserve more? */
	if (n->m_pkthdr.pf.flags & PF_TAG_GENERATED)
		m->m_pkthdr.pf.flags |= PF_TAG_GENERATED;
	m->m_pkthdr.len = m->m_len = icmplen + ICMP_MINLEN;
	m_align(m, m->m_len);
	icp = mtod(m, struct icmp *);
	if ((u_int)type > ICMP_MAXTYPE)
		panic("icmp_error");
	icmpstat_inc(icps_outhist + type);
	icp->icmp_type = type;
	if (type == ICMP_REDIRECT)
		icp->icmp_gwaddr.s_addr = dest;
	else {
		icp->icmp_void = 0;
		/*
		 * The following assignments assume an overlay with the
		 * zeroed icmp_void field.
		 */
		if (type == ICMP_PARAMPROB) {
			icp->icmp_pptr = code;
			code = 0;
		} else if (type == ICMP_UNREACH &&
		    code == ICMP_UNREACH_NEEDFRAG && destmtu)
			icp->icmp_nextmtu = htons(destmtu);
	}

	icp->icmp_code = code;
	m_copydata(n, 0, icmplen, &icp->icmp_ip);

	/*
	 * Now, copy old ip header (without options)
	 * in front of icmp message.
	 */
	m = m_prepend(m, sizeof(struct ip), M_DONTWAIT);
	if (m == NULL)
		goto freeit;
	nip = mtod(m, struct ip *);
	/* ip_v set in ip_output */
	nip->ip_hl = sizeof(struct ip) >> 2;
	nip->ip_tos = 0;
	nip->ip_len = htons(m->m_len);
	/* ip_id set in ip_output */
	nip->ip_off = 0;
	/* ip_ttl set in icmp_reflect */
	nip->ip_p = IPPROTO_ICMP;
	nip->ip_src = oip->ip_src;
	nip->ip_dst = oip->ip_dst;

	m_freem(n);
	return (m);

freeit:
	m_freem(n);
	return (NULL);
}

/*
 * Generate an error packet of type error
 * in response to bad packet ip.
 *
 * The ip packet inside has ip_off and ip_len in host byte order.
 */
void
icmp_error(struct mbuf *n, int type, int code, u_int32_t dest, int destmtu)
{
	struct mbuf *m;

	m = icmp_do_error(n, type, code, dest, destmtu);
	if (m != NULL)
		if (!icmp_reflect(m, NULL, NULL))
			icmp_send(m, NULL);
}

/*
 * Process a received ICMP message.
 */
int
icmp_input(struct mbuf **mp, int *offp, int proto, int af, struct netstack *ns)
{
	struct ifnet *ifp;

	ifp = if_get((*mp)->m_pkthdr.ph_ifidx);
	if (ifp == NULL) {
		m_freemp(mp);
		return IPPROTO_DONE;
	}

	proto = icmp_input_if(ifp, mp, offp, proto, af, ns);
	if_put(ifp);
	return proto;
}

int
icmp_input_if(struct ifnet *ifp, struct mbuf **mp, int *offp, int proto,
    int af, struct netstack *ns)
{
	struct mbuf *m = *mp;
	int hlen = *offp;
	struct icmp *icp;
	struct ip *ip = mtod(m, struct ip *);
	struct sockaddr_in sin;
	int icmplen, i, code;
	struct in_ifaddr *ia;
	void (*ctlfunc)(int, struct sockaddr *, u_int, void *);
	struct mbuf *opts;

	/*
	 * Locate icmp structure in mbuf, and check
	 * that not corrupted and of at least minimum length.
	 */
	icmplen = ntohs(ip->ip_len) - hlen;
#ifdef ICMPPRINTFS
	if (icmpprintfs) {
		char dst[INET_ADDRSTRLEN], src[INET_ADDRSTRLEN];

		inet_ntop(AF_INET, &ip->ip_dst, dst, sizeof(dst));
		inet_ntop(AF_INET, &ip->ip_src, src, sizeof(src));

		printf("icmp_input from %s to %s, len %d\n", src, dst, icmplen);
	}
#endif
	if (icmplen < ICMP_MINLEN) {
		icmpstat_inc(icps_tooshort);
		goto freeit;
	}
	i = hlen + min(icmplen, ICMP_ADVLENMAX);
	if ((m = *mp = m_pullup(m, i)) == NULL) {
		icmpstat_inc(icps_tooshort);
		return IPPROTO_DONE;
	}
	ip = mtod(m, struct ip *);
	if (in4_cksum(m, 0, hlen, icmplen)) {
		icmpstat_inc(icps_checksum);
		goto freeit;
	}

	icp = (struct icmp *)(mtod(m, caddr_t) + hlen);
#ifdef ICMPPRINTFS
	/*
	 * Message type specific processing.
	 */
	if (icmpprintfs)
		printf("icmp_input, type %d code %d\n", icp->icmp_type,
		    icp->icmp_code);
#endif
	if (icp->icmp_type > ICMP_MAXTYPE)
		goto raw;
#if NPF > 0
	if (m->m_pkthdr.pf.flags & PF_TAG_DIVERTED) {
		switch (icp->icmp_type) {
		 /*
		  * As pf_icmp_mapping() considers redirects belonging to a
		  * diverted connection, we must include it here.
		  */
		case ICMP_REDIRECT:
			/* FALLTHROUGH */
		/*
		 * These ICMP types map to other connections.  They must be
		 * delivered to pr_ctlinput() also for diverted connections.
		 */
		case ICMP_UNREACH:
		case ICMP_TIMXCEED:
		case ICMP_PARAMPROB:
		case ICMP_SOURCEQUENCH:
			/*
			 * Do not use the divert-to property of the TCP or UDP
			 * rule when doing the PCB lookup for the raw socket.
			 */
			m->m_pkthdr.pf.flags &=~ PF_TAG_DIVERTED;
			break;
		default:
			goto raw;
		}
	}
#endif /* NPF */
	icmpstat_inc(icps_inhist + icp->icmp_type);
	code = icp->icmp_code;
	switch (icp->icmp_type) {

	case ICMP_UNREACH:
		switch (code) {
		case ICMP_UNREACH_NET:
		case ICMP_UNREACH_HOST:
		case ICMP_UNREACH_PROTOCOL:
		case ICMP_UNREACH_PORT:
		case ICMP_UNREACH_SRCFAIL:
			code += PRC_UNREACH_NET;
			break;

		case ICMP_UNREACH_NEEDFRAG:
			code = PRC_MSGSIZE;
			break;

		case ICMP_UNREACH_NET_UNKNOWN:
		case ICMP_UNREACH_NET_PROHIB:
		case ICMP_UNREACH_TOSNET:
			code = PRC_UNREACH_NET;
			break;

		case ICMP_UNREACH_HOST_UNKNOWN:
		case ICMP_UNREACH_ISOLATED:
		case ICMP_UNREACH_HOST_PROHIB:
		case ICMP_UNREACH_TOSHOST:
		case ICMP_UNREACH_FILTER_PROHIB:
		case ICMP_UNREACH_HOST_PRECEDENCE:
		case ICMP_UNREACH_PRECEDENCE_CUTOFF:
			code = PRC_UNREACH_HOST;
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
		goto deliver;

	case ICMP_SOURCEQUENCH:
		if (code)
			goto badcode;
		code = PRC_QUENCH;
	deliver:
		/*
		 * Problem with datagram; advise higher level routines.
		 */
		if (icmplen < ICMP_ADVLENMIN || icmplen < ICMP_ADVLEN(icp) ||
		    icp->icmp_ip.ip_hl < (sizeof(struct ip) >> 2)) {
			icmpstat_inc(icps_badlen);
			goto freeit;
		}
		if (IN_MULTICAST(icp->icmp_ip.ip_dst.s_addr))
			goto badcode;
#ifdef INET6
		/* Get more contiguous data for a v6 in v4 ICMP message. */
		if (icp->icmp_ip.ip_p == IPPROTO_IPV6) {
			if (icmplen < ICMP_V6ADVLENMIN ||
			    icmplen < ICMP_V6ADVLEN(icp)) {
				icmpstat_inc(icps_badlen);
				goto freeit;
			}
		}
#endif /* INET6 */
#ifdef ICMPPRINTFS
		if (icmpprintfs)
			printf("deliver to protocol %d\n", icp->icmp_ip.ip_p);
#endif
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_len = sizeof(struct sockaddr_in);
		sin.sin_addr = icp->icmp_ip.ip_dst;
#if NCARP > 0
		if (carp_lsdrop(ifp, m, AF_INET, &sin.sin_addr.s_addr,
		    &ip->ip_dst.s_addr, 1))
			goto freeit;
#endif
		/*
		 * XXX if the packet contains [IPv4 AH TCP], we can't make a
		 * notification to TCP layer.
		 */
		ctlfunc = inetsw[ip_protox[icp->icmp_ip.ip_p]].pr_ctlinput;
		if (ctlfunc)
			(*ctlfunc)(code, sintosa(&sin), m->m_pkthdr.ph_rtableid,
			    &icp->icmp_ip);
		break;

	badcode:
		icmpstat_inc(icps_badcode);
		break;

	case ICMP_ECHO:
		if (atomic_load_int(&icmpbmcastecho) == 0 &&
		    (m->m_flags & (M_MCAST | M_BCAST)) != 0) {
			icmpstat_inc(icps_bmcastecho);
			break;
		}
		icp->icmp_type = ICMP_ECHOREPLY;
		goto reflect;

	case ICMP_TSTAMP:
		if (atomic_load_int(&icmptstamprepl) == 0)
			break;

		if (atomic_load_int(&icmpbmcastecho) == 0 &&
		    (m->m_flags & (M_MCAST | M_BCAST)) != 0) {
			icmpstat_inc(icps_bmcastecho);
			break;
		}
		if (icmplen < ICMP_TSLEN) {
			icmpstat_inc(icps_badlen);
			break;
		}
		icp->icmp_type = ICMP_TSTAMPREPLY;
		icp->icmp_rtime = iptime();
		icp->icmp_ttime = icp->icmp_rtime;	/* bogus, do later! */
		goto reflect;

	case ICMP_MASKREQ:
		if (atomic_load_int(&icmpmaskrepl) == 0)
			break;
		if (icmplen < ICMP_MASKLEN) {
			icmpstat_inc(icps_badlen);
			break;
		}
		/*
		 * We are not able to respond with all ones broadcast
		 * unless we receive it over a point-to-point interface.
		 */
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_len = sizeof(struct sockaddr_in);
		if (ip->ip_dst.s_addr == INADDR_BROADCAST ||
		    ip->ip_dst.s_addr == INADDR_ANY)
			sin.sin_addr = ip->ip_src;
		else
			sin.sin_addr = ip->ip_dst;
		if (ifp == NULL)
			break;
		ia = ifatoia(ifaof_ifpforaddr(sintosa(&sin), ifp));
		if (ia == NULL)
			break;
		icp->icmp_type = ICMP_MASKREPLY;
		icp->icmp_mask = ia->ia_sockmask.sin_addr.s_addr;
		if (ip->ip_src.s_addr == 0) {
			if (ifp->if_flags & IFF_BROADCAST) {
				if (ia->ia_broadaddr.sin_addr.s_addr)
					ip->ip_src = ia->ia_broadaddr.sin_addr;
				else
					ip->ip_src.s_addr = INADDR_BROADCAST;
			}
			else if (ifp->if_flags & IFF_POINTOPOINT)
				ip->ip_src = ia->ia_dstaddr.sin_addr;
		}
reflect:
#if NCARP > 0
		if (carp_lsdrop(ifp, m, AF_INET, &ip->ip_src.s_addr,
		    &ip->ip_dst.s_addr, 1))
			goto freeit;
#endif
		icmpstat_inc(icps_reflect);
		icmpstat_inc(icps_outhist + icp->icmp_type);
		if (!icmp_reflect(m, &opts, NULL)) {
			icmp_send(m, opts);
			m_free(opts);
		}
		return IPPROTO_DONE;

	case ICMP_REDIRECT:
	{
		struct sockaddr_in sdst;
		struct sockaddr_in sgw;
		struct sockaddr_in ssrc;
		struct rtentry *newrt = NULL;
		int i_am_router = (atomic_load_int(&ip_forwarding) != 0);

		if (atomic_load_int(&icmp_rediraccept) == 0 || i_am_router)
			goto freeit;
		if (code > 3)
			goto badcode;
		if (icmplen < ICMP_ADVLENMIN || icmplen < ICMP_ADVLEN(icp) ||
		    icp->icmp_ip.ip_hl < (sizeof(struct ip) >> 2)) {
			icmpstat_inc(icps_badlen);
			break;
		}
		/*
		 * Short circuit routing redirects to force
		 * immediate change in the kernel's routing
		 * tables.  The message is also handed to anyone
		 * listening on a raw socket (e.g. the routing
		 * daemon for use in updating its tables).
		 */
		memset(&sdst, 0, sizeof(sdst));
		memset(&sgw, 0, sizeof(sgw));
		memset(&ssrc, 0, sizeof(ssrc));
		sdst.sin_family = sgw.sin_family = ssrc.sin_family = AF_INET;
		sdst.sin_len = sgw.sin_len = ssrc.sin_len = sizeof(sdst);
		memcpy(&sdst.sin_addr, &icp->icmp_ip.ip_dst,
		    sizeof(sdst.sin_addr));
		memcpy(&sgw.sin_addr, &icp->icmp_gwaddr,
		    sizeof(sgw.sin_addr));
		memcpy(&ssrc.sin_addr, &ip->ip_src,
		    sizeof(ssrc.sin_addr));

#ifdef	ICMPPRINTFS
		if (icmpprintfs) {
			char gw[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];

			inet_ntop(AF_INET, &icp->icmp_gwaddr, gw, sizeof(gw));
			inet_ntop(AF_INET, &icp->icmp_ip.ip_dst,
			    dst, sizeof(dst));

			printf("redirect dst %s to %s\n", dst, gw);
		}
#endif

#if NCARP > 0
		if (carp_lsdrop(ifp, m, AF_INET, &sdst.sin_addr.s_addr,
		    &ip->ip_dst.s_addr, 1))
			goto freeit;
#endif
		rtredirect(sintosa(&sdst), sintosa(&sgw),
		    sintosa(&ssrc), &newrt, m->m_pkthdr.ph_rtableid);
		if (newrt != NULL && icmp_redirtimeout > 0) {
			rt_timer_add(newrt, &icmp_redirect_timeout_q,
			    m->m_pkthdr.ph_rtableid);
		}
		rtfree(newrt);
		pfctlinput(PRC_REDIRECT_HOST, sintosa(&sdst));
		break;
	}
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
	case ICMP_TRACEROUTE:
	case ICMP_DATACONVERR:
	case ICMP_MOBILE_REDIRECT:
	case ICMP_IPV6_WHEREAREYOU:
	case ICMP_IPV6_IAMHERE:
	case ICMP_MOBILE_REGREQUEST:
	case ICMP_MOBILE_REGREPLY:
	case ICMP_PHOTURIS:
	default:
		break;
	}

raw:
	return rip_input(mp, offp, proto, af, ns);

freeit:
	m_freem(m);
	return IPPROTO_DONE;
}

/*
 * Reflect the ip packet back to the source
 */
int
icmp_reflect(struct mbuf *m, struct mbuf **op, struct in_ifaddr *ia)
{
	struct ip *ip = mtod(m, struct ip *);
	struct mbuf *opts = NULL;
	struct sockaddr_in sin;
	struct rtentry *rt;
	struct in_addr ip_src = { INADDR_ANY };
	int optlen = (ip->ip_hl << 2) - sizeof(struct ip);
	u_int rtableid;
	u_int8_t pfflags;

	if (!in_canforward(ip->ip_src) &&
	    ((ip->ip_src.s_addr & IN_CLASSA_NET) !=
	    htonl(IN_LOOPBACKNET << IN_CLASSA_NSHIFT))) {
		m_freem(m);		/* Bad return address */
		return (EHOSTUNREACH);
	}

	if (m->m_pkthdr.ph_loopcnt++ >= M_MAXLOOP) {
		m_freem(m);
		return (ELOOP);
	}
	rtableid = m->m_pkthdr.ph_rtableid;

	/*
	 * If the incoming packet was addressed directly to us,
	 * use dst as the src for the reply.  For broadcast, use
	 * the address which corresponds to the incoming interface.
	 */
	if (ia == NULL) {
		memset(&sin, 0, sizeof(sin));
		sin.sin_len = sizeof(sin);
		sin.sin_family = AF_INET;
		sin.sin_addr = ip->ip_dst;

		rt = rtalloc(sintosa(&sin), 0, rtableid);
		if (rtisvalid(rt)) {
			if (ISSET(rt->rt_flags, RTF_LOCAL))
				ip_src = ip->ip_dst;
			else if (ISSET(rt->rt_flags, RTF_BROADCAST)) {
				ia = ifatoia(rt->rt_ifa);
				ip_src = ia->ia_addr.sin_addr;
			}
		}
		rtfree(rt);
	} else
		ip_src = ia->ia_addr.sin_addr;

	/*
	 * The following happens if the packet was not addressed to us.
	 * If we're directly connected use the closest address, otherwise
	 * try to use the sourceaddr from the routing table.
	 */
	if (ip_src.s_addr == INADDR_ANY) {
		memset(&sin, 0, sizeof(sin));
		sin.sin_len = sizeof(sin);
		sin.sin_family = AF_INET;
		sin.sin_addr = ip->ip_src;

		/* keep packet in the original virtual instance */
		rt = rtalloc(sintosa(&sin), RT_RESOLVE, rtableid);
		if (rtisvalid(rt) &&
		    !ISSET(rt->rt_flags, RTF_GATEWAY)) {
			ia = ifatoia(rt->rt_ifa);
			ip_src = ia->ia_addr.sin_addr;
		} else {
			struct sockaddr *sourceaddr;
			struct ifaddr *ifa;

			sourceaddr = rtable_getsource(rtableid, AF_INET);
			if (sourceaddr != NULL) {
				ifa = ifa_ifwithaddr(sourceaddr, rtableid);
				if (ifa != NULL &&
				    ISSET(ifa->ifa_ifp->if_flags, IFF_UP))
					ip_src = satosin(sourceaddr)->sin_addr;
			}
		}
		rtfree(rt);
	}

	/*
	 * If the above didn't find an ip_src, ip_output() will try
	 * and fill it in for us.
	 */

	pfflags = m->m_pkthdr.pf.flags;

	m_resethdr(m);
	m->m_pkthdr.ph_rtableid = rtableid;
	m->m_pkthdr.pf.flags = pfflags & PF_TAG_GENERATED;
	ip->ip_dst = ip->ip_src;
	ip->ip_src = ip_src;
	ip->ip_ttl = MAXTTL;

	if (optlen > 0) {
		u_char *cp;
		int opt, cnt;
		u_int len;

		/*
		 * Retrieve any source routing from the incoming packet;
		 * add on any record-route or timestamp options.
		 */
		cp = (u_char *) (ip + 1);
		if (op && (opts = ip_srcroute(m)) == NULL &&
		    (opts = m_gethdr(M_DONTWAIT, MT_HEADER))) {
			opts->m_len = sizeof(struct in_addr);
			mtod(opts, struct in_addr *)->s_addr = 0;
		}
		if (op && opts) {
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
				 * Should check for overflow, but it
				 * "can't happen"
				 */
				if (opt == IPOPT_RR || opt == IPOPT_TS ||
				    opt == IPOPT_SECURITY) {
					memcpy(mtod(opts, caddr_t) +
					    opts->m_len, cp, len);
					opts->m_len += len;
				}
			}
			/* Terminate & pad, if necessary */
			if ((cnt = opts->m_len % 4) != 0)
				for (; cnt < 4; cnt++) {
					*(mtod(opts, caddr_t) + opts->m_len) =
					    IPOPT_EOL;
					opts->m_len++;
				}
#ifdef ICMPPRINTFS
			if (icmpprintfs)
				printf("%d\n", opts->m_len);
#endif
		}
		ip_stripoptions(m);
	}
	m->m_flags &= ~(M_BCAST|M_MCAST);
	if (op)
		*op = opts;

	return (0);
}

/*
 * Send an icmp packet back to the ip level
 */
void
icmp_send(struct mbuf *m, struct mbuf *opts)
{
	struct ip *ip = mtod(m, struct ip *);
	int hlen;
	struct icmp *icp;

	hlen = ip->ip_hl << 2;
	icp = (struct icmp *)(mtod(m, caddr_t) + hlen);
	icp->icmp_cksum = 0;
	m->m_pkthdr.csum_flags = M_ICMP_CSUM_OUT;
#ifdef ICMPPRINTFS
	if (icmpprintfs) {
		char dst[INET_ADDRSTRLEN], src[INET_ADDRSTRLEN];

		inet_ntop(AF_INET, &ip->ip_dst, dst, sizeof(dst));
		inet_ntop(AF_INET, &ip->ip_src, src, sizeof(src));

		printf("icmp_send dst %s src %s\n", dst, src);
	}
#endif
	/*
	 * ip_send() cannot handle IP options properly. So in case we have
	 * options fill out the IP header here and use ip_send_raw() instead.
	 */
	if (opts != NULL) {
		m = ip_insertoptions(m, opts, &hlen);
		ip = mtod(m, struct ip *);
		ip->ip_hl = (hlen >> 2);
		ip->ip_v = IPVERSION;
		ip->ip_off &= htons(IP_DF);
		ip->ip_id = htons(ip_randomid());
		ipstat_inc(ips_localout);
		ip_send_raw(m);
	} else
		ip_send(m);
}

u_int32_t
iptime(void)
{
	struct timeval atv;
	u_long t;

	microtime(&atv);
	t = (atv.tv_sec % (24*60*60)) * 1000 + atv.tv_usec / 1000;
	return (htonl(t));
}

#ifndef SMALL_KERNEL
int
icmp_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	int error;

	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case ICMPCTL_REDIRTIMEOUT: {
		size_t savelen = *oldlenp;

		if ((error = sysctl_vslock(oldp, savelen)))
			break;
		NET_LOCK();
		error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &icmp_redirtimeout, 0, INT_MAX);
		rt_timer_queue_change(&icmp_redirect_timeout_q,
		    icmp_redirtimeout);
		NET_UNLOCK();
		sysctl_vsunlock(oldp, savelen);
		break;
	}
	case ICMPCTL_STATS:
		error = icmp_sysctl_icmpstat(oldp, oldlenp, newp);
		break;

	default:
		error = sysctl_bounded_arr(icmpctl_vars, nitems(icmpctl_vars),
		    name, namelen, oldp, oldlenp, newp, newlen);
		break;
	}

	return (error);
}

int
icmp_sysctl_icmpstat(void *oldp, size_t *oldlenp, void *newp)
{
	uint64_t counters[icps_ncounters];
	struct icmpstat icmpstat;
	u_long *words = (u_long *)&icmpstat;
	int i;

	CTASSERT(sizeof(icmpstat) == (nitems(counters) * sizeof(u_long)));
	memset(&icmpstat, 0, sizeof icmpstat);
	counters_read(icmpcounters, counters, nitems(counters), NULL);

	for (i = 0; i < nitems(counters); i++)
		words[i] = (u_long)counters[i];

	return (sysctl_rdstruct(oldp, oldlenp, newp,
	    &icmpstat, sizeof(icmpstat)));
}
#endif /* SMALL_KERNEL */

struct rtentry *
icmp_mtudisc_clone(struct in_addr dst, u_int rtableid, int ipsec)
{
	struct sockaddr_in sin;
	struct rtentry *rt;
	int error;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_addr = dst;

	rt = rtalloc(sintosa(&sin), RT_RESOLVE, rtableid);

	/* Check if the route is actually usable */
	if (!rtisvalid(rt))
		goto bad;
	/* IPsec needs the route only for PMTU, it can use reject for that */
	if (!ipsec && (rt->rt_flags & (RTF_REJECT|RTF_BLACKHOLE)))
		goto bad;

	/*
	 * No PMTU for local routes and permanent neighbors,
	 * ARP and NDP use the same expire timer as the route.
	 */
	if (ISSET(rt->rt_flags, RTF_LOCAL) ||
	    (ISSET(rt->rt_flags, RTF_LLINFO) && rt->rt_expire == 0))
		goto bad;

	/* If we didn't get a host route, allocate one */
	if ((rt->rt_flags & RTF_HOST) == 0) {
		struct rtentry *nrt;
		struct rt_addrinfo info;
		struct sockaddr_rtlabel sa_rl;

		memset(&info, 0, sizeof(info));
		info.rti_ifa = rt->rt_ifa;
		info.rti_flags = RTF_GATEWAY | RTF_HOST | RTF_DYNAMIC;
		info.rti_info[RTAX_DST] = sintosa(&sin);
		info.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
		info.rti_info[RTAX_LABEL] =
		    rtlabel_id2sa(rt->rt_labelid, &sa_rl);

		error = rtrequest(RTM_ADD, &info, rt->rt_priority, &nrt,
		    rtableid);
		if (error)
			goto bad;
		nrt->rt_rmx = rt->rt_rmx;
		rtfree(rt);
		rt = nrt;
		rtm_send(rt, RTM_ADD, 0, rtableid);
	}
	error = rt_timer_add(rt, &ip_mtudisc_timeout_q, rtableid);
	if (error)
		goto bad;

	return (rt);
bad:
	rtfree(rt);
	return (NULL);
}

/* Table of common MTUs: */
static const u_short mtu_table[] = {
	65535, 65280, 32000, 17914, 9180, 8166,
	4352, 2002, 1492, 1006, 508, 296, 68, 0
};

void
icmp_mtudisc(struct icmp *icp, u_int rtableid)
{
	struct rtentry *rt;
	struct ifnet *ifp;
	u_int rtmtu;
	u_long mtu = ntohs(icp->icmp_nextmtu);  /* Why a long?  IPv6 */

	rt = icmp_mtudisc_clone(icp->icmp_ip.ip_dst, rtableid, 0);
	if (rt == NULL)
		return;

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL) {
		rtfree(rt);
		return;
	}

	rtmtu = atomic_load_int(&rt->rt_mtu);
	if (mtu == 0) {
		int i = 0;

		mtu = ntohs(icp->icmp_ip.ip_len);
		/* Some 4.2BSD-based routers incorrectly adjust the ip_len */
		if (mtu > rtmtu && rtmtu != 0)
			mtu -= (icp->icmp_ip.ip_hl << 2);

		/* If we still can't guess a value, try the route */
		if (mtu == 0) {
			mtu = rtmtu;

			/* If no route mtu, default to the interface mtu */

			if (mtu == 0)
				mtu = ifp->if_mtu;
		}

		for (i = 0; i < nitems(mtu_table); i++)
			if (mtu > mtu_table[i]) {
				mtu = mtu_table[i];
				break;
			}
	}

	/*
	 * XXX:   RTV_MTU is overloaded, since the admin can set it
	 *	  to turn off PMTU for a route, and the kernel can
	 *	  set it to indicate a serious problem with PMTU
	 *	  on a route.  We should be using a separate flag
	 *	  for the kernel to indicate this.
	 */
	if ((rt->rt_locks & RTV_MTU) == 0) {
		if (mtu < 296 || mtu > ifp->if_mtu)
			rt->rt_locks |= RTV_MTU;
		else if (rtmtu > mtu || rtmtu == 0)
			atomic_cas_uint(&rt->rt_mtu, rtmtu, mtu);
	}

	if_put(ifp);
	rtfree(rt);
}

void
icmp_mtudisc_timeout(struct rtentry *rt, u_int rtableid)
{
	struct ifnet *ifp;

	NET_ASSERT_LOCKED();

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL)
		return;

	if ((rt->rt_flags & (RTF_DYNAMIC|RTF_HOST)) == (RTF_DYNAMIC|RTF_HOST)) {
		void (*ctlfunc)(int, struct sockaddr *, u_int, void *);
		struct sockaddr_in sin;

		sin = *satosin(rt_key(rt));

		rtdeletemsg(rt, ifp, rtableid);

		/* Notify TCP layer of increased Path MTU estimate */
		ctlfunc = inetsw[ip_protox[IPPROTO_TCP]].pr_ctlinput;
		if (ctlfunc)
			(*ctlfunc)(PRC_MTUINC, sintosa(&sin),
			    rtableid, NULL);
	} else {
		if ((rt->rt_locks & RTV_MTU) == 0)
			atomic_store_int(&rt->rt_mtu, 0);
	}

	if_put(ifp);
}

/*
 * Perform rate limit check.
 * Returns 0 if it is okay to send the icmp packet.
 * Returns 1 if the router SHOULD NOT send this icmp packet due to rate
 * limitation.
 *
 * XXX per-destination/type check necessary?
 */
int
icmp_ratelimit(const struct in_addr *dst, const int type, const int code)
{
	int icmperrppslim_local = atomic_load_int(&icmperrppslim);
	/* PPS limit */
	if (!ppsratecheck(&icmperrppslim_last, &icmperrpps_count,
	    icmperrppslim_local))
		return 1;	/* The packet is subject to rate limit */
	return 0;	/* okay to send */
}

int
icmp_do_exthdr(struct mbuf *m, u_int16_t class, u_int8_t ctype, void *buf,
    size_t len)
{
	struct ip *ip = mtod(m, struct ip *);
	int hlen, off;
	struct mbuf *n;
	struct icmp *icp;
	struct icmp_ext_hdr *ieh;
	struct {
		struct icmp_ext_hdr	ieh;
		struct icmp_ext_obj_hdr	ieo;
	} hdr;

	hlen = ip->ip_hl << 2;
	icp = (struct icmp *)(mtod(m, caddr_t) + hlen);
	if (icp->icmp_type != ICMP_TIMXCEED && icp->icmp_type != ICMP_UNREACH &&
	    icp->icmp_type != ICMP_PARAMPROB)
		/* exthdr not supported */
		return (0);

	if (icp->icmp_length != 0)
		/* exthdr already present, giving up */
		return (0);

	/* the actual offset starts after the common ICMP header */
	hlen += ICMP_MINLEN;
	/* exthdr must start on a word boundary */
	off = roundup(ntohs(ip->ip_len) - hlen, sizeof(u_int32_t));
	/* ... and at an offset of ICMP_EXT_OFFSET or bigger */
	off = max(off, ICMP_EXT_OFFSET);
	icp->icmp_length = off / sizeof(u_int32_t);

	memset(&hdr, 0, sizeof(hdr));
	hdr.ieh.ieh_version = ICMP_EXT_HDR_VERSION;
	hdr.ieo.ieo_length = htons(sizeof(struct icmp_ext_obj_hdr) + len);
	hdr.ieo.ieo_cnum = class;
	hdr.ieo.ieo_ctype = ctype;

	if (m_copyback(m, hlen + off, sizeof(hdr), &hdr, M_NOWAIT) ||
	    m_copyback(m, hlen + off + sizeof(hdr), len, buf, M_NOWAIT)) {
		m_freem(m);
		return (ENOBUFS);
	}

	/* calculate checksum */
	n = m_getptr(m, hlen + off, &off);
	if (n == NULL)
		panic("icmp_do_exthdr: m_getptr failure");
	ieh = (struct icmp_ext_hdr *)(mtod(n, caddr_t) + off);
	ieh->ieh_cksum = in4_cksum(n, 0, off, sizeof(hdr) + len);

	ip->ip_len = htons(m->m_pkthdr.len);

	return (0);
}
