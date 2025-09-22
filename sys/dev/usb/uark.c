/*	$OpenBSD: uark.c,v 1.26 2024/05/23 03:21:09 jsg Exp $	*/

/*
 * Copyright (c) 2006 Jonathan Gray <jsg@openbsd.org>
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
#include <sys/tty.h>
#include <sys/device.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/ucomvar.h>

#ifdef UARK_DEBUG
#define DPRINTFN(n, x)  do { if (uarkdebug > (n)) printf x; } while (0)
int	uarkebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x) DPRINTFN(0, x)

#define UARKBUFSZ		256
#define UARK_IFACE_NO		0

#define UARK_SET_DATA_BITS(x)	(x - 5)

#define UARK_PARITY_NONE	0x00
#define UARK_PARITY_ODD		0x08
#define UARK_PARITY_EVEN	0x18

#define UARK_STOP_BITS_1	0x00
#define UARK_STOP_BITS_2	0x04

#define UARK_BAUD_REF		3000000

#define UARK_WRITE		0x40
#define UARK_READ		0xc0

#define UARK_REQUEST		0xfe

struct uark_softc {
	struct device		 sc_dev;
	struct usbd_device	*sc_udev;
	struct usbd_interface	*sc_iface;
	struct device 		*sc_subdev;

	u_char			 sc_msr;
	u_char			 sc_lsr;
};

void	uark_get_status(void *, int portno, u_char *lsr, u_char *msr);
void	uark_set(void *, int, int, int);
int	uark_param(void *, int, struct termios *);
void	uark_break(void *, int, int);
int	uark_cmd(struct uark_softc *, uint16_t, uint16_t);

const struct ucom_methods uark_methods = {
	uark_get_status,
	uark_set,
	uark_param,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

static const struct usb_devno uark_devs[] = {
	{ USB_VENDOR_ARKMICRO,		USB_PRODUCT_ARKMICRO_ARK3116 }
};

int uark_match(struct device *, void *, void *);
void uark_attach(struct device *, struct device *, void *);
int uark_detach(struct device *, int);

struct cfdriver uark_cd = {
	NULL, "uark", DV_DULL
};

const struct cfattach uark_ca = {
	sizeof(struct uark_softc), uark_match, uark_attach, uark_detach
};

int
uark_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL)
		return UMATCH_NONE;

	return (usb_lookup(uark_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
uark_attach(struct device *parent, struct device *self, void *aux)
{
	struct uark_softc *sc = (struct uark_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ucom_attach_args uca;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	usbd_status error;
	int i;

	bzero(&uca, sizeof(uca));
	sc->sc_udev = uaa->device;

	/* get the first interface handle */
	error = usbd_device2interface_handle(sc->sc_udev, UARK_IFACE_NO,
	    &sc->sc_iface);
	if (error != 0) {
		printf("%s: could not get interface handle\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	id = usbd_get_interface_descriptor(sc->sc_iface);

	uca.bulkin = uca.bulkout = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor found for %d\n",
			    sc->sc_dev.dv_xname, i);
			usbd_deactivate(sc->sc_udev);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			uca.bulkin = ed->bEndpointAddress;
		else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			uca.bulkout = ed->bEndpointAddress;
	}

	if (uca.bulkin == -1 || uca.bulkout == -1) {
		printf("%s: missing endpoint\n", sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	uca.ibufsize = UARKBUFSZ;
	uca.obufsize = UARKBUFSZ;
	uca.ibufsizepad = UARKBUFSZ;
	uca.opkthdrlen = 0;
	uca.device = sc->sc_udev;
	uca.iface = sc->sc_iface;
	uca.methods = &uark_methods;
	uca.arg = sc;
	uca.info = NULL;
	
	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);
}

int
uark_detach(struct device *self, int flags)
{
	struct uark_softc *sc = (struct uark_softc *)self;
	int rv = 0;

	if (sc->sc_subdev != NULL) {
		rv = config_detach(sc->sc_subdev, flags);
		sc->sc_subdev = NULL;
	}

	return (rv);
}

void
uark_set(void *vsc, int portno, int reg, int onoff)
{
	struct uark_softc *sc = vsc;

	switch (reg) {
	case UCOM_SET_BREAK:
		uark_break(sc, portno, onoff);
		return;
	case UCOM_SET_DTR:
	case UCOM_SET_RTS:
	default:
		return;
	}
}

int
uark_param(void *vsc, int portno, struct termios *t)
{
	struct uark_softc *sc = (struct uark_softc *)vsc;
	int data;

	switch (t->c_ospeed) {
	case 300:
	case 600:
	case 1200:
	case 1800:
	case 2400:
	case 4800:
	case 9600:
	case 19200:
	case 38400:
	case 57600:
	case 115200:
		uark_cmd(sc, 3, 0x83);
		uark_cmd(sc, 0, (UARK_BAUD_REF / t->c_ospeed) & 0xFF);
		uark_cmd(sc, 1, (UARK_BAUD_REF / t->c_ospeed) >> 8);
		uark_cmd(sc, 3, 0x03);
		break;
	default:
		return (EINVAL);
	}

	if (ISSET(t->c_cflag, CSTOPB))
		data = UARK_STOP_BITS_2;
	else
		data = UARK_STOP_BITS_1;

	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			data |= UARK_PARITY_ODD;
		else
			data |= UARK_PARITY_EVEN;
	} else
		data |= UARK_PARITY_NONE;

	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		data |= UARK_SET_DATA_BITS(5);
		break;
	case CS6:
		data |= UARK_SET_DATA_BITS(6);
		break;
	case CS7:
		data |= UARK_SET_DATA_BITS(7);
		break;
	case CS8:
		data |= UARK_SET_DATA_BITS(8);
		break;
	}

	uark_cmd(sc, 3, 0x00);
	uark_cmd(sc, 3, data);

#if 0
	/* XXX flow control */
	if (ISSET(t->c_cflag, CRTSCTS))
		/*  rts/cts flow ctl */
	} else if (ISSET(t->c_iflag, IXON|IXOFF)) {
		/*  xon/xoff flow ctl */
	} else {
		/* disable flow ctl */
	}
#endif

	return (0);
}

void
uark_get_status(void *vsc, int portno, u_char *lsr, u_char *msr)
{
	struct uark_softc *sc = vsc;
	
	if (msr != NULL)
		*msr = sc->sc_msr;
	if (lsr != NULL)
		*lsr = sc->sc_lsr;
}

void
uark_break(void *vsc, int portno, int onoff)
{
#ifdef UARK_DEBUG
	struct uark_softc *sc = vsc;

	printf("%s: break %s!\n", sc->sc_dev.dv_xname,
	    onoff ? "on" : "off");

	if (onoff)
		/* break on */
		uark_cmd(sc, 4, 0x01);
	else
		uark_cmd(sc, 4, 0x00);
#endif
}

int
uark_cmd(struct uark_softc *sc, uint16_t index, uint16_t value)
{
	usb_device_request_t req;
	usbd_status err;

	req.bmRequestType = UARK_WRITE;
	req.bRequest = UARK_REQUEST;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, 0);
	err = usbd_do_request(sc->sc_udev, &req, NULL);

	if (err)
		return (EIO);

	return (0);
}
