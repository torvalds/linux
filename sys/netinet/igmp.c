/*	$OpenBSD: igmp.c,v 1.88 2025/07/08 00:47:41 jsg Exp $	*/
/*	$NetBSD: igmp.c,v 1.15 1996/02/13 23:41:25 christos Exp $	*/

/*
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
 */

/*
 * Copyright (c) 1988 Stephen Deering.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Stephen Deering of Stanford University.
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
 *	@(#)igmp.c	8.2 (Berkeley) 5/3/95
 */

/*
 * Internet Group Management Protocol (IGMP) routines.
 *
 * Written by Steve Deering, Stanford, May 1988.
 * Modified by Rosen Sharma, Stanford, Aug 1994.
 * Modified by Bill Fenner, Xerox PARC, Feb 1995.
 *
 * MULTICAST Revision: 1.3
 */

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/igmp.h>
#include <netinet/igmp_var.h>

#define IP_MULTICASTOPTS	0

int	igmp_timers_are_running;	/* [a] shortcut for fast timer */
static LIST_HEAD(, router_info) rti_head;
static struct mbuf *router_alert;
struct cpumem *igmpcounters;

int igmp_checktimer(struct ifnet *);
void igmp_sendpkt(struct ifnet *, struct in_multi *, int, in_addr_t);
int rti_fill(struct in_multi *);
struct router_info * rti_find(struct ifnet *);
int igmp_input_if(struct ifnet *, struct mbuf **, int *, int, int,
    struct netstack *);
int igmp_sysctl_igmpstat(void *, size_t *, void *);

void
igmp_init(void)
{
	struct ipoption *ra;

	igmp_timers_are_running = 0;
	LIST_INIT(&rti_head);

	igmpcounters = counters_alloc(igps_ncounters);
	router_alert = m_get(M_WAIT, MT_DATA);

	/*
	 * Construct a Router Alert option (RAO) to use in report
	 * messages as required by RFC2236.  This option has the
	 * following format:
	 *
	 *	| 10010100 | 00000100 |  2 octet value  |
	 *
	 * where a value of "0" indicates that routers shall examine
	 * the packet.
	 */
	ra = mtod(router_alert, struct ipoption *);
	ra->ipopt_dst.s_addr = INADDR_ANY;
	ra->ipopt_list[0] = IPOPT_RA;
	ra->ipopt_list[1] = 0x04;
	ra->ipopt_list[2] = 0x00;
	ra->ipopt_list[3] = 0x00;
	router_alert->m_len = sizeof(ra->ipopt_dst) + ra->ipopt_list[1];
}

int
rti_fill(struct in_multi *inm)
{
	struct router_info *rti;

	LIST_FOREACH(rti, &rti_head, rti_list) {
		if (rti->rti_ifidx == inm->inm_ifidx) {
			inm->inm_rti = rti;
			if (rti->rti_type == IGMP_v1_ROUTER)
				return (IGMP_v1_HOST_MEMBERSHIP_REPORT);
			else
				return (IGMP_v2_HOST_MEMBERSHIP_REPORT);
		}
	}

	rti = malloc(sizeof(*rti), M_MRTABLE, M_WAITOK);
	rti->rti_ifidx = inm->inm_ifidx;
	rti->rti_type = IGMP_v2_ROUTER;
	LIST_INSERT_HEAD(&rti_head, rti, rti_list);
	inm->inm_rti = rti;
	return (IGMP_v2_HOST_MEMBERSHIP_REPORT);
}

struct router_info *
rti_find(struct ifnet *ifp)
{
	struct router_info *rti;

	KERNEL_ASSERT_LOCKED();
	LIST_FOREACH(rti, &rti_head, rti_list) {
		if (rti->rti_ifidx == ifp->if_index)
			return (rti);
	}

	rti = malloc(sizeof(*rti), M_MRTABLE, M_NOWAIT);
	if (rti == NULL)
		return (NULL);
	rti->rti_ifidx = ifp->if_index;
	rti->rti_type = IGMP_v2_ROUTER;
	LIST_INSERT_HEAD(&rti_head, rti, rti_list);
	return (rti);
}

void
rti_delete(struct ifnet *ifp)
{
	struct router_info *rti, *trti;

	LIST_FOREACH_SAFE(rti, &rti_head, rti_list, trti) {
		if (rti->rti_ifidx == ifp->if_index) {
			LIST_REMOVE(rti, rti_list);
			free(rti, M_MRTABLE, sizeof(*rti));
			break;
		}
	}
}

int
igmp_input(struct mbuf **mp, int *offp, int proto, int af, struct netstack *ns)
{
	struct ifnet *ifp;

	igmpstat_inc(igps_rcv_total);

	ifp = if_get((*mp)->m_pkthdr.ph_ifidx);
	if (ifp == NULL) {
		m_freemp(mp);
		return IPPROTO_DONE;
	}

	KERNEL_LOCK();
	proto = igmp_input_if(ifp, mp, offp, proto, af, ns);
	KERNEL_UNLOCK();
	if_put(ifp);
	return proto;
}

int
igmp_input_if(struct ifnet *ifp, struct mbuf **mp, int *offp, int proto,
    int af, struct netstack *ns)
{
	struct mbuf *m = *mp;
	int iphlen = *offp;
	struct ip *ip = mtod(m, struct ip *);
	struct igmp *igmp;
	int igmplen;
	int minlen;
	struct ifmaddr *ifma;
	struct in_multi *inm;
	struct router_info *rti;
	struct in_ifaddr *ia;
	int timer, running = 0;

	igmplen = ntohs(ip->ip_len) - iphlen;

	/*
	 * Validate lengths
	 */
	if (igmplen < IGMP_MINLEN) {
		igmpstat_inc(igps_rcv_tooshort);
		m_freem(m);
		return IPPROTO_DONE;
	}
	minlen = iphlen + IGMP_MINLEN;
	if ((m->m_flags & M_EXT || m->m_len < minlen) &&
	    (m = *mp = m_pullup(m, minlen)) == NULL) {
		igmpstat_inc(igps_rcv_tooshort);
		return IPPROTO_DONE;
	}

	/*
	 * Validate checksum
	 */
	m->m_data += iphlen;
	m->m_len -= iphlen;
	igmp = mtod(m, struct igmp *);
	if (in_cksum(m, igmplen)) {
		igmpstat_inc(igps_rcv_badsum);
		m_freem(m);
		return IPPROTO_DONE;
	}
	m->m_data -= iphlen;
	m->m_len += iphlen;
	ip = mtod(m, struct ip *);

	switch (igmp->igmp_type) {

	case IGMP_HOST_MEMBERSHIP_QUERY:
		igmpstat_inc(igps_rcv_queries);

		if (ifp->if_flags & IFF_LOOPBACK)
			break;

		if (igmp->igmp_code == 0) {
			rti = rti_find(ifp);
			if (rti == NULL) {
				m_freem(m);
				return IPPROTO_DONE;
			}
			rti->rti_type = IGMP_v1_ROUTER;
			rti->rti_age = 0;

			if (ip->ip_dst.s_addr != INADDR_ALLHOSTS_GROUP) {
				igmpstat_inc(igps_rcv_badqueries);
				m_freem(m);
				return IPPROTO_DONE;
			}

			/*
			 * Start the timers in all of our membership records
			 * for the interface on which the query arrived,
			 * except those that are already running and those
			 * that belong to a "local" group (224.0.0.X).
			 */
			TAILQ_FOREACH(ifma, &ifp->if_maddrlist, ifma_list) {
				if (ifma->ifma_addr->sa_family != AF_INET)
					continue;
				inm = ifmatoinm(ifma);
				if (inm->inm_timer == 0 &&
				    !IN_LOCAL_GROUP(inm->inm_addr.s_addr)) {
					inm->inm_state = IGMP_DELAYING_MEMBER;
					inm->inm_timer = IGMP_RANDOM_DELAY(
					    IGMP_MAX_HOST_REPORT_DELAY * PR_FASTHZ);
					running = 1;
				}
			}
		} else {
			if (!IN_MULTICAST(ip->ip_dst.s_addr)) {
				igmpstat_inc(igps_rcv_badqueries);
				m_freem(m);
				return IPPROTO_DONE;
			}

			timer = igmp->igmp_code * PR_FASTHZ / IGMP_TIMER_SCALE;
			if (timer == 0)
				timer = 1;

			/*
			 * Start the timers in all of our membership records
			 * for the interface on which the query arrived,
			 * except those that are already running and those
			 * that belong to a "local" group (224.0.0.X).  For
			 * timers already running, check if they need to be
			 * reset.
			 */
			TAILQ_FOREACH(ifma, &ifp->if_maddrlist, ifma_list) {
				if (ifma->ifma_addr->sa_family != AF_INET)
					continue;
				inm = ifmatoinm(ifma);
				if (!IN_LOCAL_GROUP(inm->inm_addr.s_addr) &&
				    (ip->ip_dst.s_addr == INADDR_ALLHOSTS_GROUP ||
				     ip->ip_dst.s_addr == inm->inm_addr.s_addr)) {
					switch (inm->inm_state) {
					case IGMP_DELAYING_MEMBER:
						if (inm->inm_timer <= timer)
							break;
						/* FALLTHROUGH */
					case IGMP_IDLE_MEMBER:
					case IGMP_LAZY_MEMBER:
					case IGMP_AWAKENING_MEMBER:
						inm->inm_state =
						    IGMP_DELAYING_MEMBER;
						inm->inm_timer =
						    IGMP_RANDOM_DELAY(timer);
						running = 1;
						break;
					case IGMP_SLEEPING_MEMBER:
						inm->inm_state =
						    IGMP_AWAKENING_MEMBER;
						break;
					}
				}
			}
		}

		break;

	case IGMP_v1_HOST_MEMBERSHIP_REPORT:
		igmpstat_inc(igps_rcv_reports);

		if (ifp->if_flags & IFF_LOOPBACK)
			break;

		if (!IN_MULTICAST(igmp->igmp_group.s_addr) ||
		    igmp->igmp_group.s_addr != ip->ip_dst.s_addr) {
			igmpstat_inc(igps_rcv_badreports);
			m_freem(m);
			return IPPROTO_DONE;
		}

		/*
		 * KLUDGE: if the IP source address of the report has an
		 * unspecified (i.e., zero) subnet number, as is allowed for
		 * a booting host, replace it with the correct subnet number
		 * so that a process-level multicast routing daemon can
		 * determine which subnet it arrived from.  This is necessary
		 * to compensate for the lack of any way for a process to
		 * determine the arrival interface of an incoming packet.
		 */
		if ((ip->ip_src.s_addr & IN_CLASSA_NET) == 0) {
			IFP_TO_IA(ifp, ia);
			if (ia)
				ip->ip_src.s_addr = ia->ia_net;
		}

		/*
		 * If we belong to the group being reported, stop
		 * our timer for that group.
		 */
		IN_LOOKUP_MULTI(igmp->igmp_group, ifp, inm);
		if (inm != NULL) {
			inm->inm_timer = 0;
			igmpstat_inc(igps_rcv_ourreports);

			switch (inm->inm_state) {
			case IGMP_IDLE_MEMBER:
			case IGMP_LAZY_MEMBER:
			case IGMP_AWAKENING_MEMBER:
			case IGMP_SLEEPING_MEMBER:
				inm->inm_state = IGMP_SLEEPING_MEMBER;
				break;
			case IGMP_DELAYING_MEMBER:
				if (inm->inm_rti->rti_type == IGMP_v1_ROUTER)
					inm->inm_state = IGMP_LAZY_MEMBER;
				else
					inm->inm_state = IGMP_SLEEPING_MEMBER;
				break;
			}
		}

		break;

	case IGMP_v2_HOST_MEMBERSHIP_REPORT:
#ifdef MROUTING
		/*
		 * Make sure we don't hear our own membership report.  Fast
		 * leave requires knowing that we are the only member of a
		 * group.
		 */
		IFP_TO_IA(ifp, ia);
		if (ia && ip->ip_src.s_addr == ia->ia_addr.sin_addr.s_addr)
			break;
#endif

		igmpstat_inc(igps_rcv_reports);

		if (ifp->if_flags & IFF_LOOPBACK)
			break;

		if (!IN_MULTICAST(igmp->igmp_group.s_addr) ||
		    igmp->igmp_group.s_addr != ip->ip_dst.s_addr) {
			igmpstat_inc(igps_rcv_badreports);
			m_freem(m);
			return IPPROTO_DONE;
		}

		/*
		 * KLUDGE: if the IP source address of the report has an
		 * unspecified (i.e., zero) subnet number, as is allowed for
		 * a booting host, replace it with the correct subnet number
		 * so that a process-level multicast routing daemon can
		 * determine which subnet it arrived from.  This is necessary
		 * to compensate for the lack of any way for a process to
		 * determine the arrival interface of an incoming packet.
		 */
		if ((ip->ip_src.s_addr & IN_CLASSA_NET) == 0) {
#ifndef MROUTING
			IFP_TO_IA(ifp, ia);
#endif
			if (ia)
				ip->ip_src.s_addr = ia->ia_net;
		}

		/*
		 * If we belong to the group being reported, stop
		 * our timer for that group.
		 */
		IN_LOOKUP_MULTI(igmp->igmp_group, ifp, inm);
		if (inm != NULL) {
			inm->inm_timer = 0;
			igmpstat_inc(igps_rcv_ourreports);

			switch (inm->inm_state) {
			case IGMP_DELAYING_MEMBER:
			case IGMP_IDLE_MEMBER:
			case IGMP_AWAKENING_MEMBER:
				inm->inm_state = IGMP_LAZY_MEMBER;
				break;
			case IGMP_LAZY_MEMBER:
			case IGMP_SLEEPING_MEMBER:
				break;
			}
		}

		break;

	}

	if (running) {
		membar_producer();
		atomic_store_int(&igmp_timers_are_running, running);
	}

	/*
	 * Pass all valid IGMP packets up to any process(es) listening
	 * on a raw IGMP socket.
	 */
	return rip_input(mp, offp, proto, af, ns);
}

void
igmp_joingroup(struct in_multi *inm, struct ifnet *ifp)
{
	int i, running = 0;

	inm->inm_state = IGMP_IDLE_MEMBER;

	if (!IN_LOCAL_GROUP(inm->inm_addr.s_addr) &&
	    (ifp->if_flags & IFF_LOOPBACK) == 0) {
		i = rti_fill(inm);
		igmp_sendpkt(ifp, inm, i, 0);
		inm->inm_state = IGMP_DELAYING_MEMBER;
		inm->inm_timer = IGMP_RANDOM_DELAY(
		    IGMP_MAX_HOST_REPORT_DELAY * PR_FASTHZ);
		running = 1;
	} else
		inm->inm_timer = 0;

	if (running) {
		membar_producer();
		atomic_store_int(&igmp_timers_are_running, running);
	}
}

void
igmp_leavegroup(struct in_multi *inm, struct ifnet *ifp)
{
	switch (inm->inm_state) {
	case IGMP_DELAYING_MEMBER:
	case IGMP_IDLE_MEMBER:
		if (!IN_LOCAL_GROUP(inm->inm_addr.s_addr) &&
		    (ifp->if_flags & IFF_LOOPBACK) == 0)
			if (inm->inm_rti->rti_type != IGMP_v1_ROUTER)
				igmp_sendpkt(ifp, inm,
				    IGMP_HOST_LEAVE_MESSAGE,
				    INADDR_ALLROUTERS_GROUP);
		break;
	case IGMP_LAZY_MEMBER:
	case IGMP_AWAKENING_MEMBER:
	case IGMP_SLEEPING_MEMBER:
		break;
	}
}

void
igmp_fasttimo(void)
{
	struct ifnet *ifp;
	int running = 0;

	/*
	 * Quick check to see if any work needs to be done, in order
	 * to minimize the overhead of fasttimo processing.
	 * Variable igmp_timers_are_running is read atomically, but without
	 * lock intentionally.  In case it is not set due to MP races, we may
	 * miss to check the timers.  Then run the loop at next fast timeout.
	 */
	if (!atomic_load_int(&igmp_timers_are_running))
		return;
	membar_consumer();

	NET_LOCK();

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (igmp_checktimer(ifp))
			running = 1;
	}

	membar_producer();
	atomic_store_int(&igmp_timers_are_running, running);

	NET_UNLOCK();
}

int
igmp_checktimer(struct ifnet *ifp)
{
	struct in_multi *inm;
	struct ifmaddr *ifma;
	int running = 0;

	NET_ASSERT_LOCKED();

	TAILQ_FOREACH(ifma, &ifp->if_maddrlist, ifma_list) {
		if (ifma->ifma_addr->sa_family != AF_INET)
			continue;
		inm = ifmatoinm(ifma);
		if (inm->inm_timer == 0) {
			/* do nothing */
		} else if (--inm->inm_timer == 0) {
			if (inm->inm_state == IGMP_DELAYING_MEMBER) {
				if (inm->inm_rti->rti_type == IGMP_v1_ROUTER)
					igmp_sendpkt(ifp, inm,
					    IGMP_v1_HOST_MEMBERSHIP_REPORT, 0);
				else
					igmp_sendpkt(ifp, inm,
					    IGMP_v2_HOST_MEMBERSHIP_REPORT, 0);
				inm->inm_state = IGMP_IDLE_MEMBER;
			}
		} else {
			running = 1;
		}
	}

	return (running);
}

void
igmp_slowtimo(void)
{
	struct router_info *rti;

	NET_LOCK();

	LIST_FOREACH(rti, &rti_head, rti_list) {
		if (rti->rti_type == IGMP_v1_ROUTER &&
		    ++rti->rti_age >= IGMP_AGE_THRESHOLD) {
			rti->rti_type = IGMP_v2_ROUTER;
		}
	}

	NET_UNLOCK();
}

void
igmp_sendpkt(struct ifnet *ifp, struct in_multi *inm, int type,
    in_addr_t addr)
{
	struct mbuf *m;
	struct igmp *igmp;
	struct ip *ip;
	struct ip_moptions imo;

	MGETHDR(m, M_DONTWAIT, MT_HEADER);
	if (m == NULL)
		return;

	/*
	 * Assume max_linkhdr + sizeof(struct ip) + IGMP_MINLEN
	 * is smaller than mbuf size returned by MGETHDR.
	 */
	m->m_data += max_linkhdr;
	m->m_len = sizeof(struct ip) + IGMP_MINLEN;
	m->m_pkthdr.len = sizeof(struct ip) + IGMP_MINLEN;

	ip = mtod(m, struct ip *);
	ip->ip_tos = 0;
	ip->ip_len = htons(sizeof(struct ip) + IGMP_MINLEN);
	ip->ip_off = 0;
	ip->ip_p = IPPROTO_IGMP;
	ip->ip_src.s_addr = INADDR_ANY;
	if (addr) {
		ip->ip_dst.s_addr = addr;
	} else {
		ip->ip_dst = inm->inm_addr;
	}

	m->m_data += sizeof(struct ip);
	m->m_len -= sizeof(struct ip);
	igmp = mtod(m, struct igmp *);
	igmp->igmp_type = type;
	igmp->igmp_code = 0;
	igmp->igmp_group = inm->inm_addr;
	igmp->igmp_cksum = 0;
	igmp->igmp_cksum = in_cksum(m, IGMP_MINLEN);
	m->m_data -= sizeof(struct ip);
	m->m_len += sizeof(struct ip);

	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;
	imo.imo_ifidx = inm->inm_ifidx;
	imo.imo_ttl = 1;

	/*
	 * Request loopback of the report if we are acting as a multicast
	 * router, so that the process-level routing daemon can hear it.
	 */
#ifdef MROUTING
	imo.imo_loop = (ip_mrouter[ifp->if_rdomain] != NULL);
#else
	imo.imo_loop = 0;
#endif /* MROUTING */

	ip_output(m, router_alert, NULL, IP_MULTICASTOPTS, &imo, NULL, 0);

	igmpstat_inc(igps_snd_reports);
}

#ifndef SMALL_KERNEL
/*
 * Sysctl for igmp variables.
 */
int
igmp_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case IGMPCTL_STATS:
		return (igmp_sysctl_igmpstat(oldp, oldlenp, newp));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

int
igmp_sysctl_igmpstat(void *oldp, size_t *oldlenp, void *newp)
{
	uint64_t counters[igps_ncounters];
	struct igmpstat igmpstat;
	u_long *words = (u_long *)&igmpstat;
	int i;

	CTASSERT(sizeof(igmpstat) == (nitems(counters) * sizeof(u_long)));
	memset(&igmpstat, 0, sizeof igmpstat);
	counters_read(igmpcounters, counters, nitems(counters), NULL);

	for (i = 0; i < nitems(counters); i++)
		words[i] = (u_long)counters[i];

	return (sysctl_rdstruct(oldp, oldlenp, newp,
	    &igmpstat, sizeof(igmpstat)));
}
#endif /* SMALL_KERNEL */
