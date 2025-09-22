/*	$OpenBSD: pcib.c,v 1.4 2012/08/18 16:00:20 miod Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/autoconf.h>
#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/isa/isavar.h>

#include <dev/pci/glxreg.h>
#include <dev/pci/glxvar.h>

#include "isa.h"

int	pcibmatch(struct device *, void *, void *);
void	pcibattach(struct device *, struct device *, void *);

struct pcib_softc {
	struct device	sc_dev;
	bus_space_tag_t	sc_iot;
	bus_space_tag_t	sc_memt;
	bus_dma_tag_t	sc_dmat;
};

const struct cfattach pcib_ca = {
	sizeof(struct pcib_softc), pcibmatch, pcibattach
};

void	pcib_callback(struct device *);
void	pcib_isa_attach(struct pcib_softc *, bus_space_tag_t, bus_space_tag_t,
	    bus_dma_tag_t);
int	pcib_print(void *, const char *);

struct cfdriver pcib_cd = {
	NULL, "pcib", DV_DULL
};

int
pcibmatch(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	switch (PCI_VENDOR(pa->pa_id)) {
	case PCI_VENDOR_INTEL:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_INTEL_SIO:
		case PCI_PRODUCT_INTEL_82371MX:
		case PCI_PRODUCT_INTEL_82371AB_ISA:
		case PCI_PRODUCT_INTEL_82440MX_ISA:
			/* The above bridges mis-identify themselves */
			return (1);
		}
		break;
	case PCI_VENDOR_SIS:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_SIS_85C503:
			/* mis-identifies itself as a miscellaneous prehistoric */
			return (1);
		}
		break;
	case PCI_VENDOR_VIATECH:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_VIATECH_VT82C686A_SMB:
			/* mis-identifies itself as a ISA bridge */
			return (0);
		}
		break;
	}

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_BRIDGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_BRIDGE_ISA)
		return (1);

	return (0);
}

void
pcibattach(struct device *parent, struct device *self, void *aux)
{
	struct pcib_softc *sc = (struct pcib_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;

	printf("\n");

	/*
	 * If we are actually a glxpcib, we can't use softc fields
	 * beyond the base struct device, for this would corrupt
	 * the glxpcib softc. Decide to attach isa immediately in this
	 * case - glxpcib-based designs are not expected to have ISA
	 * slots and attaching isa early should not cause problems.
	 */
	if (strncmp(self->dv_xname, "glxpcib", 7) == 0) {
		pcib_isa_attach(sc, pa->pa_iot, pa->pa_memt, pa->pa_dmat);
	} else {
		/*
		 * Wait until all PCI devices are attached before attaching isa;
		 * existing pcib code on other platforms mentions that not
		 * doing this ``might mess the interrupt setup on some
		 * systems'', although this is very unlikely to be the case
		 * on loongson.
		 */
		sc->sc_iot = pa->pa_iot;
		sc->sc_memt = pa->pa_memt;
		sc->sc_dmat = pa->pa_dmat;
		config_defer(self, pcib_callback);
	}
}

void
pcib_callback(struct device *self)
{
	struct pcib_softc *sc = (struct pcib_softc *)self;

	pcib_isa_attach(sc, sc->sc_iot, sc->sc_memt, sc->sc_dmat);
}

void
pcib_isa_attach(struct pcib_softc *sc, bus_space_tag_t iot,
    bus_space_tag_t memt, bus_dma_tag_t dmat)
{
	struct isabus_attach_args iba;

	/*
	 * Attach the ISA bus behind this bridge.
	 */
	memset(&iba, 0, sizeof(iba));
	iba.iba_busname = "isa";
	iba.iba_iot = iot;
	iba.iba_memt = memt;
#if NISADMA > 0
	iba.iba_dmat = dmat;
#endif
	iba.iba_ic = sys_platform->isa_chipset;
	if (iba.iba_ic != NULL)
		config_found(&sc->sc_dev, &iba, pcib_print);
}

int
pcib_print(void *aux, const char *pnp)
{
	/* Only ISAs can attach to pcib's; easy. */
	if (pnp)
		printf("isa at %s", pnp);
	return (UNCONF);
}
