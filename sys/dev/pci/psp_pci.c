/*	$OpenBSD: psp_pci.c,v 1.2 2024/11/08 17:34:22 bluhm Exp $	*/

/*
 * Copyright (c) 2023-2024 Hans-Joerg Hoexer <hshoexer@genua.de>
 * Copyright (c) 2024 Alexander Bluhm <bluhm@openbsd.org>
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

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <dev/ic/ccpvar.h>
#include <dev/ic/pspvar.h>

static const struct pci_matchid psp_pci_devices[] = {
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_17_CCP_1 },
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_17_3X_CCP },
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_19_1X_PSP },
};

int
psp_pci_match(struct ccp_softc *sc, struct pci_attach_args *pa)
{
	bus_size_t reg_capabilities;
	uint32_t capabilities;

	if (!pci_matchbyid(pa, psp_pci_devices, nitems(psp_pci_devices)))
		return (0);

	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_17_CCP_1)
		reg_capabilities = PSPV1_REG_CAPABILITIES;
	else
		reg_capabilities = PSP_REG_CAPABILITIES;
	capabilities = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    reg_capabilities);
	if (!ISSET(capabilities, PSP_CAP_SEV))
		return (0);

	return (1);
}

void
psp_pci_intr_map(struct ccp_softc *sc, struct pci_attach_args *pa)
{
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	bus_size_t reg_inten, reg_intsts;

	/* clear and disable interrupts */
	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_17_CCP_1) {
		reg_inten = PSPV1_REG_INTEN;
		reg_intsts = PSPV1_REG_INTSTS;
	} else {
		reg_inten = PSP_REG_INTEN;
		reg_intsts = PSP_REG_INTSTS;
	}
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg_inten, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg_intsts, -1);

	if (pci_intr_map_msix(pa, 0, &ih) != 0 &&
	    pci_intr_map_msi(pa, &ih) != 0 && pci_intr_map(pa, &ih) != 0) {
		printf(": couldn't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_irqh = pci_intr_establish(pa->pa_pc, ih, IPL_BIO | IPL_MPSAFE,
	    psp_sev_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_irqh != NULL)
		printf(": %s", intrstr);
}

void
psp_pci_attach(struct ccp_softc *sc, struct pci_attach_args *pa)
{
	struct psp_attach_args arg;
	struct device *self = (struct device *)sc;
	bus_size_t reg_capabilities;

	memset(&arg, 0, sizeof(arg));
	arg.iot = sc->sc_iot;
	arg.ioh = sc->sc_ioh;
	arg.dmat = pa->pa_dmat;
	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_AMD_17_CCP_1:
		arg.version = 1;
		reg_capabilities = PSPV1_REG_CAPABILITIES;
		break;
	case PCI_PRODUCT_AMD_17_3X_CCP:
		arg.version = 2;
		reg_capabilities = PSP_REG_CAPABILITIES;
		break;
	case PCI_PRODUCT_AMD_19_1X_PSP:
		arg.version = 4;
		reg_capabilities = PSP_REG_CAPABILITIES;
		break;
	default:
		reg_capabilities = PSP_REG_CAPABILITIES;
		break;
	}
	arg.capabilities = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    reg_capabilities);

	sc->sc_psp = config_found_sm(self, &arg, pspprint, pspsubmatch);
	if (sc->sc_psp == NULL) {
		pci_intr_disestablish(pa->pa_pc, sc->sc_irqh);
		return;
	}
}
