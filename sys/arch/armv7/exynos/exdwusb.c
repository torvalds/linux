/*	$OpenBSD: exdwusb.c,v 1.6 2023/09/22 01:10:43 jsg Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/device.h>

#include <machine/fdt.h>
#include <machine/simplebusvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

struct exdwusb_softc {
	struct simplebus_softc	sc_sbus;
};

int	exdwusb_match(struct device *, void *, void *);
void	exdwusb_attach(struct device *, struct device *, void *);

const struct cfattach exdwusb_ca = {
	sizeof(struct exdwusb_softc), exdwusb_match, exdwusb_attach
};

struct cfdriver exdwusb_cd = {
	NULL, "exdwusb", DV_DULL
};

int
exdwusb_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "samsung,exynos5250-dwusb3");
}

void
exdwusb_attach(struct device *parent, struct device *self, void *aux)
{
	struct exdwusb_softc *sc = (struct exdwusb_softc *)self;
	struct fdt_attach_args *faa = aux;

	clock_enable_all(faa->fa_node);

	simplebus_attach(parent, &sc->sc_sbus.sc_dev, faa);
}
