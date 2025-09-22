/*	$OpenBSD: i82365_isa.c,v 1.25 2022/04/06 18:59:28 naddy Exp $	*/
/*	$NetBSD: i82365_isa.c,v 1.11 1998/06/09 07:25:00 thorpej Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciachip.h>

#include <dev/ic/i82365reg.h>
#include <dev/ic/i82365var.h>
#include <dev/isa/i82365_isavar.h>

#ifdef PCICISADEBUG
#define	DPRINTF(arg)	printf arg;
#else
#define	DPRINTF(arg)
#endif

int	pcic_isa_probe(struct device *, void *, void *);
void	pcic_isa_attach(struct device *, struct device *, void *);

const struct cfattach pcic_isa_ca = {
	sizeof(struct pcic_softc), pcic_isa_probe, pcic_isa_attach
};

static struct pcmcia_chip_functions pcic_isa_functions = {
	pcic_chip_mem_alloc,
	pcic_chip_mem_free,
	pcic_chip_mem_map,
	pcic_chip_mem_unmap,

	pcic_chip_io_alloc,
	pcic_chip_io_free,
	pcic_chip_io_map,
	pcic_chip_io_unmap,

	pcic_isa_chip_intr_establish,
	pcic_isa_chip_intr_disestablish,
	pcic_isa_chip_intr_string,

	pcic_chip_socket_enable,
	pcic_chip_socket_disable,
};

int
pcic_isa_probe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t memt = ia->ia_memt, iot = ia->ia_iot;
	bus_space_handle_t ioh, memh;
	bus_size_t msize;
	int val, found;

	/* Disallow wildcarded i/o address. */
	if (ia->ia_iobase == -1 /* ISACF_PORT_DEFAULT */)
		return (0);

	if (bus_space_map(iot, ia->ia_iobase, PCIC_IOSIZE, 0, &ioh))
		return (0);

	if (ia->ia_msize == -1)
		ia->ia_msize = PCIC_MEMSIZE;

	msize = ia->ia_msize;
	if (bus_space_map(memt, ia->ia_maddr, ia->ia_msize, 0, &memh)) {
		if (ia->ia_msize > PCIC_MEMSIZE &&
		    !bus_space_map(memt, ia->ia_maddr, PCIC_MEMSIZE, 0, &memh))
			msize = PCIC_MEMSIZE;
		else
			return (0);
	}
	found = 0;

	/*
	 * this could be done with a loop, but it would violate the
	 * abstraction
	 */

	bus_space_write_1(iot, ioh, PCIC_REG_INDEX, C0SA + PCIC_IDENT);
	val = bus_space_read_1(iot, ioh, PCIC_REG_DATA);
	if (pcic_ident_ok(val))
		found++;

	bus_space_write_1(iot, ioh, PCIC_REG_INDEX, C0SB + PCIC_IDENT);
	val = bus_space_read_1(iot, ioh, PCIC_REG_DATA);
	if (pcic_ident_ok(val))
		found++;

	bus_space_write_1(iot, ioh, PCIC_REG_INDEX, C1SA + PCIC_IDENT);
	val = bus_space_read_1(iot, ioh, PCIC_REG_DATA);
	if (pcic_ident_ok(val))
		found++;

	bus_space_write_1(iot, ioh, PCIC_REG_INDEX, C1SB + PCIC_IDENT);
	val = bus_space_read_1(iot, ioh, PCIC_REG_DATA);
	if (pcic_ident_ok(val))
		found++;

	bus_space_unmap(iot, ioh, PCIC_IOSIZE);
	bus_space_unmap(memt, memh, msize);

	if (!found)
		return (0);
	ia->ia_iosize = PCIC_IOSIZE;
	ia->ia_msize = msize;
	return (1);
}

void
pcic_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct pcic_softc *sc = (void *)self;
	struct pcic_handle *h;
	struct isa_attach_args *ia = aux;
	isa_chipset_tag_t ic = ia->ia_ic;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_tag_t memt = ia->ia_memt;
	bus_space_handle_t ioh;
	bus_space_handle_t memh;
	int irq, i;

	/* Map i/o space. */
	if (bus_space_map(iot, ia->ia_iobase, ia->ia_iosize, 0, &ioh)) {
		printf(": can't map i/o space\n");
		return;
	}

	/* Map mem space. */
	if (bus_space_map(memt, ia->ia_maddr, ia->ia_msize, 0, &memh)) {
		printf(": can't map mem space\n");
		return;
	}

	sc->membase = ia->ia_maddr;
	sc->subregionmask = (1 << (ia->ia_msize / PCIC_MEM_PAGESIZE)) - 1;

	sc->intr_est = ic;
	sc->pct = (pcmcia_chipset_tag_t)&pcic_isa_functions;

	sc->iot = iot;
	sc->ioh = ioh;
	sc->memt = memt;
	sc->memh = memh;

	printf("\n");

	pcic_attach(sc);
	pcic_isa_bus_width_probe(sc, iot, ioh, ia->ia_iobase, ia->ia_iosize);
	pcic_attach_sockets(sc);

	/*
	 * Allocate an irq.  It will be used by both controllers.  I could
	 * use two different interrupts, but interrupts are relatively
	 * scarce, shareable, and for PCIC controllers, very infrequent.
	 */
	irq = ia->ia_irq;
	if (irq == IRQUNK)
		irq = pcic_intr_find(sc, IST_EDGE);

	if (irq) {
		sc->ih = isa_intr_establish(ic, irq, IST_EDGE, IPL_TTY,
		    pcic_intr, sc, sc->dev.dv_xname);
		if (!sc->ih)
			irq = 0;
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
