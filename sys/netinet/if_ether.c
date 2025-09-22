/*	$OpenBSD: if_ether.c,v 1.277 2025/09/02 11:39:13 bluhm Exp $	*/
/*	$NetBSD: if_ether.c,v 1.31 1996/05/11 12:59:58 mycroft Exp $	*/

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
 *	@(#)if_ether.c	8.1 (Berkeley) 6/10/93
 */

/*
 * Ethernet address resolution protocol.
 * TODO:
 *	add "inuse/lock" bit (or ref. count) along with valid bit
 */

#include "carp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/timeout.h>
#include <sys/syslog.h>
#include <sys/queue.h>
#include <sys/pool.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/if_types.h>
#include <net/netisr.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip_var.h>
#if NCARP > 0
#include <netinet/ip_carp.h>
#endif

/*
 *  Locks used to protect struct members in this file:
 *	a	atomic operations
 *	I	immutable after creation
 *	K	kernel lock
 *	m	arp mutex, needed when net lock is shared
 *	N	net lock
 */

struct llinfo_arp {
	LIST_ENTRY(llinfo_arp)	 la_list;	/* [m] global arp_list */
	struct rtentry		*la_rt;		/* [I] backpointer to rtentry */
	/* keep fields above in sync with struct llinfo_arp_iterator */
	struct refcnt		 la_refcnt;	/* entry refereced by list */
	struct mbuf_queue	 la_mq;		/* packet hold queue */
	time_t			 la_refreshed;	/* when was refresh sent */
	int			 la_asked;	/* number of queries sent */
};
#define LA_HOLD_QUEUE 10
#define LA_HOLD_TOTAL 100

struct llinfo_arp_iterator {
	LIST_ENTRY(llinfo_arp)   la_list;       /* [m] global arp_list */
	struct rtentry          *la_rt;         /* [I] always NULL */
	/* keep fields above in sync with struct llinfo_arp */
};

/* timer values */
int	arpt_prune = (5 * 60);	/* [I] walk list every 5 minutes */
int	arpt_keep = (20 * 60);	/* [a] once resolved, cache for 20 minutes */
int	arpt_down = 20;	/* [a] once declared down, don't send for 20 secs */

struct mbuf *arppullup(struct mbuf *m);
void arpinvalidate(struct rtentry *);
void arptfree(struct rtentry *);
void arptimer(void *);
struct rtentry *arplookup(struct in_addr *, int, int, unsigned int);
void in_arpinput(struct ifnet *, struct mbuf *);
void in_revarpinput(struct ifnet *, struct mbuf *);
int arpcache(struct ifnet *, struct ether_arp *, struct rtentry *);
void arpreply(struct ifnet *, struct mbuf *, struct in_addr *, uint8_t *,
    unsigned int);

struct niqueue arpinq = NIQUEUE_INITIALIZER(50, NETISR_ARP);

/* llinfo_arp live time, rt_llinfo and RTF_LLINFO are protected by arp_mtx */
struct mutex arp_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);

LIST_HEAD(, llinfo_arp) arp_list = LIST_HEAD_INITIALIZER(arp_list);
				/* [m] list of llinfo_arp structures */
struct	pool arp_pool;		/* [I] pool for llinfo_arp structures */
int	arp_maxtries = 5;	/* [I] arp requests before set to rejected */
unsigned int	la_hold_total;	/* [a] packets currently in the arp queue */

#ifdef NFSCLIENT
/* revarp state */
struct in_addr revarp_myip, revarp_srvip;
int revarp_finished;
unsigned int revarp_ifidx;
#endif /* NFSCLIENT */

static struct llinfo_arp *
arpiterator(struct llinfo_arp *la, struct llinfo_arp_iterator *iter)
{
	struct llinfo_arp *tmp;

	MUTEX_ASSERT_LOCKED(&arp_mtx);

	if (la)
		tmp = LIST_NEXT((struct llinfo_arp *)iter, la_list);
	else
		tmp = LIST_FIRST(&arp_list);

	while (tmp && tmp->la_rt == NULL)
		tmp = LIST_NEXT(tmp, la_list);

	if (la) {
		LIST_REMOVE((struct llinfo_arp *)iter, la_list);
		if (refcnt_rele(&la->la_refcnt))
			pool_put(&arp_pool, la);
	}
	if (tmp) {
		LIST_INSERT_AFTER(tmp, (struct llinfo_arp *)iter, la_list);
		refcnt_take(&tmp->la_refcnt);
	}

	return tmp;
}

/*
 * Timeout routine.  Age arp table entries periodically.
 */
void
arptimer(void *arg)
{
	struct timeout *to = arg;
	struct llinfo_arp_iterator iter = { .la_rt = NULL };
	struct llinfo_arp *la = NULL;
	time_t uptime;

	uptime = getuptime();
	timeout_add_sec(to, arpt_prune);

	mtx_enter(&arp_mtx);
	while ((la = arpiterator(la, &iter)) != NULL) {
		struct rtentry *rt = la->la_rt;

		if (rt->rt_expire && rt->rt_expire < uptime) {
			rtref(rt);
			mtx_leave(&arp_mtx);
			NET_LOCK();
			arptfree(rt); /* timer has expired; clear */
			NET_UNLOCK();
			rtfree(rt);
			mtx_enter(&arp_mtx);
		}
	}
	mtx_leave(&arp_mtx);
}

void
arpinit(void)
{
	static struct timeout arptimer_to;

	pool_init(&arp_pool, sizeof(struct llinfo_arp), 0,
	    IPL_SOFTNET, 0, "arp", NULL);

	timeout_set_flags(&arptimer_to, arptimer, &arptimer_to,
	    KCLOCK_NONE, TIMEOUT_PROC | TIMEOUT_MPSAFE);
	timeout_add_sec(&arptimer_to, arpt_prune);
}

void
arp_rtrequest(struct ifnet *ifp, int req, struct rtentry *rt)
{
	struct sockaddr *gate = rt->rt_gateway;
	struct llinfo_arp *la;
	time_t uptime;

	NET_ASSERT_LOCKED();

	if (ISSET(rt->rt_flags,
	    RTF_GATEWAY|RTF_BROADCAST|RTF_MULTICAST|RTF_MPLS))
		return;

	uptime = getuptime();
	switch (req) {
	case RTM_ADD:
		if (rt->rt_flags & RTF_CLONING) {
			rt->rt_expire = 0;
			break;
		}
		if ((rt->rt_flags & RTF_LOCAL) && rt->rt_llinfo == NULL)
			rt->rt_expire = 0;
		/*
		 * Announce a new entry if requested or warn the user
		 * if another station has this IP address.
		 */
		if (rt->rt_flags & (RTF_ANNOUNCE|RTF_LOCAL))
			arprequest(ifp,
			    &satosin(rt_key(rt))->sin_addr.s_addr,
			    &satosin(rt_key(rt))->sin_addr.s_addr,
			    (u_char *)LLADDR(satosdl(gate)));
		/*FALLTHROUGH*/
	case RTM_RESOLVE:
		if (gate->sa_family != AF_LINK ||
		    gate->sa_len < sizeof(struct sockaddr_dl)) {
			log(LOG_DEBUG, "%s: bad gateway value: %s\n", __func__,
			    ifp->if_xname);
			break;
		}
		satosdl(gate)->sdl_type = ifp->if_type;
		satosdl(gate)->sdl_index = ifp->if_index;
		/*
		 * Case 2:  This route may come from cloning, or a manual route
		 * add with a LL address.
		 */
		la = pool_get(&arp_pool, PR_NOWAIT | PR_ZERO);
		if (la == NULL) {
			log(LOG_DEBUG, "%s: pool get failed\n", __func__);
			break;
		}

		mtx_enter(&arp_mtx);
		if (rt->rt_llinfo != NULL) {
			/* we lost the race, another thread has entered it */
			mtx_leave(&arp_mtx);
			pool_put(&arp_pool, la);
			break;
		}
		refcnt_init(&la->la_refcnt);
		mq_init(&la->la_mq, LA_HOLD_QUEUE, IPL_SOFTNET);
		rt->rt_llinfo = (caddr_t)la;
		la->la_rt = rt;
		rt->rt_flags |= RTF_LLINFO;
		LIST_INSERT_HEAD(&arp_list, la, la_list);
		if ((rt->rt_flags & RTF_LOCAL) == 0)
			rt->rt_expire = uptime;
		mtx_leave(&arp_mtx);

		break;

	case RTM_DELETE:
		mtx_enter(&arp_mtx);
		la = (struct llinfo_arp *)rt->rt_llinfo;
		if (la == NULL) {
			/* we lost the race, another thread has removed it */
			mtx_leave(&arp_mtx);
			break;
		}
		LIST_REMOVE(la, la_list);
		rt->rt_llinfo = NULL;
		rt->rt_flags &= ~RTF_LLINFO;
		atomic_sub_int(&la_hold_total, mq_purge(&la->la_mq));
		mtx_leave(&arp_mtx);

		if (refcnt_rele(&la->la_refcnt))
			pool_put(&arp_pool, la);
		break;

	case RTM_INVALIDATE:
		if (!ISSET(rt->rt_flags, RTF_LOCAL))
			arpinvalidate(rt);
		break;
	}
}

/*
 * Broadcast an ARP request. Caller specifies:
 *	- arp header source ip address
 *	- arp header target ip address
 *	- arp header source ethernet address
 */
void
arprequest(struct ifnet *ifp, u_int32_t *sip, u_int32_t *tip, u_int8_t *enaddr)
{
	struct mbuf *m;
	struct ether_header *eh;
	struct ether_arp *ea;
	struct sockaddr sa;

	if ((m = m_gethdr(M_DONTWAIT, MT_DATA)) == NULL)
		return;
	m->m_len = sizeof(*ea);
	m->m_pkthdr.len = sizeof(*ea);
	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;
	m->m_pkthdr.pf.prio = ifp->if_llprio;
	m_align(m, sizeof(*ea));
	ea = mtod(m, struct ether_arp *);
	eh = (struct ether_header *)sa.sa_data;
	memset(ea, 0, sizeof(*ea));
	memcpy(eh->ether_dhost, etherbroadcastaddr, sizeof(eh->ether_dhost));
	eh->ether_type = htons(ETHERTYPE_ARP);	/* if_output will not swap */
	ea->arp_hrd = htons(ARPHRD_ETHER);
	ea->arp_pro = htons(ETHERTYPE_IP);
	ea->arp_hln = sizeof(ea->arp_sha);	/* hardware address length */
	ea->arp_pln = sizeof(ea->arp_spa);	/* protocol address length */
	ea->arp_op = htons(ARPOP_REQUEST);
	memcpy(eh->ether_shost, enaddr, sizeof(eh->ether_shost));
	memcpy(ea->arp_sha, enaddr, sizeof(ea->arp_sha));
	memcpy(ea->arp_spa, sip, sizeof(ea->arp_spa));
	memcpy(ea->arp_tpa, tip, sizeof(ea->arp_tpa));
	sa.sa_family = pseudo_AF_HDRCMPLT;
	sa.sa_len = sizeof(sa);
	m->m_flags |= M_BCAST;
	ifp->if_output(ifp, m, &sa, NULL);
}

void
arpreply(struct ifnet *ifp, struct mbuf *m, struct in_addr *sip, uint8_t *eaddr,
    unsigned int rdomain)
{
	struct ether_header *eh;
	struct ether_arp *ea;
	struct sockaddr sa;

	m_resethdr(m);
	m->m_pkthdr.ph_rtableid = rdomain;

	ea = mtod(m, struct ether_arp *);
	ea->arp_op = htons(ARPOP_REPLY);
	ea->arp_pro = htons(ETHERTYPE_IP); /* let's be sure! */

	/* We're replying to a request. */
	memcpy(ea->arp_tha, ea->arp_sha, sizeof(ea->arp_sha));
	memcpy(ea->arp_tpa, ea->arp_spa, sizeof(ea->arp_spa));

	memcpy(ea->arp_sha, eaddr, sizeof(ea->arp_sha));
	memcpy(ea->arp_spa, sip, sizeof(ea->arp_spa));

	eh = (struct ether_header *)sa.sa_data;
	memcpy(eh->ether_dhost, ea->arp_tha, sizeof(eh->ether_dhost));
	memcpy(eh->ether_shost, eaddr, sizeof(eh->ether_shost));
	eh->ether_type = htons(ETHERTYPE_ARP);
	sa.sa_family = pseudo_AF_HDRCMPLT;
	sa.sa_len = sizeof(sa);
	ifp->if_output(ifp, m, &sa, NULL);
}

/*
 * Resolve an IP address into an ethernet address.  If success,
 * desten is filled in.  If there is no entry in arptab,
 * set one up and broadcast a request for the IP address.
 * Hold onto this mbuf and resend it once the address
 * is finally resolved.  A return value of 0 indicates
 * that desten has been filled in and the packet should be sent
 * normally; A return value of EAGAIN indicates that the packet
 * has been taken over here, either now or for later transmission.
 * Any other return value indicates an error.
 */
int
arpresolve(struct ifnet *ifp, struct rtentry *rt0, struct mbuf *m,
    struct sockaddr *dst, u_char *desten)
{
	struct arpcom *ac = (struct arpcom *)ifp;
	struct llinfo_arp *la;
	struct sockaddr_dl *sdl;
	struct rtentry *rt = NULL;
	char addr[INET_ADDRSTRLEN];
	time_t uptime;
	int refresh = 0, reject = 0;

	if (m->m_flags & M_BCAST) {	/* broadcast */
		memcpy(desten, etherbroadcastaddr, sizeof(etherbroadcastaddr));
		return (0);
	}
	if (m->m_flags & M_MCAST) {	/* multicast */
		ETHER_MAP_IP_MULTICAST(&satosin(dst)->sin_addr, desten);
		return (0);
	}

	uptime = getuptime();
	rt = rt_getll(rt0);

	if (rt == NULL || (ISSET(rt->rt_flags, RTF_REJECT) &&
	    (rt->rt_expire == 0 || rt->rt_expire > uptime))) {
		m_freem(m);
		return (rt == rt0 ? EHOSTDOWN : EHOSTUNREACH);
	}

	if (!ISSET(rt->rt_flags, RTF_LLINFO)) {
		log(LOG_DEBUG, "%s: %s: route contains no arp information\n",
		    __func__, inet_ntop(AF_INET, &satosin(rt_key(rt))->sin_addr,
		    addr, sizeof(addr)));
		goto bad;
	}

	sdl = satosdl(rt->rt_gateway);
	if (sdl->sdl_alen > 0 && sdl->sdl_alen != ETHER_ADDR_LEN) {
		log(LOG_DEBUG, "%s: %s: incorrect arp information\n", __func__,
		    inet_ntop(AF_INET, &satosin(dst)->sin_addr,
			addr, sizeof(addr)));
		goto bad;
	}


	/*
	 * Check the address family and length is valid, the address
	 * is resolved; otherwise, try to resolve.
	 */
	if ((rt->rt_expire == 0 || rt->rt_expire > uptime) &&
	    sdl->sdl_family == AF_LINK && sdl->sdl_alen != 0) {
		memcpy(desten, LLADDR(sdl), sdl->sdl_alen);

		/* refresh ARP entry when timeout gets close */
		if (rt->rt_expire != 0 &&
		    rt->rt_expire - atomic_load_int(&arpt_keep) / 8 < uptime) {

			mtx_enter(&arp_mtx);
			la = (struct llinfo_arp *)rt->rt_llinfo;
			if (la != NULL) {
				if (la->la_refreshed + 30 < uptime) {
					la->la_refreshed = uptime;
					refresh = 1;
				}
			}
			mtx_leave(&arp_mtx);
		}
		if (refresh) {
			arprequest(ifp,
			    &satosin(rt->rt_ifa->ifa_addr)->sin_addr.s_addr,
			    &satosin(dst)->sin_addr.s_addr,
			    ac->ac_enaddr);
		}
		return (0);
	}

	if (ifp->if_flags & (IFF_NOARP|IFF_STATICARP))
		goto bad;

	mtx_enter(&arp_mtx);
	la = (struct llinfo_arp *)rt->rt_llinfo;
	if (la == NULL) {
		mtx_leave(&arp_mtx);
		goto bad;
	}

	/*
	 * There is an arptab entry, but no ethernet address
	 * response yet. Insert mbuf in hold queue if below limit.
	 * If above the limit free the queue without queuing the new packet.
	 */
	if (atomic_inc_int_nv(&la_hold_total) <= LA_HOLD_TOTAL) {
		if (mq_push(&la->la_mq, m) != 0)
			atomic_dec_int(&la_hold_total);
	} else {
		atomic_sub_int(&la_hold_total, mq_purge(&la->la_mq) + 1);
		m_freem(m);
	}

	/*
	 * Re-send the ARP request when appropriate.
	 */
#ifdef	DIAGNOSTIC
	if (rt->rt_expire == 0) {
		/* This should never happen. (Should it? -gwr) */
		printf("%s: unresolved and rt_expire == 0\n", __func__);
		/* Set expiration time to now (expired). */
		rt->rt_expire = uptime;
	}
#endif
	if (rt->rt_expire) {
		reject = ~RTF_REJECT;
		if (la->la_asked == 0 || rt->rt_expire != uptime) {
			rt->rt_expire = uptime;
			if (la->la_asked++ < arp_maxtries)
				refresh = 1;
			else {
				reject = RTF_REJECT;
				rt->rt_expire += atomic_load_int(&arpt_down);
				la->la_asked = 0;
				la->la_refreshed = 0;
				atomic_sub_int(&la_hold_total,
				    mq_purge(&la->la_mq));
			}
		}
	}
	mtx_leave(&arp_mtx);

	if (reject == RTF_REJECT && !ISSET(rt->rt_flags, RTF_REJECT)) {
		KERNEL_LOCK();
		SET(rt->rt_flags, RTF_REJECT);
		KERNEL_UNLOCK();
	}
	if (reject == ~RTF_REJECT && ISSET(rt->rt_flags, RTF_REJECT)) {
		KERNEL_LOCK();
		CLR(rt->rt_flags, RTF_REJECT);
		KERNEL_UNLOCK();
	}
	if (refresh)
		arprequest(ifp, &satosin(rt->rt_ifa->ifa_addr)->sin_addr.s_addr,
		    &satosin(dst)->sin_addr.s_addr, ac->ac_enaddr);
	return (EAGAIN);

bad:
	m_freem(m);
	return (EINVAL);
}

struct mbuf *
arppullup(struct mbuf *m)
{
	struct arphdr *ar;
	int len;

#ifdef DIAGNOSTIC
	if ((m->m_flags & M_PKTHDR) == 0)
		panic("arp without packet header");
#endif

	len = sizeof(struct arphdr);
	if (m->m_len < len && (m = m_pullup(m, len)) == NULL)
		return NULL;

	ar = mtod(m, struct arphdr *);
	if (ntohs(ar->ar_hrd) != ARPHRD_ETHER ||
	    ntohs(ar->ar_pro) != ETHERTYPE_IP ||
	    ar->ar_hln != ETHER_ADDR_LEN ||
	    ar->ar_pln != sizeof(struct in_addr)) {
		m_freem(m);
		return NULL;
	}

	len += 2 * (ar->ar_hln + ar->ar_pln);
	if (m->m_len < len && (m = m_pullup(m, len)) == NULL)
		return NULL;

	return m;
}

/*
 * Common length and type checks are done here,
 * then the protocol-specific routine is called.
 */
void
arpinput(struct ifnet *ifp, struct mbuf *m, struct netstack *ns)
{
	if ((m = arppullup(m)) == NULL)
		return;
	niq_enqueue(&arpinq, m);
}

void
arpintr(void)
{
	struct mbuf_list ml;
	struct mbuf *m;
	struct ifnet *ifp;

	niq_delist(&arpinq, &ml);

	while ((m = ml_dequeue(&ml)) != NULL) {
		ifp = if_get(m->m_pkthdr.ph_ifidx);

		if (ifp != NULL)
			in_arpinput(ifp, m);
		else
			m_freem(m);

		if_put(ifp);
	}
}

/*
 * ARP for Internet protocols on Ethernet, RFC 826.
 * In addition, a sanity check is performed on the sender
 * protocol address, to catch impersonators.
 */
void
in_arpinput(struct ifnet *ifp, struct mbuf *m)
{
	struct ether_arp *ea;
	struct rtentry *rt = NULL;
	struct sockaddr_in sin;
	struct in_addr isaddr, itaddr;
	char addr[INET_ADDRSTRLEN];
	int op, target = 0;
	unsigned int rdomain;

	rdomain = rtable_l2(m->m_pkthdr.ph_rtableid);

	ea = mtod(m, struct ether_arp *);
	op = ntohs(ea->arp_op);
	if ((op != ARPOP_REQUEST) && (op != ARPOP_REPLY))
		goto out;

	memcpy(&itaddr, ea->arp_tpa, sizeof(itaddr));
	memcpy(&isaddr, ea->arp_spa, sizeof(isaddr));
	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;

	if (ETHER_IS_MULTICAST(ea->arp_sha) &&
	    ETHER_IS_BROADCAST(ea->arp_sha)) {
		inet_ntop(AF_INET, &isaddr, addr, sizeof(addr));
		log(LOG_ERR, "arp: ether address is broadcast for IP address "
		    "%s!\n", addr);
		goto out;
	}

	if (!memcmp(ea->arp_sha, LLADDR(ifp->if_sadl), sizeof(ea->arp_sha)))
		goto out;	/* it's from me, ignore it. */

	/* Check target against our interface addresses. */
	sin.sin_addr = itaddr;
	rt = rtalloc(sintosa(&sin), 0, rdomain);
	if (rtisvalid(rt) && ISSET(rt->rt_flags, RTF_LOCAL) &&
	    rt->rt_ifidx == ifp->if_index)
		target = 1;
	rtfree(rt);
	rt = NULL;

#if NCARP > 0
	if (target && op == ARPOP_REQUEST && ifp->if_type == IFT_CARP &&
	    !carp_iamatch(ifp))
		goto out;
#endif

	/* Do we have an ARP cache for the sender? Create if we are target. */
	rt = arplookup(&isaddr, target, 0, rdomain);

	/* Check sender against our interface addresses. */
	if (rtisvalid(rt) && ISSET(rt->rt_flags, RTF_LOCAL) &&
	    rt->rt_ifidx == ifp->if_index && isaddr.s_addr != INADDR_ANY) {
		inet_ntop(AF_INET, &isaddr, addr, sizeof(addr));
		log(LOG_ERR, "duplicate IP address %s sent from ethernet "
		    "address %s\n", addr, ether_sprintf(ea->arp_sha));
		itaddr = isaddr;
	} else if (rt != NULL) {
		if (arpcache(ifp, ea, rt))
			goto out;
	}

	if (op == ARPOP_REQUEST) {
		uint8_t *eaddr;

		if (target) {
			/* We already have all info for the reply */
			eaddr = LLADDR(ifp->if_sadl);
		} else {
			rtfree(rt);
			rt = arplookup(&itaddr, 0, SIN_PROXY, rdomain);
			/*
			 * Protect from possible duplicates, only owner
			 * should respond
			 */
			if ((rt == NULL) || (rt->rt_ifidx != ifp->if_index))
				goto out;
			eaddr = LLADDR(satosdl(rt->rt_gateway));
		}
		arpreply(ifp, m, &itaddr, eaddr, rdomain);
		rtfree(rt);
		return;
	}

out:
	rtfree(rt);
	m_freem(m);
}

int
arpcache(struct ifnet *ifp, struct ether_arp *ea, struct rtentry *rt)
{
	struct llinfo_arp *la = (struct llinfo_arp *)rt->rt_llinfo;
	struct sockaddr_dl *sdl = satosdl(rt->rt_gateway);
	struct in_addr *spa = (struct in_addr *)ea->arp_spa;
	char addr[INET_ADDRSTRLEN];
	struct ifnet *rifp;
	time_t uptime;
	int changed = 0;

	NET_ASSERT_LOCKED_EXCLUSIVE();
	KASSERT(sdl != NULL);

	/*
	 * This can happen if the entry has been deleted by another CPU
	 * after we found it.
	 */
	if (la == NULL)
		return (0);

	uptime = getuptime();
	if (sdl->sdl_alen > 0) {
		if (memcmp(ea->arp_sha, LLADDR(sdl), sdl->sdl_alen)) {
			if (ISSET(rt->rt_flags, RTF_PERMANENT_ARP|RTF_LOCAL)) {
				inet_ntop(AF_INET, spa, addr, sizeof(addr));
				log(LOG_WARNING, "arp: attempt to overwrite "
				   "permanent entry for %s by %s on %s\n", addr,
				   ether_sprintf(ea->arp_sha), ifp->if_xname);
				return (-1);
			} else if (rt->rt_ifidx != ifp->if_index) {
#if NCARP > 0
				if (ifp->if_type != IFT_CARP)
#endif
				{
					rifp = if_get(rt->rt_ifidx);
					if (rifp == NULL)
						return (-1);
					inet_ntop(AF_INET, spa, addr,
					    sizeof(addr));
					log(LOG_WARNING, "arp: attempt to "
					    "overwrite entry for %s on %s by "
					    "%s on %s\n", addr, rifp->if_xname,
					    ether_sprintf(ea->arp_sha),
					    ifp->if_xname);
					if_put(rifp);
				}
				return (-1);
			} else {
				inet_ntop(AF_INET, spa, addr, sizeof(addr));
				log(LOG_INFO, "arp info overwritten for %s by "
				    "%s on %s\n", addr,
				    ether_sprintf(ea->arp_sha), ifp->if_xname);
				rt->rt_expire = 1;/* no longer static */
			}
			changed = 1;
		}
	} else if (!if_isconnected(ifp, rt->rt_ifidx)) {
		rifp = if_get(rt->rt_ifidx);
		if (rifp == NULL)
			return (-1);
		inet_ntop(AF_INET, spa, addr, sizeof(addr));
		log(LOG_WARNING, "arp: attempt to add entry for %s on %s by %s"
		    " on %s\n", addr, rifp->if_xname,
		    ether_sprintf(ea->arp_sha), ifp->if_xname);
		if_put(rifp);
		return (-1);
	}
	sdl->sdl_alen = sizeof(ea->arp_sha);
	memcpy(LLADDR(sdl), ea->arp_sha, sizeof(ea->arp_sha));
	if (rt->rt_expire)
		rt->rt_expire = uptime + atomic_load_int(&arpt_keep);
	rt->rt_flags &= ~RTF_REJECT;

	/* Notify userland that an ARP resolution has been done. */
	if (la->la_asked || changed) {
		rtm_send(rt, RTM_RESOLVE, 0, ifp->if_rdomain);
	}

	la->la_asked = 0;
	la->la_refreshed = 0;
	if_output_mq(ifp, &la->la_mq, &la_hold_total, rt_key(rt), rt);

	return (0);
}

void
arpinvalidate(struct rtentry *rt)
{
	struct llinfo_arp *la;
	struct sockaddr_dl *sdl = satosdl(rt->rt_gateway);

	mtx_enter(&arp_mtx);
	la = (struct llinfo_arp *)rt->rt_llinfo;
	if (la == NULL) {
		mtx_leave(&arp_mtx);
		return;
	}
	atomic_sub_int(&la_hold_total, mq_purge(&la->la_mq));
	sdl->sdl_alen = 0;
	la->la_asked = 0;
	mtx_leave(&arp_mtx);
}

/*
 * Free an arp entry.
 */
void
arptfree(struct rtentry *rt)
{
	struct ifnet *ifp;

	NET_ASSERT_LOCKED_EXCLUSIVE();

	/* might have been freed between leave arp_mtx and enter net lock */
	if (!ISSET(rt->rt_flags, RTF_LLINFO))
		return;

	KASSERT(!ISSET(rt->rt_flags, RTF_LOCAL));
	arpinvalidate(rt);

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL)
		return;
	if (!ISSET(rt->rt_flags, RTF_STATIC|RTF_CACHED))
		rtdeletemsg(rt, ifp, ifp->if_rdomain);
	if_put(ifp);
}

/*
 * Lookup or enter a new address in arptab.
 */
struct rtentry *
arplookup(struct in_addr *inp, int create, int proxy, u_int tableid)
{
	struct rtentry *rt;
	struct sockaddr_inarp sin;
	int flags;

	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inp->s_addr;
	sin.sin_other = proxy ? SIN_PROXY : 0;
	flags = (create) ? RT_RESOLVE : 0;

	rt = rtalloc((struct sockaddr *)&sin, flags, tableid);
	if (!rtisvalid(rt) || ISSET(rt->rt_flags, RTF_GATEWAY) ||
	    !ISSET(rt->rt_flags, RTF_LLINFO) ||
	    rt->rt_gateway->sa_family != AF_LINK) {
		rtfree(rt);
		return (NULL);
	}

	if (proxy && !ISSET(rt->rt_flags, RTF_ANNOUNCE)) {
		while ((rt = rtable_iterate(rt)) != NULL) {
			if (ISSET(rt->rt_flags, RTF_ANNOUNCE)) {
				break;
			}
		}
	}

	return (rt);
}

/*
 * Check whether we do proxy ARP for this address and we point to ourselves.
 */
int
arpproxy(struct in_addr in, unsigned int rtableid)
{
	struct sockaddr_dl *sdl;
	struct rtentry *rt;
	struct ifnet *ifp;
	int found = 0;

	rt = arplookup(&in, 0, SIN_PROXY, rtableid);
	if (!rtisvalid(rt)) {
		rtfree(rt);
		return (0);
	}

	/* Check that arp information are correct. */
	sdl = satosdl(rt->rt_gateway);
	if (sdl->sdl_alen != ETHER_ADDR_LEN) {
		rtfree(rt);
		return (0);
	}

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL) {
		rtfree(rt);
		return (0);
	}

	if (!memcmp(LLADDR(sdl), LLADDR(ifp->if_sadl), sdl->sdl_alen))
		found = 1;

	if_put(ifp);
	rtfree(rt);
	return (found);
}

/*
 * Called from Ethernet interrupt handlers
 * when ether packet type ETHERTYPE_REVARP
 * is received.  Common length and type checks are done here,
 * then the protocol-specific routine is called.
 */
void
revarpinput(struct ifnet *ifp, struct mbuf *m, struct netstack *ns)
{
	if ((m = arppullup(m)) == NULL)
		return;
	in_revarpinput(ifp, m);
}

/*
 * RARP for Internet protocols on Ethernet.
 * Algorithm is that given in RFC 903.
 * We are only using for bootstrap purposes to get an ip address for one of
 * our interfaces.  Thus we support no user-interface.
 *
 * Since the contents of the RARP reply are specific to the interface that
 * sent the request, this code must ensure that they are properly associated.
 *
 * Note: also supports ARP via RARP packets, per the RFC.
 */
void
in_revarpinput(struct ifnet *ifp, struct mbuf *m)
{
	struct ether_arp *ar;
	int op;

	ar = mtod(m, struct ether_arp *);
	op = ntohs(ar->arp_op);
	switch (op) {
	case ARPOP_REQUEST:
	case ARPOP_REPLY:	/* per RFC */
		niq_enqueue(&arpinq, m);
		return;
	case ARPOP_REVREPLY:
		break;
	case ARPOP_REVREQUEST:	/* handled by rarpd(8) */
	default:
		goto out;
	}
#ifdef NFSCLIENT
	if (revarp_ifidx == 0)
		goto out;
	if (revarp_ifidx != m->m_pkthdr.ph_ifidx) /* !same interface */
		goto out;
	if (revarp_finished)
		goto wake;
	if (memcmp(ar->arp_tha, LLADDR(ifp->if_sadl), sizeof(ar->arp_tha)))
		goto out;
	memcpy(&revarp_srvip, ar->arp_spa, sizeof(revarp_srvip));
	memcpy(&revarp_myip, ar->arp_tpa, sizeof(revarp_myip));
	revarp_finished = 1;
wake:	/* Do wakeup every time in case it was missed. */
	wakeup((caddr_t)&revarp_myip);
#endif /* NFSCLIENT */

out:
	m_freem(m);
}

#ifdef NFSCLIENT
/*
 * Send a RARP request for the ip address of the specified interface.
 * The request should be RFC 903-compliant.
 */
void
revarprequest(struct ifnet *ifp)
{
	struct sockaddr sa;
	struct mbuf *m;
	struct ether_header *eh;
	struct ether_arp *ea;
	struct arpcom *ac = (struct arpcom *)ifp;

	if ((m = m_gethdr(M_DONTWAIT, MT_DATA)) == NULL)
		return;
	m->m_len = sizeof(*ea);
	m->m_pkthdr.len = sizeof(*ea);
	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;
	m->m_pkthdr.pf.prio = ifp->if_llprio;
	m_align(m, sizeof(*ea));
	ea = mtod(m, struct ether_arp *);
	eh = (struct ether_header *)sa.sa_data;
	memset(ea, 0, sizeof(*ea));
	memcpy(eh->ether_dhost, etherbroadcastaddr, sizeof(eh->ether_dhost));
	eh->ether_type = htons(ETHERTYPE_REVARP);
	ea->arp_hrd = htons(ARPHRD_ETHER);
	ea->arp_pro = htons(ETHERTYPE_IP);
	ea->arp_hln = sizeof(ea->arp_sha);	/* hardware address length */
	ea->arp_pln = sizeof(ea->arp_spa);	/* protocol address length */
	ea->arp_op = htons(ARPOP_REVREQUEST);
	memcpy(eh->ether_shost, ac->ac_enaddr, sizeof(ea->arp_tha));
	memcpy(ea->arp_sha, ac->ac_enaddr, sizeof(ea->arp_sha));
	memcpy(ea->arp_tha, ac->ac_enaddr, sizeof(ea->arp_tha));
	sa.sa_family = pseudo_AF_HDRCMPLT;
	sa.sa_len = sizeof(sa);
	m->m_flags |= M_BCAST;
	ifp->if_output(ifp, m, &sa, NULL);
}

/*
 * RARP for the ip address of the specified interface, but also
 * save the ip address of the server that sent the answer.
 * Timeout if no response is received.
 */
int
revarpwhoarewe(struct ifnet *ifp, struct in_addr *serv_in,
    struct in_addr *clnt_in)
{
	int result, count = 20;

	if (revarp_finished)
		return EIO;

	revarp_ifidx = ifp->if_index;
	while (count--) {
		revarprequest(ifp);
		result = tsleep_nsec(&revarp_myip, PSOCK, "revarp",
		    MSEC_TO_NSEC(500));
		if (result != EWOULDBLOCK)
			break;
	}
	revarp_ifidx = 0;
	if (!revarp_finished)
		return ENETUNREACH;

	memcpy(serv_in, &revarp_srvip, sizeof(*serv_in));
	memcpy(clnt_in, &revarp_myip, sizeof(*clnt_in));
	return 0;
}

/* For compatibility: only saves interface address. */
int
revarpwhoami(struct in_addr *in, struct ifnet *ifp)
{
	struct in_addr server;
	return (revarpwhoarewe(ifp, &server, in));
}
#endif /* NFSCLIENT */
