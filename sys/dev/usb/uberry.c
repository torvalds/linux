/*	$OpenBSD: uberry.c,v 1.25 2024/05/23 03:21:09 jsg Exp $	*/

/*-
 * Copyright (c) 2006 Theo de Raadt <deraadt@openbsd.org>
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
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

struct uberry_softc {
	struct device			sc_dev;
	struct usbd_device		*sc_udev;
	struct usbd_interface		*sc_iface;
};

#define UBERRY_INTERFACE_NO		0
#define UBERRY_CONFIG_NO		1

/*
 * Do not match on the following device, because it is type umass
 * { USB_VENDOR_RIM, USB_PRODUCT_RIM_PEARL_DUAL },
 */
struct usb_devno const uberry_devices[] = {
	{ USB_VENDOR_RIM, USB_PRODUCT_RIM_BLACKBERRY },
	{ USB_VENDOR_RIM, USB_PRODUCT_RIM_PEARL }
};

int uberry_match(struct device *, void *, void *);
void uberry_attach(struct device *, struct device *, void *);
int uberry_detach(struct device *, int);

void uberry_pearlmode(struct uberry_softc *);
void uberry_charge(struct uberry_softc *);

struct cfdriver uberry_cd = {
	NULL, "uberry", DV_DULL
};

const struct cfattach uberry_ca = {
	sizeof(struct uberry_softc), uberry_match, uberry_attach, uberry_detach
};

int
uberry_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != UBERRY_CONFIG_NO)
		return UMATCH_NONE;

	return (usb_lookup(uberry_devices, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
uberry_attach(struct device *parent, struct device *self, void *aux)
{
	struct uberry_softc *sc = (struct uberry_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_device_descriptor_t *dd;

	sc->sc_udev = uaa->device;

	dd = usbd_get_device_descriptor(uaa->device);

	printf("%s: Charging at %dmA", sc->sc_dev.dv_xname,
	    sc->sc_udev->power);
	if (sc->sc_udev->power >= 250)
		printf("\n");
	else {
		printf("... requesting higher-power charging\n");
		uberry_charge(sc);
		/*
		 * Older berry's will disconnect/reconnect at this
		 * point, and come back requesting higher power
		 */
	}

	/* On the Pearl, request a change to Dual mode */
	if (UGETW(dd->idProduct) == USB_PRODUCT_RIM_PEARL)
		uberry_pearlmode(sc);

	/* Enable the device, then it cannot idle, and will charge */
	if (usbd_set_config_no(sc->sc_udev, UBERRY_CONFIG_NO, 1) != 0) {
		printf("%s: could not set configuration no\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	if (UGETW(dd->idProduct) == USB_PRODUCT_RIM_PEARL) {
		/*
		 * Pearl does not disconnect/reconnect by itself,
		 * and therefore needs to be told to reset, so that
		 * it can come back in Dual mode.
		 */
		usb_needs_reattach(sc->sc_udev);
	}
}

int
uberry_detach(struct device *self, int flags)
{
	/* struct uberry_softc *sc = (struct uberry_softc *)self; */

	return 0;
}

void
uberry_pearlmode(struct uberry_softc *sc)
{
	usb_device_request_t req;
	char buffer[256];

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = 0xa9;
	USETW(req.wValue, 1);
	USETW(req.wIndex, 1);
	USETW(req.wLength, 2);
	(void) usbd_do_request(sc->sc_udev, &req, &buffer);
}

void 
uberry_charge(struct uberry_softc *sc)
{
	usb_device_request_t req;
	char buffer[256];

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = 0xa5;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 1);
	USETW(req.wLength, 2);
	(void) usbd_do_request(sc->sc_udev, &req, &buffer);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = 0xa2;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 1);
	USETW(req.wLength, 0);
	(void) usbd_do_request(sc->sc_udev, &req, &buffer);
}
