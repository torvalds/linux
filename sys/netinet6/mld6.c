/*	$OpenBSD: mld6.c,v 1.68 2025/07/08 00:47:41 jsg Exp $	*/
/*	$KAME: mld6.c,v 1.26 2001/02/16 14:50:35 itojun Exp $	*/

/*
 * Copyright (C) 1998 WIDE Project.
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
 *	@(#)igmp.c	8.1 (Berkeley) 7/19/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/protosw.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>
#include <netinet6/mld6.h>
#include <netinet6/mld6_var.h>

static struct ip6_pktopts ip6_opts;
int	mld6_timers_are_running;	/* [a] shortcut for fast timer */

int mld6_checktimer(struct ifnet *);
static void mld6_sendpkt(struct in6_multi *, int, const struct in6_addr *);

void
mld6_init(void)
{
	static u_int8_t hbh_buf[8];
	struct ip6_hbh *hbh = (struct ip6_hbh *)hbh_buf;
	u_int16_t rtalert_code = htons((u_int16_t)IP6OPT_RTALERT_MLD);

	mld6_timers_are_running = 0;

	/* ip6h_nxt will be fill in later */
	hbh->ip6h_len = 0;	/* (8 >> 3) - 1 */

	/* XXX: grotty hard coding... */
	hbh_buf[2] = IP6OPT_PADN;	/* 2 byte padding */
	hbh_buf[3] = 0;
	hbh_buf[4] = IP6OPT_ROUTER_ALERT;
	hbh_buf[5] = IP6OPT_RTALERT_LEN - 2;
	memcpy(&hbh_buf[6], (caddr_t)&rtalert_code, sizeof(u_int16_t));

	ip6_initpktopts(&ip6_opts);
	ip6_opts.ip6po_hbh = hbh;
}

void
mld6_start_listening(struct in6_multi *in6m)
{
	/* XXX: These are necessary for KAME's link-local hack */
	struct in6_addr all_nodes = IN6ADDR_LINKLOCAL_ALLNODES_INIT;
	int running = 0;

	/*
	 * RFC2710 page 10:
	 * The node never sends a Report or Done for the link-scope all-nodes
	 * address.
	 * MLD messages are never sent for multicast addresses whose scope is 0
	 * (reserved) or 1 (node-local).
	 */
	all_nodes.s6_addr16[1] = htons(in6m->in6m_ifidx);
	if (IN6_ARE_ADDR_EQUAL(&in6m->in6m_addr, &all_nodes) ||
	    __IPV6_ADDR_MC_SCOPE(&in6m->in6m_addr) <
	    __IPV6_ADDR_SCOPE_LINKLOCAL) {
		in6m->in6m_timer = 0;
		in6m->in6m_state = MLD_OTHERLISTENER;
	} else {
		mld6_sendpkt(in6m, MLD_LISTENER_REPORT, NULL);
		in6m->in6m_timer =
		    MLD_RANDOM_DELAY(MLD_V1_MAX_RI *
		    PR_FASTHZ);
		in6m->in6m_state = MLD_IREPORTEDLAST;
		running = 1;
	}

	if (running) {
		membar_producer();
		atomic_store_int(&mld6_timers_are_running, running);
	}
}

void
mld6_stop_listening(struct in6_multi *in6m)
{
	/* XXX: These are necessary for KAME's link-local hack */
	struct in6_addr all_nodes = IN6ADDR_LINKLOCAL_ALLNODES_INIT;
	struct in6_addr all_routers = IN6ADDR_LINKLOCAL_ALLROUTERS_INIT;

	all_nodes.s6_addr16[1] = htons(in6m->in6m_ifidx);
	/* XXX: necessary when mrouting */
	all_routers.s6_addr16[1] = htons(in6m->in6m_ifidx);

	if (in6m->in6m_state == MLD_IREPORTEDLAST &&
	    (!IN6_ARE_ADDR_EQUAL(&in6m->in6m_addr, &all_nodes)) &&
	    __IPV6_ADDR_MC_SCOPE(&in6m->in6m_addr) >
	    __IPV6_ADDR_SCOPE_INTFACELOCAL)
		mld6_sendpkt(in6m, MLD_LISTENER_DONE, &all_routers);
}

void
mld6_input(struct mbuf *m, int off)
{
	struct ip6_hdr *ip6;
	struct mld_hdr *mldh;
	struct ifnet *ifp;
	struct in6_multi *in6m;
	struct ifmaddr *ifma;
	int timer;		/* timer value in the MLD query header */
	int running = 0;
	/* XXX: These are necessary for KAME's link-local hack */
	struct in6_addr all_nodes = IN6ADDR_LINKLOCAL_ALLNODES_INIT;

	mldh = ip6_exthdr_get(&m, off, sizeof(*mldh));
	if (mldh == NULL) {
		icmp6stat_inc(icp6s_tooshort);
		return;
	}

	/* source address validation */
	ip6 = mtod(m, struct ip6_hdr *);/* in case mpullup */
	if (!IN6_IS_ADDR_LINKLOCAL(&ip6->ip6_src)) {
#if 0
		char src[INET6_ADDRSTRLEN], grp[INET6_ADDRSTRLEN];

		log(LOG_ERR,
		    "mld_input: src %s is not link-local (grp=%s)\n",
		    inet_ntop(AF_INET6, &ip6->ip6_src, src, sizeof(src)),
		    inet_ntop(AF_INET6, &mldh->mld_addr, grp, sizeof(grp)));
#endif
		/*
		 * spec (RFC2710) does not explicitly
		 * specify to discard the packet from a non link-local
		 * source address. But we believe it's expected to do so.
		 */
		m_freem(m);
		return;
	}

	ifp = if_get(m->m_pkthdr.ph_ifidx);
	if (ifp == NULL) {
		m_freem(m);
		return;
	}

	/*
	 * In the MLD6 specification, there are 3 states and a flag.
	 *
	 * In Non-Listener state, we simply don't have a membership record.
	 * In Delaying Listener state, our timer is running (in6m->in6m_timer)
	 * In Idle Listener state, our timer is not running (in6m->in6m_timer==0)
	 *
	 * The flag is in6m->in6m_state, it is set to MLD_OTHERLISTENER if
	 * we have heard a report from another member, or MLD_IREPORTEDLAST
	 * if we sent the last report.
	 */
	switch(mldh->mld_type) {
	case MLD_LISTENER_QUERY:
		if (ifp->if_flags & IFF_LOOPBACK)
			break;

		if (!IN6_IS_ADDR_UNSPECIFIED(&mldh->mld_addr) &&
		    !IN6_IS_ADDR_MULTICAST(&mldh->mld_addr))
			break;	/* print error or log stat? */
		if (IN6_IS_ADDR_MC_LINKLOCAL(&mldh->mld_addr))
			mldh->mld_addr.s6_addr16[1] =
			    htons(ifp->if_index); /* XXX */

		/*
		 * - Start the timers in all of our membership records
		 *   that the query applies to for the interface on
		 *   which the query arrived excl. those that belong
		 *   to the "all-nodes" group (ff02::1).
		 * - Restart any timer that is already running but has
		 *   A value longer than the requested timeout.
		 * - Use the value specified in the query message as
		 *   the maximum timeout.
		 */

		/*
		 * XXX: System timer resolution is too low to handle Max
		 * Response Delay, so set 1 to the internal timer even if
		 * the calculated value equals to zero when Max Response
		 * Delay is positive.
		 */
		timer = ntohs(mldh->mld_maxdelay)*PR_FASTHZ/MLD_TIMER_SCALE;
		if (timer == 0 && mldh->mld_maxdelay)
			timer = 1;
		all_nodes.s6_addr16[1] = htons(ifp->if_index);

		TAILQ_FOREACH(ifma, &ifp->if_maddrlist, ifma_list) {
			if (ifma->ifma_addr->sa_family != AF_INET6)
				continue;
			in6m = ifmatoin6m(ifma);
			if (IN6_ARE_ADDR_EQUAL(&in6m->in6m_addr, &all_nodes) ||
			    __IPV6_ADDR_MC_SCOPE(&in6m->in6m_addr) <
			    __IPV6_ADDR_SCOPE_LINKLOCAL)
				continue;

			if (IN6_IS_ADDR_UNSPECIFIED(&mldh->mld_addr) ||
			    IN6_ARE_ADDR_EQUAL(&mldh->mld_addr,
						&in6m->in6m_addr))
			{
				if (timer == 0) {
					/* send a report immediately */
					mld6_sendpkt(in6m, MLD_LISTENER_REPORT,
					    NULL);
					in6m->in6m_timer = 0; /* reset timer */
					in6m->in6m_state = MLD_IREPORTEDLAST;
				} else if (in6m->in6m_timer == 0 || /* idle */
					in6m->in6m_timer > timer) {
					in6m->in6m_timer =
					    MLD_RANDOM_DELAY(timer);
					running = 1;
				}
			}
		}

		if (IN6_IS_ADDR_MC_LINKLOCAL(&mldh->mld_addr))
			mldh->mld_addr.s6_addr16[1] = 0; /* XXX */
		break;
	case MLD_LISTENER_REPORT:
		/*
		 * For fast leave to work, we have to know that we are the
		 * last person to send a report for this group.  Reports
		 * can potentially get looped back if we are a multicast
		 * router, so discard reports sourced by me.
		 * Note that it is impossible to check IFF_LOOPBACK flag of
		 * ifp for this purpose, since ip6_mloopback pass the physical
		 * interface to if_input_local().
		 */
		if (m->m_flags & M_LOOP) /* XXX: grotty flag, but efficient */
			break;

		if (!IN6_IS_ADDR_MULTICAST(&mldh->mld_addr))
			break;

		if (IN6_IS_ADDR_MC_LINKLOCAL(&mldh->mld_addr))
			mldh->mld_addr.s6_addr16[1] =
				htons(ifp->if_index); /* XXX */
		/*
		 * If we belong to the group being reported, stop
		 * our timer for that group.
		 */
		IN6_LOOKUP_MULTI(mldh->mld_addr, ifp, in6m);
		if (in6m) {
			in6m->in6m_timer = 0; /* transit to idle state */
			in6m->in6m_state = MLD_OTHERLISTENER; /* clear flag */
		}

		if (IN6_IS_ADDR_MC_LINKLOCAL(&mldh->mld_addr))
			mldh->mld_addr.s6_addr16[1] = 0; /* XXX */
		break;
	default:		/* this is impossible */
#if 0
		/*
		 * this case should be impossible because of filtering in
		 * icmp6_input().  But we explicitly disabled this part
		 * just in case.
		 */
		log(LOG_ERR, "mld_input: illegal type(%d)", mldh->mld_type);
#endif
		break;
	}

	if (running) {
		membar_producer();
		atomic_store_int(&mld6_timers_are_running, running);
	}

	if_put(ifp);
	m_freem(m);
}

void
mld6_fasttimeo(void)
{
	struct ifnet *ifp;
	int running = 0;

	/*
	 * Quick check to see if any work needs to be done, in order
	 * to minimize the overhead of fasttimo processing.
	 * Variable mld6_timers_are_running is read atomically, but without
	 * lock intentionally.  In case it is not set due to MP races, we may
	 * miss to check the timers.  Then run the loop at next fast timeout.
	 */
	if (!atomic_load_int(&mld6_timers_are_running))
		return;
	membar_consumer();

	NET_LOCK();

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (mld6_checktimer(ifp))
			running = 1;
	}

	membar_producer();
	atomic_store_int(&mld6_timers_are_running, running);

	NET_UNLOCK();
}

int
mld6_checktimer(struct ifnet *ifp)
{
	struct in6_multi *in6m;
	struct ifmaddr *ifma;
	int running = 0;

	NET_ASSERT_LOCKED();

	TAILQ_FOREACH(ifma, &ifp->if_maddrlist, ifma_list) {
		if (ifma->ifma_addr->sa_family != AF_INET6)
			continue;
		in6m = ifmatoin6m(ifma);
		if (in6m->in6m_timer == 0) {
			/* do nothing */
		} else if (--in6m->in6m_timer == 0) {
			mld6_sendpkt(in6m, MLD_LISTENER_REPORT, NULL);
			in6m->in6m_state = MLD_IREPORTEDLAST;
		} else {
			running = 1;
		}
	}

	return (running);
}

static void
mld6_sendpkt(struct in6_multi *in6m, int type, const struct in6_addr *dst)
{
	struct mbuf *mh, *md;
	struct mld_hdr *mldh;
	struct ip6_hdr *ip6;
	struct ip6_moptions im6o;
	struct in6_ifaddr *ia6;
	struct ifnet *ifp;
	int ignflags;

	ifp = if_get(in6m->in6m_ifidx);
	if (ifp == NULL)
		return;

	/*
	 * At first, find a link local address on the outgoing interface
	 * to use as the source address of the MLD packet.
	 * We do not reject tentative addresses for MLD report to deal with
	 * the case where we first join a link-local address.
	 */
	ignflags = IN6_IFF_DUPLICATED|IN6_IFF_ANYCAST;
	if ((ia6 = in6ifa_ifpforlinklocal(ifp, ignflags)) == NULL) {
		if_put(ifp);
		return;
	}
	if ((ia6->ia6_flags & IN6_IFF_TENTATIVE))
		ia6 = NULL;

	/*
	 * Allocate mbufs to store ip6 header and MLD header.
	 * We allocate 2 mbufs and make chain in advance because
	 * it is more convenient when inserting the hop-by-hop option later.
	 */
	MGETHDR(mh, M_DONTWAIT, MT_HEADER);
	if (mh == NULL) {
		if_put(ifp);
		return;
	}
	MGET(md, M_DONTWAIT, MT_DATA);
	if (md == NULL) {
		m_free(mh);
		if_put(ifp);
		return;
	}
	mh->m_next = md;

	mh->m_pkthdr.ph_ifidx = 0;
	mh->m_pkthdr.ph_rtableid = ifp->if_rdomain;
	mh->m_pkthdr.len = sizeof(struct ip6_hdr) + sizeof(struct mld_hdr);
	mh->m_len = sizeof(struct ip6_hdr);
	m_align(mh, sizeof(struct ip6_hdr));

	/* fill in the ip6 header */
	ip6 = mtod(mh, struct ip6_hdr *);
	ip6->ip6_flow = 0;
	ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc |= IPV6_VERSION;
	/* ip6_plen will be set later */
	ip6->ip6_nxt = IPPROTO_ICMPV6;
	/* ip6_hlim will be set by im6o.im6o_hlim */
	ip6->ip6_src = ia6 ? ia6->ia_addr.sin6_addr : in6addr_any;
	ip6->ip6_dst = dst ? *dst : in6m->in6m_addr;

	/* fill in the MLD header */
	md->m_len = sizeof(struct mld_hdr);
	mldh = mtod(md, struct mld_hdr *);
	mldh->mld_type = type;
	mldh->mld_code = 0;
	mldh->mld_cksum = 0;
	/* XXX: we assume the function will not be called for query messages */
	mldh->mld_maxdelay = 0;
	mldh->mld_reserved = 0;
	mldh->mld_addr = in6m->in6m_addr;
	if (IN6_IS_ADDR_MC_LINKLOCAL(&mldh->mld_addr))
		mldh->mld_addr.s6_addr16[1] = 0; /* XXX */
	mh->m_pkthdr.csum_flags |= M_ICMP_CSUM_OUT;

	/* construct multicast option */
	bzero(&im6o, sizeof(im6o));
	im6o.im6o_ifidx = ifp->if_index;
	im6o.im6o_hlim = 1;

	/*
	 * Request loopback of the report if we are acting as a multicast
	 * router, so that the process-level routing daemon can hear it.
	 */
#ifdef MROUTING
	im6o.im6o_loop = (ip6_mrouter[ifp->if_rdomain] != NULL);
#endif
	if_put(ifp);

	icmp6stat_inc(icp6s_outhist + type);
	ip6_output(mh, &ip6_opts, NULL, ia6 ? 0 : IPV6_UNSPECSRC, &im6o, NULL);
}
