/*	$OpenBSD: puc_cardbus.c,v 1.10 2024/05/24 06:26:47 jsg Exp $	*/

/*
 * Copyright (c) 2006 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/cardbus/cardbusvar.h>

#include <dev/pci/pucvar.h>

struct puc_cardbus_softc {
	struct puc_softc sc_psc;

	struct cardbus_devfunc *ct;
	int intrline;
};

int	puc_cardbus_match(struct device *, void *, void *);
void	puc_cardbus_attach(struct device *, struct device *, void *);
int	puc_cardbus_detach(struct device *, int);

const char *puc_cardbus_intr_string(struct puc_attach_args *);
void *puc_cardbus_intr_establish(struct puc_attach_args *, int,
    int (*)(void *), void *, char *);

const struct cfattach puc_cardbus_ca = {
	sizeof(struct puc_cardbus_softc), puc_cardbus_match,
	puc_cardbus_attach, puc_cardbus_detach
};

int
puc_cardbus_match(struct device *parent, void *match, void *aux)
{
	struct cardbus_attach_args *ca = aux;
	pci_chipset_tag_t pc = ca->ca_pc;
	pcireg_t bhlc, reg;

	bhlc = pci_conf_read(pc, ca->ca_tag, PCI_BHLC_REG);
	if (PCI_HDRTYPE_TYPE(bhlc) != 0)
		return(0);

	/* this one is some sort of a bridge and not a puc */
	if (PCI_VENDOR(ca->ca_id) == PCI_VENDOR_OXFORD2 &&
	    PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_OXFORD2_EXSYS_EX41098)
		return (0);

	reg = pci_conf_read(pc, ca->ca_tag, PCI_SUBSYS_ID_REG);
	if (puc_find_description(PCI_VENDOR(ca->ca_id),
	    PCI_PRODUCT(ca->ca_id), PCI_VENDOR(reg), PCI_PRODUCT(reg)))
		return (10);

	return (0);
}

void
puc_cardbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct puc_cardbus_softc *csc = (struct puc_cardbus_softc *)self;
	struct puc_softc *sc = &csc->sc_psc;
	struct cardbus_attach_args *ca = aux;
	struct cardbus_devfunc *ct = ca->ca_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	pci_chipset_tag_t pc = ca->ca_pc;
	cardbus_function_tag_t cf = ct->ct_cf;
	struct puc_attach_args paa;
	pcireg_t reg;
	int i;

	Cardbus_function_enable(ct);

	csc->ct = ct;

	reg = pci_conf_read(pc, ca->ca_tag, PCI_SUBSYS_ID_REG);
	sc->sc_desc = puc_find_description(PCI_VENDOR(ca->ca_id),
	    PCI_PRODUCT(ca->ca_id), PCI_VENDOR(reg), PCI_PRODUCT(reg));

	puc_print_ports(sc->sc_desc);

	/* the fifth one is some memory we dunno */
	for (i = 0; i < PUC_NBARS; i++) {
		pcireg_t type;
		int bar;

		sc->sc_bar_mappings[i].mapped = 0;
		bar = PCI_MAPREG_START + 4 * i;
		if (!pci_mapreg_probe(pc, ca->ca_tag, bar, &type))
			continue;

		if (!(sc->sc_bar_mappings[i].mapped = !Cardbus_mapreg_map(ct,
		    bar, type, 0,
		    &sc->sc_bar_mappings[i].t, &sc->sc_bar_mappings[i].h,
		    &sc->sc_bar_mappings[i].a, &sc->sc_bar_mappings[i].s)))
			printf("%s: couldn't map BAR at offset 0x%lx\n",
			    sc->sc_dev.dv_xname, (long)bar);
		sc->sc_bar_mappings[i].type = type;
	}

	csc->intrline = ca->ca_intrline;

	if (pci_get_capability(pc, ca->ca_tag, PCI_CAP_PWRMGMT, &reg,
	    0)) {
		reg = pci_conf_read(pc, ca->ca_tag, reg + 4) & 3;
		if (reg) {
			printf("%s: awakening from state D%d\n",
			    sc->sc_dev.dv_xname, reg);
			pci_conf_write(pc, ca->ca_tag, reg + 4, 0);
		}
	}

	(*cf->cardbus_ctrl)(cc, CARDBUS_MEM_ENABLE);
	(*cf->cardbus_ctrl)(cc, CARDBUS_IO_ENABLE);
	(*cf->cardbus_ctrl)(cc, CARDBUS_BM_ENABLE);

	paa.puc = sc;
	paa.intr_string = &puc_cardbus_intr_string;
	paa.intr_establish = &puc_cardbus_intr_establish;

	puc_common_attach(sc, &paa);
}

const char *
puc_cardbus_intr_string(struct puc_attach_args *paa)
{
	struct puc_cardbus_softc *sc = paa->puc;
	static char str[16];

	snprintf(str, sizeof str, "irq %d", sc->intrline);
	return (str);
}

void *
puc_cardbus_intr_establish(struct puc_attach_args *paa, int type,
    int (*func)(void *), void *arg, char *name)
{
	struct puc_cardbus_softc *sc = paa->puc;
	struct puc_softc *psc = &sc->sc_psc;
	struct cardbus_devfunc *ct = sc->ct;

	psc->sc_ports[paa->port].intrhand =
	    cardbus_intr_establish(ct->ct_cc, ct->ct_cf, sc->intrline,
		type, func, arg, name);

	return (psc->sc_ports[paa->port].intrhand);
}

int
puc_cardbus_detach(struct device *self, int flags)
{
	struct puc_cardbus_softc *sc = (struct puc_cardbus_softc *)self;
	struct puc_softc *psc = &sc->sc_psc;
	struct cardbus_devfunc *ct = sc->ct;
	int i, rv;

	for (i = PUC_MAX_PORTS; i--; ) {
		if (psc->sc_ports[i].intrhand)
			cardbus_intr_disestablish(ct->ct_cc, ct->ct_cf,
			    psc->sc_ports[i].intrhand);
		if (psc->sc_ports[i].dev)
			if ((rv = config_detach(psc->sc_ports[i].dev, flags)))
				return (rv);
	}

	for (i = PUC_NBARS; i--; )
		if (psc->sc_bar_mappings[i].mapped)
			Cardbus_mapreg_unmap(ct, psc->sc_bar_mappings[i].type,
			    psc->sc_bar_mappings[i].t,
			    psc->sc_bar_mappings[i].h,
			    psc->sc_bar_mappings[i].s);

	return (0);
}
