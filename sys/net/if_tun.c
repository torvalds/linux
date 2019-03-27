/*	$NetBSD: if_tun.c,v 1.14 1994/06/29 06:36:25 cgd Exp $	*/

/*-
 * Copyright (c) 1988, Julian Onions <jpo@cs.nott.ac.uk>
 * Nottingham University 1987.
 *
 * This source may be freely distributed, however I would be interested
 * in any changes that are made.
 *
 * This driver takes packets off the IP i/f and hands them up to a
 * user process to have its wicked way with. This driver has it's
 * roots in a similar driver written by Phil Cockcroft (formerly) at
 * UCL. This driver is based much more on read/write/poll mode of
 * operation though.
 *
 * $FreeBSD$
 */

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/jail.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/sockio.h>
#include <sys/ttycom.h>
#include <sys/poll.h>
#include <sys/selinfo.h>
#include <sys/signalvar.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/random.h>
#include <sys/ctype.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>
#include <net/if_types.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/vnet.h>
#ifdef INET
#include <netinet/in.h>
#endif
#include <net/bpf.h>
#include <net/if_tun.h>

#include <sys/queue.h>
#include <sys/condvar.h>

#include <security/mac/mac_framework.h>

/*
 * tun_list is protected by global tunmtx.  Other mutable fields are
 * protected by tun->tun_mtx, or by their owning subsystem.  tun_dev is
 * static for the duration of a tunnel interface.
 */
struct tun_softc {
	TAILQ_ENTRY(tun_softc)	tun_list;
	struct cdev *tun_dev;
	u_short	tun_flags;		/* misc flags */
#define	TUN_OPEN	0x0001
#define	TUN_INITED	0x0002
#define	TUN_RCOLL	0x0004
#define	TUN_IASET	0x0008
#define	TUN_DSTADDR	0x0010
#define	TUN_LMODE	0x0020
#define	TUN_RWAIT	0x0040
#define	TUN_ASYNC	0x0080
#define	TUN_IFHEAD	0x0100

#define TUN_READY       (TUN_OPEN | TUN_INITED)

	/*
	 * XXXRW: tun_pid is used to exclusively lock /dev/tun.  Is this
	 * actually needed?  Can we just return EBUSY if already open?
	 * Problem is that this involved inherent races when a tun device
	 * is handed off from one process to another, as opposed to just
	 * being slightly stale informationally.
	 */
	pid_t	tun_pid;		/* owning pid */
	struct	ifnet *tun_ifp;		/* the interface */
	struct  sigio *tun_sigio;	/* information for async I/O */
	struct	selinfo	tun_rsel;	/* read select */
	struct mtx	tun_mtx;	/* protect mutable softc fields */
	struct cv	tun_cv;		/* protect against ref'd dev destroy */
};
#define TUN2IFP(sc)	((sc)->tun_ifp)

#define TUNDEBUG	if (tundebug) if_printf

/*
 * All mutable global variables in if_tun are locked using tunmtx, with
 * the exception of tundebug, which is used unlocked, and tunclones,
 * which is static after setup.
 */
static struct mtx tunmtx;
static eventhandler_tag tag;
static const char tunname[] = "tun";
static MALLOC_DEFINE(M_TUN, tunname, "Tunnel Interface");
static int tundebug = 0;
static int tundclone = 1;
static struct clonedevs *tunclones;
static TAILQ_HEAD(,tun_softc)	tunhead = TAILQ_HEAD_INITIALIZER(tunhead);
SYSCTL_INT(_debug, OID_AUTO, if_tun_debug, CTLFLAG_RW, &tundebug, 0, "");

SYSCTL_DECL(_net_link);
static SYSCTL_NODE(_net_link, OID_AUTO, tun, CTLFLAG_RW, 0,
    "IP tunnel software network interface.");
SYSCTL_INT(_net_link_tun, OID_AUTO, devfs_cloning, CTLFLAG_RWTUN, &tundclone, 0,
    "Enable legacy devfs interface creation.");

static void	tunclone(void *arg, struct ucred *cred, char *name,
		    int namelen, struct cdev **dev);
static void	tuncreate(const char *name, struct cdev *dev);
static int	tunifioctl(struct ifnet *, u_long, caddr_t);
static void	tuninit(struct ifnet *);
static int	tunmodevent(module_t, int, void *);
static int	tunoutput(struct ifnet *, struct mbuf *,
		    const struct sockaddr *, struct route *ro);
static void	tunstart(struct ifnet *);

static int	tun_clone_match(struct if_clone *ifc, const char *name);
static int	tun_clone_create(struct if_clone *, char *, size_t, caddr_t);
static int	tun_clone_destroy(struct if_clone *, struct ifnet *);
static struct unrhdr	*tun_unrhdr;
VNET_DEFINE_STATIC(struct if_clone *, tun_cloner);
#define V_tun_cloner VNET(tun_cloner)

static d_open_t		tunopen;
static d_close_t	tunclose;
static d_read_t		tunread;
static d_write_t	tunwrite;
static d_ioctl_t	tunioctl;
static d_poll_t		tunpoll;
static d_kqfilter_t	tunkqfilter;

static int		tunkqread(struct knote *, long);
static int		tunkqwrite(struct knote *, long);
static void		tunkqdetach(struct knote *);

static struct filterops tun_read_filterops = {
	.f_isfd =	1,
	.f_attach =	NULL,
	.f_detach =	tunkqdetach,
	.f_event =	tunkqread,
};

static struct filterops tun_write_filterops = {
	.f_isfd =	1,
	.f_attach =	NULL,
	.f_detach =	tunkqdetach,
	.f_event =	tunkqwrite,
};

static struct cdevsw tun_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDMINOR,
	.d_open =	tunopen,
	.d_close =	tunclose,
	.d_read =	tunread,
	.d_write =	tunwrite,
	.d_ioctl =	tunioctl,
	.d_poll =	tunpoll,
	.d_kqfilter =	tunkqfilter,
	.d_name =	tunname,
};

static int
tun_clone_match(struct if_clone *ifc, const char *name)
{
	if (strncmp(tunname, name, 3) == 0 &&
	    (name[3] == '\0' || isdigit(name[3])))
		return (1);

	return (0);
}

static int
tun_clone_create(struct if_clone *ifc, char *name, size_t len, caddr_t params)
{
	struct cdev *dev;
	int err, unit, i;

	err = ifc_name2unit(name, &unit);
	if (err != 0)
		return (err);

	if (unit != -1) {
		/* If this unit number is still available that/s okay. */
		if (alloc_unr_specific(tun_unrhdr, unit) == -1)
			return (EEXIST);
	} else {
		unit = alloc_unr(tun_unrhdr);
	}

	snprintf(name, IFNAMSIZ, "%s%d", tunname, unit);

	/* find any existing device, or allocate new unit number */
	i = clone_create(&tunclones, &tun_cdevsw, &unit, &dev, 0);
	if (i) {
		/* No preexisting struct cdev *, create one */
		dev = make_dev(&tun_cdevsw, unit,
		    UID_UUCP, GID_DIALER, 0600, "%s%d", tunname, unit);
	}
	tuncreate(tunname, dev);

	return (0);
}

static void
tunclone(void *arg, struct ucred *cred, char *name, int namelen,
    struct cdev **dev)
{
	char devname[SPECNAMELEN + 1];
	int u, i, append_unit;

	if (*dev != NULL)
		return;

	/*
	 * If tun cloning is enabled, only the superuser can create an
	 * interface.
	 */
	if (!tundclone || priv_check_cred(cred, PRIV_NET_IFCREATE) != 0)
		return;

	if (strcmp(name, tunname) == 0) {
		u = -1;
	} else if (dev_stdclone(name, NULL, tunname, &u) != 1)
		return;	/* Don't recognise the name */
	if (u != -1 && u > IF_MAXUNIT)
		return;	/* Unit number too high */

	if (u == -1)
		append_unit = 1;
	else
		append_unit = 0;

	CURVNET_SET(CRED_TO_VNET(cred));
	/* find any existing device, or allocate new unit number */
	i = clone_create(&tunclones, &tun_cdevsw, &u, dev, 0);
	if (i) {
		if (append_unit) {
			namelen = snprintf(devname, sizeof(devname), "%s%d",
			    name, u);
			name = devname;
		}
		/* No preexisting struct cdev *, create one */
		*dev = make_dev_credf(MAKEDEV_REF, &tun_cdevsw, u, cred,
		    UID_UUCP, GID_DIALER, 0600, "%s", name);
	}

	if_clone_create(name, namelen, NULL);
	CURVNET_RESTORE();
}

static void
tun_destroy(struct tun_softc *tp)
{
	struct cdev *dev;

	mtx_lock(&tp->tun_mtx);
	if ((tp->tun_flags & TUN_OPEN) != 0)
		cv_wait_unlock(&tp->tun_cv, &tp->tun_mtx);
	else
		mtx_unlock(&tp->tun_mtx);

	CURVNET_SET(TUN2IFP(tp)->if_vnet);
	dev = tp->tun_dev;
	bpfdetach(TUN2IFP(tp));
	if_detach(TUN2IFP(tp));
	free_unr(tun_unrhdr, TUN2IFP(tp)->if_dunit);
	if_free(TUN2IFP(tp));
	destroy_dev(dev);
	seldrain(&tp->tun_rsel);
	knlist_clear(&tp->tun_rsel.si_note, 0);
	knlist_destroy(&tp->tun_rsel.si_note);
	mtx_destroy(&tp->tun_mtx);
	cv_destroy(&tp->tun_cv);
	free(tp, M_TUN);
	CURVNET_RESTORE();
}

static int
tun_clone_destroy(struct if_clone *ifc, struct ifnet *ifp)
{
	struct tun_softc *tp = ifp->if_softc;

	mtx_lock(&tunmtx);
	TAILQ_REMOVE(&tunhead, tp, tun_list);
	mtx_unlock(&tunmtx);
	tun_destroy(tp);

	return (0);
}

static void
vnet_tun_init(const void *unused __unused)
{
	V_tun_cloner = if_clone_advanced(tunname, 0, tun_clone_match,
			tun_clone_create, tun_clone_destroy);
}
VNET_SYSINIT(vnet_tun_init, SI_SUB_PROTO_IF, SI_ORDER_ANY,
		vnet_tun_init, NULL);

static void
vnet_tun_uninit(const void *unused __unused)
{
	if_clone_detach(V_tun_cloner);
}
VNET_SYSUNINIT(vnet_tun_uninit, SI_SUB_PROTO_IF, SI_ORDER_ANY,
    vnet_tun_uninit, NULL);

static void
tun_uninit(const void *unused __unused)
{
	struct tun_softc *tp;

	EVENTHANDLER_DEREGISTER(dev_clone, tag);
	drain_dev_clone_events();

	mtx_lock(&tunmtx);
	while ((tp = TAILQ_FIRST(&tunhead)) != NULL) {
		TAILQ_REMOVE(&tunhead, tp, tun_list);
		mtx_unlock(&tunmtx);
		tun_destroy(tp);
		mtx_lock(&tunmtx);
	}
	mtx_unlock(&tunmtx);
	delete_unrhdr(tun_unrhdr);
	clone_cleanup(&tunclones);
	mtx_destroy(&tunmtx);
}
SYSUNINIT(tun_uninit, SI_SUB_PROTO_IF, SI_ORDER_ANY, tun_uninit, NULL);

static int
tunmodevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		mtx_init(&tunmtx, "tunmtx", NULL, MTX_DEF);
		clone_setup(&tunclones);
		tun_unrhdr = new_unrhdr(0, IF_MAXUNIT, &tunmtx);
		tag = EVENTHANDLER_REGISTER(dev_clone, tunclone, 0, 1000);
		if (tag == NULL)
			return (ENOMEM);
		break;
	case MOD_UNLOAD:
		/* See tun_uninit, so it's done after the vnet_sysuninit() */
		break;
	default:
		return EOPNOTSUPP;
	}
	return 0;
}

static moduledata_t tun_mod = {
	"if_tun",
	tunmodevent,
	0
};

DECLARE_MODULE(if_tun, tun_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(if_tun, 1);

static void
tunstart(struct ifnet *ifp)
{
	struct tun_softc *tp = ifp->if_softc;
	struct mbuf *m;

	TUNDEBUG(ifp,"%s starting\n", ifp->if_xname);
	if (ALTQ_IS_ENABLED(&ifp->if_snd)) {
		IFQ_LOCK(&ifp->if_snd);
		IFQ_POLL_NOLOCK(&ifp->if_snd, m);
		if (m == NULL) {
			IFQ_UNLOCK(&ifp->if_snd);
			return;
		}
		IFQ_UNLOCK(&ifp->if_snd);
	}

	mtx_lock(&tp->tun_mtx);
	if (tp->tun_flags & TUN_RWAIT) {
		tp->tun_flags &= ~TUN_RWAIT;
		wakeup(tp);
	}
	selwakeuppri(&tp->tun_rsel, PZERO + 1);
	KNOTE_LOCKED(&tp->tun_rsel.si_note, 0);
	if (tp->tun_flags & TUN_ASYNC && tp->tun_sigio) {
		mtx_unlock(&tp->tun_mtx);
		pgsigio(&tp->tun_sigio, SIGIO, 0);
	} else
		mtx_unlock(&tp->tun_mtx);
}

/* XXX: should return an error code so it can fail. */
static void
tuncreate(const char *name, struct cdev *dev)
{
	struct tun_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_TUN, M_WAITOK | M_ZERO);
	mtx_init(&sc->tun_mtx, "tun_mtx", NULL, MTX_DEF);
	cv_init(&sc->tun_cv, "tun_condvar");
	sc->tun_flags = TUN_INITED;
	sc->tun_dev = dev;
	mtx_lock(&tunmtx);
	TAILQ_INSERT_TAIL(&tunhead, sc, tun_list);
	mtx_unlock(&tunmtx);

	ifp = sc->tun_ifp = if_alloc(IFT_PPP);
	if (ifp == NULL)
		panic("%s%d: failed to if_alloc() interface.\n",
		    name, dev2unit(dev));
	if_initname(ifp, name, dev2unit(dev));
	ifp->if_mtu = TUNMTU;
	ifp->if_ioctl = tunifioctl;
	ifp->if_output = tunoutput;
	ifp->if_start = tunstart;
	ifp->if_flags = IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_softc = sc;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = 0;
	IFQ_SET_READY(&ifp->if_snd);
	knlist_init_mtx(&sc->tun_rsel.si_note, &sc->tun_mtx);
	ifp->if_capabilities |= IFCAP_LINKSTATE;
	ifp->if_capenable |= IFCAP_LINKSTATE;

	if_attach(ifp);
	bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));
	dev->si_drv1 = sc;
	TUNDEBUG(ifp, "interface %s is created, minor = %#x\n",
	    ifp->if_xname, dev2unit(dev));
}

static int
tunopen(struct cdev *dev, int flag, int mode, struct thread *td)
{
	struct ifnet	*ifp;
	struct tun_softc *tp;

	/*
	 * XXXRW: Non-atomic test and set of dev->si_drv1 requires
	 * synchronization.
	 */
	tp = dev->si_drv1;
	if (!tp) {
		tuncreate(tunname, dev);
		tp = dev->si_drv1;
	}

	/*
	 * XXXRW: This use of tun_pid is subject to error due to the
	 * fact that a reference to the tunnel can live beyond the
	 * death of the process that created it.  Can we replace this
	 * with a simple busy flag?
	 */
	mtx_lock(&tp->tun_mtx);
	if (tp->tun_pid != 0 && tp->tun_pid != td->td_proc->p_pid) {
		mtx_unlock(&tp->tun_mtx);
		return (EBUSY);
	}
	tp->tun_pid = td->td_proc->p_pid;

	tp->tun_flags |= TUN_OPEN;
	ifp = TUN2IFP(tp);
	if_link_state_change(ifp, LINK_STATE_UP);
	TUNDEBUG(ifp, "open\n");
	mtx_unlock(&tp->tun_mtx);

	return (0);
}

/*
 * tunclose - close the device - mark i/f down & delete
 * routing info
 */
static	int
tunclose(struct cdev *dev, int foo, int bar, struct thread *td)
{
	struct tun_softc *tp;
	struct ifnet *ifp;

	tp = dev->si_drv1;
	ifp = TUN2IFP(tp);

	mtx_lock(&tp->tun_mtx);
	tp->tun_flags &= ~TUN_OPEN;
	tp->tun_pid = 0;

	/*
	 * junk all pending output
	 */
	CURVNET_SET(ifp->if_vnet);
	IFQ_PURGE(&ifp->if_snd);

	if (ifp->if_flags & IFF_UP) {
		mtx_unlock(&tp->tun_mtx);
		if_down(ifp);
		mtx_lock(&tp->tun_mtx);
	}

	/* Delete all addresses and routes which reference this interface. */
	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		struct ifaddr *ifa;

		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		mtx_unlock(&tp->tun_mtx);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			/* deal w/IPv4 PtP destination; unlocked read */
			if (ifa->ifa_addr->sa_family == AF_INET) {
				rtinit(ifa, (int)RTM_DELETE,
				    tp->tun_flags & TUN_DSTADDR ? RTF_HOST : 0);
			} else {
				rtinit(ifa, (int)RTM_DELETE, 0);
			}
		}
		if_purgeaddrs(ifp);
		mtx_lock(&tp->tun_mtx);
	}
	if_link_state_change(ifp, LINK_STATE_DOWN);
	CURVNET_RESTORE();

	funsetown(&tp->tun_sigio);
	selwakeuppri(&tp->tun_rsel, PZERO + 1);
	KNOTE_LOCKED(&tp->tun_rsel.si_note, 0);
	TUNDEBUG (ifp, "closed\n");

	cv_broadcast(&tp->tun_cv);
	mtx_unlock(&tp->tun_mtx);
	return (0);
}

static void
tuninit(struct ifnet *ifp)
{
	struct tun_softc *tp = ifp->if_softc;
#ifdef INET
	struct ifaddr *ifa;
#endif

	TUNDEBUG(ifp, "tuninit\n");

	mtx_lock(&tp->tun_mtx);
	ifp->if_flags |= IFF_UP;
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	getmicrotime(&ifp->if_lastchange);

#ifdef INET
	if_addr_rlock(ifp);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family == AF_INET) {
			struct sockaddr_in *si;

			si = (struct sockaddr_in *)ifa->ifa_addr;
			if (si->sin_addr.s_addr)
				tp->tun_flags |= TUN_IASET;

			si = (struct sockaddr_in *)ifa->ifa_dstaddr;
			if (si && si->sin_addr.s_addr)
				tp->tun_flags |= TUN_DSTADDR;
		}
	}
	if_addr_runlock(ifp);
#endif
	mtx_unlock(&tp->tun_mtx);
}

/*
 * Process an ioctl request.
 */
static int
tunifioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;
	struct tun_softc *tp = ifp->if_softc;
	struct ifstat *ifs;
	int		error = 0;

	switch(cmd) {
	case SIOCGIFSTATUS:
		ifs = (struct ifstat *)data;
		mtx_lock(&tp->tun_mtx);
		if (tp->tun_pid)
			snprintf(ifs->ascii, sizeof(ifs->ascii),
			    "\tOpened by PID %d\n", tp->tun_pid);
		else
			ifs->ascii[0] = '\0';
		mtx_unlock(&tp->tun_mtx);
		break;
	case SIOCSIFADDR:
		tuninit(ifp);
		TUNDEBUG(ifp, "address set\n");
		break;
	case SIOCSIFMTU:
		ifp->if_mtu = ifr->ifr_mtu;
		TUNDEBUG(ifp, "mtu set\n");
		break;
	case SIOCSIFFLAGS:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;
	default:
		error = EINVAL;
	}
	return (error);
}

/*
 * tunoutput - queue packets from higher level ready to put out.
 */
static int
tunoutput(struct ifnet *ifp, struct mbuf *m0, const struct sockaddr *dst,
    struct route *ro)
{
	struct tun_softc *tp = ifp->if_softc;
	u_short cached_tun_flags;
	int error;
	u_int32_t af;

	TUNDEBUG (ifp, "tunoutput\n");

#ifdef MAC
	error = mac_ifnet_check_transmit(ifp, m0);
	if (error) {
		m_freem(m0);
		return (error);
	}
#endif

	/* Could be unlocked read? */
	mtx_lock(&tp->tun_mtx);
	cached_tun_flags = tp->tun_flags;
	mtx_unlock(&tp->tun_mtx);
	if ((cached_tun_flags & TUN_READY) != TUN_READY) {
		TUNDEBUG (ifp, "not ready 0%o\n", tp->tun_flags);
		m_freem (m0);
		return (EHOSTDOWN);
	}

	if ((ifp->if_flags & IFF_UP) != IFF_UP) {
		m_freem (m0);
		return (EHOSTDOWN);
	}

	/* BPF writes need to be handled specially. */
	if (dst->sa_family == AF_UNSPEC)
		bcopy(dst->sa_data, &af, sizeof(af));
	else
		af = dst->sa_family;

	if (bpf_peers_present(ifp->if_bpf))
		bpf_mtap2(ifp->if_bpf, &af, sizeof(af), m0);

	/* prepend sockaddr? this may abort if the mbuf allocation fails */
	if (cached_tun_flags & TUN_LMODE) {
		/* allocate space for sockaddr */
		M_PREPEND(m0, dst->sa_len, M_NOWAIT);

		/* if allocation failed drop packet */
		if (m0 == NULL) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			return (ENOBUFS);
		} else {
			bcopy(dst, m0->m_data, dst->sa_len);
		}
	}

	if (cached_tun_flags & TUN_IFHEAD) {
		/* Prepend the address family */
		M_PREPEND(m0, 4, M_NOWAIT);

		/* if allocation failed drop packet */
		if (m0 == NULL) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			return (ENOBUFS);
		} else
			*(u_int32_t *)m0->m_data = htonl(af);
	} else {
#ifdef INET
		if (af != AF_INET)
#endif
		{
			m_freem(m0);
			return (EAFNOSUPPORT);
		}
	}

	error = (ifp->if_transmit)(ifp, m0);
	if (error)
		return (ENOBUFS);
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	return (0);
}

/*
 * the cdevsw interface is now pretty minimal.
 */
static	int
tunioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
	struct ifreq ifr;
	struct tun_softc *tp = dev->si_drv1;
	struct tuninfo *tunp;
	int error;

	switch (cmd) {
	case TUNSIFINFO:
		tunp = (struct tuninfo *)data;
		if (TUN2IFP(tp)->if_type != tunp->type)
			return (EPROTOTYPE);
		mtx_lock(&tp->tun_mtx);
		if (TUN2IFP(tp)->if_mtu != tunp->mtu) {
			strlcpy(ifr.ifr_name, if_name(TUN2IFP(tp)), IFNAMSIZ);
			ifr.ifr_mtu = tunp->mtu;
			CURVNET_SET(TUN2IFP(tp)->if_vnet);
			error = ifhwioctl(SIOCSIFMTU, TUN2IFP(tp),
			    (caddr_t)&ifr, td);
			CURVNET_RESTORE();
			if (error) {
				mtx_unlock(&tp->tun_mtx);
				return (error);
			}
		}
		TUN2IFP(tp)->if_baudrate = tunp->baudrate;
		mtx_unlock(&tp->tun_mtx);
		break;
	case TUNGIFINFO:
		tunp = (struct tuninfo *)data;
		mtx_lock(&tp->tun_mtx);
		tunp->mtu = TUN2IFP(tp)->if_mtu;
		tunp->type = TUN2IFP(tp)->if_type;
		tunp->baudrate = TUN2IFP(tp)->if_baudrate;
		mtx_unlock(&tp->tun_mtx);
		break;
	case TUNSDEBUG:
		tundebug = *(int *)data;
		break;
	case TUNGDEBUG:
		*(int *)data = tundebug;
		break;
	case TUNSLMODE:
		mtx_lock(&tp->tun_mtx);
		if (*(int *)data) {
			tp->tun_flags |= TUN_LMODE;
			tp->tun_flags &= ~TUN_IFHEAD;
		} else
			tp->tun_flags &= ~TUN_LMODE;
		mtx_unlock(&tp->tun_mtx);
		break;
	case TUNSIFHEAD:
		mtx_lock(&tp->tun_mtx);
		if (*(int *)data) {
			tp->tun_flags |= TUN_IFHEAD;
			tp->tun_flags &= ~TUN_LMODE;
		} else
			tp->tun_flags &= ~TUN_IFHEAD;
		mtx_unlock(&tp->tun_mtx);
		break;
	case TUNGIFHEAD:
		mtx_lock(&tp->tun_mtx);
		*(int *)data = (tp->tun_flags & TUN_IFHEAD) ? 1 : 0;
		mtx_unlock(&tp->tun_mtx);
		break;
	case TUNSIFMODE:
		/* deny this if UP */
		if (TUN2IFP(tp)->if_flags & IFF_UP)
			return(EBUSY);

		switch (*(int *)data & ~IFF_MULTICAST) {
		case IFF_POINTOPOINT:
		case IFF_BROADCAST:
			mtx_lock(&tp->tun_mtx);
			TUN2IFP(tp)->if_flags &=
			    ~(IFF_BROADCAST|IFF_POINTOPOINT|IFF_MULTICAST);
			TUN2IFP(tp)->if_flags |= *(int *)data;
			mtx_unlock(&tp->tun_mtx);
			break;
		default:
			return(EINVAL);
		}
		break;
	case TUNSIFPID:
		mtx_lock(&tp->tun_mtx);
		tp->tun_pid = curthread->td_proc->p_pid;
		mtx_unlock(&tp->tun_mtx);
		break;
	case FIONBIO:
		break;
	case FIOASYNC:
		mtx_lock(&tp->tun_mtx);
		if (*(int *)data)
			tp->tun_flags |= TUN_ASYNC;
		else
			tp->tun_flags &= ~TUN_ASYNC;
		mtx_unlock(&tp->tun_mtx);
		break;
	case FIONREAD:
		if (!IFQ_IS_EMPTY(&TUN2IFP(tp)->if_snd)) {
			struct mbuf *mb;
			IFQ_LOCK(&TUN2IFP(tp)->if_snd);
			IFQ_POLL_NOLOCK(&TUN2IFP(tp)->if_snd, mb);
			for (*(int *)data = 0; mb != NULL; mb = mb->m_next)
				*(int *)data += mb->m_len;
			IFQ_UNLOCK(&TUN2IFP(tp)->if_snd);
		} else
			*(int *)data = 0;
		break;
	case FIOSETOWN:
		return (fsetown(*(int *)data, &tp->tun_sigio));

	case FIOGETOWN:
		*(int *)data = fgetown(&tp->tun_sigio);
		return (0);

	/* This is deprecated, FIOSETOWN should be used instead. */
	case TIOCSPGRP:
		return (fsetown(-(*(int *)data), &tp->tun_sigio));

	/* This is deprecated, FIOGETOWN should be used instead. */
	case TIOCGPGRP:
		*(int *)data = -fgetown(&tp->tun_sigio);
		return (0);

	default:
		return (ENOTTY);
	}
	return (0);
}

/*
 * The cdevsw read interface - reads a packet at a time, or at
 * least as much of a packet as can be read.
 */
static	int
tunread(struct cdev *dev, struct uio *uio, int flag)
{
	struct tun_softc *tp = dev->si_drv1;
	struct ifnet	*ifp = TUN2IFP(tp);
	struct mbuf	*m;
	int		error=0, len;

	TUNDEBUG (ifp, "read\n");
	mtx_lock(&tp->tun_mtx);
	if ((tp->tun_flags & TUN_READY) != TUN_READY) {
		mtx_unlock(&tp->tun_mtx);
		TUNDEBUG (ifp, "not ready 0%o\n", tp->tun_flags);
		return (EHOSTDOWN);
	}

	tp->tun_flags &= ~TUN_RWAIT;

	do {
		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL) {
			if (flag & O_NONBLOCK) {
				mtx_unlock(&tp->tun_mtx);
				return (EWOULDBLOCK);
			}
			tp->tun_flags |= TUN_RWAIT;
			error = mtx_sleep(tp, &tp->tun_mtx, PCATCH | (PZERO + 1),
			    "tunread", 0);
			if (error != 0) {
				mtx_unlock(&tp->tun_mtx);
				return (error);
			}
		}
	} while (m == NULL);
	mtx_unlock(&tp->tun_mtx);

	while (m && uio->uio_resid > 0 && error == 0) {
		len = min(uio->uio_resid, m->m_len);
		if (len != 0)
			error = uiomove(mtod(m, void *), len, uio);
		m = m_free(m);
	}

	if (m) {
		TUNDEBUG(ifp, "Dropping mbuf\n");
		m_freem(m);
	}
	return (error);
}

/*
 * the cdevsw write interface - an atomic write is a packet - or else!
 */
static	int
tunwrite(struct cdev *dev, struct uio *uio, int flag)
{
	struct tun_softc *tp = dev->si_drv1;
	struct ifnet	*ifp = TUN2IFP(tp);
	struct mbuf	*m;
	uint32_t	family, mru;
	int 		isr;

	TUNDEBUG(ifp, "tunwrite\n");

	if ((ifp->if_flags & IFF_UP) != IFF_UP)
		/* ignore silently */
		return (0);

	if (uio->uio_resid == 0)
		return (0);

	mru = TUNMRU;
	if (tp->tun_flags & TUN_IFHEAD)
		mru += sizeof(family);
	if (uio->uio_resid < 0 || uio->uio_resid > mru) {
		TUNDEBUG(ifp, "len=%zd!\n", uio->uio_resid);
		return (EIO);
	}

	if ((m = m_uiotombuf(uio, M_NOWAIT, 0, 0, M_PKTHDR)) == NULL) {
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		return (ENOBUFS);
	}

	m->m_pkthdr.rcvif = ifp;
#ifdef MAC
	mac_ifnet_create_mbuf(ifp, m);
#endif

	/* Could be unlocked read? */
	mtx_lock(&tp->tun_mtx);
	if (tp->tun_flags & TUN_IFHEAD) {
		mtx_unlock(&tp->tun_mtx);
		if (m->m_len < sizeof(family) &&
		    (m = m_pullup(m, sizeof(family))) == NULL)
			return (ENOBUFS);
		family = ntohl(*mtod(m, u_int32_t *));
		m_adj(m, sizeof(family));
	} else {
		mtx_unlock(&tp->tun_mtx);
		family = AF_INET;
	}

	BPF_MTAP2(ifp, &family, sizeof(family), m);

	switch (family) {
#ifdef INET
	case AF_INET:
		isr = NETISR_IP;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		isr = NETISR_IPV6;
		break;
#endif
	default:
		m_freem(m);
		return (EAFNOSUPPORT);
	}
	random_harvest_queue(m, sizeof(*m), RANDOM_NET_TUN);
	if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	CURVNET_SET(ifp->if_vnet);
	M_SETFIB(m, ifp->if_fib);
	netisr_dispatch(isr, m);
	CURVNET_RESTORE();
	return (0);
}

/*
 * tunpoll - the poll interface, this is only useful on reads
 * really. The write detect always returns true, write never blocks
 * anyway, it either accepts the packet or drops it.
 */
static	int
tunpoll(struct cdev *dev, int events, struct thread *td)
{
	struct tun_softc *tp = dev->si_drv1;
	struct ifnet	*ifp = TUN2IFP(tp);
	int		revents = 0;
	struct mbuf	*m;

	TUNDEBUG(ifp, "tunpoll\n");

	if (events & (POLLIN | POLLRDNORM)) {
		IFQ_LOCK(&ifp->if_snd);
		IFQ_POLL_NOLOCK(&ifp->if_snd, m);
		if (m != NULL) {
			TUNDEBUG(ifp, "tunpoll q=%d\n", ifp->if_snd.ifq_len);
			revents |= events & (POLLIN | POLLRDNORM);
		} else {
			TUNDEBUG(ifp, "tunpoll waiting\n");
			selrecord(td, &tp->tun_rsel);
		}
		IFQ_UNLOCK(&ifp->if_snd);
	}
	if (events & (POLLOUT | POLLWRNORM))
		revents |= events & (POLLOUT | POLLWRNORM);

	return (revents);
}

/*
 * tunkqfilter - support for the kevent() system call.
 */
static int
tunkqfilter(struct cdev *dev, struct knote *kn)
{
	struct tun_softc	*tp = dev->si_drv1;
	struct ifnet	*ifp = TUN2IFP(tp);

	switch(kn->kn_filter) {
	case EVFILT_READ:
		TUNDEBUG(ifp, "%s kqfilter: EVFILT_READ, minor = %#x\n",
		    ifp->if_xname, dev2unit(dev));
		kn->kn_fop = &tun_read_filterops;
		break;

	case EVFILT_WRITE:
		TUNDEBUG(ifp, "%s kqfilter: EVFILT_WRITE, minor = %#x\n",
		    ifp->if_xname, dev2unit(dev));
		kn->kn_fop = &tun_write_filterops;
		break;

	default:
		TUNDEBUG(ifp, "%s kqfilter: invalid filter, minor = %#x\n",
		    ifp->if_xname, dev2unit(dev));
		return(EINVAL);
	}

	kn->kn_hook = tp;
	knlist_add(&tp->tun_rsel.si_note, kn, 0);

	return (0);
}

/*
 * Return true of there is data in the interface queue.
 */
static int
tunkqread(struct knote *kn, long hint)
{
	int			ret;
	struct tun_softc	*tp = kn->kn_hook;
	struct cdev		*dev = tp->tun_dev;
	struct ifnet	*ifp = TUN2IFP(tp);

	if ((kn->kn_data = ifp->if_snd.ifq_len) > 0) {
		TUNDEBUG(ifp,
		    "%s have data in the queue.  Len = %d, minor = %#x\n",
		    ifp->if_xname, ifp->if_snd.ifq_len, dev2unit(dev));
		ret = 1;
	} else {
		TUNDEBUG(ifp,
		    "%s waiting for data, minor = %#x\n", ifp->if_xname,
		    dev2unit(dev));
		ret = 0;
	}

	return (ret);
}

/*
 * Always can write, always return MTU in kn->data.
 */
static int
tunkqwrite(struct knote *kn, long hint)
{
	struct tun_softc	*tp = kn->kn_hook;
	struct ifnet	*ifp = TUN2IFP(tp);

	kn->kn_data = ifp->if_mtu;

	return (1);
}

static void
tunkqdetach(struct knote *kn)
{
	struct tun_softc	*tp = kn->kn_hook;

	knlist_remove(&tp->tun_rsel.si_note, kn, 0);
}
