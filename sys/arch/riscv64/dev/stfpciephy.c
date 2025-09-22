/*	$OpenBSD: stfpciephy.c,v 1.2 2024/10/17 01:57:18 jsg Exp $	*/
/*
 * Copyright (c) 2023 Mark Kettenis <kettenis@openbsd.org>
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

#define PCIE_KVO_LEVEL		0x28
#define  PCIE_KVO_FINE_TUNE_LEVEL	0x91
#define PCIE_KVO_TUNE_SIGNAL	0x80
#define  PCIE_KVO_FINE_TUNE_SIGNAL	0x0c

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct stfpciephy_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct phy_device	sc_pd;
};

int	stfpciephy_match(struct device *, void *, void *);
void	stfpciephy_attach(struct device *, struct device *, void *);

const struct cfattach stfpciephy_ca = {
	sizeof (struct stfpciephy_softc), stfpciephy_match, stfpciephy_attach
};

struct cfdriver stfpciephy_cd = {
	NULL, "stfpciephy", DV_DULL
};

int	stfpciephy_enable(void *, uint32_t *);

int
stfpciephy_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "starfive,jh7110-pcie-phy");
}

void
stfpciephy_attach(struct device *parent, struct device *self, void *aux)
{
	struct stfpciephy_softc *sc = (struct stfpciephy_softc *)self;
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

	printf("\n");

	HWRITE4(sc, PCIE_KVO_LEVEL, PCIE_KVO_FINE_TUNE_LEVEL);
	HWRITE4(sc, PCIE_KVO_TUNE_SIGNAL, PCIE_KVO_FINE_TUNE_SIGNAL);

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	sc->sc_pd.pd_enable = stfpciephy_enable;
	phy_register(&sc->sc_pd);
}

int
stfpciephy_enable(void *cookie, uint32_t *cells)
{
	return 0;
}
