/*	$OpenBSD: tcic2_isa.c,v 1.11 2022/04/06 18:59:29 naddy Exp $	*/
/*	$NetBSD: tcic2_isa.c,v 1.2 1999/04/08 16:14:29 bad Exp $	*/

#undef	TCICISADEBUG

/*
 *
 * Copyright (c) 1998, 1999 Christoph Badura. All rights reserved.
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

#include <dev/ic/tcic2reg.h>
#include <dev/ic/tcic2var.h>

/*****************************************************************************
 * Configurable parameters.
 *****************************************************************************/

/*
 * Default I/O allocation range.  If both are set to non-zero, these
 * values will be used instead.  Otherwise, the code attempts to probe
 * the bus width.  Systems with 10 address bits should use 0x300 and 0xff.
 * Systems with 12 address bits (most) should use 0x400 and 0xbff.
 */

#ifndef TCIC_ISA_ALLOC_IOBASE
#define	TCIC_ISA_ALLOC_IOBASE		0
#endif

#ifndef TCIC_ISA_ALLOC_IOSIZE
#define	TCIC_ISA_ALLOC_IOSIZE		0
#endif

int	tcic_isa_alloc_iobase = TCIC_ISA_ALLOC_IOBASE;
int	tcic_isa_alloc_iosize = TCIC_ISA_ALLOC_IOSIZE;

/*
 * Default IRQ allocation bitmask.  This defines the range of allowable
 * IRQs for PCMCIA slots.  Useful if order of probing would screw up other
 * devices, or if TCIC hardware/cards have trouble with certain interrupt
 * lines.
 *
 * We disable IRQ 10 by default, since some common laptops (namely, the
 * NEC Versa series) reserve IRQ 10 for the docking station SCSI interface.
 *
 * XXX Do we care about this?  the Versa doesn't use a tcic. -chb
 */

#ifndef TCIC_ISA_INTR_ALLOC_MASK
#define	TCIC_ISA_INTR_ALLOC_MASK	0xffff
#endif

int	tcic_isa_intr_alloc_mask = TCIC_ISA_INTR_ALLOC_MASK;

/*****************************************************************************
 * End of configurable parameters.
 *****************************************************************************/

#ifdef TCICISADEBUG
int	tcic_isa_debug = 1;
#define	DPRINTF(arg) if (tcic_isa_debug) printf arg;
#else
#define	DPRINTF(arg)
#endif

int	tcic_isa_probe(struct device *, void *, void *);
void	tcic_isa_attach(struct device *, struct device *, void *);

void	*tcic_isa_chip_intr_establish(pcmcia_chipset_handle_t,
	    struct pcmcia_function *, int, int (*) (void *), void *, char *);
void	tcic_isa_chip_intr_disestablish(pcmcia_chipset_handle_t, void *);
const char *tcic_isa_chip_intr_string(pcmcia_chipset_handle_t, void *);

const struct cfattach tcic_isa_ca = {
	sizeof(struct tcic_softc), tcic_isa_probe, tcic_isa_attach
};

static struct pcmcia_chip_functions tcic_isa_functions = {
	tcic_chip_mem_alloc,
	tcic_chip_mem_free,
	tcic_chip_mem_map,
	tcic_chip_mem_unmap,

	tcic_chip_io_alloc,
	tcic_chip_io_free,
	tcic_chip_io_map,
	tcic_chip_io_unmap,

	tcic_isa_chip_intr_establish,
	tcic_isa_chip_intr_disestablish,
	tcic_isa_chip_intr_string,

	tcic_chip_socket_enable,
	tcic_chip_socket_disable,
};

int
tcic_isa_probe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh, memh;
	int val, found;

	/* Disallow wildcarded i/o address. */
	if (ia->ia_iobase == -1 /* ISACF_PORT_DEFAULT */)
		return (0);

	if (bus_space_map(iot, ia->ia_iobase, TCIC_IOSIZE, 0, &ioh))
		return (0);

	if (ia->ia_msize == 0)
		ia->ia_msize = TCIC_MEMSIZE;

	if (bus_space_map(ia->ia_memt, ia->ia_maddr, ia->ia_msize, 0, &memh))
		return (0);

	DPRINTF(("tcic probing 0x%03x\n", ia->ia_iobase));
	found = 0;

	/*
	 * First, check for the reserved bits to be zero.
	 */
	if (tcic_check_reserved_bits(iot, ioh)) {
		DPRINTF(("tcic: reserved bits checked OK\n"));
		/* Second, check whether the we know how to handle the chip. */
		if ((val = tcic_chipid(iot, ioh))) {
			DPRINTF(("tcic id: 0x%02x\n", val));
			if (tcic_chipid_known(val))
				found++;
		}
	}
	else
		DPRINTF(("tcic: reserved bits didn't check OK\n"));

	bus_space_unmap(iot, ioh, TCIC_IOSIZE);
	bus_space_unmap(ia->ia_memt, memh, ia->ia_msize);

	if (!found)
		return (0);

	ia->ia_iosize = TCIC_IOSIZE;

	return (1);
}

void
tcic_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct tcic_softc *sc = (void *) self;
	struct isa_attach_args *ia = aux;
	isa_chipset_tag_t ic = ia->ia_ic;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_tag_t memt = ia->ia_memt;
	bus_space_handle_t ioh;
	bus_space_handle_t memh;

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
	sc->subregionmask = (1 << (ia->ia_msize / TCIC_MEM_PAGESIZE)) - 1;
	sc->memsize2 = tcic_log2((u_int)ia->ia_msize);

	sc->intr_est = ic;
	sc->pct = (pcmcia_chipset_tag_t) &tcic_isa_functions;

	sc->iot = iot;
	sc->ioh = ioh;
	sc->memt = memt;
	sc->memh = memh;

	/*
	 * determine chip type and initialise some chip type dependent
	 * parameters in softc.
	 */
	sc->chipid = tcic_chipid(iot, ioh);
	sc->validirqs = tcic_validirqs(sc->chipid);

	/*
	 * allocate an irq.  interrupts are relatively
	 * scarce but for TCIC controllers very infrequent.
	 */

	if ((sc->irq = ia->ia_irq) == IRQUNK) {
		if (isa_intr_alloc(ic,
		    sc->validirqs & (tcic_isa_intr_alloc_mask & 0xff00),
		    IST_EDGE, &sc->irq)) {
			printf("\n%s: can't allocate interrupt\n",
			    sc->dev.dv_xname);
			return;
		}
		printf(": using irq %d", sc->irq);
	}
	printf("\n");

	tcic_attach(sc);


	/*
	 * XXX mycroft recommends I/O space range 0x400-0xfff.
	 */

	/*
	 * XXX some hardware doesn't seem to grok addresses in 0x400 range--
	 * apparently missing a bit or more of address lines. (e.g.
	 * CIRRUS_PD672X with Linksys EthernetCard ne2000 clone in TI
	 * TravelMate 5000--not clear which is at fault)
	 * 
	 * Add a kludge to detect 10 bit wide buses and deal with them,
	 * and also a config file option to override the probe.
	 */

#if 0
	/*
	 * This is what we'd like to use, but...
	 */
	sc->iobase = 0x400;
	sc->iosize = 0xbff;
#else
	/*
	 * ...the above bus width probe doesn't always work.
	 * So, experimentation has shown the following range
	 * to not lose on systems that 0x300-0x3ff loses on
	 * (e.g. the NEC Versa 6030X).
	 */
	sc->iobase = 0x330;
	sc->iosize = 0x0cf;
#endif

	DPRINTF(("%s: bus_space_alloc range 0x%04lx-0x%04lx)\n",
	    sc->dev.dv_xname, (long) sc->iobase,
	    (long) sc->iobase + sc->iosize));

	if (tcic_isa_alloc_iobase && tcic_isa_alloc_iosize) {
		sc->iobase = tcic_isa_alloc_iobase;
		sc->iosize = tcic_isa_alloc_iosize;

		DPRINTF(("%s: bus_space_alloc range 0x%04lx-0x%04lx "
		    "(config override)\n", sc->dev.dv_xname, (long) sc->iobase,
		    (long) sc->iobase + sc->iosize));
	}
	sc->ih = isa_intr_establish(ic, sc->irq, IST_EDGE, IPL_TTY,
	    tcic_intr, sc, sc->dev.dv_xname);
	if (sc->ih == NULL) {
		printf("%s: can't establish interrupt\n", sc->dev.dv_xname);
		return;
	}

	tcic_attach_sockets(sc);
}

void *
tcic_isa_chip_intr_establish(pcmcia_chipset_handle_t pch,
    struct pcmcia_function *pf, int ipl, int (*fct)(void *), void *arg,
    char *xname)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;
	int irq, ist, val, reg;
	void *ih;
	int irqmap[] = {
	    0, 0, 0, TCIC_SCF1_IRQ3, TCIC_SCF1_IRQ4, TCIC_SCF1_IRQ5,
	    TCIC_SCF1_IRQ6, TCIC_SCF1_IRQ7, 0, TCIC_SCF1_IRQ9,
	    TCIC_SCF1_IRQ10, TCIC_SCF1_IRQ11, TCIC_SCF1_IRQ12, 0,
	    TCIC_SCF1_IRQ14, TCIC_SCF1_IRQ15
	};

	DPRINTF(("%s: tcic_isa_chip_intr_establish\n", h->sc->dev.dv_xname));

	/* XXX should we convert level to pulse? -chb  */
	if (pf->cfe->flags & PCMCIA_CFE_IRQLEVEL)
		ist = IST_LEVEL;
	else if (pf->cfe->flags & PCMCIA_CFE_IRQPULSE)
		ist = IST_PULSE;
	else
		ist = IST_LEVEL;

	if (isa_intr_alloc(h->sc->intr_est,
	    h->sc->validirqs & tcic_isa_intr_alloc_mask, ist, &irq))
		return (NULL);
	if ((ih = isa_intr_establish(h->sc->intr_est, irq, ist, ipl,
	    fct, arg, h->pcmcia->dv_xname)) == NULL)
		return (NULL);

	DPRINTF(("%s: intr established\n", h->sc->dev.dv_xname));

	h->ih_irq = irq;

	reg = TCIC_IR_SCF1_N(h->sock);
	val = (tcic_read_ind_2(h, reg) & (~TCIC_SCF1_IRQ_MASK)) | irqmap[irq];
	tcic_write_ind_2(h, reg, val);

	printf(" irq %d", irq);
	return (ih);
}

void 
tcic_isa_chip_intr_disestablish(pcmcia_chipset_handle_t pch, void *ih)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;
	int val, reg;

	DPRINTF(("%s: tcic_isa_chip_intr_disestablish\n", h->sc->dev.dv_xname));

	h->ih_irq = 0;

	reg = TCIC_IR_SCF1_N(h->sock);
	val = tcic_read_ind_2(h, reg);
	val &= ~TCIC_SCF1_IRQ_MASK;
	tcic_write_ind_2(h, reg, val);

	isa_intr_disestablish(h->sc->intr_est, ih);
}

const char *
tcic_isa_chip_intr_string(pcmcia_chipset_handle_t pch, void *ih)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;
	static char irqstr[64];

	if (ih == NULL)
		snprintf(irqstr, sizeof(irqstr), "couldn't establish interrupt");
	else
		snprintf(irqstr, sizeof(irqstr), "irq %d", h->ih_irq);
	return (irqstr);
}
