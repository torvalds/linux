/*	$OpenBSD: if_atw_pci.c,v 1.20 2024/05/24 06:02:53 jsg Exp $	*/
/*	$NetBSD: if_atw_pci.c,v 1.7 2004/07/23 07:07:55 dyoung Exp $	*/

/*-
 * Copyright (c) 1998, 1999, 2000, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center; Charles M. Hannum; and David Young.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * PCI bus front-end for the ADMtek ADM8211 802.11 MAC/BBP chip.
 *
 * Derived from the ``Tulip'' PCI bus front-end.
 */

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/device.h>
 
#include <net/if.h>
#include <net/if_media.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_var.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ic/atwreg.h>
#include <dev/ic/atwvar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

/*
 * PCI configuration space registers used by the ADM8211.
 */
#define	ATW_PCI_IOBA		0x10	/* i/o mapped base */
#define	ATW_PCI_MMBA		0x14	/* memory mapped base */

struct atw_pci_softc {
	struct atw_softc	psc_atw;	/* real ADM8211 softc */

	pci_intr_handle_t	psc_ih;		/* interrupt handle */
	void			*psc_intrcookie;

	pci_chipset_tag_t	psc_pc;		/* our PCI chipset */
	pcitag_t		psc_pcitag;	/* our PCI tag */
};

int	atw_pci_match(struct device *, void *, void *);
void	atw_pci_attach(struct device *, struct device *, void *);
int	atw_pci_detach(struct device *, int);

const struct cfattach atw_pci_ca = {
    sizeof (struct atw_softc), atw_pci_match, atw_pci_attach, atw_pci_detach,
    atw_activate
};

const struct pci_matchid atw_pci_devices[] = {
	{ PCI_VENDOR_ADMTEK,		PCI_PRODUCT_ADMTEK_ADM8211 },
	{ PCI_VENDOR_3COM,		PCI_PRODUCT_3COM_3CRSHPW796 }
};

int
atw_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, atw_pci_devices,
	    nitems(atw_pci_devices)));
}

static int
atw_pci_enable(struct atw_softc *sc)
{
	struct atw_pci_softc *psc = (void *)sc;

	/* Establish the interrupt. */
	psc->psc_intrcookie = pci_intr_establish(psc->psc_pc, psc->psc_ih,
	    IPL_NET, atw_intr, sc, sc->sc_dev.dv_xname);
	if (psc->psc_intrcookie == NULL) {
		printf("%s: unable to establish interrupt\n",
		    sc->sc_dev.dv_xname);
		return (1);
	}

	return (0);
}

static void
atw_pci_disable(struct atw_softc *sc)
{
	struct atw_pci_softc *psc = (void *)sc;

	/* Unhook the interrupt handler. */
	pci_intr_disestablish(psc->psc_pc, psc->psc_intrcookie);
	psc->psc_intrcookie = NULL;
}

void
atw_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct atw_pci_softc *psc = (void *) self;
	struct atw_softc *sc = &psc->psc_atw;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	const char *intrstr = NULL;
	bus_space_tag_t iot, memt;
	bus_space_handle_t ioh, memh;
	bus_size_t iosize, memsize;
	int ioh_valid, memh_valid;

	psc->psc_pc = pa->pa_pc;
	psc->psc_pcitag = pa->pa_tag;

	/*
	 * No power management hooks.
	 * XXX Maybe we should add some!
	 */
	sc->sc_flags |= ATWF_ENABLED;

	/*
	 * Get revision info, and set some chip-specific variables.
	 */
	sc->sc_rev = PCI_REVISION(pa->pa_class);

	/*
	 * Check to see if the device is in power-save mode, and
	 * being it out if necessary.
	 *
	 * XXX This code comes almost verbatim from if_tlp_pci.c. I do
	 * not understand it. Tulip clears the "sleep mode" bit in the
	 * CFDA register, first.  There is an equivalent (?) register at the
	 * same place in the ADM8211, but the docs do not assign its bits
	 * any meanings. -dcy
	 */
	pci_set_powerstate(pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/*
	 * Map the device.
	 */
	ioh_valid = (pci_mapreg_map(pa, ATW_PCI_IOBA,
	    PCI_MAPREG_TYPE_IO, 0,
	    &iot, &ioh, NULL, &iosize, 0) == 0);
	memh_valid = (pci_mapreg_map(pa, ATW_PCI_MMBA,
	    PCI_MAPREG_TYPE_MEM|PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &memt, &memh, NULL, &memsize, 0) == 0);

	if (memh_valid) {
		sc->sc_st = memt;
		sc->sc_sh = memh;
		sc->sc_mapsize = memsize;
	} else if (ioh_valid) {
		sc->sc_st = iot;
		sc->sc_sh = ioh;
		sc->sc_mapsize = iosize;
	} else {
		printf(": unable to map device registers\n");
		return;
	}

	sc->sc_dmat = pa->pa_dmat;

	/*
	 * Get the cacheline size.
	 */
	sc->sc_cacheline = PCI_CACHELINE(pci_conf_read(pc, pa->pa_tag,
	    PCI_BHLC_REG));

	/*
	 * Get PCI data moving command info.
	 */
	if (pa->pa_flags & PCI_FLAGS_MRL_OKAY) /* read line */
		sc->sc_flags |= ATWF_MRL;
	if (pa->pa_flags & PCI_FLAGS_MRM_OKAY) /* read multiple */
		sc->sc_flags |= ATWF_MRM;
	if (pa->pa_flags & PCI_FLAGS_MWI_OKAY) /* write invalidate */
		sc->sc_flags |= ATWF_MWI;

	/*
	 * Map and establish our interrupt.
	 */
	if (pci_intr_map(pa, &psc->psc_ih)) {
		printf(": unable to map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pc, psc->psc_ih); 
	psc->psc_intrcookie = pci_intr_establish(pc, psc->psc_ih, IPL_NET,
	    atw_intr, sc, sc->sc_dev.dv_xname);
	if (psc->psc_intrcookie == NULL) {
		printf(": unable to establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}

	printf(": %s\n", intrstr);

	sc->sc_enable = atw_pci_enable;
	sc->sc_disable = atw_pci_disable;

	/*
	 * Finish off the attach.
	 */
	atw_attach(sc);
}

int
atw_pci_detach(struct device *self, int flags)
{
	struct atw_pci_softc *psc = (void *)self;
	struct atw_softc *sc = &psc->psc_atw;
	int rv;

	rv = atw_detach(sc);
	if (rv)
		return (rv);

	if (psc->psc_intrcookie != NULL)
		pci_intr_disestablish(psc->psc_pc, psc->psc_intrcookie);

	bus_space_unmap(sc->sc_st, sc->sc_sh, sc->sc_mapsize);

	return (0);
}
