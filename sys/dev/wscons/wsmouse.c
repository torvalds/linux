/* $OpenBSD: wsmouse.c,v 1.76 2025/07/18 17:34:29 mvs Exp $ */
/* $NetBSD: wsmouse.c,v 1.35 2005/02/27 00:27:52 perry Exp $ */

/*
 * Copyright (c) 1996, 1997 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)ms.c	8.1 (Berkeley) 6/11/93
 */

/*
 * Copyright (c) 2015, 2016 Ulf Brosziewski
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Mouse driver.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/signalvar.h>
#include <sys/device.h>
#include <sys/vnode.h>
#include <sys/malloc.h>

#include <dev/wscons/wscons_features.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>
#include <dev/wscons/wseventvar.h>
#include <dev/wscons/wsmouseinput.h>

#include "wsmux.h"
#include "wsdisplay.h"
#include "wskbd.h"

#include <dev/wscons/wsmuxvar.h>

#if defined(WSMUX_DEBUG) && NWSMUX > 0
#define	DPRINTF(x)	if (wsmuxdebug) printf x
extern int wsmuxdebug;
#else
#define	DPRINTF(x)
#endif

struct wsmouse_softc {
	struct wsevsrc	sc_base;

	const struct wsmouse_accessops *sc_accessops;
	void		*sc_accesscookie;

	struct wsmouseinput sc_input;

	int		sc_refcnt;
	u_char		sc_dying;	/* device is being detached */
};

int	wsmouse_match(struct device *, void *, void *);
void	wsmouse_attach(struct device *, struct device *, void *);
int	wsmouse_detach(struct device *, int);
int	wsmouse_activate(struct device *, int);

int	wsmouse_do_ioctl(struct wsmouse_softc *, u_long, caddr_t,
			      int, struct proc *);

#if NWSMUX > 0
int	wsmouse_mux_open(struct wsevsrc *, struct wseventvar *);
int	wsmouse_mux_close(struct wsevsrc *);
#endif

int	wsmousedoioctl(struct device *, u_long, caddr_t, int,
			    struct proc *);
int	wsmousedoopen(struct wsmouse_softc *, struct wseventvar *);

struct cfdriver wsmouse_cd = {
	NULL, "wsmouse", DV_TTY
};

const struct cfattach wsmouse_ca = {
	sizeof (struct wsmouse_softc), wsmouse_match, wsmouse_attach,
	wsmouse_detach, wsmouse_activate
};

#if NWSMUX > 0
struct wssrcops wsmouse_srcops = {
	.type		= WSMUX_MOUSE,
	.dopen		= wsmouse_mux_open,
	.dclose		= wsmouse_mux_close,
	.dioctl		= wsmousedoioctl,
	.ddispioctl	= NULL,
	.dsetdisplay	= NULL,
};
#endif

/*
 * Print function (for parent devices).
 */
int
wsmousedevprint(void *aux, const char *pnp)
{

	if (pnp)
		printf("wsmouse at %s", pnp);
	return (UNCONF);
}

int
wsmouse_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

void
wsmouse_attach(struct device *parent, struct device *self, void *aux)
{
	struct wsmouse_softc *sc = (struct wsmouse_softc *)self;
	struct wsmousedev_attach_args *ap = aux;
#if NWSMUX > 0
	int mux, error;
#endif

	sc->sc_accessops = ap->accessops;
	sc->sc_accesscookie = ap->accesscookie;

	sc->sc_input.evar = &sc->sc_base.me_evp;

#if NWSMUX > 0
	sc->sc_base.me_ops = &wsmouse_srcops;
	mux = sc->sc_base.me_dv.dv_cfdata->wsmousedevcf_mux;
	if (mux >= 0) {
		error = wsmux_attach_sc(wsmux_getmux(mux), &sc->sc_base);
		if (error)
			printf(" attach error=%d", error);
		else
			printf(" mux %d", mux);
	}
#else
#if 0	/* not worth keeping, especially since the default value is not -1... */
	if (sc->sc_base.me_dv.dv_cfdata->wsmousedevcf_mux >= 0)
		printf(" (mux ignored)");
#endif
#endif	/* NWSMUX > 0 */

	printf("\n");
}

int
wsmouse_activate(struct device *self, int act)
{
	struct wsmouse_softc *sc = (struct wsmouse_softc *)self;

	if (act == DVACT_DEACTIVATE)
		sc->sc_dying = 1;
	return (0);
}

/*
 * Detach a mouse.  To keep track of users of the softc we keep
 * a reference count that's incremented while inside, e.g., read.
 * If the mouse is active and the reference count is > 0 (0 is the
 * normal state) we post an event and then wait for the process
 * that had the reference to wake us up again.  Then we blow away the
 * vnode and return (which will deallocate the softc).
 */
int
wsmouse_detach(struct device *self, int flags)
{
	struct wsmouse_softc *sc = (struct wsmouse_softc *)self;
	struct wseventvar *evar;
	int maj, mn;

#if NWSMUX > 0
	/* Tell parent mux we're leaving. */
	if (sc->sc_base.me_parent != NULL) {
		DPRINTF(("%s\n", __func__));
		wsmux_detach_sc(&sc->sc_base);
	}
#endif

	/* If we're open ... */
	evar = sc->sc_base.me_evp;
	if (evar != NULL) {
		if (--sc->sc_refcnt >= 0) {
			mtx_enter(&evar->ws_mtx);
			/* Wake everyone by generating a dummy event. */
			if (++evar->ws_put >= WSEVENT_QSIZE)
				evar->ws_put = 0;
			mtx_leave(&evar->ws_mtx);
			wsevent_wakeup(evar);
			/* Wait for processes to go away. */
			if (tsleep_nsec(sc, PZERO, "wsmdet", SEC_TO_NSEC(60)))
				printf("wsmouse_detach: %s didn't detach\n",
				       sc->sc_base.me_dv.dv_xname);
		}
	}

	/* locate the major number */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == wsmouseopen)
			break;

	/* Nuke the vnodes for any open instances (calls close). */
	mn = self->dv_unit;
	vdevgone(maj, mn, mn, VCHR);

	wsmouse_input_cleanup(&sc->sc_input);

	return (0);
}

int
wsmouseopen(dev_t dev, int flags, int mode, struct proc *p)
{
	struct wsmouse_softc *sc;
	struct wseventvar *evar;
	int error, unit;

	unit = minor(dev);
	if (unit >= wsmouse_cd.cd_ndevs ||	/* make sure it was attached */
	    (sc = wsmouse_cd.cd_devs[unit]) == NULL)
		return (ENXIO);

#if NWSMUX > 0
	DPRINTF(("%s: %s mux=%p\n", __func__, sc->sc_base.me_dv.dv_xname,
		 sc->sc_base.me_parent));
#endif

	if (sc->sc_dying)
		return (EIO);

	if ((flags & (FREAD | FWRITE)) == FWRITE)
		return (0);			/* always allow open for write
						   so ioctl() is possible. */

#if NWSMUX > 0
	if (sc->sc_base.me_parent != NULL) {
		/* Grab the mouse out of the greedy hands of the mux. */
		DPRINTF(("%s: detach\n", __func__));
		wsmux_detach_sc(&sc->sc_base);
	}
#endif

	if (sc->sc_base.me_evp != NULL)
		return (EBUSY);

	evar = &sc->sc_base.me_evar;
	if (wsevent_init(evar))
		return (EBUSY);

	error = wsmousedoopen(sc, evar);
	if (error)
		wsevent_fini(evar);
	return (error);
}

int
wsmouseclose(dev_t dev, int flags, int mode, struct proc *p)
{
	struct wsmouse_softc *sc =
	    (struct wsmouse_softc *)wsmouse_cd.cd_devs[minor(dev)];
	struct wseventvar *evar = sc->sc_base.me_evp;

	if ((flags & (FREAD | FWRITE)) == FWRITE)
		/* Not open for read */
		return (0);

	sc->sc_base.me_evp = NULL;
	(*sc->sc_accessops->disable)(sc->sc_accesscookie);
	wsevent_fini(evar);

#if NWSMUX > 0
	if (sc->sc_base.me_parent == NULL) {
		int mux, error;

		DPRINTF(("%s: attach\n", __func__));
		mux = sc->sc_base.me_dv.dv_cfdata->wsmousedevcf_mux;
		if (mux >= 0) {
			error = wsmux_attach_sc(wsmux_getmux(mux), &sc->sc_base);
			if (error)
				printf("%s: can't attach mux (error=%d)\n",
				    sc->sc_base.me_dv.dv_xname, error);
		}
	}
#endif

	return (0);
}

int
wsmousedoopen(struct wsmouse_softc *sc, struct wseventvar *evp)
{
	int error;

	/* The device could already be attached to a mux. */
	if (sc->sc_base.me_evp != NULL)
		return (EBUSY);
	sc->sc_base.me_evp = evp;

	wsmouse_input_reset(&sc->sc_input);

	/* enable the device, and punt if that's not possible */
	error = (*sc->sc_accessops->enable)(sc->sc_accesscookie);
	if (error)
		sc->sc_base.me_evp = NULL;
	return (error);
}

int
wsmouseread(dev_t dev, struct uio *uio, int flags)
{
	struct wsmouse_softc *sc = wsmouse_cd.cd_devs[minor(dev)];
	int error;

	if (sc->sc_dying)
		return (EIO);

#ifdef DIAGNOSTIC
	if (sc->sc_base.me_evp == NULL) {
		printf("wsmouseread: evp == NULL\n");
		return (EINVAL);
	}
#endif

	sc->sc_refcnt++;
	error = wsevent_read(sc->sc_base.me_evp, uio, flags);
	if (--sc->sc_refcnt < 0) {
		wakeup(sc);
		error = EIO;
	}
	return (error);
}

int
wsmouseioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return (wsmousedoioctl(wsmouse_cd.cd_devs[minor(dev)],
	    cmd, data, flag, p));
}

/* A wrapper around the ioctl() workhorse to make reference counting easy. */
int
wsmousedoioctl(struct device *dv, u_long cmd, caddr_t data, int flag,
    struct proc *p)
{
	struct wsmouse_softc *sc = (struct wsmouse_softc *)dv;
	int error;

	sc->sc_refcnt++;
	error = wsmouse_do_ioctl(sc, cmd, data, flag, p);
	if (--sc->sc_refcnt < 0)
		wakeup(sc);
	return (error);
}

int
wsmouse_param_ioctl(struct wsmouse_softc *sc,
    u_long cmd, struct wsmouse_param *params, u_int nparams)
{
	struct wsmouse_param *buf;
	int error, s, size;

	if (params == NULL || nparams > WSMOUSECFG_MAX)
		return (EINVAL);

	size = nparams * sizeof(struct wsmouse_param);
	buf = malloc(size, M_DEVBUF, M_WAITOK);
	if (buf == NULL)
		return (ENOMEM);

	if ((error = copyin(params, buf, size))) {
		free(buf, M_DEVBUF, size);
		return (error);
	}

	s = spltty();
	if (cmd == WSMOUSEIO_SETPARAMS) {
		if (wsmouse_set_params((struct device *) sc, buf, nparams))
			error = EINVAL;
	} else {
		if (wsmouse_get_params((struct device *) sc, buf, nparams))
			error = EINVAL;
		else
			error = copyout(buf, params, size);
	}
	splx(s);
	free(buf, M_DEVBUF, size);
	return (error);
}

int
wsmouse_do_ioctl(struct wsmouse_softc *sc, u_long cmd, caddr_t data, int flag,
    struct proc *p)
{
	struct wseventvar *evar;
	int error;

	if (sc->sc_dying)
		return (EIO);

	/*
	 * Try the generic ioctls that the wsmouse interface supports.
	 */

	switch (cmd) {
	case FIOASYNC:
	case FIOSETOWN:
	case TIOCSPGRP:
		if ((flag & FWRITE) == 0)
			return (EACCES);
	}

	switch (cmd) {
	case FIOASYNC:
		if (sc->sc_base.me_evp == NULL)
			return (EINVAL);
		mtx_enter(&sc->sc_base.me_evp->ws_mtx);
		sc->sc_base.me_evp->ws_async = *(int *)data != 0;
		mtx_leave(&sc->sc_base.me_evp->ws_mtx);
		return (0);

	case FIOGETOWN:
	case TIOCGPGRP:
		evar = sc->sc_base.me_evp;
		if (evar == NULL)
			return (EINVAL);
		sigio_getown(&evar->ws_sigio, cmd, data);
		return (0);

	case FIOSETOWN:
	case TIOCSPGRP:
		evar = sc->sc_base.me_evp;
		if (evar == NULL)
			return (EINVAL);
		return (sigio_setown(&evar->ws_sigio, cmd, data));

	case WSMOUSEIO_GETPARAMS:
	case WSMOUSEIO_SETPARAMS:
		return (wsmouse_param_ioctl(sc, cmd,
		    ((struct wsmouse_parameters *) data)->params,
		    ((struct wsmouse_parameters *) data)->nparams));
	}

	/*
	 * Try the mouse driver for WSMOUSEIO ioctls.  It returns -1
	 * if it didn't recognize the request.
	 */
	error = (*sc->sc_accessops->ioctl)(sc->sc_accesscookie, cmd,
	    data, flag, p);
	return (error != -1 ? error : ENOTTY);
}

int
wsmousekqfilter(dev_t dev, struct knote *kn)
{
	struct wsmouse_softc *sc = wsmouse_cd.cd_devs[minor(dev)];

	if (sc->sc_base.me_evp == NULL)
		return (ENXIO);
	return (wsevent_kqfilter(sc->sc_base.me_evp, kn));
}

#if NWSMUX > 0
int
wsmouse_mux_open(struct wsevsrc *me, struct wseventvar *evp)
{
	struct wsmouse_softc *sc = (struct wsmouse_softc *)me;

	return (wsmousedoopen(sc, evp));
}

int
wsmouse_mux_close(struct wsevsrc *me)
{
	struct wsmouse_softc *sc = (struct wsmouse_softc *)me;

	(*sc->sc_accessops->disable)(sc->sc_accesscookie);
	sc->sc_base.me_evp = NULL;

	return (0);
}

int
wsmouse_add_mux(int unit, struct wsmux_softc *muxsc)
{
	struct wsmouse_softc *sc;

	if (unit < 0 || unit >= wsmouse_cd.cd_ndevs ||
	    (sc = wsmouse_cd.cd_devs[unit]) == NULL)
		return (ENXIO);

	if (sc->sc_base.me_parent != NULL || sc->sc_base.me_evp != NULL)
		return (EBUSY);

	return (wsmux_attach_sc(muxsc, &sc->sc_base));
}
#endif	/* NWSMUX > 0 */

void
wsmouse_buttons(struct device *sc, u_int buttons)
{
	struct btn_state *btn = &((struct wsmouse_softc *) sc)->sc_input.btn;

	if (btn->sync)
		/* Restore the old state. */
		btn->buttons ^= btn->sync;

	btn->sync = btn->buttons ^ buttons;
	btn->buttons = buttons;
}

void
wsmouse_motion(struct device *sc, int dx, int dy, int dz, int dw)
{
	struct motion_state *motion =
	    &((struct wsmouse_softc *) sc)->sc_input.motion;

	motion->dx = dx;
	motion->dy = dy;
	motion->dz = dz;
	motion->dw = dw;
	if (dx || dy || dz || dw)
		motion->sync |= SYNC_DELTAS;
}

static inline void
set_x(struct position *pos, int x, u_int *sync, u_int mask)
{
	if (*sync & mask) {
		if (x == pos->x)
			return;
		pos->x -= pos->dx;
		pos->acc_dx -= pos->dx;
	}
	if ((pos->dx = x - pos->x)) {
		pos->x = x;
		if ((pos->dx > 0) == (pos->acc_dx > 0))
			pos->acc_dx += pos->dx;
		else
			pos->acc_dx = pos->dx;
		*sync |= mask;
	}
}

static inline void
set_y(struct position *pos, int y, u_int *sync, u_int mask)
{
	if (*sync & mask) {
		if (y == pos->y)
			return;
		pos->y -= pos->dy;
		pos->acc_dy -= pos->dy;
	}
	if ((pos->dy = y - pos->y)) {
		pos->y = y;
		if ((pos->dy > 0) == (pos->acc_dy > 0))
			pos->acc_dy += pos->dy;
		else
			pos->acc_dy = pos->dy;
		*sync |= mask;
	}
}

static inline void
cleardeltas(struct position *pos)
{
	pos->dx = pos->acc_dx = 0;
	pos->dy = pos->acc_dy = 0;
}

void
wsmouse_position(struct device *sc, int x, int y)
{
	struct motion_state *motion =
	    &((struct wsmouse_softc *) sc)->sc_input.motion;

	set_x(&motion->pos, x, &motion->sync, SYNC_X);
	set_y(&motion->pos, y, &motion->sync, SYNC_Y);
}

static inline int
normalized_pressure(struct wsmouseinput *input, int pressure)
{
	int limit = imax(input->touch.min_pressure, 1);

	if (pressure >= limit)
		return pressure;
	else
		return (pressure < 0 ? limit : 0);
}

void
wsmouse_touch(struct device *sc, int pressure, int contacts)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;
	struct touch_state *touch = &input->touch;

	pressure = normalized_pressure(input, pressure);
	contacts = (pressure ? imax(contacts, 1) : 0);

	if (pressure == 0 || pressure != touch->pressure) {
		/*
		 * pressure == 0: Drivers may report possibly arbitrary
		 * coordinates in this case; touch_update will correct them.
		 */
		touch->pressure = pressure;
		touch->sync |= SYNC_PRESSURE;
	}
	if (contacts != touch->contacts) {
		touch->contacts = contacts;
		touch->sync |= SYNC_CONTACTS;
	}
}

void
wsmouse_mtstate(struct device *sc, int slot, int x, int y, int pressure)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;
	struct mt_state *mt = &input->mt;
	struct mt_slot *mts;
	u_int bit;

	if (slot < 0 || slot >= mt->num_slots)
		return;

	bit = (1 << slot);
	mt->frame |= bit;

	mts = &mt->slots[slot];

	set_x(&mts->pos, x, mt->sync + MTS_X, bit);
	set_y(&mts->pos, y, mt->sync + MTS_Y, bit);

	/* Is this a new touch? */
	if ((mt->touches & bit) == (mt->sync[MTS_TOUCH] & bit))
		cleardeltas(&mts->pos);

	pressure = normalized_pressure(input, pressure);
	if (pressure != mts->pressure) {
		mts->pressure = pressure;
		mt->sync[MTS_PRESSURE] |= bit;

		if (pressure) {
			if ((mt->touches & bit) == 0) {
				mt->num_touches++;
				mt->touches |= bit;
				mt->sync[MTS_TOUCH] |= bit;

				mt->sync[MTS_X] |= bit;
				mt->sync[MTS_Y] |= bit;
			}
		} else if (mt->touches & bit) {
			mt->num_touches--;
			mt->touches ^= bit;
			mt->sync[MTS_TOUCH] |= bit;
			mt->ptr_mask &= mt->touches;
		}
	}
}

void
wsmouse_set(struct device *sc, enum wsmouseval type, int value, int aux)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;
	struct mt_slot *mts;

	if (WSMOUSE_IS_MT_CODE(type)) {
		if (aux < 0 || aux >= input->mt.num_slots)
			return;
		mts = &input->mt.slots[aux];
	}

	switch (type) {
	case WSMOUSE_REL_X:
		value += input->motion.pos.x; /* fall through */
	case WSMOUSE_ABS_X:
		wsmouse_position(sc, value, input->motion.pos.y);
		return;
	case WSMOUSE_REL_Y:
		value += input->motion.pos.y; /* fall through */
	case WSMOUSE_ABS_Y:
		wsmouse_position(sc, input->motion.pos.x, value);
		return;
	case WSMOUSE_PRESSURE:
		wsmouse_touch(sc, value, input->touch.contacts);
		return;
	case WSMOUSE_CONTACTS:
		/* Contact counts can be overridden by wsmouse_touch. */
		if (value != input->touch.contacts) {
			input->touch.contacts = value;
			input->touch.sync |= SYNC_CONTACTS;
		}
		return;
	case WSMOUSE_TOUCH_WIDTH:
		if (value != input->touch.width) {
			input->touch.width = value;
			input->touch.sync |= SYNC_TOUCH_WIDTH;
		}
		return;
	case WSMOUSE_MT_REL_X:
		value += mts->pos.x; /* fall through */
	case WSMOUSE_MT_ABS_X:
		wsmouse_mtstate(sc, aux, value, mts->pos.y, mts->pressure);
		return;
	case WSMOUSE_MT_REL_Y:
		value += mts->pos.y; /* fall through */
	case WSMOUSE_MT_ABS_Y:
		wsmouse_mtstate(sc, aux, mts->pos.x, value, mts->pressure);
		return;
	case WSMOUSE_MT_PRESSURE:
		wsmouse_mtstate(sc, aux, mts->pos.x, mts->pos.y, value);
		return;
	}
}

/* Make touch and motion state consistent. */
void
wsmouse_touch_update(struct wsmouseinput *input)
{
	struct motion_state *motion = &input->motion;
	struct touch_state *touch = &input->touch;

	if (touch->pressure == 0) {
		/*
		 * There may be zero coordinates, or coordinates of
		 * touches with pressure values below min_pressure.
		 */
		if (motion->sync & SYNC_POSITION) {
			/* Restore valid coordinates. */
			motion->pos.x -= motion->pos.dx;
			motion->pos.y -= motion->pos.dy;
			motion->sync &= ~SYNC_POSITION;
		}

		if (touch->prev_contacts == 0)
			touch->sync &= ~SYNC_PRESSURE;

	}

	if (touch->sync & SYNC_CONTACTS)
		/* Suppress pointer movement. */
		cleardeltas(&motion->pos);

	if ((touch->sync & SYNC_PRESSURE) && touch->min_pressure) {
		if (touch->pressure >= input->filter.pressure_hi)
			touch->min_pressure = input->filter.pressure_lo;
		else if (touch->pressure < input->filter.pressure_lo)
			touch->min_pressure = input->filter.pressure_hi;
	}
}

/* Normalize multitouch state. */
void
wsmouse_mt_update(struct wsmouseinput *input)
{
	int i;

	/*
	 * The same as above: There may be arbitrary coordinates if
	 * (pressure == 0). Clear the sync flags for touches that have
	 * been released.
	 */
	if (input->mt.frame & ~input->mt.touches) {
		for (i = MTS_X; i < MTS_SIZE; i++)
			input->mt.sync[i] &= input->mt.touches;
	}
}

/* Return TRUE if a coordinate update may be noise. */
int
wsmouse_hysteresis(struct wsmouseinput *input, struct position *pos)
{
	return (abs(pos->acc_dx) < input->filter.h.hysteresis
	    && abs(pos->acc_dy) < input->filter.v.hysteresis);
}

/*
 * Select the pointer-controlling MT slot.
 *
 * Pointer-control is assigned to slots with non-zero motion deltas if
 * at least one such slot exists. This function doesn't impose any
 * restrictions on the way drivers use wsmouse_mtstate(), it covers
 * partial, unordered, and "delta-filtered" input.
 *
 * The "cycle" is the set of slots with X/Y updates in previous sync
 * operations; it will be cleared and rebuilt whenever a slot that is
 * being updated is already a member. If a cycle ends that doesn't
 * contain the pointer-controlling slot, a new slot will be selected.
 */
void
wsmouse_ptr_ctrl(struct wsmouseinput *input)
{
	struct mt_state *mt = &input->mt;
	u_int updates;
	int select, slot;

	mt->prev_ptr = mt->ptr;

	if (mt->num_touches <= 1) {
		mt->ptr = mt->touches;
		mt->ptr_cycle = mt->ptr;
		return;
	}

	updates = (mt->sync[MTS_X] | mt->sync[MTS_Y]) & ~mt->sync[MTS_TOUCH];
	FOREACHBIT(updates, slot) {
		/*
		 * Touches that just produce noise are no problem if the
		 * frequency of zero deltas is high enough, but there might
		 * be no guarantee for that.
		 */
		if (wsmouse_hysteresis(input, &mt->slots[slot].pos))
			updates ^= (1 << slot);
	}

	/*
	 * If there is no pointer-controlling slot, or if it should be
	 * masked, select a new one.
	 */
	select = ((mt->ptr & mt->touches & ~mt->ptr_mask) == 0);

	/* Remove slots without coordinate deltas from the cycle. */
	mt->ptr_cycle &= ~(mt->frame ^ updates);

	if (mt->ptr_cycle & updates) {
		select |= ((mt->ptr_cycle & mt->ptr) == 0);
		mt->ptr_cycle = updates;
	} else {
		mt->ptr_cycle |= updates;
	}
	if (select) {
		if (mt->ptr_cycle & ~mt->ptr_mask)
			slot = ffs(mt->ptr_cycle & ~mt->ptr_mask) - 1;
		else if (mt->touches & ~mt->ptr_mask)
			slot = ffs(mt->touches & ~mt->ptr_mask) - 1;
		else
			slot = ffs(mt->touches) - 1;
		mt->ptr = (1 << slot);
	}
}

/* Derive touch and motion state from MT state. */
void
wsmouse_mt_convert(struct device *sc)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;
	struct mt_state *mt = &input->mt;
	struct mt_slot *mts;
	int slot, pressure;

	wsmouse_ptr_ctrl(input);

	if (mt->ptr) {
		slot = ffs(mt->ptr) - 1;
		mts = &mt->slots[slot];
		if (mts->pos.x != input->motion.pos.x)
			input->motion.sync |= SYNC_X;
		if (mts->pos.y != input->motion.pos.y)
			input->motion.sync |= SYNC_Y;
		if (mt->ptr != mt->prev_ptr)
			/* Suppress pointer movement. */
			mts->pos.dx = mts->pos.dy = 0;
		memcpy(&input->motion.pos, &mts->pos, sizeof(struct position));

		pressure = mts->pressure;
	} else {
		pressure = 0;
	}

	wsmouse_touch(sc, pressure, mt->num_touches);
}

void
wsmouse_evq_put(struct evq_access *evq, int ev_type, int ev_value)
{
	struct wscons_event *ev;
	int space;

	mtx_enter(&evq->evar->ws_mtx);
	space = evq->evar->ws_get - evq->put;
	mtx_leave(&evq->evar->ws_mtx);

	if (space != 1 && space != 1 - WSEVENT_QSIZE) {
		ev = &evq->evar->ws_q[evq->put++];
		evq->put %= WSEVENT_QSIZE;
		ev->type = ev_type;
		ev->value = ev_value;
		memcpy(&ev->time, &evq->ts, sizeof(struct timespec));
		evq->result |= EVQ_RESULT_SUCCESS;
	} else {
		evq->result = EVQ_RESULT_OVERFLOW;
	}
}


void
wsmouse_btn_sync(struct btn_state *btn, struct evq_access *evq)
{
	int button, ev_type;
	u_int bit, sync;

	for (sync = btn->sync; sync; sync ^= bit) {
		button = ffs(sync) - 1;
		bit = (1 << button);
		ev_type = (btn->buttons & bit) ? BTN_DOWN_EV : BTN_UP_EV;
		wsmouse_evq_put(evq, ev_type, button);
	}
}

/*
 * Scale with a [*.12] fixed-point factor and a remainder:
 */
static inline int
scale(int val, int factor, int *rmdr)
{
	val = val * factor + *rmdr;
	if (val >= 0) {
		*rmdr = val & 0xfff;
		return (val >> 12);
	} else {
		*rmdr = -(-val & 0xfff);
		return -(-val >> 12);
	}
}

void
wsmouse_motion_sync(struct wsmouseinput *input, struct evq_access *evq)
{
	struct motion_state *motion = &input->motion;
	struct axis_filter *h = &input->filter.h;
	struct axis_filter *v = &input->filter.v;
	int x, y, dx, dy, dz, dw;

	if (motion->sync & SYNC_DELTAS) {
		dx = h->inv ? -motion->dx : motion->dx;
		dy = v->inv ? -motion->dy : motion->dy;
		if (h->scale)
			dx = scale(dx, h->scale, &h->rmdr);
		if (v->scale)
			dy = scale(dy, v->scale, &v->rmdr);
		if (dx)
			wsmouse_evq_put(evq, DELTA_X_EV(input), dx);
		if (dy)
			wsmouse_evq_put(evq, DELTA_Y_EV(input), dy);
		if (motion->dz) {
			dz = (input->flags & REVERSE_SCROLLING)
			    ? -motion->dz : motion->dz;
			if (IS_TOUCHPAD(input))
				wsmouse_evq_put(evq, VSCROLL_EV, dz);
			else
				wsmouse_evq_put(evq, DELTA_Z_EV, dz);
		}
		if (motion->dw) {
			dw = (input->flags & REVERSE_SCROLLING)
			    ? -motion->dw : motion->dw;
			if (IS_TOUCHPAD(input))
				wsmouse_evq_put(evq, HSCROLL_EV, dw);
			else
				wsmouse_evq_put(evq, DELTA_W_EV, dw);
		}
	}
	if (motion->sync & SYNC_POSITION) {
		if (motion->sync & SYNC_X) {
			x = (h->inv ? h->inv - motion->pos.x : motion->pos.x);
			wsmouse_evq_put(evq, ABS_X_EV(input), x);
		}
		if (motion->sync & SYNC_Y) {
			y = (v->inv ? v->inv - motion->pos.y : motion->pos.y);
			wsmouse_evq_put(evq, ABS_Y_EV(input), y);
		}
		if (motion->pos.dx == 0 && motion->pos.dy == 0
		    && (input->flags & TPAD_NATIVE_MODE ))
			/* Suppress pointer motion. */
			wsmouse_evq_put(evq, WSCONS_EVENT_TOUCH_RESET, 0);
	}
}

void
wsmouse_touch_sync(struct wsmouseinput *input, struct evq_access *evq)
{
	struct touch_state *touch = &input->touch;

	if (touch->sync & SYNC_PRESSURE)
		wsmouse_evq_put(evq, ABS_Z_EV, touch->pressure);
	if (touch->sync & SYNC_CONTACTS)
		wsmouse_evq_put(evq, ABS_W_EV, touch->contacts);
	if ((touch->sync & SYNC_TOUCH_WIDTH)
	    && (input->flags & TPAD_NATIVE_MODE))
		wsmouse_evq_put(evq, WSCONS_EVENT_TOUCH_WIDTH, touch->width);
}

void
wsmouse_log_input(struct wsmouseinput *input, struct timespec *ts)
{
	struct motion_state *motion = &input->motion;
	int t_sync, mt_sync;

	t_sync = (input->touch.sync & SYNC_CONTACTS);
	mt_sync = (input->mt.frame && (input->mt.sync[MTS_TOUCH]
	    || input->mt.ptr != input->mt.prev_ptr));

	if (motion->sync || mt_sync || t_sync || input->btn.sync)
		printf("[%s-in][%04d]", DEVNAME(input), LOGTIME(ts));
	else
		return;

	if (motion->sync & SYNC_POSITION)
		printf(" abs:%d,%d", motion->pos.x, motion->pos.y);
	if (motion->sync & SYNC_DELTAS)
		printf(" rel:%d,%d,%d,%d", motion->dx, motion->dy,
		    motion->dz, motion->dw);
	if (mt_sync)
		printf(" mt:0x%02x:%d", input->mt.touches,
		    ffs(input->mt.ptr) - 1);
	else if (t_sync)
		printf(" t:%d", input->touch.contacts);
	if (input->btn.sync)
		printf(" btn:0x%02x", input->btn.buttons);
	printf("\n");
}

void
wsmouse_log_events(struct wsmouseinput *input, struct evq_access *evq)
{
	struct wscons_event *ev;
	int n;

	mtx_enter(&evq->evar->ws_mtx);
	n = evq->evar->ws_put;
	mtx_leave(&evq->evar->ws_mtx);

	if (n != evq->put) {
		printf("[%s-ev][%04d]", DEVNAME(input), LOGTIME(&evq->ts));
		while (n != evq->put) {
			ev = &evq->evar->ws_q[n++];
			n %= WSEVENT_QSIZE;
			printf(" %d:%d", ev->type, ev->value);
		}
		printf("\n");
	}
}

static inline void
clear_sync_flags(struct wsmouseinput *input)
{
	int i;

	input->btn.sync = 0;
	input->sbtn.sync = 0;
	input->motion.sync = 0;
	input->touch.sync = 0;
	input->touch.prev_contacts = input->touch.contacts;
	if (input->mt.frame) {
		input->mt.frame = 0;
		for (i = 0; i < MTS_SIZE; i++)
			input->mt.sync[i] = 0;
	}
}

void
wsmouse_input_sync(struct device *sc)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;
	struct evq_access evq;

	evq.evar = *input->evar;
	if (evq.evar == NULL)
		return;
	mtx_enter(&evq.evar->ws_mtx);
	evq.put = evq.evar->ws_put;
	mtx_leave(&evq.evar->ws_mtx);
	evq.result = EVQ_RESULT_NONE;
	getnanotime(&evq.ts);

	enqueue_randomness(input->btn.buttons
	    ^ input->motion.dx ^ input->motion.dy
	    ^ input->motion.pos.x ^ input->motion.pos.y
	    ^ input->motion.dz ^ input->motion.dw);

	if (input->mt.frame) {
		wsmouse_mt_update(input);
		wsmouse_mt_convert(sc);
	}
	if (input->touch.sync)
		wsmouse_touch_update(input);

	if (input->flags & LOG_INPUT)
		wsmouse_log_input(input, &evq.ts);

	if (input->flags & TPAD_COMPAT_MODE)
		wstpad_compat_convert(input, &evq);

	if (input->flags & RESYNC) {
		input->flags &= ~RESYNC;
		input->motion.sync &= SYNC_POSITION;
	}

	if (input->btn.sync)
		wsmouse_btn_sync(&input->btn, &evq);
	if (input->sbtn.sync)
		wsmouse_btn_sync(&input->sbtn, &evq);
	if (input->motion.sync)
		wsmouse_motion_sync(input, &evq);
	if (input->touch.sync)
		wsmouse_touch_sync(input, &evq);
	/* No MT events are generated yet. */

	if (evq.result == EVQ_RESULT_SUCCESS) {
		wsmouse_evq_put(&evq, WSCONS_EVENT_SYNC, 0);
		if (evq.result == EVQ_RESULT_SUCCESS) {
			if (input->flags & LOG_EVENTS) {
				wsmouse_log_events(input, &evq);
			}
			mtx_enter(&evq.evar->ws_mtx);
			evq.evar->ws_put = evq.put;
			mtx_leave(&evq.evar->ws_mtx);
			wsevent_wakeup(evq.evar);
		}
	}

	if (evq.result != EVQ_RESULT_OVERFLOW)
		clear_sync_flags(input);
	else
		input->flags |= RESYNC;
}

int
wsmouse_id_to_slot(struct device *sc, int id)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;
	struct mt_state *mt = &input->mt;
	int slot;

	if (mt->num_slots == 0)
		return (-1);

	FOREACHBIT(mt->touches, slot) {
		if (mt->slots[slot].id == id)
			return slot;
	}
	slot = ffs(~(mt->touches | mt->frame)) - 1;
	if (slot >= 0 && slot < mt->num_slots) {
		mt->frame |= 1 << slot;
		mt->slots[slot].id = id;
		return (slot);
	} else {
		return (-1);
	}
}

/*
 * Find a minimum-weight matching for an m-by-n matrix.
 *
 * m must be greater than or equal to n. The size of the buffer must be
 * at least 3m + 3n.
 *
 * On return, the first m elements of the buffer contain the row-to-
 * column mappings, i.e., buffer[i] is the column index for row i, or -1
 * if there is no assignment for that row (which may happen if n < m).
 *
 * Wrong results because of overflows will not occur with input values
 * in the range of 0 to INT_MAX / 2 inclusive.
 *
 * The function applies the Dinic-Kronrod algorithm. It is not modern or
 * popular, but it seems to be a good choice for small matrices at least.
 * The original form of the algorithm is modified as follows: There is no
 * initial search for row minima, the initial assignments are in a
 * "virtual" column with the index -1 and zero values. This permits inputs
 * with n < m, and it simplifies the reassignments.
 */
void
wsmouse_matching(int *matrix, int m, int n, int *buffer)
{
	int i, j, k, d, e, row, col, delta;
	int *p;
	int *r2c = buffer;	/* row-to-column assignments */
	int *red = r2c + m;	/* reduced values of the assignments */
	int *mc = red + m;	/* row-wise minimal elements of cs */
	int *cs = mc + m;	/* the column set */
	int *c2r = cs + n;	/* column-to-row assignments in cs */
	int *cd = c2r + n;	/* column deltas (reduction) */

	for (p = r2c; p < red; *p++ = -1) {}
	for (; p < mc; *p++ = 0) {}
	for (col = 0; col < n; col++) {
		delta = INT_MAX;
		row = 0;
		for (i = 0, p = matrix + col; i < m; i++, p += n) {
			d = *p - red[i];
			if (d < delta || (d == delta && r2c[i] < 0)) {
				delta = d;
				row = i;
			}
		}
		cd[col] = delta;
		if (r2c[row] < 0) {
			r2c[row] = col;
			continue;
		}
		for (p = mc; p < cs; *p++ = col) {}
		for (k = 0; (j = r2c[row]) >= 0;) {
			cs[k++] = j;
			c2r[j] = row;
			mc[row] -= n;
			delta = INT_MAX;
			for (i = 0, p = matrix; i < m; i++, p += n)
				if (mc[i] >= 0) {
					d = p[mc[i]] - cd[mc[i]];
					e = p[j] - cd[j];
					if (e < d) {
						d = e;
						mc[i] = j;
					}
					d -= red[i];
					if (d < delta || (d == delta
					    && r2c[i] < 0)) {
						delta = d;
						row = i;
					}
				}
			cd[col] += delta;
			for (i = 0; i < k; i++) {
				cd[cs[i]] += delta;
				red[c2r[cs[i]]] -= delta;
			}
		}
		for (j = mc[row]; (r2c[row] = j) != col;) {
			row = c2r[j];
			j = mc[row] + n;
		}
	}
}

/*
 * Assign slot numbers to the points in the pt array, and update all slots by
 * calling wsmouse_mtstate internally.  The slot numbers are passed to the
 * caller in the pt->slot fields.
 *
 * The slot assignment pairs the points with points of the previous frame in
 * such a way that the sum of the squared distances is minimal.  Using
 * squares instead of simple distances favours assignments with more uniform
 * distances, and it is faster.
 */
void
wsmouse_mtframe(struct device *sc, struct mtpoint *pt, int size)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;
	struct mt_state *mt = &input->mt;
	int i, j, m, n, dx, dy, slot, maxdist;
	int *p, *r2c, *c2r;
	u_int touches;

	if (mt->num_slots == 0 || mt->matrix == NULL)
		return;

	size = imax(0, imin(size, mt->num_slots));
	p = mt->matrix;
	touches = mt->touches;
	if (mt->num_touches >= size) {
		FOREACHBIT(touches, slot)
			for (i = 0; i < size; i++) {
				dx = pt[i].x - mt->slots[slot].pos.x;
				dy = pt[i].y - mt->slots[slot].pos.y;
				*p++ = dx * dx + dy * dy;
			}
		m = mt->num_touches;
		n = size;
	} else {
		for (i = 0; i < size; i++)
			FOREACHBIT(touches, slot) {
				dx = pt[i].x - mt->slots[slot].pos.x;
				dy = pt[i].y - mt->slots[slot].pos.y;
				*p++ = dx * dx + dy * dy;
			}
		m = size;
		n = mt->num_touches;
	}
	wsmouse_matching(mt->matrix, m, n, p);

	r2c = p;
	c2r = p + m;
	maxdist = input->filter.tracking_maxdist;
	maxdist = (maxdist ? maxdist * maxdist : INT_MAX);
	for (i = 0, p = mt->matrix; i < m; i++, p += n)
		if ((j = r2c[i]) >= 0) {
			if (p[j] <= maxdist)
				c2r[j] = i;
			else
				c2r[j] = r2c[i] = -1;
		}

	p = (n == size ? c2r : r2c);
	for (i = 0; i < size; i++)
		if (*p++ < 0) {
			slot = ffs(~(mt->touches | mt->frame)) - 1;
			if (slot < 0 || slot >= mt->num_slots)
				break;
			wsmouse_mtstate(sc, slot,
			    pt[i].x, pt[i].y, pt[i].pressure);
			pt[i].slot = slot;
		}

	p = (n == size ? r2c : c2r);
	FOREACHBIT(touches, slot)
		if ((i = *p++) >= 0) {
			wsmouse_mtstate(sc, slot,
			    pt[i].x, pt[i].y, pt[i].pressure);
			pt[i].slot = slot;
		} else {
			wsmouse_mtstate(sc, slot, 0, 0, 0);
		}
}

static inline void
free_mt_slots(struct wsmouseinput *input)
{
	int n, size;

	if ((n = input->mt.num_slots)) {
		size = n * sizeof(struct mt_slot);
		if (input->flags & MT_TRACKING)
			size += MATRIX_SIZE(n);
		input->mt.num_slots = 0;
		free(input->mt.slots, M_DEVBUF, size);
		input->mt.slots = NULL;
		input->mt.matrix = NULL;
	}
}

/* Allocate the MT slots and, if necessary, the buffers for MT tracking. */
int
wsmouse_mt_init(struct device *sc, int num_slots, int tracking)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;
	int n, size;

	if (num_slots == input->mt.num_slots
	    && (!tracking == ((input->flags & MT_TRACKING) == 0)))
		return (0);

	free_mt_slots(input);

	if (tracking)
		input->flags |= MT_TRACKING;
	else
		input->flags &= ~MT_TRACKING;
	n = imin(imax(num_slots, 0), WSMOUSE_MT_SLOTS_MAX);
	if (n) {
		size = n * sizeof(struct mt_slot);
		if (input->flags & MT_TRACKING)
			size += MATRIX_SIZE(n);
		input->mt.slots = malloc(size, M_DEVBUF, M_WAITOK | M_ZERO);
		if (input->mt.slots != NULL) {
			if (input->flags & MT_TRACKING)
				input->mt.matrix = (int *)
				    (input->mt.slots + n);
			input->mt.num_slots = n;
			return (0);
		}
	}
	return (-1);
}

int
wsmouse_get_params(struct device *sc,
    struct wsmouse_param *params, u_int nparams)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;
	int i, key, error = 0;

	for (i = 0; i < nparams; i++) {
		key = params[i].key;
		switch (key) {
		case WSMOUSECFG_DX_SCALE:
			params[i].value = input->filter.h.scale;
			break;
		case WSMOUSECFG_DY_SCALE:
			params[i].value = input->filter.v.scale;
			break;
		case WSMOUSECFG_PRESSURE_LO:
			params[i].value = input->filter.pressure_lo;
			break;
		case WSMOUSECFG_PRESSURE_HI:
			params[i].value = input->filter.pressure_hi;
			break;
		case WSMOUSECFG_TRKMAXDIST:
			params[i].value = input->filter.tracking_maxdist;
			break;
		case WSMOUSECFG_SWAPXY:
			params[i].value = input->filter.swapxy;
			break;
		case WSMOUSECFG_X_INV:
			params[i].value = input->filter.h.inv;
			break;
		case WSMOUSECFG_Y_INV:
			params[i].value = input->filter.v.inv;
			break;
		case WSMOUSECFG_REVERSE_SCROLLING:
			params[i].value = !!(input->flags & REVERSE_SCROLLING);
			break;
		case WSMOUSECFG_DX_MAX:
			params[i].value = input->filter.h.dmax;
			break;
		case WSMOUSECFG_DY_MAX:
			params[i].value = input->filter.v.dmax;
			break;
		case WSMOUSECFG_X_HYSTERESIS:
			params[i].value = input->filter.h.hysteresis;
			break;
		case WSMOUSECFG_Y_HYSTERESIS:
			params[i].value = input->filter.v.hysteresis;
			break;
		case WSMOUSECFG_DECELERATION:
			params[i].value = input->filter.dclr;
			break;
		case WSMOUSECFG_STRONG_HYSTERESIS:
			params[i].value = 0; /* The feature has been removed. */
			break;
		case WSMOUSECFG_SMOOTHING:
			params[i].value =
			    input->filter.mode & SMOOTHING_MASK;
			break;
		case WSMOUSECFG_LOG_INPUT:
			params[i].value = !!(input->flags & LOG_INPUT);
			break;
		case WSMOUSECFG_LOG_EVENTS:
			params[i].value = !!(input->flags & LOG_EVENTS);
			break;
		default:
			error = wstpad_get_param(input, key, &params[i].value);
			if (error != 0)
				return (error);
			break;
		}
	}

	return (0);
}

int
wsmouse_set_params(struct device *sc,
    const struct wsmouse_param *params, u_int nparams)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;
	int i, val, key, needreset = 0, error = 0;

	for (i = 0; i < nparams; i++) {
		key = params[i].key;
		val = params[i].value;
		switch (params[i].key) {
		case WSMOUSECFG_PRESSURE_LO:
			input->filter.pressure_lo = val;
			if (val > input->filter.pressure_hi)
				input->filter.pressure_hi = val;
			input->touch.min_pressure = input->filter.pressure_hi;
			break;
		case WSMOUSECFG_PRESSURE_HI:
			input->filter.pressure_hi = val;
			if (val < input->filter.pressure_lo)
				input->filter.pressure_lo = val;
			input->touch.min_pressure = val;
			break;
		case WSMOUSECFG_X_HYSTERESIS:
			input->filter.h.hysteresis = val;
			break;
		case WSMOUSECFG_Y_HYSTERESIS:
			input->filter.v.hysteresis = val;
			break;
		case WSMOUSECFG_DECELERATION:
			input->filter.dclr = val;
			wstpad_init_deceleration(input);
			break;
		case WSMOUSECFG_DX_SCALE:
			input->filter.h.scale = val;
			break;
		case WSMOUSECFG_DY_SCALE:
			input->filter.v.scale = val;
			break;
		case WSMOUSECFG_TRKMAXDIST:
			input->filter.tracking_maxdist = val;
			break;
		case WSMOUSECFG_SWAPXY:
			input->filter.swapxy = val;
			break;
		case WSMOUSECFG_X_INV:
			input->filter.h.inv = val;
			break;
		case WSMOUSECFG_Y_INV:
			input->filter.v.inv = val;
			break;
		case WSMOUSECFG_REVERSE_SCROLLING:
			if (val)
				input->flags |= REVERSE_SCROLLING;
			else
				input->flags &= ~REVERSE_SCROLLING;
			break;
		case WSMOUSECFG_DX_MAX:
			input->filter.h.dmax = val;
			break;
		case WSMOUSECFG_DY_MAX:
			input->filter.v.dmax = val;
			break;
		case WSMOUSECFG_SMOOTHING:
			input->filter.mode &= ~SMOOTHING_MASK;
			input->filter.mode |= (val & SMOOTHING_MASK);
			break;
		case WSMOUSECFG_LOG_INPUT:
			if (val)
				input->flags |= LOG_INPUT;
			else
				input->flags &= ~LOG_INPUT;
			break;
		case WSMOUSECFG_LOG_EVENTS:
			if (val)
				input->flags |= LOG_EVENTS;
			else
				input->flags &= ~LOG_EVENTS;
			break;
		default:
			needreset = 1;
			error = wstpad_set_param(input, key, val);
			if (error != 0)
				return (error);
			break;
		}
	}

	/* Reset soft-states if touchpad parameters changed */
	if (needreset) {
		wstpad_reset(input);
		return (wstpad_configure(input));
	}

	return (0);
}

int
wsmouse_set_mode(struct device *sc, int mode)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;

	if (mode == WSMOUSE_COMPAT) {
		input->flags &= ~TPAD_NATIVE_MODE;
		input->flags |= TPAD_COMPAT_MODE;
		return (0);
	} else if (mode == WSMOUSE_NATIVE) {
		input->flags &= ~TPAD_COMPAT_MODE;
		input->flags |= TPAD_NATIVE_MODE;
		return (0);
	}
	return (-1);
}

struct wsmousehw *
wsmouse_get_hw(struct device *sc)
{
	return &((struct wsmouse_softc *) sc)->sc_input.hw;
}

/*
 * Create a default configuration based on the hardware infos in the 'hw'
 * fields. The 'params' argument is optional, hardware drivers can use it
 * to modify the generic defaults. Up to now this function is only useful
 * for touchpads.
 */
int
wsmouse_configure(struct device *sc,
    struct wsmouse_param *params, u_int nparams)
{
	struct wsmouseinput *input = &((struct wsmouse_softc *) sc)->sc_input;
	int error;

	if (!(input->flags & CONFIGURED)) {
		if (input->hw.x_max && input->hw.y_max) {
			if (input->hw.flags & WSMOUSEHW_LR_DOWN) {
				input->filter.v.inv =
				    input->hw.y_max + input->hw.y_min;
			}
		}
		input->filter.ratio = 1 << 12;
		if (input->hw.h_res > 0 && input->hw.v_res > 0) {
			input->filter.ratio *= input->hw.h_res;
			input->filter.ratio /= input->hw.v_res;
		}
		if (wsmouse_mt_init(sc, input->hw.mt_slots,
		    (input->hw.flags & WSMOUSEHW_MT_TRACKING))) {
			printf("wsmouse_configure: "
			    "MT initialization failed.\n");
			return (-1);
		}
		if (IS_TOUCHPAD(input) && wstpad_configure(input)) {
			printf("wstpad_configure: "
			    "Initialization failed.\n");
			return (-1);
		}
		input->flags |= CONFIGURED;
		if (params != NULL) {
			if ((error = wsmouse_set_params(sc, params, nparams)))
				return (error);
		}
	}
	if (IS_TOUCHPAD(input))
		wsmouse_set_mode(sc, WSMOUSE_COMPAT);

	return (0);
}


void
wsmouse_input_reset(struct wsmouseinput *input)
{
	int num_slots, *matrix;
	struct mt_slot *slots;

	memset(&input->btn, 0, sizeof(struct btn_state));
	memset(&input->motion, 0, sizeof(struct motion_state));
	memset(&input->touch, 0, sizeof(struct touch_state));
	input->touch.min_pressure = input->filter.pressure_hi;
	if ((num_slots = input->mt.num_slots)) {
		slots = input->mt.slots;
		matrix = input->mt.matrix;
		memset(&input->mt, 0, sizeof(struct mt_state));
		memset(slots, 0, num_slots * sizeof(struct mt_slot));
		input->mt.num_slots = num_slots;
		input->mt.slots = slots;
		input->mt.matrix = matrix;
	}
	if (input->tp != NULL)
		wstpad_reset(input);
}

void
wsmouse_input_cleanup(struct wsmouseinput *input)
{
	if (input->tp != NULL)
		wstpad_cleanup(input);

	free_mt_slots(input);
}
