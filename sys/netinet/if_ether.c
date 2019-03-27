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
 *	@(#)if_ether.c	8.1 (Berkeley) 6/10/93
 */

/*
 * Ethernet address resolution protocol.
 * TODO:
 *	add "inuse/lock" bit (or ref. count) along with valid bit
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/netisr.h>
#include <net/ethernet.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_fib.h>
#include <netinet/in_var.h>
#include <net/if_llatbl.h>
#include <netinet/if_ether.h>
#ifdef INET
#include <netinet/ip_carp.h>
#endif

#include <security/mac/mac_framework.h>

#define SIN(s) ((const struct sockaddr_in *)(s))

static struct timeval arp_lastlog;
static int arp_curpps;
static int arp_maxpps = 1;

/* Simple ARP state machine */
enum arp_llinfo_state {
	ARP_LLINFO_INCOMPLETE = 0, /* No LLE data */
	ARP_LLINFO_REACHABLE,	/* LLE is valid */
	ARP_LLINFO_VERIFY,	/* LLE is valid, need refresh */
	ARP_LLINFO_DELETED,	/* LLE is deleted */
};

SYSCTL_DECL(_net_link_ether);
static SYSCTL_NODE(_net_link_ether, PF_INET, inet, CTLFLAG_RW, 0, "");
static SYSCTL_NODE(_net_link_ether, PF_ARP, arp, CTLFLAG_RW, 0, "");

/* timer values */
VNET_DEFINE_STATIC(int, arpt_keep) = (20*60);	/* once resolved, good for 20
						 * minutes */
VNET_DEFINE_STATIC(int, arp_maxtries) = 5;
VNET_DEFINE_STATIC(int, arp_proxyall) = 0;
VNET_DEFINE_STATIC(int, arpt_down) = 20;	/* keep incomplete entries for
						 * 20 seconds */
VNET_DEFINE_STATIC(int, arpt_rexmit) = 1;	/* retransmit arp entries, sec*/
VNET_PCPUSTAT_DEFINE(struct arpstat, arpstat);  /* ARP statistics, see if_arp.h */
VNET_PCPUSTAT_SYSINIT(arpstat);

#ifdef VIMAGE
VNET_PCPUSTAT_SYSUNINIT(arpstat);
#endif /* VIMAGE */

VNET_DEFINE_STATIC(int, arp_maxhold) = 1;

#define	V_arpt_keep		VNET(arpt_keep)
#define	V_arpt_down		VNET(arpt_down)
#define	V_arpt_rexmit		VNET(arpt_rexmit)
#define	V_arp_maxtries		VNET(arp_maxtries)
#define	V_arp_proxyall		VNET(arp_proxyall)
#define	V_arp_maxhold		VNET(arp_maxhold)

SYSCTL_INT(_net_link_ether_inet, OID_AUTO, max_age, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(arpt_keep), 0,
	"ARP entry lifetime in seconds");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, maxtries, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(arp_maxtries), 0,
	"ARP resolution attempts before returning error");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, proxyall, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(arp_proxyall), 0,
	"Enable proxy ARP for all suitable requests");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, wait, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(arpt_down), 0,
	"Incomplete ARP entry lifetime in seconds");
SYSCTL_VNET_PCPUSTAT(_net_link_ether_arp, OID_AUTO, stats, struct arpstat,
    arpstat, "ARP statistics (struct arpstat, net/if_arp.h)");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, maxhold, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(arp_maxhold), 0,
	"Number of packets to hold per ARP entry");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, max_log_per_second,
	CTLFLAG_RW, &arp_maxpps, 0,
	"Maximum number of remotely triggered ARP messages that can be "
	"logged per second");

/*
 * Due to the exponential backoff algorithm used for the interval between GARP
 * retransmissions, the maximum number of retransmissions is limited for
 * sanity. This limit corresponds to a maximum interval between retransmissions
 * of 2^16 seconds ~= 18 hours.
 *
 * Making this limit more dynamic is more complicated than worthwhile,
 * especially since sending out GARPs spaced days apart would be of little
 * use. A maximum dynamic limit would look something like:
 *
 * const int max = fls(INT_MAX / hz) - 1;
 */
#define MAX_GARP_RETRANSMITS 16
static int sysctl_garp_rexmit(SYSCTL_HANDLER_ARGS);
static int garp_rexmit_count = 0; /* GARP retransmission setting. */

SYSCTL_PROC(_net_link_ether_inet, OID_AUTO, garp_rexmit_count,
    CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_MPSAFE,
    &garp_rexmit_count, 0, sysctl_garp_rexmit, "I",
    "Number of times to retransmit GARP packets;"
    " 0 to disable, maximum of 16");

VNET_DEFINE_STATIC(int, arp_log_level) = LOG_INFO;	/* Min. log(9) level. */
#define	V_arp_log_level		VNET(arp_log_level)
SYSCTL_INT(_net_link_ether_arp, OID_AUTO, log_level, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(arp_log_level), 0,
	"Minimum log(9) level for recording rate limited arp log messages. "
	"The higher will be log more (emerg=0, info=6 (default), debug=7).");
#define	ARP_LOG(pri, ...)	do {					\
	if ((pri) <= V_arp_log_level &&					\
	    ppsratecheck(&arp_lastlog, &arp_curpps, arp_maxpps))	\
		log((pri), "arp: " __VA_ARGS__);			\
} while (0)


static void	arpintr(struct mbuf *);
static void	arptimer(void *);
#ifdef INET
static void	in_arpinput(struct mbuf *);
#endif

static void arp_check_update_lle(struct arphdr *ah, struct in_addr isaddr,
    struct ifnet *ifp, int bridged, struct llentry *la);
static void arp_mark_lle_reachable(struct llentry *la);
static void arp_iflladdr(void *arg __unused, struct ifnet *ifp);

static eventhandler_tag iflladdr_tag;

static const struct netisr_handler arp_nh = {
	.nh_name = "arp",
	.nh_handler = arpintr,
	.nh_proto = NETISR_ARP,
	.nh_policy = NETISR_POLICY_SOURCE,
};

/*
 * Timeout routine.  Age arp_tab entries periodically.
 */
static void
arptimer(void *arg)
{
	struct llentry *lle = (struct llentry *)arg;
	struct ifnet *ifp;
	int r_skip_req;

	if (lle->la_flags & LLE_STATIC) {
		return;
	}
	LLE_WLOCK(lle);
	if (callout_pending(&lle->lle_timer)) {
		/*
		 * Here we are a bit odd here in the treatment of 
		 * active/pending. If the pending bit is set, it got
		 * rescheduled before I ran. The active
		 * bit we ignore, since if it was stopped
		 * in ll_tablefree() and was currently running
		 * it would have return 0 so the code would
		 * not have deleted it since the callout could
		 * not be stopped so we want to go through
		 * with the delete here now. If the callout
		 * was restarted, the pending bit will be back on and
		 * we just want to bail since the callout_reset would
		 * return 1 and our reference would have been removed
		 * by arpresolve() below.
		 */
		LLE_WUNLOCK(lle);
 		return;
 	}
	ifp = lle->lle_tbl->llt_ifp;
	CURVNET_SET(ifp->if_vnet);

	switch (lle->ln_state) {
	case ARP_LLINFO_REACHABLE:

		/*
		 * Expiration time is approaching.
		 * Let's try to refresh entry if it is still
		 * in use.
		 *
		 * Set r_skip_req to get feedback from
		 * fast path. Change state and re-schedule
		 * ourselves.
		 */
		LLE_REQ_LOCK(lle);
		lle->r_skip_req = 1;
		LLE_REQ_UNLOCK(lle);
		lle->ln_state = ARP_LLINFO_VERIFY;
		callout_schedule(&lle->lle_timer, hz * V_arpt_rexmit);
		LLE_WUNLOCK(lle);
		CURVNET_RESTORE();
		return;
	case ARP_LLINFO_VERIFY:
		LLE_REQ_LOCK(lle);
		r_skip_req = lle->r_skip_req;
		LLE_REQ_UNLOCK(lle);

		if (r_skip_req == 0 && lle->la_preempt > 0) {
			/* Entry was used, issue refresh request */
			struct in_addr dst;
			dst = lle->r_l3addr.addr4;
			lle->la_preempt--;
			callout_schedule(&lle->lle_timer, hz * V_arpt_rexmit);
			LLE_WUNLOCK(lle);
			arprequest(ifp, NULL, &dst, NULL);
			CURVNET_RESTORE();
			return;
		}
		/* Nothing happened. Reschedule if not too late */
		if (lle->la_expire > time_uptime) {
			callout_schedule(&lle->lle_timer, hz * V_arpt_rexmit);
			LLE_WUNLOCK(lle);
			CURVNET_RESTORE();
			return;
		}
		break;
	case ARP_LLINFO_INCOMPLETE:
	case ARP_LLINFO_DELETED:
		break;
	}

	if ((lle->la_flags & LLE_DELETED) == 0) {
		int evt;

		if (lle->la_flags & LLE_VALID)
			evt = LLENTRY_EXPIRED;
		else
			evt = LLENTRY_TIMEDOUT;
		EVENTHANDLER_INVOKE(lle_event, lle, evt);
	}

	callout_stop(&lle->lle_timer);

	/* XXX: LOR avoidance. We still have ref on lle. */
	LLE_WUNLOCK(lle);
	IF_AFDATA_LOCK(ifp);
	LLE_WLOCK(lle);

	/* Guard against race with other llentry_free(). */
	if (lle->la_flags & LLE_LINKED) {
		LLE_REMREF(lle);
		lltable_unlink_entry(lle->lle_tbl, lle);
	}
	IF_AFDATA_UNLOCK(ifp);

	size_t pkts_dropped = llentry_free(lle);

	ARPSTAT_ADD(dropped, pkts_dropped);
	ARPSTAT_INC(timeouts);

	CURVNET_RESTORE();
}

/*
 * Stores link-layer header for @ifp in format suitable for if_output()
 * into buffer @buf. Resulting header length is stored in @bufsize.
 *
 * Returns 0 on success.
 */
static int
arp_fillheader(struct ifnet *ifp, struct arphdr *ah, int bcast, u_char *buf,
    size_t *bufsize)
{
	struct if_encap_req ereq;
	int error;

	bzero(buf, *bufsize);
	bzero(&ereq, sizeof(ereq));
	ereq.buf = buf;
	ereq.bufsize = *bufsize;
	ereq.rtype = IFENCAP_LL;
	ereq.family = AF_ARP;
	ereq.lladdr = ar_tha(ah);
	ereq.hdata = (u_char *)ah;
	if (bcast)
		ereq.flags = IFENCAP_FLAG_BROADCAST;
	error = ifp->if_requestencap(ifp, &ereq);
	if (error == 0)
		*bufsize = ereq.bufsize;

	return (error);
}


/*
 * Broadcast an ARP request. Caller specifies:
 *	- arp header source ip address
 *	- arp header target ip address
 *	- arp header source ethernet address
 */
static int
arprequest_internal(struct ifnet *ifp, const struct in_addr *sip,
    const struct in_addr *tip, u_char *enaddr)
{
	struct mbuf *m;
	struct arphdr *ah;
	struct sockaddr sa;
	u_char *carpaddr = NULL;
	uint8_t linkhdr[LLE_MAX_LINKHDR];
	size_t linkhdrsize;
	struct route ro;
	int error;

	if (sip == NULL) {
		/*
		 * The caller did not supply a source address, try to find
		 * a compatible one among those assigned to this interface.
		 */
		struct epoch_tracker et;
		struct ifaddr *ifa;

		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;

			if (ifa->ifa_carp) {
				if ((*carp_iamatch_p)(ifa, &carpaddr) == 0)
					continue;
				sip = &IA_SIN(ifa)->sin_addr;
			} else {
				carpaddr = NULL;
				sip = &IA_SIN(ifa)->sin_addr;
			}

			if (0 == ((sip->s_addr ^ tip->s_addr) &
			    IA_MASKSIN(ifa)->sin_addr.s_addr))
				break;  /* found it. */
		}
		NET_EPOCH_EXIT(et);
		if (sip == NULL) {
			printf("%s: cannot find matching address\n", __func__);
			return (EADDRNOTAVAIL);
		}
	}
	if (enaddr == NULL)
		enaddr = carpaddr ? carpaddr : (u_char *)IF_LLADDR(ifp);

	if ((m = m_gethdr(M_NOWAIT, MT_DATA)) == NULL)
		return (ENOMEM);
	m->m_len = sizeof(*ah) + 2 * sizeof(struct in_addr) +
		2 * ifp->if_addrlen;
	m->m_pkthdr.len = m->m_len;
	M_ALIGN(m, m->m_len);
	ah = mtod(m, struct arphdr *);
	bzero((caddr_t)ah, m->m_len);
#ifdef MAC
	mac_netinet_arp_send(ifp, m);
#endif
	ah->ar_pro = htons(ETHERTYPE_IP);
	ah->ar_hln = ifp->if_addrlen;		/* hardware address length */
	ah->ar_pln = sizeof(struct in_addr);	/* protocol address length */
	ah->ar_op = htons(ARPOP_REQUEST);
	bcopy(enaddr, ar_sha(ah), ah->ar_hln);
	bcopy(sip, ar_spa(ah), ah->ar_pln);
	bcopy(tip, ar_tpa(ah), ah->ar_pln);
	sa.sa_family = AF_ARP;
	sa.sa_len = 2;

	/* Calculate link header for sending frame */
	bzero(&ro, sizeof(ro));
	linkhdrsize = sizeof(linkhdr);
	error = arp_fillheader(ifp, ah, 1, linkhdr, &linkhdrsize);
	if (error != 0 && error != EAFNOSUPPORT) {
		ARP_LOG(LOG_ERR, "Failed to calculate ARP header on %s: %d\n",
		    if_name(ifp), error);
		return (error);
	}

	ro.ro_prepend = linkhdr;
	ro.ro_plen = linkhdrsize;
	ro.ro_flags = 0;

	m->m_flags |= M_BCAST;
	m_clrprotoflags(m);	/* Avoid confusing lower layers. */
	error = (*ifp->if_output)(ifp, m, &sa, &ro);
	ARPSTAT_INC(txrequests);
	if (error) {
		ARPSTAT_INC(txerrors);
		ARP_LOG(LOG_DEBUG, "Failed to send ARP packet on %s: %d\n",
		    if_name(ifp), error);
	}
	return (error);
}

void
arprequest(struct ifnet *ifp, const struct in_addr *sip,
    const struct in_addr *tip, u_char *enaddr)
{

	(void) arprequest_internal(ifp, sip, tip, enaddr);
}

/*
 * Resolve an IP address into an ethernet address - heavy version.
 * Used internally by arpresolve().
 * We have already checked that we can't use an existing lle without
 * modification so we have to acquire an LLE_EXCLUSIVE lle lock.
 *
 * On success, desten and pflags are filled in and the function returns 0;
 * If the packet must be held pending resolution, we return EWOULDBLOCK
 * On other errors, we return the corresponding error code.
 * Note that m_freem() handles NULL.
 */
static int
arpresolve_full(struct ifnet *ifp, int is_gw, int flags, struct mbuf *m,
	const struct sockaddr *dst, u_char *desten, uint32_t *pflags,
	struct llentry **plle)
{
	struct llentry *la = NULL, *la_tmp;
	struct mbuf *curr = NULL;
	struct mbuf *next = NULL;
	int error, renew;
	char *lladdr;
	int ll_len;

	if (pflags != NULL)
		*pflags = 0;
	if (plle != NULL)
		*plle = NULL;

	if ((flags & LLE_CREATE) == 0) {
		struct epoch_tracker et;

		NET_EPOCH_ENTER(et);
		la = lla_lookup(LLTABLE(ifp), LLE_EXCLUSIVE, dst);
		NET_EPOCH_EXIT(et);
	}
	if (la == NULL && (ifp->if_flags & (IFF_NOARP | IFF_STATICARP)) == 0) {
		la = lltable_alloc_entry(LLTABLE(ifp), 0, dst);
		if (la == NULL) {
			char addrbuf[INET_ADDRSTRLEN];

			log(LOG_DEBUG,
			    "arpresolve: can't allocate llinfo for %s on %s\n",
			    inet_ntoa_r(SIN(dst)->sin_addr, addrbuf),
			    if_name(ifp));
			m_freem(m);
			return (EINVAL);
		}

		IF_AFDATA_WLOCK(ifp);
		LLE_WLOCK(la);
		la_tmp = lla_lookup(LLTABLE(ifp), LLE_EXCLUSIVE, dst);
		/* Prefer ANY existing lle over newly-created one */
		if (la_tmp == NULL)
			lltable_link_entry(LLTABLE(ifp), la);
		IF_AFDATA_WUNLOCK(ifp);
		if (la_tmp != NULL) {
			lltable_free_entry(LLTABLE(ifp), la);
			la = la_tmp;
		}
	}
	if (la == NULL) {
		m_freem(m);
		return (EINVAL);
	}

	if ((la->la_flags & LLE_VALID) &&
	    ((la->la_flags & LLE_STATIC) || la->la_expire > time_uptime)) {
		if (flags & LLE_ADDRONLY) {
			lladdr = la->ll_addr;
			ll_len = ifp->if_addrlen;
		} else {
			lladdr = la->r_linkdata;
			ll_len = la->r_hdrlen;
		}
		bcopy(lladdr, desten, ll_len);

		/* Notify LLE code that the entry was used by datapath */
		llentry_mark_used(la);
		if (pflags != NULL)
			*pflags = la->la_flags & (LLE_VALID|LLE_IFADDR);
		if (plle) {
			LLE_ADDREF(la);
			*plle = la;
		}
		LLE_WUNLOCK(la);
		return (0);
	}

	renew = (la->la_asked == 0 || la->la_expire != time_uptime);
	/*
	 * There is an arptab entry, but no ethernet address
	 * response yet.  Add the mbuf to the list, dropping
	 * the oldest packet if we have exceeded the system
	 * setting.
	 */
	if (m != NULL) {
		if (la->la_numheld >= V_arp_maxhold) {
			if (la->la_hold != NULL) {
				next = la->la_hold->m_nextpkt;
				m_freem(la->la_hold);
				la->la_hold = next;
				la->la_numheld--;
				ARPSTAT_INC(dropped);
			}
		}
		if (la->la_hold != NULL) {
			curr = la->la_hold;
			while (curr->m_nextpkt != NULL)
				curr = curr->m_nextpkt;
			curr->m_nextpkt = m;
		} else
			la->la_hold = m;
		la->la_numheld++;
	}
	/*
	 * Return EWOULDBLOCK if we have tried less than arp_maxtries. It
	 * will be masked by ether_output(). Return EHOSTDOWN/EHOSTUNREACH
	 * if we have already sent arp_maxtries ARP requests. Retransmit the
	 * ARP request, but not faster than one request per second.
	 */
	if (la->la_asked < V_arp_maxtries)
		error = EWOULDBLOCK;	/* First request. */
	else
		error = is_gw != 0 ? EHOSTUNREACH : EHOSTDOWN;

	if (renew) {
		int canceled, e;

		LLE_ADDREF(la);
		la->la_expire = time_uptime;
		canceled = callout_reset(&la->lle_timer, hz * V_arpt_down,
		    arptimer, la);
		if (canceled)
			LLE_REMREF(la);
		la->la_asked++;
		LLE_WUNLOCK(la);
		e = arprequest_internal(ifp, NULL, &SIN(dst)->sin_addr, NULL);
		/*
		 * Only overwrite 'error' in case of error; in case of success
		 * the proper return value was already set above.
		 */
		if (e != 0)
			return (e);
		return (error);
	}

	LLE_WUNLOCK(la);
	return (error);
}

/*
 * Lookups link header based on an IP address.
 * On input:
 *    ifp is the interface we use
 *    is_gw != 0 if @dst represents gateway to some destination
 *    m is the mbuf. May be NULL if we don't have a packet.
 *    dst is the next hop,
 *    desten is the storage to put LL header.
 *    flags returns subset of lle flags: LLE_VALID | LLE_IFADDR
 *
 * On success, full/partial link header and flags are filled in and
 * the function returns 0.
 * If the packet must be held pending resolution, we return EWOULDBLOCK
 * On other errors, we return the corresponding error code.
 * Note that m_freem() handles NULL.
 */
int
arpresolve(struct ifnet *ifp, int is_gw, struct mbuf *m,
	const struct sockaddr *dst, u_char *desten, uint32_t *pflags,
	struct llentry **plle)
{
	struct epoch_tracker et;
	struct llentry *la = NULL;

	if (pflags != NULL)
		*pflags = 0;
	if (plle != NULL)
		*plle = NULL;

	if (m != NULL) {
		if (m->m_flags & M_BCAST) {
			/* broadcast */
			(void)memcpy(desten,
			    ifp->if_broadcastaddr, ifp->if_addrlen);
			return (0);
		}
		if (m->m_flags & M_MCAST) {
			/* multicast */
			ETHER_MAP_IP_MULTICAST(&SIN(dst)->sin_addr, desten);
			return (0);
		}
	}

	NET_EPOCH_ENTER(et);
	la = lla_lookup(LLTABLE(ifp), plle ? LLE_EXCLUSIVE : LLE_UNLOCKED, dst);
	if (la != NULL && (la->r_flags & RLLE_VALID) != 0) {
		/* Entry found, let's copy lle info */
		bcopy(la->r_linkdata, desten, la->r_hdrlen);
		if (pflags != NULL)
			*pflags = LLE_VALID | (la->r_flags & RLLE_IFADDR);
		/* Notify the LLE handling code that the entry was used. */
		llentry_mark_used(la);
		if (plle) {
			LLE_ADDREF(la);
			*plle = la;
			LLE_WUNLOCK(la);
		}
		NET_EPOCH_EXIT(et);
		return (0);
	}
	if (plle && la)
		LLE_WUNLOCK(la);
	NET_EPOCH_EXIT(et);

	return (arpresolve_full(ifp, is_gw, la == NULL ? LLE_CREATE : 0, m, dst,
	    desten, pflags, plle));
}

/*
 * Common length and type checks are done here,
 * then the protocol-specific routine is called.
 */
static void
arpintr(struct mbuf *m)
{
	struct arphdr *ar;
	struct ifnet *ifp;
	char *layer;
	int hlen;

	ifp = m->m_pkthdr.rcvif;

	if (m->m_len < sizeof(struct arphdr) &&
	    ((m = m_pullup(m, sizeof(struct arphdr))) == NULL)) {
		ARP_LOG(LOG_NOTICE, "packet with short header received on %s\n",
		    if_name(ifp));
		return;
	}
	ar = mtod(m, struct arphdr *);

	/* Check if length is sufficient */
	if (m->m_len <  arphdr_len(ar)) {
		m = m_pullup(m, arphdr_len(ar));
		if (m == NULL) {
			ARP_LOG(LOG_NOTICE, "short packet received on %s\n",
			    if_name(ifp));
			return;
		}
		ar = mtod(m, struct arphdr *);
	}

	hlen = 0;
	layer = "";
	switch (ntohs(ar->ar_hrd)) {
	case ARPHRD_ETHER:
		hlen = ETHER_ADDR_LEN; /* RFC 826 */
		layer = "ethernet";
		break;
	case ARPHRD_INFINIBAND:
		hlen = 20;	/* RFC 4391, INFINIBAND_ALEN */ 
		layer = "infiniband";
		break;
	case ARPHRD_IEEE1394:
		hlen = 0; /* SHALL be 16 */ /* RFC 2734 */
		layer = "firewire";

		/*
		 * Restrict too long hardware addresses.
		 * Currently we are capable of handling 20-byte
		 * addresses ( sizeof(lle->ll_addr) )
		 */
		if (ar->ar_hln >= 20)
			hlen = 16;
		break;
	default:
		ARP_LOG(LOG_NOTICE,
		    "packet with unknown hardware format 0x%02d received on "
		    "%s\n", ntohs(ar->ar_hrd), if_name(ifp));
		m_freem(m);
		return;
	}

	if (hlen != 0 && hlen != ar->ar_hln) {
		ARP_LOG(LOG_NOTICE,
		    "packet with invalid %s address length %d received on %s\n",
		    layer, ar->ar_hln, if_name(ifp));
		m_freem(m);
		return;
	}

	ARPSTAT_INC(received);
	switch (ntohs(ar->ar_pro)) {
#ifdef INET
	case ETHERTYPE_IP:
		in_arpinput(m);
		return;
#endif
	}
	m_freem(m);
}

#ifdef INET
/*
 * ARP for Internet protocols on 10 Mb/s Ethernet.
 * Algorithm is that given in RFC 826.
 * In addition, a sanity check is performed on the sender
 * protocol address, to catch impersonators.
 * We no longer handle negotiations for use of trailer protocol:
 * Formerly, ARP replied for protocol type ETHERTYPE_TRAIL sent
 * along with IP replies if we wanted trailers sent to us,
 * and also sent them in response to IP replies.
 * This allowed either end to announce the desire to receive
 * trailer packets.
 * We no longer reply to requests for ETHERTYPE_TRAIL protocol either,
 * but formerly didn't normally send requests.
 */
static int log_arp_wrong_iface = 1;
static int log_arp_movements = 1;
static int log_arp_permanent_modify = 1;
static int allow_multicast = 0;

SYSCTL_INT(_net_link_ether_inet, OID_AUTO, log_arp_wrong_iface, CTLFLAG_RW,
	&log_arp_wrong_iface, 0,
	"log arp packets arriving on the wrong interface");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, log_arp_movements, CTLFLAG_RW,
	&log_arp_movements, 0,
	"log arp replies from MACs different than the one in the cache");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, log_arp_permanent_modify, CTLFLAG_RW,
	&log_arp_permanent_modify, 0,
	"log arp replies from MACs different than the one in the permanent arp entry");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, allow_multicast, CTLFLAG_RW,
	&allow_multicast, 0, "accept multicast addresses");

static void
in_arpinput(struct mbuf *m)
{
	struct rm_priotracker in_ifa_tracker;
	struct arphdr *ah;
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct llentry *la = NULL, *la_tmp;
	struct ifaddr *ifa;
	struct in_ifaddr *ia;
	struct sockaddr sa;
	struct in_addr isaddr, itaddr, myaddr;
	u_int8_t *enaddr = NULL;
	int op;
	int bridged = 0, is_bridge = 0;
	int carped;
	struct sockaddr_in sin;
	struct sockaddr *dst;
	struct nhop4_basic nh4;
	uint8_t linkhdr[LLE_MAX_LINKHDR];
	struct route ro;
	size_t linkhdrsize;
	int lladdr_off;
	int error;
	char addrbuf[INET_ADDRSTRLEN];
	struct epoch_tracker et;

	sin.sin_len = sizeof(struct sockaddr_in);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = 0;

	if (ifp->if_bridge)
		bridged = 1;
	if (ifp->if_type == IFT_BRIDGE)
		is_bridge = 1;

	/*
	 * We already have checked that mbuf contains enough contiguous data
	 * to hold entire arp message according to the arp header.
	 */
	ah = mtod(m, struct arphdr *);

	/*
	 * ARP is only for IPv4 so we can reject packets with
	 * a protocol length not equal to an IPv4 address.
	 */
	if (ah->ar_pln != sizeof(struct in_addr)) {
		ARP_LOG(LOG_NOTICE, "requested protocol length != %zu\n",
		    sizeof(struct in_addr));
		goto drop;
	}

	if (allow_multicast == 0 && ETHER_IS_MULTICAST(ar_sha(ah))) {
		ARP_LOG(LOG_NOTICE, "%*D is multicast\n",
		    ifp->if_addrlen, (u_char *)ar_sha(ah), ":");
		goto drop;
	}

	op = ntohs(ah->ar_op);
	(void)memcpy(&isaddr, ar_spa(ah), sizeof (isaddr));
	(void)memcpy(&itaddr, ar_tpa(ah), sizeof (itaddr));

	if (op == ARPOP_REPLY)
		ARPSTAT_INC(rxreplies);

	/*
	 * For a bridge, we want to check the address irrespective
	 * of the receive interface. (This will change slightly
	 * when we have clusters of interfaces).
	 */
	IN_IFADDR_RLOCK(&in_ifa_tracker);
	LIST_FOREACH(ia, INADDR_HASH(itaddr.s_addr), ia_hash) {
		if (((bridged && ia->ia_ifp->if_bridge == ifp->if_bridge) ||
		    ia->ia_ifp == ifp) &&
		    itaddr.s_addr == ia->ia_addr.sin_addr.s_addr &&
		    (ia->ia_ifa.ifa_carp == NULL ||
		    (*carp_iamatch_p)(&ia->ia_ifa, &enaddr))) {
			ifa_ref(&ia->ia_ifa);
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			goto match;
		}
	}
	LIST_FOREACH(ia, INADDR_HASH(isaddr.s_addr), ia_hash)
		if (((bridged && ia->ia_ifp->if_bridge == ifp->if_bridge) ||
		    ia->ia_ifp == ifp) &&
		    isaddr.s_addr == ia->ia_addr.sin_addr.s_addr) {
			ifa_ref(&ia->ia_ifa);
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			goto match;
		}

#define BDG_MEMBER_MATCHES_ARP(addr, ifp, ia)				\
  (ia->ia_ifp->if_bridge == ifp->if_softc &&				\
  !bcmp(IF_LLADDR(ia->ia_ifp), IF_LLADDR(ifp), ifp->if_addrlen) &&	\
  addr == ia->ia_addr.sin_addr.s_addr)
	/*
	 * Check the case when bridge shares its MAC address with
	 * some of its children, so packets are claimed by bridge
	 * itself (bridge_input() does it first), but they are really
	 * meant to be destined to the bridge member.
	 */
	if (is_bridge) {
		LIST_FOREACH(ia, INADDR_HASH(itaddr.s_addr), ia_hash) {
			if (BDG_MEMBER_MATCHES_ARP(itaddr.s_addr, ifp, ia)) {
				ifa_ref(&ia->ia_ifa);
				ifp = ia->ia_ifp;
				IN_IFADDR_RUNLOCK(&in_ifa_tracker);
				goto match;
			}
		}
	}
#undef BDG_MEMBER_MATCHES_ARP
	IN_IFADDR_RUNLOCK(&in_ifa_tracker);

	/*
	 * No match, use the first inet address on the receive interface
	 * as a dummy address for the rest of the function.
	 */
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link)
		if (ifa->ifa_addr->sa_family == AF_INET &&
		    (ifa->ifa_carp == NULL ||
		    (*carp_iamatch_p)(ifa, &enaddr))) {
			ia = ifatoia(ifa);
			ifa_ref(ifa);
			NET_EPOCH_EXIT(et);
			goto match;
		}
	NET_EPOCH_EXIT(et);

	/*
	 * If bridging, fall back to using any inet address.
	 */
	IN_IFADDR_RLOCK(&in_ifa_tracker);
	if (!bridged || (ia = CK_STAILQ_FIRST(&V_in_ifaddrhead)) == NULL) {
		IN_IFADDR_RUNLOCK(&in_ifa_tracker);
		goto drop;
	}
	ifa_ref(&ia->ia_ifa);
	IN_IFADDR_RUNLOCK(&in_ifa_tracker);
match:
	if (!enaddr)
		enaddr = (u_int8_t *)IF_LLADDR(ifp);
	carped = (ia->ia_ifa.ifa_carp != NULL);
	myaddr = ia->ia_addr.sin_addr;
	ifa_free(&ia->ia_ifa);
	if (!bcmp(ar_sha(ah), enaddr, ifp->if_addrlen))
		goto drop;	/* it's from me, ignore it. */
	if (!bcmp(ar_sha(ah), ifp->if_broadcastaddr, ifp->if_addrlen)) {
		ARP_LOG(LOG_NOTICE, "link address is broadcast for IP address "
		    "%s!\n", inet_ntoa_r(isaddr, addrbuf));
		goto drop;
	}

	if (ifp->if_addrlen != ah->ar_hln) {
		ARP_LOG(LOG_WARNING, "from %*D: addr len: new %d, "
		    "i/f %d (ignored)\n", ifp->if_addrlen,
		    (u_char *) ar_sha(ah), ":", ah->ar_hln,
		    ifp->if_addrlen);
		goto drop;
	}

	/*
	 * Warn if another host is using the same IP address, but only if the
	 * IP address isn't 0.0.0.0, which is used for DHCP only, in which
	 * case we suppress the warning to avoid false positive complaints of
	 * potential misconfiguration.
	 */
	if (!bridged && !carped && isaddr.s_addr == myaddr.s_addr &&
	    myaddr.s_addr != 0) {
		ARP_LOG(LOG_ERR, "%*D is using my IP address %s on %s!\n",
		   ifp->if_addrlen, (u_char *)ar_sha(ah), ":",
		   inet_ntoa_r(isaddr, addrbuf), ifp->if_xname);
		itaddr = myaddr;
		ARPSTAT_INC(dupips);
		goto reply;
	}
	if (ifp->if_flags & IFF_STATICARP)
		goto reply;

	bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(struct sockaddr_in);
	sin.sin_family = AF_INET;
	sin.sin_addr = isaddr;
	dst = (struct sockaddr *)&sin;
	NET_EPOCH_ENTER(et);
	la = lla_lookup(LLTABLE(ifp), LLE_EXCLUSIVE, dst);
	NET_EPOCH_EXIT(et);
	if (la != NULL)
		arp_check_update_lle(ah, isaddr, ifp, bridged, la);
	else if (itaddr.s_addr == myaddr.s_addr) {
		/*
		 * Request/reply to our address, but no lle exists yet.
		 * Calculate full link prepend to use in lle.
		 */
		linkhdrsize = sizeof(linkhdr);
		if (lltable_calc_llheader(ifp, AF_INET, ar_sha(ah), linkhdr,
		    &linkhdrsize, &lladdr_off) != 0)
			goto reply;

		/* Allocate new entry */
		la = lltable_alloc_entry(LLTABLE(ifp), 0, dst);
		if (la == NULL) {

			/*
			 * lle creation may fail if source address belongs
			 * to non-directly connected subnet. However, we
			 * will try to answer the request instead of dropping
			 * frame.
			 */
			goto reply;
		}
		lltable_set_entry_addr(ifp, la, linkhdr, linkhdrsize,
		    lladdr_off);

		IF_AFDATA_WLOCK(ifp);
		LLE_WLOCK(la);
		la_tmp = lla_lookup(LLTABLE(ifp), LLE_EXCLUSIVE, dst);

		/*
		 * Check if lle still does not exists.
		 * If it does, that means that we either
		 * 1) have configured it explicitly, via
		 * 1a) 'arp -s' static entry or
		 * 1b) interface address static record
		 * or
		 * 2) it was the result of sending first packet to-host
		 * or
		 * 3) it was another arp reply packet we handled in
		 * different thread.
		 *
		 * In all cases except 3) we definitely need to prefer
		 * existing lle. For the sake of simplicity, prefer any
		 * existing lle over newly-create one.
		 */
		if (la_tmp == NULL)
			lltable_link_entry(LLTABLE(ifp), la);
		IF_AFDATA_WUNLOCK(ifp);

		if (la_tmp == NULL) {
			arp_mark_lle_reachable(la);
			LLE_WUNLOCK(la);
		} else {
			/* Free newly-create entry and handle packet */
			lltable_free_entry(LLTABLE(ifp), la);
			la = la_tmp;
			la_tmp = NULL;
			arp_check_update_lle(ah, isaddr, ifp, bridged, la);
			/* arp_check_update_lle() returns @la unlocked */
		}
		la = NULL;
	}
reply:
	if (op != ARPOP_REQUEST)
		goto drop;
	ARPSTAT_INC(rxrequests);

	if (itaddr.s_addr == myaddr.s_addr) {
		/* Shortcut.. the receiving interface is the target. */
		(void)memcpy(ar_tha(ah), ar_sha(ah), ah->ar_hln);
		(void)memcpy(ar_sha(ah), enaddr, ah->ar_hln);
	} else {
		struct llentry *lle = NULL;

		sin.sin_addr = itaddr;
		NET_EPOCH_ENTER(et);
		lle = lla_lookup(LLTABLE(ifp), 0, (struct sockaddr *)&sin);
		NET_EPOCH_EXIT(et);

		if ((lle != NULL) && (lle->la_flags & LLE_PUB)) {
			(void)memcpy(ar_tha(ah), ar_sha(ah), ah->ar_hln);
			(void)memcpy(ar_sha(ah), lle->ll_addr, ah->ar_hln);
			LLE_RUNLOCK(lle);
		} else {

			if (lle != NULL)
				LLE_RUNLOCK(lle);

			if (!V_arp_proxyall)
				goto drop;

			/* XXX MRT use table 0 for arp reply  */
			if (fib4_lookup_nh_basic(0, itaddr, 0, 0, &nh4) != 0)
				goto drop;

			/*
			 * Don't send proxies for nodes on the same interface
			 * as this one came out of, or we'll get into a fight
			 * over who claims what Ether address.
			 */
			if (nh4.nh_ifp == ifp)
				goto drop;

			(void)memcpy(ar_tha(ah), ar_sha(ah), ah->ar_hln);
			(void)memcpy(ar_sha(ah), enaddr, ah->ar_hln);

			/*
			 * Also check that the node which sent the ARP packet
			 * is on the interface we expect it to be on. This
			 * avoids ARP chaos if an interface is connected to the
			 * wrong network.
			 */

			/* XXX MRT use table 0 for arp checks */
			if (fib4_lookup_nh_basic(0, isaddr, 0, 0, &nh4) != 0)
				goto drop;
			if (nh4.nh_ifp != ifp) {
				ARP_LOG(LOG_INFO, "proxy: ignoring request"
				    " from %s via %s\n",
				    inet_ntoa_r(isaddr, addrbuf),
				    ifp->if_xname);
				goto drop;
			}

#ifdef DEBUG_PROXY
			printf("arp: proxying for %s\n",
			    inet_ntoa_r(itaddr, addrbuf));
#endif
		}
	}

	if (itaddr.s_addr == myaddr.s_addr &&
	    IN_LINKLOCAL(ntohl(itaddr.s_addr))) {
		/* RFC 3927 link-local IPv4; always reply by broadcast. */
#ifdef DEBUG_LINKLOCAL
		printf("arp: sending reply for link-local addr %s\n",
		    inet_ntoa_r(itaddr, addrbuf));
#endif
		m->m_flags |= M_BCAST;
		m->m_flags &= ~M_MCAST;
	} else {
		/* default behaviour; never reply by broadcast. */
		m->m_flags &= ~(M_BCAST|M_MCAST);
	}
	(void)memcpy(ar_tpa(ah), ar_spa(ah), ah->ar_pln);
	(void)memcpy(ar_spa(ah), &itaddr, ah->ar_pln);
	ah->ar_op = htons(ARPOP_REPLY);
	ah->ar_pro = htons(ETHERTYPE_IP); /* let's be sure! */
	m->m_len = sizeof(*ah) + (2 * ah->ar_pln) + (2 * ah->ar_hln);
	m->m_pkthdr.len = m->m_len;
	m->m_pkthdr.rcvif = NULL;
	sa.sa_family = AF_ARP;
	sa.sa_len = 2;

	/* Calculate link header for sending frame */
	bzero(&ro, sizeof(ro));
	linkhdrsize = sizeof(linkhdr);
	error = arp_fillheader(ifp, ah, 0, linkhdr, &linkhdrsize);

	/*
	 * arp_fillheader() may fail due to lack of support inside encap request
	 * routing. This is not necessary an error, AF_ARP can/should be handled
	 * by if_output().
	 */
	if (error != 0 && error != EAFNOSUPPORT) {
		ARP_LOG(LOG_ERR, "Failed to calculate ARP header on %s: %d\n",
		    if_name(ifp), error);
		return;
	}

	ro.ro_prepend = linkhdr;
	ro.ro_plen = linkhdrsize;
	ro.ro_flags = 0;

	m_clrprotoflags(m);	/* Avoid confusing lower layers. */
	(*ifp->if_output)(ifp, m, &sa, &ro);
	ARPSTAT_INC(txreplies);
	return;

drop:
	m_freem(m);
}
#endif

/*
 * Checks received arp data against existing @la.
 * Updates lle state/performs notification if necessary.
 */
static void
arp_check_update_lle(struct arphdr *ah, struct in_addr isaddr, struct ifnet *ifp,
    int bridged, struct llentry *la)
{
	struct sockaddr sa;
	struct mbuf *m_hold, *m_hold_next;
	uint8_t linkhdr[LLE_MAX_LINKHDR];
	size_t linkhdrsize;
	int lladdr_off;
	char addrbuf[INET_ADDRSTRLEN];

	LLE_WLOCK_ASSERT(la);

	/* the following is not an error when doing bridging */
	if (!bridged && la->lle_tbl->llt_ifp != ifp) {
		if (log_arp_wrong_iface)
			ARP_LOG(LOG_WARNING, "%s is on %s "
			    "but got reply from %*D on %s\n",
			    inet_ntoa_r(isaddr, addrbuf),
			    la->lle_tbl->llt_ifp->if_xname,
			    ifp->if_addrlen, (u_char *)ar_sha(ah), ":",
			    ifp->if_xname);
		LLE_WUNLOCK(la);
		return;
	}
	if ((la->la_flags & LLE_VALID) &&
	    bcmp(ar_sha(ah), la->ll_addr, ifp->if_addrlen)) {
		if (la->la_flags & LLE_STATIC) {
			LLE_WUNLOCK(la);
			if (log_arp_permanent_modify)
				ARP_LOG(LOG_ERR,
				    "%*D attempts to modify "
				    "permanent entry for %s on %s\n",
				    ifp->if_addrlen,
				    (u_char *)ar_sha(ah), ":",
				    inet_ntoa_r(isaddr, addrbuf),
				    ifp->if_xname);
			return;
		}
		if (log_arp_movements) {
			ARP_LOG(LOG_INFO, "%s moved from %*D "
			    "to %*D on %s\n",
			    inet_ntoa_r(isaddr, addrbuf),
			    ifp->if_addrlen,
			    (u_char *)la->ll_addr, ":",
			    ifp->if_addrlen, (u_char *)ar_sha(ah), ":",
			    ifp->if_xname);
		}
	}

	/* Calculate full link prepend to use in lle */
	linkhdrsize = sizeof(linkhdr);
	if (lltable_calc_llheader(ifp, AF_INET, ar_sha(ah), linkhdr,
	    &linkhdrsize, &lladdr_off) != 0)
		return;

	/* Check if something has changed */
	if (memcmp(la->r_linkdata, linkhdr, linkhdrsize) != 0 ||
	    (la->la_flags & LLE_VALID) == 0) {
		/* Try to perform LLE update */
		if (lltable_try_set_entry_addr(ifp, la, linkhdr, linkhdrsize,
		    lladdr_off) == 0)
			return;

		/* Clear fast path feedback request if set */
		la->r_skip_req = 0;
	}

	arp_mark_lle_reachable(la);

	/*
	 * The packets are all freed within the call to the output
	 * routine.
	 *
	 * NB: The lock MUST be released before the call to the
	 * output routine.
	 */
	if (la->la_hold != NULL) {
		m_hold = la->la_hold;
		la->la_hold = NULL;
		la->la_numheld = 0;
		lltable_fill_sa_entry(la, &sa);
		LLE_WUNLOCK(la);
		for (; m_hold != NULL; m_hold = m_hold_next) {
			m_hold_next = m_hold->m_nextpkt;
			m_hold->m_nextpkt = NULL;
			/* Avoid confusing lower layers. */
			m_clrprotoflags(m_hold);
			(*ifp->if_output)(ifp, m_hold, &sa, NULL);
		}
	} else
		LLE_WUNLOCK(la);
}

static void
arp_mark_lle_reachable(struct llentry *la)
{
	int canceled, wtime;

	LLE_WLOCK_ASSERT(la);

	la->ln_state = ARP_LLINFO_REACHABLE;
	EVENTHANDLER_INVOKE(lle_event, la, LLENTRY_RESOLVED);

	if (!(la->la_flags & LLE_STATIC)) {
		LLE_ADDREF(la);
		la->la_expire = time_uptime + V_arpt_keep;
		wtime = V_arpt_keep - V_arp_maxtries * V_arpt_rexmit;
		if (wtime < 0)
			wtime = V_arpt_keep;
		canceled = callout_reset(&la->lle_timer,
		    hz * wtime, arptimer, la);
		if (canceled)
			LLE_REMREF(la);
	}
	la->la_asked = 0;
	la->la_preempt = V_arp_maxtries;
}

/*
 * Add permanent link-layer record for given interface address.
 */
static __noinline void
arp_add_ifa_lle(struct ifnet *ifp, const struct sockaddr *dst)
{
	struct llentry *lle, *lle_tmp;

	/*
	 * Interface address LLE record is considered static
	 * because kernel code relies on LLE_STATIC flag to check
	 * if these entries can be rewriten by arp updates.
	 */
	lle = lltable_alloc_entry(LLTABLE(ifp), LLE_IFADDR | LLE_STATIC, dst);
	if (lle == NULL) {
		log(LOG_INFO, "arp_ifinit: cannot create arp "
		    "entry for interface address\n");
		return;
	}

	IF_AFDATA_WLOCK(ifp);
	LLE_WLOCK(lle);
	/* Unlink any entry if exists */
	lle_tmp = lla_lookup(LLTABLE(ifp), LLE_EXCLUSIVE, dst);
	if (lle_tmp != NULL)
		lltable_unlink_entry(LLTABLE(ifp), lle_tmp);

	lltable_link_entry(LLTABLE(ifp), lle);
	IF_AFDATA_WUNLOCK(ifp);

	if (lle_tmp != NULL)
		EVENTHANDLER_INVOKE(lle_event, lle_tmp, LLENTRY_EXPIRED);

	EVENTHANDLER_INVOKE(lle_event, lle, LLENTRY_RESOLVED);
	LLE_WUNLOCK(lle);
	if (lle_tmp != NULL)
		lltable_free_entry(LLTABLE(ifp), lle_tmp);
}

/*
 * Handle the garp_rexmit_count. Like sysctl_handle_int(), but limits the range
 * of valid values.
 */
static int
sysctl_garp_rexmit(SYSCTL_HANDLER_ARGS)
{
	int error;
	int rexmit_count = *(int *)arg1;

	error = sysctl_handle_int(oidp, &rexmit_count, 0, req);

	/* Enforce limits on any new value that may have been set. */
	if (!error && req->newptr) {
		/* A new value was set. */
		if (rexmit_count < 0) {
			rexmit_count = 0;
		} else if (rexmit_count > MAX_GARP_RETRANSMITS) {
			rexmit_count = MAX_GARP_RETRANSMITS;
		}
		*(int *)arg1 = rexmit_count;
	}

	return (error);
}

/*
 * Retransmit a Gratuitous ARP (GARP) and, if necessary, schedule a callout to
 * retransmit it again. A pending callout owns a reference to the ifa.
 */
static void
garp_rexmit(void *arg)
{
	struct in_ifaddr *ia = arg;

	if (callout_pending(&ia->ia_garp_timer) ||
	    !callout_active(&ia->ia_garp_timer)) {
		IF_ADDR_WUNLOCK(ia->ia_ifa.ifa_ifp);
		ifa_free(&ia->ia_ifa);
		return;
	}

	CURVNET_SET(ia->ia_ifa.ifa_ifp->if_vnet);

	/*
	 * Drop lock while the ARP request is generated.
	 */
	IF_ADDR_WUNLOCK(ia->ia_ifa.ifa_ifp);

	arprequest(ia->ia_ifa.ifa_ifp, &IA_SIN(ia)->sin_addr,
	    &IA_SIN(ia)->sin_addr, IF_LLADDR(ia->ia_ifa.ifa_ifp));

	/*
	 * Increment the count of retransmissions. If the count has reached the
	 * maximum value, stop sending the GARP packets. Otherwise, schedule
	 * the callout to retransmit another GARP packet.
	 */
	++ia->ia_garp_count;
	if (ia->ia_garp_count >= garp_rexmit_count) {
		ifa_free(&ia->ia_ifa);
	} else {
		int rescheduled;
		IF_ADDR_WLOCK(ia->ia_ifa.ifa_ifp);
		rescheduled = callout_reset(&ia->ia_garp_timer,
		    (1 << ia->ia_garp_count) * hz,
		    garp_rexmit, ia);
		IF_ADDR_WUNLOCK(ia->ia_ifa.ifa_ifp);
		if (rescheduled) {
			ifa_free(&ia->ia_ifa);
		}
	}

	CURVNET_RESTORE();
}

/*
 * Start the GARP retransmit timer.
 *
 * A single GARP is always transmitted when an IPv4 address is added
 * to an interface and that is usually sufficient. However, in some
 * circumstances, such as when a shared address is passed between
 * cluster nodes, this single GARP may occasionally be dropped or
 * lost. This can lead to neighbors on the network link working with a
 * stale ARP cache and sending packets destined for that address to
 * the node that previously owned the address, which may not respond.
 *
 * To avoid this situation, GARP retransmits can be enabled by setting
 * the net.link.ether.inet.garp_rexmit_count sysctl to a value greater
 * than zero. The setting represents the maximum number of
 * retransmissions. The interval between retransmissions is calculated
 * using an exponential backoff algorithm, doubling each time, so the
 * retransmission intervals are: {1, 2, 4, 8, 16, ...} (seconds).
 */
static void
garp_timer_start(struct ifaddr *ifa)
{
	struct in_ifaddr *ia = (struct in_ifaddr *) ifa;

	IF_ADDR_WLOCK(ia->ia_ifa.ifa_ifp);
	ia->ia_garp_count = 0;
	if (callout_reset(&ia->ia_garp_timer, (1 << ia->ia_garp_count) * hz,
	    garp_rexmit, ia) == 0) {
		ifa_ref(ifa);
	}
	IF_ADDR_WUNLOCK(ia->ia_ifa.ifa_ifp);
}

void
arp_ifinit(struct ifnet *ifp, struct ifaddr *ifa)
{
	const struct sockaddr_in *dst_in;
	const struct sockaddr *dst;

	if (ifa->ifa_carp != NULL)
		return;

	dst = ifa->ifa_addr;
	dst_in = (const struct sockaddr_in *)dst;

	if (ntohl(dst_in->sin_addr.s_addr) == INADDR_ANY)
		return;
	arp_announce_ifaddr(ifp, dst_in->sin_addr, IF_LLADDR(ifp));
	if (garp_rexmit_count > 0) {
		garp_timer_start(ifa);
	}

	arp_add_ifa_lle(ifp, dst);
}

void
arp_announce_ifaddr(struct ifnet *ifp, struct in_addr addr, u_char *enaddr)
{

	if (ntohl(addr.s_addr) != INADDR_ANY)
		arprequest(ifp, &addr, &addr, enaddr);
}

/*
 * Sends gratuitous ARPs for each ifaddr to notify other
 * nodes about the address change.
 */
static __noinline void
arp_handle_ifllchange(struct ifnet *ifp)
{
	struct ifaddr *ifa;

	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family == AF_INET)
			arp_ifinit(ifp, ifa);
	}
}

/*
 * A handler for interface link layer address change event.
 */
static void
arp_iflladdr(void *arg __unused, struct ifnet *ifp)
{

	lltable_update_ifaddr(LLTABLE(ifp));

	if ((ifp->if_flags & IFF_UP) != 0)
		arp_handle_ifllchange(ifp);
}

static void
vnet_arp_init(void)
{

	if (IS_DEFAULT_VNET(curvnet)) {
		netisr_register(&arp_nh);
		iflladdr_tag = EVENTHANDLER_REGISTER(iflladdr_event,
		    arp_iflladdr, NULL, EVENTHANDLER_PRI_ANY);
	}
#ifdef VIMAGE
	else
		netisr_register_vnet(&arp_nh);
#endif
}
VNET_SYSINIT(vnet_arp_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_SECOND,
    vnet_arp_init, 0);

#ifdef VIMAGE
/*
 * We have to unregister ARP along with IP otherwise we risk doing INADDR_HASH
 * lookups after destroying the hash.  Ideally this would go on SI_ORDER_3.5.
 */
static void
vnet_arp_destroy(__unused void *arg)
{

	netisr_unregister_vnet(&arp_nh);
}
VNET_SYSUNINIT(vnet_arp_uninit, SI_SUB_PROTO_DOMAIN, SI_ORDER_THIRD,
    vnet_arp_destroy, NULL);
#endif
