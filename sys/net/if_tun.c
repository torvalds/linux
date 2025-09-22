/*	$OpenBSD: if_tun.c,v 1.252 2025/07/07 02:28:50 jsg Exp $	*/
/*	$NetBSD: if_tun.c,v 1.24 1996/05/07 02:40:48 thorpej Exp $	*/

/*
 * Copyright (c) 1988, Julian Onions <Julian.Onions@nexor.co.uk>
 * Nottingham University 1987.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This driver takes packets off the IP i/f and hands them up to a
 * user process to have its wicked way with. This driver has its
 * roots in a similar driver written by Phil Cockcroft (formerly) at
 * UCL. This driver is based much more on read/write/select mode of
 * operation though.
 */

/* #define	TUN_DEBUG	9 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/sigio.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/signalvar.h>
#include <sys/conf.h>
#include <sys/event.h>
#include <sys/mutex.h>
#include <sys/smr.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/rtable.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#ifdef MPLS
#include <netmpls/mpls.h>
#endif /* MPLS */

#include <net/if_tun.h>

struct tun_softc {
	struct arpcom		sc_ac;		/* ethernet common data */
#define sc_if			sc_ac.ac_if
	struct mutex		sc_mtx;
	struct klist		sc_rklist;	/* knotes for read */
	struct klist		sc_wklist;	/* knotes for write (unused) */
	SMR_LIST_ENTRY(tun_softc)
				sc_entry;	/* all tunnel interfaces */
	int			sc_unit;
	struct sigio_ref	sc_sigio;	/* async I/O registration */
	unsigned int		sc_flags;	/* misc flags */
#define TUN_DEAD			(1 << 16)
#define TUN_HDR				(1 << 17)

	dev_t			sc_dev;
	struct refcnt		sc_refs;
	unsigned int		sc_reading;
};

#ifdef	TUN_DEBUG
int	tundebug = TUN_DEBUG;
#define TUNDEBUG(a)	(tundebug? printf a : 0)
#else
#define TUNDEBUG(a)	/* (tundebug? printf a : 0) */
#endif

/* Pretend that these IFF flags are changeable by TUNSIFINFO */
#define TUN_IFF_FLAGS (IFF_POINTOPOINT|IFF_MULTICAST|IFF_BROADCAST)

#define TUN_IF_CAPS ( \
	IFCAP_CSUM_IPv4 | \
	IFCAP_CSUM_TCPv4|IFCAP_CSUM_UDPv4|IFCAP_CSUM_TCPv6|IFCAP_CSUM_UDPv6 | \
	IFCAP_VLAN_MTU|IFCAP_VLAN_HWTAGGING|IFCAP_VLAN_HWOFFLOAD | \
	IFCAP_TSOv4|IFCAP_TSOv6|IFCAP_LRO \
)

void	tunattach(int);

int	tun_dev_open(dev_t, const struct if_clone *, int, struct proc *);
int	tun_dev_close(dev_t, struct proc *);
int	tun_dev_ioctl(dev_t, u_long, void *);
int	tun_dev_read(dev_t, struct uio *, int);
int	tun_dev_write(dev_t, struct uio *, int, int);
int	tun_dev_kqfilter(dev_t, struct knote *);

int	tun_ioctl(struct ifnet *, u_long, caddr_t);
void	tun_input(struct ifnet *, struct mbuf *, struct netstack *);
int	tun_output(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *);
int	tun_enqueue(struct ifnet *, struct mbuf *);
int	tun_clone_create(struct if_clone *, int);
int	tap_clone_create(struct if_clone *, int);
int	tun_create(struct if_clone *, int, int);
int	tun_clone_destroy(struct ifnet *);
void	tun_wakeup(struct tun_softc *);
void	tun_start(struct ifnet *);
int	filt_tunread(struct knote *, long);
int	filt_tunwrite(struct knote *, long);
int	filt_tunmodify(struct kevent *, struct knote *);
int	filt_tunprocess(struct knote *, struct kevent *);
void	filt_tunrdetach(struct knote *);
void	filt_tunwdetach(struct knote *);
void	tun_link_state(struct ifnet *, int);

const struct filterops tunread_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_tunrdetach,
	.f_event	= filt_tunread,
	.f_modify	= filt_tunmodify,
	.f_process	= filt_tunprocess,
};

const struct filterops tunwrite_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_tunwdetach,
	.f_event	= filt_tunwrite,
	.f_modify	= filt_tunmodify,
	.f_process	= filt_tunprocess,
};

SMR_LIST_HEAD(tun_list, tun_softc);

struct if_clone tun_cloner =
    IF_CLONE_INITIALIZER("tun", tun_clone_create, tun_clone_destroy);

struct if_clone tap_cloner =
    IF_CLONE_INITIALIZER("tap", tap_clone_create, tun_clone_destroy);

void
tunattach(int n)
{
	if_clone_attach(&tun_cloner);
	if_clone_attach(&tap_cloner);
}

int
tun_clone_create(struct if_clone *ifc, int unit)
{
	return (tun_create(ifc, unit, 0));
}

int
tap_clone_create(struct if_clone *ifc, int unit)
{
	return (tun_create(ifc, unit, TUN_LAYER2));
}

struct tun_list tun_devs_list = SMR_LIST_HEAD_INITIALIZER(tun_list);

struct tun_softc *
tun_name_lookup(const char *name)
{
	struct tun_softc *sc;

	KERNEL_ASSERT_LOCKED();

	SMR_LIST_FOREACH_LOCKED(sc, &tun_devs_list, sc_entry) {
		if (strcmp(sc->sc_if.if_xname, name) == 0)
			return (sc);
	}

	return (NULL);
}

int
tun_insert(struct tun_softc *sc)
{
	int error = 0;

	/* check for a race */
	if (tun_name_lookup(sc->sc_if.if_xname) != NULL)
		error = EEXIST;
	else {
		/* tun_name_lookup checks for the right lock already */
		SMR_LIST_INSERT_HEAD_LOCKED(&tun_devs_list, sc, sc_entry);
	}

	return (error);
}

int
tun_create(struct if_clone *ifc, int unit, int flags)
{
	struct tun_softc	*sc;
	struct ifnet		*ifp;

	if (unit > minor(~0U))
		return (ENXIO);

	KERNEL_ASSERT_LOCKED();

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	refcnt_init(&sc->sc_refs);

	ifp = &sc->sc_if;
	snprintf(ifp->if_xname, sizeof(ifp->if_xname),
	    "%s%d", ifc->ifc_name, unit);
	mtx_init(&sc->sc_mtx, IPL_NET);
	klist_init_mutex(&sc->sc_rklist, &sc->sc_mtx);
	klist_init_mutex(&sc->sc_wklist, &sc->sc_mtx);
	ifp->if_softc = sc;

	/* this is enough state for tun_dev_open to work with */

	if (tun_insert(sc) != 0)
		goto exists;

	/* build the interface */

	ifp->if_ioctl = tun_ioctl;
	ifp->if_enqueue = tun_enqueue;
	ifp->if_start = tun_start;
	ifp->if_hardmtu = TUNMRU;
	ifp->if_link_state = LINK_STATE_DOWN;

	if_counters_alloc(ifp);

	if ((flags & TUN_LAYER2) == 0) {
#if NBPFILTER > 0
		ifp->if_bpf_mtap = bpf_mtap;
#endif
		ifp->if_input = tun_input;
		ifp->if_output = tun_output;
		ifp->if_mtu = ETHERMTU;
		ifp->if_flags = (IFF_POINTOPOINT|IFF_MULTICAST);
		ifp->if_type = IFT_TUNNEL;
		ifp->if_hdrlen = sizeof(u_int32_t);
		ifp->if_rtrequest = p2p_rtrequest;

		if_attach(ifp);
		if_alloc_sadl(ifp);

#if NBPFILTER > 0
		bpfattach(&ifp->if_bpf, ifp, DLT_LOOP, sizeof(u_int32_t));
#endif
	} else {
		sc->sc_flags |= TUN_LAYER2;
		ether_fakeaddr(ifp);
		ifp->if_flags =
		    (IFF_BROADCAST|IFF_SIMPLEX|IFF_MULTICAST);

		if_attach(ifp);
		ether_ifattach(ifp);
	}

	sigio_init(&sc->sc_sigio);

	/* tell tun_dev_open we're initialised */

	sc->sc_flags |= TUN_INITED|TUN_STAYUP;
	wakeup(sc);

	return (0);

exists:
	klist_free(&sc->sc_rklist);
	klist_free(&sc->sc_wklist);
	free(sc, M_DEVBUF, sizeof(*sc));
	return (EEXIST);
}

int
tun_clone_destroy(struct ifnet *ifp)
{
	struct tun_softc	*sc = ifp->if_softc;
	dev_t			 dev;

	KERNEL_ASSERT_LOCKED();

	if (ISSET(sc->sc_flags, TUN_DEAD))
		return (ENXIO);
	SET(sc->sc_flags, TUN_DEAD);

	/* kick userland off the device */
	dev = sc->sc_dev;
	if (dev) {
		struct vnode *vp;

		if (vfinddev(dev, VCHR, &vp))
			VOP_REVOKE(vp, REVOKEALL);

		KASSERT(sc->sc_dev == 0);
	}

	/* prevent userland from getting to the device again */
	SMR_LIST_REMOVE_LOCKED(sc, sc_entry);
	smr_barrier();

	/* help read() give up */
	if (sc->sc_reading)
		wakeup(&ifp->if_snd);

	/* wait for device entrypoints to finish */
	refcnt_finalize(&sc->sc_refs, "tundtor");

	klist_invalidate(&sc->sc_rklist);
	klist_invalidate(&sc->sc_wklist);

	klist_free(&sc->sc_rklist);
	klist_free(&sc->sc_wklist);

	if (ISSET(sc->sc_flags, TUN_LAYER2))
		ether_ifdetach(ifp);

	if_detach(ifp);
	sigio_free(&sc->sc_sigio);

	free(sc, M_DEVBUF, sizeof *sc);
	return (0);
}

static struct tun_softc *
tun_get(dev_t dev)
{
	struct tun_softc *sc;

	smr_read_enter();
	SMR_LIST_FOREACH(sc, &tun_devs_list, sc_entry) {
		if (sc->sc_dev == dev) {
			refcnt_take(&sc->sc_refs);
			break;
		}
	}
	smr_read_leave();

	return (sc);
}

static inline void
tun_put(struct tun_softc *sc)
{
	refcnt_rele_wake(&sc->sc_refs);
}

int
tunopen(dev_t dev, int flag, int mode, struct proc *p)
{
	return (tun_dev_open(dev, &tun_cloner, mode, p));
}

int
tapopen(dev_t dev, int flag, int mode, struct proc *p)
{
	return (tun_dev_open(dev, &tap_cloner, mode, p));
}

int
tun_dev_open(dev_t dev, const struct if_clone *ifc, int mode, struct proc *p)
{
	struct tun_softc *sc;
	struct ifnet *ifp;
	int error;
	u_short stayup = 0;
	struct vnode *vp;

	char name[IFNAMSIZ];
	unsigned int rdomain;

	/*
	 * Find the vnode associated with this open before we sleep
	 * and let something else revoke it. Our caller has a reference
	 * to it so we don't need to account for it.
	 */
	if (!vfinddev(dev, VCHR, &vp))
		panic("%s vfinddev failed", __func__);

	snprintf(name, sizeof(name), "%s%u", ifc->ifc_name, minor(dev));
	rdomain = rtable_l2(p->p_p->ps_rtableid);

	/* let's find or make an interface to work with */
	while ((sc = tun_name_lookup(name)) == NULL) {
		error = if_clone_create(name, rdomain);
		switch (error) {
		case 0: /* it's probably ours */
			stayup = TUN_STAYUP;
			/* FALLTHROUGH */
		case EEXIST: /* we may have lost a race with someone else */
			break;
		default:
			return (error);
		}
	}

	refcnt_take(&sc->sc_refs);

	/* wait for it to be fully constructed before we use it */
	for (;;) {
		if (ISSET(sc->sc_flags, TUN_DEAD)) {
			error = ENXIO;
			goto done;
		}

		if (ISSET(sc->sc_flags, TUN_INITED))
			break;

		error = tsleep_nsec(sc, PCATCH, "tuninit", INFSLP);
		if (error != 0) {
			/* XXX if_clone_destroy if stayup? */
			goto done;
		}
	}

	/* Has tun_clone_destroy torn the rug out under us? */
	if (vp->v_type == VBAD) {
		error = ENXIO;
		goto done;
	}

	if (sc->sc_dev != 0) {
		/* aww, we lost */
		error = EBUSY;
		goto done;
	}
	/* it's ours now */
	sc->sc_dev = dev;
	CLR(sc->sc_flags, stayup);

	/* automatically mark the interface running on open */
	ifp = &sc->sc_if;
	NET_LOCK();
	SET(ifp->if_flags, IFF_UP | IFF_RUNNING);
	NET_UNLOCK();
	tun_link_state(ifp, LINK_STATE_FULL_DUPLEX);
	error = 0;

done:
	tun_put(sc);
	return (error);
}

/*
 * tunclose - close the device; if closing the real device, flush pending
 *  output and unless STAYUP bring down and destroy the interface.
 */
int
tunclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return (tun_dev_close(dev, p));
}

int
tapclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return (tun_dev_close(dev, p));
}

int
tun_dev_close(dev_t dev, struct proc *p)
{
	struct tun_softc	*sc;
	struct ifnet		*ifp;
	int			 error = 0;
	char			 name[IFNAMSIZ];
	int			 destroy = 0;

	sc = tun_get(dev);
	if (sc == NULL)
		return (ENXIO);

	ifp = &sc->sc_if;

	/*
	 * junk all pending output
	 */
	NET_LOCK();
	CLR(ifp->if_flags, IFF_UP | IFF_RUNNING);
	CLR(ifp->if_capabilities, TUN_IF_CAPS);
	NET_UNLOCK();
	ifq_purge(&ifp->if_snd);

	CLR(sc->sc_flags, TUN_ASYNC|TUN_HDR);
	sigio_free(&sc->sc_sigio);

	if (!ISSET(sc->sc_flags, TUN_DEAD)) {
		/* we can't hold a reference to sc before we start a dtor */
		if (!ISSET(sc->sc_flags, TUN_STAYUP)) {
			destroy = 1;
			strlcpy(name, ifp->if_xname, sizeof(name));
		} else {
			tun_link_state(ifp, LINK_STATE_DOWN);
		}
	}

	sc->sc_dev = 0;

	tun_put(sc);

	if (destroy)
		if_clone_destroy(name);

	return (error);
}

/*
 * Process an ioctl request.
 */
int
tun_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct tun_softc	*sc = (struct tun_softc *)(ifp->if_softc);
	struct ifreq		*ifr = (struct ifreq *)data;
	int			 error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		SET(ifp->if_flags, IFF_UP);
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP))
			SET(ifp->if_flags, IFF_RUNNING);
		else
			CLR(ifp->if_flags, IFF_RUNNING);
		break;

	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > TUNMRU)
			error = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;
	default:
		if (sc->sc_flags & TUN_LAYER2)
			error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		else
			error = ENOTTY;
	}

	return (error);
}

/*
 * tun_output - queue packets from higher level ready to put out.
 */
int
tun_output(struct ifnet *ifp, struct mbuf *m0, struct sockaddr *dst,
    struct rtentry *rt)
{
	u_int32_t		*af;

	if (!ISSET(ifp->if_flags, IFF_RUNNING)) {
		m_freem(m0);
		return (EHOSTDOWN);
	}

	M_PREPEND(m0, sizeof(*af), M_DONTWAIT);
	if (m0 == NULL)
		return (ENOBUFS);
	af = mtod(m0, u_int32_t *);
	*af = htonl(dst->sa_family);

	return (if_enqueue(ifp, m0));
}

int
tun_enqueue(struct ifnet *ifp, struct mbuf *m0)
{
	struct tun_softc	*sc = ifp->if_softc;
	int			 error;

	error = ifq_enqueue(&ifp->if_snd, m0);
	if (error != 0)
		return (error);

	tun_wakeup(sc);

	return (0);
}

void
tun_wakeup(struct tun_softc *sc)
{
	if (sc->sc_reading)
		wakeup(&sc->sc_if.if_snd);

	knote(&sc->sc_rklist, 0);

	if (sc->sc_flags & TUN_ASYNC)
		pgsigio(&sc->sc_sigio, SIGIO, 0);
}

/*
 * the cdevsw interface is now pretty minimal.
 */
int
tunioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return (tun_dev_ioctl(dev, cmd, data));
}

int
tapioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return (tun_dev_ioctl(dev, cmd, data));
}

static int
tun_set_capabilities(struct tun_softc *sc, const struct tun_capabilities *cap)
{
	if (ISSET(cap->tun_if_capabilities, ~TUN_IF_CAPS))
		return (EINVAL);

	KERNEL_ASSERT_LOCKED();
	SET(sc->sc_flags, TUN_HDR);

	NET_LOCK();
	CLR(sc->sc_if.if_capabilities, TUN_IF_CAPS);
	SET(sc->sc_if.if_capabilities, cap->tun_if_capabilities);
	NET_UNLOCK();
	return (0);
}

static int
tun_get_capabilities(struct tun_softc *sc, struct tun_capabilities *cap)
{
	int error = 0;

	NET_LOCK_SHARED();
	if (ISSET(sc->sc_flags, TUN_HDR)) {
		cap->tun_if_capabilities =
		    (sc->sc_if.if_capabilities & TUN_IF_CAPS);
	} else
		error = ENODEV;
	NET_UNLOCK_SHARED();

	return (error);
}

static int
tun_del_capabilities(struct tun_softc *sc)
{
	NET_LOCK();
	CLR(sc->sc_if.if_capabilities, TUN_IF_CAPS);
	NET_UNLOCK();

	KERNEL_ASSERT_LOCKED();
	CLR(sc->sc_flags, TUN_HDR);

	return (0);
}

static int
tun_hdatalen(struct tun_softc *sc)
{
	struct ifnet		*ifp = &sc->sc_if;
	int			 len;

	len = ifq_hdatalen(&ifp->if_snd);
	if (len > 0 && ISSET(sc->sc_flags, TUN_HDR))
		len += sizeof(struct tun_hdr);

	return (len);
}

int
tun_dev_ioctl(dev_t dev, u_long cmd, void *data)
{
	struct tun_softc	*sc;
	struct tuninfo		*tunp;
	int			 error = 0;

	sc = tun_get(dev);
	if (sc == NULL)
		return (ENXIO);

	switch (cmd) {
	case TUNSIFINFO:
		tunp = (struct tuninfo *)data;
		if (tunp->mtu < ETHERMIN || tunp->mtu > TUNMRU) {
			error = EINVAL;
			break;
		}
		if (tunp->type != sc->sc_if.if_type) {
			error = EINVAL;
			break;
		}
		if (tunp->flags != (sc->sc_if.if_flags & TUN_IFF_FLAGS)) {
			error = EINVAL;
			break;
		}
		sc->sc_if.if_mtu = tunp->mtu;
		sc->sc_if.if_baudrate = tunp->baudrate;
		break;
	case TUNGIFINFO:
		tunp = (struct tuninfo *)data;
		tunp->mtu = sc->sc_if.if_mtu;
		tunp->type = sc->sc_if.if_type;
		tunp->flags = sc->sc_if.if_flags & TUN_IFF_FLAGS;
		tunp->baudrate = sc->sc_if.if_baudrate;
		break;
#ifdef TUN_DEBUG
	case TUNSDEBUG:
		tundebug = *(int *)data;
		break;
	case TUNGDEBUG:
		*(int *)data = tundebug;
		break;
#endif
	case TUNSIFMODE:
		if (*(int *)data != (sc->sc_if.if_flags & TUN_IFF_FLAGS)) {
			error = EINVAL;
			break;
		}
		break;

	case TUNSCAP:
		error = tun_set_capabilities(sc,
		    (const struct tun_capabilities *)data);
		break;
	case TUNGCAP:
		error = tun_get_capabilities(sc,
		    (struct tun_capabilities *)data);
		break;
	case TUNDCAP:
		error = tun_del_capabilities(sc);
		break;

	case FIOASYNC:
		if (*(int *)data)
			sc->sc_flags |= TUN_ASYNC;
		else
			sc->sc_flags &= ~TUN_ASYNC;
		break;
	case FIONREAD:
		*(int *)data = tun_hdatalen(sc);
		break;
	case FIOSETOWN:
	case TIOCSPGRP:
		error = sigio_setown(&sc->sc_sigio, cmd, data);
		break;
	case FIOGETOWN:
	case TIOCGPGRP:
		sigio_getown(&sc->sc_sigio, cmd, data);
		break;
	case SIOCGIFADDR:
		if (!(sc->sc_flags & TUN_LAYER2)) {
			error = EINVAL;
			break;
		}
		bcopy(sc->sc_ac.ac_enaddr, data,
		    sizeof(sc->sc_ac.ac_enaddr));
		break;

	case SIOCSIFADDR:
		if (!(sc->sc_flags & TUN_LAYER2)) {
			error = EINVAL;
			break;
		}
		bcopy(data, sc->sc_ac.ac_enaddr,
		    sizeof(sc->sc_ac.ac_enaddr));
		break;
	default:
		error = ENOTTY;
		break;
	}

	tun_put(sc);
	return (error);
}

/*
 * The cdevsw read interface - reads a packet at a time, or at
 * least as much of a packet as can be read.
 */
int
tunread(dev_t dev, struct uio *uio, int ioflag)
{
	return (tun_dev_read(dev, uio, ioflag));
}

int
tapread(dev_t dev, struct uio *uio, int ioflag)
{
	return (tun_dev_read(dev, uio, ioflag));
}

int
tun_dev_read(dev_t dev, struct uio *uio, int ioflag)
{
	struct tun_softc	*sc;
	struct ifnet		*ifp;
	struct mbuf		*m, *m0;
	size_t			 len;
	int			 error = 0;

	sc = tun_get(dev);
	if (sc == NULL)
		return (ENXIO);

	ifp = &sc->sc_if;

	error = ifq_deq_sleep(&ifp->if_snd, &m0, ISSET(ioflag, IO_NDELAY),
	    (PZERO + 1)|PCATCH, "tunread", &sc->sc_reading, &sc->sc_dev);
	if (error != 0)
		goto put;

#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif

	if (ISSET(sc->sc_flags, TUN_HDR)) {
		struct tun_hdr th;

		KASSERT(ISSET(m0->m_flags, M_PKTHDR));

		th.th_flags = 0;
		if (ISSET(m0->m_pkthdr.csum_flags, M_IPV4_CSUM_OUT))
			SET(th.th_flags, TUN_H_IPV4_CSUM);
		if (ISSET(m0->m_pkthdr.csum_flags, M_TCP_CSUM_OUT))
			SET(th.th_flags, TUN_H_TCP_CSUM);
		if (ISSET(m0->m_pkthdr.csum_flags, M_UDP_CSUM_OUT))
			SET(th.th_flags, TUN_H_UDP_CSUM);
		if (ISSET(m0->m_pkthdr.csum_flags, M_ICMP_CSUM_OUT))
			SET(th.th_flags, TUN_H_ICMP_CSUM);

		th.th_pad = 0;

		th.th_vtag = 0;
		if (ISSET(m0->m_flags, M_VLANTAG)) {
			SET(th.th_flags, TUN_H_VTAG);
			th.th_vtag = m0->m_pkthdr.ether_vtag;
		}

		th.th_mss = 0;
		if (ISSET(m0->m_pkthdr.csum_flags, M_TCP_TSO)) {
			SET(th.th_flags, TUN_H_TCP_MSS);
			th.th_mss = m0->m_pkthdr.ph_mss;
		}

		len = ulmin(uio->uio_resid, sizeof(th));
		if (len > 0) {
			error = uiomove(&th, len, uio);
			if (error != 0)
				goto free;
		}
	}

	m = m0;
	while (uio->uio_resid > 0) {
		len = ulmin(uio->uio_resid, m->m_len);
		if (len > 0) {
			error = uiomove(mtod(m, void *), len, uio);
			if (error != 0)
				break;
		}

		m = m->m_next;
		if (m == NULL)
			break;
	}

free:
	m_freem(m0);

put:
	tun_put(sc);
	return (error);
}

/*
 * the cdevsw write interface - an atomic write is a packet - or else!
 */
int
tunwrite(dev_t dev, struct uio *uio, int ioflag)
{
	return (tun_dev_write(dev, uio, ioflag, 0));
}

int
tapwrite(dev_t dev, struct uio *uio, int ioflag)
{
	return (tun_dev_write(dev, uio, ioflag, ETHER_ALIGN));
}

int
tun_dev_write(dev_t dev, struct uio *uio, int ioflag, int align)
{
	struct tun_softc	*sc;
	struct ifnet		*ifp;
	struct mbuf		*m0, *m, *n;
	int			error = 0;
	size_t			len, alen, mlen;
	size_t			hlen;
	struct tun_hdr		th;

	sc = tun_get(dev);
	if (sc == NULL)
		return (ENXIO);

	ifp = &sc->sc_if;

	hlen = ifp->if_hdrlen;
	if (ISSET(sc->sc_flags, TUN_HDR))
		hlen += sizeof(th);
	if (uio->uio_resid < hlen ||
	    uio->uio_resid > (hlen + MAXMCLBYTES)) {
		error = EMSGSIZE;
		goto put;
	}

	m0 = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m0 == NULL) {
		error = ENOMEM;
		goto put;
	}

	if (ISSET(sc->sc_flags, TUN_HDR)) {
		error = uiomove(&th, sizeof(th), uio);
		if (error != 0)
			goto drop;

		if (ISSET(th.th_flags, TUN_H_IPV4_CSUM)) {
			SET(m0->m_pkthdr.csum_flags,
			    M_IPV4_CSUM_OUT | M_IPV4_CSUM_IN_OK);
		}

		switch (th.th_flags &
		    (TUN_H_TCP_CSUM|TUN_H_UDP_CSUM|TUN_H_ICMP_CSUM)) {
		case 0:
			break;
		case TUN_H_TCP_CSUM:
			SET(m0->m_pkthdr.csum_flags,
			    M_TCP_CSUM_OUT | M_TCP_CSUM_IN_OK);
			break;
		case TUN_H_UDP_CSUM:
			SET(m0->m_pkthdr.csum_flags,
			    M_UDP_CSUM_OUT | M_UDP_CSUM_IN_OK);
			break;
		case TUN_H_ICMP_CSUM:
			SET(m0->m_pkthdr.csum_flags,
			    M_ICMP_CSUM_OUT | M_ICMP_CSUM_IN_OK);
			break;
		default:
			error = EINVAL;
			goto drop;
		}

		if (ISSET(th.th_flags, TUN_H_VTAG)) {
			if (!ISSET(sc->sc_flags, TUN_LAYER2)) {
				error = EINVAL;
				goto drop;
			}
			SET(m0->m_flags, M_VLANTAG);
			m0->m_pkthdr.ether_vtag = th.th_vtag;
		}

		if (ISSET(th.th_flags, TUN_H_TCP_MSS)) {
			SET(m0->m_pkthdr.csum_flags, M_TCP_TSO);
			m0->m_pkthdr.ph_mss = th.th_mss;
		}
	}

	align += roundup(max_linkhdr, sizeof(long));
	mlen = MHLEN; /* how much space in the mbuf */

	len = uio->uio_resid;
	m0->m_pkthdr.len = len;

	m = m0;
	for (;;) {
		alen = align + len; /* what we want to put in this mbuf */
		if (alen > mlen) {
			if (alen > MAXMCLBYTES)
				alen = MAXMCLBYTES;
			m_clget(m, M_DONTWAIT, alen);
			if (!ISSET(m->m_flags, M_EXT)) {
				error = ENOMEM;
				goto put;
			}
		}

		m->m_len = alen;
		if (align > 0) {
			/* avoid m_adj to protect m0->m_pkthdr.len */
			m->m_data += align;
			m->m_len -= align;
		}

		error = uiomove(mtod(m, void *), m->m_len, uio);
		if (error != 0)
			goto drop;

		len = uio->uio_resid;
		if (len == 0)
			break;

		n = m_get(M_DONTWAIT, MT_DATA);
		if (n == NULL) {
			error = ENOMEM;
			goto put;
		}

		align = 0;
		mlen = MLEN;

		m->m_next = n;
		m = n;
	}

	NET_LOCK();
	if_vinput(ifp, m0, NULL);
	NET_UNLOCK();

	tun_put(sc);
	return (0);

drop:
	m_freem(m0);
put:
	tun_put(sc);
	return (error);
}

void
tun_input(struct ifnet *ifp, struct mbuf *m0, struct netstack *ns)
{
	uint32_t		af;

	KASSERT(m0->m_len >= sizeof(af));

	af = *mtod(m0, uint32_t *);
	/* strip the tunnel header */
	m_adj(m0, sizeof(af));

	switch (ntohl(af)) {
	case AF_INET:
		ipv4_input(ifp, m0, ns);
		break;
#ifdef INET6
	case AF_INET6:
		ipv6_input(ifp, m0, ns);
		break;
#endif
#ifdef MPLS
	case AF_MPLS:
		mpls_input(ifp, m0, ns);
		break;
#endif
	default:
		m_freem(m0);
		break;
	}
}

int
tunkqfilter(dev_t dev, struct knote *kn)
{
	return (tun_dev_kqfilter(dev, kn));
}

int
tapkqfilter(dev_t dev, struct knote *kn)
{
	return (tun_dev_kqfilter(dev, kn));
}

int
tun_dev_kqfilter(dev_t dev, struct knote *kn)
{
	struct tun_softc	*sc;
	struct klist		*klist;
	int			 error = 0;

	sc = tun_get(dev);
	if (sc == NULL)
		return (ENXIO);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		klist = &sc->sc_rklist;
		kn->kn_fop = &tunread_filtops;
		break;
	case EVFILT_WRITE:
		klist = &sc->sc_wklist;
		kn->kn_fop = &tunwrite_filtops;
		break;
	default:
		error = EINVAL;
		goto put;
	}

	kn->kn_hook = sc;

	klist_insert(klist, kn);

put:
	tun_put(sc);
	return (error);
}

void
filt_tunrdetach(struct knote *kn)
{
	struct tun_softc	*sc = kn->kn_hook;

	klist_remove(&sc->sc_rklist, kn);
}

int
filt_tunread(struct knote *kn, long hint)
{
	struct tun_softc	*sc = kn->kn_hook;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	kn->kn_data = tun_hdatalen(sc);

	return (kn->kn_data > 0);
}

void
filt_tunwdetach(struct knote *kn)
{
	struct tun_softc	*sc = kn->kn_hook;

	klist_remove(&sc->sc_wklist, kn);
}

int
filt_tunwrite(struct knote *kn, long hint)
{
	struct tun_softc	*sc = kn->kn_hook;
	struct ifnet		*ifp = &sc->sc_if;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	kn->kn_data = ifp->if_hdrlen + ifp->if_hardmtu;

	return (1);
}

int
filt_tunmodify(struct kevent *kev, struct knote *kn)
{
	struct tun_softc	*sc = kn->kn_hook;
	int			 active;

	mtx_enter(&sc->sc_mtx);
	active = knote_modify(kev, kn);
	mtx_leave(&sc->sc_mtx);

	return (active);
}

int
filt_tunprocess(struct knote *kn, struct kevent *kev)
{
	struct tun_softc	*sc = kn->kn_hook;
	int			 active;

	mtx_enter(&sc->sc_mtx);
	active = knote_process(kn, kev);
	mtx_leave(&sc->sc_mtx);

	return (active);
}

void
tun_start(struct ifnet *ifp)
{
	struct tun_softc	*sc = ifp->if_softc;

	splassert(IPL_NET);

	if (ifq_len(&ifp->if_snd))
		tun_wakeup(sc);
}

void
tun_link_state(struct ifnet *ifp, int link_state)
{
	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}
