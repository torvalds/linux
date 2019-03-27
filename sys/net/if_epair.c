/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 The FreeBSD Foundation
 * Copyright (c) 2009-2010 Bjoern A. Zeeb <bz@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by CK Software GmbH under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * A pair of virtual back-to-back connected ethernet like interfaces
 * (``two interfaces with a virtual cross-over cable'').
 *
 * This is mostly intended to be used to provide connectivity between
 * different virtual network stack instances.
 */
/*
 * Things to re-think once we have more experience:
 * - ifp->if_reassign function once we can test with vimage. Depending on
 *   how if_vmove() is going to be improved.
 * - Real random etheraddrs that are checked to be uniquish; we would need
 *   to re-do them in case we move the interface between network stacks
 *   in a private if_reassign function.
 *   In case we bridge to a real interface/network or between indepedent
 *   epairs on multiple stacks/machines, we may need this.
 *   For now let the user handle that case.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/hash.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/refcount.h>
#include <sys/queue.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>
#include <net/if_media.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/netisr.h>
#include <net/vnet.h>

SYSCTL_DECL(_net_link);
static SYSCTL_NODE(_net_link, OID_AUTO, epair, CTLFLAG_RW, 0, "epair sysctl");

#ifdef EPAIR_DEBUG
static int epair_debug = 0;
SYSCTL_INT(_net_link_epair, OID_AUTO, epair_debug, CTLFLAG_RW,
    &epair_debug, 0, "if_epair(4) debugging.");
#define	DPRINTF(fmt, arg...)						\
	if (epair_debug)						\
		printf("[%s:%d] " fmt, __func__, __LINE__, ##arg)
#else
#define	DPRINTF(fmt, arg...)
#endif

static void epair_nh_sintr(struct mbuf *);
static struct mbuf *epair_nh_m2cpuid(struct mbuf *, uintptr_t, u_int *);
static void epair_nh_drainedcpu(u_int);

static void epair_start_locked(struct ifnet *);
static int epair_media_change(struct ifnet *);
static void epair_media_status(struct ifnet *, struct ifmediareq *);

static int epair_clone_match(struct if_clone *, const char *);
static int epair_clone_create(struct if_clone *, char *, size_t, caddr_t);
static int epair_clone_destroy(struct if_clone *, struct ifnet *);

static const char epairname[] = "epair";
static unsigned int next_index = 0;

/* Netisr related definitions and sysctl. */
static struct netisr_handler epair_nh = {
	.nh_name	= epairname,
	.nh_proto	= NETISR_EPAIR,
	.nh_policy	= NETISR_POLICY_CPU,
	.nh_handler	= epair_nh_sintr,
	.nh_m2cpuid	= epair_nh_m2cpuid,
	.nh_drainedcpu	= epair_nh_drainedcpu,
};

static int
sysctl_epair_netisr_maxqlen(SYSCTL_HANDLER_ARGS)
{
	int error, qlimit;

	netisr_getqlimit(&epair_nh, &qlimit);
	error = sysctl_handle_int(oidp, &qlimit, 0, req);
	if (error || !req->newptr)
		return (error);
	if (qlimit < 1)
		return (EINVAL);
	return (netisr_setqlimit(&epair_nh, qlimit));
}
SYSCTL_PROC(_net_link_epair, OID_AUTO, netisr_maxqlen, CTLTYPE_INT|CTLFLAG_RW,
    0, 0, sysctl_epair_netisr_maxqlen, "I",
    "Maximum if_epair(4) netisr \"hw\" queue length");

struct epair_softc {
	struct ifnet	*ifp;		/* This ifp. */
	struct ifnet	*oifp;		/* other ifp of pair. */
	struct ifmedia	media;		/* Media config (fake). */
	u_int		refcount;	/* # of mbufs in flight. */
	u_int		cpuid;		/* CPU ID assigned upon creation. */
	void		(*if_qflush)(struct ifnet *);
					/* Original if_qflush routine. */
};

/*
 * Per-CPU list of ifps with data in the ifq that needs to be flushed
 * to the netisr ``hw'' queue before we allow any further direct queuing
 * to the ``hw'' queue.
 */
struct epair_ifp_drain {
	STAILQ_ENTRY(epair_ifp_drain)	ifp_next;
	struct ifnet			*ifp;
};
STAILQ_HEAD(eid_list, epair_ifp_drain);

#define	EPAIR_LOCK_INIT(dpcpu)		mtx_init(&(dpcpu)->if_epair_mtx, \
					    "if_epair", NULL, MTX_DEF)
#define	EPAIR_LOCK_DESTROY(dpcpu)	mtx_destroy(&(dpcpu)->if_epair_mtx)
#define	EPAIR_LOCK_ASSERT(dpcpu)	mtx_assert(&(dpcpu)->if_epair_mtx, \
					    MA_OWNED)
#define	EPAIR_LOCK(dpcpu)		mtx_lock(&(dpcpu)->if_epair_mtx)
#define	EPAIR_UNLOCK(dpcpu)		mtx_unlock(&(dpcpu)->if_epair_mtx)

#ifdef INVARIANTS
#define	EPAIR_REFCOUNT_INIT(r, v)	refcount_init((r), (v))
#define	EPAIR_REFCOUNT_AQUIRE(r)	refcount_acquire((r))
#define	EPAIR_REFCOUNT_RELEASE(r)	refcount_release((r))
#define	EPAIR_REFCOUNT_ASSERT(a, p)	KASSERT(a, p)
#else
#define	EPAIR_REFCOUNT_INIT(r, v)
#define	EPAIR_REFCOUNT_AQUIRE(r)
#define	EPAIR_REFCOUNT_RELEASE(r)
#define	EPAIR_REFCOUNT_ASSERT(a, p)
#endif

static MALLOC_DEFINE(M_EPAIR, epairname,
    "Pair of virtual cross-over connected Ethernet-like interfaces");

VNET_DEFINE_STATIC(struct if_clone *, epair_cloner);
#define	V_epair_cloner	VNET(epair_cloner)

/*
 * DPCPU area and functions.
 */
struct epair_dpcpu {
	struct mtx	if_epair_mtx;		/* Per-CPU locking. */
	int		epair_drv_flags;	/* Per-CPU ``hw'' drv flags. */
	struct eid_list	epair_ifp_drain_list;	/* Per-CPU list of ifps with
						 * data in the ifq. */
};
DPCPU_DEFINE(struct epair_dpcpu, epair_dpcpu);

static void
epair_dpcpu_init(void)
{
	struct epair_dpcpu *epair_dpcpu;
	struct eid_list *s;
	u_int cpuid;

	CPU_FOREACH(cpuid) {
		epair_dpcpu = DPCPU_ID_PTR(cpuid, epair_dpcpu);

		/* Initialize per-cpu lock. */
		EPAIR_LOCK_INIT(epair_dpcpu);

		/* Driver flags are per-cpu as are our netisr "hw" queues. */
		epair_dpcpu->epair_drv_flags = 0;

		/*
		 * Initialize per-cpu drain list.
		 * Manually do what STAILQ_HEAD_INITIALIZER would do.
		 */
		s = &epair_dpcpu->epair_ifp_drain_list;
		s->stqh_first = NULL;
		s->stqh_last = &s->stqh_first;
	} 
}

static void
epair_dpcpu_detach(void)
{
	struct epair_dpcpu *epair_dpcpu;
	u_int cpuid;

	CPU_FOREACH(cpuid) {
		epair_dpcpu = DPCPU_ID_PTR(cpuid, epair_dpcpu);

		/* Destroy per-cpu lock. */
		EPAIR_LOCK_DESTROY(epair_dpcpu);
	}
}

/*
 * Helper functions.
 */
static u_int
cpuid_from_ifp(struct ifnet *ifp)
{
	struct epair_softc *sc;

	if (ifp == NULL)
		return (0);
	sc = ifp->if_softc;

	return (sc->cpuid);
}

/*
 * Netisr handler functions.
 */
static void
epair_nh_sintr(struct mbuf *m)
{
	struct ifnet *ifp;
	struct epair_softc *sc __unused;

	ifp = m->m_pkthdr.rcvif;
	(*ifp->if_input)(ifp, m);
	sc = ifp->if_softc;
	EPAIR_REFCOUNT_RELEASE(&sc->refcount);
	EPAIR_REFCOUNT_ASSERT((int)sc->refcount >= 1,
	    ("%s: ifp=%p sc->refcount not >= 1: %d",
	    __func__, ifp, sc->refcount));
	DPRINTF("ifp=%p refcount=%u\n", ifp, sc->refcount);
}

static struct mbuf *
epair_nh_m2cpuid(struct mbuf *m, uintptr_t source, u_int *cpuid)
{

	*cpuid = cpuid_from_ifp(m->m_pkthdr.rcvif);

	return (m);
}

static void
epair_nh_drainedcpu(u_int cpuid)
{
	struct epair_dpcpu *epair_dpcpu;
	struct epair_ifp_drain *elm, *tvar;
	struct ifnet *ifp;

	epair_dpcpu = DPCPU_ID_PTR(cpuid, epair_dpcpu);
	EPAIR_LOCK(epair_dpcpu);
	/*
	 * Assume our "hw" queue and possibly ifq will be emptied
	 * again. In case we will overflow the "hw" queue while
	 * draining, epair_start_locked will set IFF_DRV_OACTIVE
	 * again and we will stop and return.
	 */
	STAILQ_FOREACH_SAFE(elm, &epair_dpcpu->epair_ifp_drain_list,
	    ifp_next, tvar) {
		ifp = elm->ifp;
		epair_dpcpu->epair_drv_flags &= ~IFF_DRV_OACTIVE;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		epair_start_locked(ifp);

		IFQ_LOCK(&ifp->if_snd);
		if (IFQ_IS_EMPTY(&ifp->if_snd)) {
			struct epair_softc *sc __unused;

			STAILQ_REMOVE(&epair_dpcpu->epair_ifp_drain_list,
			    elm, epair_ifp_drain, ifp_next);
			/* The cached ifp goes off the list. */
			sc = ifp->if_softc;
			EPAIR_REFCOUNT_RELEASE(&sc->refcount);
			EPAIR_REFCOUNT_ASSERT((int)sc->refcount >= 1,
			    ("%s: ifp=%p sc->refcount not >= 1: %d",
			    __func__, ifp, sc->refcount));
			free(elm, M_EPAIR);
		}
		IFQ_UNLOCK(&ifp->if_snd);

		if ((ifp->if_drv_flags & IFF_DRV_OACTIVE) != 0) {
			/* Our "hw"q overflew again. */
			epair_dpcpu->epair_drv_flags |= IFF_DRV_OACTIVE;
			DPRINTF("hw queue length overflow at %u\n",
			    epair_nh.nh_qlimit);
			break;
		}
	}
	EPAIR_UNLOCK(epair_dpcpu);
}

/*
 * Network interface (`if') related functions.
 */
static void
epair_remove_ifp_from_draining(struct ifnet *ifp)
{
	struct epair_dpcpu *epair_dpcpu;
	struct epair_ifp_drain *elm, *tvar;
	u_int cpuid;

	CPU_FOREACH(cpuid) {
		epair_dpcpu = DPCPU_ID_PTR(cpuid, epair_dpcpu);
		EPAIR_LOCK(epair_dpcpu);
		STAILQ_FOREACH_SAFE(elm, &epair_dpcpu->epair_ifp_drain_list,
		    ifp_next, tvar) {
			if (ifp == elm->ifp) {
				struct epair_softc *sc __unused;

				STAILQ_REMOVE(
				    &epair_dpcpu->epair_ifp_drain_list, elm,
				    epair_ifp_drain, ifp_next);
				/* The cached ifp goes off the list. */
				sc = ifp->if_softc;
				EPAIR_REFCOUNT_RELEASE(&sc->refcount);
				EPAIR_REFCOUNT_ASSERT((int)sc->refcount >= 1,
				    ("%s: ifp=%p sc->refcount not >= 1: %d",
				    __func__, ifp, sc->refcount));
				free(elm, M_EPAIR);
			}
		}
		EPAIR_UNLOCK(epair_dpcpu);
	}
}

static int
epair_add_ifp_for_draining(struct ifnet *ifp)
{
	struct epair_dpcpu *epair_dpcpu;
	struct epair_softc *sc;
	struct epair_ifp_drain *elm = NULL;

	sc = ifp->if_softc;
	epair_dpcpu = DPCPU_ID_PTR(sc->cpuid, epair_dpcpu);
	EPAIR_LOCK_ASSERT(epair_dpcpu);
	STAILQ_FOREACH(elm, &epair_dpcpu->epair_ifp_drain_list, ifp_next)
		if (elm->ifp == ifp)
			break;
	/* If the ifp is there already, return success. */
	if (elm != NULL)
		return (0);

	elm = malloc(sizeof(struct epair_ifp_drain), M_EPAIR, M_NOWAIT|M_ZERO);
	if (elm == NULL)
		return (ENOMEM);

	elm->ifp = ifp;
	/* Add a reference for the ifp pointer on the list. */
	EPAIR_REFCOUNT_AQUIRE(&sc->refcount);
	STAILQ_INSERT_TAIL(&epair_dpcpu->epair_ifp_drain_list, elm, ifp_next);

	return (0);
}

static void
epair_start_locked(struct ifnet *ifp)
{
	struct epair_dpcpu *epair_dpcpu;
	struct mbuf *m;
	struct epair_softc *sc;
	struct ifnet *oifp;
	int error;

	DPRINTF("ifp=%p\n", ifp);
	sc = ifp->if_softc;
	epair_dpcpu = DPCPU_ID_PTR(sc->cpuid, epair_dpcpu);
	EPAIR_LOCK_ASSERT(epair_dpcpu);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;
	if ((ifp->if_flags & IFF_UP) == 0)
		return;

	/*
	 * We get packets here from ether_output via if_handoff()
	 * and need to put them into the input queue of the oifp
	 * and call oifp->if_input() via netisr/epair_sintr().
	 */
	oifp = sc->oifp;
	sc = oifp->if_softc;
	for (;;) {
		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		BPF_MTAP(ifp, m);

		/*
		 * In case the outgoing interface is not usable,
		 * drop the packet.
		 */
		if ((oifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
		    (oifp->if_flags & IFF_UP) ==0) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			m_freem(m);
			continue;
		}
		DPRINTF("packet %s -> %s\n", ifp->if_xname, oifp->if_xname);

		/*
		 * Add a reference so the interface cannot go while the
		 * packet is in transit as we rely on rcvif to stay valid.
		 */
		EPAIR_REFCOUNT_AQUIRE(&sc->refcount);
		m->m_pkthdr.rcvif = oifp;
		CURVNET_SET_QUIET(oifp->if_vnet);
		error = netisr_queue(NETISR_EPAIR, m);
		CURVNET_RESTORE();
		if (!error) {
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
			/* Someone else received the packet. */
			if_inc_counter(oifp, IFCOUNTER_IPACKETS, 1);
		} else {
			/* The packet was freed already. */
			epair_dpcpu->epair_drv_flags |= IFF_DRV_OACTIVE;
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			(void) epair_add_ifp_for_draining(ifp);
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			EPAIR_REFCOUNT_RELEASE(&sc->refcount);
			EPAIR_REFCOUNT_ASSERT((int)sc->refcount >= 1,
			    ("%s: ifp=%p sc->refcount not >= 1: %d",
			    __func__, oifp, sc->refcount));
		}
	}
}

static void
epair_start(struct ifnet *ifp)
{
	struct epair_dpcpu *epair_dpcpu;

	epair_dpcpu = DPCPU_ID_PTR(cpuid_from_ifp(ifp), epair_dpcpu);
	EPAIR_LOCK(epair_dpcpu);
	epair_start_locked(ifp);
	EPAIR_UNLOCK(epair_dpcpu);
}

static int
epair_transmit_locked(struct ifnet *ifp, struct mbuf *m)
{
	struct epair_dpcpu *epair_dpcpu;
	struct epair_softc *sc;
	struct ifnet *oifp;
	int error, len;
	short mflags;

	DPRINTF("ifp=%p m=%p\n", ifp, m);
	sc = ifp->if_softc;
	epair_dpcpu = DPCPU_ID_PTR(sc->cpuid, epair_dpcpu);
	EPAIR_LOCK_ASSERT(epair_dpcpu);

	if (m == NULL)
		return (0);
	
	/*
	 * We are not going to use the interface en/dequeue mechanism
	 * on the TX side. We are called from ether_output_frame()
	 * and will put the packet into the incoming queue of the
	 * other interface of our pair via the netsir.
	 */
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		m_freem(m);
		return (ENXIO);
	}
	if ((ifp->if_flags & IFF_UP) == 0) {
		m_freem(m);
		return (ENETDOWN);
	}

	BPF_MTAP(ifp, m);

	/*
	 * In case the outgoing interface is not usable,
	 * drop the packet.
	 */
	oifp = sc->oifp;
	if ((oifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    (oifp->if_flags & IFF_UP) ==0) {
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		m_freem(m);
		return (0);
	}
	len = m->m_pkthdr.len;
	mflags = m->m_flags;
	DPRINTF("packet %s -> %s\n", ifp->if_xname, oifp->if_xname);

#ifdef ALTQ
	/* Support ALTQ via the classic if_start() path. */
	IF_LOCK(&ifp->if_snd);
	if (ALTQ_IS_ENABLED(&ifp->if_snd)) {
		ALTQ_ENQUEUE(&ifp->if_snd, m, NULL, error);
		if (error)
			if_inc_counter(ifp, IFCOUNTER_OQDROPS, 1);
		IF_UNLOCK(&ifp->if_snd);
		if (!error) {
			if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
			if (mflags & (M_BCAST|M_MCAST))
				if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);
			
			if ((ifp->if_drv_flags & IFF_DRV_OACTIVE) == 0)
				epair_start_locked(ifp);
			else
				(void)epair_add_ifp_for_draining(ifp);
		}
		return (error);
	}
	IF_UNLOCK(&ifp->if_snd);
#endif

	if ((epair_dpcpu->epair_drv_flags & IFF_DRV_OACTIVE) != 0) {
		/*
		 * Our hardware queue is full, try to fall back
		 * queuing to the ifq but do not call ifp->if_start.
		 * Either we are lucky or the packet is gone.
		 */
		IFQ_ENQUEUE(&ifp->if_snd, m, error);
		if (!error)
			(void)epair_add_ifp_for_draining(ifp);
		return (error);
	}
	sc = oifp->if_softc;
	/*
	 * Add a reference so the interface cannot go while the
	 * packet is in transit as we rely on rcvif to stay valid.
	 */
	EPAIR_REFCOUNT_AQUIRE(&sc->refcount);
	m->m_pkthdr.rcvif = oifp;
	CURVNET_SET_QUIET(oifp->if_vnet);
	error = netisr_queue(NETISR_EPAIR, m);
	CURVNET_RESTORE();
	if (!error) {
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		/*
		 * IFQ_HANDOFF_ADJ/ip_handoff() update statistics,
		 * but as we bypass all this we have to duplicate
		 * the logic another time.
		 */
		if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
		if (mflags & (M_BCAST|M_MCAST))
			if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);
		/* Someone else received the packet. */
		if_inc_counter(oifp, IFCOUNTER_IPACKETS, 1);
	} else {
		/* The packet was freed already. */
		epair_dpcpu->epair_drv_flags |= IFF_DRV_OACTIVE;
		ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		EPAIR_REFCOUNT_RELEASE(&sc->refcount);
		EPAIR_REFCOUNT_ASSERT((int)sc->refcount >= 1,
		    ("%s: ifp=%p sc->refcount not >= 1: %d",
		    __func__, oifp, sc->refcount));
	}

	return (error);
}

static int
epair_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct epair_dpcpu *epair_dpcpu;
	int error;

	epair_dpcpu = DPCPU_ID_PTR(cpuid_from_ifp(ifp), epair_dpcpu);
	EPAIR_LOCK(epair_dpcpu);
	error = epair_transmit_locked(ifp, m);
	EPAIR_UNLOCK(epair_dpcpu);
	return (error);
}

static void
epair_qflush(struct ifnet *ifp)
{
	struct epair_softc *sc;
	
	sc = ifp->if_softc;
	KASSERT(sc != NULL, ("%s: ifp=%p, epair_softc gone? sc=%p\n",
	    __func__, ifp, sc));
	/*
	 * Remove this ifp from all backpointer lists. The interface will not
	 * usable for flushing anyway nor should it have anything to flush
	 * after if_qflush().
	 */
	epair_remove_ifp_from_draining(ifp);

	if (sc->if_qflush)
		sc->if_qflush(ifp);
}

static int
epair_media_change(struct ifnet *ifp __unused)
{

	/* Do nothing. */
	return (0);
}

static void
epair_media_status(struct ifnet *ifp __unused, struct ifmediareq *imr)
{

	imr->ifm_status = IFM_AVALID | IFM_ACTIVE;
	imr->ifm_active = IFM_ETHER | IFM_10G_T | IFM_FDX;
}

static int
epair_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct epair_softc *sc;
	struct ifreq *ifr;
	int error;

	ifr = (struct ifreq *)data;
	switch (cmd) {
	case SIOCSIFFLAGS:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = 0;
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		sc = ifp->if_softc;
		error = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
		break;

	case SIOCSIFMTU:
		/* We basically allow all kinds of MTUs. */
		ifp->if_mtu = ifr->ifr_mtu;
		error = 0;
		break;

	default:
		/* Let the common ethernet handler process this. */
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static void
epair_init(void *dummy __unused)
{
}


/*
 * Interface cloning functions.
 * We use our private ones so that we can create/destroy our secondary
 * device along with the primary one.
 */
static int
epair_clone_match(struct if_clone *ifc, const char *name)
{
	const char *cp;

	DPRINTF("name='%s'\n", name);

	/*
	 * Our base name is epair.
	 * Our interfaces will be named epair<n>[ab].
	 * So accept anything of the following list:
	 * - epair
	 * - epair<n>
	 * but not the epair<n>[ab] versions.
	 */
	if (strncmp(epairname, name, sizeof(epairname)-1) != 0)
		return (0);

	for (cp = name + sizeof(epairname) - 1; *cp != '\0'; cp++) {
		if (*cp < '0' || *cp > '9')
			return (0);
	}

	return (1);
}

static int
epair_clone_create(struct if_clone *ifc, char *name, size_t len, caddr_t params)
{
	struct epair_softc *sca, *scb;
	struct ifnet *ifp;
	char *dp;
	int error, unit, wildcard;
	uint64_t hostid;
	uint32_t key[3];
	uint32_t hash;
	uint8_t eaddr[ETHER_ADDR_LEN];	/* 00:00:00:00:00:00 */

	/*
	 * We are abusing params to create our second interface.
	 * Actually we already created it and called if_clone_create()
	 * for it to do the official insertion procedure the moment we knew
	 * it cannot fail anymore. So just do attach it here.
	 */
	if (params) {
		scb = (struct epair_softc *)params;
		ifp = scb->ifp;
		/* Copy epairNa etheraddr and change the last byte. */
		memcpy(eaddr, scb->oifp->if_hw_addr, ETHER_ADDR_LEN);
		eaddr[5] = 0x0b;
		ether_ifattach(ifp, eaddr);
		/* Correctly set the name for the cloner list. */
		strlcpy(name, ifp->if_xname, len);
		return (0);
	}

	/* Try to see if a special unit was requested. */
	error = ifc_name2unit(name, &unit);
	if (error != 0)
		return (error);
	wildcard = (unit < 0);

	error = ifc_alloc_unit(ifc, &unit);
	if (error != 0)
		return (error);

	/*
	 * If no unit had been given, we need to adjust the ifName.
	 * Also make sure there is space for our extra [ab] suffix.
	 */
	for (dp = name; *dp != '\0'; dp++);
	if (wildcard) {
		error = snprintf(dp, len - (dp - name), "%d", unit);
		if (error > len - (dp - name) - 1) {
			/* ifName too long. */
			ifc_free_unit(ifc, unit);
			return (ENOSPC);
		}
		dp += error;
	}
	if (len - (dp - name) - 1 < 1) {
		/* No space left for our [ab] suffix. */
		ifc_free_unit(ifc, unit);
		return (ENOSPC);
	}
	*dp = 'b';
	/* Must not change dp so we can replace 'a' by 'b' later. */
	*(dp+1) = '\0';

	/* Check if 'a' and 'b' interfaces already exist. */ 
	if (ifunit(name) != NULL)
		return (EEXIST);
	*dp = 'a';
	if (ifunit(name) != NULL)
		return (EEXIST);

	/* Allocate memory for both [ab] interfaces */
	sca = malloc(sizeof(struct epair_softc), M_EPAIR, M_WAITOK | M_ZERO);
	EPAIR_REFCOUNT_INIT(&sca->refcount, 1);
	sca->ifp = if_alloc(IFT_ETHER);
	if (sca->ifp == NULL) {
		free(sca, M_EPAIR);
		ifc_free_unit(ifc, unit);
		return (ENOSPC);
	}

	scb = malloc(sizeof(struct epair_softc), M_EPAIR, M_WAITOK | M_ZERO);
	EPAIR_REFCOUNT_INIT(&scb->refcount, 1);
	scb->ifp = if_alloc(IFT_ETHER);
	if (scb->ifp == NULL) {
		free(scb, M_EPAIR);
		if_free(sca->ifp);
		free(sca, M_EPAIR);
		ifc_free_unit(ifc, unit);
		return (ENOSPC);
	}
	
	/*
	 * Cross-reference the interfaces so we will be able to free both.
	 */
	sca->oifp = scb->ifp;
	scb->oifp = sca->ifp;

	/*
	 * Calculate the cpuid for netisr queueing based on the
	 * ifIndex of the interfaces. As long as we cannot configure
	 * this or use cpuset information easily we cannot guarantee
	 * cache locality but we can at least allow parallelism.
	 */
	sca->cpuid =
	    netisr_get_cpuid(sca->ifp->if_index);
	scb->cpuid =
	    netisr_get_cpuid(scb->ifp->if_index);

	/* Initialise pseudo media types. */
	ifmedia_init(&sca->media, 0, epair_media_change, epair_media_status);
	ifmedia_add(&sca->media, IFM_ETHER | IFM_10G_T, 0, NULL);
	ifmedia_set(&sca->media, IFM_ETHER | IFM_10G_T);
	ifmedia_init(&scb->media, 0, epair_media_change, epair_media_status);
	ifmedia_add(&scb->media, IFM_ETHER | IFM_10G_T, 0, NULL);
	ifmedia_set(&scb->media, IFM_ETHER | IFM_10G_T);
	
	/* Finish initialization of interface <n>a. */
	ifp = sca->ifp;
	ifp->if_softc = sca;
	strlcpy(ifp->if_xname, name, IFNAMSIZ);
	ifp->if_dname = epairname;
	ifp->if_dunit = unit;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ifp->if_capenable = IFCAP_VLAN_MTU;
	ifp->if_start = epair_start;
	ifp->if_ioctl = epair_ioctl;
	ifp->if_init  = epair_init;
	if_setsendqlen(ifp, ifqmaxlen);
	if_setsendqready(ifp);

	/*
	 * Calculate the etheraddr hashing the hostid and the
	 * interface index. The result would be hopefully unique.
	 * Note that the "a" component of an epair instance may get moved
	 * to a different VNET after creation. In that case its index
	 * will be freed and the index can get reused by new epair instance.
	 * Make sure we do not create same etheraddr again.
	 */
	getcredhostid(curthread->td_ucred, (unsigned long *)&hostid);
	if (hostid == 0) 
		arc4rand(&hostid, sizeof(hostid), 0);

	if (ifp->if_index > next_index)
		next_index = ifp->if_index;
	else
		next_index++;

	key[0] = (uint32_t)next_index;
	key[1] = (uint32_t)(hostid & 0xffffffff);
	key[2] = (uint32_t)((hostid >> 32) & 0xfffffffff);
	hash = jenkins_hash32(key, 3, 0);

	eaddr[0] = 0x02;
	memcpy(&eaddr[1], &hash, 4);
	eaddr[5] = 0x0a;
	ether_ifattach(ifp, eaddr);
	sca->if_qflush = ifp->if_qflush;
	ifp->if_qflush = epair_qflush;
	ifp->if_transmit = epair_transmit;
	ifp->if_baudrate = IF_Gbps(10);	/* arbitrary maximum */

	/* Swap the name and finish initialization of interface <n>b. */
	*dp = 'b';

	ifp = scb->ifp;
	ifp->if_softc = scb;
	strlcpy(ifp->if_xname, name, IFNAMSIZ);
	ifp->if_dname = epairname;
	ifp->if_dunit = unit;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ifp->if_capenable = IFCAP_VLAN_MTU;
	ifp->if_start = epair_start;
	ifp->if_ioctl = epair_ioctl;
	ifp->if_init  = epair_init;
	if_setsendqlen(ifp, ifqmaxlen);
	if_setsendqready(ifp);
	/* We need to play some tricks here for the second interface. */
	strlcpy(name, epairname, len);
	error = if_clone_create(name, len, (caddr_t)scb);
	if (error)
		panic("%s: if_clone_create() for our 2nd iface failed: %d",
		    __func__, error);
	scb->if_qflush = ifp->if_qflush;
	ifp->if_qflush = epair_qflush;
	ifp->if_transmit = epair_transmit;
	ifp->if_baudrate = IF_Gbps(10);	/* arbitrary maximum */

	/*
	 * Restore name to <n>a as the ifp for this will go into the
	 * cloner list for the initial call.
	 */
	strlcpy(name, sca->ifp->if_xname, len);
	DPRINTF("name='%s/%db' created sca=%p scb=%p\n", name, unit, sca, scb);

	/* Tell the world, that we are ready to rock. */
	sca->ifp->if_drv_flags |= IFF_DRV_RUNNING;
	scb->ifp->if_drv_flags |= IFF_DRV_RUNNING;
	if_link_state_change(sca->ifp, LINK_STATE_UP);
	if_link_state_change(scb->ifp, LINK_STATE_UP);

	return (0);
}

static int
epair_clone_destroy(struct if_clone *ifc, struct ifnet *ifp)
{
	struct ifnet *oifp;
	struct epair_softc *sca, *scb;
	int unit, error;

	DPRINTF("ifp=%p\n", ifp);

	/*
	 * In case we called into if_clone_destroyif() ourselves
	 * again to remove the second interface, the softc will be
	 * NULL. In that case so not do anything but return success.
	 */
	if (ifp->if_softc == NULL)
		return (0);
	
	unit = ifp->if_dunit;
	sca = ifp->if_softc;
	oifp = sca->oifp;
	scb = oifp->if_softc;

	DPRINTF("ifp=%p oifp=%p\n", ifp, oifp);
	if_link_state_change(ifp, LINK_STATE_DOWN);
	if_link_state_change(oifp, LINK_STATE_DOWN);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	oifp->if_drv_flags &= ~IFF_DRV_RUNNING;

	/*
	 * Get rid of our second half. As the other of the two
	 * interfaces may reside in a different vnet, we need to
	 * switch before freeing them.
	 */
	CURVNET_SET_QUIET(oifp->if_vnet);
	ether_ifdetach(oifp);
	/*
	 * Wait for all packets to be dispatched to if_input.
	 * The numbers can only go down as the interface is
	 * detached so there is no need to use atomics.
	 */
	DPRINTF("scb refcnt=%u\n", scb->refcount);
	EPAIR_REFCOUNT_ASSERT(scb->refcount == 1,
	    ("%s: ifp=%p scb->refcount!=1: %d", __func__, oifp, scb->refcount));
	oifp->if_softc = NULL;
	error = if_clone_destroyif(ifc, oifp);
	if (error)
		panic("%s: if_clone_destroyif() for our 2nd iface failed: %d",
		    __func__, error);
	if_free(oifp);
	ifmedia_removeall(&scb->media);
	free(scb, M_EPAIR);
	CURVNET_RESTORE();

	ether_ifdetach(ifp);
	/*
	 * Wait for all packets to be dispatched to if_input.
	 */
	DPRINTF("sca refcnt=%u\n", sca->refcount);
	EPAIR_REFCOUNT_ASSERT(sca->refcount == 1,
	    ("%s: ifp=%p sca->refcount!=1: %d", __func__, ifp, sca->refcount));
	if_free(ifp);
	ifmedia_removeall(&sca->media);
	free(sca, M_EPAIR);
	ifc_free_unit(ifc, unit);

	return (0);
}

static void
vnet_epair_init(const void *unused __unused)
{

	V_epair_cloner = if_clone_advanced(epairname, 0,
	    epair_clone_match, epair_clone_create, epair_clone_destroy);
#ifdef VIMAGE
	netisr_register_vnet(&epair_nh);
#endif
}
VNET_SYSINIT(vnet_epair_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    vnet_epair_init, NULL);

static void
vnet_epair_uninit(const void *unused __unused)
{

#ifdef VIMAGE
	netisr_unregister_vnet(&epair_nh);
#endif
	if_clone_detach(V_epair_cloner);
}
VNET_SYSUNINIT(vnet_epair_uninit, SI_SUB_INIT_IF, SI_ORDER_ANY,
    vnet_epair_uninit, NULL);

static void
epair_uninit(const void *unused __unused)
{
	netisr_unregister(&epair_nh);
	epair_dpcpu_detach();
	if (bootverbose)
		printf("%s unloaded.\n", epairname);
}
SYSUNINIT(epair_uninit, SI_SUB_INIT_IF, SI_ORDER_MIDDLE,
    epair_uninit, NULL);

static int
epair_modevent(module_t mod, int type, void *data)
{
	int qlimit;

	switch (type) {
	case MOD_LOAD:
		/* For now limit us to one global mutex and one inq. */
		epair_dpcpu_init();
		epair_nh.nh_qlimit = 42 * ifqmaxlen; /* 42 shall be the number. */
		if (TUNABLE_INT_FETCH("net.link.epair.netisr_maxqlen", &qlimit))
		    epair_nh.nh_qlimit = qlimit;
		netisr_register(&epair_nh);
		if (bootverbose)
			printf("%s initialized.\n", epairname);
		break;
	case MOD_UNLOAD:
		/* Handled in epair_uninit() */
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t epair_mod = {
	"if_epair",
	epair_modevent,
	0
};

DECLARE_MODULE(if_epair, epair_mod, SI_SUB_PSEUDO, SI_ORDER_MIDDLE);
MODULE_VERSION(if_epair, 1);
