/*	$OpenBSD: ujoy.c,v 1.6 2024/12/30 02:46:00 guenther Exp $ */

/*
 * Copyright (c) 2021 Thomas Frohwein	<thfr@openbsd.org>
 * Copyright (c) 2021 Bryan Steele	<brynet@openbsd.org>
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
#include <sys/ioctl.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <sys/fcntl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>

#include <dev/usb/usbdi.h>

#include <dev/usb/uhidev.h>
#include <dev/usb/uhid.h>

int ujoy_match(struct device *, void *, void *);

struct cfdriver ujoy_cd = {
	NULL, "ujoy", DV_DULL
};

const struct cfattach ujoy_ca = {
	sizeof(struct uhid_softc),
	ujoy_match,
	uhid_attach,
	uhid_detach,
};

/*
 * XXX workaround:
 *
 * This is a copy of sys/dev/hid/hid.c:hid_is_collection(), synced up to the
 * NetBSD version.  Our current hid_is_collection() is not playing nice with
 * all HID devices like the PS4 controller.  But applying this version
 * globally breaks other HID devices like ims(4) and imt(4).  Until our global
 * hid_is_collection() can't be fixed to play nice with all HID devices, we
 * go for this dedicated ujoy(4) version.
 */
int
ujoy_hid_is_collection(const void *desc, int size, uint8_t id, int32_t usage)
{
	struct hid_data *hd;
	struct hid_item hi;
	uint32_t coll_usage = ~0;

	hd = hid_start_parse(desc, size, hid_input);
	if (hd == NULL)
		return (0);

	while (hid_get_item(hd, &hi)) {
		if (hi.kind == hid_collection &&
		    hi.collection == HCOLL_APPLICATION)
			coll_usage = hi.usage;

		if (hi.kind == hid_endcollection)
			coll_usage = ~0;

		if (hi.kind == hid_input &&
		    coll_usage == usage &&
		    hi.report_ID == id) {
			hid_end_parse(hd);
			return (1);
		}
	}
	hid_end_parse(hd);

	return (0);
}

int
ujoy_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	int			  size;
	void			 *desc;
	int			  ret = UMATCH_NONE;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (ret);

	/* Find the general usage page and gamecontroller collections */
	uhidev_get_report_desc(uha->parent, &desc, &size);

	if (ujoy_hid_is_collection(desc, size, uha->reportid,
	    HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_JOYSTICK)))
    		ret = UMATCH_IFACECLASS;

	if (ujoy_hid_is_collection(desc, size, uha->reportid,
	    HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_GAME_PAD)))
    		ret = UMATCH_IFACECLASS;

	return (ret);
}

int
ujoyopen(dev_t dev, int flag, int mode, struct proc *p)
{
	/* Restrict ujoy devices to read operations */
	if ((flag & FWRITE))
    		return (EPERM);
	return (uhid_do_open(dev, flag, mode, p));
}

int
ujoyioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	switch (cmd) {
	case FIOASYNC:
	case USB_GET_DEVICEINFO:
    	case USB_GET_REPORT:
    	case USB_GET_REPORT_DESC:
    	case USB_GET_REPORT_ID:
		break;
	default:
		return (EPERM);
	}

	return (uhidioctl(dev, cmd, addr, flag, p));
}
