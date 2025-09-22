/* $OpenBSD: mvahci.c,v 1.2 2021/10/24 17:52:27 mpi Exp $ */
/*
 * Copyright (c) 2013,2017 Patrick Wildt <patrick@blueri.se>
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

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <armv7/marvell/mvmbusvar.h>

#include <dev/ic/ahcireg.h>
#include <dev/ic/ahcivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

#define MVAHCI_READ(sc, reg) \
	bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define MVAHCI_WRITE(sc, reg, val) \
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

#define MVAHCI_NWINDOW			4
#define MVAHCI_WINDOW_CTRL(x)		(0x60 + ((x) << 4))
#define MVAHCI_WINDOW_BASE(x)		(0x64 + ((x) << 4))
#define MVAHCI_WINDOW_SIZE(x)		(0x68 + ((x) << 4))

#define MVAHCI_TARGET(target)		(((target) & 0xf) << 4)
#define MVAHCI_ATTR(attr)		(((attr) & 0xff) << 8)
#define MVAHCI_BASEADDR(base)		((base) >> 16)
#define MVAHCI_SIZE(size)		(((size) - 1) & 0xffff0000)
#define MVAHCI_WINEN			(1 << 0)

void	mvahci_wininit(struct ahci_softc *);

int	mvahci_match(struct device *, void *, void *);
void	mvahci_attach(struct device *, struct device *, void *);
int	mvahci_detach(struct device *, int);
int	mvahci_activate(struct device *, int);

extern int ahci_intr(void *);

const struct cfattach mvahci_ca = {
	sizeof(struct ahci_softc),
	mvahci_match,
	mvahci_attach,
	mvahci_detach,
	mvahci_activate
};

struct cfdriver mvahci_cd = {
	NULL, "mvahci", DV_DULL
};

void
mvahci_wininit(struct ahci_softc *sc)
{
	int i;

	if (mvmbus_dram_info == NULL)
		panic("%s: mbus dram information not set up", __func__);

	for (i = 0; i < MVAHCI_NWINDOW; i++) {
		MVAHCI_WRITE(sc, MVAHCI_WINDOW_CTRL(i), 0);
		MVAHCI_WRITE(sc, MVAHCI_WINDOW_BASE(i), 0);
		MVAHCI_WRITE(sc, MVAHCI_WINDOW_SIZE(i), 0);
	}

	for (i = 0; i < mvmbus_dram_info->numcs; i++) {
		struct mbus_dram_window *win = &mvmbus_dram_info->cs[i];

		MVAHCI_WRITE(sc, MVAHCI_WINDOW_CTRL(i),
		    MVAHCI_WINEN |
		    MVAHCI_TARGET(mvmbus_dram_info->targetid) |
		    MVAHCI_ATTR(win->attr));
		MVAHCI_WRITE(sc, MVAHCI_WINDOW_BASE(i),
		    MVAHCI_BASEADDR(win->base));
		MVAHCI_WRITE(sc, MVAHCI_WINDOW_SIZE(i),
		    MVAHCI_SIZE(win->size));
	}
}

int
mvahci_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-380-ahci");
}

void
mvahci_attach(struct device *parent, struct device *self, void *aux)
{
	struct ahci_softc *sc = (struct ahci_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	sc->sc_ios = faa->fa_reg[0].size;
	sc->sc_dmat = faa->fa_dmat;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("mvahci_attach: bus_space_map failed!");

	clock_enable_all(faa->fa_node);
	reset_deassert_all(faa->fa_node);

	sc->sc_ih = arm_intr_establish_fdt(faa->fa_node, IPL_BIO,
	    ahci_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt\n");
		goto unmap;
	}

	/* Set up MBUS windows. */
	mvahci_wininit(sc);

	printf(":");

	if (ahci_attach(sc) != 0) {
		/* error printed by ahci_attach */
		goto irq;
	}

	return;
irq:
	arm_intr_disestablish_fdt(sc->sc_ih);
unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
}

int
mvahci_detach(struct device *self, int flags)
{
	struct ahci_softc *sc = (struct ahci_softc *)self;

	ahci_detach(sc, flags);
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	return 0;
}

int
mvahci_activate(struct device *self, int act)
{
	struct ahci_softc *sc = (struct ahci_softc *)self;

	return ahci_activate((struct device *)sc, act);
}
