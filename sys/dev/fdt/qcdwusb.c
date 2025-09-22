/*	$OpenBSD: qcdwusb.c,v 1.2 2023/09/22 01:10:44 jsg Exp $	*/
/*
 * Copyright (c) 2017, 2018 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2020, 2022 Patrick Wildt <patrick@blueri.se>
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
#include <machine/fdt.h>
#include <machine/simplebusvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

struct qcdwusb_softc {
	struct simplebus_softc	sc_sbus;
};

int	qcdwusb_match(struct device *, void *, void *);
void	qcdwusb_attach(struct device *, struct device *, void *);

const struct cfattach qcdwusb_ca = {
	sizeof(struct qcdwusb_softc), qcdwusb_match, qcdwusb_attach
};

struct cfdriver qcdwusb_cd = {
	NULL, "qcdwusb", DV_DULL
};

int
qcdwusb_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,dwc3");
}

void
qcdwusb_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcdwusb_softc *sc = (struct qcdwusb_softc *)self;
	struct fdt_attach_args *faa = aux;

	power_domain_enable(faa->fa_node);
	clock_enable_all(faa->fa_node);
	reset_deassert_all(faa->fa_node);

	simplebus_attach(parent, &sc->sc_sbus.sc_dev, faa);
}
