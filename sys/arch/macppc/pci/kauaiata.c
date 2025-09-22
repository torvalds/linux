/*	$OpenBSD: kauaiata.c,v 1.12 2023/04/11 00:45:07 jsg Exp $ */

/*
 * Copyright (c) 2003 Dale Rahn
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Glue to attach kauai ata to the macobio_wdc
 * which it heavily resembles.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/ofw/openfirm.h>

#include <machine/autoconf.h>


struct kauaiata_softc {
	struct device sc_dev;
	struct ppc_bus_space sc_membus_space;
	/* XXX */
};

int kauaiatamatch(struct device *parent, void *match, void *aux);
void kauaiataattach(struct device *parent, struct device *self, void *aux);
int kauaiata_print(void *aux, const char *dev);


const struct cfattach kauaiata_ca = {
	sizeof(struct kauaiata_softc), kauaiatamatch, kauaiataattach,
};

struct cfdriver kauaiata_cd = {
	NULL, "kauaiata", DV_DULL,
};

int
kauaiatamatch(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	/*
	 * Match the adapter
	 * XXX match routine??
	 */
	switch(PCI_VENDOR(pa->pa_id)) {
	case PCI_VENDOR_APPLE:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_APPLE_UNINORTH_ATA:
		case PCI_PRODUCT_APPLE_INTREPID_ATA:
		case PCI_PRODUCT_APPLE_INTREPID2_ATA:
		case PCI_PRODUCT_APPLE_K2_ATA:
		case PCI_PRODUCT_APPLE_SHASTA_ATA:
			return (1);
		}
		break;
	}
	return 0;
}

void
kauaiataattach(struct device *parent, struct device *self, void *aux)
{
	int node;
	struct confargs ca;
	int namelen;
	u_int32_t reg[20];
	char name[32];
	int32_t intr[8];

	struct kauaiata_softc *sc = (struct kauaiata_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;

	/* XXX not necessarily the right device */
	node = OF_finddevice("uata");
	if (node == -1)
		node = OF_finddevice("/pci@f4000000/ata-6");

	if (node == -1) {
		printf("\n");
		return;
	}

	/*
	 * XXX - need to compare node and PCI id to verify this is the 
	 * correct device.
	 */

	ca.ca_nreg  = OF_getprop(node, "reg", reg, sizeof(reg));

	/*
	 * The PCI Interrupt Configuration Registers seems to be
	 * hardwired to 0.  Get the interrupt line from OpenFirmware.
	 */
	/* XXX */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_APPLE &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_APPLE_SHASTA_ATA) {
		ca.ca_nintr = OF_getprop(node, "interrupts",
		    intr, sizeof intr);
	} else {
		ca.ca_nintr = 4;
		intr[0] = 0x27;
	}

	namelen = OF_getprop(node, "name", name, sizeof(name));
	if ((namelen < 0) || (namelen >= sizeof(name))) {
		printf(" bad name prop len %x\n", namelen);
		return;
	}

	name[namelen] = 0; /* name property may not be null terminated */

	/* config read */
	sc->sc_membus_space.bus_base =
	    pci_conf_read(pc, pa->pa_tag, PCI_MAPREG_START);

	/* make sure device memory access is enabled */
	{
		bus_space_tag_t		iot;
		bus_space_handle_t	ioh;
		bus_size_t		size;

		if (pci_mapreg_map(pa, PCI_MAPREG_START, PCI_MAPREG_TYPE_MEM, 0,
		    &iot, &ioh, NULL, &size, 0)) {
			printf(": mapping memory failed\n");
			return;
		}
			
		bus_space_unmap(iot, ioh, size);
	}
#if 0
	pci_conf_write(pc, pa->pa_tag, PCI_MAPREG_START, 0xffffffff);
	size =  ~(pci_conf_read(pc, pa->pa_tag, PCI_MAPREG_START));
	pci_conf_write(pc, pa->pa_tag, PCI_MAPREG_START,
		sc->sc_membus_space.bus_base);
#endif

	ca.ca_baseaddr = sc->sc_membus_space.bus_base;

	ca.ca_name = name;
	ca.ca_iot = &sc->sc_membus_space;
	ca.ca_dmat = pa->pa_dmat;

	ca.ca_reg = reg;
	reg[0] = 0x2000; /* offset to wdc registers */
	reg[1] = reg[9] - 0x2000; /* map size of wdc registers */
	reg[2] = 0x1000; /* offset to dbdma registers */
	reg[3] = 0x1000; /* map size of dbdma registers */
	ca.ca_intr = intr;

	printf("\n");

	config_found(self, &ca, kauaiata_print);
}

int
kauaiata_print(void *aux, const char *dev)
{
	return QUIET;
}
