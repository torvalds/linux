/*	$OpenBSD: sili_pci.c,v 1.16 2024/05/24 06:02:58 jsg Exp $ */

/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
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
#include <sys/timeout.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ata/atascsi.h>

#include <dev/ic/silireg.h>
#include <dev/ic/silivar.h>

int	sili_pci_match(struct device *, void *, void *);
void	sili_pci_attach(struct device *, struct device *, void *);
int	sili_pci_detach(struct device *, int);
int	sili_pci_activate(struct device *, int);

struct sili_pci_softc {
	struct sili_softc	psc_sili;

	pci_chipset_tag_t	psc_pc;
	pcitag_t		psc_tag;

	void			*psc_ih;
};

const struct cfattach sili_pci_ca = {
	sizeof(struct sili_pci_softc),
	sili_pci_match,
	sili_pci_attach,
	sili_pci_detach,
	sili_pci_activate
};

struct sili_device {
	pci_vendor_id_t		sd_vendor;
	pci_product_id_t	sd_product;
	u_int			sd_nports;
};

const struct sili_device *sili_lookup(struct pci_attach_args *);

static const struct sili_device sili_devices[] = {
	{ PCI_VENDOR_CMDTECH,	PCI_PRODUCT_CMDTECH_3124, 4 },
	{ PCI_VENDOR_CMDTECH,	PCI_PRODUCT_CMDTECH_3131, 1 },
	{ PCI_VENDOR_CMDTECH,	PCI_PRODUCT_CMDTECH_3132, 2 },
	{ PCI_VENDOR_CMDTECH,	PCI_PRODUCT_CMDTECH_3531, 1 },
	{ PCI_VENDOR_CMDTECH,	PCI_PRODUCT_CMDTECH_AAR_1220SA, 2 },
	{ PCI_VENDOR_CMDTECH,	PCI_PRODUCT_CMDTECH_AAR_1225SA, 2 },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_3124, 4 }
};

const struct sili_device *
sili_lookup(struct pci_attach_args *pa)
{
	int				i;
	const struct sili_device	*sd;

	for (i = 0; i < nitems(sili_devices); i++) {
		sd = &sili_devices[i];
		if (sd->sd_vendor == PCI_VENDOR(pa->pa_id) &&
		    sd->sd_product == PCI_PRODUCT(pa->pa_id))
			return (sd);
	}

	return (NULL);
}

int
sili_pci_match(struct device *parent, void *match, void *aux)
{
	return (sili_lookup((struct pci_attach_args *)aux) != NULL);
}

void
sili_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct sili_pci_softc		*psc = (void *)self;
	struct sili_softc		*sc = &psc->psc_sili;
	struct pci_attach_args		*pa = aux;
	const struct sili_device	*sd;
	pcireg_t			memtype;
	pci_intr_handle_t		ih;
	const char			*intrstr;

	sd = sili_lookup(pa);

	psc->psc_pc = pa->pa_pc;
	psc->psc_tag = pa->pa_tag;
	psc->psc_ih = NULL;
	sc->sc_dmat = pa->pa_dmat;
	sc->sc_ios_global = 0;
	sc->sc_ios_port = 0;
	sc->sc_nports = sd->sd_nports;

	memtype = pci_mapreg_type(psc->psc_pc, psc->psc_tag,
	    SILI_PCI_BAR_GLOBAL);
	if (pci_mapreg_map(pa, SILI_PCI_BAR_GLOBAL, memtype, 0,
	    &sc->sc_iot_global, &sc->sc_ioh_global,
	    NULL, &sc->sc_ios_global, 0) != 0) {
		printf(": unable to map global registers\n");
		return;
	}

	memtype = pci_mapreg_type(psc->psc_pc, psc->psc_tag,
	    SILI_PCI_BAR_PORT);
	if (pci_mapreg_map(pa, SILI_PCI_BAR_PORT, memtype, 0,
	    &sc->sc_iot_port, &sc->sc_ioh_port,
	    NULL, &sc->sc_ios_port, 0) != 0) {
		printf(": unable to map port registers\n");
		goto unmap_global;
	}

	/* hook up the interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": unable to map interrupt\n");
		goto unmap_port;
	}
	intrstr = pci_intr_string(psc->psc_pc, ih);
	psc->psc_ih = pci_intr_establish(psc->psc_pc, ih, IPL_BIO,
	    sili_intr, sc, sc->sc_dev.dv_xname);
	if (psc->psc_ih == NULL) {
		printf(": unable to map interrupt%s%s\n",
		    intrstr == NULL ? "" : " at ",
		    intrstr == NULL ? "" : intrstr);
		goto unmap_port;
	}
	printf(": %s", intrstr);

	if (sili_attach(sc) != 0) {
		/* error printed by sili_attach */
		goto deintr;
	}

	return;

deintr:
	pci_intr_disestablish(psc->psc_pc, psc->psc_ih);
	psc->psc_ih = NULL;
unmap_port:
	bus_space_unmap(sc->sc_iot_port, sc->sc_ioh_port, sc->sc_ios_port);
	sc->sc_ios_port = 0;
unmap_global:
	bus_space_unmap(sc->sc_iot_global, sc->sc_ioh_global,
	    sc->sc_ios_global);
	sc->sc_ios_global = 0;
}

int
sili_pci_detach(struct device *self, int flags)
{
	struct sili_pci_softc		*psc = (struct sili_pci_softc *)self;
	struct sili_softc		*sc = &psc->psc_sili;
	int				rv;

	rv = sili_detach(sc, flags);
	if (rv != 0)
		return (rv);

	if (psc->psc_ih != NULL) {
		pci_intr_disestablish(psc->psc_pc, psc->psc_ih);
		psc->psc_ih = NULL;
	}
	if (sc->sc_ios_port != 0) {
		bus_space_unmap(sc->sc_iot_port, sc->sc_ioh_port,
		    sc->sc_ios_port);
		sc->sc_ios_port = 0;
	}
	if (sc->sc_ios_global != 0) {
		bus_space_unmap(sc->sc_iot_global, sc->sc_ioh_global,
		    sc->sc_ios_global);
		sc->sc_ios_global = 0;
	}

	return (0);
}

int
sili_pci_activate(struct device *self, int act)
{
	struct sili_softc		*sc = (struct sili_softc *)self;
	int				 rv = 0;

	switch (act) {
	case DVACT_RESUME:
		sili_resume(sc);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}
