/*	$OpenBSD: i82365_pci.c,v 1.17 2024/09/04 07:54:52 mglocker Exp $ */
/*	$NetBSD: i82365_pci.c,v 1.11 2000/02/24 03:42:44 itohy Exp $	*/

/*
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
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
 *	This product includes software developed by Marc Horowitz.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * XXX this driver frontend is *very* i386 dependent and should be relocated
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/ic/i82365reg.h>
#include <dev/ic/i82365var.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/i82365_pcivar.h>

#include <dev/isa/i82365_isavar.h>

/*
 * PCI constants.
 * XXX These should be in a common file!
 */
#define	PCI_CBIO		0x10	/* Configuration Base IO Address */

int	pcic_pci_match(struct device *, void *, void *);
void	pcic_pci_attach(struct device *, struct device *, void *);

const struct cfattach pcic_pci_ca = {
	sizeof(struct pcic_pci_softc), pcic_pci_match, pcic_pci_attach
};

static struct pcmcia_chip_functions pcic_pci_functions = {
	pcic_chip_mem_alloc,
	pcic_chip_mem_free,
	pcic_chip_mem_map,
	pcic_chip_mem_unmap,

	pcic_chip_io_alloc,
	pcic_chip_io_free,
	pcic_chip_io_map,
	pcic_chip_io_unmap,

	/* XXX */
	pcic_isa_chip_intr_establish,
	pcic_isa_chip_intr_disestablish,
	pcic_isa_chip_intr_string,

	pcic_chip_socket_enable,
	pcic_chip_socket_disable,
};

int
pcic_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *) aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_CIRRUS &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_CIRRUS_CL_PD6729)
		return (1);
	return (0);
}

void
pcic_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct pcic_softc *sc = (void *) self;
	struct pcic_pci_softc *psc = (void *) self;
	struct pcic_handle *h;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	bus_space_tag_t memt = pa->pa_memt;
	bus_space_handle_t memh;
	bus_size_t size;
	int irq, i;

	if (pci_mapreg_map(pa, PCI_CBIO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->iot, &sc->ioh, NULL, &size, 0)) {
		printf(": can't map i/o space\n");
		return;
	}

	/*
	 * XXX need some memory for mapping pcmcia cards into. Ideally, this
	 * would be completely dynamic.  Practically this doesn't work,
	 * because the extent mapper doesn't know about all the devices all
	 * the time.  With ISA we could finesse the issue by specifying the
	 * memory region in the config line.  We can't do that here, so we
	 * cheat for now. Jason Thorpe, you are my Savior, come up with a fix
	 * :-)
	 */

	/* Map mem space. */
	if (bus_space_map(memt, 0xd0000, 0x10000, 0, &memh)) {
		printf(": can't map mem space");
		bus_space_unmap(sc->iot, sc->ioh, size);
		return;
	}

	sc->membase = 0xd0000;
	sc->subregionmask = (1 << (0x10000 / PCIC_MEM_PAGESIZE)) - 1;

	/* same deal for io allocation */

	sc->iobase = 0x400;
	sc->iosize = 0xbff;

	/* end XXX */

	sc->pct = (pcmcia_chipset_tag_t) & pcic_pci_functions;

	sc->memt = memt;
	sc->memh = memh;

	printf("\n");
	pcic_attach(sc);
	pcic_attach_sockets(sc);

	/*
	 * Check to see if we're using PCI or ISA interrupts. I don't
	 * know of any i386 systems that use the 6729 in PCI interrupt
	 * mode, but maybe when the PCMCIA code runs on other platforms
	 * we'll need to fix this.
	 */
	pcic_write(&sc->handle[0], PCIC_CIRRUS_EXTENDED_INDEX,
		   PCIC_CIRRUS_EXT_CONTROL_1);
	if ((pcic_read(&sc->handle[0], PCIC_CIRRUS_EXTENDED_DATA) &
	    PCIC_CIRRUS_EXT_CONTROL_1_PCI_INTR_MASK)) {
		printf("%s: PCI interrupts not supported\n",
		       sc->dev.dv_xname);
		return;
	}

	psc->intr_est = pcic_pci_machdep_intr_est(pc);

	irq = pcic_intr_find(sc, IST_EDGE);

	/* Map and establish the interrupt. */
	if (irq) {
		sc->ih = pcic_pci_machdep_pcic_intr_establish(sc, pcic_intr);
		if (sc->ih == NULL) {
			printf("%s: couldn't map interrupt\n",
			    sc->dev.dv_xname);
			bus_space_unmap(memt, memh, 0x10000);
			bus_space_unmap(sc->iot, sc->ioh, size);
			return;
		}
	}
	sc->irq = irq;

	if (irq) {
                printf("%s: irq %d, ", sc->dev.dv_xname, irq);

                /* Set up the pcic to interrupt on card detect. */
                for (i = 0; i < PCIC_NSLOTS; i++) {
                        h = &sc->handle[i];
                        if (h->flags & PCIC_FLAG_SOCKETP) {
                                pcic_write(h, PCIC_CSC_INTR,
                                    (sc->irq << PCIC_CSC_INTR_IRQ_SHIFT) |
                                    PCIC_CSC_INTR_CD_ENABLE);
                        }
                }
        } else
                printf("%s: no irq, ", sc->dev.dv_xname);

        printf("polling enabled\n");
        if (sc->poll_established == 0) {
                timeout_set(&sc->poll_timeout, pcic_poll_intr, sc);
                timeout_add_msec(&sc->poll_timeout, 500);
                sc->poll_established = 1;
        }
}
