/*      $OpenBSD: if_ath_pci.c,v 1.28 2024/05/24 06:02:53 jsg Exp $   */
/*	$NetBSD: if_ath_pci.c,v 1.7 2004/06/30 05:58:17 mycroft Exp $	*/

/*-
 * Copyright (c) 2002-2004 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

/*
 * PCI front-end for the Atheros Wireless LAN controller driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/gpio.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_rssadapt.h>

#include <dev/gpio/gpiovar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/athvar.h>

/*
 * PCI glue.
 */

struct ath_pci_softc {
	struct ath_softc	sc_sc;

	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_pcitag;

	void			*sc_ih;		/* Interrupt handler. */
};

/* Base Address Register */
#define ATH_BAR0	0x10

int	 ath_pci_match(struct device *, void *, void *);
void	 ath_pci_attach(struct device *, struct device *, void *);
int	 ath_pci_detach(struct device *, int);

const struct cfattach ath_pci_ca = {
	sizeof(struct ath_pci_softc),
	ath_pci_match,
	ath_pci_attach,
	ath_pci_detach,
	ath_activate
};

int
ath_pci_match(struct device *parent, void *match, void *aux)
{
	const char* devname;
	struct pci_attach_args *pa = aux;
	pci_vendor_id_t vendor;

	vendor = PCI_VENDOR(pa->pa_id);
	if (vendor == 0x128c)
		vendor = PCI_VENDOR_ATHEROS;
	devname = ath_hal_probe(vendor, PCI_PRODUCT(pa->pa_id));
	if (devname)
		return 1;

	return 0;
}

void
ath_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct ath_pci_softc *psc = (struct ath_pci_softc *)self;
	struct ath_softc *sc = &psc->sc_sc;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t pt = pa->pa_tag;
	pci_intr_handle_t ih;
	pcireg_t mem_type;
	const char *intrstr = NULL;

	psc->sc_pc = pc;
	psc->sc_pcitag = pt;

	/* 
	 * Setup memory-mapping of PCI registers.
	 */
	mem_type = pci_mapreg_type(pc, pa->pa_tag, ATH_BAR0);
	if (mem_type != PCI_MAPREG_TYPE_MEM &&
	    mem_type != PCI_MAPREG_MEM_TYPE_64BIT) {
		printf(": bad PCI register type %d\n", (int)mem_type);
		goto fail;
	}
	if (pci_mapreg_map(pa, ATH_BAR0, mem_type, 0, &sc->sc_st, &sc->sc_sh,
	    NULL, &sc->sc_ss, 0)) {
		printf(": can't map register space\n");
		goto fail;
	}

	/*
	 * PCI Express check.
	 */
	if (pci_get_capability(pc, pa->pa_tag, PCI_CAP_PCIEXPRESS,
	    NULL, NULL) != 0)
		sc->sc_pcie = 1;

	sc->sc_invalid = 1;

	/*
	 * Arrange interrupt line.
	 */
	if (pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		goto unmap;
	}

	intrstr = pci_intr_string(pc, ih);
	psc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, ath_intr, sc,
	    sc->sc_dev.dv_xname);
	if (psc->sc_ih == NULL) {
		printf(": can't map interrupt\n");
		goto unmap;
	}

	printf(": %s\n", intrstr);

	sc->sc_dmat = pa->pa_dmat;

	if (ath_attach(PCI_PRODUCT(pa->pa_id), sc) == 0)
		return;

	pci_intr_disestablish(pc, psc->sc_ih);
unmap:
	bus_space_unmap(sc->sc_st, sc->sc_sh, sc->sc_ss);
fail:
	return;
}

int
ath_pci_detach(struct device *self, int flags)
{
	struct ath_pci_softc *psc = (struct ath_pci_softc *)self;
	struct ath_softc *sc = &psc->sc_sc;

	ath_detach(&psc->sc_sc, flags);

	if (psc->sc_ih != NULL) {
		pci_intr_disestablish(psc->sc_pc, psc->sc_ih);
		psc->sc_ih = NULL;
	}

	if (sc->sc_ss != 0) {
		bus_space_unmap(sc->sc_st, sc->sc_sh, sc->sc_ss);
		sc->sc_ss = 0;
	}

	return (0);
}
