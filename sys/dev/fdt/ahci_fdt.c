/* $OpenBSD: ahci_fdt.c,v 1.8 2023/04/08 02:32:38 dlg Exp $ */
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/queue.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ic/ahcireg.h>
#include <dev/ic/ahcivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

int	ahci_fdt_match(struct device *, void *, void *);
void	ahci_fdt_attach(struct device *, struct device *, void *);
int	ahci_fdt_detach(struct device *, int);
int	ahci_fdt_activate(struct device *, int);

extern int ahci_intr(void *);

const struct cfattach ahci_fdt_ca = {
	sizeof(struct ahci_softc),
	ahci_fdt_match,
	ahci_fdt_attach,
	ahci_fdt_detach,
	ahci_fdt_activate
};

int
ahci_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "generic-ahci") ||
	    OF_is_compatible(faa->fa_node, "cavium,octeon-7130-ahci") ||
	    OF_is_compatible(faa->fa_node, "marvell,armada-3700-ahci") ||
	    OF_is_compatible(faa->fa_node, "snps,dwc-ahci");
}

void
ahci_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct ahci_softc *sc = (struct ahci_softc *) self;
	struct fdt_attach_args *faa = aux;
	uint32_t pi;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	sc->sc_ios = faa->fa_reg[0].size;
	sc->sc_dmat = faa->fa_dmat;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("ahci_fdt_attach: bus_space_map failed!");

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO,
	    ahci_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	clock_set_assigned(faa->fa_node);
	clock_enable_all(faa->fa_node);
	phy_enable(faa->fa_node, "sata-phy");

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, 0x118, 1U << 22);
	pi = OF_getpropint(faa->fa_node, "ports-implemented", 0x0);
	if (pi != 0)
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, AHCI_REG_PI, pi);

	printf(":");

	if (ahci_attach(sc) != 0) {
		/* error printed by ahci_attach */
		goto irq; /* disable phy and clocks? */
	}

	return;
irq:
	fdt_intr_disestablish(sc->sc_ih);
unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
}

int
ahci_fdt_detach(struct device *self, int flags)
{
	struct ahci_softc *sc = (struct ahci_softc *) self;

	ahci_detach(sc, flags);
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	return 0;
}

int
ahci_fdt_activate(struct device *self, int act)
{
	struct ahci_softc *sc = (struct ahci_softc *) self;

	return ahci_activate((struct device *)sc, act);
}
