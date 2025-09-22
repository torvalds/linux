/*	$OpenBSD: wsmux.c,v 1.62 2025/07/18 17:34:29 mvs Exp $	*/
/*      $NetBSD: wsmux.c,v 1.37 2005/04/30 03:47:12 augustss Exp $      */

/*
 * Copyright (c) 1998, 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Author: Lennart Augustsson <augustss@carlstedt.se>
 *         Carlstedt Research & Technology
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "wsmux.h"
#include "wsdisplay.h"
#include "wskbd.h"
#include "wsmouse.h"

/*
 * wscons mux device.
 *
 * The mux device is a collection of real mice and keyboards and acts as
 * a merge point for all the events from the different real devices.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/signalvar.h>
#include <sys/device.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wseventvar.h>
#include <dev/wscons/wsmuxvar.h>

#define WSMUX_MAXDEPTH	8

#ifdef WSMUX_DEBUG
#define DPRINTF(x)	if (wsmuxdebug) printf x
#define DPRINTFN(n,x)	if (wsmuxdebug > (n)) printf x
int	wsmuxdebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/*
 * The wsmux pseudo device is used to multiplex events from several wsmouse,
 * wskbd, and/or wsmux devices together.
 * The devices connected together form a tree with muxes in the interior
 * and real devices (mouse and kbd) at the leaves.  The special case of
 * a tree with one node (mux or other) is supported as well.
 * Only the device at the root of the tree can be opened (if a non-root
 * device is opened the subtree rooted at that point is severed from the
 * containing tree).  When the root is opened it allocates a wseventvar
 * struct which all the nodes in the tree will send their events too.
 * An ioctl() performed on the root is propagated to all the nodes.
 * There are also ioctl() operations to add and remove nodes from a tree.
 */

int	wsmux_mux_open(struct wsevsrc *, struct wseventvar *);
int	wsmux_mux_close(struct wsevsrc *);

int	wsmux_do_open(struct wsmux_softc *, struct wseventvar *);

void	wsmux_do_close(struct wsmux_softc *);
#if NWSDISPLAY > 0
int	wsmux_evsrc_set_display(struct device *, struct device *);
#else
#define wsmux_evsrc_set_display NULL
#endif

int	wsmux_do_displayioctl(struct device *dev, u_long cmd, caddr_t data,
	    int flag, struct proc *p);
int	wsmux_do_ioctl(struct device *, u_long, caddr_t,int,struct proc *);

int	wsmux_add_mux(int, struct wsmux_softc *);

int	wsmux_depth(struct wsmux_softc *);

void	wsmuxattach(int);

void	wsmux_detach_sc_locked(struct wsmux_softc *, struct wsevsrc *);

struct wssrcops wsmux_srcops = {
	.type		= WSMUX_MUX,
	.dopen		= wsmux_mux_open,
	.dclose		= wsmux_mux_close,
	.dioctl		= wsmux_do_ioctl,
	.ddispioctl	= wsmux_do_displayioctl,
	.dsetdisplay	= wsmux_evsrc_set_display,
};

/*
 * Lock used by wsmux_add_mux() to grant exclusive access to the tree of
 * stacked wsmux devices.
 */
struct rwlock wsmux_tree_lock = RWLOCK_INITIALIZER("wsmuxtreelk");

/* From upper level */
void
wsmuxattach(int n)
{
}

/* Keep track of all muxes that have been allocated */
int nwsmux = 0;
struct wsmux_softc **wsmuxdevs = NULL;

/* Return mux n, create if necessary */
struct wsmux_softc *
wsmux_getmux(int n)
{
	struct wsmux_softc *sc;
	struct wsmux_softc **new, **old;
	int i;

	if (n < 0 || n >= WSMUX_MAXDEV)
		return (NULL);

	/* Make sure there is room for mux n in the table */
	if (n >= nwsmux) {
		old = wsmuxdevs;
		new = mallocarray(n + 1, sizeof (*wsmuxdevs),
		    M_DEVBUF, M_NOWAIT);
		if (new == NULL) {
			printf("wsmux_getmux: no memory for mux %d\n", n);
			return (NULL);
		}
		if (old != NULL)
			bcopy(old, new, nwsmux * sizeof(*wsmuxdevs));
		for (i = nwsmux; i < (n + 1); i++)
			new[i] = NULL;
		if (old != NULL)
			free(old, M_DEVBUF, nwsmux * sizeof(*wsmuxdevs));
		wsmuxdevs = new;
		nwsmux = n + 1;
	}

	sc = wsmuxdevs[n];
	if (sc == NULL) {
		sc = wsmux_create("wsmux", n);
		if (sc == NULL)
			printf("wsmux: attach out of memory\n");
		wsmuxdevs[n] = sc;
	}
	return (sc);
}

/*
 * open() of the pseudo device from device table.
 */
int
wsmuxopen(dev_t dev, int flags, int mode, struct proc *p)
{
	struct wsmux_softc *sc;
	struct wseventvar *evar;
	int error, unit;

	unit = minor(dev);
	sc = wsmux_getmux(unit);
	if (sc == NULL)
		return (ENXIO);

	DPRINTF(("%s: %s: sc=%p\n", __func__, sc->sc_base.me_dv.dv_xname, sc));

	if ((flags & (FREAD | FWRITE)) == FWRITE) {
		/* Not opening for read, only ioctl is available. */
		return (0);
	}

	if (sc->sc_base.me_parent != NULL) {
		/* Grab the mux out of the greedy hands of the parent mux. */
		DPRINTF(("%s: detach\n", __func__));
		wsmux_detach_sc(&sc->sc_base);
	}

	if (sc->sc_base.me_evp != NULL)
		/* Already open. */
		return (EBUSY);

	evar = &sc->sc_base.me_evar;
	if (wsevent_init(evar))
		return (EBUSY);
#ifdef WSDISPLAY_COMPAT_RAWKBD
	sc->sc_rawkbd = 0;
#endif

	error = wsmux_do_open(sc, evar);
	if (error)
		wsevent_fini(evar);
	return (error);
}

/*
 * Open of a mux via the parent mux.
 */
int
wsmux_mux_open(struct wsevsrc *me, struct wseventvar *evar)
{
	struct wsmux_softc *sc = (struct wsmux_softc *)me;

#ifdef DIAGNOSTIC
	if (sc->sc_base.me_parent == NULL) {
		printf("wsmux_mux_open: no parent\n");
		return (EINVAL);
	}
#endif

	return (wsmux_do_open(sc, evar));
}

/* Common part of opening a mux. */
int
wsmux_do_open(struct wsmux_softc *sc, struct wseventvar *evar)
{
	struct wsevsrc *me;
#ifdef DIAGNOSTIC
	int error;
#endif

	/* The device could already be attached to a mux. */
	if (sc->sc_base.me_evp != NULL)
		return (EBUSY);
	sc->sc_base.me_evp = evar; /* remember event variable, mark as open */

	/* Open all children. */
	rw_enter_read(&sc->sc_lock);
	TAILQ_FOREACH(me, &sc->sc_cld, me_next) {
		DPRINTF(("%s: %s: m=%p dev=%s\n", __func__,
			 sc->sc_base.me_dv.dv_xname, me,
			 me->me_dv.dv_xname));
#ifdef DIAGNOSTIC
		if (me->me_evp != NULL) {
			printf("wsmuxopen: dev already in use\n");
			continue;
		}
		if (me->me_parent != sc) {
			printf("wsmux_do_open: bad child=%p\n", me);
			continue;
		}
		error = wsevsrc_open(me, evar);
		if (error) {
			DPRINTF(("%s: open failed %d\n", __func__, error));
		}
#else
		/* ignore errors, failing children will not be marked open */
		(void)wsevsrc_open(me, evar);
#endif
	}
	rw_exit_read(&sc->sc_lock);

	return (0);
}

/*
 * close() of the pseudo device from device table.
 */
int
wsmuxclose(dev_t dev, int flags, int mode, struct proc *p)
{
	struct wsmux_softc *sc =
	    (struct wsmux_softc *)wsmuxdevs[minor(dev)];
	struct wseventvar *evar = sc->sc_base.me_evp;

	if ((flags & (FREAD | FWRITE)) == FWRITE)
		/* Not open for read */
		return (0);

	wsmux_do_close(sc);
	sc->sc_base.me_evp = NULL;
	wsevent_fini(evar);
	return (0);
}

/*
 * Close of a mux via the parent mux.
 */
int
wsmux_mux_close(struct wsevsrc *me)
{
	struct wsmux_softc *sc = (struct wsmux_softc *)me;

	wsmux_do_close(sc);
	sc->sc_base.me_evp = NULL;

	return (0);
}

/* Common part of closing a mux. */
void
wsmux_do_close(struct wsmux_softc *sc)
{
	struct wsevsrc *me;

	DPRINTF(("%s: %s: sc=%p\n", __func__, sc->sc_base.me_dv.dv_xname, sc));

	/* Close all the children. */
	rw_enter_read(&sc->sc_lock);
	TAILQ_FOREACH(me, &sc->sc_cld, me_next) {
		DPRINTF(("%s %s: m=%p dev=%s\n", __func__,
			 sc->sc_base.me_dv.dv_xname, me, me->me_dv.dv_xname));
#ifdef DIAGNOSTIC
		if (me->me_parent != sc) {
			printf("wsmuxclose: bad child=%p\n", me);
			continue;
		}
#endif
		(void)wsevsrc_close(me);
	}
	rw_exit_read(&sc->sc_lock);
}

/*
 * read() of the pseudo device from device table.
 */
int
wsmuxread(dev_t dev, struct uio *uio, int flags)
{
	struct wsmux_softc *sc = wsmuxdevs[minor(dev)];
	struct wseventvar *evar;
	int error;

	evar = sc->sc_base.me_evp;
	if (evar == NULL) {
#ifdef DIAGNOSTIC
		/* XXX can we get here? */
		printf("wsmuxread: not open\n");
#endif
		return (EINVAL);
	}

	DPRINTFN(5, ("%s: %s event read evar=%p\n", __func__,
		     sc->sc_base.me_dv.dv_xname, evar));
	error = wsevent_read(evar, uio, flags);
	DPRINTFN(5, ("%s: %s event read ==> error=%d\n", __func__,
		     sc->sc_base.me_dv.dv_xname, error));
	return (error);
}

/*
 * ioctl of the pseudo device from device table.
 */
int
wsmuxioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return wsmux_do_ioctl(&wsmuxdevs[minor(dev)]->sc_base.me_dv, cmd, data, flag, p);
}

/*
 * ioctl of a mux via the parent mux, continuation of wsmuxioctl().
 */
int
wsmux_do_ioctl(struct device *dv, u_long cmd, caddr_t data, int flag,
    struct proc *p)
{
	struct wsmux_softc *sc = (struct wsmux_softc *)dv;
	struct wsevsrc *me;
	int error, ok;
	int put, get, n;
	struct wseventvar *evar;
	struct wscons_event *ev;
	struct wsmux_device_list *l;

	DPRINTF(("%s: %s: enter sc=%p, cmd=%08lx\n", __func__,
		 sc->sc_base.me_dv.dv_xname, sc, cmd));

	switch (cmd) {
	case WSMUXIO_INJECTEVENT:
	case WSMUXIO_ADD_DEVICE:
	case WSMUXIO_REMOVE_DEVICE:
#ifdef WSDISPLAY_COMPAT_RAWKBD
	case WSKBDIO_SETMODE:
#endif
		if ((flag & FWRITE) == 0)
			return (EACCES);
	}

	switch (cmd) {
	case WSMUXIO_INJECTEVENT:
		/* Inject an event, e.g., from moused. */
		DPRINTF(("%s: inject\n", sc->sc_base.me_dv.dv_xname));
		evar = sc->sc_base.me_evp;
		if (evar == NULL) {
			/* No event sink, so ignore it. */
			DPRINTF(("%s: event ignored\n", __func__));
			return (0);
		}

		mtx_enter(&evar->ws_mtx);
		get = evar->ws_get;
		put = evar->ws_put;
		ev = &evar->ws_q[put];
		if (++put % WSEVENT_QSIZE == get) {
			mtx_leave(&evar->ws_mtx);
			return (ENOSPC);
		}
		if (put >= WSEVENT_QSIZE)
			put = 0;
		*ev = *(struct wscons_event *)data;
		nanotime(&ev->time);
		evar->ws_put = put;
		mtx_leave(&evar->ws_mtx);
		wsevent_wakeup(evar);
		return (0);
	case WSMUXIO_ADD_DEVICE:
#define d ((struct wsmux_device *)data)
		DPRINTF(("%s: add type=%d, no=%d\n", sc->sc_base.me_dv.dv_xname,
			 d->type, d->idx));
		if (d->idx < 0)
			return (ENXIO);
		switch (d->type) {
#if NWSMOUSE > 0
		case WSMUX_MOUSE:
			return (wsmouse_add_mux(d->idx, sc));
#endif
#if NWSKBD > 0
		case WSMUX_KBD:
			return (wskbd_add_mux(d->idx, sc));
#endif
		case WSMUX_MUX:
			return (wsmux_add_mux(d->idx, sc));
		default:
			return (EINVAL);
		}
	case WSMUXIO_REMOVE_DEVICE:
		DPRINTF(("%s: rem type=%d, no=%d\n", sc->sc_base.me_dv.dv_xname,
			 d->type, d->idx));
		/* Locate the device */
		rw_enter_write(&sc->sc_lock);
		TAILQ_FOREACH(me, &sc->sc_cld, me_next) {
			if (me->me_ops->type == d->type &&
			    me->me_dv.dv_unit == d->idx) {
				DPRINTF(("%s: detach\n", __func__));
				wsmux_detach_sc_locked(sc, me);
				rw_exit_write(&sc->sc_lock);
				return (0);
			}
		}
		rw_exit_write(&sc->sc_lock);
		return (EINVAL);
#undef d

	case WSMUXIO_LIST_DEVICES:
		DPRINTF(("%s: list\n", sc->sc_base.me_dv.dv_xname));
		l = (struct wsmux_device_list *)data;
		n = 0;
		rw_enter_read(&sc->sc_lock);
		TAILQ_FOREACH(me, &sc->sc_cld, me_next) {
			if (n >= WSMUX_MAXDEV)
				break;
			l->devices[n].type = me->me_ops->type;
			l->devices[n].idx = me->me_dv.dv_unit;
			n++;
		}
		rw_exit_read(&sc->sc_lock);
		l->ndevices = n;
		return (0);
#ifdef WSDISPLAY_COMPAT_RAWKBD
	case WSKBDIO_SETMODE:
		sc->sc_rawkbd = *(int *)data;
		DPRINTF(("%s: save rawkbd = %d\n", __func__, sc->sc_rawkbd));
		break;
#endif
	case FIOASYNC:
		DPRINTF(("%s: FIOASYNC\n", sc->sc_base.me_dv.dv_xname));
		evar = sc->sc_base.me_evp;
		if (evar == NULL)
			return (EINVAL);
		mtx_enter(&evar->ws_mtx);
		evar->ws_async = *(int *)data != 0;
		mtx_leave(&evar->ws_mtx);
		return (0);
	case FIOGETOWN:
	case TIOCGPGRP:
		DPRINTF(("%s: getown (%lu)\n", sc->sc_base.me_dv.dv_xname,
			 cmd));
		evar = sc->sc_base.me_evp;
		if (evar == NULL)
			return (EINVAL);
		sigio_getown(&evar->ws_sigio, cmd, data);
		return (0);
	case FIOSETOWN:
	case TIOCSPGRP:
		DPRINTF(("%s: setown (%lu)\n", sc->sc_base.me_dv.dv_xname,
			 cmd));
		evar = sc->sc_base.me_evp;
		if (evar == NULL)
			return (EINVAL);
		return (sigio_setown(&evar->ws_sigio, cmd, data));
	default:
		DPRINTF(("%s: unknown\n", sc->sc_base.me_dv.dv_xname));
		break;
	}

	if (sc->sc_base.me_evp == NULL
#if NWSDISPLAY > 0
	    && sc->sc_displaydv == NULL
#endif
	    )
		return (EACCES);

	/*
	 * If children are attached: return 0 if any of the ioctl() succeeds,
	 * otherwise the last error.
	 */
	error = ENOTTY;
	ok = 0;
	rw_enter_read(&sc->sc_lock);
	TAILQ_FOREACH(me, &sc->sc_cld, me_next) {
#ifdef DIAGNOSTIC
		/* XXX check evp? */
		if (me->me_parent != sc) {
			printf("wsmux_do_ioctl: bad child %p\n", me);
			continue;
		}
#endif
		error = wsevsrc_ioctl(me, cmd, data, flag, p);
		DPRINTF(("%s: %s: me=%p dev=%s ==> %d\n", __func__,
			 sc->sc_base.me_dv.dv_xname, me, me->me_dv.dv_xname,
			 error));
		if (!error)
			ok = 1;
	}
	rw_exit_read(&sc->sc_lock);
	if (ok)
		error = 0;

	return (error);
}

int
wsmuxkqfilter(dev_t dev, struct knote *kn)
{
	struct wsmux_softc *sc = wsmuxdevs[minor(dev)];

	if (sc->sc_base.me_evp == NULL)
		return (ENXIO);
	return (wsevent_kqfilter(sc->sc_base.me_evp, kn));
}

/*
 * Add mux unit as a child to muxsc.
 */
int
wsmux_add_mux(int unit, struct wsmux_softc *muxsc)
{
	struct wsmux_softc *sc, *m;
	int error;
	int depth = 0;

	sc = wsmux_getmux(unit);
	if (sc == NULL)
		return (ENXIO);

	rw_enter_write(&wsmux_tree_lock);

	DPRINTF(("%s: %s(%p) to %s(%p)\n", __func__,
		 sc->sc_base.me_dv.dv_xname, sc,
		 muxsc->sc_base.me_dv.dv_xname, muxsc));

	if (sc->sc_base.me_parent != NULL || sc->sc_base.me_evp != NULL) {
		error = EBUSY;
		goto out;
	}

	/* The mux we are adding must not be an ancestor of itself. */
	for (m = muxsc; m != NULL; m = m->sc_base.me_parent) {
		if (m == sc) {
			error = EINVAL;
			goto out;
		}
		depth++;
	}

	/*
	 * Limit the number of stacked wsmux devices to avoid exhausting
	 * the kernel stack during wsmux_do_open().
	 */
	if (depth + wsmux_depth(sc) > WSMUX_MAXDEPTH) {
		error = EBUSY;
		goto out;
	}

	error = wsmux_attach_sc(muxsc, &sc->sc_base);
out:
	rw_exit_write(&wsmux_tree_lock);
	return (error);
}

/* Create a new mux softc. */
struct wsmux_softc *
wsmux_create(const char *name, int unit)
{
	struct wsmux_softc *sc;

	DPRINTF(("%s: allocating\n", __func__));
	sc = malloc(sizeof *sc, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc == NULL)
		return (NULL);
	TAILQ_INIT(&sc->sc_cld);
	rw_init_flags(&sc->sc_lock, "wsmuxlk", RWL_DUPOK);
	snprintf(sc->sc_base.me_dv.dv_xname, sizeof sc->sc_base.me_dv.dv_xname,
		 "%s%d", name, unit);
	sc->sc_base.me_dv.dv_unit = unit;
	sc->sc_base.me_ops = &wsmux_srcops;
	sc->sc_kbd_layout = KB_NONE;
	return (sc);
}

/* Attach me as a child to sc. */
int
wsmux_attach_sc(struct wsmux_softc *sc, struct wsevsrc *me)
{
	int error;

	if (sc == NULL)
		return (EINVAL);

	rw_enter_write(&sc->sc_lock);

	DPRINTF(("%s: %s(%p): type=%d\n", __func__,
		 sc->sc_base.me_dv.dv_xname, sc, me->me_ops->type));

#ifdef DIAGNOSTIC
	if (me->me_parent != NULL) {
		rw_exit_write(&sc->sc_lock);
		printf("wsmux_attach_sc: busy\n");
		return (EBUSY);
	}
#endif
	me->me_parent = sc;
	TAILQ_INSERT_TAIL(&sc->sc_cld, me, me_next);

	error = 0;
#if NWSDISPLAY > 0
	if (sc->sc_displaydv != NULL) {
		/* This is a display mux, so attach the new device to it. */
		DPRINTF(("%s: %s: set display %p\n", __func__,
			 sc->sc_base.me_dv.dv_xname, sc->sc_displaydv));
		if (me->me_ops->dsetdisplay != NULL) {
			error = wsevsrc_set_display(me, sc->sc_displaydv);
			/* Ignore that the console already has a display. */
			if (error == EBUSY)
				error = 0;
			if (!error) {
#ifdef WSDISPLAY_COMPAT_RAWKBD
				DPRINTF(("%s: %s set rawkbd=%d\n", __func__,
					 me->me_dv.dv_xname, sc->sc_rawkbd));
				(void)wsevsrc_ioctl(me, WSKBDIO_SETMODE,
						    &sc->sc_rawkbd, FWRITE, 0);
#endif
			}
		}
	}
#endif
	if (sc->sc_base.me_evp != NULL) {
		/* Mux is open, try to open the subdevice. */
		error = wsevsrc_open(me, sc->sc_base.me_evp);
	} else {
		/* Mux is closed, ensure that the subdevice is also closed. */
		if (me->me_evp != NULL)
			error = EBUSY;
	}

	if (error) {
		me->me_parent = NULL;
		TAILQ_REMOVE(&sc->sc_cld, me, me_next);
	}

	rw_exit_write(&sc->sc_lock);

	DPRINTF(("%s: %s(%p) done, error=%d\n", __func__,
		 sc->sc_base.me_dv.dv_xname, sc, error));
	return (error);
}

/* Remove me from the parent. */
void
wsmux_detach_sc(struct wsevsrc *me)
{
	struct wsmux_softc *sc = me->me_parent;

	if (sc == NULL) {
		printf("wsmux_detach_sc: %s has no parent\n",
		       me->me_dv.dv_xname);
		return;
	}

	rw_enter_write(&sc->sc_lock);
	wsmux_detach_sc_locked(sc, me);
	rw_exit_write(&sc->sc_lock);
}

void
wsmux_detach_sc_locked(struct wsmux_softc *sc, struct wsevsrc *me)
{
	rw_assert_wrlock(&sc->sc_lock);

	DPRINTF(("%s: %s(%p) parent=%p\n", __func__,
		 me->me_dv.dv_xname, me, sc));

	if (me->me_parent != sc) {
		/* Device detached or attached to another mux while sleeping. */
		return;
	}

#if NWSDISPLAY > 0
	if (sc->sc_displaydv != NULL) {
		if (me->me_ops->dsetdisplay != NULL)
			/* ignore error, there's nothing we can do */
			(void)wsevsrc_set_display(me, NULL);
	} else
#endif
		if (me->me_evp != NULL) {
		DPRINTF(("%s: close\n", __func__));
		/* mux device is open, so close multiplexee */
		(void)wsevsrc_close(me);
	}

	TAILQ_REMOVE(&sc->sc_cld, me, me_next);
	me->me_parent = NULL;

	DPRINTF(("%s: done sc=%p\n", __func__, sc));
}

/*
 * Display ioctl() of a mux via the parent mux.
 */
int
wsmux_do_displayioctl(struct device *dv, u_long cmd, caddr_t data, int flag,
    struct proc *p)
{
	struct wsmux_softc *sc = (struct wsmux_softc *)dv;
	struct wsevsrc *me;
	int error, ok;

	DPRINTF(("%s: %s: sc=%p, cmd=%08lx\n", __func__,
		 sc->sc_base.me_dv.dv_xname, sc, cmd));

#ifdef WSDISPLAY_COMPAT_RAWKBD
	if (cmd == WSKBDIO_SETMODE) {
		sc->sc_rawkbd = *(int *)data;
		DPRINTF(("%s: rawkbd = %d\n", __func__, sc->sc_rawkbd));
	}
#endif

	/*
	 * Return 0 if any of the ioctl() succeeds, otherwise the last error.
	 * Return -1 if no mux component accepts the ioctl.
	 */
	error = -1;
	ok = 0;
	rw_enter_read(&sc->sc_lock);
	TAILQ_FOREACH(me, &sc->sc_cld, me_next) {
		DPRINTF(("%s: me=%p\n", __func__, me));
#ifdef DIAGNOSTIC
		if (me->me_parent != sc) {
			printf("wsmux_displayioctl: bad child %p\n", me);
			continue;
		}
#endif
		if (me->me_ops->ddispioctl != NULL) {
			error = wsevsrc_display_ioctl(me, cmd, data, flag, p);
			DPRINTF(("%s: me=%p dev=%s ==> %d\n", __func__,
				 me, me->me_dv.dv_xname, error));
			if (!error)
				ok = 1;
		}
	}
	rw_exit_read(&sc->sc_lock);
	if (ok)
		error = 0;

	return (error);
}

#if NWSDISPLAY > 0
/*
 * Set display of a mux via the parent mux.
 */
int
wsmux_evsrc_set_display(struct device *dv, struct device *displaydv)
{
	struct wsmux_softc *sc = (struct wsmux_softc *)dv;

	DPRINTF(("%s: %s: displaydv=%p\n", __func__,
		 sc->sc_base.me_dv.dv_xname, displaydv));

	if (displaydv != NULL) {
		if (sc->sc_displaydv != NULL)
			return (EBUSY);
	} else {
		if (sc->sc_displaydv == NULL)
			return (ENXIO);
	}

	return wsmux_set_display(sc, displaydv);
}

int
wsmux_set_display(struct wsmux_softc *sc, struct device *displaydv)
{
	struct device *odisplaydv;
	struct wsevsrc *me;
	struct wsmux_softc *nsc = displaydv ? sc : NULL;
	int error, ok;

	rw_enter_read(&sc->sc_lock);

	odisplaydv = sc->sc_displaydv;
	sc->sc_displaydv = displaydv;

	if (displaydv) {
		DPRINTF(("%s: connecting to %s\n",
		       sc->sc_base.me_dv.dv_xname, displaydv->dv_xname));
	}
	ok = 0;
	error = 0;
	TAILQ_FOREACH(me, &sc->sc_cld, me_next) {
#ifdef DIAGNOSTIC
		if (me->me_parent != sc) {
			printf("wsmux_set_display: bad child parent %p\n", me);
			continue;
		}
#endif
		if (me->me_ops->dsetdisplay != NULL) {
			error = wsevsrc_set_display(me,
			    nsc ? nsc->sc_displaydv : NULL);
			DPRINTF(("%s: m=%p dev=%s error=%d\n", __func__,
				 me, me->me_dv.dv_xname, error));
			if (!error) {
				ok = 1;
#ifdef WSDISPLAY_COMPAT_RAWKBD
				DPRINTF(("%s: %s set rawkbd=%d\n", __func__,
					 me->me_dv.dv_xname, sc->sc_rawkbd));
				(void)wsevsrc_ioctl(me, WSKBDIO_SETMODE,
						    &sc->sc_rawkbd, FWRITE, 0);
#endif
			}
		}
	}
	if (ok)
		error = 0;

	if (displaydv == NULL) {
		DPRINTF(("%s: disconnecting from %s\n",
		       sc->sc_base.me_dv.dv_xname, odisplaydv->dv_xname));
	}

	rw_exit_read(&sc->sc_lock);

	return (error);
}
#endif /* NWSDISPLAY > 0 */

uint32_t
wsmux_get_layout(struct wsmux_softc *sc)
{
	return sc->sc_kbd_layout;
}

void
wsmux_set_layout(struct wsmux_softc *sc, uint32_t layout)
{
	if ((layout & (KB_DEFAULT | KB_NOENCODING)) == 0)
		sc->sc_kbd_layout = layout;
}

/*
 * Returns the depth of the longest chain of nested wsmux devices starting
 * from sc.
 */
int
wsmux_depth(struct wsmux_softc *sc)
{
	struct wsevsrc *me;
	int depth;
	int maxdepth = 0;

	rw_assert_anylock(&wsmux_tree_lock);

	rw_enter_read(&sc->sc_lock);
	TAILQ_FOREACH(me, &sc->sc_cld, me_next) {
		if (me->me_ops->type != WSMUX_MUX)
			continue;

		depth = wsmux_depth((struct wsmux_softc *)me);
		if (depth > maxdepth)
			maxdepth = depth;
	}
	rw_exit_read(&sc->sc_lock);

	return (maxdepth + 1);
}
