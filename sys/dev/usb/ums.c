/*	$OpenBSD: ums.c,v 1.53 2024/05/26 20:06:27 mglocker Exp $ */
/*	$NetBSD: ums.c,v 1.60 2003/03/11 16:44:00 augustss Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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
 * HID spec: https://www.usb.org/sites/default/files/hid1_11.pdf
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usb_quirks.h>
#include <dev/usb/uhidev.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

#include <dev/hid/hidmsvar.h>

struct ums_softc {
	struct uhidev	sc_hdev;
	struct hidms	sc_ms;
	uint32_t	sc_quirks;
};

void ums_intr(struct uhidev *addr, void *ibuf, u_int len);

int	ums_enable(void *);
void	ums_disable(void *);
int	ums_ioctl(void *, u_long, caddr_t, int, struct proc *);
void	ums_fix_elecom_descriptor(struct ums_softc *, void *, int, int);

const struct wsmouse_accessops ums_accessops = {
	ums_enable,
	ums_ioctl,
	ums_disable,
};

int ums_match(struct device *, void *, void *);
void ums_attach(struct device *, struct device *, void *);
int ums_detach(struct device *, int);

struct cfdriver ums_cd = {
	NULL, "ums", DV_DULL
};

const struct cfattach ums_ca = {
	sizeof(struct ums_softc), ums_match, ums_attach, ums_detach
};

int
ums_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	int size;
	void *desc;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (UMATCH_NONE);

	uhidev_get_report_desc(uha->parent, &desc, &size);

	if (hid_is_collection(desc, size, uha->reportid,
	    HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_POINTER)))
		return (UMATCH_IFACECLASS);

	if (hid_is_collection(desc, size, uha->reportid,
	    HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_MOUSE)))
		return (UMATCH_IFACECLASS);

	if (hid_is_collection(desc, size, uha->reportid,
	    HID_USAGE2(HUP_DIGITIZERS, HUD_TOUCHSCREEN)))
		return (UMATCH_IFACECLASS);

	if (hid_is_collection(desc, size, uha->reportid,
	    HID_USAGE2(HUP_DIGITIZERS, HUD_PEN)))
		return (UMATCH_IFACECLASS);

	return (UMATCH_NONE);
}

void
ums_attach(struct device *parent, struct device *self, void *aux)
{
	struct ums_softc *sc = (struct ums_softc *)self;
	struct hidms *ms = &sc->sc_ms;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	struct usb_attach_arg *uaa = uha->uaa;
	int size, repid;
	void *desc;
	u_int32_t qflags = 0;

	sc->sc_hdev.sc_intr = ums_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_udev = uaa->device;
	sc->sc_hdev.sc_report_id = uha->reportid;

	usbd_set_idle(uha->parent->sc_udev, uha->parent->sc_ifaceno, 0, 0);

	sc->sc_quirks = usbd_get_quirks(sc->sc_hdev.sc_udev)->uq_flags;
	uhidev_get_report_desc(uha->parent, &desc, &size);

	if (uaa->vendor == USB_VENDOR_ELECOM)
		ums_fix_elecom_descriptor(sc, desc, size, uaa->product);

	repid = uha->reportid;
	sc->sc_hdev.sc_isize = hid_report_size(desc, size, hid_input, repid);
	sc->sc_hdev.sc_osize = hid_report_size(desc, size, hid_output, repid);
	sc->sc_hdev.sc_fsize = hid_report_size(desc, size, hid_feature, repid);

	if (sc->sc_quirks & UQ_MS_REVZ)
		qflags |= HIDMS_REVZ;
	if (sc->sc_quirks & UQ_SPUR_BUT_UP)
		qflags |= HIDMS_SPUR_BUT_UP;
	if (sc->sc_quirks & UQ_MS_BAD_CLASS)
		qflags |= HIDMS_MS_BAD_CLASS;
	if (sc->sc_quirks & UQ_MS_LEADING_BYTE)
		qflags |= HIDMS_LEADINGBYTE;
	if (sc->sc_quirks & UQ_MS_VENDOR_BUTTONS)
		qflags |= HIDMS_VENDOR_BUTTONS;

	if (hidms_setup(self, ms, qflags, uha->reportid, desc, size) != 0)
		return;

	/*
	 * The Microsoft Wireless Notebook Optical Mouse 3000 Model 1049 has
	 * five Report IDs: 19, 23, 24, 17, 18 (in the order they appear in
	 * report descriptor), it seems that report 17 contains the necessary
	 * mouse information (3-buttons, X, Y, wheel) so we specify it
	 * manually.
	 */
	if (uaa->vendor == USB_VENDOR_MICROSOFT &&
	    uaa->product == USB_PRODUCT_MICROSOFT_WLNOTEBOOK3) {
		ms->sc_flags = HIDMS_Z;
		ms->sc_num_buttons = 3;
		/* XXX change sc_hdev isize to 5? */
		ms->sc_loc_x.pos = 8;
		ms->sc_loc_y.pos = 16;
		ms->sc_loc_z.pos = 24;
		ms->sc_loc_btn[0].pos = 0;
		ms->sc_loc_btn[1].pos = 1;
		ms->sc_loc_btn[2].pos = 2;
	}

	if (sc->sc_quirks & UQ_ALWAYS_OPEN) {
		/* open uhidev and keep it open */
		ums_enable(sc);
		/* but mark the hidms not in use */
		ums_disable(sc);
	}

	hidms_attach(ms, &ums_accessops);
}

int
ums_detach(struct device *self, int flags)
{
	struct ums_softc *sc = (struct ums_softc *)self;
	struct hidms *ms = &sc->sc_ms;

	return hidms_detach(ms, flags);
}

void
ums_intr(struct uhidev *addr, void *buf, u_int len)
{
	struct ums_softc *sc = (struct ums_softc *)addr;
	struct hidms *ms = &sc->sc_ms;

	if (ms->sc_enabled != 0)
		hidms_input(ms, (uint8_t *)buf, len);
}

int
ums_enable(void *v)
{
	struct ums_softc *sc = v;
	struct hidms *ms = &sc->sc_ms;
	int rv;

	if (usbd_is_dying(sc->sc_hdev.sc_udev))
		return EIO;

	if ((rv = hidms_enable(ms)) != 0)
		return rv;

	if ((sc->sc_quirks & UQ_ALWAYS_OPEN) &&
	    (sc->sc_hdev.sc_state & UHIDEV_OPEN))
		rv = 0;
	else
		rv = uhidev_open(&sc->sc_hdev);

	return rv;
}

void
ums_disable(void *v)
{
	struct ums_softc *sc = v;
	struct hidms *ms = &sc->sc_ms;

	hidms_disable(ms);

	if (sc->sc_quirks & UQ_ALWAYS_OPEN)
		return;

	uhidev_close(&sc->sc_hdev);
}

int
ums_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct ums_softc *sc = v;
	struct hidms *ms = &sc->sc_ms;
	int rc;

	rc = uhidev_ioctl(&sc->sc_hdev, cmd, data, flag, p);
	if (rc != -1)
		return rc;
	rc = hidms_ioctl(ms, cmd, data, flag, p);
	if (rc != -1)
		return rc;

	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_USB;
		return 0;
	default:
		return -1;
	}
}

/*
 * Some ELECOM devices present flawed report descriptors.  Instead of a 5-bit
 * field and a 3-bit padding as defined by the descriptor they actually use
 * 6 or 8 Bits to report button states, see
 *     https://flameeyes.blog/2017/04/24/elecom-deft-and-the-broken-descriptor
 * This function adapts the Report Count value for the button page, its Usage
 * Maximum and the report size for the padding bits (at offset 31).
 */
void
ums_fix_elecom_descriptor(struct ums_softc *sc, void *desc, int size,
		int product)
{
	static uByte match[] = {    /* a descriptor fragment, offset: 12 */
	    0x95, 0x05, 0x75, 0x01, /* Report Count (5), Report Size (1) */
	    0x05, 0x09, 0x19, 0x01, /* Usage Page (Button), Usage Minimum (1) */
	    0x29, 0x05,             /* Usage Maximum (5) */
	};
	uByte *udesc = desc;
	int nbuttons;

	switch (product) {
	case USB_PRODUCT_ELECOM_MXT3URBK:	/* EX-G Trackballs */
	case USB_PRODUCT_ELECOM_MXT3DRBK:
	case USB_PRODUCT_ELECOM_MXT4DRBK:
		nbuttons = 6;
		break;
	case USB_PRODUCT_ELECOM_MDT1URBK:	/* DEFT Trackballs */
	case USB_PRODUCT_ELECOM_MDT1DRBK:
	case USB_PRODUCT_ELECOM_MHT1URBK:	/* HUGE Trackballs */
	case USB_PRODUCT_ELECOM_MHT1DRBK:
		nbuttons = 8;
		break;
	default:
		return;
	}

	if (udesc == NULL || size < 32
	    || memcmp(&udesc[12], match, sizeof(match))
	    || udesc[30] != 0x75 || udesc[31] != 3)
		return;

	printf("%s: fixing Elecom report descriptor (buttons: %d)\n",
		sc->sc_hdev.sc_dev.dv_xname, nbuttons);
	udesc[13] = nbuttons;
	udesc[21] = nbuttons;
	udesc[31] = 8 - nbuttons;
}
