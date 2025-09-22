/*	$OpenBSD: nvme_pci.c,v 1.13 2024/11/19 02:31:35 jcs Exp $ */

/*
 * Copyright (c) 2014 David Gwynne <dlg@openbsd.org>
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
#include <sys/queue.h>
#include <sys/mutex.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/nvmereg.h>
#include <dev/ic/nvmevar.h>

#define NVME_PCI_BAR		0x10
#define NVME_PCI_INTERFACE	0x02

struct nvme_pci_softc {
	struct nvme_softc	psc_nvme;
	pci_chipset_tag_t	psc_pc;
};

int	nvme_pci_match(struct device *, void *, void *);
void	nvme_pci_attach(struct device *, struct device *, void *);
int	nvme_pci_detach(struct device *, int);
int	nvme_pci_activate(struct device *, int);

const struct cfattach nvme_pci_ca = {
	sizeof(struct nvme_pci_softc),
	nvme_pci_match,
	nvme_pci_attach,
	nvme_pci_detach,
	nvme_pci_activate
};

int
nvme_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_MASS_STORAGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_MASS_STORAGE_NVM &&
	    PCI_INTERFACE(pa->pa_class) == NVME_PCI_INTERFACE)
		return (1);

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_APPLE &&
	    (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_APPLE_NVME1 ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_APPLE_NVME2))
	    	return (1);

	return (0);
}

void
nvme_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct nvme_pci_softc *psc = (struct nvme_pci_softc *)self;
	struct nvme_softc *sc = &psc->psc_nvme;
	struct pci_attach_args *pa = aux;
	pcireg_t maptype;
	pci_intr_handle_t ih;
	int msi = 1;

	psc->psc_pc = pa->pa_pc;
	sc->sc_dmat = pa->pa_dmat;

	printf(": ");

	maptype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, NVME_PCI_BAR);
	if (pci_mapreg_map(pa, NVME_PCI_BAR, maptype, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &sc->sc_ios, 0) != 0) {
		printf("unable to map registers\n");
		return;
	}

	if (pci_intr_map_msix(pa, 0, &ih) != 0 &&
	    pci_intr_map_msi(pa, &ih) != 0) {
		if (pci_intr_map(pa, &ih) != 0) {
			printf("unable to map interrupt\n");
			goto unmap;
		}
		msi = 0;
	}

	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO,
	    msi ? nvme_intr : nvme_intr_intx, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL) {
		printf("unable to establish interrupt\n");
		goto unmap;
	}

	printf("%s, ", pci_intr_string(pa->pa_pc, ih));
	if (nvme_attach(sc) != 0) {
		/* error printed by nvme_attach() */
		goto disestablish;
	}

	return;

disestablish:
	pci_intr_disestablish(pa->pa_pc, sc->sc_ih);
	sc->sc_ih = NULL;

unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	sc->sc_ios = 0;
}

int
nvme_pci_detach(struct device *self, int flags)
{
	return config_detach_children(self, flags);
}

int
nvme_pci_activate(struct device *self, int act)
{
	struct nvme_pci_softc *psc = (struct nvme_pci_softc *)self;

	return (nvme_activate(&psc->psc_nvme, act));
}
