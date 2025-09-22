/*	$OpenBSD: if_athn_pci.c,v 1.24 2024/05/24 06:02:53 jsg Exp $	*/

/*-
 * Copyright (c) 2009 Damien Bergamini <damien.bergamini@free.fr>
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
 * PCI front-end for Atheros 802.11a/g/n chipsets.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_ra.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/ic/athnreg.h>
#include <dev/ic/athnvar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define PCI_SUBSYSID_ATHEROS_COEX2WIRE		0x309b
#define PCI_SUBSYSID_ATHEROS_COEX3WIRE_SA	0x30aa
#define PCI_SUBSYSID_ATHEROS_COEX3WIRE_DA	0x30ab

struct athn_pci_softc {
	struct athn_softc	sc_sc;

	/* PCI specific goo. */
	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;
	void			*sc_ih;
	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
	bus_size_t		sc_mapsize;
	int			sc_cap_off;
};

int		athn_pci_match(struct device *, void *, void *);
void		athn_pci_attach(struct device *, struct device *, void *);
int		athn_pci_detach(struct device *, int);
int		athn_pci_activate(struct device *, int);
void		athn_pci_wakeup(struct athn_pci_softc *);
uint32_t	athn_pci_read(struct athn_softc *, uint32_t);
void		athn_pci_write(struct athn_softc *, uint32_t, uint32_t);
void		athn_pci_write_barrier(struct athn_softc *);
void		athn_pci_disable_aspm(struct athn_softc *);

const struct cfattach athn_pci_ca = {
	sizeof (struct athn_pci_softc),
	athn_pci_match,
	athn_pci_attach,
	athn_pci_detach,
	athn_pci_activate
};

static const struct pci_matchid athn_pci_devices[] = {
	{ PCI_VENDOR_ATHEROS, PCI_PRODUCT_ATHEROS_AR5416 },
	{ PCI_VENDOR_ATHEROS, PCI_PRODUCT_ATHEROS_AR5418 },
	{ PCI_VENDOR_ATHEROS, PCI_PRODUCT_ATHEROS_AR9160 },
	{ PCI_VENDOR_ATHEROS, PCI_PRODUCT_ATHEROS_AR9280 },
	{ PCI_VENDOR_ATHEROS, PCI_PRODUCT_ATHEROS_AR928X },
	{ PCI_VENDOR_ATHEROS, PCI_PRODUCT_ATHEROS_AR9285 },
	{ PCI_VENDOR_ATHEROS, PCI_PRODUCT_ATHEROS_AR2427 },
	{ PCI_VENDOR_ATHEROS, PCI_PRODUCT_ATHEROS_AR9227 },
	{ PCI_VENDOR_ATHEROS, PCI_PRODUCT_ATHEROS_AR9287 }
};

int
athn_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, athn_pci_devices,
	    nitems(athn_pci_devices)));
}

void
athn_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct athn_pci_softc *psc = (struct athn_pci_softc *)self;
	struct athn_softc *sc = &psc->sc_sc;
	struct pci_attach_args *pa = aux;
	const char *intrstr;
	pci_intr_handle_t ih;
	pcireg_t memtype, reg;
	pci_product_id_t subsysid;
	int error;

	sc->sc_dmat = pa->pa_dmat;
	psc->sc_pc = pa->pa_pc;
	psc->sc_tag = pa->pa_tag;

	sc->ops.read = athn_pci_read;
	sc->ops.write = athn_pci_write;
	sc->ops.write_barrier = athn_pci_write_barrier;

	/*
	 * Get the offset of the PCI Express Capability Structure in PCI
	 * Configuration Space (Linux hardcodes it as 0x60.)
	 */
	error = pci_get_capability(pa->pa_pc, pa->pa_tag, PCI_CAP_PCIEXPRESS,
	    &psc->sc_cap_off, NULL);
	if (error != 0) {	/* Found. */
		sc->sc_disable_aspm = athn_pci_disable_aspm;
		sc->flags |= ATHN_FLAG_PCIE;
	}
	/* 
	 * Clear device-specific "PCI retry timeout" register (41h) to prevent
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, 0x40);
	if (reg & 0xff00)
		pci_conf_write(pa->pa_pc, pa->pa_tag, 0x40, reg & ~0xff00);

	/* 
	 * Set the cache line size to a reasonable value if it is 0.
	 * Change latency timer; default value yields poor results.
	 */
	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG);
	if (PCI_CACHELINE(reg) == 0) {
		reg &= ~(PCI_CACHELINE_MASK << PCI_CACHELINE_SHIFT);
		reg |= 8 << PCI_CACHELINE_SHIFT;
	}
	reg &= ~(PCI_LATTIMER_MASK << PCI_LATTIMER_SHIFT);
	reg |= 168 << PCI_LATTIMER_SHIFT;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG, reg);

	/* Determine if bluetooth is also supported (combo chip.) */
	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);
	subsysid = PCI_PRODUCT(reg);
	if (subsysid == PCI_SUBSYSID_ATHEROS_COEX3WIRE_SA ||
	    subsysid == PCI_SUBSYSID_ATHEROS_COEX3WIRE_DA)
		sc->flags |= ATHN_FLAG_BTCOEX3WIRE;
	else if (subsysid == PCI_SUBSYSID_ATHEROS_COEX2WIRE)
		sc->flags |= ATHN_FLAG_BTCOEX2WIRE;

	/* Map control/status registers. */
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, PCI_MAPREG_START);
	error = pci_mapreg_map(pa, PCI_MAPREG_START, memtype, 0, &psc->sc_st,
	    &psc->sc_sh, NULL, &psc->sc_mapsize, 0);
	if (error != 0) {
		printf(": can't map mem space\n");
		return;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(psc->sc_pc, ih);
	psc->sc_ih = pci_intr_establish(psc->sc_pc, ih, IPL_NET,
	    athn_intr, sc, sc->sc_dev.dv_xname);
	if (psc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);

	athn_attach(sc);
}

int
athn_pci_detach(struct device *self, int flags)
{
	struct athn_pci_softc *psc = (struct athn_pci_softc *)self;
	struct athn_softc *sc = &psc->sc_sc;

	if (psc->sc_ih != NULL) {
		athn_detach(sc);
		pci_intr_disestablish(psc->sc_pc, psc->sc_ih);
	}
	if (psc->sc_mapsize > 0)
		bus_space_unmap(psc->sc_st, psc->sc_sh, psc->sc_mapsize);

	return (0);
}

int
athn_pci_activate(struct device *self, int act)
{
	struct athn_pci_softc *psc = (struct athn_pci_softc *)self;
	struct athn_softc *sc = &psc->sc_sc;

	switch (act) {
	case DVACT_SUSPEND:
		athn_suspend(sc);
		break;
	case DVACT_WAKEUP:
		athn_pci_wakeup(psc);
		break;
	}

	return (0);
}

void
athn_pci_wakeup(struct athn_pci_softc *psc)
{
	struct athn_softc *sc = &psc->sc_sc;
	pcireg_t reg;
	int s;

	reg = pci_conf_read(psc->sc_pc, psc->sc_tag, 0x40);
	if (reg & 0xff00)
		pci_conf_write(psc->sc_pc, psc->sc_tag, 0x40, reg & ~0xff00);

	s = splnet();
	athn_wakeup(sc);
	splx(s);
}

uint32_t
athn_pci_read(struct athn_softc *sc, uint32_t addr)
{
	struct athn_pci_softc *psc = (struct athn_pci_softc *)sc;

	return (bus_space_read_4(psc->sc_st, psc->sc_sh, addr));
}

void
athn_pci_write(struct athn_softc *sc, uint32_t addr, uint32_t val)
{
	struct athn_pci_softc *psc = (struct athn_pci_softc *)sc;

	bus_space_write_4(psc->sc_st, psc->sc_sh, addr, val);
}

void
athn_pci_write_barrier(struct athn_softc *sc)
{
	struct athn_pci_softc *psc = (struct athn_pci_softc *)sc;

	bus_space_barrier(psc->sc_st, psc->sc_sh, 0, psc->sc_mapsize,
	    BUS_SPACE_BARRIER_WRITE);
}

void
athn_pci_disable_aspm(struct athn_softc *sc)
{
	struct athn_pci_softc *psc = (struct athn_pci_softc *)sc;
	pcireg_t reg;

	/* Disable PCIe Active State Power Management (ASPM). */
	reg = pci_conf_read(psc->sc_pc, psc->sc_tag,
	    psc->sc_cap_off + PCI_PCIE_LCSR);
	reg &= ~(PCI_PCIE_LCSR_ASPM_L0S | PCI_PCIE_LCSR_ASPM_L1);
	pci_conf_write(psc->sc_pc, psc->sc_tag,
	    psc->sc_cap_off + PCI_PCIE_LCSR, reg);
}
