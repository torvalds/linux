/* $OpenBSD: mfi_pci.c,v 1.32 2024/05/24 06:02:58 jsg Exp $ */
/*
 * Copyright (c) 2006 Marco Peereboom <marco@peereboom.us>
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
#include <sys/rwlock.h>
#include <sys/sensors.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ic/mfireg.h>
#include <dev/ic/mfivar.h>

#define	MFI_BAR		0x10
#define	MFI_BAR_GEN2	0x14
#define	MFI_PCI_MEMSIZE	0x2000 /* 8k */

int	mfi_pci_match(struct device *, void *, void *);
void	mfi_pci_attach(struct device *, struct device *, void *);

const struct cfattach mfi_pci_ca = {
	sizeof(struct mfi_softc), mfi_pci_match, mfi_pci_attach
};

static const
struct	mfi_pci_device {
	pcireg_t		mpd_vendor;
	pcireg_t		mpd_product;
	enum mfi_iop		mpd_iop;
} mfi_pci_devices[] = {
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_SAS,
	  MFI_IOP_XSCALE },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_VERDE_ZCR,
	  MFI_IOP_XSCALE },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS1078,
	  MFI_IOP_PPC },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS1078DE,
	  MFI_IOP_PPC },
	{ PCI_VENDOR_DELL,	PCI_PRODUCT_DELL_PERC5,
	  MFI_IOP_XSCALE },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2108_1,
	  MFI_IOP_GEN2 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2108_2,
	  MFI_IOP_GEN2 },
	{ PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2008_1,
	  MFI_IOP_SKINNY }
};

const struct mfi_pci_device *mfi_pci_find_device(struct pci_attach_args *);

const struct mfi_pci_device *
mfi_pci_find_device(struct pci_attach_args *pa)
{
	const struct mfi_pci_device *mpd;
	int i;

	for (i = 0; i < nitems(mfi_pci_devices); i++) {
		mpd = &mfi_pci_devices[i];

		if (mpd->mpd_vendor == PCI_VENDOR(pa->pa_id) &&
		    mpd->mpd_product == PCI_PRODUCT(pa->pa_id))
			return (mpd);
	}

	return (NULL);
}

int
mfi_pci_match(struct device *parent, void *match, void *aux)
{
	return ((mfi_pci_find_device(aux) != NULL) ? 1 : 0);
}

void
mfi_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct mfi_softc	*sc = (struct mfi_softc *)self;
	struct pci_attach_args	*pa = aux;
	const struct mfi_pci_device *mpd;
	pci_intr_handle_t	ih;
	bus_size_t		size;
	pcireg_t		reg;
	int			regbar;

	mpd = mfi_pci_find_device(pa);
	if (mpd == NULL) {
		printf(": can't find matching pci device\n");
		return;
	}

	if (mpd->mpd_iop == MFI_IOP_GEN2 || mpd->mpd_iop == MFI_IOP_SKINNY)
		regbar = MFI_BAR_GEN2;
	else
		regbar = MFI_BAR;

	reg = pci_mapreg_type(pa->pa_pc, pa->pa_tag, regbar);
	if (pci_mapreg_map(pa, regbar, reg, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &size, MFI_PCI_MEMSIZE)) {
		printf(": can't map controller pci space\n");
		return;
	}

	sc->sc_dmat = pa->pa_dmat;

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		goto unmap;
	}
	printf(": %s\n", pci_intr_string(pa->pa_pc, ih));

	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO | IPL_MPSAFE,
	    mfi_intr, sc, sc->sc_dev.dv_xname);
	if (!sc->sc_ih) {
		printf("%s: can't establish interrupt\n", DEVNAME(sc));
		goto unmap;
	}

	if (mfi_attach(sc, mpd->mpd_iop)) {
		printf("%s: can't attach\n", DEVNAME(sc));
		goto unintr;
	}

	return;
unintr:
	pci_intr_disestablish(pa->pa_pc, sc->sc_ih);
	sc->sc_ih = NULL;
unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, size);
}
