/*-
 * Copyright (c) 2014, 2017 Kevin Lo
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/conf.h>
#include <sys/fcntl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbhid.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/uled_ioctl.h>

struct uled_softc {
	struct usb_fifo_sc	sc_fifo;
	struct mtx		sc_mtx;

	struct usb_device	*sc_udev;
	struct uled_color	sc_color;

	uint8_t			sc_state;
#define	ULED_ENABLED	0x01

	int			sc_flags;
#define	ULED_FLAG_BLINK1	0x0001
};

/* Initial commands. */
static uint8_t blink1[] = { 0x1, 'v', 0, 0, 0, 0, 0, 0 };
static uint8_t dl100b[] = { 0x1f, 0x2, 0, 0x5f, 0, 0, 0x1a, 0x3 };

/* Prototypes. */
static device_probe_t	uled_probe;
static device_attach_t	uled_attach;
static device_detach_t	uled_detach;

static usb_fifo_open_t	uled_open;
static usb_fifo_close_t	uled_close;
static usb_fifo_ioctl_t	uled_ioctl;

static struct usb_fifo_methods uled_fifo_methods = {
	.f_open = &uled_open,
	.f_close = &uled_close,
	.f_ioctl = &uled_ioctl,
	.basename[0] = "uled",
};

static usb_error_t	uled_ctrl_msg(struct uled_softc *, uint8_t, uint8_t,
			    uint16_t, uint16_t, void *, uint16_t);
static int		uled_enable(struct uled_softc *);

static devclass_t uled_devclass;

static device_method_t uled_methods[] = {
	DEVMETHOD(device_probe,		uled_probe),
	DEVMETHOD(device_attach,	uled_attach),
	DEVMETHOD(device_detach,	uled_detach),

	DEVMETHOD_END
};

static driver_t uled_driver = {
	.name = "uled",
	.methods = uled_methods,
	.size = sizeof(struct uled_softc),
};

static const STRUCT_USB_HOST_ID uled_devs[] = {
#define	ULED_DEV(v,p,i)	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, i) }
	ULED_DEV(DREAMLINK, DL100B, 0),
	ULED_DEV(THINGM, BLINK1, ULED_FLAG_BLINK1),
#undef ULED_DEV
};

DRIVER_MODULE(uled, uhub, uled_driver, uled_devclass, NULL, NULL);
MODULE_DEPEND(uled, usb, 1, 1, 1);
MODULE_VERSION(uled, 1);
USB_PNP_HOST_INFO(uled_devs);

static int
uled_probe(device_t dev)
{
	struct usb_attach_arg *uaa;

	uaa = device_get_ivars(dev);
	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bInterfaceClass != UICLASS_HID)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(uled_devs, sizeof(uled_devs), uaa));
}

static int
uled_attach(device_t dev)
{
	struct usb_attach_arg *uaa;
	struct uled_softc *sc;
	int unit;
	usb_error_t error;

	uaa = device_get_ivars(dev);
	sc = device_get_softc(dev);
	unit = device_get_unit(dev);
	sc->sc_flags = USB_GET_DRIVER_INFO(uaa);

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "uled lock", NULL, MTX_DEF | MTX_RECURSE);

	sc->sc_udev = uaa->device;

	error = usb_fifo_attach(uaa->device, sc, &sc->sc_mtx,
	    &uled_fifo_methods, &sc->sc_fifo, unit, -1,
	    uaa->info.bIfaceIndex, UID_ROOT, GID_OPERATOR, 0644);
	if (error != 0)
		goto detach;

	sc->sc_color.red = 0;
	sc->sc_color.green = 0;
	sc->sc_color.blue = 0;

	return (0);

detach:
	uled_detach(dev);
	return (ENOMEM);
}

static int
uled_detach(device_t dev)
{
	struct uled_softc *sc;

	sc = device_get_softc(dev);
	usb_fifo_detach(&sc->sc_fifo);
	mtx_destroy(&sc->sc_mtx);
	return (0);
}

static usb_error_t
uled_ctrl_msg(struct uled_softc *sc, uint8_t rt, uint8_t reqno,
    uint16_t value, uint16_t index, void *buf, uint16_t buflen)
{
	struct usb_device_request req;

	req.bmRequestType = rt;
	req.bRequest = reqno;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, buflen);

	return (usbd_do_request_flags(sc->sc_udev, &sc->sc_mtx, &req, buf,
	    0, NULL, 2000));
}

static int
uled_enable(struct uled_softc *sc)
{
	uint8_t *cmdbuf;
	int error;

	cmdbuf = (sc->sc_flags & ULED_FLAG_BLINK1) ? blink1 : dl100b;

	sc->sc_state |= ULED_ENABLED;
	mtx_lock(&sc->sc_mtx);
	error = uled_ctrl_msg(sc, UT_WRITE_CLASS_INTERFACE, UR_SET_REPORT,
	    0x200, 0, cmdbuf, sizeof(cmdbuf));
	mtx_unlock(&sc->sc_mtx);
	return (error);
}

static int
uled_open(struct usb_fifo *fifo, int fflags)
{
	if (fflags & FREAD) {
		struct uled_softc *sc;
		int rc;

		sc = usb_fifo_softc(fifo);
		if (sc->sc_state & ULED_ENABLED)
			return (EBUSY);
		if ((rc = uled_enable(sc)) != 0)
			return (rc);
	}
	return (0);
}

static void
uled_close(struct usb_fifo *fifo, int fflags)
{
	if (fflags & FREAD) {
		struct uled_softc *sc;

		sc = usb_fifo_softc(fifo);
		sc->sc_state &= ~ULED_ENABLED;
	}
}

static int
uled_ioctl(struct usb_fifo *fifo, u_long cmd, void *addr, int fflags)
{
	struct uled_softc *sc;
	struct uled_color color;
	int error;

	sc = usb_fifo_softc(fifo);
	error = 0;

	mtx_lock(&sc->sc_mtx);

	switch(cmd) {
	case ULED_GET_COLOR:
		*(struct uled_color *)addr = sc->sc_color;
		break;
	case ULED_SET_COLOR:
		color = *(struct uled_color *)addr;
		uint8_t buf[8];

		sc->sc_color.red = color.red;
		sc->sc_color.green = color.green;
		sc->sc_color.blue = color.blue;

		if (sc->sc_flags & ULED_FLAG_BLINK1) {
			buf[0] = 0x1;
			buf[1] = 'n';
			buf[2] = color.red;
			buf[3] = color.green;
			buf[4] = color.blue;
			buf[5] = buf[6] = buf[7] = 0;
		} else {
			buf[0] = color.red;
			buf[1] = color.green;
			buf[2] = color.blue;
			buf[3] = buf[4] = buf[5] = 0;
			buf[6] = 0x1a;
			buf[7] = 0x05;
		}
		error = uled_ctrl_msg(sc, UT_WRITE_CLASS_INTERFACE,
		    UR_SET_REPORT, 0x200, 0, buf, sizeof(buf));
		break;
	default:
		error = ENOTTY;
		break;
	}

	mtx_unlock(&sc->sc_mtx);
	return (error);
}
