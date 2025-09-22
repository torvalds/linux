/*	$OpenBSD: ucom.c,v 1.79 2024/05/23 03:21:09 jsg Exp $ */
/*	$NetBSD: ucom.c,v 1.49 2003/01/01 00:10:25 thorpej Exp $	*/

/*
 * Copyright (c) 1998, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
/*
 * This code is very heavily based on the 16550 driver, com.c.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/rwlock.h>
#include <sys/ioctl.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <dev/usb/usb.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/uhidev.h>

#include <dev/usb/ucomvar.h>

#include "ucom.h"

#if NUCOM > 0

#ifdef UCOM_DEBUG
#define DPRINTFN(n, x)	do { if (ucomdebug > (n)) printf x; } while (0)
int ucomdebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x) DPRINTFN(0, x)

#define	UCOMUNIT_MASK		0x7f
#define	UCOMCUA_MASK		0x80

#define LINESW(tp, func)	(linesw[(tp)->t_line].func)

#define	UCOMUNIT(x)		(minor(x) & UCOMUNIT_MASK)
#define	UCOMCUA(x)		(minor(x) & UCOMCUA_MASK)

#define ROUTEROOTPORT(_x)	((_x) & 0xff)
#define ROUTESTRING(_x)		(((_x) >> 8) & 0xfffff)

struct ucom_softc {
	struct device		sc_dev;		/* base device */

	struct usbd_device	*sc_uparent;	/* USB device */
	struct uhidev_softc	*sc_uhidev;	/* hid device (if deeper) */

	struct usbd_interface	*sc_iface;	/* data interface */

	int			sc_bulkin_no;	/* bulk in endpoint address */
	struct usbd_pipe	*sc_bulkin_pipe;/* bulk in pipe */
	struct usbd_xfer	*sc_ixfer;	/* read request */
	u_char			*sc_ibuf;	/* read buffer */
	u_int			sc_ibufsize;	/* read buffer size */
	u_int			sc_ibufsizepad;	/* read buffer size padded */

	int			sc_bulkout_no;	/* bulk out endpoint address */
	struct usbd_pipe	*sc_bulkout_pipe;/* bulk out pipe */
	struct usbd_xfer	*sc_oxfer;	/* write request */
	u_char			*sc_obuf;	/* write buffer */
	u_int			sc_obufsize;	/* write buffer size */
	u_int			sc_opkthdrlen;	/* header length of
						 * output packet */

	struct usbd_pipe	*sc_ipipe;	/* hid interrupt input pipe */
	struct usbd_pipe	*sc_opipe;	/* hid interrupt pipe */

	const struct ucom_methods *sc_methods;
	void                    *sc_parent;
	int			sc_portno;

	struct tty		*sc_tty;	/* our tty */
	u_char			sc_lsr;
	u_char			sc_msr;
	u_char			sc_mcr;
	u_char			sc_tx_stopped;
	int			sc_swflags;

	u_char			sc_cua;
	int			sc_error;

	struct rwlock		sc_lock;	/* lock during open */
	int			sc_open;
	int			sc_refcnt;
};

void	ucom_cleanup(struct ucom_softc *);
void	ucom_hwiflow(struct ucom_softc *);
int	ucomparam(struct tty *, struct termios *);
void	ucomstart(struct tty *);
void	ucom_shutdown(struct ucom_softc *);
int	ucom_do_open(dev_t, int, int, struct proc *);
int	ucom_do_ioctl(struct ucom_softc *, u_long, caddr_t, int, struct proc *);
int	ucom_do_close(struct ucom_softc *, int, int , struct proc *);
void	ucom_dtr(struct ucom_softc *, int);
void	ucom_rts(struct ucom_softc *, int);
void	ucom_break(struct ucom_softc *, int);
usbd_status ucomstartread(struct ucom_softc *);
void	ucomreadcb(struct usbd_xfer *, void *, usbd_status);
void	ucomwritecb(struct usbd_xfer *, void *, usbd_status);
void	tiocm_to_ucom(struct ucom_softc *, u_long, int);
int	ucom_to_tiocm(struct ucom_softc *);
void	ucom_lock(struct ucom_softc *);
void	ucom_unlock(struct ucom_softc *);

int ucom_match(struct device *, void *, void *);
void ucom_attach(struct device *, struct device *, void *);
int ucom_detach(struct device *, int);

struct cfdriver ucom_cd = {
	NULL, "ucom", DV_TTY
};

const struct cfattach ucom_ca = {
	sizeof(struct ucom_softc),
	ucom_match,
	ucom_attach,
	ucom_detach,
};

static int ucom_change;
struct rwlock sysctl_ucomlock = RWLOCK_INITIALIZER("sysctlulk");

void
ucom_lock(struct ucom_softc *sc)
{
	rw_enter_write(&sc->sc_lock);
}

void
ucom_unlock(struct ucom_softc *sc)
{
	rw_exit_write(&sc->sc_lock);
}

int
ucom_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

void
ucom_attach(struct device *parent, struct device *self, void *aux)
{
	char path[32];	/* "usb000.000.00000.000" */
	struct ucom_softc *sc = (struct ucom_softc *)self;
	struct ucom_attach_args *uca = aux;
	struct tty *tp;
	uint32_t route;
	uint8_t bus, ifaceno;

	if (uca->info != NULL)
		printf(", %s", uca->info);

	sc->sc_uparent = uca->device;
	sc->sc_iface = uca->iface;
	sc->sc_bulkout_no = uca->bulkout;
	sc->sc_bulkin_no = uca->bulkin;
	sc->sc_uhidev = uca->uhidev;
	sc->sc_ibufsize = uca->ibufsize;
	sc->sc_ibufsizepad = uca->ibufsizepad;
	sc->sc_obufsize = uca->obufsize;
	sc->sc_opkthdrlen = uca->opkthdrlen;
	sc->sc_methods = uca->methods;
	sc->sc_parent = uca->arg;
	sc->sc_portno = uca->portno;

	if (usbd_get_location(sc->sc_uparent, sc->sc_iface, &bus, &route,
	    &ifaceno) == 0) {
		if (snprintf(path, sizeof(path), "usb%u.%u.%05x.%u", bus,
		    ROUTEROOTPORT(route), ROUTESTRING(route), ifaceno) <
		    sizeof(path))
			printf(": %s", path);
	}
	printf("\n");

	tp = ttymalloc(1000000);
	tp->t_oproc = ucomstart;
	tp->t_param = ucomparam;
	sc->sc_tty = tp;
	sc->sc_cua = 0;

	ucom_change = 1;
	rw_init(&sc->sc_lock, "ucomlk");
}

int
ucom_detach(struct device *self, int flags)
{
	struct ucom_softc *sc = (struct ucom_softc *)self;
	struct tty *tp = sc->sc_tty;
	int maj, mn;
	int s;

	DPRINTF(("ucom_detach: sc=%p flags=%d tp=%p, pipe=%d,%d\n",
		 sc, flags, tp, sc->sc_bulkin_no, sc->sc_bulkout_no));

	if (sc->sc_bulkin_pipe != NULL) {
		usbd_close_pipe(sc->sc_bulkin_pipe);
		sc->sc_bulkin_pipe = NULL;
	}
	if (sc->sc_bulkout_pipe != NULL) {
		usbd_close_pipe(sc->sc_bulkout_pipe);
		sc->sc_bulkout_pipe = NULL;
	}
	if (sc->sc_ixfer != NULL) {
		if (sc->sc_bulkin_no != -1) {
			usbd_free_buffer(sc->sc_ixfer);
			sc->sc_ibuf = NULL;
			usbd_free_xfer(sc->sc_ixfer);
		}
		sc->sc_ixfer = NULL;
	}
	if (sc->sc_oxfer != NULL) {
		usbd_free_buffer(sc->sc_oxfer);
		sc->sc_obuf = NULL;
		if (sc->sc_bulkin_no != -1)
			usbd_free_xfer(sc->sc_oxfer);
		sc->sc_oxfer = NULL;
	}

	s = splusb();
	if (--sc->sc_refcnt >= 0) {
		/* Wake up anyone waiting */
		if (tp != NULL) {
			CLR(tp->t_state, TS_CARR_ON);
			CLR(tp->t_cflag, CLOCAL | MDMBUF);
			ttyflush(tp, FREAD|FWRITE);
		}
		usb_detach_wait(&sc->sc_dev);
	}
	splx(s);

	/* locate the major number */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == ucomopen)
			break;

	/* Nuke the vnodes for any open instances. */
	mn = self->dv_unit;
	DPRINTF(("ucom_detach: maj=%d mn=%d\n", maj, mn));
	vdevgone(maj, mn, mn, VCHR);
	vdevgone(maj, mn | UCOMCUA_MASK, mn | UCOMCUA_MASK, VCHR);

	/* Detach and free the tty. */
	if (tp != NULL) {
		(*LINESW(tp, l_close))(tp, FNONBLOCK, curproc);
		s = spltty();
		CLR(tp->t_state, TS_BUSY | TS_FLUSH);
		ttyclose(tp);
		splx(s);
		ttyfree(tp);
		sc->sc_tty = NULL;
	}

	ucom_change = 1;
	return (0);
}

void
ucom_shutdown(struct ucom_softc *sc)
{
	struct tty *tp = sc->sc_tty;

	DPRINTF(("ucom_shutdown\n"));
	/*
	 * Hang up if necessary.  Wait a bit, so the other side has time to
	 * notice even if we immediately open the port again.
	 */
	if (ISSET(tp->t_cflag, HUPCL)) {
		ucom_dtr(sc, 0);
		ucom_rts(sc, 0);
		tsleep_nsec(sc, TTIPRI, ttclos, SEC_TO_NSEC(1));
	}
}

int
ucomopen(dev_t dev, int flag, int mode, struct proc *p)
{
	int unit = UCOMUNIT(dev);
	struct ucom_softc *sc;
	int error;

	if (unit >= ucom_cd.cd_ndevs)
		return (ENXIO);
	sc = ucom_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	sc->sc_error = 0;

	if (usbd_is_dying(sc->sc_uparent))
		return (EIO);

	if (ISSET(sc->sc_dev.dv_flags, DVF_ACTIVE) == 0)
		return (ENXIO);

	sc->sc_refcnt++;
	error = ucom_do_open(dev, flag, mode, p);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);

	return (error);
}

int
ucom_do_open(dev_t dev, int flag, int mode, struct proc *p)
{
	struct ucom_softc *sc = ucom_cd.cd_devs[UCOMUNIT(dev)];
	usbd_status err;
	struct tty *tp;
	struct termios t;
	int error, s;

	/* open the pipes if this is the first open */
	ucom_lock(sc);
	s = splusb();
	if (sc->sc_open == 0) {
		DPRINTF(("ucomopen: open pipes in=%d out=%d\n",
		    sc->sc_bulkin_no, sc->sc_bulkout_no));
		DPRINTF(("ucomopen: hid %p pipes in=%p out=%p\n",
		    sc->sc_uhidev, sc->sc_ipipe, sc->sc_opipe));

		if (sc->sc_bulkin_no != -1) {

			/* Open the bulk pipes */
			err = usbd_open_pipe(sc->sc_iface, sc->sc_bulkin_no, 0,
			    &sc->sc_bulkin_pipe);
			if (err) {
				DPRINTF(("%s: open bulk out error (addr %d), err=%s\n",
				    sc->sc_dev.dv_xname, sc->sc_bulkin_no,
				    usbd_errstr(err)));
				error = EIO;
				goto fail_0;
			}
			err = usbd_open_pipe(sc->sc_iface, sc->sc_bulkout_no,
			    USBD_EXCLUSIVE_USE, &sc->sc_bulkout_pipe);
			if (err) {
				DPRINTF(("%s: open bulk in error (addr %d), err=%s\n",
				    sc->sc_dev.dv_xname, sc->sc_bulkout_no,
				    usbd_errstr(err)));
				error = EIO;
				goto fail_1;
			}

			/* Allocate a request and an input buffer and start reading. */
			sc->sc_ixfer = usbd_alloc_xfer(sc->sc_uparent);
			if (sc->sc_ixfer == NULL) {
				error = ENOMEM;
				goto fail_2;
			}

			sc->sc_ibuf = usbd_alloc_buffer(sc->sc_ixfer,
			    sc->sc_ibufsizepad);
			if (sc->sc_ibuf == NULL) {
				error = ENOMEM;
				goto fail_2;
			}

			sc->sc_oxfer = usbd_alloc_xfer(sc->sc_uparent);
			if (sc->sc_oxfer == NULL) {
				error = ENOMEM;
				goto fail_3;
			}
		} else {
			/*
			 * input/output pipes and xfers already allocated
			 * as is the input buffer.
			 */
			sc->sc_ipipe = sc->sc_uhidev->sc_ipipe;
			sc->sc_ixfer = sc->sc_uhidev->sc_ixfer;
			sc->sc_opipe = sc->sc_uhidev->sc_opipe;
			sc->sc_oxfer = sc->sc_uhidev->sc_oxfer;
		}

		sc->sc_obuf = usbd_alloc_buffer(sc->sc_oxfer,
		    sc->sc_obufsize + sc->sc_opkthdrlen);
		if (sc->sc_obuf == NULL) {
			error = ENOMEM;
			goto fail_4;
		}

		if (sc->sc_methods->ucom_open != NULL) {
			error = sc->sc_methods->ucom_open(sc->sc_parent,
			    sc->sc_portno);
			if (error) {
				ucom_cleanup(sc);
				splx(s);
				ucom_unlock(sc);
				return (error);
			}
		}

		ucom_status_change(sc);

		ucomstartread(sc);
		sc->sc_open = 1;
	}
	splx(s);
	s = spltty();
	ucom_unlock(sc);
	tp = sc->sc_tty;
	splx(s);

	DPRINTF(("ucomopen: unit=%d, tp=%p\n", UCOMUNIT(dev), tp));

	tp->t_dev = dev;
	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		SET(tp->t_state, TS_WOPEN);
		ttychars(tp);

		/*
		 * Initialize the termios status to the defaults.  Add in the
		 * sticky bits from TIOCSFLAGS.
		 */
		t.c_ispeed = 0;
		t.c_ospeed = TTYDEF_SPEED;
		t.c_cflag = TTYDEF_CFLAG;
		if (ISSET(sc->sc_swflags, TIOCFLAG_CLOCAL))
			SET(t.c_cflag, CLOCAL);
		if (ISSET(sc->sc_swflags, TIOCFLAG_CRTSCTS))
			SET(t.c_cflag, CRTSCTS);
		if (ISSET(sc->sc_swflags, TIOCFLAG_MDMBUF))
			SET(t.c_cflag, MDMBUF);

		/* Make sure ucomparam() will do something. */
		tp->t_ospeed = 0;
		(void) ucomparam(tp, &t);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;

		s = spltty();
		ttsetwater(tp);

		/*
		 * Turn on DTR.  We must always do this, even if carrier is not
		 * present, because otherwise we'd have to use TIOCSDTR
		 * immediately after setting CLOCAL, which applications do not
		 * expect.  We always assert DTR while the device is open
		 * unless explicitly requested to deassert it.
		 */
		ucom_dtr(sc, 1);
		/* When not using CRTSCTS, RTS follows DTR. */
		if (!ISSET(t.c_cflag, CRTSCTS))
			ucom_rts(sc, 1);

		/* XXX CLR(sc->sc_rx_flags, RX_ANY_BLOCK);*/
		ucom_hwiflow(sc);

		if (ISSET(sc->sc_swflags, TIOCFLAG_SOFTCAR) || UCOMCUA(dev) ||
		    ISSET(sc->sc_msr, UMSR_DCD) || ISSET(tp->t_cflag, MDMBUF))
			SET(tp->t_state, TS_CARR_ON);
		else
			CLR(tp->t_state, TS_CARR_ON);
	} else if (ISSET(tp->t_state, TS_XCLUDE) && suser(p) != 0)
		return (EBUSY);
	else
		s = spltty();

	if (UCOMCUA(dev)) {
		if (ISSET(tp->t_state, TS_ISOPEN)) {
			/* Someone is already dialed in */
			splx(s);
			return (EBUSY);
		}
		sc->sc_cua = 1;
	} else {
		/* tty (not cua) device, wait for carrier */
		if (ISSET(flag, O_NONBLOCK)) {
			if (sc->sc_cua) {
				splx(s);
				return (EBUSY);
			}
		} else {
			while (sc->sc_cua || (!ISSET(tp->t_cflag, CLOCAL) &&
			    !ISSET(tp->t_state, TS_CARR_ON))) {
				SET(tp->t_state, TS_WOPEN);
				error = ttysleep(tp, &tp->t_rawq,
				    TTIPRI | PCATCH, ttopen);

				if (usbd_is_dying(sc->sc_uparent)) {
					splx(s);
					return (EIO);
				}

				/*
				 * If TS_WOPEN has been reset, that means the
				 * cua device has been closed.  We don't want
				 * to fail in that case, so just go around
				 * again.
				 */
				if (error && ISSET(tp->t_state, TS_WOPEN)) {
					CLR(tp->t_state, TS_WOPEN);
					splx(s);
					goto bad;
				}
			}
		}
	}
	splx(s);

	error = (*LINESW(tp, l_open))(dev, tp, p);
	if (error)
		goto bad;

	return (0);

fail_4:
	if (sc->sc_bulkin_no != -1)
		usbd_free_xfer(sc->sc_oxfer);
	sc->sc_oxfer = NULL;
fail_3:
	usbd_free_xfer(sc->sc_ixfer);
	sc->sc_ixfer = NULL;
fail_2:
	usbd_close_pipe(sc->sc_bulkout_pipe);
	sc->sc_bulkout_pipe = NULL;
fail_1:
	usbd_close_pipe(sc->sc_bulkin_pipe);
	sc->sc_bulkin_pipe = NULL;
fail_0:
	splx(s);
	ucom_unlock(sc);
	return (error);

bad:
	ucom_lock(sc);
	ucom_cleanup(sc);
	ucom_unlock(sc);

	return (error);
}

int
ucomclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct ucom_softc *sc = ucom_cd.cd_devs[UCOMUNIT(dev)];
	int error;

	if (sc == NULL || usbd_is_dying(sc->sc_uparent))
		return (EIO);

	DPRINTF(("ucomclose: unit=%d\n", UCOMUNIT(dev)));

	sc->sc_refcnt++;
	error = ucom_do_close(sc, flag, mode, p);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);

	return (error);
}

int
ucom_do_close(struct ucom_softc *sc, int flag, int mode, struct proc *p)
{
	struct tty *tp = sc->sc_tty;
	int s;

	if (!ISSET(tp->t_state, TS_ISOPEN))
		return (0);

	ucom_lock(sc);

	(*LINESW(tp, l_close))(tp, flag, p);
	s = spltty();
	CLR(tp->t_state, TS_BUSY | TS_FLUSH);
	sc->sc_cua = 0;
	ttyclose(tp);
	splx(s);
	ucom_cleanup(sc);

	if (sc->sc_methods->ucom_close != NULL)
		sc->sc_methods->ucom_close(sc->sc_parent, sc->sc_portno);

	ucom_unlock(sc);

	return (0);
}

int
ucomread(dev_t dev, struct uio *uio, int flag)
{
	struct ucom_softc *sc = ucom_cd.cd_devs[UCOMUNIT(dev)];
	struct tty *tp;
	int error;

	if (sc == NULL || usbd_is_dying(sc->sc_uparent))
		return (EIO);

	if (sc->sc_error)
		return (sc->sc_error);

	sc->sc_refcnt++;
	tp = sc->sc_tty;
	error = (*LINESW(tp, l_read))(tp, uio, flag);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	return (error);
}

int
ucomwrite(dev_t dev, struct uio *uio, int flag)
{
	struct ucom_softc *sc = ucom_cd.cd_devs[UCOMUNIT(dev)];
	struct tty *tp;
	int error;

	if (sc == NULL || usbd_is_dying(sc->sc_uparent))
		return (EIO);

	sc->sc_refcnt++;
	tp = sc->sc_tty;
	error = (*LINESW(tp, l_write))(tp, uio, flag);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	return (error);
}

struct tty *
ucomtty(dev_t dev)
{
	struct ucom_softc *sc = ucom_cd.cd_devs[UCOMUNIT(dev)];

	/*
	 * Return a pointer to our tty even if the device is dying
	 * in order to properly close it in the detach routine.
	 */
	if (sc == NULL)
		return (NULL);

	return (sc->sc_tty);
}

int
ucomioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct ucom_softc *sc = ucom_cd.cd_devs[UCOMUNIT(dev)];
	int error;

	if (sc == NULL || usbd_is_dying(sc->sc_uparent))
		return (EIO);

	sc->sc_refcnt++;
	error = ucom_do_ioctl(sc, cmd, data, flag, p);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	return (error);
}

int
ucom_do_ioctl(struct ucom_softc *sc, u_long cmd, caddr_t data,
	      int flag, struct proc *p)
{
	struct tty *tp = sc->sc_tty;
	int error;
	int s;

	DPRINTF(("ucomioctl: cmd=0x%08lx\n", cmd));

	error = (*LINESW(tp, l_ioctl))(tp, cmd, data, flag, p);
	if (error >= 0)
		return (error);

	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return (error);

	if (sc->sc_methods->ucom_ioctl != NULL) {
		error = sc->sc_methods->ucom_ioctl(sc->sc_parent,
			    sc->sc_portno, cmd, data, flag, p);
		if (error != ENOTTY)
			return (error);
	}

	error = 0;

	DPRINTF(("ucomioctl: our cmd=0x%08lx\n", cmd));
	s = spltty();

	switch (cmd) {
	case TIOCSBRK:
		ucom_break(sc, 1);
		break;

	case TIOCCBRK:
		ucom_break(sc, 0);
		break;

	case TIOCSDTR:
		ucom_dtr(sc, 1);
		break;

	case TIOCCDTR:
		ucom_dtr(sc, 0);
		break;

	case TIOCGFLAGS:
		*(int *)data = sc->sc_swflags;
		break;

	case TIOCSFLAGS:
		error = suser(p);
		if (error)
			break;
		sc->sc_swflags = *(int *)data;
		break;

	case TIOCMSET:
	case TIOCMBIS:
	case TIOCMBIC:
		tiocm_to_ucom(sc, cmd, *(int *)data);
		break;

	case TIOCMGET:
		*(int *)data = ucom_to_tiocm(sc);
		break;

	default:
		error = ENOTTY;
		break;
	}

	splx(s);

	return (error);
}

void
tiocm_to_ucom(struct ucom_softc *sc, u_long how, int ttybits)
{
	u_char combits;

	combits = 0;
	if (ISSET(ttybits, TIOCM_DTR))
		SET(combits, UMCR_DTR);
	if (ISSET(ttybits, TIOCM_RTS))
		SET(combits, UMCR_RTS);

	switch (how) {
	case TIOCMBIC:
		CLR(sc->sc_mcr, combits);
		break;

	case TIOCMBIS:
		SET(sc->sc_mcr, combits);
		break;

	case TIOCMSET:
		CLR(sc->sc_mcr, UMCR_DTR | UMCR_RTS);
		SET(sc->sc_mcr, combits);
		break;
	}

	if (how == TIOCMSET || ISSET(combits, UMCR_DTR))
		ucom_dtr(sc, (sc->sc_mcr & UMCR_DTR) != 0);
	if (how == TIOCMSET || ISSET(combits, UMCR_RTS))
		ucom_rts(sc, (sc->sc_mcr & UMCR_RTS) != 0);
}

int
ucom_to_tiocm(struct ucom_softc *sc)
{
	u_char combits;
	int ttybits = 0;

	combits = sc->sc_mcr;
	if (ISSET(combits, UMCR_DTR))
		SET(ttybits, TIOCM_DTR);
	if (ISSET(combits, UMCR_RTS))
		SET(ttybits, TIOCM_RTS);

	combits = sc->sc_msr;
	if (ISSET(combits, UMSR_DCD))
		SET(ttybits, TIOCM_CD);
	if (ISSET(combits, UMSR_CTS))
		SET(ttybits, TIOCM_CTS);
	if (ISSET(combits, UMSR_DSR))
		SET(ttybits, TIOCM_DSR);
	if (ISSET(combits, UMSR_RI | UMSR_TERI))
		SET(ttybits, TIOCM_RI);

#if 0
XXX;
	if (sc->sc_ier != 0)
		SET(ttybits, TIOCM_LE);
#endif

	return (ttybits);
}

void
ucom_break(struct ucom_softc *sc, int onoff)
{
	DPRINTF(("ucom_break: onoff=%d\n", onoff));

	if (sc->sc_methods->ucom_set != NULL)
		sc->sc_methods->ucom_set(sc->sc_parent, sc->sc_portno,
		    UCOM_SET_BREAK, onoff);
}

void
ucom_dtr(struct ucom_softc *sc, int onoff)
{
	DPRINTF(("ucom_dtr: onoff=%d\n", onoff));

	if (sc->sc_methods->ucom_set != NULL)
		sc->sc_methods->ucom_set(sc->sc_parent, sc->sc_portno,
		    UCOM_SET_DTR, onoff);
}

void
ucom_rts(struct ucom_softc *sc, int onoff)
{
	DPRINTF(("ucom_rts: onoff=%d\n", onoff));

	if (sc->sc_methods->ucom_set != NULL)
		sc->sc_methods->ucom_set(sc->sc_parent, sc->sc_portno,
		    UCOM_SET_RTS, onoff);
}

void
ucom_status_change(struct ucom_softc *sc)
{
	struct tty *tp = sc->sc_tty;
	u_char old_msr;

	if (sc->sc_methods->ucom_get_status != NULL) {
		old_msr = sc->sc_msr;
		sc->sc_methods->ucom_get_status(sc->sc_parent, sc->sc_portno,
		    &sc->sc_lsr, &sc->sc_msr);

		ttytstamp(tp, old_msr & UMSR_CTS, sc->sc_msr & UMSR_CTS,
		    old_msr & UMSR_DCD, sc->sc_msr & UMSR_DCD);

		if (ISSET((sc->sc_msr ^ old_msr), UMSR_DCD))
			(*LINESW(tp, l_modem))(tp,
			    ISSET(sc->sc_msr, UMSR_DCD));
	} else {
		sc->sc_lsr = 0;
		sc->sc_msr = 0;
	}
}

int
ucomparam(struct tty *tp, struct termios *t)
{
	struct ucom_softc *sc = ucom_cd.cd_devs[UCOMUNIT(tp->t_dev)];
	int error;

	if (sc == NULL || usbd_is_dying(sc->sc_uparent))
		return (EIO);

	/* Check requested parameters. */
	if (t->c_ispeed && t->c_ispeed != t->c_ospeed)
		return (EINVAL);

	/*
	 * For the console, always force CLOCAL and !HUPCL, so that the port
	 * is always active.
	 */
	if (ISSET(sc->sc_swflags, TIOCFLAG_SOFTCAR)) {
		SET(t->c_cflag, CLOCAL);
		CLR(t->c_cflag, HUPCL);
	}

	/*
	 * If there were no changes, don't do anything.  This avoids dropping
	 * input and improves performance when all we did was frob things like
	 * VMIN and VTIME.
	 */
	if (tp->t_ospeed == t->c_ospeed &&
	    tp->t_cflag == t->c_cflag)
		return (0);

	/* XXX lcr = ISSET(sc->sc_lcr, LCR_SBREAK) | cflag2lcr(t->c_cflag); */

	/* And copy to tty. */
	tp->t_ispeed = 0;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;

	/*
	 * When not using CRTSCTS, RTS follows DTR.
	 * This assumes that the ucom_param() call will enable these signals
	 * for real.
	 */
	if (!ISSET(t->c_cflag, CRTSCTS))
		sc->sc_mcr = UMCR_DTR | UMCR_RTS;
	else
		sc->sc_mcr = UMCR_DTR;

	if (sc->sc_methods->ucom_param != NULL) {
		error = sc->sc_methods->ucom_param(sc->sc_parent, sc->sc_portno,
			    t);
		if (error)
			return (error);
	}

	/* XXX worry about CHWFLOW */

	/*
	 * Update the tty layer's idea of the carrier bit, in case we changed
	 * CLOCAL or MDMBUF.  We don't hang up here; we only do that by
	 * explicit request.
	 */
	DPRINTF(("ucomparam: l_modem\n"));
	(void) (*LINESW(tp, l_modem))(tp, 1 /* XXX carrier */ );

#if 0
XXX what if the hardware is not open
	if (!ISSET(t->c_cflag, CHWFLOW)) {
		if (sc->sc_tx_stopped) {
			sc->sc_tx_stopped = 0;
			ucomstart(tp);
		}
	}
#endif

	return (0);
}

/*
 * (un)block input via hw flowcontrol
 */
void
ucom_hwiflow(struct ucom_softc *sc)
{
	DPRINTF(("ucom_hwiflow:\n"));
#if 0
XXX
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	if (sc->sc_mcr_rts == 0)
		return;

	if (ISSET(sc->sc_rx_flags, RX_ANY_BLOCK)) {
		CLR(sc->sc_mcr, sc->sc_mcr_rts);
		CLR(sc->sc_mcr_active, sc->sc_mcr_rts);
	} else {
		SET(sc->sc_mcr, sc->sc_mcr_rts);
		SET(sc->sc_mcr_active, sc->sc_mcr_rts);
	}
	bus_space_write_1(iot, ioh, com_mcr, sc->sc_mcr_active);
#endif
}

void
ucomstart(struct tty *tp)
{
	struct ucom_softc *sc = ucom_cd.cd_devs[UCOMUNIT(tp->t_dev)];
	usbd_status err;
	int s;
	u_char *data;
	int cnt;

	if (sc == NULL || usbd_is_dying(sc->sc_uparent))
		return;

	s = spltty();
	if (ISSET(tp->t_state, TS_BUSY | TS_TIMEOUT | TS_TTSTOP)) {
		DPRINTFN(4,("ucomstart: no go, state=0x%x\n", tp->t_state));
		goto out;
	}
	if (sc->sc_tx_stopped)
		goto out;

	ttwakeupwr(tp);
	if (tp->t_outq.c_cc == 0)
		goto out;

	/* Grab the first contiguous region of buffer space. */
	data = tp->t_outq.c_cf;
	cnt = ndqb(&tp->t_outq, 0);

	if (cnt == 0) {
		DPRINTF(("ucomstart: cnt==0\n"));
		goto out;
	}

	SET(tp->t_state, TS_BUSY);

	if (cnt > sc->sc_obufsize) {
		DPRINTF(("ucomstart: big buffer %d chars\n", cnt));
		cnt = sc->sc_obufsize;
	}
	if (sc->sc_methods->ucom_write != NULL)
		sc->sc_methods->ucom_write(sc->sc_parent, sc->sc_portno,
					   sc->sc_obuf, data, &cnt);
	else
		memcpy(sc->sc_obuf, data, cnt);

	DPRINTFN(4,("ucomstart: %d chars\n", cnt));
#ifdef DIAGNOSTIC
	if (sc->sc_oxfer == NULL) {
		printf("ucomstart: null oxfer\n");
		goto out;
	}
#endif
	if (sc->sc_bulkout_pipe != NULL) {
		usbd_setup_xfer(sc->sc_oxfer, sc->sc_bulkout_pipe,
		    (void *)sc, sc->sc_obuf, cnt,
		    USBD_NO_COPY, USBD_NO_TIMEOUT, ucomwritecb);
	} else {
		usbd_setup_xfer(sc->sc_oxfer, sc->sc_opipe,
		    (void *)sc, sc->sc_obuf, cnt,
		    USBD_NO_COPY, USBD_NO_TIMEOUT, ucomwritecb);
	}
	/* What can we do on error? */
	err = usbd_transfer(sc->sc_oxfer);
#ifdef DIAGNOSTIC
	if (err != USBD_IN_PROGRESS)
		printf("ucomstart: err=%s\n", usbd_errstr(err));
#endif

out:
	splx(s);
}

int
ucomstop(struct tty *tp, int flag)
{
	DPRINTF(("ucomstop: flag=%d\n", flag));
#if 0
	/*struct ucom_softc *sc = ucom_cd.cd_devs[UCOMUNIT(tp->t_dev)];*/
	int s;

	s = spltty();
	if (ISSET(tp->t_state, TS_BUSY)) {
		DPRINTF(("ucomstop: XXX\n"));
		/* sc->sc_tx_stopped = 1; */
		if (!ISSET(tp->t_state, TS_TTSTOP))
			SET(tp->t_state, TS_FLUSH);
	}
	splx(s);
#endif
	return (0);
}

void
ucomwritecb(struct usbd_xfer *xfer, void *p, usbd_status status)
{
	struct ucom_softc *sc = (struct ucom_softc *)p;
	struct tty *tp = sc->sc_tty;
	u_int32_t cc;
	int s;

	DPRINTFN(5,("ucomwritecb: %p %p status=%d\n", xfer, p, status));

	if (status == USBD_CANCELLED || usbd_is_dying(sc->sc_uparent))
		goto error;

	if (sc->sc_bulkin_pipe != NULL) {
		if (status) {
			usbd_clear_endpoint_stall_async(sc->sc_bulkin_pipe);
			/* XXX we should restart after some delay. */
			goto error;
		}
		usbd_get_xfer_status(xfer, NULL, NULL, &cc, NULL);
	} else {
		usbd_get_xfer_status(xfer, NULL, NULL, &cc, NULL);
		// XXX above gives me wrong cc, no?
	}

	DPRINTFN(5,("ucomwritecb: cc=%d\n", cc));
	/* convert from USB bytes to tty bytes */
	cc -= sc->sc_opkthdrlen;

	s = spltty();
	CLR(tp->t_state, TS_BUSY);
	if (ISSET(tp->t_state, TS_FLUSH))
		CLR(tp->t_state, TS_FLUSH);
	else
		ndflush(&tp->t_outq, cc);
	(*LINESW(tp, l_start))(tp);
	splx(s);
	return;

error:
	s = spltty();
	CLR(tp->t_state, TS_BUSY);
	splx(s);
}

usbd_status
ucomstartread(struct ucom_softc *sc)
{
	usbd_status err;

	DPRINTFN(5,("ucomstartread: start\n"));
#ifdef DIAGNOSTIC
	if (sc->sc_ixfer == NULL) {
		DPRINTF(("ucomstartread: null ixfer\n"));
		return (USBD_INVAL);
	}
#endif

	if (sc->sc_bulkin_pipe != NULL) {
		usbd_setup_xfer(sc->sc_ixfer, sc->sc_bulkin_pipe,
			(void *)sc,
			sc->sc_ibuf, sc->sc_ibufsize,
			USBD_SHORT_XFER_OK | USBD_NO_COPY,
			USBD_NO_TIMEOUT, ucomreadcb);
		err = usbd_transfer(sc->sc_ixfer);
		if (err != USBD_IN_PROGRESS) {
			DPRINTF(("ucomstartread: err=%s\n", usbd_errstr(err)));
			return (err);
		}
	}

	return (USBD_NORMAL_COMPLETION);
}

void
ucomreadcb(struct usbd_xfer *xfer, void *p, usbd_status status)
{
	struct ucom_softc *sc = (struct ucom_softc *)p;
	struct tty *tp = sc->sc_tty;
	int (*rint)(int c, struct tty *tp) = LINESW(tp, l_rint);
	usbd_status err;
	u_int32_t cc;
	u_char *cp;
	int s;

	DPRINTFN(5,("ucomreadcb: status=%d\n", status));

	if (status == USBD_CANCELLED || status == USBD_IOERROR ||
	    usbd_is_dying(sc->sc_uparent)) {
		DPRINTF(("ucomreadcb: dying\n"));
		/* Send something to wake upper layer */
		sc->sc_error = EIO;
		s = spltty();
		(*rint)('\n', tp);
		ttwakeup(tp);
		splx(s);
		return;
	}

	if (status) {
		if (sc->sc_bulkin_pipe != NULL) {
			usbd_clear_endpoint_stall_async(sc->sc_bulkin_pipe);
			/* XXX we should restart after some delay. */
			return;
		}
	}

	usbd_get_xfer_status(xfer, NULL, (void *)&cp, &cc, NULL);
	DPRINTFN(5,("ucomreadcb: got %d chars, tp=%p\n", cc, tp));
	if (sc->sc_methods->ucom_read != NULL)
		sc->sc_methods->ucom_read(sc->sc_parent, sc->sc_portno,
					  &cp, &cc);

	s = spltty();
	/* Give characters to tty layer. */
	while (cc-- > 0) {
		DPRINTFN(7,("ucomreadcb: char=0x%02x\n", *cp));
		if ((*rint)(*cp++, tp) == -1) {
			/* XXX what should we do? */
			printf("%s: lost %d chars\n", sc->sc_dev.dv_xname,
			       cc);
			break;
		}
	}
	splx(s);

	err = ucomstartread(sc);
	if (err) {
		printf("%s: read start failed\n", sc->sc_dev.dv_xname);
		/* XXX what should we dow now? */
	}
}

void
ucom_cleanup(struct ucom_softc *sc)
{
	DPRINTF(("ucom_cleanup: closing pipes\n"));

	sc->sc_open = 0;

	ucom_shutdown(sc);
	if (sc->sc_bulkin_pipe != NULL) {
		usbd_close_pipe(sc->sc_bulkin_pipe);
		sc->sc_bulkin_pipe = NULL;
	}
	if (sc->sc_bulkout_pipe != NULL) {
		usbd_close_pipe(sc->sc_bulkout_pipe);
		sc->sc_bulkout_pipe = NULL;
	}
	if (sc->sc_ixfer != NULL) {
		if (sc->sc_bulkin_no != -1) {
			usbd_free_buffer(sc->sc_ixfer);
			sc->sc_ibuf = NULL;
			usbd_free_xfer(sc->sc_ixfer);
		}
		sc->sc_ixfer = NULL;
	}
	if (sc->sc_oxfer != NULL) {
		usbd_free_buffer(sc->sc_oxfer);
		sc->sc_obuf = NULL;
		if (sc->sc_bulkin_no != -1)
			usbd_free_xfer(sc->sc_oxfer);
		sc->sc_oxfer = NULL;
	}
}

/*
 * Update ucom names for export by sysctl.
 */
char *
sysctl_ucominit(void)
{
	static char *ucoms = NULL;
	static size_t ucomslen = 0;
	char name[64];	/* dv_xname + ":usb000.000.00000.000," */
	struct ucom_softc *sc;
	int rslt;
	unsigned int unit;
	uint32_t route;
	uint8_t bus, ifaceno;

	KERNEL_ASSERT_LOCKED();

	if (rw_enter(&sysctl_ucomlock, RW_WRITE|RW_INTR) != 0)
		return NULL;

	if (ucoms == NULL || ucom_change) {
		free(ucoms, M_SYSCTL, ucomslen);
		ucomslen = ucom_cd.cd_ndevs * sizeof(name);
		ucoms = malloc(ucomslen, M_SYSCTL, M_WAITOK | M_ZERO);
		for (unit = 0; unit < ucom_cd.cd_ndevs; unit++) {
			sc = ucom_cd.cd_devs[unit];
			if (sc == NULL || sc->sc_iface == NULL)
				continue;
			if (usbd_get_location(sc->sc_uparent, sc->sc_iface,
			    &bus, &route, &ifaceno) == -1)
				continue;
			rslt = snprintf(name, sizeof(name),
			    "%s:usb%u.%u.%05x.%u,", sc->sc_dev.dv_xname, bus,
			    ROUTEROOTPORT(route), ROUTESTRING(route), ifaceno);
			if (rslt < sizeof(name) && (strlen(ucoms) + rslt) <
			    ucomslen)
				strlcat(ucoms, name, ucomslen);
		}
	}

	/* Remove trailing ','. */
	if (strlen(ucoms))
		ucoms[strlen(ucoms) - 1] = '\0';

	rw_exit_write(&sysctl_ucomlock);

	return ucoms;
}
#endif	/* NUCOM > 0 */

int
ucomprint(void *aux, const char *pnp)
{
	struct ucom_attach_args *uca = aux;

	if (pnp)
		printf("ucom at %s", pnp);
	if (uca->portno != UCOM_UNK_PORTNO)
		printf(" portno %d", uca->portno);
	return (UNCONF);
}

int
ucomsubmatch(struct device *parent, void *match, void *aux)
{
        struct ucom_attach_args *uca = aux;
        struct cfdata *cf = match;

	if (uca->portno != UCOM_UNK_PORTNO &&
	    cf->ucomcf_portno != UCOM_UNK_PORTNO &&
	    cf->ucomcf_portno != uca->portno)
		return (0);
	return ((*cf->cf_attach->ca_match)(parent, cf, aux));
}
