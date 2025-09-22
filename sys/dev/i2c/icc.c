/*	$OpenBSD: icc.c,v 1.2 2024/08/17 15:10:00 deraadt Exp $	*/

/*
 * Copyright (c) 2021 Anton Lindqvist <anton@openbsd.org>
 * Copyright (c) 2022 Matthieu Herrb <matthieu@openbsd.org>
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

#include <dev/i2c/i2cvar.h>
#include <dev/i2c/ihidev.h>

#include <dev/hid/hid.h>
#include <dev/hid/hidccvar.h>

struct icc_softc {
	struct ihidev	 sc_hdev;
	struct hidcc	*sc_cc;
	int		 sc_keydown;
};

int	icc_match(struct device *, void *, void *);
void	icc_attach(struct device *, struct device *, void *);
int	icc_detach(struct device *, int);
void	icc_intr(struct ihidev *, void *, u_int);

int	icc_enable(void *, int);

struct cfdriver icc_cd = {
	NULL, "icc", DV_DULL
};

const struct cfattach icc_ca = {
	sizeof(struct icc_softc),
	icc_match,
	icc_attach,
	icc_detach
};

int
icc_match(struct device *parent, void *match, void *aux)
{
	struct ihidev_attach_arg *iha = aux;
	void *desc;
	int size;

	if (iha->reportid == IHIDEV_CLAIM_MULTIPLEID)
		return IMATCH_NONE;

	ihidev_get_report_desc(iha->parent, &desc, &size);
	if (hid_report_size(desc, size, hid_input, iha->reportid) == 0)
		return IMATCH_NONE;

	if (!hidcc_match(desc, size, iha->reportid))
		return IMATCH_NONE;
	return IMATCH_IFACECLASS;
}

void
icc_attach(struct device *parent, struct device *self, void *aux)
{
	struct icc_softc *sc = (struct icc_softc *)self;
	struct ihidev_attach_arg *iha = aux;
	void *desc;
	int repid, size;

	sc->sc_hdev.sc_intr = icc_intr;
	sc->sc_hdev.sc_parent = iha->parent;
	sc->sc_hdev.sc_report_id = iha->reportid;

	ihidev_get_report_desc(iha->parent, &desc, &size);
	repid = iha->reportid;
	sc->sc_hdev.sc_isize = hid_report_size(desc, size, hid_input, repid);
	sc->sc_hdev.sc_osize = hid_report_size(desc, size, hid_output, repid);
	sc->sc_hdev.sc_fsize = hid_report_size(desc, size, hid_feature, repid);

	struct hidcc_attach_arg hca = {
		.device		= &sc->sc_hdev.sc_idev,
		.audio_cookie	= iha->iaa->ia_cookie,
		.desc		= desc,
		.descsiz	= size,
		.repid		= repid,
		.isize		= sc->sc_hdev.sc_isize,
		.enable		= icc_enable,
		.arg		= self,
	};
	sc->sc_cc = hidcc_attach(&hca);
}

int
icc_detach(struct device *self, int flags)
{
	struct icc_softc *sc = (struct icc_softc *)self;
	int error = 0;

	ihidev_close(&sc->sc_hdev);
	if (sc->sc_cc != NULL)
		error = hidcc_detach(sc->sc_cc, flags);
	return error;
}

void
icc_intr(struct ihidev *addr, void *data, u_int len)
{
	struct icc_softc *sc = (struct icc_softc *)addr;

	if (sc->sc_cc != NULL)
		hidcc_intr(sc->sc_cc, data, len);
}

int
icc_enable(void *v, int on)
{
	struct icc_softc *sc = v;
	int error = 0;

	if (on)
		error = ihidev_open(&sc->sc_hdev);
	else
		ihidev_close(&sc->sc_hdev);
	return error;
}
