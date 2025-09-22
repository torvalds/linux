/*	$OpenBSD: mvpxa.c,v 1.4 2022/01/18 11:36:21 patrick Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis
 * Copyright (c) 2017 Patrick Wildt <patrick@blueri.se>
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

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <armv7/marvell/mvmbusvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#include <dev/sdmmc/sdhcreg.h>
#include <dev/sdmmc/sdhcvar.h>

#define MVPXA_READ(sc, reg) \
	bus_space_read_4((sc)->sc_iot, (sc)->mbus_ioh, (reg))
#define MVPXA_WRITE(sc, reg, val) \
	bus_space_write_4((sc)->sc_iot, (sc)->mbus_ioh, (reg), (val))

#define MVPXA_NWINDOW			8
#define MVPXA_CTRL(x)			(0x80 + ((x) << 3))
#define MVPXA_BASE(x)			(0x84 + ((x) << 3))

#define MVPXA_TARGET(target)		(((target) & 0xf) << 4)
#define MVPXA_ATTR(attr)		(((attr) & 0xff) << 8)
#define MVPXA_SIZE(size)		(((size) - 1) & 0xffff0000)
#define MVPXA_WINEN			(1 << 0)

struct mvpxa_softc {
	struct sdhc_softc	sc;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	mbus_ioh;
	bus_space_handle_t	conf_ioh;
	bus_size_t		sc_size;
	void			*sc_ih;

	struct sdhc_host 	*sc_host;
};

int	mvpxa_match(struct device *, void *, void *);
void	mvpxa_attach(struct device *, struct device *, void *);

struct cfdriver mvpxa_cd = {
	NULL, "mvpxa", DV_DULL
};

const struct cfattach mvpxa_ca = {
	sizeof(struct mvpxa_softc), mvpxa_match, mvpxa_attach
};

void	mvpxa_wininit(struct mvpxa_softc *);

void
mvpxa_wininit(struct mvpxa_softc *sc)
{
	int i;

	if (mvmbus_dram_info == NULL)
		panic("%s: mbus dram information not set up", __func__);

	for (i = 0; i < MVPXA_NWINDOW; i++) {
		MVPXA_WRITE(sc, MVPXA_CTRL(i), 0);
		MVPXA_WRITE(sc, MVPXA_BASE(i), 0);
	}

	for (i = 0; i < mvmbus_dram_info->numcs; i++) {
		struct mbus_dram_window *win = &mvmbus_dram_info->cs[i];

		MVPXA_WRITE(sc, MVPXA_CTRL(i),
		    MVPXA_WINEN |
		    MVPXA_TARGET(mvmbus_dram_info->targetid) |
		    MVPXA_ATTR(win->attr) |
		    MVPXA_SIZE(win->size));
		MVPXA_WRITE(sc, MVPXA_BASE(i), win->base);
	}
}


int
mvpxa_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-380-sdhci");
}

void
mvpxa_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvpxa_softc *sc = (struct mvpxa_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint64_t capmask = 0, capset = 0;

	if (faa->fa_nreg < 3) {
		printf(": not enough registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	sc->sc_size = faa->fa_reg[0].size;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->mbus_ioh)) {
		printf(": can't map registers\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_size);
		return;
	}

	if (bus_space_map(sc->sc_iot, faa->fa_reg[2].addr,
	    faa->fa_reg[2].size, 0, &sc->conf_ioh)) {
		printf(": can't map registers\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_size);
		bus_space_unmap(sc->sc_iot, sc->mbus_ioh, faa->fa_reg[1].size);
		return;
	}

	pinctrl_byname(faa->fa_node, "default");

	clock_enable_all(faa->fa_node);
	reset_deassert_all(faa->fa_node);

	/* Set up MBUS windows. */
	mvpxa_wininit(sc);

	sc->sc_ih = arm_intr_establish_fdt(faa->fa_node, IPL_BIO,
	    sdhc_intr, sc, sc->sc.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	printf("\n");

	sc->sc.sc_host = &sc->sc_host;
	sc->sc.sc_dmat = faa->fa_dmat;

	if (OF_getproplen(faa->fa_node, "no-1-8-v") >= 0)
		capmask |= SDHC_VOLTAGE_SUPP_1_8V;

	sdhc_host_found(&sc->sc, sc->sc_iot, sc->sc_ioh, sc->sc_size, 1,
	    capmask, capset);
	return;

unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_size);
	bus_space_unmap(sc->sc_iot, sc->mbus_ioh, faa->fa_reg[1].size);
	bus_space_unmap(sc->sc_iot, sc->conf_ioh, faa->fa_reg[2].size);
}
