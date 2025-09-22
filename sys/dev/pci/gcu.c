/*	$OpenBSD: gcu.c,v 1.7 2024/05/13 01:15:51 jsg Exp $	*/

/*
 * Copyright (c) 2009 Dariusz Swiderski <sfires@sfires.net>
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
 * Driver for a GCU device that appears on embedded intel systems, like 80579
 */                                                                     

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/gcu_var.h>

int gcu_probe(struct device *, void *, void *);
void gcu_attach(struct device *, struct device *, void *);

const struct pci_matchid gcu_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EP80579_GCU }
};
        
struct cfdriver gcu_cd = {
	NULL, "gcu", DV_IFNET
};

const struct cfattach gcu_ca = {
	sizeof(struct gcu_softc), gcu_probe, gcu_attach
};

int
gcu_probe(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, gcu_devices,
	    nitems(gcu_devices)));
}

void
gcu_attach(struct device *parent, struct device *self, void *aux)
{
	struct gcu_softc *sc = (struct gcu_softc *)self;
	struct pci_attach_args *pa = aux;
	int val;

	val = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_MAPREG_START);
	if (PCI_MAPREG_TYPE(val) != PCI_MAPREG_TYPE_MEM) {
		printf(": mmba is not mem space\n");
		return;
	}

	if (pci_mapreg_map(pa, 0x10, PCI_MAPREG_MEM_TYPE(val), 0, &sc->tag, 
	    &sc->handle, &sc->addr, &sc->size, 0)) {
		printf(": cannot find mem space\n");
		return;
	}

	mtx_init(&sc->mdio_mtx, IPL_NET);

	printf("\n");
}
