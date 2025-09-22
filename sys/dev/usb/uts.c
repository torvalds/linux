/*	$OpenBSD: uts.c,v 1.45 2024/05/23 03:21:09 jsg Exp $ */

/*
 * Copyright (c) 2007 Robert Nagy <robert@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <machine/intr.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

#ifdef UTS_DEBUG
#define DPRINTF(x)		do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif

struct tsscale {
	int	minx, maxx;
	int	miny, maxy;
	int	swapxy;
	int	resx, resy;
} def_scale = {
	67, 1931, 102, 1937, 0, 1024, 768
};

struct uts_softc {
	struct device		sc_dev;
	struct usbd_device	*sc_udev;
	struct usbd_interface	*sc_iface;
	int			sc_product;
	int			sc_vendor;

	int			sc_intr_number;
	struct usbd_pipe	*sc_intr_pipe;
	u_char			*sc_ibuf;
	int			sc_isize;
	u_int8_t		sc_pkts;

	struct device		*sc_wsmousedev;

	int	sc_enabled;
	int	sc_buttons;
	int	sc_oldx;
	int	sc_oldy;
	int	sc_rawmode;

	struct tsscale sc_tsscale;
};

struct uts_pos {
	int	down;
	int	x;
	int	y;
	int	z;	/* touch pressure */
};

const struct usb_devno uts_devs[] = {
	{ USB_VENDOR_FTDI,		USB_PRODUCT_FTDI_ITM_TOUCH },
	{ USB_VENDOR_EGALAX,		USB_PRODUCT_EGALAX_TPANEL },
	{ USB_VENDOR_EGALAX,		USB_PRODUCT_EGALAX_TPANEL2 },
	{ USB_VENDOR_GUNZE,		USB_PRODUCT_GUNZE_TOUCHPANEL }
};

void uts_intr(struct usbd_xfer *, void *, usbd_status);
void uts_get_pos(void *addr, struct uts_pos *tp);

int	uts_enable(void *);
void	uts_disable(void *);
int	uts_ioctl(void *, u_long, caddr_t, int, struct proc *);

const struct wsmouse_accessops uts_accessops = {
	uts_enable,
	uts_ioctl,
	uts_disable,
};

int uts_match(struct device *, void *, void *);
void uts_attach(struct device *, struct device *, void *);
int uts_detach(struct device *, int);
int uts_activate(struct device *, int);

struct cfdriver uts_cd = {
	NULL, "uts", DV_DULL
};

const struct cfattach uts_ca = {
	sizeof(struct uts_softc),
	uts_match,
	uts_attach,
	uts_detach,
	uts_activate,
};

int
uts_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	/* Some eGalax touch screens are HID devices. ignore them */
	id = usbd_get_interface_descriptor(uaa->iface);
	if (id != NULL && id->bInterfaceClass == UICLASS_HID)
		return (UMATCH_NONE);

	return (usb_lookup(uts_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
uts_attach(struct device *parent, struct device *self, void *aux)
{
	struct uts_softc *sc = (struct uts_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_config_descriptor_t *cdesc;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	struct wsmousedev_attach_args a;
	int i;

	sc->sc_udev = uaa->device;
	sc->sc_product = uaa->product;
	sc->sc_vendor = uaa->vendor;
	sc->sc_intr_number = -1;
	sc->sc_intr_pipe = NULL;
	sc->sc_enabled = sc->sc_isize = 0;

	/* Copy the default scale values to each softc */
	bcopy(&def_scale, &sc->sc_tsscale, sizeof(sc->sc_tsscale));

	/* get the config descriptor */
	cdesc = usbd_get_config_descriptor(sc->sc_udev);
	if (cdesc == NULL) {
		printf("%s: failed to get configuration descriptor\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* get the interface */
	if (usbd_device2interface_handle(uaa->device, 0, &sc->sc_iface) != 0) {
		printf("%s: failed to get interface\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* Find the interrupt endpoint */
	id = usbd_get_interface_descriptor(sc->sc_iface);

	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for %d\n",
			    sc->sc_dev.dv_xname, i);
			usbd_deactivate(sc->sc_udev);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->sc_intr_number = ed->bEndpointAddress;
			sc->sc_isize = UGETW(ed->wMaxPacketSize);
		}
	}

	if (sc->sc_intr_number== -1) {
		printf("%s: Could not find interrupt in\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	a.accessops = &uts_accessops;
	a.accesscookie = sc;

	sc->sc_wsmousedev = config_found(self, &a, wsmousedevprint);
}

int
uts_detach(struct device *self, int flags)
{
	struct uts_softc *sc = (struct uts_softc *)self;
	int rv = 0;

	if (sc->sc_intr_pipe != NULL) {
		usbd_close_pipe(sc->sc_intr_pipe);
		sc->sc_intr_pipe = NULL;
	}

	if (sc->sc_wsmousedev != NULL) {
		rv = config_detach(sc->sc_wsmousedev, flags);
		sc->sc_wsmousedev = NULL;
	}

	return (rv);
}

int
uts_activate(struct device *self, int act)
{
	struct uts_softc *sc = (struct uts_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_DEACTIVATE:
		if (sc->sc_wsmousedev != NULL)
			rv = config_deactivate(sc->sc_wsmousedev);
		usbd_deactivate(sc->sc_udev);
		break;
	}

	return (rv);
}

int
uts_enable(void *v)
{
	struct uts_softc *sc = v;
	int err;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	if (sc->sc_enabled)
		return (EBUSY);

	if (sc->sc_isize == 0)
		return (0);
	sc->sc_ibuf = malloc(sc->sc_isize, M_USBDEV, M_WAITOK);
	err = usbd_open_pipe_intr(sc->sc_iface, sc->sc_intr_number,
	    USBD_SHORT_XFER_OK, &sc->sc_intr_pipe, sc, sc->sc_ibuf,
	    sc->sc_isize, uts_intr, USBD_DEFAULT_INTERVAL);
	if (err) {
		free(sc->sc_ibuf, M_USBDEV, sc->sc_isize);
		sc->sc_intr_pipe = NULL;
		return (EIO);
	}

	sc->sc_enabled = 1;
	sc->sc_buttons = 0;

	return (0);
}

void
uts_disable(void *v)
{
	struct uts_softc *sc = v;

	if (!sc->sc_enabled) {
		printf("uts_disable: already disabled!\n");
		return;
	}

	/* Disable interrupts. */
	if (sc->sc_intr_pipe != NULL) {
		usbd_close_pipe(sc->sc_intr_pipe);
		sc->sc_intr_pipe = NULL;
	}

	if (sc->sc_ibuf != NULL) {
		free(sc->sc_ibuf, M_USBDEV, sc->sc_isize);
		sc->sc_ibuf = NULL;
	}

	sc->sc_enabled = 0;
}

int
uts_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *l)
{
	int error = 0;
	struct uts_softc *sc = v;
	struct wsmouse_calibcoords *wsmc = (struct wsmouse_calibcoords *)data;

	DPRINTF(("uts_ioctl(%zu, '%c', %zu)\n",
	    IOCPARM_LEN(cmd), (int) IOCGROUP(cmd), cmd & 0xff));

	switch (cmd) {
	case WSMOUSEIO_SCALIBCOORDS:
		if (!(wsmc->minx >= -32768 && wsmc->maxx >= 0 &&
		    wsmc->miny >= -32768 && wsmc->maxy >= 0 &&
		    wsmc->resx >= 0 && wsmc->resy >= 0 &&
		    wsmc->minx < 32768 && wsmc->maxx < 32768 &&
		    wsmc->miny < 32768 && wsmc->maxy < 32768 &&
		    (wsmc->maxx - wsmc->minx) != 0 &&
		    (wsmc->maxy - wsmc->miny) != 0 &&
		    wsmc->resx < 32768 && wsmc->resy < 32768 &&
		    wsmc->swapxy >= 0 && wsmc->swapxy <= 1 &&
		    wsmc->samplelen >= 0 && wsmc->samplelen <= 1))
			return (EINVAL);

		sc->sc_tsscale.minx = wsmc->minx;
		sc->sc_tsscale.maxx = wsmc->maxx;
		sc->sc_tsscale.miny = wsmc->miny;
		sc->sc_tsscale.maxy = wsmc->maxy;
		sc->sc_tsscale.swapxy = wsmc->swapxy;
		sc->sc_tsscale.resx = wsmc->resx;
		sc->sc_tsscale.resy = wsmc->resy;
		sc->sc_rawmode = wsmc->samplelen;
		break;
	case WSMOUSEIO_GCALIBCOORDS:
		wsmc->minx = sc->sc_tsscale.minx;
		wsmc->maxx = sc->sc_tsscale.maxx;
		wsmc->miny = sc->sc_tsscale.miny;
		wsmc->maxy = sc->sc_tsscale.maxy;
		wsmc->swapxy = sc->sc_tsscale.swapxy;
		wsmc->resx = sc->sc_tsscale.resx;
		wsmc->resy = sc->sc_tsscale.resy;
		wsmc->samplelen = sc->sc_rawmode;
		break;
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_TPANEL;
		break;
	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

void
uts_get_pos(void *addr, struct uts_pos *tp)
{
	struct uts_softc *sc = addr;
	u_char *p = sc->sc_ibuf;
	int down, x, y, z;

	switch (sc->sc_product) {
	case USB_PRODUCT_FTDI_ITM_TOUCH:
		down = (~p[7] & 0x20);
		x = ((p[0] & 0x1f) << 7) | (p[3] & 0x7f);
		/* Invert the Y coordinate */
		y = 0x0fff - abs(((p[1] & 0x1f) << 7) | (p[4] & 0x7f));
		z = ((p[2] & 0x1) << 7) | (p[5] & 0x7f);
		sc->sc_pkts = 0x8;
		break;
	case USB_PRODUCT_EGALAX_TPANEL:
	case USB_PRODUCT_EGALAX_TPANEL2:
		/*
		 * eGalax and Gunze USB touch panels have the same device ID,
		 * so decide upon the vendor ID.
		 */
		switch (sc->sc_vendor) {
		case USB_VENDOR_EGALAX:
			down = (p[0] & 0x01);
			/* Invert the X coordinate */
			x = 0x07ff - abs(((p[3] & 0x0f) << 7) | (p[4] & 0x7f));
			y = ((p[1] & 0x0f) << 7) | (p[2] & 0x7f);
			z = down;
			sc->sc_pkts = 0x5;
			break;
		case USB_VENDOR_GUNZE:
			down = (~p[7] & 0x20);
			/* Invert the X coordinate */
			x = 0x0fff - abs(((p[0] & 0x1f) << 7) | (p[2] & 0x7f));
			y = ((p[1] & 0x1f) << 7) | (p[3] & 0x7f);
			z = (down != 0);
			sc->sc_pkts = 0x4;
			break;
		}
		break;
	}

	DPRINTF(("%s: down = 0x%x, sc->sc_pkts = 0x%x\n",
	    sc->sc_dev.dv_xname, down, sc->sc_pkts));

	/* x/y values are not reliable if there is no pressure */
	if (down) {
		if (sc->sc_tsscale.swapxy && !sc->sc_rawmode) {
			/* Swap X/Y-Axis */
			tp->y = x;
			tp->x = y;
		} else {
			tp->x = x;
			tp->y = y;
		}
		if (!sc->sc_rawmode &&
		    (sc->sc_tsscale.maxx - sc->sc_tsscale.minx) != 0 &&
		    (sc->sc_tsscale.maxy - sc->sc_tsscale.miny) != 0) {
			/* Scale down to the screen resolution. */
			tp->x = ((tp->x - sc->sc_tsscale.minx) *
			    sc->sc_tsscale.resx) /
			    (sc->sc_tsscale.maxx - sc->sc_tsscale.minx);
			tp->y = ((tp->y - sc->sc_tsscale.miny) *
			    sc->sc_tsscale.resy) /
			    (sc->sc_tsscale.maxy - sc->sc_tsscale.miny);
		}
	} else {
		tp->x = sc->sc_oldx;
		tp->y = sc->sc_oldy;
	}
	tp->z = z;
	tp->down = down;
}

void
uts_intr(struct usbd_xfer *xfer, void *addr, usbd_status status)
{
	struct uts_softc *sc = addr;
	u_int32_t len;
	int s;
	struct uts_pos tp;

	if (status == USBD_CANCELLED)
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		printf("%s: status %d\n", sc->sc_dev.dv_xname, status);
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_intr_pipe);
		return;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	s = spltty();

	uts_get_pos(sc, &tp);

	if (len != sc->sc_pkts) {
		DPRINTF(("%s: bad input length %d != %d\n",
		    sc->sc_dev.dv_xname, len, sc->sc_isize));
		splx(s);
		return;
	}

	DPRINTF(("%s: tp.down = %d, tp.z = %d, tp.x = %d, tp.y = %d\n",
	    sc->sc_dev.dv_xname, tp.down, tp.z, tp.x, tp.y));

	WSMOUSE_TOUCH(sc->sc_wsmousedev, tp.down, tp.x, tp.y, tp.z, 0);
	sc->sc_oldy = tp.y;
	sc->sc_oldx = tp.x;

	splx(s);
}
