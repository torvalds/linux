/*	$OpenBSD: uipaq.c,v 1.29 2024/05/23 03:21:09 jsg Exp $	*/

/*
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
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
 * iPAQ driver
 * 
 * 19 July 2003:	Incorporated changes suggested by Sam Lawrance from
 * 			the uppc module
 *
 *
 * Contact isis@cs.umd.edu if you have any questions/comments about this driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/tty.h>

#include <dev/usb/usb.h>

#include <dev/usb/usbcdc.h>	/*UCDC_* stuff */

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/ucomvar.h>

#ifdef UIPAQ_DEBUG
#define DPRINTF(x)	if (uipaqdebug) printf x
#define DPRINTFN(n,x)	if (uipaqdebug>(n)) printf x
int uipaqdebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define UIPAQ_CONFIG_NO		1
#define UIPAQ_IFACE_INDEX	0

#define UIPAQIBUFSIZE 1024
#define UIPAQOBUFSIZE 1024

struct uipaq_softc {
	struct device		 sc_dev;	/* base device */
	struct usbd_device	*sc_udev;	/* device */
	struct usbd_interface	*sc_iface;	/* interface */

	struct device		*sc_subdev;	/* ucom uses that */
	u_int16_t		 sc_lcr;	/* state for DTR/RTS */

	u_int16_t		 sc_flags;
};

/* Callback routines */
void	uipaq_set(void *, int, int, int);


/* Support routines. */
/* based on uppc module by Sam Lawrance */
void	uipaq_dtr(struct uipaq_softc *sc, int onoff);
void	uipaq_rts(struct uipaq_softc *sc, int onoff);
void	uipaq_break(struct uipaq_softc* sc, int onoff);


const struct ucom_methods uipaq_methods = {
	NULL,
	uipaq_set,
	NULL,
	NULL,
	NULL,	/*open*/
	NULL,	/*close*/
	NULL,
	NULL
};

struct uipaq_type {
	struct usb_devno	uv_dev;
	u_int16_t		uv_flags;
};

static const struct uipaq_type uipaq_devs[] = {
	{{ USB_VENDOR_ASUS, USB_PRODUCT_ASUS_MYPAL_A730} , 0},
	{{ USB_VENDOR_CASIO, USB_PRODUCT_CASIO_BE300} , 0},
	{{ USB_VENDOR_COMPAQ, USB_PRODUCT_COMPAQ_IPAQPOCKETPC} , 0},
	{{ USB_VENDOR_HTC, USB_PRODUCT_HTC_SMARTPHONE }, 0},
	{{ USB_VENDOR_HP, USB_PRODUCT_HP_2215 }, 0 },
	{{ USB_VENDOR_HP, USB_PRODUCT_HP_568J }, 0},
	{{ USB_VENDOR_HTC, USB_PRODUCT_HTC_PPC6700MODEM }, 0}
};

#define uipaq_lookup(v, p) ((struct uipaq_type *)usb_lookup(uipaq_devs, v, p))

int uipaq_match(struct device *, void *, void *);
void uipaq_attach(struct device *, struct device *, void *);
int uipaq_detach(struct device *, int);

struct cfdriver uipaq_cd = {
	NULL, "uipaq", DV_DULL
};

const struct cfattach uipaq_ca = {
	sizeof(struct uipaq_softc), uipaq_match, uipaq_attach, uipaq_detach
};

int
uipaq_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != UIPAQ_CONFIG_NO)
		return (UMATCH_NONE);

	DPRINTFN(20,("uipaq: vendor=0x%x, product=0x%x\n",
	    uaa->vendor, uaa->product));

	return (uipaq_lookup(uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
uipaq_attach(struct device *parent, struct device *self, void *aux)
{
	struct uipaq_softc *sc = (struct uipaq_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	struct usbd_interface *iface;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	char *devname = sc->sc_dev.dv_xname;
	int i;
	usbd_status err;
	struct ucom_attach_args uca;

	DPRINTFN(10,("\nuipaq_attach: sc=%p\n", sc));

	err = usbd_device2interface_handle(dev, UIPAQ_IFACE_INDEX, &iface);
	if (err) {
		printf(": failed to get interface, err=%s\n",
		    usbd_errstr(err));
		goto bad;
	}

	sc->sc_flags = uipaq_lookup(uaa->vendor, uaa->product)->uv_flags;

	id = usbd_get_interface_descriptor(iface);

	sc->sc_udev = dev;
	sc->sc_iface = iface;

	uca.ibufsize = UIPAQIBUFSIZE;
	uca.obufsize = UIPAQOBUFSIZE;
	uca.ibufsizepad = UIPAQIBUFSIZE;
	uca.opkthdrlen = 0;
	uca.device = dev;
	uca.iface = iface;
	uca.methods = &uipaq_methods;
	uca.arg = sc;
	uca.portno = UCOM_UNK_PORTNO;
	uca.info = "Generic";

/*	err = uipaq_init(sc);
	if (err) {
		printf("%s: init failed, %s\n", sc->sc_dev.dv_xname,
		    usbd_errstr(err));
		goto bad;
	}*/

	uca.bulkin = uca.bulkout = -1;
	for (i=0; i<id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for %d\n",
					devname,i);
			goto bad;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			uca.bulkin = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			uca.bulkout = ed->bEndpointAddress;
		}
	}
	if (uca.bulkin != -1 && uca.bulkout != -1)
		sc->sc_subdev = config_found_sm(self, &uca,
		    ucomprint, ucomsubmatch);
	else
		printf("%s: no proper endpoints found (%d,%d) \n",
		    devname, uca.bulkin, uca.bulkout);

	return;

bad:
	DPRINTF(("uipaq_attach: ATTACH ERROR\n"));
	usbd_deactivate(sc->sc_udev);
}


void
uipaq_dtr(struct uipaq_softc* sc, int onoff)
{
	usb_device_request_t req;
	usbd_status err;
	int retries = 3;

	DPRINTF(("%s: uipaq_dtr: onoff=%x\n", sc->sc_dev.dv_xname, onoff));

	/* Avoid sending unnecessary requests */
	if (onoff && (sc->sc_lcr & UCDC_LINE_DTR))
		return;
	if (!onoff && !(sc->sc_lcr & UCDC_LINE_DTR))
		return;

	/* Other parameters depend on reg */
	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	sc->sc_lcr = onoff ? sc->sc_lcr | UCDC_LINE_DTR : sc->sc_lcr & ~UCDC_LINE_DTR;
	USETW(req.wValue, sc->sc_lcr);
	USETW(req.wIndex, 0x0);
	USETW(req.wLength, 0);

	/* Fire off the request a few times if necessary */
	while (retries) {
		err = usbd_do_request(sc->sc_udev, &req, NULL);
		if (!err)
			break;
		retries--;
	}
}


void
uipaq_rts(struct uipaq_softc* sc, int onoff)
{
	usb_device_request_t req;
	usbd_status err;
	int retries = 3;

	DPRINTF(("%s: uipaq_rts: onoff=%x\n", sc->sc_dev.dv_xname, onoff));

	/* Avoid sending unnecessary requests */
	if (onoff && (sc->sc_lcr & UCDC_LINE_RTS)) return;
	if (!onoff && !(sc->sc_lcr & UCDC_LINE_RTS)) return;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	sc->sc_lcr = onoff ? sc->sc_lcr | UCDC_LINE_RTS : sc->sc_lcr & ~UCDC_LINE_RTS;
	USETW(req.wValue, sc->sc_lcr);
	USETW(req.wIndex, 0x0);
	USETW(req.wLength, 0);

	while (retries) {
		err = usbd_do_request(sc->sc_udev, &req, NULL);
		if (!err)
			break;
		retries--;
	}
}


void
uipaq_break(struct uipaq_softc* sc, int onoff)
{
	usb_device_request_t req;
	usbd_status err;
	int retries = 3;

	DPRINTF(("%s: uipaq_break: onoff=%x\n", sc->sc_dev.dv_xname, onoff));

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SEND_BREAK;

	USETW(req.wValue, onoff ? UCDC_BREAK_ON : UCDC_BREAK_OFF);
	USETW(req.wIndex, 0x0);
	USETW(req.wLength, 0);

	while (retries) {
		err = usbd_do_request(sc->sc_udev, &req, NULL);
		if (!err)
			break;
		retries--;
	}
}


void
uipaq_set(void *addr, int portno, int reg, int onoff)
{
	struct uipaq_softc* sc = addr;

	switch (reg) {
	case UCOM_SET_DTR:
		uipaq_dtr(addr, onoff);
		break;
	case UCOM_SET_RTS:
		uipaq_rts(addr, onoff);
		break;
	case UCOM_SET_BREAK:
		uipaq_break(addr, onoff);
		break;
	default:
		printf("%s: unhandled set request: reg=%x onoff=%x\n",
		    sc->sc_dev.dv_xname, reg, onoff);
		return;
	}
}


int
uipaq_detach(struct device *self, int flags)
{
	struct uipaq_softc *sc = (struct uipaq_softc *)self;
	int rv = 0;

	DPRINTF(("uipaq_detach: sc=%p flags=%d\n", sc, flags));
	if (sc->sc_subdev != NULL) {
		rv |= config_detach(sc->sc_subdev, flags);
		sc->sc_subdev = NULL;
	}

	return (rv);
}

