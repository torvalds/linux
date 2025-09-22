/*	$OpenBSD: fido.c,v 1.7 2024/12/30 02:46:00 guenther Exp $	*/

/*
 * Copyright (c) 2019 Reyk Floeter <reyk@openbsd.org>
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

#include "fido.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/conf.h>
#include <sys/tty.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>

#include <dev/usb/usbdi.h>

#include <dev/usb/uhidev.h>
#include <dev/usb/uhid.h>

int fido_match(struct device *, void *, void *);

struct cfdriver fido_cd = {
	NULL, "fido", DV_DULL
};

const struct cfattach fido_ca = {
	sizeof(struct uhid_softc),
	fido_match,
	uhid_attach,
	uhid_detach,
};

int
fido_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	int			  size;
	void			 *desc;
	int			  ret = UMATCH_NONE;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (ret);

	/* Find the FIDO usage page and U2F collection */
	uhidev_get_report_desc(uha->parent, &desc, &size);
	if (hid_is_collection(desc, size, uha->reportid,
	    HID_USAGE2(HUP_FIDO, HUF_U2FHID)))
		ret = UMATCH_IFACECLASS;

	return (ret);
}

int
fidoopen(dev_t dev, int flag, int mode, struct proc *p)
{
	return (uhid_do_open(dev, flag, mode, p));
}

int
fidoioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	int	 error;

	switch (cmd) {
	case FIOASYNC:
	case USB_GET_DEVICEINFO:
		break;
	default:
		/*
		 * Users don't need USB/HID ioctl access to fido(4) devices
		 * but it can still be useful for debugging by root.
		 */
		if ((error = suser(p)) != 0)
			return (error);
		break;
	}

	return (uhidioctl(dev, cmd, addr, flag, p));
}
