/*	$OpenBSD: rtsx_pci.c,v 1.16 2023/04/13 15:07:43 miod Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 * Copyright (c) 2012 Stefan Sperling <stsp@openbsd.org>
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
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/ic/rtsxreg.h>
#include <dev/ic/rtsxvar.h>
#include <dev/sdmmc/sdmmcvar.h>

#define RTSX_PCI_BAR 		0x10
#define RTSX_PCI_BAR_525A 	0x14

struct rtsx_pci_softc {
	struct rtsx_softc sc;
	void *sc_ih;
};

int	rtsx_pci_match(struct device *, void *, void *);
void	rtsx_pci_attach(struct device *, struct device *, void *);

const struct cfattach rtsx_pci_ca = {
	sizeof(struct rtsx_pci_softc), rtsx_pci_match, rtsx_pci_attach,
	NULL, rtsx_activate
};

int
rtsx_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	/* 
	 * Explicitly match the UNDEFINED device class only. Some RTS5209
	 * devices advertise a SYSTEM/SDHC class in addition to the UNDEFINED
	 * device class. Let sdhc(4) handle the SYSTEM/SDHC ones.
	 */
	if (PCI_VENDOR(pa->pa_id) != PCI_VENDOR_REALTEK ||
	    PCI_CLASS(pa->pa_class) != PCI_CLASS_UNDEFINED)
		return 0;

	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RTS5209 ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RTS5227 ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RTS5229 ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RTS522A ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RTS5249 ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RTS525A ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RTL8402 ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RTL8411 ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RTL8411B)
		return 1;

	return 0;
}

void
rtsx_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct rtsx_pci_softc *sc = (struct rtsx_pci_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	char const *intrstr;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_size_t size;
	int flags;
	int bar = RTSX_PCI_BAR;
	pcireg_t type;

	if ((pci_conf_read(pa->pa_pc, pa->pa_tag, RTSX_CFG_PCI)
	    & RTSX_CFG_ASIC) != 0) {
		printf("%s: no asic\n", sc->sc.sc_dev.dv_xname);
		return;
	}

	if (pci_intr_map_msi(pa, &ih) && pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_SDMMC,
	    rtsx_intr, sc, sc->sc.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}
	printf(": %s\n", intrstr);

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_REALTEK_RTS5209:
		flags = RTSX_F_5209;
		break;
	case PCI_PRODUCT_REALTEK_RTS5229:
	case PCI_PRODUCT_REALTEK_RTS5249:
		flags = RTSX_F_5229;
		break;
	case PCI_PRODUCT_REALTEK_RTS525A:
		flags = RTSX_F_525A;
		bar = RTSX_PCI_BAR_525A;
		break;
	default:
		flags = 0;
		break;
	}

	type = pci_mapreg_type(pa->pa_pc, pa->pa_tag, bar);
	if (pci_mapreg_info(pa->pa_pc, pa->pa_tag, bar, type, NULL, NULL,
	    NULL) != 0) {
		printf("%s: can't find registers\n", sc->sc.sc_dev.dv_xname);
	    	return;
	}
	if (pci_mapreg_map(pa, bar, type, 0, &iot, &ioh, NULL, &size, 0)) {
		printf("%s: can't map registers\n", sc->sc.sc_dev.dv_xname);
		return;
	}

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	if (rtsx_attach(&sc->sc, iot, ioh, size, pa->pa_dmat, flags) != 0)
		printf("%s: can't initialize chip\n", sc->sc.sc_dev.dv_xname);
}
