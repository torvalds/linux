/*	$OpenBSD: puc.c,v 1.32 2024/11/09 10:23:06 miod Exp $	*/
/*	$NetBSD: puc.c,v 1.3 1999/02/06 06:29:54 cgd Exp $	*/

/*
 * Copyright (c) 1996, 1998, 1999
 *	Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * PCI "universal" communication card device driver, glues com, lpt,
 * and similar ports to PCI via bridge chip often much larger than
 * the devices being glued.
 *
 * Author: Christopher G. Demetriou, May 14, 1998 (derived from NetBSD
 * sys/dev/pci/pciide.c, revision 1.6).
 *
 * These devices could be (and some times are) described as
 * communications/{serial,parallel}, etc. devices with known
 * programming interfaces, but those programming interfaces (in
 * particular the BAR assignments for devices, etc.) in fact are not
 * particularly well defined.
 *
 * After I/we have seen more of these devices, it may be possible
 * to generalize some of these bits.  In particular, devices which
 * describe themselves as communications/serial/16[45]50, and
 * communications/parallel/??? might be attached via direct
 * 'com' and 'lpt' attachments to pci.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/tty.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pucvar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

#include "com.h"

struct puc_pci_softc {
	struct puc_softc	sc_psc;

	pci_chipset_tag_t	pc;
	pci_intr_handle_t	ih;
};

int	puc_pci_match(struct device *, void *, void *);
void	puc_pci_attach(struct device *, struct device *, void *);
int	puc_pci_detach(struct device *, int);
const char *puc_pci_intr_string(struct puc_attach_args *);
void	*puc_pci_intr_establish(struct puc_attach_args *, int,
    int (*)(void *), void *, char *);
int	puc_pci_xr17v35x_intr(void *arg);

const struct cfattach puc_pci_ca = {
	sizeof(struct puc_pci_softc), puc_pci_match,
	puc_pci_attach, puc_pci_detach
};

struct cfdriver puc_cd = {
	NULL, "puc", DV_DULL
};

const char *puc_port_type_name(int);

int
puc_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;
	const struct puc_device_description *desc;
	pcireg_t bhlc, subsys;

	bhlc = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG);
	if (PCI_HDRTYPE_TYPE(bhlc) != 0)
		return (0);

	subsys = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);

	desc = puc_find_description(PCI_VENDOR(pa->pa_id),
	    PCI_PRODUCT(pa->pa_id), PCI_VENDOR(subsys), PCI_PRODUCT(subsys));
	if (desc != NULL)
		return (1);

	return (0);
}

const char *
puc_pci_intr_string(struct puc_attach_args *paa)
{
	struct puc_pci_softc *sc = paa->puc;

	return (pci_intr_string(sc->pc, sc->ih));
}

void *
puc_pci_intr_establish(struct puc_attach_args *paa, int type,
    int (*func)(void *), void *arg, char *name)
{
	struct puc_pci_softc *sc = paa->puc;
	struct puc_softc *psc = &sc->sc_psc;

	if (psc->sc_xr17v35x) {
		psc->sc_ports[paa->port].real_intrhand = func;
		psc->sc_ports[paa->port].real_intrhand_arg = arg;
		if (paa->port == 0)
			psc->sc_ports[paa->port].intrhand =
			    pci_intr_establish(sc->pc, sc->ih, type,
			    puc_pci_xr17v35x_intr, sc, name);
		return (psc->sc_ports[paa->port].real_intrhand);
	}

	psc->sc_ports[paa->port].intrhand =
	    pci_intr_establish(sc->pc, sc->ih, type, func, arg, name);

	return (psc->sc_ports[paa->port].intrhand);
}

void
puc_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct puc_pci_softc *psc = (struct puc_pci_softc *)self;
	struct puc_softc *sc = &psc->sc_psc;
	struct pci_attach_args *pa = aux;
	struct puc_attach_args paa;
	pcireg_t subsys;
	int i;

	subsys = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);
	sc->sc_desc = puc_find_description(PCI_VENDOR(pa->pa_id),
	    PCI_PRODUCT(pa->pa_id), PCI_VENDOR(subsys), PCI_PRODUCT(subsys));

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_EXAR &&
	    (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_EXAR_XR17V352 ||
	     PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_EXAR_XR17V354))
		sc->sc_xr17v35x = 1;

	puc_print_ports(sc->sc_desc);

	for (i = 0; i < PUC_NBARS; i++) {
		pcireg_t type;
		int bar;

		sc->sc_bar_mappings[i].mapped = 0;
		bar = PCI_MAPREG_START + 4 * i;
		if (!pci_mapreg_probe(pa->pa_pc, pa->pa_tag, bar, &type))
			continue;

		sc->sc_bar_mappings[i].mapped = (pci_mapreg_map(pa, bar, type,
		    0, &sc->sc_bar_mappings[i].t, &sc->sc_bar_mappings[i].h,
		    &sc->sc_bar_mappings[i].a, &sc->sc_bar_mappings[i].s, 0)
		      == 0);
		if (sc->sc_bar_mappings[i].mapped) {
			if (type == PCI_MAPREG_MEM_TYPE_64BIT)
				i++;
			continue;
		}

#if NCOM > 0
		/*
		 * If a port on this card is used as serial console,
		 * mapping the associated BAR will fail because the
		 * bus space is already mapped.  In that case, we try
		 * to re-use the already existing mapping.
		 * Unfortunately this means that if a BAR is used to
		 * support multiple ports, only the first port will
		 * work.
		 */
		if (pci_mapreg_info(pa->pa_pc, pa->pa_tag, bar, type,
		    &sc->sc_bar_mappings[i].a, NULL, NULL) == 0 &&
		    pa->pa_iot == comconsiot &&
		    sc->sc_bar_mappings[i].a == comconsaddr) {
			sc->sc_bar_mappings[i].t = comconsiot;
			sc->sc_bar_mappings[i].h = comconsioh;
			sc->sc_bar_mappings[i].s = COM_NPORTS;
			sc->sc_bar_mappings[i].mapped = 1;
			if (type == PCI_MAPREG_MEM_TYPE_64BIT)
				i++;
			continue;
		}
#endif

		printf("%s: couldn't map BAR at offset 0x%lx\n",
		    sc->sc_dev.dv_xname, (long)bar);
	}

	/* Map interrupt. */
	psc->pc = pa->pa_pc;
	if (pci_intr_map(pa, &psc->ih)) {
		printf("%s: couldn't map interrupt\n", sc->sc_dev.dv_xname);
		return;
	}

	paa.puc = sc;
	paa.intr_string = &puc_pci_intr_string;
	paa.intr_establish = &puc_pci_intr_establish;

	puc_common_attach(sc, &paa);
}

void
puc_common_attach(struct puc_softc *sc, struct puc_attach_args *paa)
{
	const struct puc_device_description *desc = sc->sc_desc;
	int i, bar;

	/* Configure each port. */
	for (i = 0; i < PUC_MAX_PORTS; i++) {
		if (desc->ports[i].type == 0)	/* neither com or lpt */
			continue;
		/* make sure the base address register is mapped */
		bar = PUC_PORT_BAR_INDEX(desc->ports[i].bar);
		if (!sc->sc_bar_mappings[bar].mapped) {
			printf("%s: %s port uses unmapped BAR (0x%x)\n",
			    sc->sc_dev.dv_xname,
			    puc_port_type_name(desc->ports[i].type),
			    desc->ports[i].bar);
			continue;
		}

		/* set up to configure the child device */
		paa->port = i;
		paa->a = sc->sc_bar_mappings[bar].a;
		paa->t = sc->sc_bar_mappings[bar].t;

		paa->type = desc->ports[i].type;

		if (desc->ports[i].offset >= sc->sc_bar_mappings[bar].s ||
		    bus_space_subregion(sc->sc_bar_mappings[bar].t,
		    sc->sc_bar_mappings[bar].h, desc->ports[i].offset,
		    sc->sc_bar_mappings[bar].s - desc->ports[i].offset,
		    &paa->h)) {
			printf("%s: couldn't get subregion for port %d\n",
			    sc->sc_dev.dv_xname, i);
			continue;
		}

#if 0
		if (autoconf_verbose)
			printf("%s: port %d: %s @ (index %d) 0x%x "
			    "(0x%lx, 0x%lx)\n", sc->sc_dev.dv_xname, paa->port,
			    puc_port_type_name(paa->type), bar, (int)paa->a,
			    (long)paa->t, (long)paa->h);
#endif

		/* and configure it */
		sc->sc_ports[i].dev = config_found_sm(&sc->sc_dev, paa,
		    puc_print, puc_submatch);
	}
}

int
puc_pci_detach(struct device *self, int flags)
{
	struct puc_pci_softc *sc = (struct puc_pci_softc *)self;
	struct puc_softc *psc = &sc->sc_psc;
	int i, rv;

	for (i = PUC_MAX_PORTS; i--; ) {
		if (psc->sc_ports[i].intrhand)
			pci_intr_disestablish(sc->pc,
			    psc->sc_ports[i].intrhand);
		if (psc->sc_ports[i].dev)
			if ((rv = config_detach(psc->sc_ports[i].dev, flags)))
				return (rv);
	}

	for (i = PUC_NBARS; i--; )
		if (psc->sc_bar_mappings[i].mapped)
			bus_space_unmap(psc->sc_bar_mappings[i].t,
			    psc->sc_bar_mappings[i].h,
			    psc->sc_bar_mappings[i].s);

	return (0);
}

int
puc_print(void *aux, const char *pnp)
{
	struct puc_attach_args *paa = aux;

	if (pnp)
		printf("%s at %s", puc_port_type_name(paa->type), pnp);
	printf(" port %d", paa->port);
	return (UNCONF);
}

int
puc_submatch(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = (struct cfdata *)vcf;
	struct puc_attach_args *aa = aux;

	if (cf->cf_loc[0] != -1 && cf->cf_loc[0] != aa->port)
		return 0;
	return ((*cf->cf_attach->ca_match)(parent, cf, aux));
}

const struct puc_device_description *
puc_find_description(u_int16_t vend, u_int16_t prod,
    u_int16_t svend, u_int16_t sprod)
{
	int i;

	for (i = 0; i < puc_ndevs; i++)
		if ((vend & puc_devs[i].rmask[0]) == puc_devs[i].rval[0] &&
		    (prod & puc_devs[i].rmask[1]) == puc_devs[i].rval[1] &&
		    (svend & puc_devs[i].rmask[2]) == puc_devs[i].rval[2] &&
		    (sprod & puc_devs[i].rmask[3]) == puc_devs[i].rval[3])
			return (&puc_devs[i]);

	return (NULL);
}

const char *
puc_port_type_name(int type)
{
	if (PUC_IS_COM(type))
		return "com";
	if (PUC_IS_LPT(type))
		return "lpt";
	return (NULL);
}

void
puc_print_ports(const struct puc_device_description *desc)
{
	int i, ncom, nlpt;

	printf(": ports: ");
	for (i = ncom = nlpt = 0; i < PUC_MAX_PORTS; i++) {
		if (PUC_IS_COM(desc->ports[i].type))
			ncom++;
		else if (PUC_IS_LPT(desc->ports[i].type))
			nlpt++;
	}
	if (ncom)
		printf("%d com", ncom);
	if (nlpt) {
		if (ncom)
			printf(", ");
		printf("%d lpt", nlpt);
	}
	printf("\n");
}

int
puc_pci_xr17v35x_intr(void *arg)
{
	struct puc_pci_softc *sc = arg;
	struct puc_softc *psc = &sc->sc_psc;
	int ports, i;

	ports = bus_space_read_1(psc->sc_bar_mappings[0].t,
	    psc->sc_bar_mappings[0].h, UART_EXAR_INT0);

	for (i = 0; i < 8; i++) {
		if ((ports & (1 << i)) && psc->sc_ports[i].real_intrhand)
			(*(psc->sc_ports[i].real_intrhand))(
			    psc->sc_ports[i].real_intrhand_arg);
	}

	return (1);
}
