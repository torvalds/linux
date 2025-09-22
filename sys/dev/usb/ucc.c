/*	$OpenBSD: ucc.c,v 1.37 2022/11/14 00:16:46 deraadt Exp $	*/

/*
 * Copyright (c) 2021 Anton Lindqvist <anton@openbsd.org>
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

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/uhidev.h>

#include <dev/hid/hidccvar.h>

struct ucc_softc {
	struct uhidev	 sc_hdev;
	struct hidcc	*sc_cc;
};

int	ucc_match(struct device *, void *, void *);
void	ucc_attach(struct device *, struct device *, void *);
int	ucc_detach(struct device *, int);
void	ucc_intr(struct uhidev *, void *, u_int);

int	ucc_enable(void *, int);

struct cfdriver ucc_cd = {
	NULL, "ucc", DV_DULL
};

const struct cfattach ucc_ca = {
	sizeof(struct ucc_softc),
	ucc_match,
	ucc_attach,
	ucc_detach,
};

int
ucc_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	void *desc;
	int size;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return UMATCH_NONE;

	uhidev_get_report_desc(uha->parent, &desc, &size);
	if (hid_report_size(desc, size, hid_input, uha->reportid) == 0)
		return UMATCH_NONE;
	if (!hidcc_match(desc, size, uha->reportid))
		return UMATCH_NONE;

	return UMATCH_IFACECLASS;
}

void
ucc_attach(struct device *parent, struct device *self, void *aux)
{
	struct ucc_softc *sc = (struct ucc_softc *)self;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	void *desc;
	int repid, size;

	sc->sc_hdev.sc_intr = ucc_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_udev = uha->uaa->device;
	sc->sc_hdev.sc_report_id = uha->reportid;

	uhidev_get_report_desc(uha->parent, &desc, &size);
	repid = uha->reportid;
	sc->sc_hdev.sc_isize = hid_report_size(desc, size, hid_input, repid);
	sc->sc_hdev.sc_osize = hid_report_size(desc, size, hid_output, repid);
	sc->sc_hdev.sc_fsize = hid_report_size(desc, size, hid_feature, repid);

	struct hidcc_attach_arg hca = {
		.device		= &sc->sc_hdev.sc_dev,
		.audio_cookie	= uha->uaa->cookie,
		.desc		= desc,
		.descsiz	= size,
		.repid		= repid,
		.isize		= sc->sc_hdev.sc_isize,
		.enable		= ucc_enable,
		.arg		= self,
	};
	sc->sc_cc = hidcc_attach(&hca);
}

int
ucc_detach(struct device *self, int flags)
{
	struct ucc_softc *sc = (struct ucc_softc *)self;
	int error = 0;

	uhidev_close(&sc->sc_hdev);
	if (sc->sc_cc != NULL)
		error = hidcc_detach(sc->sc_cc, flags);
	return error;
}

void
ucc_intr(struct uhidev *addr, void *data, u_int len)
{
	struct ucc_softc *sc = (struct ucc_softc *)addr;

	if (sc->sc_cc != NULL)
		hidcc_intr(sc->sc_cc, data, len);
}

int
ucc_enable(void *v, int on)
{
	struct ucc_softc *sc = (struct ucc_softc *)v;
	int error = 0;

	if (on)
		error = uhidev_open(&sc->sc_hdev);
	else
		uhidev_close(&sc->sc_hdev);
	return error;
}
