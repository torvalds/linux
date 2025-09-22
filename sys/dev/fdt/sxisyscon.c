/*	$OpenBSD: sxisyscon.c,v 1.3 2024/02/07 22:00:38 uaa Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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

struct sxisyscon_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

int	sxisyscon_match(struct device *, void *, void *);
void	sxisyscon_attach(struct device *, struct device *, void *);

const struct cfattach sxisyscon_ca = {
	sizeof(struct sxisyscon_softc), sxisyscon_match, sxisyscon_attach
};

struct cfdriver sxisyscon_cd = {
	NULL, "sxisyscon", DV_DULL
};

int
sxisyscon_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;

	if (OF_is_compatible(node, "allwinner,sun8i-h3-system-control") ||
	    OF_is_compatible(node, "allwinner,sun50i-a64-system-control") ||
	    OF_is_compatible(node, "allwinner,sun50i-h5-system-control") ||
	    OF_is_compatible(node, "allwinner,sun50i-h6-system-control") ||
	    OF_is_compatible(node, "allwinner,sun50i-h616-system-control"))
		return 10;	/* Must beat syscon(4). */

	return 0;
}

void
sxisyscon_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxisyscon_softc *sc = (struct sxisyscon_softc *)self;
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
