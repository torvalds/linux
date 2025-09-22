/*	$OpenBSD: if_epic_pci.c,v 1.18 2024/05/24 06:02:53 jsg Exp $	*/
/*	$NetBSD: if_epic_pci.c,v 1.28 2005/02/27 00:27:32 perry Exp $	*/

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * PCI bus front-end for the Standard Microsystems Corp. 83C170
 * Ethernet PCI Integrated Controller (EPIC/100) driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/miivar.h>

#include <dev/ic/smc83c170reg.h>
#include <dev/ic/smc83c170var.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

/*
 * PCI configuration space registers used by the EPIC.
 */
#define	EPIC_PCI_IOBA		0x10	/* i/o mapped base */
#define	EPIC_PCI_MMBA		0x14	/* memory mapped base */

struct epic_pci_softc {
	struct epic_softc sc_epic;	/* real EPIC softc */

	/* PCI-specific goo. */
	void	*sc_ih;			/* interrupt handle */
};

int	epic_pci_match(struct device *, void *, void *);
void	epic_pci_attach(struct device *, struct device *, void *);

const struct cfattach epic_pci_ca = {
	sizeof(struct epic_pci_softc), epic_pci_match, epic_pci_attach
};

const struct pci_matchid epic_pci_devices[] = {
	{ PCI_VENDOR_SMC, PCI_PRODUCT_SMC_83C170 },
	{ PCI_VENDOR_SMC, PCI_PRODUCT_SMC_83C175 },
};

static const struct epic_pci_subsys_info {
	pcireg_t subsysid;
	int flags;
} epic_pci_subsys_info[] = {
	{ PCI_ID_CODE(PCI_VENDOR_SMC, 0xa015), /* SMC9432BTX */
	  EPIC_HAS_BNC },
	{ PCI_ID_CODE(PCI_VENDOR_SMC, 0xa024), /* SMC9432BTX1 */
	  EPIC_HAS_BNC },
	{ PCI_ID_CODE(PCI_VENDOR_SMC, 0xa016), /* SMC9432FTX */
	  EPIC_HAS_MII_FIBER | EPIC_DUPLEXLED_ON_694 },
	{ 0xffffffff,
	  0 }
};

static const struct epic_pci_subsys_info *
epic_pci_subsys_lookup(const struct pci_attach_args *pa)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcireg_t reg;
	const struct epic_pci_subsys_info *esp;

	reg = pci_conf_read(pc, pa->pa_tag, PCI_SUBSYS_ID_REG);

	for (esp = epic_pci_subsys_info; esp->subsysid != 0xffffffff; esp++)
		if (esp->subsysid == reg)
			return (esp);

	return (NULL);
}

int
epic_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, epic_pci_devices,
	    nitems(epic_pci_devices)));
}

void
epic_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct epic_pci_softc *psc = (struct epic_pci_softc *)self;
	struct epic_softc *sc = &psc->sc_epic;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	const struct epic_pci_subsys_info *esp;
	bus_space_tag_t iot, memt;
	bus_space_handle_t ioh, memh;
	int ioh_valid, memh_valid;

	pci_set_powerstate(pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/*
	 * Map the device.
	 */
	ioh_valid = (pci_mapreg_map(pa, EPIC_PCI_IOBA,
	    PCI_MAPREG_TYPE_IO, 0,
	    &iot, &ioh, NULL, NULL, 0) == 0);
	memh_valid = (pci_mapreg_map(pa, EPIC_PCI_MMBA,
	    PCI_MAPREG_TYPE_MEM|PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &memt, &memh, NULL, NULL, 0) == 0);

	if (memh_valid) {
		sc->sc_st = memt;
		sc->sc_sh = memh;
	} else if (ioh_valid) {
		sc->sc_st = iot;
		sc->sc_sh = ioh;
	} else {
		printf(": unable to map device registers\n");
		return;
	}

	sc->sc_dmat = pa->pa_dmat;

	/*
	 * Map and establish our interrupt.
	 */
	if (pci_intr_map(pa, &ih)) {
		printf(": unable to map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	psc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, epic_intr, sc,
	    self->dv_xname);
	if (psc->sc_ih == NULL) {
		printf(": unable to establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}

	esp = epic_pci_subsys_lookup(pa);
	if (esp)
		sc->sc_hwflags = esp->flags;

	/*
	 * Finish off the attach.
	 */
	epic_attach(sc, intrstr);
}
