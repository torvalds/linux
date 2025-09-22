/*	$OpenBSD: if_re_pci.c,v 1.59 2024/08/31 16:23:09 deraadt Exp $	*/

/*
 * Copyright (c) 2005 Peter Valchev <pvalchev@openbsd.org>
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

/*
 * PCI front-end for the Realtek 8169
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/rtl81x9reg.h>
#include <dev/ic/revar.h>

struct re_pci_softc {
	/* General */
	struct rl_softc sc_rl;

	/* PCI-specific data */
	pci_chipset_tag_t sc_pc;
	pcitag_t sc_pcitag;

	bus_size_t sc_iosize;
};

const struct pci_matchid re_pci_devices[] = {
	{ PCI_VENDOR_COREGA, PCI_PRODUCT_COREGA_CGLAPCIGT },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DGE528T },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DGE530T_C1 },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_E2500V2 },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_E2600 },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8101E },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8168 },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8168_2 },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8169 },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8169SC },
	{ PCI_VENDOR_TTTECH, PCI_PRODUCT_TTTECH_MC322 },
	{ PCI_VENDOR_USR2, PCI_PRODUCT_USR2_USR997902 }
};

#define RE_LINKSYS_EG1032_SUBID 0x00241737

int	re_pci_probe(struct device *, void *, void *);
void	re_pci_attach(struct device *, struct device *, void *);
int	re_pci_detach(struct device *, int);
int	re_pci_activate(struct device *, int);

/*
 * PCI autoconfig definitions
 */
const struct cfattach re_pci_ca = {
	sizeof(struct re_pci_softc),
	re_pci_probe,
	re_pci_attach,
	re_pci_detach,
	re_pci_activate
};

/*
 * Probe for a Realtek 8169/8110 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
re_pci_probe(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcireg_t subid;

	subid = pci_conf_read(pc, pa->pa_tag, PCI_SUBSYS_ID_REG);

	/* C+ mode 8139's */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_REALTEK &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RT8139 &&
	    PCI_REVISION(pa->pa_class) == 0x20)
		return (1);

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_LINKSYS &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_LINKSYS_EG1032 &&
	    subid == RE_LINKSYS_EG1032_SUBID)
		return (1);

	return (pci_matchbyid((struct pci_attach_args *)aux, re_pci_devices,
	    nitems(re_pci_devices)));
}

/*
 * PCI-specific attach routine
 */
void
re_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct re_pci_softc	*psc = (struct re_pci_softc *)self;
	struct rl_softc		*sc = &psc->sc_rl;
	struct pci_attach_args	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
	const char		*intrstr = NULL;
	pcireg_t		reg;
	int			offset;

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

#ifndef SMALL_KERNEL
	/* Enable power management for wake on lan. */
	pci_conf_write(pc, pa->pa_tag, RL_PCI_PMCSR, RL_PME_EN);
#endif

	/*
	 * Map control/status registers.
	 */
	if (pci_mapreg_map(pa, RL_PCI_LOMEM64, PCI_MAPREG_TYPE_MEM |
	    PCI_MAPREG_MEM_TYPE_64BIT, 0, &sc->rl_btag, &sc->rl_bhandle,
	    NULL, &psc->sc_iosize, 0)) {
		if (pci_mapreg_map(pa, RL_PCI_LOMEM, PCI_MAPREG_TYPE_MEM |
		    PCI_MAPREG_MEM_TYPE_32BIT, 0, &sc->rl_btag, &sc->rl_bhandle,
		    NULL, &psc->sc_iosize, 0)) {
			if (pci_mapreg_map(pa, RL_PCI_LOIO, PCI_MAPREG_TYPE_IO,
			    0, &sc->rl_btag, &sc->rl_bhandle, NULL,
			    &psc->sc_iosize, 0)) {
				printf(": can't map mem or i/o space\n");
				return;
			}
		}
	}

	/* Allocate interrupt */
	if (pci_intr_map_msi(pa, &ih) == 0)
		sc->rl_flags |= RL_FLAG_MSI;
	else if (pci_intr_map(pa, &ih) != 0) {
		printf(": couldn't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET | IPL_MPSAFE, re_intr,
	    sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		return;
	}

	sc->sc_dmat = pa->pa_dmat;
	psc->sc_pc = pc;

	/*
	 * PCI Express check.
	 */
	if (pci_get_capability(pc, pa->pa_tag, PCI_CAP_PCIEXPRESS,
	    &offset, NULL)) {
		/* Disable PCIe ASPM and ECPM. */
		reg = pci_conf_read(pc, pa->pa_tag, offset + PCI_PCIE_LCSR);
		reg &= ~(PCI_PCIE_LCSR_ASPM_L0S | PCI_PCIE_LCSR_ASPM_L1 |
		    PCI_PCIE_LCSR_ECPM);
		pci_conf_write(pc, pa->pa_tag, offset + PCI_PCIE_LCSR, reg);
		sc->rl_flags |= RL_FLAG_PCIE;
	}

	if (!(PCI_VENDOR(pa->pa_id) == PCI_VENDOR_REALTEK &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RT8139)) {
		u_int8_t	cfg;

		CSR_WRITE_1(sc, RL_EECMD, RL_EE_MODE);
		cfg = CSR_READ_1(sc, RL_CFG2);
		if (sc->rl_flags & RL_FLAG_MSI) {
			cfg |= RL_CFG2_MSI;
			CSR_WRITE_1(sc, RL_CFG2, cfg);
		} else {
			if ((cfg & RL_CFG2_MSI) != 0) {
				cfg &= ~RL_CFG2_MSI;
				CSR_WRITE_1(sc, RL_CFG2, cfg);
			}
		}
		CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);
	}

	sc->sc_product = PCI_PRODUCT(pa->pa_id);

	/* Call bus-independent attach routine */
	if (re_attach(sc, intrstr)) {
		pci_intr_disestablish(pc, sc->sc_ih);
		bus_space_unmap(sc->rl_btag, sc->rl_bhandle, psc->sc_iosize);
	}
}

int
re_pci_detach(struct device *self, int flags)
{
	struct re_pci_softc	*psc = (struct re_pci_softc *)self;
	struct rl_softc		*sc = &psc->sc_rl;

	re_detach(sc);

	/* Disable interrupts */
	if (sc->sc_ih != NULL)
		pci_intr_disestablish(psc->sc_pc, sc->sc_ih);

	/* Free pci resources */
	bus_space_unmap(sc->rl_btag, sc->rl_bhandle, psc->sc_iosize);

	return (0);
}

int
re_pci_activate(struct device *self, int act)
{
	struct re_pci_softc	*psc = (struct re_pci_softc *)self;
	struct rl_softc		*sc = &psc->sc_rl;
	struct ifnet 		*ifp = &sc->sc_arpcom.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			re_stop(ifp);
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP)
			re_init(ifp);
		break;
	}
	return (0);
}
