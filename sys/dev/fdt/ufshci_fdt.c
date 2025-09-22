/*      $OpenBSD: ufshci_fdt.c,v 1.2 2024/10/08 00:46:29 jsg Exp $ */
/*
 * Copyright (c) 2024 Marcus Glocker <mglocker@openbsd.org>
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
 
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/ufshcivar.h>

int	ufshci_fdt_match(struct device *, void *, void *);
void	ufshci_fdt_attach(struct device *, struct device *, void *);

const struct cfattach ufshci_fdt_ca = {
	sizeof(struct ufshci_softc),
	ufshci_fdt_match,
	ufshci_fdt_attach,
	NULL,
	ufshci_activate
};

int
ufshci_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "jedec,ufs-2.0");
}

void
ufshci_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct ufshci_softc *sc = (struct ufshci_softc *)self;
	struct fdt_attach_args *faa = aux;
	void *ih;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	sc->sc_ios = faa->fa_reg[0].size;
	sc->sc_dmat = faa->fa_dmat;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	ih = fdt_intr_establish(faa->fa_node, IPL_BIO,
	    ufshci_intr, sc, sc->sc_dev.dv_xname);
	if (ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	clock_set_assigned(faa->fa_node);
	clock_enable_all(faa->fa_node);
	phy_enable(faa->fa_node, "ufsphy");

	if (ufshci_attach(sc) != 0)
		goto irq;

	return;

irq:
	fdt_intr_disestablish(ih);
unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
}
