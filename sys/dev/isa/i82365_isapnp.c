/*	$OpenBSD: i82365_isapnp.c,v 1.11 2022/04/06 18:59:28 naddy Exp $ */
/*	$NetBSD: i82365_isapnp.c,v 1.8 2000/02/23 17:22:11 soren Exp $	*/

/*
 * Copyright (c) 1998 Bill Sommerfeld.  All rights reserved.
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

#include <dev/isa/isapnpreg.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciachip.h>

#include <dev/ic/i82365reg.h>
#include <dev/ic/i82365var.h>
#include <dev/isa/i82365_isavar.h>

#undef DPRINTF
#ifdef PCICISADEBUG
int	pcicisapnp_debug = 0 /* XXX */ ;
#define	DPRINTF(arg) if (pcicisapnp_debug) printf arg;
#else
#define	DPRINTF(arg)
#endif

int	pcic_isapnp_match(struct device *, void *, void *);
void	pcic_isapnp_attach(struct device *, struct device *, void *);

const struct cfattach pcic_isapnp_ca = {
	sizeof(struct pcic_softc), pcic_isapnp_match, pcic_isapnp_attach
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
pcic_isapnp_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

void
pcic_isapnp_attach(struct device *parent, struct device *self, void *aux)
{
	struct pcic_softc *sc = (void *) self;
	struct pcic_handle *h;
	struct isa_attach_args *ipa = aux;
	isa_chipset_tag_t ic = ipa->ia_ic;
	bus_space_tag_t iot = ipa->ia_iot;
	bus_space_tag_t memt = ipa->ia_memt;
	bus_space_handle_t ioh;
	bus_space_handle_t memh;
	int tmp1, i;

	printf("\n");

	if (isapnp_config(iot, memt, ipa)) {
		printf("%s: error in region allocation\n", sc->dev.dv_xname);
		return;
	}

	printf("%s: %s %s", sc->dev.dv_xname, ipa->ipa_devident,
	    ipa->ipa_devclass);

	/* sanity check that we get at least one hunk of IO space.. */
	if (ipa->ipa_nio < 1) {
		printf("%s: failed to get one chunk of i/o space\n",
		       sc->dev.dv_xname);
		return;
	}

	/* Find i/o space. */
	ioh = ipa->ipa_io[0].h;

	/* sanity check to make sure we have a real PCIC there.. */
	bus_space_write_1(iot, ioh, PCIC_REG_INDEX, C0SA + PCIC_IDENT);
	tmp1 = bus_space_read_1(iot, ioh, PCIC_REG_DATA);
	printf("(ident 0x%x", tmp1);
	if (pcic_ident_ok(tmp1)) {
	  	printf(" OK)");
	} else {
	        printf(" Not OK)\n");
		return;
	}

	if (bus_space_map(memt, ipa->ia_maddr, ipa->ia_msize, 0, &memh)) {
		printf(": can't map mem space\n");
		return;
	}

	sc->membase = ipa->ia_maddr;
	sc->subregionmask = (1 << (ipa->ia_msize / PCIC_MEM_PAGESIZE)) - 1;

	sc->intr_est = ic;
	sc->pct = (pcmcia_chipset_tag_t) & pcic_isa_functions;

	sc->iot = iot;
	sc->ioh = ioh;
	sc->memt = memt;
	sc->memh = memh;

	printf("\n");

	pcic_attach(sc);
	pcic_isa_bus_width_probe(sc, iot, ioh, ipa->ipa_io[0].base,
	    ipa->ipa_io[0].length);
	pcic_attach_sockets(sc);

	/*
	 * allocate an irq.  it will be used by both controllers.  I could
	 * use two different interrupts, but interrupts are relatively
	 * scarce, shareable, and for PCIC controllers, very infrequent.
	 */

	if (ipa->ipa_nirq > 0)
		sc->irq = ipa->ipa_irq[0].num;
	else
		sc->irq = IRQUNK;

	if (sc->irq == IRQUNK)
		sc->irq = pcic_intr_find(sc, IST_EDGE);

	if (sc->irq) {
		sc->ih = isa_intr_establish(ic, sc->irq, IST_EDGE, IPL_TTY,
		    pcic_intr, sc, sc->dev.dv_xname);
		if (!sc->ih)
			sc->irq = 0;
	}

	if (sc->irq) {
		printf("%s: irq %d, ", sc->dev.dv_xname, sc->irq);

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
