/* $OpenBSD: mvdog.c,v 1.2 2021/10/24 17:52:26 mpi Exp $ */
/*
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
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

#define CNTR_RETRIGGER		0
#define CNTR_WDOG		1

#define CNTR_CTRL(x)		((x) * 0x10)
#define  CNTR_CTRL_ENABLE		(1 << 0)

#define WDT_TIMER_SELECT	0x64

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct mvdog_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	struct regmap		*sc_rm;
};

int	 mvdog_match(struct device *, void *, void *);
void	 mvdog_attach(struct device *, struct device *, void *);

const struct cfattach mvdog_ca = {
	sizeof (struct mvdog_softc), mvdog_match, mvdog_attach
};

struct cfdriver mvdog_cd = {
	NULL, "mvdog", DV_DULL
};

int
mvdog_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-3700-wdt");
}

void
mvdog_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvdog_softc *sc = (struct mvdog_softc *)self;
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

	sc->sc_rm = regmap_byphandle(OF_getpropint(faa->fa_node,
	    "marvell,system-controller", 0));
	if (sc->sc_rm == NULL) {
		printf(": can't get regmap\n");
		return;
	}

	printf("\n");

	/* Disable watchdog timer. */
	HCLR4(sc, CNTR_CTRL(CNTR_WDOG), CNTR_CTRL_ENABLE);
	HCLR4(sc, CNTR_CTRL(CNTR_RETRIGGER), CNTR_CTRL_ENABLE);
	regmap_write_4(sc->sc_rm, WDT_TIMER_SELECT, 0);
}
