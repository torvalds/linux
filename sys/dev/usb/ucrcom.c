/*	$OpenBSD: ucrcom.c,v 1.3 2024/05/23 03:21:09 jsg Exp $	*/

/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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

#define UCRCOMBUFSZ		64

struct ucrcom_softc {
	struct device		sc_dev;
	struct device		*sc_subdev;
};

const struct ucom_methods ucrcom_methods = { NULL };

int ucrcom_match(struct device *, void *, void *);
void ucrcom_attach(struct device *, struct device *, void *);
int ucrcom_detach(struct device *, int);

struct cfdriver ucrcom_cd = {
	NULL, "ucrcom", DV_DULL
};

const struct cfattach ucrcom_ca = {
	sizeof(struct ucrcom_softc), ucrcom_match, ucrcom_attach, ucrcom_detach
};

int
ucrcom_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	usb_device_descriptor_t *dd;

	if (uaa->iface == NULL)
		return UMATCH_NONE;

	id = usbd_get_interface_descriptor(uaa->iface);
	dd = usbd_get_device_descriptor(uaa->device);
	if (id == NULL || dd == NULL)
		return UMATCH_NONE;

	if (UGETW(dd->idVendor) == USB_VENDOR_GOOGLE &&
	    id->bInterfaceClass == UICLASS_VENDOR &&
	    id->bInterfaceSubClass == 0x50 &&
	    id->bInterfaceProtocol == 1)
		return UMATCH_VENDOR_IFACESUBCLASS_IFACEPROTO;

	return UMATCH_NONE;
}

void
ucrcom_attach(struct device *parent, struct device *self, void *aux)
{
	struct ucrcom_softc *sc = (struct ucrcom_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ucom_attach_args uca;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i;

	id = usbd_get_interface_descriptor(uaa->iface);

	memset(&uca, 0, sizeof(uca));
	uca.bulkin = uca.bulkout = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(uaa->iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for %d\n",
				sc->sc_dev.dv_xname, i);
			usbd_deactivate(uaa->device);
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
		usbd_deactivate(uaa->device);
		return;
	}

	uca.ibufsize = UCRCOMBUFSZ;
	uca.ibufsizepad = UCRCOMBUFSZ;
	uca.obufsize = UCRCOMBUFSZ;
	uca.opkthdrlen = 0;
	uca.device = uaa->device;
	uca.iface = uaa->iface;
	uca.methods = &ucrcom_methods;
	uca.arg = sc;

	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);
}

int
ucrcom_detach(struct device *self, int flags)
{
	struct ucrcom_softc *sc = (struct ucrcom_softc *)self;
	int rv = 0;

	if (sc->sc_subdev != NULL)
		rv = config_detach(sc->sc_subdev, flags);

	return rv;
}
