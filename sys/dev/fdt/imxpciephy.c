/* $OpenBSD: imxpciephy.c,v 1.2 2022/04/06 18:59:28 naddy Exp $ */
/*
 * Copyright (c) 2020 Patrick Wildt <patrick@blueri.se>
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

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

struct imxpciephy_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

int	imxpciephy_match(struct device *, void *, void *);
void	imxpciephy_attach(struct device *, struct device *, void *);

const struct cfattach imxpciephy_ca = {
	sizeof(struct imxpciephy_softc), imxpciephy_match, imxpciephy_attach
};

struct cfdriver imxpciephy_cd = {
	NULL, "imxpciephy", DV_DULL
};

int
imxpciephy_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;

	if (OF_is_compatible(node, "fsl,imx7d-pcie-phy"))
		return 10;	/* Must beat syscon(4). */

	return 0;
}

void
imxpciephy_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxpciephy_softc *sc = (struct imxpciephy_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	regmap_register(faa->fa_node, sc->sc_iot, sc->sc_ioh,
	    faa->fa_reg[0].size);

	printf("\n");
}
