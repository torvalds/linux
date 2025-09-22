/*	$OpenBSD: ufshci_pci.c,v 1.5 2024/10/08 00:46:29 jsg Exp $ */

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
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/ufshcivar.h>

#define UFSHCI_PCI_BAR		0x10
#define UFSHCI_PCI_INTERFACE	0x01

struct ufshci_pci_softc {
	struct ufshci_softc	 psc_ufshci;

	pci_chipset_tag_t	 psc_pc;
	void			*psc_ih;
};

int	ufshci_pci_match(struct device *, void *, void *);
void	ufshci_pci_attach(struct device *, struct device *, void *);
int	ufshci_pci_detach(struct device *, int);

const struct cfattach ufshci_pci_ca = {
	sizeof(struct ufshci_pci_softc),
	ufshci_pci_match,
	ufshci_pci_attach,
	ufshci_pci_detach,
	ufshci_activate
};

int
ufshci_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_MASS_STORAGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_MASS_STORAGE_UFS &&
	    PCI_INTERFACE(pa->pa_class) == UFSHCI_PCI_INTERFACE)
		return 1;

	return 0;
}

void
ufshci_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct ufshci_pci_softc *psc = (struct ufshci_pci_softc *)self;
	struct ufshci_softc *sc = &psc->psc_ufshci;
	struct pci_attach_args *pa = aux;
	pcireg_t maptype;
	pci_intr_handle_t ih;

	psc->psc_pc = pa->pa_pc;
	sc->sc_dmat = pa->pa_dmat;

	/* Map registers */
	maptype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, UFSHCI_PCI_BAR);
	if (pci_mapreg_map(pa, UFSHCI_PCI_BAR, maptype, 0, &sc->sc_iot,
	    &sc->sc_ioh, NULL, &sc->sc_ios, 0) != 0) {
		printf(": can't map registers\n");
		return;
	}

	/* Map interrupt */
	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}
	printf(": %s", pci_intr_string(pa->pa_pc, ih));

	/* Establish interrupt */
	psc->psc_ih = pci_intr_establish(psc->psc_pc, ih, IPL_BIO, ufshci_intr,
	    sc, sc->sc_dev.dv_xname);
	if (psc->psc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	/* Call the driver attach */
	ufshci_attach(sc);
}

int
ufshci_pci_detach(struct device *self, int flags)
{
	return 0;
}
