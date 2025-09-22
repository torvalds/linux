/*	$OpenBSD: i82365_isasubr.c,v 1.26 2021/03/07 06:17:03 jsg Exp $	*/
/*	$NetBSD: i82365_isasubr.c,v 1.1 1998/06/07 18:28:31 sommerfe Exp $  */

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

/*****************************************************************************
 * Configurable parameters.
 *****************************************************************************/

/*
 * Default I/O allocation range.  If both are set to non-zero, these
 * values will be used instead.  Otherwise, the code attempts to probe
 * the bus width.
 */

#ifndef PCIC_ISA_ALLOC_IOBASE
#define	PCIC_ISA_ALLOC_IOBASE		0
#endif

#ifndef PCIC_ISA_ALLOC_IOSIZE
#define	PCIC_ISA_ALLOC_IOSIZE		0
#endif

int	pcic_isa_alloc_iobase = PCIC_ISA_ALLOC_IOBASE;
int	pcic_isa_alloc_iosize = PCIC_ISA_ALLOC_IOSIZE;

/*
 * I am well aware that some of later irqs below are not for real, but there
 * is a way to deal with that in the search loop.  For beauty's sake I want
 * this list to be a permutation of 0..15.
 */
char	pcic_isa_intr_list[] = {
	3, 4, 14, 9, 5, 12, 10, 11, 15, 13, 7, 1, 6, 2, 0, 8
};

struct pcic_ranges pcic_isa_addr[] = {
	{ 0x340, 0x030 },
	{ 0x300, 0x030 },
	{ 0x390, 0x020 },
	{ 0x400, 0xbff },
	{ 0, 0 },		/* terminator */
};


/*****************************************************************************
 * End of configurable parameters.
 *****************************************************************************/

#ifdef PCICISADEBUG
int	pcicsubr_debug = 1 /* XXX */ ;
#define	DPRINTF(arg) if (pcicsubr_debug) printf arg;
#else
#define	DPRINTF(arg)
#endif

static int pcic_intr_seen;

void
pcic_isa_bus_width_probe(struct pcic_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh, bus_addr_t base, u_int32_t length)
{
	bus_space_handle_t ioh_high;
	int i, iobuswidth, tmp1, tmp2;

	/*
	 * figure out how wide the isa bus is.  Do this by checking if the
	 * pcic controller is mirrored 0x400 above where we expect it to be.
	 *
	 * XXX some hardware doesn't seem to grok addresses in 0x400
	 * range-- apparently missing a bit or more of address lines.
	 * (e.g. CIRRUS_PD672X with Linksys EthernetCard ne2000 clone
	 * in TI TravelMate 5000 -- not clear which is at fault)
	 * 
	 * Add a kludge to detect 10 bit wide buses and deal with them,
	 * and also a config file option to override the probe.
	 */
	iobuswidth = 12;

	/* Map i/o space. */
	if (bus_space_map(iot, base + 0x400, length, 0, &ioh_high)) {
		printf("%s: can't map high i/o space\n", sc->dev.dv_xname);
		return;
	}

	for (i = 0; i < PCIC_NSLOTS; i++) {
		if (sc->handle[i].flags & PCIC_FLAG_SOCKETP) {
			/*
			 * read the ident flags from the normal space and
			 * from the mirror, and compare them
			 */
			bus_space_write_1(iot, ioh, PCIC_REG_INDEX,
			    sc->handle[i].sock + PCIC_IDENT);
			tmp1 = bus_space_read_1(iot, ioh, PCIC_REG_DATA);

			bus_space_write_1(iot, ioh_high, PCIC_REG_INDEX,
			    sc->handle[i].sock + PCIC_IDENT);
			tmp2 = bus_space_read_1(iot, ioh_high, PCIC_REG_DATA);

			if (tmp1 == tmp2)
				iobuswidth = 10;
		}
	}

	bus_space_unmap(iot, ioh_high, length);

	sc->ranges = pcic_isa_addr;
	if (iobuswidth == 10) {
		sc->iobase = 0x000;
		sc->iosize = 0x400;
	} else {
		sc->iobase = 0x0000;
		sc->iosize = 0x1000;
	}

	DPRINTF(("%s: bus_space_alloc range 0x%04lx-0x%04lx (probed)\n",
	    sc->dev.dv_xname, (long) sc->iobase,
	    (long) sc->iobase + sc->iosize));

	if (pcic_isa_alloc_iobase && pcic_isa_alloc_iosize) {
		sc->iobase = pcic_isa_alloc_iobase;
		sc->iosize = pcic_isa_alloc_iosize;

		DPRINTF(("%s: bus_space_alloc range 0x%04lx-0x%04lx "
		    "(config override)\n", sc->dev.dv_xname, (long) sc->iobase,
		    (long) sc->iobase + sc->iosize));
	}
}


void *
pcic_isa_chip_intr_establish(pcmcia_chipset_handle_t pch,
    struct pcmcia_function *pf, int ipl, int (*fct)(void *), void *arg,
    char *xname)
{
	struct pcic_handle *h = (struct pcic_handle *)pch;
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);
	isa_chipset_tag_t ic = sc->intr_est;
	int irq, ist, reg;

	if (pf->cfe->flags & PCMCIA_CFE_IRQLEVEL)
		ist = IST_LEVEL;
	else if (pf->cfe->flags & PCMCIA_CFE_IRQPULSE)
		ist = IST_PULSE;
	else
		ist = IST_EDGE;

	irq = pcic_intr_find(sc, ist);
	if (!irq)
		return (NULL);

	h->ih_irq = irq;
	reg = pcic_read(h, PCIC_INTR);
	reg &= ~(PCIC_INTR_IRQ_MASK | PCIC_INTR_ENABLE);
	pcic_write(h, PCIC_INTR, reg | irq);

	return isa_intr_establish(ic, irq, ist, ipl, fct, arg,
	    h->pcmcia->dv_xname);
}

void 
pcic_isa_chip_intr_disestablish(pcmcia_chipset_handle_t pch, void *ih)
{
	struct pcic_handle *h = (struct pcic_handle *) pch;
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);
	isa_chipset_tag_t ic = sc->intr_est;
	int reg;

	h->ih_irq = 0;

	isa_intr_disestablish(ic, ih);

	reg = pcic_read(h, PCIC_INTR);
	reg &= ~(PCIC_INTR_IRQ_MASK | PCIC_INTR_ENABLE);
	pcic_write(h, PCIC_INTR, reg);
}

const char *
pcic_isa_chip_intr_string(pcmcia_chipset_handle_t pch, void *ih)
{
	struct pcic_handle *h = (struct pcic_handle *)pch;
	static char irqstr[64];

	if (ih == NULL)
		snprintf(irqstr, sizeof(irqstr), "couldn't establish interrupt");
	else
		snprintf(irqstr, sizeof(irqstr), "irq %d", h->ih_irq);
	return (irqstr);
}

int
pcic_intr_probe(void *v)
{
	pcic_intr_seen = 1;
	return (1);
}

/*
 * Try to find a working interrupt, first by searching for a unique
 * irq that is known to work, verified by tickling the pcic, then
 * by searching for a shareable irq known to work.  If the pcic does
 * not allow tickling we then fallback to the same strategy but without
 * tickling just assuming the first usable irq found works.
 */
int
pcic_intr_find(struct pcic_softc *sc, int ist)
{
	struct pcic_handle *ph = &sc->handle[0];
	isa_chipset_tag_t ic = sc->intr_est;
	int i, tickle, check, irq, chosen_irq = 0, csc_touched = 0;
	void *ih;
	u_int8_t saved_csc_intr;

	/*
	 * First time, look for entirely free interrupts, last
	 * time accept shareable ones.
	 */
	for (tickle = 1; tickle >= 0; tickle--) {
		if (tickle)
			/*
			 * Remember card status change interrupt
			 * configuration.
			 */
			saved_csc_intr = pcic_read(ph, PCIC_CSC_INTR);

		for (check = 2; check; check--) {

			/* Walk over all possible interrupts. */
			for (i = 0; i < 16; i++) {
				irq = pcic_isa_intr_list[i];

				if (((1 << irq) &
				     PCIC_CSC_INTR_IRQ_VALIDMASK) == 0)
					continue;

				if (isa_intr_check(ic, irq, ist) < check)
					continue;

				if (!tickle) {
					chosen_irq = irq;
					goto out;
				}

				/*
				 * Prepare for an interrupt tickle.
				 * As this can be called from an
				 * IPL_TTY context (the card status
				 * change interrupt) we need to do
				 * higher.
				 */
				ih = isa_intr_establish(ic, irq, ist,
				    IPL_VM | IPL_MPSAFE, pcic_intr_probe,
				    0, sc->dev.dv_xname);
				if (ih == NULL)
					continue;
				pcic_intr_seen = 0;
				pcic_write(ph, PCIC_CSC_INTR,
				    (saved_csc_intr & ~PCIC_CSC_INTR_IRQ_MASK)
				    | PCIC_CSC_INTR_CD_ENABLE
				    | (irq << PCIC_CSC_INTR_IRQ_SHIFT));
				csc_touched = 1;

				/* Teehee, you tickle me! ;-) */
				pcic_write(ph, PCIC_CARD_DETECT,
				    pcic_read(ph, PCIC_CARD_DETECT) |
				    PCIC_CARD_DETECT_SW_INTR);

				/*
				 * Delay for 10 ms and then shut the
				 * probe off.  That should be plenty
				 * of time for the interrupt to be
				 * handled.
				 */
				delay(10000);

				/* Acknowledge the interrupt. */
				pcic_read(ph, PCIC_CSC);

				isa_intr_disestablish(ic, ih);

				if (pcic_intr_seen) {
					chosen_irq = irq;
					goto out;
				}
			}
		}
	}

out:
	if (csc_touched)
		/* Restore card detection bit. */
		pcic_write(ph, PCIC_CSC_INTR, saved_csc_intr);
	return (chosen_irq);
}
