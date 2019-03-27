/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1999-2000 by Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * BASED ON:
 * -------------------------------------------------------------------------
 *
 * Copyright (c) 1988, Julian Onions <jpo@cs.nott.ac.uk>
 * Nottingham University 1987.
 */

/*
 * $FreeBSD$
 * $Id: if_tap.c,v 0.21 2000/07/23 21:46:02 max Exp $
 */

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/poll.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/selinfo.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/ttycom.h>
#include <sys/uio.h>
#include <sys/queue.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>

#include <net/if_tapvar.h>
#include <net/if_tap.h>

#define CDEV_NAME	"tap"
#define TAPDEBUG	if (tapdebug) printf

static const char tapname[] = "tap";
static const char vmnetname[] = "vmnet";
#define TAPMAXUNIT	0x7fff
#define VMNET_DEV_MASK	CLONE_FLAG0

/* module */
static int		tapmodevent(module_t, int, void *);

/* device */
static void		tapclone(void *, struct ucred *, char *, int,
			    struct cdev **);
static void		tapcreate(struct cdev *);

/* network interface */
static void		tapifstart(struct ifnet *);
static int		tapifioctl(struct ifnet *, u_long, caddr_t);
static void		tapifinit(void *);

static int		tap_clone_create(struct if_clone *, int, caddr_t);
static void		tap_clone_destroy(struct ifnet *);
static struct if_clone *tap_cloner;
static int		vmnet_clone_create(struct if_clone *, int, caddr_t);
static void		vmnet_clone_destroy(struct ifnet *);
static struct if_clone *vmnet_cloner;

/* character device */
static d_open_t		tapopen;
static d_close_t	tapclose;
static d_read_t		tapread;
static d_write_t	tapwrite;
static d_ioctl_t	tapioctl;
static d_poll_t		tappoll;
static d_kqfilter_t	tapkqfilter;

/* kqueue(2) */
static int		tapkqread(struct knote *, long);
static int		tapkqwrite(struct knote *, long);
static void		tapkqdetach(struct knote *);

static struct filterops	tap_read_filterops = {
	.f_isfd =	1,
	.f_attach =	NULL,
	.f_detach =	tapkqdetach,
	.f_event =	tapkqread,
};

static struct filterops	tap_write_filterops = {
	.f_isfd =	1,
	.f_attach =	NULL,
	.f_detach =	tapkqdetach,
	.f_event =	tapkqwrite,
};

static struct cdevsw	tap_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDMINOR,
	.d_open =	tapopen,
	.d_close =	tapclose,
	.d_read =	tapread,
	.d_write =	tapwrite,
	.d_ioctl =	tapioctl,
	.d_poll =	tappoll,
	.d_name =	CDEV_NAME,
	.d_kqfilter =	tapkqfilter,
};

/*
 * All global variables in if_tap.c are locked with tapmtx, with the
 * exception of tapdebug, which is accessed unlocked; tapclones is
 * static at runtime.
 */
static struct mtx		tapmtx;
static int			tapdebug = 0;        /* debug flag   */
static int			tapuopen = 0;        /* allow user open() */
static int			tapuponopen = 0;    /* IFF_UP on open() */
static int			tapdclone = 1;	/* enable devfs cloning */
static SLIST_HEAD(, tap_softc)	taphead;             /* first device */
static struct clonedevs 	*tapclones;

MALLOC_DECLARE(M_TAP);
MALLOC_DEFINE(M_TAP, CDEV_NAME, "Ethernet tunnel interface");
SYSCTL_INT(_debug, OID_AUTO, if_tap_debug, CTLFLAG_RW, &tapdebug, 0, "");

SYSCTL_DECL(_net_link);
static SYSCTL_NODE(_net_link, OID_AUTO, tap, CTLFLAG_RW, 0,
    "Ethernet tunnel software network interface");
SYSCTL_INT(_net_link_tap, OID_AUTO, user_open, CTLFLAG_RW, &tapuopen, 0,
	"Allow user to open /dev/tap (based on node permissions)");
SYSCTL_INT(_net_link_tap, OID_AUTO, up_on_open, CTLFLAG_RW, &tapuponopen, 0,
	"Bring interface up when /dev/tap is opened");
SYSCTL_INT(_net_link_tap, OID_AUTO, devfs_cloning, CTLFLAG_RWTUN, &tapdclone, 0,
	"Enable legacy devfs interface creation");
SYSCTL_INT(_net_link_tap, OID_AUTO, debug, CTLFLAG_RW, &tapdebug, 0, "");

DEV_MODULE(if_tap, tapmodevent, NULL);

static int
tap_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct cdev *dev;
	int i;

	/* Find any existing device, or allocate new unit number. */
	i = clone_create(&tapclones, &tap_cdevsw, &unit, &dev, 0);
	if (i) {
		dev = make_dev(&tap_cdevsw, unit, UID_ROOT, GID_WHEEL, 0600,
		    "%s%d", tapname, unit);
	}

	tapcreate(dev);
	return (0);
}

/* vmnet devices are tap devices in disguise */
static int
vmnet_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct cdev *dev;
	int i;

	/* Find any existing device, or allocate new unit number. */
	i = clone_create(&tapclones, &tap_cdevsw, &unit, &dev, VMNET_DEV_MASK);
	if (i) {
		dev = make_dev(&tap_cdevsw, unit | VMNET_DEV_MASK, UID_ROOT,
		    GID_WHEEL, 0600, "%s%d", vmnetname, unit);
	}

	tapcreate(dev);
	return (0);
}

static void
tap_destroy(struct tap_softc *tp)
{
	struct ifnet *ifp = tp->tap_ifp;

	CURVNET_SET(ifp->if_vnet);
	destroy_dev(tp->tap_dev);
	seldrain(&tp->tap_rsel);
	knlist_clear(&tp->tap_rsel.si_note, 0);
	knlist_destroy(&tp->tap_rsel.si_note);
	ether_ifdetach(ifp);
	if_free(ifp);

	mtx_destroy(&tp->tap_mtx);
	free(tp, M_TAP);
	CURVNET_RESTORE();
}

static void
tap_clone_destroy(struct ifnet *ifp)
{
	struct tap_softc *tp = ifp->if_softc;

	mtx_lock(&tapmtx);
	SLIST_REMOVE(&taphead, tp, tap_softc, tap_next);
	mtx_unlock(&tapmtx);
	tap_destroy(tp);
}

/* vmnet devices are tap devices in disguise */
static void
vmnet_clone_destroy(struct ifnet *ifp)
{
	tap_clone_destroy(ifp);
}

/*
 * tapmodevent
 *
 * module event handler
 */
static int
tapmodevent(module_t mod, int type, void *data)
{
	static eventhandler_tag	 eh_tag = NULL;
	struct tap_softc	*tp = NULL;
	struct ifnet		*ifp = NULL;

	switch (type) {
	case MOD_LOAD:

		/* intitialize device */

		mtx_init(&tapmtx, "tapmtx", NULL, MTX_DEF);
		SLIST_INIT(&taphead);

		clone_setup(&tapclones);
		eh_tag = EVENTHANDLER_REGISTER(dev_clone, tapclone, 0, 1000);
		if (eh_tag == NULL) {
			clone_cleanup(&tapclones);
			mtx_destroy(&tapmtx);
			return (ENOMEM);
		}
		tap_cloner = if_clone_simple(tapname, tap_clone_create,
		    tap_clone_destroy, 0);
		vmnet_cloner = if_clone_simple(vmnetname, vmnet_clone_create,
		    vmnet_clone_destroy, 0);
		return (0);

	case MOD_UNLOAD:
		/*
		 * The EBUSY algorithm here can't quite atomically
		 * guarantee that this is race-free since we have to
		 * release the tap mtx to deregister the clone handler.
		 */
		mtx_lock(&tapmtx);
		SLIST_FOREACH(tp, &taphead, tap_next) {
			mtx_lock(&tp->tap_mtx);
			if (tp->tap_flags & TAP_OPEN) {
				mtx_unlock(&tp->tap_mtx);
				mtx_unlock(&tapmtx);
				return (EBUSY);
			}
			mtx_unlock(&tp->tap_mtx);
		}
		mtx_unlock(&tapmtx);

		EVENTHANDLER_DEREGISTER(dev_clone, eh_tag);
		if_clone_detach(tap_cloner);
		if_clone_detach(vmnet_cloner);
		drain_dev_clone_events();

		mtx_lock(&tapmtx);
		while ((tp = SLIST_FIRST(&taphead)) != NULL) {
			SLIST_REMOVE_HEAD(&taphead, tap_next);
			mtx_unlock(&tapmtx);

			ifp = tp->tap_ifp;

			TAPDEBUG("detaching %s\n", ifp->if_xname);

			tap_destroy(tp);
			mtx_lock(&tapmtx);
		}
		mtx_unlock(&tapmtx);
		clone_cleanup(&tapclones);

		mtx_destroy(&tapmtx);

		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
} /* tapmodevent */


/*
 * DEVFS handler
 *
 * We need to support two kind of devices - tap and vmnet
 */
static void
tapclone(void *arg, struct ucred *cred, char *name, int namelen, struct cdev **dev)
{
	char		devname[SPECNAMELEN + 1];
	int		i, unit, append_unit;
	int		extra;

	if (*dev != NULL)
		return;

	if (!tapdclone ||
	    (!tapuopen && priv_check_cred(cred, PRIV_NET_IFCREATE) != 0))
		return;

	unit = 0;
	append_unit = 0;
	extra = 0;

	/* We're interested in only tap/vmnet devices. */
	if (strcmp(name, tapname) == 0) {
		unit = -1;
	} else if (strcmp(name, vmnetname) == 0) {
		unit = -1;
		extra = VMNET_DEV_MASK;
	} else if (dev_stdclone(name, NULL, tapname, &unit) != 1) {
		if (dev_stdclone(name, NULL, vmnetname, &unit) != 1) {
			return;
		} else {
			extra = VMNET_DEV_MASK;
		}
	}

	if (unit == -1)
		append_unit = 1;

	CURVNET_SET(CRED_TO_VNET(cred));
	/* find any existing device, or allocate new unit number */
	i = clone_create(&tapclones, &tap_cdevsw, &unit, dev, extra);
	if (i) {
		if (append_unit) {
			/*
			 * We were passed 'tun' or 'tap', with no unit specified
			 * so we'll need to append it now.
			 */
			namelen = snprintf(devname, sizeof(devname), "%s%d", name,
			    unit);
			name = devname;
		}

		*dev = make_dev_credf(MAKEDEV_REF, &tap_cdevsw, unit | extra,
		     cred, UID_ROOT, GID_WHEEL, 0600, "%s", name);
	}

	if_clone_create(name, namelen, NULL);
	CURVNET_RESTORE();
} /* tapclone */


/*
 * tapcreate
 *
 * to create interface
 */
static void
tapcreate(struct cdev *dev)
{
	struct ifnet		*ifp = NULL;
	struct tap_softc	*tp = NULL;
	unsigned short		 macaddr_hi;
	uint32_t		 macaddr_mid;
	int			 unit;
	const char		*name = NULL;
	u_char			eaddr[6];

	/* allocate driver storage and create device */
	tp = malloc(sizeof(*tp), M_TAP, M_WAITOK | M_ZERO);
	mtx_init(&tp->tap_mtx, "tap_mtx", NULL, MTX_DEF);
	mtx_lock(&tapmtx);
	SLIST_INSERT_HEAD(&taphead, tp, tap_next);
	mtx_unlock(&tapmtx);

	unit = dev2unit(dev);

	/* select device: tap or vmnet */
	if (unit & VMNET_DEV_MASK) {
		name = vmnetname;
		tp->tap_flags |= TAP_VMNET;
	} else
		name = tapname;

	unit &= TAPMAXUNIT;

	TAPDEBUG("tapcreate(%s%d). minor = %#x\n", name, unit, dev2unit(dev));

	/* generate fake MAC address: 00 bd xx xx xx unit_no */
	macaddr_hi = htons(0x00bd);
	macaddr_mid = (uint32_t) ticks;
	bcopy(&macaddr_hi, eaddr, sizeof(short));
	bcopy(&macaddr_mid, &eaddr[2], sizeof(uint32_t));
	eaddr[5] = (u_char)unit;

	/* fill the rest and attach interface */
	ifp = tp->tap_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL)
		panic("%s%d: can not if_alloc()", name, unit);
	ifp->if_softc = tp;
	if_initname(ifp, name, unit);
	ifp->if_init = tapifinit;
	ifp->if_start = tapifstart;
	ifp->if_ioctl = tapifioctl;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = (IFF_BROADCAST|IFF_SIMPLEX|IFF_MULTICAST);
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_capabilities |= IFCAP_LINKSTATE;
	ifp->if_capenable |= IFCAP_LINKSTATE;

	dev->si_drv1 = tp;
	tp->tap_dev = dev;

	ether_ifattach(ifp, eaddr);

	mtx_lock(&tp->tap_mtx);
	tp->tap_flags |= TAP_INITED;
	mtx_unlock(&tp->tap_mtx);

	knlist_init_mtx(&tp->tap_rsel.si_note, &tp->tap_mtx);

	TAPDEBUG("interface %s is created. minor = %#x\n", 
		ifp->if_xname, dev2unit(dev));
} /* tapcreate */


/*
 * tapopen
 *
 * to open tunnel. must be superuser
 */
static int
tapopen(struct cdev *dev, int flag, int mode, struct thread *td)
{
	struct tap_softc	*tp = NULL;
	struct ifnet		*ifp = NULL;
	int			 error;

	if (tapuopen == 0) {
		error = priv_check(td, PRIV_NET_TAP);
		if (error)
			return (error);
	}

	if ((dev2unit(dev) & CLONE_UNITMASK) > TAPMAXUNIT)
		return (ENXIO);

	tp = dev->si_drv1;

	mtx_lock(&tp->tap_mtx);
	if (tp->tap_flags & TAP_OPEN) {
		mtx_unlock(&tp->tap_mtx);
		return (EBUSY);
	}

	bcopy(IF_LLADDR(tp->tap_ifp), tp->ether_addr, sizeof(tp->ether_addr));
	tp->tap_pid = td->td_proc->p_pid;
	tp->tap_flags |= TAP_OPEN;
	ifp = tp->tap_ifp;

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	if (tapuponopen)
		ifp->if_flags |= IFF_UP;
	if_link_state_change(ifp, LINK_STATE_UP);
	mtx_unlock(&tp->tap_mtx);

	TAPDEBUG("%s is open. minor = %#x\n", ifp->if_xname, dev2unit(dev));

	return (0);
} /* tapopen */


/*
 * tapclose
 *
 * close the device - mark i/f down & delete routing info
 */
static int
tapclose(struct cdev *dev, int foo, int bar, struct thread *td)
{
	struct ifaddr		*ifa;
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = tp->tap_ifp;

	/* junk all pending output */
	mtx_lock(&tp->tap_mtx);
	CURVNET_SET(ifp->if_vnet);
	IF_DRAIN(&ifp->if_snd);

	/*
	 * Do not bring the interface down, and do not anything with
	 * interface, if we are in VMnet mode. Just close the device.
	 */
	if (((tp->tap_flags & TAP_VMNET) == 0) &&
	    (ifp->if_flags & (IFF_UP | IFF_LINK0)) == IFF_UP) {
		mtx_unlock(&tp->tap_mtx);
		if_down(ifp);
		mtx_lock(&tp->tap_mtx);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			mtx_unlock(&tp->tap_mtx);
			CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
				rtinit(ifa, (int)RTM_DELETE, 0);
			}
			if_purgeaddrs(ifp);
			mtx_lock(&tp->tap_mtx);
		}
	}

	if_link_state_change(ifp, LINK_STATE_DOWN);
	CURVNET_RESTORE();

	funsetown(&tp->tap_sigio);
	selwakeuppri(&tp->tap_rsel, PZERO+1);
	KNOTE_LOCKED(&tp->tap_rsel.si_note, 0);

	tp->tap_flags &= ~TAP_OPEN;
	tp->tap_pid = 0;
	mtx_unlock(&tp->tap_mtx);

	TAPDEBUG("%s is closed. minor = %#x\n", 
		ifp->if_xname, dev2unit(dev));

	return (0);
} /* tapclose */


/*
 * tapifinit
 *
 * network interface initialization function
 */
static void
tapifinit(void *xtp)
{
	struct tap_softc	*tp = (struct tap_softc *)xtp;
	struct ifnet		*ifp = tp->tap_ifp;

	TAPDEBUG("initializing %s\n", ifp->if_xname);

	mtx_lock(&tp->tap_mtx);
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	mtx_unlock(&tp->tap_mtx);

	/* attempt to start output */
	tapifstart(ifp);
} /* tapifinit */


/*
 * tapifioctl
 *
 * Process an ioctl request on network interface
 */
static int
tapifioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct tap_softc	*tp = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	struct ifstat		*ifs = NULL;
	struct ifmediareq	*ifmr = NULL;
	int			 dummy, error = 0;

	switch (cmd) {
		case SIOCSIFFLAGS: /* XXX -- just like vmnet does */
		case SIOCADDMULTI:
		case SIOCDELMULTI:
			break;

		case SIOCGIFMEDIA:
			ifmr = (struct ifmediareq *)data;
			dummy = ifmr->ifm_count;
			ifmr->ifm_count = 1;
			ifmr->ifm_status = IFM_AVALID;
			ifmr->ifm_active = IFM_ETHER;
			if (tp->tap_flags & TAP_OPEN)
				ifmr->ifm_status |= IFM_ACTIVE;
			ifmr->ifm_current = ifmr->ifm_active;
			if (dummy >= 1) {
				int media = IFM_ETHER;
				error = copyout(&media, ifmr->ifm_ulist,
				    sizeof(int));
			}
			break;

		case SIOCSIFMTU:
			ifp->if_mtu = ifr->ifr_mtu;
			break;

		case SIOCGIFSTATUS:
			ifs = (struct ifstat *)data;
			mtx_lock(&tp->tap_mtx);
			if (tp->tap_pid != 0)
				snprintf(ifs->ascii, sizeof(ifs->ascii),
					"\tOpened by PID %d\n", tp->tap_pid);
			else
				ifs->ascii[0] = '\0';
			mtx_unlock(&tp->tap_mtx);
			break;

		default:
			error = ether_ioctl(ifp, cmd, data);
			break;
	}

	return (error);
} /* tapifioctl */


/*
 * tapifstart
 *
 * queue packets from higher level ready to put out
 */
static void
tapifstart(struct ifnet *ifp)
{
	struct tap_softc	*tp = ifp->if_softc;

	TAPDEBUG("%s starting\n", ifp->if_xname);

	/*
	 * do not junk pending output if we are in VMnet mode.
	 * XXX: can this do any harm because of queue overflow?
	 */

	mtx_lock(&tp->tap_mtx);
	if (((tp->tap_flags & TAP_VMNET) == 0) &&
	    ((tp->tap_flags & TAP_READY) != TAP_READY)) {
		struct mbuf *m;

		/* Unlocked read. */
		TAPDEBUG("%s not ready, tap_flags = 0x%x\n", ifp->if_xname, 
		    tp->tap_flags);

		for (;;) {
			IF_DEQUEUE(&ifp->if_snd, m);
			if (m != NULL) {
				m_freem(m);
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			} else
				break;
		}
		mtx_unlock(&tp->tap_mtx);

		return;
	}

	ifp->if_drv_flags |= IFF_DRV_OACTIVE;

	if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
		if (tp->tap_flags & TAP_RWAIT) {
			tp->tap_flags &= ~TAP_RWAIT;
			wakeup(tp);
		}

		if ((tp->tap_flags & TAP_ASYNC) && (tp->tap_sigio != NULL)) {
			mtx_unlock(&tp->tap_mtx);
			pgsigio(&tp->tap_sigio, SIGIO, 0);
			mtx_lock(&tp->tap_mtx);
		}

		selwakeuppri(&tp->tap_rsel, PZERO+1);
		KNOTE_LOCKED(&tp->tap_rsel.si_note, 0);
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1); /* obytes are counted in ether_output */
	}

	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	mtx_unlock(&tp->tap_mtx);
} /* tapifstart */


/*
 * tapioctl
 *
 * the cdevsw interface is now pretty minimal
 */
static int
tapioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
	struct ifreq		 ifr;
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = tp->tap_ifp;
	struct tapinfo		*tapp = NULL;
	int			 f;
	int			 error;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4)
	int			 ival;
#endif

	switch (cmd) {
		case TAPSIFINFO:
			tapp = (struct tapinfo *)data;
			if (ifp->if_type != tapp->type)
				return (EPROTOTYPE);
			mtx_lock(&tp->tap_mtx);
			if (ifp->if_mtu != tapp->mtu) {
				strlcpy(ifr.ifr_name, if_name(ifp), IFNAMSIZ);
				ifr.ifr_mtu = tapp->mtu;
				CURVNET_SET(ifp->if_vnet);
				error = ifhwioctl(SIOCSIFMTU, ifp,
				    (caddr_t)&ifr, td);
				CURVNET_RESTORE();
				if (error) {
					mtx_unlock(&tp->tap_mtx);
					return (error);
				}
			}
			ifp->if_baudrate = tapp->baudrate;
			mtx_unlock(&tp->tap_mtx);
			break;

		case TAPGIFINFO:
			tapp = (struct tapinfo *)data;
			mtx_lock(&tp->tap_mtx);
			tapp->mtu = ifp->if_mtu;
			tapp->type = ifp->if_type;
			tapp->baudrate = ifp->if_baudrate;
			mtx_unlock(&tp->tap_mtx);
			break;

		case TAPSDEBUG:
			tapdebug = *(int *)data;
			break;

		case TAPGDEBUG:
			*(int *)data = tapdebug;
			break;

		case TAPGIFNAME: {
			struct ifreq	*ifr = (struct ifreq *) data;

			strlcpy(ifr->ifr_name, ifp->if_xname, IFNAMSIZ);
			} break;

		case FIONBIO:
			break;

		case FIOASYNC:
			mtx_lock(&tp->tap_mtx);
			if (*(int *)data)
				tp->tap_flags |= TAP_ASYNC;
			else
				tp->tap_flags &= ~TAP_ASYNC;
			mtx_unlock(&tp->tap_mtx);
			break;

		case FIONREAD:
			if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
				struct mbuf *mb;

				IFQ_LOCK(&ifp->if_snd);
				IFQ_POLL_NOLOCK(&ifp->if_snd, mb);
				for (*(int *)data = 0; mb != NULL;
				     mb = mb->m_next)
					*(int *)data += mb->m_len;
				IFQ_UNLOCK(&ifp->if_snd);
			} else
				*(int *)data = 0;
			break;

		case FIOSETOWN:
			return (fsetown(*(int *)data, &tp->tap_sigio));

		case FIOGETOWN:
			*(int *)data = fgetown(&tp->tap_sigio);
			return (0);

		/* this is deprecated, FIOSETOWN should be used instead */
		case TIOCSPGRP:
			return (fsetown(-(*(int *)data), &tp->tap_sigio));

		/* this is deprecated, FIOGETOWN should be used instead */
		case TIOCGPGRP:
			*(int *)data = -fgetown(&tp->tap_sigio);
			return (0);

		/* VMware/VMnet port ioctl's */

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4)
		case _IO('V', 0):
			ival = IOCPARM_IVAL(data);
			data = (caddr_t)&ival;
			/* FALLTHROUGH */
#endif
		case VMIO_SIOCSIFFLAGS: /* VMware/VMnet SIOCSIFFLAGS */
			f = *(int *)data;
			f &= 0x0fff;
			f &= ~IFF_CANTCHANGE;
			f |= IFF_UP;

			mtx_lock(&tp->tap_mtx);
			ifp->if_flags = f | (ifp->if_flags & IFF_CANTCHANGE);
			mtx_unlock(&tp->tap_mtx);
			break;

		case SIOCGIFADDR:	/* get MAC address of the remote side */
			mtx_lock(&tp->tap_mtx);
			bcopy(tp->ether_addr, data, sizeof(tp->ether_addr));
			mtx_unlock(&tp->tap_mtx);
			break;

		case SIOCSIFADDR:	/* set MAC address of the remote side */
			mtx_lock(&tp->tap_mtx);
			bcopy(data, tp->ether_addr, sizeof(tp->ether_addr));
			mtx_unlock(&tp->tap_mtx);
			break;

		default:
			return (ENOTTY);
	}
	return (0);
} /* tapioctl */


/*
 * tapread
 *
 * the cdevsw read interface - reads a packet at a time, or at
 * least as much of a packet as can be read
 */
static int
tapread(struct cdev *dev, struct uio *uio, int flag)
{
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = tp->tap_ifp;
	struct mbuf		*m = NULL;
	int			 error = 0, len;

	TAPDEBUG("%s reading, minor = %#x\n", ifp->if_xname, dev2unit(dev));

	mtx_lock(&tp->tap_mtx);
	if ((tp->tap_flags & TAP_READY) != TAP_READY) {
		mtx_unlock(&tp->tap_mtx);

		/* Unlocked read. */
		TAPDEBUG("%s not ready. minor = %#x, tap_flags = 0x%x\n",
			ifp->if_xname, dev2unit(dev), tp->tap_flags);

		return (EHOSTDOWN);
	}

	tp->tap_flags &= ~TAP_RWAIT;

	/* sleep until we get a packet */
	do {
		IF_DEQUEUE(&ifp->if_snd, m);

		if (m == NULL) {
			if (flag & O_NONBLOCK) {
				mtx_unlock(&tp->tap_mtx);
				return (EWOULDBLOCK);
			}

			tp->tap_flags |= TAP_RWAIT;
			error = mtx_sleep(tp, &tp->tap_mtx, PCATCH | (PZERO + 1),
			    "taprd", 0);
			if (error) {
				mtx_unlock(&tp->tap_mtx);
				return (error);
			}
		}
	} while (m == NULL);
	mtx_unlock(&tp->tap_mtx);

	/* feed packet to bpf */
	BPF_MTAP(ifp, m);

	/* xfer packet to user space */
	while ((m != NULL) && (uio->uio_resid > 0) && (error == 0)) {
		len = min(uio->uio_resid, m->m_len);
		if (len == 0)
			break;

		error = uiomove(mtod(m, void *), len, uio);
		m = m_free(m);
	}

	if (m != NULL) {
		TAPDEBUG("%s dropping mbuf, minor = %#x\n", ifp->if_xname, 
			dev2unit(dev));
		m_freem(m);
	}

	return (error);
} /* tapread */


/*
 * tapwrite
 *
 * the cdevsw write interface - an atomic write is a packet - or else!
 */
static int
tapwrite(struct cdev *dev, struct uio *uio, int flag)
{
	struct ether_header	*eh;
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = tp->tap_ifp;
	struct mbuf		*m;

	TAPDEBUG("%s writing, minor = %#x\n", 
		ifp->if_xname, dev2unit(dev));

	if (uio->uio_resid == 0)
		return (0);

	if ((uio->uio_resid < 0) || (uio->uio_resid > TAPMRU)) {
		TAPDEBUG("%s invalid packet len = %zd, minor = %#x\n",
			ifp->if_xname, uio->uio_resid, dev2unit(dev));

		return (EIO);
	}

	if ((m = m_uiotombuf(uio, M_NOWAIT, 0, ETHER_ALIGN,
	    M_PKTHDR)) == NULL) {
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		return (ENOBUFS);
	}

	m->m_pkthdr.rcvif = ifp;

	/*
	 * Only pass a unicast frame to ether_input(), if it would actually
	 * have been received by non-virtual hardware.
	 */
	if (m->m_len < sizeof(struct ether_header)) {
		m_freem(m);
		return (0);
	}
	eh = mtod(m, struct ether_header *);

	if (eh && (ifp->if_flags & IFF_PROMISC) == 0 &&
	    !ETHER_IS_MULTICAST(eh->ether_dhost) &&
	    bcmp(eh->ether_dhost, IF_LLADDR(ifp), ETHER_ADDR_LEN) != 0) {
		m_freem(m);
		return (0);
	}

	/* Pass packet up to parent. */
	CURVNET_SET(ifp->if_vnet);
	(*ifp->if_input)(ifp, m);
	CURVNET_RESTORE();
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1); /* ibytes are counted in parent */

	return (0);
} /* tapwrite */


/*
 * tappoll
 *
 * the poll interface, this is only useful on reads
 * really. the write detect always returns true, write never blocks
 * anyway, it either accepts the packet or drops it
 */
static int
tappoll(struct cdev *dev, int events, struct thread *td)
{
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = tp->tap_ifp;
	int			 revents = 0;

	TAPDEBUG("%s polling, minor = %#x\n", 
		ifp->if_xname, dev2unit(dev));

	if (events & (POLLIN | POLLRDNORM)) {
		IFQ_LOCK(&ifp->if_snd);
		if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
			TAPDEBUG("%s have data in queue. len = %d, " \
				"minor = %#x\n", ifp->if_xname,
				ifp->if_snd.ifq_len, dev2unit(dev));

			revents |= (events & (POLLIN | POLLRDNORM));
		} else {
			TAPDEBUG("%s waiting for data, minor = %#x\n",
				ifp->if_xname, dev2unit(dev));

			selrecord(td, &tp->tap_rsel);
		}
		IFQ_UNLOCK(&ifp->if_snd);
	}

	if (events & (POLLOUT | POLLWRNORM))
		revents |= (events & (POLLOUT | POLLWRNORM));

	return (revents);
} /* tappoll */


/*
 * tap_kqfilter
 *
 * support for kevent() system call
 */
static int
tapkqfilter(struct cdev *dev, struct knote *kn)
{
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = tp->tap_ifp;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		TAPDEBUG("%s kqfilter: EVFILT_READ, minor = %#x\n",
			ifp->if_xname, dev2unit(dev));
		kn->kn_fop = &tap_read_filterops;
		break;

	case EVFILT_WRITE:
		TAPDEBUG("%s kqfilter: EVFILT_WRITE, minor = %#x\n",
			ifp->if_xname, dev2unit(dev));
		kn->kn_fop = &tap_write_filterops;
		break;

	default:
		TAPDEBUG("%s kqfilter: invalid filter, minor = %#x\n",
			ifp->if_xname, dev2unit(dev));
		return (EINVAL);
		/* NOT REACHED */
	}

	kn->kn_hook = tp;
	knlist_add(&tp->tap_rsel.si_note, kn, 0);

	return (0);
} /* tapkqfilter */


/*
 * tap_kqread
 * 
 * Return true if there is data in the interface queue
 */
static int
tapkqread(struct knote *kn, long hint)
{
	int			 ret;
	struct tap_softc	*tp = kn->kn_hook;
	struct cdev		*dev = tp->tap_dev;
	struct ifnet		*ifp = tp->tap_ifp;

	if ((kn->kn_data = ifp->if_snd.ifq_len) > 0) {
		TAPDEBUG("%s have data in queue. len = %d, minor = %#x\n",
			ifp->if_xname, ifp->if_snd.ifq_len, dev2unit(dev));
		ret = 1;
	} else {
		TAPDEBUG("%s waiting for data, minor = %#x\n",
			ifp->if_xname, dev2unit(dev));
		ret = 0;
	}

	return (ret);
} /* tapkqread */


/*
 * tap_kqwrite
 *
 * Always can write. Return the MTU in kn->data
 */
static int
tapkqwrite(struct knote *kn, long hint)
{
	struct tap_softc	*tp = kn->kn_hook;
	struct ifnet		*ifp = tp->tap_ifp;

	kn->kn_data = ifp->if_mtu;

	return (1);
} /* tapkqwrite */


static void
tapkqdetach(struct knote *kn)
{
	struct tap_softc	*tp = kn->kn_hook;

	knlist_remove(&tp->tap_rsel.si_note, kn, 0);
} /* tapkqdetach */

