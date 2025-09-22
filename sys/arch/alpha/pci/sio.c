/*	$OpenBSD: sio.c,v 1.43 2025/06/29 15:55:21 miod Exp $	*/
/*	$NetBSD: sio.c,v 1.15 1996/12/05 01:39:36 cgd Exp $	*/

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/isa/isavar.h>
#include <dev/eisa/eisavar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <alpha/pci/siovar.h>

#include "eisa.h"
#include "isadma.h"

struct sio_softc {
	struct device			sc_dv;

	bus_space_tag_t			sc_iot, sc_memt;
	bus_dma_tag_t			sc_dmat;
	int				sc_haseisa;

	struct alpha_eisa_chipset	sc_ec;
	struct alpha_isa_chipset	sc_ic;
};

int	siomatch(struct device *, void *, void *);
void	sioattach(struct device *, struct device *, void *);
int	sioactivate(struct device *, int);

extern int sio_intr_alloc(isa_chipset_tag_t, int, int, int *);
extern int sio_intr_check(isa_chipset_tag_t, int, int);

const struct cfattach sio_ca = {
	.ca_devsize = sizeof(struct sio_softc),
	.ca_match = siomatch,
	.ca_attach = sioattach,
	.ca_activate = sioactivate
};

struct cfdriver sio_cd = {
	NULL, "sio", DV_DULL,
};

int	pcebmatch(struct device *, void *, void *);

const struct cfattach pceb_ca = {
	sizeof(struct sio_softc), pcebmatch, sioattach,
};

struct cfdriver pceb_cd = {
	NULL, "pceb", DV_DULL,
};

union sio_attach_args {
	const char *sa_name;			/* XXX should be common */
	struct isabus_attach_args sa_iba;
	struct eisabus_attach_args sa_eba;
};

int	sioprint(void *, const char *pnp);
void	sio_isa_attach_hook(struct device *, struct device *,
	    struct isabus_attach_args *);
void	sio_eisa_attach_hook(struct device *, struct device *,
	    struct eisabus_attach_args *);
int	sio_eisa_intr_map(void *, u_int, eisa_intr_handle_t *);
void	sio_bridge_callback(struct device *);

int
siomatch(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_CONTAQ &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_CONTAQ_82C693 &&
	    pa->pa_function == 0)
		return (1);

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INTEL &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_SIO)
		return (1);

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_ALI &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ALI_M1533)
		return(1);

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_ALI &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ALI_M1543)
		return(1);
	return (0);
}

int
pcebmatch(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INTEL &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_PCEB)
		return (1);

	return (0);
}

void
sioattach(struct device *parent, struct device *self, void *aux)
{
	struct sio_softc *sc = (struct sio_softc *)self;
	struct pci_attach_args *pa = aux;

	printf("\n");

	sc->sc_iot = pa->pa_iot;
	sc->sc_memt = pa->pa_memt;
	sc->sc_dmat = pa->pa_dmat;
	sc->sc_haseisa = (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INTEL &&
		PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_PCEB);

	config_defer(self, sio_bridge_callback);
}

int
sioactivate(struct device *self, int act)
{
	int rv = 0;

	switch (act) {
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
		sio_intr_shutdown();
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

void
sio_bridge_callback(struct device *self)
{
	struct sio_softc *sc = (struct sio_softc *)self;
	union sio_attach_args sa;

	if (sc->sc_haseisa) {
		sc->sc_ec.ec_v = NULL;
		sc->sc_ec.ec_maxslots = 0; /* will be filled by attach_hook */
		sc->sc_ec.ec_attach_hook = sio_eisa_attach_hook;
		sc->sc_ec.ec_intr_map = sio_eisa_intr_map;
		sc->sc_ec.ec_intr_string = sio_intr_string;
		sc->sc_ec.ec_intr_establish = sio_intr_establish;
		sc->sc_ec.ec_intr_disestablish = sio_intr_disestablish;

		sa.sa_eba.eba_busname = "eisa";
		sa.sa_eba.eba_iot = sc->sc_iot;
		sa.sa_eba.eba_memt = sc->sc_memt;
		sa.sa_eba.eba_dmat =
		    alphabus_dma_get_tag(sc->sc_dmat, ALPHA_BUS_EISA);
		sa.sa_eba.eba_ec = &sc->sc_ec;
		config_found(&sc->sc_dv, &sa.sa_eba, sioprint);
	}

	sc->sc_ic.ic_v = NULL;
	sc->sc_ic.ic_attach_hook = sio_isa_attach_hook;
	sc->sc_ic.ic_intr_establish = sio_intr_establish;
	sc->sc_ic.ic_intr_disestablish = sio_intr_disestablish;
	sc->sc_ic.ic_intr_alloc = sio_intr_alloc;
	sc->sc_ic.ic_intr_check = sio_intr_check;

	sa.sa_iba.iba_busname = "isa";
	sa.sa_iba.iba_iot = sc->sc_iot;
	sa.sa_iba.iba_memt = sc->sc_memt;
#if NISADMA > 0
	sa.sa_iba.iba_dmat =
		alphabus_dma_get_tag(sc->sc_dmat, ALPHA_BUS_ISA);
#endif
	sa.sa_iba.iba_ic = &sc->sc_ic;
	config_found(&sc->sc_dv, &sa.sa_iba, sioprint);
}

int
sioprint(void *aux, const char *pnp)
{
	register union sio_attach_args *sa = aux;

	if (pnp)
		printf("%s at %s", sa->sa_name, pnp);
	return (UNCONF);
}

void
sio_isa_attach_hook(struct device *parent, struct device *self,
    struct isabus_attach_args *iba)
{
	/* Nothing to do. */
}

void
sio_eisa_attach_hook(struct device *parent, struct device *self,
    struct eisabus_attach_args *eba)
{
#if NEISA > 0
	eisa_init(eba->eba_ec);
#endif
}

int
sio_eisa_intr_map(void *v, u_int irq, eisa_intr_handle_t *ihp)
{

#define	ICU_LEN		16	/* number of ISA IRQs (XXX) */

	if (irq >= ICU_LEN) {
		printf("sio_eisa_intr_map: bad IRQ %d\n", irq);
		*ihp = -1;
		return 1;
	}
	if (irq == 2) {
		printf("sio_eisa_intr_map: changed IRQ 2 to IRQ 9\n");
		irq = 9;
	}

	*ihp = irq;
	return 0;
}
