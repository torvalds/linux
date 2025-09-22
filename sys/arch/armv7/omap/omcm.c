/* $OpenBSD: omcm.c,v 1.4 2023/09/22 01:10:43 jsg Exp $ */
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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

#include <dev/ofw/openfirm.h>

#include <machine/simplebusvar.h>

struct omcm_softc {
	struct simplebus_softc	sc_bus;
};

int	omcm_match(struct device *, void *, void *);
void	omcm_attach(struct device *, struct device *, void *);

const struct cfattach omcm_ca = {
	sizeof(struct omcm_softc), omcm_match, omcm_attach
};

struct cfdriver omcm_cd = {
	NULL, "omcm", DV_DULL
};

int
omcm_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "ti,omap4-cm");
}

void
omcm_attach(struct device *parent, struct device *self, void *aux)
{
	struct omcm_softc *sc = (struct omcm_softc *)self;
	struct fdt_attach_args *faa = aux;

	simplebus_attach(parent, &sc->sc_bus.sc_dev, faa);
}
