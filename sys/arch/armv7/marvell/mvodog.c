/* $OpenBSD: mvodog.c,v 1.1 2023/03/02 09:56:52 jmatthew Exp $ */
/*
 * Copyright (c) 2022 Jonathan Matthew <jmatthew@openbsd.org>
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

#define WDT_ENABLE		(1 << 8)

#define RSTOUT_ENABLE		(1 << 8)
#define RSTOUT_MASK		(1 << 10)

#define HREAD4(sc, ioh, reg)						\
	(bus_space_read_4((sc)->sc_iot, (ioh), (reg)))
#define HWRITE4(sc, ioh, reg, val)					\
	bus_space_write_4((sc)->sc_iot, (ioh), (reg), (val))
#define HSET4(sc, ioh, reg, bits)					\
	HWRITE4((sc), (ioh), (reg), HREAD4((sc), (ioh), (reg)) | (bits))
#define HCLR4(sc, ioh, reg, bits)					\
	HWRITE4((sc), (ioh), (reg), HREAD4((sc), (ioh), (reg)) & ~(bits))

struct mvodog_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_reg_ioh;
	bus_space_handle_t	 sc_rstout_ioh;
	bus_space_handle_t	 sc_rstout_mask_ioh;
};

int	 mvodog_match(struct device *, void *, void *);
void	 mvodog_attach(struct device *, struct device *, void *);

const struct cfattach mvodog_ca = {
	sizeof (struct mvodog_softc), mvodog_match, mvodog_attach
};

struct cfdriver mvodog_cd = {
	NULL, "mvodog", DV_DULL
};

int
mvodog_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-380-wdt");
}

void
mvodog_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvodog_softc *sc = (struct mvodog_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_reg_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_rstout_ioh)) {
		printf(": can't map rstout\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, faa->fa_reg[2].addr,
	    faa->fa_reg[2].size, 0, &sc->sc_rstout_mask_ioh)) {
		printf(": can't map rstout mask\n");
		return;
	}

	printf("\n");

	/* Disable watchdog timer. */
	HSET4(sc, sc->sc_rstout_mask_ioh, 0, RSTOUT_MASK);
	HCLR4(sc, sc->sc_rstout_ioh, 0, RSTOUT_ENABLE);
	HCLR4(sc, sc->sc_reg_ioh, 0, WDT_ENABLE);
}
