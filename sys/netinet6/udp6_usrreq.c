/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 *	$KAME: udp6_usrreq.c,v 1.27 2001/05/21 05:45:10 jinmei Exp $
 *	$KAME: udp6_output.c,v 1.31 2001/05/21 16:39:15 jinmei Exp $
 */

/*-
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1995
 *	The Regents of the University of California.
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
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
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

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/rss_config.h>

#include <netinet/in.h>
#include <netinet/in_kdtrace.h>
#include <netinet/in_pcb.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/udplite.h>

#include <netinet6/ip6protosw.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/in6_rss.h>
#include <netinet6/udp6_var.h>
#include <netinet6/scope6_var.h>

#include <netipsec/ipsec_support.h>

#include <security/mac/mac_framework.h>

/*
 * UDP protocol implementation.
 * Per RFC 768, August, 1980.
 */

extern struct protosw	inetsw[];
static void		udp6_detach(struct socket *so);

static int
udp6_append(struct inpcb *inp, struct mbuf *n, int off,
    struct sockaddr_in6 *fromsa)
{
	struct socket *so;
	struct mbuf *opts = NULL, *tmp_opts;
	struct udpcb *up;

	INP_LOCK_ASSERT(inp);

	/*
	 * Engage the tunneling protocol.
	 */
	up = intoudpcb(inp);
	if (up->u_tun_func != NULL) {
		in_pcbref(inp);
		INP_RUNLOCK(inp);
		(*up->u_tun_func)(n, off, inp, (struct sockaddr *)&fromsa[0],
		    up->u_tun_ctx);
		INP_RLOCK(inp);
		return (in_pcbrele_rlocked(inp));
	}
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	/* Check AH/ESP integrity. */
	if (IPSEC_ENABLED(ipv6)) {
		if (IPSEC_CHECK_POLICY(ipv6, n, inp) != 0) {
			m_freem(n);
			return (0);
		}
	}
#endif /* IPSEC */
#ifdef MAC
	if (mac_inpcb_check_deliver(inp, n) != 0) {
		m_freem(n);
		return (0);
	}
#endif
	opts = NULL;
	if (inp->inp_flags & INP_CONTROLOPTS ||
	    inp->inp_socket->so_options & SO_TIMESTAMP)
		ip6_savecontrol(inp, n, &opts);
	if ((inp->inp_vflag & INP_IPV6) && (inp->inp_flags2 & INP_ORIGDSTADDR)) {
		tmp_opts = sbcreatecontrol((caddr_t)&fromsa[1],
                        sizeof(struct sockaddr_in6), IPV6_ORIGDSTADDR, IPPROTO_IPV6);
                if (tmp_opts) {
                        if (opts) {
                                tmp_opts->m_next = opts;
                                opts = tmp_opts;
                        } else
                                opts = tmp_opts;
                }

	}
	m_adj(n, off + sizeof(struct udphdr));

	so = inp->inp_socket;
	SOCKBUF_LOCK(&so->so_rcv);
	if (sbappendaddr_locked(&so->so_rcv, (struct sockaddr *)&fromsa[0], n,
	    opts) == 0) {
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
udp6_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	struct ifnet *ifp;
	struct ip6_hdr *ip6;
	struct udphdr *uh;
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;
	struct udpcb *up;
	int off = *offp;
	int cscov_partial;
	int plen, ulen;
	struct epoch_tracker et;
	struct sockaddr_in6 fromsa[2];
	struct m_tag *fwd_tag;
	uint16_t uh_sum;
	uint8_t nxt;

	ifp = m->m_pkthdr.rcvif;

#ifndef PULLDOWN_TEST
	IP6_EXTHDR_CHECK(m, off, sizeof(struct udphdr), IPPROTO_DONE);
	ip6 = mtod(m, struct ip6_hdr *);
	uh = (struct udphdr *)((caddr_t)ip6 + off);
#else
	IP6_EXTHDR_GET(uh, struct udphdr *, m, off, sizeof(*uh));
	if (!uh)
		return (IPPROTO_DONE);
	ip6 = mtod(m, struct ip6_hdr *);
#endif

	UDPSTAT_INC(udps_ipackets);

	/*
	 * Destination port of 0 is illegal, based on RFC768.
	 */
	if (uh->uh_dport == 0)
		goto badunlocked;

	plen = ntohs(ip6->ip6_plen) - off + sizeof(*ip6);
	ulen = ntohs((u_short)uh->uh_ulen);

	nxt = proto;
	cscov_partial = (nxt == IPPROTO_UDPLITE) ? 1 : 0;
	if (nxt == IPPROTO_UDPLITE) {
		/* Zero means checksum over the complete packet. */
		if (ulen == 0)
			ulen = plen;
		if (ulen == plen)
			cscov_partial = 0;
		if ((ulen < sizeof(struct udphdr)) || (ulen > plen)) {
			/* XXX: What is the right UDPLite MIB counter? */
			goto badunlocked;
		}
		if (uh->uh_sum == 0) {
			/* XXX: What is the right UDPLite MIB counter? */
			goto badunlocked;
		}
	} else {
		if ((ulen < sizeof(struct udphdr)) || (plen != ulen)) {
			UDPSTAT_INC(udps_badlen);
			goto badunlocked;
		}
		if (uh->uh_sum == 0) {
			UDPSTAT_INC(udps_nosum);
			goto badunlocked;
		}
	}

	if ((m->m_pkthdr.csum_flags & CSUM_DATA_VALID_IPV6) &&
	    !cscov_partial) {
		if (m->m_pkthdr.csum_flags & CSUM_PSEUDO_HDR)
			uh_sum = m->m_pkthdr.csum_data;
		else
			uh_sum = in6_cksum_pseudo(ip6, ulen, nxt,
			    m->m_pkthdr.csum_data);
		uh_sum ^= 0xffff;
	} else
		uh_sum = in6_cksum_partial(m, nxt, off, plen, ulen);

	if (uh_sum != 0) {
		UDPSTAT_INC(udps_badsum);
		goto badunlocked;
	}

	/*
	 * Construct sockaddr format source address.
	 */
	init_sin6(&fromsa[0], m, 0);
	fromsa[0].sin6_port = uh->uh_sport;
	init_sin6(&fromsa[1], m, 1);
	fromsa[1].sin6_port = uh->uh_dport;

	pcbinfo = udp_get_inpcbinfo(nxt);
	if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
		struct inpcb *last;
		struct inpcbhead *pcblist;
		struct ip6_moptions *imo;

		INP_INFO_RLOCK_ET(pcbinfo, et);
		/*
		 * In the event that laddr should be set to the link-local
		 * address (this happens in RIPng), the multicast address
		 * specified in the received packet will not match laddr.  To
		 * handle this situation, matching is relaxed if the
		 * receiving interface is the same as one specified in the
		 * socket and if the destination multicast address matches
		 * one of the multicast groups specified in the socket.
		 */

		/*
		 * KAME note: traditionally we dropped udpiphdr from mbuf
		 * here.  We need udphdr for IPsec processing so we do that
		 * later.
		 */
		pcblist = udp_get_pcblist(nxt);
		last = NULL;
		CK_LIST_FOREACH(inp, pcblist, inp_list) {
			if ((inp->inp_vflag & INP_IPV6) == 0)
				continue;
			if (inp->inp_lport != uh->uh_dport)
				continue;
			if (inp->inp_fport != 0 &&
			    inp->inp_fport != uh->uh_sport)
				continue;
			if (!IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr)) {
				if (!IN6_ARE_ADDR_EQUAL(&inp->in6p_laddr,
							&ip6->ip6_dst))
					continue;
			}
			if (!IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr)) {
				if (!IN6_ARE_ADDR_EQUAL(&inp->in6p_faddr,
							&ip6->ip6_src) ||
				    inp->inp_fport != uh->uh_sport)
					continue;
			}

			/*
			 * XXXRW: Because we weren't holding either the inpcb
			 * or the hash lock when we checked for a match 
			 * before, we should probably recheck now that the 
			 * inpcb lock is (supposed to be) held.
			 */

			/*
			 * Handle socket delivery policy for any-source
			 * and source-specific multicast. [RFC3678]
			 */
			imo = inp->in6p_moptions;
			if (imo && IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
				struct sockaddr_in6	 mcaddr;
				int			 blocked;

				INP_RLOCK(inp);
				if (__predict_false(inp->inp_flags2 & INP_FREED)) {
					INP_RUNLOCK(inp);
					continue;
				}

				bzero(&mcaddr, sizeof(struct sockaddr_in6));
				mcaddr.sin6_len = sizeof(struct sockaddr_in6);
				mcaddr.sin6_family = AF_INET6;
				mcaddr.sin6_addr = ip6->ip6_dst;

				blocked = im6o_mc_filter(imo, ifp,
					(struct sockaddr *)&mcaddr,
					(struct sockaddr *)&fromsa[0]);
				if (blocked != MCAST_PASS) {
					if (blocked == MCAST_NOTGMEMBER)
						IP6STAT_INC(ip6s_notmember);
					if (blocked == MCAST_NOTSMEMBER ||
					    blocked == MCAST_MUTED)
						UDPSTAT_INC(udps_filtermcast);
					INP_RUNLOCK(inp); /* XXX */
					continue;
				}

				INP_RUNLOCK(inp);
			}
			if (last != NULL) {
				struct mbuf *n;

				if ((n = m_copym(m, 0, M_COPYALL, M_NOWAIT)) !=
				    NULL) {
					INP_RLOCK(last);
					if (__predict_true(last->inp_flags2 & INP_FREED) == 0) {
						if (nxt == IPPROTO_UDPLITE)
							UDPLITE_PROBE(receive, NULL, last,
							    ip6, last, uh);
						else
							UDP_PROBE(receive, NULL, last,
							    ip6, last, uh);
						if (udp6_append(last, n, off, fromsa))
							goto inp_lost;
					}
					INP_RUNLOCK(last);
				}
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
			UDPSTAT_INC(udps_noport);
			UDPSTAT_INC(udps_noportmcast);
			goto badheadlocked;
		}
		INP_RLOCK(last);
		if (__predict_true(last->inp_flags2 & INP_FREED) == 0) {
			if (nxt == IPPROTO_UDPLITE)
				UDPLITE_PROBE(receive, NULL, last, ip6, last, uh);
			else
				UDP_PROBE(receive, NULL, last, ip6, last, uh);
			if (udp6_append(last, m, off, fromsa) == 0)
				INP_RUNLOCK(last);
		} else
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
	if ((m->m_flags & M_IP6_NEXTHOP) &&
	    (fwd_tag = m_tag_find(m, PACKET_TAG_IPFORWARD, NULL)) != NULL) {
		struct sockaddr_in6 *next_hop6;

		next_hop6 = (struct sockaddr_in6 *)(fwd_tag + 1);

		/*
		 * Transparently forwarded. Pretend to be the destination.
		 * Already got one like this?
		 */
		inp = in6_pcblookup_mbuf(pcbinfo, &ip6->ip6_src,
		    uh->uh_sport, &ip6->ip6_dst, uh->uh_dport,
		    INPLOOKUP_RLOCKPCB, m->m_pkthdr.rcvif, m);
		if (!inp) {
			/*
			 * It's new.  Try to find the ambushing socket.
			 * Because we've rewritten the destination address,
			 * any hardware-generated hash is ignored.
			 */
			inp = in6_pcblookup(pcbinfo, &ip6->ip6_src,
			    uh->uh_sport, &next_hop6->sin6_addr,
			    next_hop6->sin6_port ? htons(next_hop6->sin6_port) :
			    uh->uh_dport, INPLOOKUP_WILDCARD |
			    INPLOOKUP_RLOCKPCB, m->m_pkthdr.rcvif);
		}
		/* Remove the tag from the packet. We don't need it anymore. */
		m_tag_delete(m, fwd_tag);
		m->m_flags &= ~M_IP6_NEXTHOP;
	} else
		inp = in6_pcblookup_mbuf(pcbinfo, &ip6->ip6_src,
		    uh->uh_sport, &ip6->ip6_dst, uh->uh_dport,
		    INPLOOKUP_WILDCARD | INPLOOKUP_RLOCKPCB,
		    m->m_pkthdr.rcvif, m);
	if (inp == NULL) {
		if (udp_log_in_vain) {
			char ip6bufs[INET6_ADDRSTRLEN];
			char ip6bufd[INET6_ADDRSTRLEN];

			log(LOG_INFO,
			    "Connection attempt to UDP [%s]:%d from [%s]:%d\n",
			    ip6_sprintf(ip6bufd, &ip6->ip6_dst),
			    ntohs(uh->uh_dport),
			    ip6_sprintf(ip6bufs, &ip6->ip6_src),
			    ntohs(uh->uh_sport));
		}
		if (nxt == IPPROTO_UDPLITE)
			UDPLITE_PROBE(receive, NULL, NULL, ip6, NULL, uh);
		else
			UDP_PROBE(receive, NULL, NULL, ip6, NULL, uh);
		UDPSTAT_INC(udps_noport);
		if (m->m_flags & M_MCAST) {
			printf("UDP6: M_MCAST is set in a unicast packet.\n");
			UDPSTAT_INC(udps_noportmcast);
			goto badunlocked;
		}
		if (V_udp_blackhole)
			goto badunlocked;
		icmp6_error(m, ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_NOPORT, 0);
		return (IPPROTO_DONE);
	}
	INP_RLOCK_ASSERT(inp);
	up = intoudpcb(inp);
	if (cscov_partial) {
		if (up->u_rxcslen == 0 || up->u_rxcslen > ulen) {
			INP_RUNLOCK(inp);
			m_freem(m);
			return (IPPROTO_DONE);
		}
	}
	if (nxt == IPPROTO_UDPLITE)
		UDPLITE_PROBE(receive, NULL, inp, ip6, inp, uh);
	else
		UDP_PROBE(receive, NULL, inp, ip6, inp, uh);
	if (udp6_append(inp, m, off, fromsa) == 0)
		INP_RUNLOCK(inp);
	return (IPPROTO_DONE);

badheadlocked:
	INP_INFO_RUNLOCK_ET(pcbinfo, et);
badunlocked:
	if (m)
		m_freem(m);
	return (IPPROTO_DONE);
}

static void
udp6_common_ctlinput(int cmd, struct sockaddr *sa, void *d,
    struct inpcbinfo *pcbinfo)
{
	struct udphdr uh;
	struct ip6_hdr *ip6;
	struct mbuf *m;
	int off = 0;
	struct ip6ctlparam *ip6cp = NULL;
	const struct sockaddr_in6 *sa6_src = NULL;
	void *cmdarg;
	struct inpcb *(*notify)(struct inpcb *, int) = udp_notify;
	struct udp_portonly {
		u_int16_t uh_sport;
		u_int16_t uh_dport;
	} *uhp;

	if (sa->sa_family != AF_INET6 ||
	    sa->sa_len != sizeof(struct sockaddr_in6))
		return;

	if ((unsigned)cmd >= PRC_NCMDS)
		return;
	if (PRC_IS_REDIRECT(cmd))
		notify = in6_rtchange, d = NULL;
	else if (cmd == PRC_HOSTDEAD)
		d = NULL;
	else if (inet6ctlerrmap[cmd] == 0)
		return;

	/* if the parameter is from icmp6, decode it. */
	if (d != NULL) {
		ip6cp = (struct ip6ctlparam *)d;
		m = ip6cp->ip6c_m;
		ip6 = ip6cp->ip6c_ip6;
		off = ip6cp->ip6c_off;
		cmdarg = ip6cp->ip6c_cmdarg;
		sa6_src = ip6cp->ip6c_src;
	} else {
		m = NULL;
		ip6 = NULL;
		cmdarg = NULL;
		sa6_src = &sa6_any;
	}

	if (ip6) {
		/*
		 * XXX: We assume that when IPV6 is non NULL,
		 * M and OFF are valid.
		 */

		/* Check if we can safely examine src and dst ports. */
		if (m->m_pkthdr.len < off + sizeof(*uhp))
			return;

		bzero(&uh, sizeof(uh));
		m_copydata(m, off, sizeof(*uhp), (caddr_t)&uh);

		if (!PRC_IS_REDIRECT(cmd)) {
			/* Check to see if its tunneled */
			struct inpcb *inp;
			inp = in6_pcblookup_mbuf(pcbinfo, &ip6->ip6_dst,
			    uh.uh_dport, &ip6->ip6_src, uh.uh_sport,
			    INPLOOKUP_WILDCARD | INPLOOKUP_RLOCKPCB,
			    m->m_pkthdr.rcvif, m);
			if (inp != NULL) {
				struct udpcb *up;
				
				up = intoudpcb(inp);
				if (up->u_icmp_func) {
					/* Yes it is. */
					INP_RUNLOCK(inp);
					(*up->u_icmp_func)(cmd, (struct sockaddr *)ip6cp->ip6c_src,
					      d, up->u_tun_ctx);
					return;
				} else {
					/* Can't find it. */
					INP_RUNLOCK(inp);
				}
			}
		}
		(void)in6_pcbnotify(pcbinfo, sa, uh.uh_dport,
		    (struct sockaddr *)ip6cp->ip6c_src, uh.uh_sport, cmd,
		    cmdarg, notify);
	} else
		(void)in6_pcbnotify(pcbinfo, sa, 0,
		    (const struct sockaddr *)sa6_src, 0, cmd, cmdarg, notify);
}

void
udp6_ctlinput(int cmd, struct sockaddr *sa, void *d)
{

	return (udp6_common_ctlinput(cmd, sa, d, &V_udbinfo));
}

void
udplite6_ctlinput(int cmd, struct sockaddr *sa, void *d)
{

	return (udp6_common_ctlinput(cmd, sa, d, &V_ulitecbinfo));
}

static int
udp6_getcred(SYSCTL_HANDLER_ARGS)
{
	struct xucred xuc;
	struct sockaddr_in6 addrs[2];
	struct inpcb *inp;
	int error;

	error = priv_check(req->td, PRIV_NETINET_GETCRED);
	if (error)
		return (error);

	if (req->newlen != sizeof(addrs))
		return (EINVAL);
	if (req->oldlen != sizeof(struct xucred))
		return (EINVAL);
	error = SYSCTL_IN(req, addrs, sizeof(addrs));
	if (error)
		return (error);
	if ((error = sa6_embedscope(&addrs[0], V_ip6_use_defzone)) != 0 ||
	    (error = sa6_embedscope(&addrs[1], V_ip6_use_defzone)) != 0) {
		return (error);
	}
	inp = in6_pcblookup(&V_udbinfo, &addrs[1].sin6_addr,
	    addrs[1].sin6_port, &addrs[0].sin6_addr, addrs[0].sin6_port,
	    INPLOOKUP_WILDCARD | INPLOOKUP_RLOCKPCB, NULL);
	if (inp != NULL) {
		INP_RLOCK_ASSERT(inp);
		if (inp->inp_socket == NULL)
			error = ENOENT;
		if (error == 0)
			error = cr_canseesocket(req->td->td_ucred,
			    inp->inp_socket);
		if (error == 0)
			cru2x(inp->inp_cred, &xuc);
		INP_RUNLOCK(inp);
	} else
		error = ENOENT;
	if (error == 0)
		error = SYSCTL_OUT(req, &xuc, sizeof(struct xucred));
	return (error);
}

SYSCTL_PROC(_net_inet6_udp6, OID_AUTO, getcred, CTLTYPE_OPAQUE|CTLFLAG_RW, 0,
    0, udp6_getcred, "S,xucred", "Get the xucred of a UDP6 connection");

#define	UH_WLOCKED	2
#define	UH_RLOCKED	1
#define	UH_UNLOCKED	0
static int
udp6_output(struct socket *so, int flags_arg, struct mbuf *m,
    struct sockaddr *addr6, struct mbuf *control, struct thread *td)
{
	struct inpcbinfo *pcbinfo;
	struct inpcb *inp;
	struct ip6_hdr *ip6;
	struct udphdr *udp6;
	struct in6_addr *laddr, *faddr, in6a;
	struct ip6_pktopts *optp, opt;
	struct sockaddr_in6 *sin6, tmp;
	struct epoch_tracker et;
	int cscov_partial, error, flags, hlen, scope_ambiguous;
	u_int32_t ulen, plen;
	uint16_t cscov;
	u_short fport;
	uint8_t nxt, unlock_inp, unlock_udbinfo;

	/* addr6 has been validated in udp6_send(). */
	sin6 = (struct sockaddr_in6 *)addr6;

	/*
	 * In contrast to to IPv4 we do not validate the max. packet length
	 * here due to IPv6 Jumbograms (RFC2675).
	 */

	scope_ambiguous = 0;
	if (sin6) {
		/* Protect *addr6 from overwrites. */
		tmp = *sin6;
		sin6 = &tmp;

		/*
		 * Application should provide a proper zone ID or the use of
		 * default zone IDs should be enabled.  Unfortunately, some
		 * applications do not behave as it should, so we need a
		 * workaround.  Even if an appropriate ID is not determined,
		 * we'll see if we can determine the outgoing interface.  If we
		 * can, determine the zone ID based on the interface below.
		 */
		if (sin6->sin6_scope_id == 0 && !V_ip6_use_defzone)
			scope_ambiguous = 1;
		if ((error = sa6_embedscope(sin6, V_ip6_use_defzone)) != 0) {
			if (control)
				m_freem(control);
			m_freem(m);
			return (error);
		}
	}

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("%s: inp == NULL", __func__));
	/*
	 * In the following cases we want a write lock on the inp for either
	 * local operations or for possible route cache updates in the IPv6
	 * output path:
	 * - on connected sockets (sin6 is NULL) for route cache updates,
	 * - when we are not bound to an address and source port (it is
	 *   in6_pcbsetport() which will require the write lock).
	 */
	if (sin6 == NULL || (IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr) &&
	    inp->inp_lport == 0)) {
		INP_WLOCK(inp);
		unlock_inp = UH_WLOCKED;
	} else {
		INP_RLOCK(inp);
		unlock_inp = UH_RLOCKED;
	}
	nxt = (inp->inp_socket->so_proto->pr_protocol == IPPROTO_UDP) ?
	    IPPROTO_UDP : IPPROTO_UDPLITE;

#ifdef INET
	if ((inp->inp_flags & IN6P_IPV6_V6ONLY) == 0) {
		int hasv4addr;

		if (sin6 == NULL)
			hasv4addr = (inp->inp_vflag & INP_IPV4);
		else
			hasv4addr = IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)
			    ? 1 : 0;
		if (hasv4addr) {
			struct pr_usrreqs *pru;

			/*
			 * XXXRW: We release UDP-layer locks before calling
			 * udp_send() in order to avoid recursion.  However,
			 * this does mean there is a short window where inp's
			 * fields are unstable.  Could this lead to a
			 * potential race in which the factors causing us to
			 * select the UDPv4 output routine are invalidated?
			 */
			if (unlock_inp == UH_WLOCKED)
				INP_WUNLOCK(inp);
			else
				INP_RUNLOCK(inp);
			if (sin6)
				in6_sin6_2_sin_in_sock((struct sockaddr *)sin6);
			pru = inetsw[ip_protox[nxt]].pr_usrreqs;
			/* addr will just be freed in sendit(). */
			return ((*pru->pru_send)(so, flags_arg, m,
			    (struct sockaddr *)sin6, control, td));
		}
	} else
#endif
	if (sin6 && IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
		/*
		 * Given this is either an IPv6-only socket or no INET is
		 * supported we will fail the send if the given destination
		 * address is a v4mapped address.
		 */
		if (unlock_inp == UH_WLOCKED)
			INP_WUNLOCK(inp);
		else
			INP_RUNLOCK(inp);
		return (EINVAL);
	}

	if (control) {
		if ((error = ip6_setpktopts(control, &opt,
		    inp->in6p_outputopts, td->td_ucred, nxt)) != 0) {
			if (unlock_inp == UH_WLOCKED)
				INP_WUNLOCK(inp);
			else
				INP_RUNLOCK(inp);
			ip6_clearpktopts(&opt, -1);
			if (control)
				m_freem(control);
			m_freem(m);
			return (error);
		}
		optp = &opt;
	} else
		optp = inp->in6p_outputopts;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	if (sin6 != NULL &&
	    IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr) && inp->inp_lport == 0) {
		INP_HASH_WLOCK(pcbinfo);
		unlock_udbinfo = UH_WLOCKED;
	} else if (sin6 != NULL &&
	    (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) ||
	    IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr) ||
	    inp->inp_lport == 0)) {
		INP_HASH_RLOCK_ET(pcbinfo, et);
		unlock_udbinfo = UH_RLOCKED;
	} else
		unlock_udbinfo = UH_UNLOCKED;

	if (sin6) {

		/*
		 * Since we saw no essential reason for calling in_pcbconnect,
		 * we get rid of such kind of logic, and call in6_selectsrc
		 * and in6_pcbsetport in order to fill in the local address
		 * and the local port.
		 */
		if (sin6->sin6_port == 0) {
			error = EADDRNOTAVAIL;
			goto release;
		}

		if (!IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr)) {
			/* how about ::ffff:0.0.0.0 case? */
			error = EISCONN;
			goto release;
		}

		/*
		 * Given we handle the v4mapped case in the INET block above
		 * assert here that it must not happen anymore.
		 */
		KASSERT(!IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr),
		    ("%s: sin6(%p)->sin6_addr is v4mapped which we "
		    "should have handled.", __func__, sin6));

		/* This only requires read-locking. */
		error = in6_selectsrc_socket(sin6, optp, inp,
		    td->td_ucred, scope_ambiguous, &in6a, NULL);
		if (error)
			goto release;
		laddr = &in6a;

		if (inp->inp_lport == 0) {

			INP_WLOCK_ASSERT(inp);
			error = in6_pcbsetport(laddr, inp, td->td_ucred);
			if (error != 0) {
				/* Undo an address bind that may have occurred. */
				inp->in6p_laddr = in6addr_any;
				goto release;
			}
		}
		faddr = &sin6->sin6_addr;
		fport = sin6->sin6_port; /* allow 0 port */

	} else {
		if (IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr)) {
			error = ENOTCONN;
			goto release;
		}
		laddr = &inp->in6p_laddr;
		faddr = &inp->in6p_faddr;
		fport = inp->inp_fport;
	}

	ulen = m->m_pkthdr.len;
	plen = sizeof(struct udphdr) + ulen;
	hlen = sizeof(struct ip6_hdr);

	/*
	 * Calculate data length and get a mbuf
	 * for UDP and IP6 headers.
	 */
	M_PREPEND(m, hlen + sizeof(struct udphdr), M_NOWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto release;
	}

	/*
	 * Stuff checksum and output datagram.
	 */
	cscov = cscov_partial = 0;
	udp6 = (struct udphdr *)(mtod(m, caddr_t) + hlen);
	udp6->uh_sport = inp->inp_lport; /* lport is always set in the PCB */
	udp6->uh_dport = fport;
	if (nxt == IPPROTO_UDPLITE) {
		struct udpcb *up;

		up = intoudpcb(inp);
		cscov = up->u_txcslen;
		if (cscov >= plen)
			cscov = 0;
		udp6->uh_ulen = htons(cscov);
		/*
		 * For UDP-Lite, checksum coverage length of zero means
		 * the entire UDPLite packet is covered by the checksum.
		 */
		cscov_partial = (cscov == 0) ? 0 : 1;
	} else if (plen <= 0xffff)
		udp6->uh_ulen = htons((u_short)plen);
	else
		udp6->uh_ulen = 0;
	udp6->uh_sum = 0;

	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_flow	= inp->inp_flow & IPV6_FLOWINFO_MASK;
	ip6->ip6_vfc	&= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc	|= IPV6_VERSION;
	ip6->ip6_plen	= htons((u_short)plen);
	ip6->ip6_nxt	= nxt;
	ip6->ip6_hlim	= in6_selecthlim(inp, NULL);
	ip6->ip6_src	= *laddr;
	ip6->ip6_dst	= *faddr;

#ifdef MAC
	mac_inpcb_create_mbuf(inp, m);
#endif

	if (cscov_partial) {
		if ((udp6->uh_sum = in6_cksum_partial(m, nxt,
		    sizeof(struct ip6_hdr), plen, cscov)) == 0)
			udp6->uh_sum = 0xffff;
	} else {
		udp6->uh_sum = in6_cksum_pseudo(ip6, plen, nxt, 0);
		m->m_pkthdr.csum_flags = CSUM_UDP_IPV6;
		m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum);
	}

	flags = 0;
#ifdef	RSS
	{
		uint32_t hash_val, hash_type;
		uint8_t pr;

		pr = inp->inp_socket->so_proto->pr_protocol;
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
		if (rss_proto_software_hash_v6(faddr, laddr, fport,
		    inp->inp_lport, pr, &hash_val, &hash_type) == 0) {
			m->m_pkthdr.flowid = hash_val;
			M_HASHTYPE_SET(m, hash_type);
		}

		/*
		 * Don't override with the inp cached flowid.
		 *
		 * Until the whole UDP path is vetted, it may actually
		 * be incorrect.
		 */
		flags |= IP_NODEFAULTFLOWID;
	}
#endif

	UDPSTAT_INC(udps_opackets);
	if (unlock_udbinfo == UH_WLOCKED)
		INP_HASH_WUNLOCK(pcbinfo);
	else if (unlock_udbinfo == UH_RLOCKED)
		INP_HASH_RUNLOCK_ET(pcbinfo, et);
	if (nxt == IPPROTO_UDPLITE)
		UDPLITE_PROBE(send, NULL, inp, ip6, inp, udp6);
	else
		UDP_PROBE(send, NULL, inp, ip6, inp, udp6);
	error = ip6_output(m, optp,
	    (unlock_inp == UH_WLOCKED) ? &inp->inp_route6 : NULL, flags,
	    inp->in6p_moptions, NULL, inp);
	if (unlock_inp == UH_WLOCKED)
		INP_WUNLOCK(inp);
	else
		INP_RUNLOCK(inp);

	if (control) {
		ip6_clearpktopts(&opt, -1);
		m_freem(control);
	}
	return (error);

release:
	if (unlock_udbinfo == UH_WLOCKED) {
		KASSERT(unlock_inp == UH_WLOCKED, ("%s: excl udbinfo lock, "
		    "non-excl inp lock: pcbinfo %p %#x inp %p %#x",
		    __func__, pcbinfo, unlock_udbinfo, inp, unlock_inp));
		INP_HASH_WUNLOCK(pcbinfo);
		INP_WUNLOCK(inp);
	} else if (unlock_udbinfo == UH_RLOCKED) {
		KASSERT(unlock_inp == UH_RLOCKED, ("%s: non-excl udbinfo lock, "
		    "excl inp lock: pcbinfo %p %#x inp %p %#x",
		    __func__, pcbinfo, unlock_udbinfo, inp, unlock_inp));
		INP_HASH_RUNLOCK_ET(pcbinfo, et);
		INP_RUNLOCK(inp);
	} else if (unlock_inp == UH_WLOCKED)
		INP_WUNLOCK(inp);
	else
		INP_RUNLOCK(inp);
	if (control) {
		ip6_clearpktopts(&opt, -1);
		m_freem(control);
	}
	m_freem(m);

	return (error);
}

static void
udp6_abort(struct socket *so)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp6_abort: inp == NULL"));

	INP_WLOCK(inp);
#ifdef INET
	if (inp->inp_vflag & INP_IPV4) {
		struct pr_usrreqs *pru;
		uint8_t nxt;

		nxt = (inp->inp_socket->so_proto->pr_protocol == IPPROTO_UDP) ?
		    IPPROTO_UDP : IPPROTO_UDPLITE;
		INP_WUNLOCK(inp);
		pru = inetsw[ip_protox[nxt]].pr_usrreqs;
		(*pru->pru_abort)(so);
		return;
	}
#endif

	if (!IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr)) {
		INP_HASH_WLOCK(pcbinfo);
		in6_pcbdisconnect(inp);
		inp->in6p_laddr = in6addr_any;
		INP_HASH_WUNLOCK(pcbinfo);
		soisdisconnected(so);
	}
	INP_WUNLOCK(inp);
}

static int
udp6_attach(struct socket *so, int proto, struct thread *td)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;
	int error;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp == NULL, ("udp6_attach: inp != NULL"));

	if (so->so_snd.sb_hiwat == 0 || so->so_rcv.sb_hiwat == 0) {
		error = soreserve(so, udp_sendspace, udp_recvspace);
		if (error)
			return (error);
	}
	INP_INFO_WLOCK(pcbinfo);
	error = in_pcballoc(so, pcbinfo);
	if (error) {
		INP_INFO_WUNLOCK(pcbinfo);
		return (error);
	}
	inp = (struct inpcb *)so->so_pcb;
	inp->inp_vflag |= INP_IPV6;
	if ((inp->inp_flags & IN6P_IPV6_V6ONLY) == 0)
		inp->inp_vflag |= INP_IPV4;
	inp->in6p_hops = -1;	/* use kernel default */
	inp->in6p_cksum = -1;	/* just to be sure */
	/*
	 * XXX: ugly!!
	 * IPv4 TTL initialization is necessary for an IPv6 socket as well,
	 * because the socket may be bound to an IPv6 wildcard address,
	 * which may match an IPv4-mapped IPv6 address.
	 */
	inp->inp_ip_ttl = V_ip_defttl;

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

static int
udp6_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;
	int error;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp6_bind: inp == NULL"));

	INP_WLOCK(inp);
	INP_HASH_WLOCK(pcbinfo);
	inp->inp_vflag &= ~INP_IPV4;
	inp->inp_vflag |= INP_IPV6;
	if ((inp->inp_flags & IN6P_IPV6_V6ONLY) == 0) {
		struct sockaddr_in6 *sin6_p;

		sin6_p = (struct sockaddr_in6 *)nam;

		if (IN6_IS_ADDR_UNSPECIFIED(&sin6_p->sin6_addr))
			inp->inp_vflag |= INP_IPV4;
#ifdef INET
		else if (IN6_IS_ADDR_V4MAPPED(&sin6_p->sin6_addr)) {
			struct sockaddr_in sin;

			in6_sin6_2_sin(&sin, sin6_p);
			inp->inp_vflag |= INP_IPV4;
			inp->inp_vflag &= ~INP_IPV6;
			error = in_pcbbind(inp, (struct sockaddr *)&sin,
			    td->td_ucred);
			goto out;
		}
#endif
	}

	error = in6_pcbbind(inp, nam, td->td_ucred);
#ifdef INET
out:
#endif
	INP_HASH_WUNLOCK(pcbinfo);
	INP_WUNLOCK(inp);
	return (error);
}

static void
udp6_close(struct socket *so)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp6_close: inp == NULL"));

	INP_WLOCK(inp);
#ifdef INET
	if (inp->inp_vflag & INP_IPV4) {
		struct pr_usrreqs *pru;
		uint8_t nxt;

		nxt = (inp->inp_socket->so_proto->pr_protocol == IPPROTO_UDP) ?
		    IPPROTO_UDP : IPPROTO_UDPLITE;
		INP_WUNLOCK(inp);
		pru = inetsw[ip_protox[nxt]].pr_usrreqs;
		(*pru->pru_disconnect)(so);
		return;
	}
#endif
	if (!IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr)) {
		INP_HASH_WLOCK(pcbinfo);
		in6_pcbdisconnect(inp);
		inp->in6p_laddr = in6addr_any;
		INP_HASH_WUNLOCK(pcbinfo);
		soisdisconnected(so);
	}
	INP_WUNLOCK(inp);
}

static int
udp6_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;
	struct sockaddr_in6 *sin6;
	int error;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	sin6 = (struct sockaddr_in6 *)nam;
	KASSERT(inp != NULL, ("udp6_connect: inp == NULL"));

	/*
	 * XXXRW: Need to clarify locking of v4/v6 flags.
	 */
	INP_WLOCK(inp);
#ifdef INET
	if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
		struct sockaddr_in sin;

		if ((inp->inp_flags & IN6P_IPV6_V6ONLY) != 0) {
			error = EINVAL;
			goto out;
		}
		if ((inp->inp_vflag & INP_IPV4) == 0) {
			error = EAFNOSUPPORT;
			goto out;
		}
		if (inp->inp_faddr.s_addr != INADDR_ANY) {
			error = EISCONN;
			goto out;
		}
		in6_sin6_2_sin(&sin, sin6);
		inp->inp_vflag |= INP_IPV4;
		inp->inp_vflag &= ~INP_IPV6;
		error = prison_remote_ip4(td->td_ucred, &sin.sin_addr);
		if (error != 0)
			goto out;
		INP_HASH_WLOCK(pcbinfo);
		error = in_pcbconnect(inp, (struct sockaddr *)&sin,
		    td->td_ucred);
		INP_HASH_WUNLOCK(pcbinfo);
		if (error == 0)
			soisconnected(so);
		goto out;
	} else {
		if ((inp->inp_vflag & INP_IPV6) == 0) {
			error = EAFNOSUPPORT;
			goto out;
		}
	}
#endif
	if (!IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr)) {
		error = EISCONN;
		goto out;
	}
	inp->inp_vflag &= ~INP_IPV4;
	inp->inp_vflag |= INP_IPV6;
	error = prison_remote_ip6(td->td_ucred, &sin6->sin6_addr);
	if (error != 0)
		goto out;
	INP_HASH_WLOCK(pcbinfo);
	error = in6_pcbconnect(inp, nam, td->td_ucred);
	INP_HASH_WUNLOCK(pcbinfo);
	if (error == 0)
		soisconnected(so);
out:
	INP_WUNLOCK(inp);
	return (error);
}

static void
udp6_detach(struct socket *so)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;
	struct udpcb *up;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp6_detach: inp == NULL"));

	INP_INFO_WLOCK(pcbinfo);
	INP_WLOCK(inp);
	up = intoudpcb(inp);
	KASSERT(up != NULL, ("%s: up == NULL", __func__));
	in_pcbdetach(inp);
	in_pcbfree(inp);
	INP_INFO_WUNLOCK(pcbinfo);
	udp_discardcb(up);
}

static int
udp6_disconnect(struct socket *so)
{
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;

	pcbinfo = udp_get_inpcbinfo(so->so_proto->pr_protocol);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("udp6_disconnect: inp == NULL"));

	INP_WLOCK(inp);
#ifdef INET
	if (inp->inp_vflag & INP_IPV4) {
		struct pr_usrreqs *pru;
		uint8_t nxt;

		nxt = (inp->inp_socket->so_proto->pr_protocol == IPPROTO_UDP) ?
		    IPPROTO_UDP : IPPROTO_UDPLITE;
		INP_WUNLOCK(inp);
		pru = inetsw[ip_protox[nxt]].pr_usrreqs;
		(void)(*pru->pru_disconnect)(so);
		return (0);
	}
#endif

	if (IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr)) {
		INP_WUNLOCK(inp);
		return (ENOTCONN);
	}

	INP_HASH_WLOCK(pcbinfo);
	in6_pcbdisconnect(inp);
	inp->in6p_laddr = in6addr_any;
	INP_HASH_WUNLOCK(pcbinfo);
	SOCK_LOCK(so);
	so->so_state &= ~SS_ISCONNECTED;		/* XXX */
	SOCK_UNLOCK(so);
	INP_WUNLOCK(inp);
	return (0);
}

static int
udp6_send(struct socket *so, int flags, struct mbuf *m,
    struct sockaddr *addr, struct mbuf *control, struct thread *td)
{
	int error;

	if (addr) {
		if (addr->sa_len != sizeof(struct sockaddr_in6)) {
			error = EINVAL;
			goto bad;
		}
		if (addr->sa_family != AF_INET6) {
			error = EAFNOSUPPORT;
			goto bad;
		}
	}

	return (udp6_output(so, flags, m, addr, control, td));

bad:
	if (control)
		m_freem(control);
	m_freem(m);
	return (error);
}

struct pr_usrreqs udp6_usrreqs = {
	.pru_abort =		udp6_abort,
	.pru_attach =		udp6_attach,
	.pru_bind =		udp6_bind,
	.pru_connect =		udp6_connect,
	.pru_control =		in6_control,
	.pru_detach =		udp6_detach,
	.pru_disconnect =	udp6_disconnect,
	.pru_peeraddr =		in6_mapped_peeraddr,
	.pru_send =		udp6_send,
	.pru_shutdown =		udp_shutdown,
	.pru_sockaddr =		in6_mapped_sockaddr,
	.pru_soreceive =	soreceive_dgram,
	.pru_sosend =		sosend_dgram,
	.pru_sosetlabel =	in_pcbsosetlabel,
	.pru_close =		udp6_close
};
