/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1995
 *	The Regents of the University of California.
 * Copyright (c) 2008 Robert N. M. Watson
 * Copyright (c) 2010-2011 Juniper Networks, Inc.
 * Copyright (c) 2014 Kevin Lo
 * All rights reserved.
 *
 * Portions of this software were developed by Robert N. M. Watson under
 * contract to Juniper Networks, Inc.
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
 *	@(#)udp_usrreq.c	8.6 (Berkeley) 5/23/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/domain.h>
#include <sys/eventhandler.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/sdt.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systm.h>

#include <vm/uma.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/rss_config.h>

#include <netinet/in.h>
#include <netinet/in_kdtrace.h>
#include <netinet/in_pcb.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_options.h>
#ifdef INET6
#include <netinet6/ip6_var.h>
#endif
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/udplite.h>
#include <netinet/in_rss.h>

#include <netipsec/ipsec_support.h>

#include <machine/in_cksum.h>

#include <security/mac/mac_framework.h>

/*
 * UDP and UDP-Lite protocols implementation.
 * Per RFC 768, August, 1980.
 * Per RFC 3828, July, 2004.
 */

/*
 * BSD 4.2 defaulted the udp checksum to be off.  Turning off udp checksums
 * removes the only data integrity mechanism for packets and malformed
 * packets that would otherwise be discarded due to bad checksums, and may
 * cause problems (especially for NFS data blocks).
 */
VNET_DEFINE(int, udp_cksum) = 1;
SYSCTL_INT(_net_inet_udp, UDPCTL_CHECKSUM, checksum, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(udp_cksum), 0, "compute udp checksum");

int	udp_log_in_vain = 0;
SYSCTL_INT(_net_inet_udp, OID_AUTO, log_in_vain, CTLFLAG_RW,
    &udp_log_in_vain, 0, "Log all incoming UDP packets");

VNET_DEFINE(int, udp_blackhole) = 0;
SYSCTL_INT(_net_inet_udp, OID_AUTO, blackhole, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(udp_blackhole), 0,
    "Do not send port unreachables for refused connects");

u_long	udp_sendspace = 9216;		/* really max datagram size */
SYSCTL_ULONG(_net_inet_udp, UDPCTL_MAXDGRAM, maxdgram, CTLFLAG_RW,
    &udp_sendspace, 0, "Maximum outgoing UDP datagram size");

u_long	udp_recvspace = 40 * (1024 +
#ifdef INET6
				      sizeof(struct sockaddr_in6)
#else
				      sizeof(struct sockaddr_in)
#endif
				      );	/* 40 1K datagrams */

SYSCTL_ULONG(_net_inet_udp, UDPCTL_RECVSPACE, recvspace, CTLFLAG_RW,
    &udp_recvspace, 0, "Maximum space for incoming UDP datagrams");

VNET_DEFINE(struct inpcbhead, udb);		/* from udp_var.h */
VNET_DEFINE(struct inpcbinfo, udbinfo);
VNET_DEFINE(struct inpcbhead, ulitecb);
VNET_DEFINE(struct inpcbinfo, ulitecbinfo);
VNET_DEFINE_STATIC(uma_zone_t, udpcb_zone);
#define	V_udpcb_zone			VNET(udpcb_zone)

#ifndef UDBHASHSIZE
#define	UDBHASHSIZE	128
#endif

VNET_PCPUSTAT_DEFINE(struct udpstat, udpstat);		/* from udp_var.h */
VNET_PCPUSTAT_SYSINIT(udpstat);
SYSCTL_VNET_PCPUSTAT(_net_inet_udp, UDPCTL_STATS, stats, struct udpstat,
    udpstat, "UDP statistics (struct udpstat, netinet/udp_var.h)");

#ifdef VIMAGE
VNET_PCPUSTAT_SYSUNINIT(udpstat);
#endif /* VIMAGE */
#ifdef INET
static void	udp_detach(struct socket *so);
static int	udp_output(struct inpcb *, struct mbuf *, struct sockaddr *,
		    struct mbuf *, struct thread *);
#endif

static void
udp_zone_change(void *tag)
{

	uma_zone_set_max(V_udbinfo.ipi_zone, maxsockets);
	uma_zone_set_max(V_udpcb_zone, maxsockets);
}

static int
udp_inpcb_init(void *mem, int size, int flags)
{
	struct inpcb *inp;

	inp = mem;
	INP_LOCK_INIT(inp, "inp", "udpinp");
	return (0);
}

static int
udplite_inpcb_init(void *mem, int size, int flags)
{
	struct inpcb *inp;

	inp = mem;
	INP_LOCK_INIT(inp, "inp", "udpliteinp");
	return (0);
}

void
udp_init(void)
{

	/*
	 * For now default to 2-tuple UDP hashing - until the fragment
	 * reassembly code can also update the flowid.
	 *
	 * Once we can calculate the flowid that way and re-establish
	 * a 4-tuple, flip this to 4-tuple.
	 */
	in_pcbinfo_init(&V_udbinfo, "udp", &V_udb, UDBHASHSIZE, UDBHASHSIZE,
	    "udp_inpcb", udp_inpcb_init, IPI_HASHFIELDS_2TUPLE);
	V_udpcb_zone = uma_zcreate("udpcb", sizeof(struct udpcb),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	uma_zone_set_max(V_udpcb_zone, maxsockets);
	uma_zone_set_warning(V_udpcb_zone, "kern.ipc.maxsockets limit reached");
	EVENTHANDLER_REGISTER(maxsockets_change, udp_zone_change, NULL,
	    EVENTHANDLER_PRI_ANY);
}

void
udplite_init(void)
{

	in_pcbinfo_init(&V_ulitecbinfo, "udplite", &V_ulitecb, UDBHASHSIZE,
	    UDBHASHSIZE, "udplite_inpcb", udplite_inpcb_init,
	    IPI_HASHFIELDS_2TUPLE);
}

/*
 * Kernel module interface for updating udpstat.  The argument is an index
 * into udpstat treated as an array of u_long.  While this encodes the
 * general layout of udpstat into the caller, it doesn't encode its location,
 * so that future changes to add, for example, per-CPU stats support won't
 * cause binary compatibility problems for kernel modules.
 */
void
kmod_udpstat_inc(int statnum)
{

	counter_u64_add(VNET(udpstat)[statnum], 1);
}

int
udp_newudpcb(struct inpcb *inp)
{
	struct udpcb *up;

	up = uma_zalloc(V_udpcb_zone, M_NOWAIT | M_ZERO);
	if (up == NULL)
		return (ENOBUFS);
	inp->inp_ppcb = up;
	return (0);
}

void
udp_discardcb(struct udpcb *up)
{

	uma_zfree(V_udpcb_zone, up);
}

#ifdef VIMAGE
static void
udp_destroy(void *unused __unused)
{

	in_pcbinfo_destroy(&V_udbinfo);
	uma_zdestroy(V_udpcb_zone);
}
VNET_SYSUNINIT(udp, SI_SUB_PROTO_DOMAIN, SI_ORDER_FOURTH, udp_destroy, NULL);

static void
udplite_destroy(void *unused __unused)
{

	in_pcbinfo_destroy(&V_ulitecbinfo);
}
VNET_SYSUNINIT(udplite, SI_SUB_PROTO_DOMAIN, SI_ORDER_FOURTH, udplite_destroy,
    NULL);
#endif

#ifdef INET
/*
 * Subroutine of udp_input(), which appends the provided mbuf chain to the
 * passed pcb/socket.  The caller must provide a sockaddr_in via udp_in that
 * contains the source address.  If the socket ends up being an IPv6 socket,
 * udp_append() will convert to a sockaddr_in6 before passing the address
 * into the socket code.
 *
 * In the normal case udp_append() will return 0, indicating that you
 * must unlock the inp. However if a tunneling protocol is in place we increment
 * the inpcb refcnt and unlock the inp, on return from the tunneling protocol we
 * then decrement the reference count. If the inp_rele returns 1, indicating the
 * inp is gone, we return that to the caller to tell them *not* to unlock
 * the inp. In the case of multi-cast this will cause the distribution
 * to stop (though most tunneling protocols known currently do *not* use
 * multicast).
 */
static int
udp_append(struct inpcb *inp, struct ip *ip, struct mbuf *n, int off,
    struct sockaddr_in *udp_in)
{
	struct sockaddr *append_sa;
	struct socket *so;
	struct mbuf *tmpopts, *opts = NULL;
#ifdef INET6
	struct sockaddr_in6 udp_in6;
#endif
	struct udpcb *up;

	INP_LOCK_ASSERT(inp);

	/*
	 * Engage the tunneling protocol.
	 */
	up = intoudpcb(inp);
	if (up->u_tun_func != NULL) {
		in_pcbref(inp);
		INP_RUNLOCK(inp);
		(*up->u_tun_func)(n, off, inp, (struct sockaddr *)&udp_in[0],
		    up->u_tun_ctx);
		INP_RLOCK(inp);
		return (in_pcbrele_rlocked(inp));
	}

	off += sizeof(struct udphdr);

#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	/* Check AH/ESP integrity. */
	if (IPSEC_ENABLED(ipv4) &&
	    IPSEC_CHECK_POLICY(ipv4, n, inp) != 0) {
		m_freem(n);
		return (0);
	}
	if (up->u_flags & UF_ESPINUDP) {/* IPSec UDP encaps. */
		if (IPSEC_ENABLED(ipv4) &&
		    UDPENCAP_INPUT(n, off, AF_INET) != 0)
			return (0);	/* Consumed. */
	}
#endif /* IPSEC */
#ifdef MAC
	if (mac_inpcb_check_deliver(inp, n) != 0) {
		m_freem(n);
		return (0);
	}
#endif /* MAC */
	if (inp->inp_flags & INP_CONTROLOPTS ||
	    inp->inp_socket->so_options & (SO_TIMESTAMP | SO_BINTIME)) {
#ifdef INET6
		if (inp->inp_vflag & INP_IPV6)
			(void)ip6_savecontrol_v4(inp, n, &opts, NULL);
		else
#endif /* INET6 */
			ip_savecontrol(inp, &opts, ip, n);
	}
	if ((inp->inp_vflag & INP_IPV4) && (inp->inp_flags2 & INP_ORIGDSTADDR)) {
		tmpopts = sbcreatecontrol((caddr_t)&udp_in[1],
			sizeof(struct sockaddr_in), IP_ORIGDSTADDR, IPPROTO_IP);
		if (tmpopts) {
			if (opts) {
				tmpopts->m_next = opts;
				opts = tmpopts;
			} else
				opts = tmpopts;
		}
	}
#ifdef INET6
	if (inp->inp_vflag & INP_IPV6) {
		bzero(&udp_in6, sizeof(udp_in6));
		udp_in6.sin6_len = sizeof(udp_in6);
		udp_in6.sin6_family = AF_INET6;
		in6_sin_2_v4mapsin6(&udp_in[0], &udp_in6);
		append_sa = (struct sockaddr *)&udp_in6;
	} else
#endif /* INET6 */
		append_sa = (struct sockaddr *)&udp_in[0];
	m_adj(n, off);

	so = inp->inp_socket;
	SOCKBUF_LOCK(&so->so_rcv);
	if (sbappendaddr_locked(&so->so_rcv, append_sa, n, opts) == 0) {
		SOCKBUF_UNLOCK(&so->so_rcv);
		m_freem(n);
		if (opts)
			m_freem(opts);
		UDPSTAT_INC(udps_fullsock);
	} else
		sorwakeup_locked(so);
	return (0);
}

int
udp_input(struct mbuf **mp, int *offp, int proto)
{
	struct ip *ip;
	struct udphdr *uh;
	struct ifnet *ifp;
	struct inpcb *inp;
	uint16_t len, ip_len;
	struct inpcbinfo *pcbinfo;
	struct ip save_ip;
	struct sockaddr_in udp_in[2];
	struct mbuf *m;
	struct m_tag *fwd_tag;
	struct epoch_tracker et;
	int cscov_partial, iphlen;

	m = *mp;
	iphlen = *offp;
	ifp = m->m_pkthdr.rcvif;
	*mp = NULL;
	UDPSTAT_INC(udps_ipackets);

	/*
	 * Strip IP options, if any; should skip this, make available to
	 * user, and use on returned packets, but we don't yet have a way to
	 * check the checksum with options still present.
	 */
	if (iphlen > sizeof (struct ip)) {
		ip_stripoptions(m);
		iphlen = sizeof(struct ip);
	}

	/*
	 * Get IP and UDP header together in first mbuf.
	 */
	ip = mtod(m, struct ip *);
	if (m->m_len < iphlen + sizeof(struct udphdr)) {
		if ((m = m_pullup(m, iphlen + sizeof(struct udphdr))) == NULL) {
			UDPSTAT_INC(udps_hdrops);
			return (IPPROTO_DONE);
		}
		ip = mtod(m, struct ip *);
	}
	uh = (struct udphdr *)((caddr_t)ip + iphlen);
	cscov_partial = (proto == IPPROTO_UDPLITE) ? 1 : 0;

	/*
	 * Destination port of 0 is illegal, based on RFC768.
	 */
	if (uh->uh_dport == 0)
		goto badunlocked;

	/*
	 * Construct sockaddr format source address.  Stuff source address
	 * and datagram in user buffer.
	 */
	bzero(&udp_in[0], sizeof(struct sockaddr_in) * 2);
	udp_in[0].sin_len = sizeof(struct sockaddr_in);
	udp_in[0].sin_family = AF_INET;
	udp_in[0].sin_port = uh->uh_sport;
	udp_in[0].sin_addr = ip->ip_src;
	udp_in[1].sin_len = sizeof(struct sockaddr_in);
	udp_in[1].sin_family = AF_INET;
	udp_in[1].sin_port = uh->uh_dport;
	udp_in[1].sin_addr = ip->ip_dst;

	/*
	 * Make mbuf data length reflect UDP length.  If not enough data to
	 * reflect UDP length, drop.
	 */
	len = ntohs((u_short)uh->uh_ulen);
	ip_len = ntohs(ip->ip_len) - iphlen;
	if (proto == IPPROTO_UDPLITE && (len == 0 || len == ip_len)) {
		/* Zero means checksum over the complete packet. */
		if (len == 0)
			len = ip_len;
		cscov_partial = 0;
	}
	if (ip_len != len) {
		if (len > ip_len || len < sizeof(struct udphdr)) {
			UDPSTAT_INC(udps_badlen);
			goto badunlocked;
		}
		if (proto == IPPROTO_UDP)
			m_adj(m, len - ip_len);
	}

	/*
	 * Save a copy of the IP header in case we want restore it for
	 * sending an ICMP error message in response.
	 */
	if (!V_udp_blackhole)
		save_ip = *ip;
	else
		memset(&save_ip, 0, sizeof(save_ip));

	/*
	 * Checksum extended UDP header and data.
	 */
	if (uh->uh_sum) {
		u_short uh_sum;

		if ((m->m_pkthdr.csum_flags & CSUM_DATA_VALID) &&
		    !cscov_partial) {
			if (m->m_pkthdr.csum_flags & CSUM_PSEUDO_HDR)
				uh_sum = m->m_pkthdr.csum_data;
			else
				uh_sum = in_pseudo(ip->ip_src.s_addr,
				    ip->ip_dst.s_addr, htonl((u_short)len +
				    m->m_pkthdr.csum_data + proto));
			uh_sum ^= 0xffff;
		} else {
			char b[9];

			bcopy(((struct ipovly *)ip)->ih_x1, b, 9);
			bzero(((struct ipovly *)ip)->ih_x1, 9);
			((struct ipovly *)ip)->ih_len = (proto == IPPROTO_UDP) ?
			    uh->uh_ulen : htons(ip_len);
			uh_sum = in_cksum(m, len + sizeof (struct ip));
			bcopy(b, ((struct ipovly *)ip)->ih_x1, 9);
		}
		if (uh_sum) {
			UDPSTAT_INC(udps_badsum);
			m_freem(m);
			return (IPPROTO_DONE);
		}
	} else {
		if (proto == IPPROTO_UDP) {
			UDPSTAT_INC(udps_nosum);
		} else {
			/* UDPLite requires a checksum */
			/* XXX: What is the right UDPLite MIB counter here? */
			m_freem(m);
			return (IPPROTO_DONE);
		}
	}

	pcbinfo = udp_get_inpcbinfo(proto);
	if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) ||
	    in_broadcast(ip->ip_dst, ifp)) {
		struct inpcb *last;
		struct inpcbhead *pcblist;

		INP_INFO_RLOCK_ET(pcbinfo, et);
		pcblist = udp_get_pcblist(proto);
		last = NULL;
		CK_LIST_FOREACH(inp, pcblist, inp_list) {
			if (inp->inp_lport != uh->uh_dport)
				continue;
#ifdef INET6
			if ((inp->inp_vflag & INP_IPV4) == 0)
				continue;
#endif
			if (inp->inp_laddr.s_addr != INADDR_ANY &&
			    inp->inp_laddr.s_addr != ip->ip_dst.s_addr)
				continue;
			if (inp->inp_faddr.s_addr != INADDR_ANY &&
			    inp->inp_faddr.s_addr != ip->ip_src.s_addr)
				continue;
			if (inp->inp_fport != 0 &&
			    inp->inp_fport != uh->uh_sport)
				continue;

			INP_RLOCK(inp);

			if (__predict_false(inp->inp_flags2 & INP_FREED)) {
				INP_RUNLOCK(inp);
				continue;
			}

			/*
			 * XXXRW: Because we weren't holding either the inpcb
			 * or the hash lock when we checked for a match
			 * before, we should probably recheck now that the
			 * inpcb lock is held.
			 */

			/*
			 * Handle socket delivery policy for any-source
			 * and source-specific multicast. [RFC3678]
			 */
			if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr))) {
				struct ip_moptions	*imo;
				struct sockaddr_in	 group;
				int			 blocked;

				imo = inp->inp_moptions;
				if (imo == NULL) {
					INP_RUNLOCK(inp);
					continue;
				}
				bzero(&group, sizeof(struct sockaddr_in));
				group.sin_len = sizeof(struct sockaddr_in);
				group.sin_family = AF_INET;
				group.sin_addr = ip->ip_dst;

				blocked = imo_multi_filter(imo, ifp,
					(struct sockaddr *)&group,
					(struct sockaddr *)&udp_in[0]);
				if (blocked != MCAST_PASS) {
					if (blocked == MCAST_NOTGMEMBER)
						IPSTAT_INC(ips_notmember);
					if (blocked == MCAST_NOTSMEMBER ||
					    blocked == MCAST_MUTED)
						UDPSTAT_INC(udps_filtermcast);
					INP_RUNLOCK(inp);
					continue;
				}
			}
			if (last != NULL) {
				struct mbuf *n;

				if ((n = m_copym(m, 0, M_COPYALL, M_NOWAIT)) !=
				    NULL) {
					if (proto == IPPROTO_UDPLITE)
						UDPLITE_PROBE(receive, NULL, last, ip,
						    last, uh);
					else
						UDP_PROBE(receive, NULL, last, ip, last,
						    uh);
					if (udp_append(last, ip, n, iphlen,
						udp_in)) {
						goto inp_lost;
					}
				}
				INP_RUNLOCK(last);
			}
			last = inp;
			/*
			 * Don't look for additional matches if this one does
			 * not have either the SO_REUSEPORT or SO_REUSEADDR
			 * socket options set.  This heuristic avoids
			 * searching through all pcbs in the common case of a
			 * non-shared port.  It assumes that an application
			 * will never clear these options after setting them.
			 */
			if ((last->inp_socket->so_options &
			    (SO_REUSEPORT|SO_REUSEPORT_LB|SO_REUSEADDR)) == 0)
				break;
		}

		if (last == NULL) {
			/*
			 * No matching pcb found; discard datagram.  (No need
			 * to send an ICMP Port Unreachable for a broadcast
			 * or multicast datgram.)
			 */
			UDPSTAT_INC(udps_noportbcast);
			if (inp)
				INP_RUNLOCK(inp);
			INP_INFO_RUNLOCK_ET(pcbinfo, et);
			goto badunlocked;
		}
		if (proto == IPPROTO_UDPLITE)
			UDPLITE_PROBE(receive, NULL, last, ip, last, uh);
		else
			UDP_PROBE(receive, NULL, last, ip, last, uh);
		if (udp_append(last, ip, m, iphlen, udp_in) == 0) 
			INP_RUNLOCK(last);
	inp_lost:
		INP_INFO_RUNLOCK_ET(pcbinfo, et);
		return (IPPROTO_DONE);
	}

	/*
	 * Locate pcb for datagram.
	 */

	/*
	 * Grab info from PACKET_TAG_IPFORWARD tag prepended to the chain.
	 */
	if ((m->m_flags & M_IP_NEXTHOP) &&
	    (fwd_tag = m_tag_find(m, PACKET_TAG_IPFORWARD, NULL)) != NULL) {
		struct sockaddr_in *next_hop;

		next_hop = (struct sockaddr_in *)(fwd_tag + 1);

		/*
		 * Transparently forwarded. Pretend to be the destination.
		 * Already got one like this?
		 */
		inp = in_pcblookup_mbuf(pcbinfo, ip->ip_src, uh->uh_sport,
		    ip->ip_dst, uh->uh_dport, INPLOOKUP_RLOCKPCB, ifp, m);
		if (!inp) {
			/*
			 * It's new.  Try to find the ambushing socket.
			 * Because we've rewritten the destination address,
			 * any hardware-generated hash is ignored.
			 */
			inp = in_pcblookup(pcbinfo, ip->ip_src,
			    uh->uh_sport, next_hop->sin_addr,
			    next_hop->sin_port ? htons(next_hop->sin_port) :
			    uh->uh_dport, INPLOOKUP_WILDCARD |
			    INPLOOKUP_RLOCKPCB, ifp);
		}
		/* Remove the tag from the packet. We don't need it anymore. */
		m_tag_delete(m, fwd_tag);
		m->m_flags &= ~M_IP_NEXTHOP;
	} else
		inp = in_pcblookup_mbuf(pcbinfo, ip->ip_src, uh->uh_sport,
		    ip->ip_dst, uh->uh_dport, INPLOOKUP_WILDCARD |
		    INPLOOKUP_RLOCKPCB, ifp, m);
	if (inp == NULL) {
		if (udp_log_in_vain) {
			char src[INET_ADDRSTRLEN];
			char dst[INET_ADDRSTRLEN];

			log(LOG_INFO,
			    "Connection attempt to UDP %s:%d from %s:%d\n",
			    inet_ntoa_r(ip->ip_dst, dst), ntohs(uh->uh_dport),
			    inet_ntoa_r(ip->ip_src, src), ntohs(uh->uh_sport));
		}
		if (proto == IPPROTO_UDPLITE)
			UDPLITE_PROBE(receive, NULL, NULL, ip, NULL, uh);
		else
			UDP_PROBE(receive, NULL, NULL, ip, NULL, uh);
		UDPSTAT_INC(udps_noport);
		if (m->m_flags & (M_BCAST | M_MCAST)) {
			UDPSTAT_INC(udps_noportbcast);
			goto badunlocked;
		}
		if (V_udp_blackhole)
			goto badunlocked;
		if (badport_bandlim(BANDLIM_ICMP_UNREACH) < 0)
			goto badunlocked;
		*ip = save_ip;
		icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_PORT, 0, 0);
		return (IPPROTO_DONE);
	}

	/*
	 * Check the minimum TTL for socket.
	 */
	INP_RLOCK_ASSERT(inp);
	if (inp->inp_ip_minttl && inp->inp_ip_minttl > ip->ip_ttl) {
		if (proto == IPPROTO_UDPLITE)
			UDPLITE_PROBE(receive, NULL, inp, ip, inp, uh);
		else
			UDP_PROBE(receive, NULL, inp, ip, inp, uh);
		INP_RUNLOCK(inp);
		m_freem(m);
		return (IPPROTO_DONE);
	}
	if (cscov_partial) {
		struct udpcb *up;

		up = intoudpcb(inp);
		if (up->u_rxcslen == 0 || up->u_rxcslen > len) {
			INP_RUNLOCK(inp);
			m_freem(m);
			return (IPPROTO_DONE);
		}
	}

	if (proto == IPPROTO_UDPLITE)
		UDPLITE_PROBE(receive, NULL, inp, ip, inp, uh);
	else
		UDP_PROBE(receive, NULL, inp, ip, inp, uh);
	if (udp_append(inp, ip, m, iphlen, udp_in) == 0) 
		INP_RUNLOCK(inp);
	return (IPPROTO_DONE);

badunlocked:
	m_freem(m);
	return (IPPROTO_DONE);
}
#endif /* INET */

/*
 * Notify a udp user of an asynchronous error; just wake up so that they can
 * collect error status.
 */
struct inpcb *
udp_notify(struct inpcb *inp, int errno)
{

	INP_WLOCK_ASSERT(inp);
	if ((errno == EHOSTUNREACH || errno == ENETUNREACH ||
	     errno == EHOSTDOWN) && inp->inp_route.ro_rt) {
		RTFREE(inp->inp_route.ro_rt);
		inp->inp_route.ro_rt = (struct rtentry *)NULL;
	}

	inp->inp_socket->so_error = errno;
	sorwakeup(inp->inp_socket);
	sowwakeup(inp->inp_socket);
	return (inp);
}

#ifdef INET
static void
udp_common_ctlinput(int cmd, struct sockaddr *sa, void *vip,
    struct inpcbinfo *pcbinfo)
{
	struct ip *ip = vip;
	struct udphdr *uh;
	struct in_addr faddr;
	struct inpcb *inp;

	faddr = ((struct sockaddr_in *)sa)->sin_addr;
	if (sa->sa_family != AF_INET || faddr.s_addr == INADDR_ANY)
		return;

	if (PRC_IS_REDIRECT(cmd)) {
		/* signal EHOSTDOWN, as it flushes the cached route */
		in_pcbnotifyall(&V_udbinfo, faddr, EHOSTDOWN, udp_notify);
		return;
	}

	/*
	 * Hostdead is ugly because it goes linearly through all PCBs.
	 *
	 * XXX: We never get this from ICMP, otherwise it makes an excellent
	 * DoS attack on machines with many connections.
	 */
	if (cmd == PRC_HOSTDEAD)
		ip = NULL;
	else if ((unsigned)cmd >= PRC_NCMDS || inetctlerrmap[cmd] == 0)
		return;
	if (ip != NULL) {
		uh = (struct udphdr *)((caddr_t)ip + (ip->ip_hl << 2));
		inp = in_pcblookup(pcbinfo, faddr, uh->uh_dport,
		    ip->ip_src, uh->uh_sport, INPLOOKUP_WLOCKPCB, NULL);
		if (inp != NULL) {
			INP_WLOCK_ASSERT(inp);
			if (inp->inp_socket != NULL) {
				udp_notify(inp, inetctlerrmap[cmd]);
			}
			INP_WUNLOCK(inp);
		} else {
			inp = in_pcblookup(pcbinfo, faddr, uh->uh_dport,
					   ip->ip_src, uh->uh_sport,
					   INPLOOKUP_WILDCARD | INPLOOKUP_RLOCKPCB, NULL);
			if (inp != NULL) {
				struct udpcb *up;
				void *ctx;
				udp_tun_icmp_t func;

				up = intoudpcb(inp);
				ctx = up->u_tun_ctx;
				func = up->u_icmp_func;
				INP_RUNLOCK(inp);
				if (func != NULL)
					(*func)(cmd, sa, vip, ctx);
			}
		}
	} else
		in_pcbnotifyall(pcbinfo, faddr, inetctlerrmap[cmd],
		    udp_notify);
}
void
udp_ctlinput(int cmd, struct sockaddr *sa, void *vip)
{

	return (udp_common_ctlinput(cmd, sa, vip, &V_udbinfo));
}

void
udplite_ctlinput(int cmd, struct sockaddr *sa, void *vip)
{

	return (udp_common_ctlinput(cmd, sa, vip, &V_ulitecbinfo));
}
#endif /* INET */

static int
udp_pcblist(SYSCTL_HANDLER_ARGS)
{
	int error, i, n;
	struct inpcb *inp, **inp_list;
	inp_gen_t gencnt;
	struct xinpgen xig;
	struct epoch_tracker et;

	/*
	 * The process of preparing the PCB list is too time-consuming and
	 * resource-intensive to repeat twice on every request.
	 */
	if (req->oldptr == 0) {
		n = V_udbinfo.ipi_count;
		n += imax(n / 8, 10);
		req->oldidx = 2 * (sizeof xig) + n * sizeof(struct xinpcb);
		return (0);
	}

	if (req->newptr != 0)
		return (EPERM);

	/*
	 * OK, now we're committed to doing something.
	 */
	INP_INFO_RLOCK_ET(&V_udbinfo, et);
	gencnt = V_udbinfo.ipi_gencnt;
	n = V_udbinfo.ipi_count;
	INP_INFO_RUNLOCK_ET(&V_udbinfo, et);

	error = sysctl_wire_old_buffer(req, 2 * (sizeof xig)
		+ n * sizeof(struct xinpcb));
	if (error != 0)
		return (error);

	bzero(&xig, sizeof(xig));
	xig.xig_len = sizeof xig;
	xig.xig_count = n;
	xig.xig_gen = gencnt;
	xig.xig_sogen = so_gencnt;
	error = SYSCTL_OUT(req, &xig, sizeof xig);
	if (error)
		return (error);

	inp_list = malloc(n * sizeof *inp_list, M_TEMP, M_WAITOK);
	if (inp_list == NULL)
		return (ENOMEM);

	INP_INFO_RLOCK_ET(&V_udbinfo, et);
	for (inp = CK_LIST_FIRST(V_udbinfo.ipi_listhead), i = 0; inp && i < n;
	     inp = CK_LIST_NEXT(inp, inp_list)) {
		INP_WLOCK(inp);
		if (inp->inp_gencnt <= gencnt &&
		    cr_canseeinpcb(req->td->td_ucred, inp) == 0) {
			in_pcbref(inp);
			inp_list[i++] = inp;
		}
		INP_WUNLOCK(inp);
	}
	INP_INFO_RUNLOCK_ET(&V_udbinfo, et);
	n = i;

	error = 0;
	for (i = 0; i < n; i++) {
		inp = inp_list[i];
		INP_RLOCK(inp);
		if (inp->inp_gencnt <= gencnt) {
			struct xinpcb xi;

			in_pcbtoxinpcb(inp, &xi);
			INP_RUNLOCK(inp);
			error = SYSCTL_OUT(req, &xi, sizeof xi);
		} else
			INP_RUNLOCK(inp);
	}
	INP_INFO_WLOCK(&V_udbinfo);
	for (i = 0; i < n; i++) {
		inp = inp_list[i];
		INP_RLOCK(inp);
		if (!in_pcbrele_rlocked(inp))
			INP_RUNLOCK(inp);
	}
	INP_INFO_WUNLOCK(&V_udbinfo);

	if (!error) {
		/*
		 * Give the user an updated idea of our state.  If the
		 * generation differs from what we told her before, she knows
		 * that something happened while we were processing this
		 * request, and it might be necessary to retry.
		 */
		INP_INFO_RLOCK_ET(&V_udbinfo, et);
		xig.xig_gen = V_udbinfo.ipi_gencnt;
		xig.xig_sogen = so_gencnt;
		xig.xig_count = V_udbinfo.ipi_count;
		INP_INFO_RUNLOCK_ET(&V_udbinfo, et);
		error = SYSCTL_OUT(req, &xig, sizeof xig);
	}
	free(inp_list, M_TEMP);
	return (error);
}

SYSCTL_PROC(_net_inet_udp, UDPCTL_PCBLIST, pcblist,
    CTLTYPE_OPAQUE | CTLFLAG_RD, NULL, 0,
    udp_pcblist, "S,xinpcb", "List of active UDP sockets");

#ifdef INET
static int
udp_getcred(SYSCTL_HANDLER_ARGS)
{
	struct xucred xuc;
	struct sockaddr_in addrs[2];
	struct inpcb *inp;
	int error;

	error = priv_check(req->td, PRIV_NETINET_GETCRED);
	if (error)
		return (error);
	error = SYSCTL_IN(req, addrs, sizeof(addrs));
	if (error)
		return (error);
	inp = in_pcblookup(&V_udbinfo, addrs[1].sin_addr, addrs[1].sin_port,
	    addrs[0].sin_addr, addrs[0].sin_port,
	    INPLOOKUP_WILDCARD | INPLOOKUP_RLOCKPCB, NULL);
	if (inp != NULL) {
		INP_RLOCK_ASSERT(inp);
		if (inp->inp_socket == NULL)
			error = ENOENT;
		if (error == 0)
			error = cr_canseeinpcb(req->td->td_ucred, inp);
		if (error == 0)
			cru2x(inp->inp_cred, &xuc);
		INP_RUNLOCK(inp);
	} else
		error = ENOENT;
	if (error == 0)
		error = SYSCTL_OUT(req, &xuc, sizeof(struct xucred));
	return (error);
}

SYSCTL_PROC(_net_inet_udp, OID_AUTO, getcred,
    CTLTYPE_OPAQUE|CTLFLAG_RW|CTLFLAG_PRISON, 0, 0,
    udp_getcred, "S,xucred", "Get the xucred of a UDP connection");
#endif /* INET */

int
udp_ctloutput(struct socket *so, struct sockopt *sopt)
{
	struct inpcb *inp;
	struct udpcb *up;
	int isudplite, error, optval;

	error = 0;
	isudplite = (so->so_proto->pr_protocol == IPPROTO_UDPLITE) ? 1 : 0;
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("%s: inp == NULL", __func__));
	INP_WLOCK(inp);
	if (sopt->sopt_level != so->so_proto->pr_protocol) {
#ifdef INET6
		if (INP_CHECK_SOCKAF(so, AF_INET6)) {
			INP_WUNLOCK(inp);
			error = ip6_ctloutput(so, sopt);
		}
#endif
#if defined(INET) && defined(INET6)
		else
#endif
#ifdef INET
		{
			INP_WUNLOCK(inp);
			error = ip_ctloutput(so, sopt);
		}
#endif
		return (error);
	}

	switch (sopt->sopt_dir) {
	case SOPT_SET:
		switch (sopt->sopt_name) {
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
#ifdef INET
		case UDP_ENCAP:
			if (!IPSEC_ENABLED(ipv4)) {
				INP_WUNLOCK(inp);
				return (ENOPROTOOPT);
			}
			error = UDPENCAP_PCBCTL(inp, sopt);
			break;
#endif /* INET */
#endif /* IPSEC */
		case UDPLITE_SEND_CSCOV:
		case UDPLITE_RECV_CSCOV:
			if (!isudplite) {
				INP_WUNLOCK(inp);
				error = ENOPROTOOPT;
				break;
			}
			INP_WUNLOCK(inp);
			error = sooptcopyin(sopt, &optval, sizeof(optval),
			    sizeof(optval));
			if (error != 0)
				break;
			inp = sotoinpcb(so);
			KASSERT(inp != NULL, ("%s: inp == NULL", __func__));
			INP_WLOCK(inp);
			up = intoudpcb(inp);
			KASSERT(up != NULL, ("%s: up == NULL", __func__));
			if ((optval != 0 && optval < 8) || (optval > 65535)) {
				INP_WUNLOCK(inp);
				error = EINVAL;
				break;
			}
			if (sopt->sopt_name == UDPLITE_SEND_CSCOV)
				up->u_txcslen = optval;
			else
				up->u_rxcslen = optval;
			INP_WUNLOCK(inp);
			break;
		default:
			INP_WUNLOCK(inp);
			error = ENOPROTOOPT;
			break;
		}
		break;
	case SOPT_GET:
		switch (sopt->sopt_name) {
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
#ifdef INET
		case UDP_ENCAP:
			if (!IPSEC_ENABLED(ipv4)) {
				INP_WUNLOCK(inp);
				return (ENOPROTOOPT);
			}
			error = UDPENCAP_PCBCTL(inp, sopt);
			break;
#endif /* INET */
#endif /* IPSEC */
		case UDPLITE_SEND_CSCOV:
		case UDPLITE_RECV_CSCOV:
			if (!isudplite) {
				INP_WUNLOCK(inp);
				error = ENOPROTOOPT;
				break;
			}
			up = intoudpcb(inp);
			KASSERT(up != NULL, ("%s: up == NULL", __func__));
			if (sopt->sopt_name == UDPLITE_SEND_CSCOV)
				optval = up->u_txcslen;
			else
				optval = up->u_rxcslen;
			INP_WUNLOCK(inp);
			error = sooptcopyout(sopt, &optval, sizeof(optval));
			break;
		default:
			INP_WUNLOCK(inp);
			error = ENOPROTOOPT;
			break;
		}
		break;
	}	
	return (error);
}

#ifdef INET
#define	UH_WLOCKED	2
#define	UH_RLOCKED	1
#define	UH_UNLOCKED	0
static int
udp_output(struct inpcb *inp, struct mbuf *m, struct sockaddr *addr,
    struct mbuf *control, struct thread *td)
{
	struct udpiphdr *ui;
	int len = m->m_pkthdr.len;
	struct in_addr faddr, laddr;
	struct cmsghdr *cm;
	struct inpcbinfo *pcbinfo;
	struct sockaddr_in *sin, src;
	struct epoch_tracker et;
	int cscov_partial = 0;
	int error = 0;
	int ipflags;
	u_short fport, lport;
	int unlock_udbinfo, unlock_inp;
	u_char tos;
	uint8_t pr;
	uint16_t cscov = 0;
	uint32_t flowid = 0;
	uint8_t flowtype = M_HASHTYPE_NONE;

	/*
	 * udp_output() may need to temporarily bind or connect the current
	 * inpcb.  As such, we don't know up front whether we will need the
	 * pcbinfo lock or not.  Do any work to decide what is needed up
	 * front before acquiring any locks.
	 */
	if (len + sizeof(struct udpiphdr) > IP_MAXPACKET) {
		if (control)
			m_freem(control);
		m_freem(m);
		return (EMSGSIZE);
	}

	src.sin_family = 0;
	sin = (struct sockaddr_in *)addr;
	if (sin == NULL ||
	    (inp->inp_laddr.s_addr == INADDR_ANY && inp->inp_lport == 0)) {
		INP_WLOCK(inp);
		unlock_inp = UH_WLOCKED;
	} else {
		INP_RLOCK(inp);
		unlock_inp = UH_RLOCKED;
	}
	tos = inp->inp_ip_tos;
	if (control != NULL) {
		/*
		 * XXX: Currently, we assume all the optional information is
		 * stored in a single mbuf.
		 */
		if (control->m_next) {
			if (unlock_inp == UH_WLOCKED)
				INP_WUNLOCK(inp);
			else
				INP_RUNLOCK(inp);
			m_freem(control);
			m_freem(m);
			return (EINVAL);
		}
		for (; control->m_len > 0;
		    control->m_data += CMSG_ALIGN(cm->cmsg_len),
		    control->m_len -= CMSG_ALIGN(cm->cmsg_len)) {
			cm = mtod(control, struct cmsghdr *);
			if (control->m_len < sizeof(*cm) || cm->cmsg_len == 0
			    || cm->cmsg_len > control->m_len) {
				error = EINVAL;
				break;
			}
			if (cm->cmsg_level != IPPROTO_IP)
				continue;

			switch (cm->cmsg_type) {
			case IP_SENDSRCADDR:
				if (cm->cmsg_len !=
				    CMSG_LEN(sizeof(struct in_addr))) {
					error = EINVAL;
					break;
				}
				bzero(&src, sizeof(src));
				src.sin_family = AF_INET;
				src.sin_len = sizeof(src);
				src.sin_port = inp->inp_lport;
				src.sin_addr =
				    *(struct in_addr *)CMSG_DATA(cm);
				break;

			case IP_TOS:
				if (cm->cmsg_len != CMSG_LEN(sizeof(u_char))) {
					error = EINVAL;
					break;
				}
				tos = *(u_char *)CMSG_DATA(cm);
				break;

			case IP_FLOWID:
				if (cm->cmsg_len != CMSG_LEN(sizeof(uint32_t))) {
					error = EINVAL;
					break;
				}
				flowid = *(uint32_t *) CMSG_DATA(cm);
				break;

			case IP_FLOWTYPE:
				if (cm->cmsg_len != CMSG_LEN(sizeof(uint32_t))) {
					error = EINVAL;
					break;
				}
				flowtype = *(uint32_t *) CMSG_DATA(cm);
				break;

#ifdef	RSS
			case IP_RSSBUCKETID:
				if (cm->cmsg_len != CMSG_LEN(sizeof(uint32_t))) {
					error = EINVAL;
					break;
				}
				/* This is just a placeholder for now */
				break;
#endif	/* RSS */
			default:
				error = ENOPROTOOPT;
				break;
			}
			if (error)
				break;
		}
		m_freem(control);
	}
	if (error) {
		if (unlock_inp == UH_WLOCKED)
			INP_WUNLOCK(inp);
		else
			INP_RUNLOCK(inp);
		m_freem(m);
		return (error);
	}

	/*
	 * Depending on whether or not the application has bound or connected
	 * the socket, we may have to do varying levels of work.  The optimal
	 * case is for a connected UDP socket, as a global lock isn't
	 * required at all.
	 *
	 * In order to decide which we need, we require stability of the
	 * inpcb binding, which we ensure by acquiring a read lock on the
	 * inpcb.  This doesn't strictly follow the lock order, so we play
	 * the trylock and retry game; note that we may end up with more
	 * conservative locks than required the second time around, so later
	 * assertions have to accept that.  Further analysis of the number of
	 * misses under contention is required.
	 *
	 * XXXRW: Check that hash locking update here is correct.
	 */
	pr = inp->inp_socket->so_proto->pr_protocol;
	pcbinfo = udp_get_inpcbinfo(pr);
	sin = (struct sockaddr_in *)addr;
	if (sin != NULL &&
	    (inp->inp_laddr.s_addr == INADDR_ANY && inp->inp_lport == 0)) {
		INP_HASH_WLOCK(pcbinfo);
		unlock_udbinfo = UH_WLOCKED;
	} else if ((sin != NULL && (
	    (sin->sin_addr.s_addr == INADDR_ANY) ||
	    (sin->sin_addr.s_addr == INADDR_BROADCAST) ||
	    (inp->inp_laddr.s_addr == INADDR_ANY) ||
	    (inp->inp_lport == 0))) ||
	    (src.sin_family == AF_INET)) {
		INP_HASH_RLOCK_ET(pcbinfo, et);
		unlock_udbinfo = UH_RLOCKED;
	} else
		unlock_udbinfo = UH_UNLOCKED;

	/*
	 * If the IP_SENDSRCADDR control message was specified, override the
	 * source address for this datagram.  Its use is invalidated if the
	 * address thus specified is incomplete or clobbers other inpcbs.
	 */
	laddr = inp->inp_laddr;
	lport = inp->inp_lport;
	if (src.sin_family == AF_INET) {
		INP_HASH_LOCK_ASSERT(pcbinfo);
		if ((lport == 0) ||
		    (laddr.s_addr == INADDR_ANY &&
		     src.sin_addr.s_addr == INADDR_ANY)) {
			error = EINVAL;
			goto release;
		}
		error = in_pcbbind_setup(inp, (struct sockaddr *)&src,
		    &laddr.s_addr, &lport, td->td_ucred);
		if (error)
			goto release;
	}

	/*
	 * If a UDP socket has been connected, then a local address/port will
	 * have been selected and bound.
	 *
	 * If a UDP socket has not been connected to, then an explicit
	 * destination address must be used, in which case a local
	 * address/port may not have been selected and bound.
	 */
	if (sin != NULL) {
		INP_LOCK_ASSERT(inp);
		if (inp->inp_faddr.s_addr != INADDR_ANY) {
			error = EISCONN;
			goto release;
		}

		/*
		 * Jail may rewrite the destination address, so let it do
		 * that before we use it.
		 */
		error = prison_remote_ip4(td->td_ucred, &sin->sin_addr);
		if (error)
			goto release;

		/*
		 * If a local address or port hasn't yet been selected, or if
		 * the destination address needs to be rewritten due to using
		 * a special INADDR_ constant, invoke in_pcbconnect_setup()
		 * to do the heavy lifting.  Once a port is selected, we
		 * commit the binding back to the socket; we also commit the
		 * binding of the address if in jail.
		 *
		 * If we already have a valid binding and we're not
		 * requesting a destination address rewrite, use a fast path.
		 */
		if (inp->inp_laddr.s_addr == INADDR_ANY ||
		    inp->inp_lport == 0 ||
		    sin->sin_addr.s_addr == INADDR_ANY ||
		    sin->sin_addr.s_addr == INADDR_BROADCAST) {
			INP_HASH_LOCK_ASSERT(pcbinfo);
			error = in_pcbconnect_setup(inp, addr, &laddr.s_addr,
			    &lport, &faddr.s_addr, &fport, NULL,
			    td->td_ucred);
			if (error)
				goto release;

			/*
			 * XXXRW: Why not commit the port if the address is
			 * !INADDR_ANY?
			 */
			/* Commit the local port if newly assigned. */
			if (inp->inp_laddr.s_addr == INADDR_ANY &&
			    inp->inp_lport == 0) {
				INP_WLOCK_ASSERT(inp);
				INP_HASH_WLOCK_ASSERT(pcbinfo);
				/*
				 * Remember addr if jailed, to prevent
				 * rebinding.
				 */
				if (prison_flag(td->td_ucred, PR_IP4))
					inp->inp_laddr = laddr;
				inp->inp_lport = lport;
				if (in_pcbinshash(inp) != 0) {
					inp->inp_lport = 0;
					error = EAGAIN;
					goto release;
				}
				inp->inp_flags |= INP_ANONPORT;
			}
		} else {
			faddr = sin->sin_addr;
			fport = sin->sin_port;
		}
	} else {
		INP_LOCK_ASSERT(inp);
		faddr = inp->inp_faddr;
		fport = inp->inp_fport;
		if (faddr.s_addr == INADDR_ANY) {
			error = ENOTCONN;
			goto release;
		}
	}

	/*
	 * Calculate data length and get a mbuf for UDP, IP, and possible
	 * link-layer headers.  Immediate slide the data pointer back forward
	 * since we won't use that space at this layer.
	 */
	M_PREPEND(m, sizeof(struct udpiphdr) + max_linkhdr, M_NOWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto release;
	}
	m->m_data += max_linkhdr;
	m->m_len -= max_linkhdr;
	m->m_pkthdr.len -= max_linkhdr;

	/*
	 * Fill in mbuf with extended UDP header and addresses and length put
	 * into network format.
	 */
	ui = mtod(m, struct udpiphdr *);
	bzero(ui->ui_x1, sizeof(ui->ui_x1));	/* XXX still needed? */
	ui->ui_v = IPVERSION << 4;
	ui->ui_pr = pr;
	ui->ui_src = laddr;
	ui->ui_dst = faddr;
	ui->ui_sport = lport;
	ui->ui_dport = fport;
	ui->ui_ulen = htons((u_short)len + sizeof(struct udphdr));
	if (pr == IPPROTO_UDPLITE) {
		struct udpcb *up;
		uint16_t plen;

		up = intoudpcb(inp);
		cscov = up->u_txcslen;
		plen = (u_short)len + sizeof(struct udphdr);
		if (cscov >= plen)
			cscov = 0;
		ui->ui_len = htons(plen);
		ui->ui_ulen = htons(cscov);
		/*
		 * For UDP-Lite, checksum coverage length of zero means
		 * the entire UDPLite packet is covered by the checksum.
		 */
		cscov_partial = (cscov == 0) ? 0 : 1;
	}

	/*
	 * Set the Don't Fragment bit in the IP header.
	 */
	if (inp->inp_flags & INP_DONTFRAG) {
		struct ip *ip;

		ip = (struct ip *)&ui->ui_i;
		ip->ip_off |= htons(IP_DF);
	}

	ipflags = 0;
	if (inp->inp_socket->so_options & SO_DONTROUTE)
		ipflags |= IP_ROUTETOIF;
	if (inp->inp_socket->so_options & SO_BROADCAST)
		ipflags |= IP_ALLOWBROADCAST;
	if (inp->inp_flags & INP_ONESBCAST)
		ipflags |= IP_SENDONES;

#ifdef MAC
	mac_inpcb_create_mbuf(inp, m);
#endif

	/*
	 * Set up checksum and output datagram.
	 */
	ui->ui_sum = 0;
	if (pr == IPPROTO_UDPLITE) {
		if (inp->inp_flags & INP_ONESBCAST)
			faddr.s_addr = INADDR_BROADCAST;
		if (cscov_partial) {
			if ((ui->ui_sum = in_cksum(m, sizeof(struct ip) + cscov)) == 0)
				ui->ui_sum = 0xffff;
		} else {
			if ((ui->ui_sum = in_cksum(m, sizeof(struct udpiphdr) + len)) == 0)
				ui->ui_sum = 0xffff;
		}
	} else if (V_udp_cksum) {
		if (inp->inp_flags & INP_ONESBCAST)
			faddr.s_addr = INADDR_BROADCAST;
		ui->ui_sum = in_pseudo(ui->ui_src.s_addr, faddr.s_addr,
		    htons((u_short)len + sizeof(struct udphdr) + pr));
		m->m_pkthdr.csum_flags = CSUM_UDP;
		m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum);
	}
	((struct ip *)ui)->ip_len = htons(sizeof(struct udpiphdr) + len);
	((struct ip *)ui)->ip_ttl = inp->inp_ip_ttl;	/* XXX */
	((struct ip *)ui)->ip_tos = tos;		/* XXX */
	UDPSTAT_INC(udps_opackets);

	/*
	 * Setup flowid / RSS information for outbound socket.
	 *
	 * Once the UDP code decides to set a flowid some other way,
	 * this allows the flowid to be overridden by userland.
	 */
	if (flowtype != M_HASHTYPE_NONE) {
		m->m_pkthdr.flowid = flowid;
		M_HASHTYPE_SET(m, flowtype);
#ifdef	RSS
	} else {
		uint32_t hash_val, hash_type;
		/*
		 * Calculate an appropriate RSS hash for UDP and
		 * UDP Lite.
		 *
		 * The called function will take care of figuring out
		 * whether a 2-tuple or 4-tuple hash is required based
		 * on the currently configured scheme.
		 *
		 * Later later on connected socket values should be
		 * cached in the inpcb and reused, rather than constantly
		 * re-calculating it.
		 *
		 * UDP Lite is a different protocol number and will
		 * likely end up being hashed as a 2-tuple until
		 * RSS / NICs grow UDP Lite protocol awareness.
		 */
		if (rss_proto_software_hash_v4(faddr, laddr, fport, lport,
		    pr, &hash_val, &hash_type) == 0) {
			m->m_pkthdr.flowid = hash_val;
			M_HASHTYPE_SET(m, hash_type);
		}
#endif
	}

#ifdef	RSS
	/*
	 * Don't override with the inp cached flowid value.
	 *
	 * Depending upon the kind of send being done, the inp
	 * flowid/flowtype values may actually not be appropriate
	 * for this particular socket send.
	 *
	 * We should either leave the flowid at zero (which is what is
	 * currently done) or set it to some software generated
	 * hash value based on the packet contents.
	 */
	ipflags |= IP_NODEFAULTFLOWID;
#endif	/* RSS */

	if (unlock_udbinfo == UH_WLOCKED)
		INP_HASH_WUNLOCK(pcbinfo);
	else if (unlock_udbinfo == UH_RLOCKED)
		INP_HASH_RUNLOCK_ET(pcbinfo, et);
	if (pr == IPPROTO_UDPLITE)
		UDPLITE_PROBE(send, NULL, inp, &ui->ui_i, inp, &ui->ui_u);
	else
		UDP_PROBE(send, NULL, inp, &ui->ui_i, inp, &ui->ui_u);
	error = ip_output(m, inp->inp_options,
	    (unlock_inp == UH_WLOCKED ? &inp->inp_route : NULL), ipflags,
	    inp->inp_moptions, inp);
	if (unlock_inp == UH_WLOCKED)
		INP_WUNLOCK(inp);
	else
		INP_RUNLOCK(inp);
	return (error);

release:
	if (unlock_udbinfo == UH_WLOCKED) {
		KASSERT(unlock_inp == UH_WLOCKED,
		    ("%s: excl udbinfo lock, shared inp lock", __func__));
		INP_HASH_WUNLOCK(pcbinfo);
		INP_WUNLOCK(inp);
	} else if (unlock_udbinfo == UH_RLOCKED) {
		KASSERT(unlock_inp == UH_RLOCKED,
		    ("%s: shared udbinfo lock, excl inp lock", __func__));
		INP_HASH_RUNLOCK_ET(pcbinfo, et);
		INP_RUNLOCK(inp);
	} else if (unlock_inp == UH_WLOCKED)
		INP_WUNLOCK(inp);
	else
		INP_RUNLOCK(inp);
	m_freem(m);
	return (error);
}

static void
udp_abort(struct socket *so)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp_abort: inp == NULL"));
	INP_WLOCK(inp);
	if (inp->inp_faddr.s_addr != INADDR_ANY) {
		INP_HASH_WLOCK(pcbinfo);
		in_pcbdisconnect(inp);
		inp->inp_laddr.s_addr = INADDR_ANY;
		INP_HASH_WUNLOCK(pcbinfo);
		soisdisconnected(so);
	}
	INP_WUNLOCK(inp);
}

static int
udp_attach(struct socket *so, int proto, struct thread *td)
{
	static uint32_t udp_flowid;
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;
	int error;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp == NULL, ("udp_attach: inp != NULL"));
	error = soreserve(so, udp_sendspace, udp_recvspace);
	if (error)
		return (error);
	INP_INFO_WLOCK(pcbinfo);
	error = in_pcballoc(so, pcbinfo);
	if (error) {
		INP_INFO_WUNLOCK(pcbinfo);
		return (error);
	}

	inp = sotoinpcb(so);
	inp->inp_vflag |= INP_IPV4;
	inp->inp_ip_ttl = V_ip_defttl;
	inp->inp_flowid = atomic_fetchadd_int(&udp_flowid, 1);
	inp->inp_flowtype = M_HASHTYPE_OPAQUE;

	error = udp_newudpcb(inp);
	if (error) {
		in_pcbdetach(inp);
		in_pcbfree(inp);
		INP_INFO_WUNLOCK(pcbinfo);
		return (error);
	}

	INP_WUNLOCK(inp);
	INP_INFO_WUNLOCK(pcbinfo);
	return (0);
}
#endif /* INET */

int
udp_set_kernel_tunneling(struct socket *so, udp_tun_func_t f, udp_tun_icmp_t i, void *ctx)
{
	struct inpcb *inp;
	struct udpcb *up;

	KASSERT(so->so_type == SOCK_DGRAM,
	    ("udp_set_kernel_tunneling: !dgram"));
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp_set_kernel_tunneling: inp == NULL"));
	INP_WLOCK(inp);
	up = intoudpcb(inp);
	if ((up->u_tun_func != NULL) ||
	    (up->u_icmp_func != NULL)) {
		INP_WUNLOCK(inp);
		return (EBUSY);
	}
	up->u_tun_func = f;
	up->u_icmp_func = i;
	up->u_tun_ctx = ctx;
	INP_WUNLOCK(inp);
	return (0);
}

#ifdef INET
static int
udp_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;
	int error;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp_bind: inp == NULL"));
	INP_WLOCK(inp);
	INP_HASH_WLOCK(pcbinfo);
	error = in_pcbbind(inp, nam, td->td_ucred);
	INP_HASH_WUNLOCK(pcbinfo);
	INP_WUNLOCK(inp);
	return (error);
}

static void
udp_close(struct socket *so)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp_close: inp == NULL"));
	INP_WLOCK(inp);
	if (inp->inp_faddr.s_addr != INADDR_ANY) {
		INP_HASH_WLOCK(pcbinfo);
		in_pcbdisconnect(inp);
		inp->inp_laddr.s_addr = INADDR_ANY;
		INP_HASH_WUNLOCK(pcbinfo);
		soisdisconnected(so);
	}
	INP_WUNLOCK(inp);
}

static int
udp_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;
	struct sockaddr_in *sin;
	int error;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp_connect: inp == NULL"));
	INP_WLOCK(inp);
	if (inp->inp_faddr.s_addr != INADDR_ANY) {
		INP_WUNLOCK(inp);
		return (EISCONN);
	}
	sin = (struct sockaddr_in *)nam;
	error = prison_remote_ip4(td->td_ucred, &sin->sin_addr);
	if (error != 0) {
		INP_WUNLOCK(inp);
		return (error);
	}
	INP_HASH_WLOCK(pcbinfo);
	error = in_pcbconnect(inp, nam, td->td_ucred);
	INP_HASH_WUNLOCK(pcbinfo);
	if (error == 0)
		soisconnected(so);
	INP_WUNLOCK(inp);
	return (error);
}

static void
udp_detach(struct socket *so)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;
	struct udpcb *up;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp_detach: inp == NULL"));
	KASSERT(inp->inp_faddr.s_addr == INADDR_ANY,
	    ("udp_detach: not disconnected"));
	INP_INFO_WLOCK(pcbinfo);
	INP_WLOCK(inp);
	up = intoudpcb(inp);
	KASSERT(up != NULL, ("%s: up == NULL", __func__));
	inp->inp_ppcb = NULL;
	in_pcbdetach(inp);
	in_pcbfree(inp);
	INP_INFO_WUNLOCK(pcbinfo);
	udp_discardcb(up);
}

static int
udp_disconnect(struct socket *so)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp_disconnect: inp == NULL"));
	INP_WLOCK(inp);
	if (inp->inp_faddr.s_addr == INADDR_ANY) {
		INP_WUNLOCK(inp);
		return (ENOTCONN);
	}
	INP_HASH_WLOCK(pcbinfo);
	in_pcbdisconnect(inp);
	inp->inp_laddr.s_addr = INADDR_ANY;
	INP_HASH_WUNLOCK(pcbinfo);
	SOCK_LOCK(so);
	so->so_state &= ~SS_ISCONNECTED;		/* XXX */
	SOCK_UNLOCK(so);
	INP_WUNLOCK(inp);
	return (0);
}

static int
udp_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *addr,
    struct mbuf *control, struct thread *td)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp_send: inp == NULL"));
	return (udp_output(inp, m, addr, control, td));
}
#endif /* INET */

int
udp_shutdown(struct socket *so)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp_shutdown: inp == NULL"));
	INP_WLOCK(inp);
	socantsendmore(so);
	INP_WUNLOCK(inp);
	return (0);
}

#ifdef INET
struct pr_usrreqs udp_usrreqs = {
	.pru_abort =		udp_abort,
	.pru_attach =		udp_attach,
	.pru_bind =		udp_bind,
	.pru_connect =		udp_connect,
	.pru_control =		in_control,
	.pru_detach =		udp_detach,
	.pru_disconnect =	udp_disconnect,
	.pru_peeraddr =		in_getpeeraddr,
	.pru_send =		udp_send,
	.pru_soreceive =	soreceive_dgram,
	.pru_sosend =		sosend_dgram,
	.pru_shutdown =		udp_shutdown,
	.pru_sockaddr =		in_getsockaddr,
	.pru_sosetlabel =	in_pcbsosetlabel,
	.pru_close =		udp_close,
};
#endif /* INET */
