/* $OpenBSD: omsysc.c,v 1.4 2023/09/22 01:10:43 jsg Exp $ */
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
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

#include <machine/simplebusvar.h>

struct omsysc_softc {
	struct simplebus_softc	sc_bus;
};

int	omsysc_match(struct device *, void *, void *);
void	omsysc_attach(struct device *, struct device *, void *);

const struct cfattach omsysc_ca = {
	sizeof(struct omsysc_softc), omsysc_match, omsysc_attach
};

struct cfdriver omsysc_cd = {
	NULL, "omsysc", DV_DULL
};

int
omsysc_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct cfdata *cf = cfdata;
	int node;

	/*
	 * On AM33xx we need to make sure we attach the PRCM and SCM
	 * modules since they provide clock and pinctrl for other
	 * hardware modules.  Therefore we attach those during the
	 * "early" phase.
	 */
	node = OF_child(faa->fa_node);
	if (node && cf->cf_loc[0]) {
		return (OF_is_compatible(node, "ti,am3-prcm") ||
		    OF_is_compatible(node, "ti,am3-scm"));
	}

	return OF_is_compatible(faa->fa_node, "ti,sysc");
}

void
omsysc_attach(struct device *parent, struct device *self, void *aux)
{
	struct omsysc_softc *sc = (struct omsysc_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (OF_getproplen(faa->fa_node, "ti,hwmods") < 0 &&
	    OF_is_compatible(faa->fa_node, "ti,sysc-omap2"))
		clock_enable(faa->fa_node, "fck");

	simplebus_attach(parent, &sc->sc_bus.sc_dev, faa);
}
