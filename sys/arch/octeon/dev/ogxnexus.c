/*	$OpenBSD: ogxnexus.c,v 1.1 2019/11/04 14:58:40 visa Exp $	*/

/*
 * Copyright (c) 2019 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

/*
 * Driver for OCTEON BGX nexus.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <octeon/dev/ogxreg.h>
#include <octeon/dev/ogxvar.h>

struct ogxnexus_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	int			 sc_bgxid;
};

#define NEXUS_RD_8(sc, reg) \
	bus_space_write_8((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define NEXUS_WR_8(sc, reg, val) \
	bus_space_write_8((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

int	ogxnexus_match(struct device *, void *, void *);
void	ogxnexus_attach(struct device *, struct device *, void *);

const struct cfattach ogxnexus_ca = {
	sizeof(struct ogxnexus_softc), ogxnexus_match, ogxnexus_attach
};

struct cfdriver ogxnexus_cd = {
	NULL, "ogxnexus", DV_DULL
};

int
ogxnexus_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "cavium,octeon-7890-bgx");
}

void
ogxnexus_attach(struct device *parent, struct device *self, void *aux)
{
	struct ogx_attach_args oaa;
	struct fdt_attach_args *faa = aux;
	struct ogxnexus_softc *sc = (struct ogxnexus_softc *)self;
	int i, node;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_bgxid = (faa->fa_reg[0].addr >> 24) & 7;

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh)) {
		printf(": can't map IO space\n");
		return;
	}

	/* Disable all CAM entries. */
	for (i = 0; i < BGX_NCAM; i++)
		NEXUS_WR_8(sc, BGX_CMR_RX_ADR_CAM(i), 0);

	/* Set the number of LMACs per BGX. */
	NEXUS_WR_8(sc, BGX_CMR_RX_LMACS, BGX_NLMAC);
	NEXUS_WR_8(sc, BGX_CMR_TX_LMACS, BGX_NLMAC);

	printf("\n");

	for (node = OF_child(faa->fa_node); node != 0; node = OF_peer(node)) {
		memset(&oaa, 0, sizeof(oaa));
		oaa.oaa_node = node;
		oaa.oaa_bgxid = sc->sc_bgxid;
		oaa.oaa_iot = sc->sc_iot;
		oaa.oaa_ioh = sc->sc_ioh;
		oaa.oaa_dmat = faa->fa_dmat;

		config_found(&sc->sc_dev, &oaa, NULL);
	}
}
