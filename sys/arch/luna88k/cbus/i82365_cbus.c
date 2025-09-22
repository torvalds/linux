/*	$OpenBSD: i82365_cbus.c,v 1.9 2024/06/01 00:48:16 aoyama Exp $	*/
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

/*
 * Driver for PC-9801-102 & PC-9821X[AE]-E01 PC Card slot adapter
 *  based on OpenBSD:src/sys/dev/isa/i82365_isa{,subr}.c
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/malloc.h>

#include <machine/board.h>		/* PC_BASE */
#include <machine/bus.h>
#include <machine/intr.h>

#include <arch/luna88k/cbus/cbusvar.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciachip.h>

#include <dev/ic/i82365reg.h>
#include <dev/ic/i82365var.h>

#ifdef PCICCBUSDEBUG
#define	DPRINTF(arg)	printf arg;
#else
#define	DPRINTF(arg)
#endif

/*
 * XXX:
 * The C-bus expects edge-triggered interrupts, but some PC Cards and
 * the controller itself produce level-triggered interrupts.  This causes
 * spurious interrupts on C-bus.
 * Then, we use CL-PD67XX 'pulse IRQ' feature in this driver.  This seems
 * to solve stray interrupts on C-bus.
 * (BTW, all NEC genuine C-bus PC Card slot adapters use CL-PD67XX)
 */

/* Cirrus Logic CL-PD67XX Misc Control register */
#define PCIC_CIRRUS_MISC_CTL_1			0x16
#define  PCIC_CIRRUS_MISC_CTL_1_PULSE_MGMT_INTR	0x04
#define  PCIC_CIRRUS_MISC_CTL_1_PULSE_SYS_IRQ	0x08

/* prototypes */
void	*pcic_cbus_chip_intr_establish(pcmcia_chipset_handle_t,
	    struct pcmcia_function *, int, int (*) (void *), void *, char *);
void	pcic_cbus_chip_intr_disestablish(pcmcia_chipset_handle_t, void *);
const char *pcic_cbus_chip_intr_string(pcmcia_chipset_handle_t, void *);
int	pcic_cbus_intlevel_find(void);
int	pcic_cbus_chip_io_alloc(pcmcia_chipset_handle_t, bus_addr_t,
	    bus_size_t, bus_size_t, struct pcmcia_io_handle *);
void	pcic_cbus_chip_io_free(pcmcia_chipset_handle_t,
	    struct pcmcia_io_handle *);

int	pcic_cbus_probe(struct device *, void *, void *);
void	pcic_cbus_attach(struct device *, struct device *, void *);

/* bus space tag for pcic_cbus */
struct luna88k_bus_space_tag pcic_cbus_io_bst = {
	.bs_stride_1 = 0,
	.bs_stride_2 = 0,
	.bs_stride_4 = 0,
	.bs_stride_8 = 0,	/* not used */
	.bs_offset = PCEXIO_BASE,
	.bs_flags = TAG_LITTLE_ENDIAN
};

struct luna88k_bus_space_tag pcic_cbus_mem_bst = {
	.bs_stride_1 = 0,
	.bs_stride_2 = 0,
	.bs_stride_4 = 0,
	.bs_stride_8 = 0,	/* not used */
	.bs_offset = PCEXMEM_BASE,
	.bs_flags = TAG_LITTLE_ENDIAN
};

const struct cfattach pcic_cbus_ca = {
	sizeof(struct pcic_softc), pcic_cbus_probe, pcic_cbus_attach
};

static struct pcmcia_chip_functions pcic_cbus_functions = {
	.mem_alloc	= pcic_chip_mem_alloc,
	.mem_free	= pcic_chip_mem_free,
	.mem_map	= pcic_chip_mem_map,
	.mem_unmap	= pcic_chip_mem_unmap,

	.io_alloc	= pcic_cbus_chip_io_alloc,
	.io_free	= pcic_cbus_chip_io_free,
	.io_map		= pcic_chip_io_map,
	.io_unmap	= pcic_chip_io_unmap,

	.intr_establish		= pcic_cbus_chip_intr_establish,
	.intr_disestablish	= pcic_cbus_chip_intr_disestablish,
	.intr_string		= pcic_cbus_chip_intr_string,

	.socket_enable	= pcic_chip_socket_enable,
	.socket_disable	= pcic_chip_socket_disable,
};

/*
 * NEC PC-9801 architecture uses different IRQ notation from PC-AT
 * architecture, so-called INT.  The MI pcic(4) driver internally uses
 * IRQ, so here is a table to convert INT to IRQ.
 */
static const int pcic_cbus_int2irq[NCBUSISR] = {
	PCIC_INTR_IRQ3,			/* INT 0 */
	PCIC_INTR_IRQ5,			/* INT 1 */
	PCIC_INTR_IRQ_RESERVED6,	/* INT 2 */
	PCIC_INTR_IRQ9,			/* INT 3 */
	PCIC_INTR_IRQ10,		/* INT 4(41) */
	PCIC_INTR_IRQ12,		/* INT 5 */
	PCIC_INTR_IRQ_RESERVED13	/* INT 6 */
};

/* And, a table to convert IRQ to INT */
static const int pcic_cbus_irq2int[] = {
	-1, -1, -1,  0, -1,  1,  2, -1,	/* IRQ 0- 7 */
	-1,  3,  4, -1,  5,  6, -1, -1	/* IRQ 8-15 */
};

struct pcic_ranges pcic_cbus_addr[] = {
	{ 0x340, 0x030 },
	{ 0x300, 0x030 },
	{ 0x390, 0x020 },
	{ 0x400, 0xbff },
	{ 0, 0},	/* terminator */
};

int
pcic_cbus_probe(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct cfdata *cf = match;
	struct cbus_attach_args *caa = aux;
	bus_space_tag_t iot = &pcic_cbus_io_bst;
	bus_space_tag_t memt = &pcic_cbus_mem_bst;
	bus_space_handle_t ioh, memh;
	bus_size_t msize;
	int val, found;

	if (strcmp(caa->ca_name, cf->cf_driver->cd_name) != 0)
		return (0);

	SET_TAG_LITTLE_ENDIAN(iot);
	SET_TAG_LITTLE_ENDIAN(memt);

	caa->ca_iobase = cf->cf_iobase;
	caa->ca_maddr  = cf->cf_maddr;
	caa->ca_msize  = cf->cf_msize;
	caa->ca_int    = cf->cf_int;

	/* Disallow wildcarded i/o address. */
	if (caa->ca_iobase == -1)
		return (0);

	if (bus_space_map(iot, caa->ca_iobase, PCIC_IOSIZE, 0, &ioh))
		return (0);

	if (caa->ca_msize == -1)
		msize = PCIC_MEMSIZE;
	if (bus_space_map(memt, caa->ca_maddr, msize, 0, &memh))
		return (0);

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
	caa->ca_iosize = PCIC_IOSIZE;
	caa->ca_msize = msize;
	return (1);
}

void
pcic_cbus_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct pcic_softc *sc = (void *)self;
	struct pcic_handle *h;
	struct cbus_attach_args *caa = aux;
	bus_space_tag_t iot = &pcic_cbus_io_bst;
	bus_space_tag_t memt = &pcic_cbus_mem_bst;
	bus_space_handle_t ioh;
	bus_space_handle_t memh;
	int intlevel, irq, i, reg;

	SET_TAG_LITTLE_ENDIAN(iot);
	SET_TAG_LITTLE_ENDIAN(memt);

	/* Map i/o space. */
	if (bus_space_map(iot, caa->ca_iobase, caa->ca_iosize, 0, &ioh)) {
		printf(": can't map i/o space\n");
		return;
	}

	/* Map mem space. */
	if (bus_space_map(memt, caa->ca_maddr, caa->ca_msize, 0, &memh)) {
		printf(": can't map mem space\n");
		return;
	}

	sc->membase = caa->ca_maddr;
	sc->subregionmask = (1 << (caa->ca_msize / PCIC_MEM_PAGESIZE)) - 1;

	sc->intr_est = NULL;	/* not used on luna88k */
	sc->pct = (pcmcia_chipset_tag_t)&pcic_cbus_functions;

	sc->iot = iot;
	sc->ioh = ioh;
	sc->memt = memt;
	sc->memh = memh;

	printf("\n");

	pcic_attach(sc);

	sc->ranges = pcic_cbus_addr;
	sc->iobase = 0x0000;
	sc->iosize = 0x1000;
	DPRINTF(("%s: bus_space_alloc range 0x%04lx-0x%04lx\n",
	    sc->dev.dv_xname, (long) sc->iobase,
	    (long) sc->iobase + sc->iosize));

	pcic_attach_sockets(sc);

	/*
	 * Allocate an INT.  It will be used by both controllers.  We could
	 * use two different interrupts, but interrupts are relatively
	 * scarce, shareable, and for PCIC controllers, very infrequent.
	 */
	intlevel = pcic_cbus_intlevel_find();
	if (intlevel == -1) {
		printf("pcic_cbus_attach: no free int found\n");
		return;
	}

	irq = pcic_cbus_int2irq[intlevel];
	cbus_isrlink(pcic_intr, sc, intlevel, IPL_TTY, sc->dev.dv_xname);
	sc->ih = (void *)pcic_intr;
	sc->irq = irq;

	if (irq) {
		printf("%s: int %d (irq %d), ", sc->dev.dv_xname,
		    intlevel, irq);

		/* Set up the pcic to interrupt on card detect. */
		for (i = 0; i < PCIC_NSLOTS; i++) {
			h = &sc->handle[i];
			if (h->flags & PCIC_FLAG_SOCKETP) {
				/* set 'pulse management interrupt' mode */
				reg = pcic_read(h, PCIC_CIRRUS_MISC_CTL_1);
				reg |= PCIC_CIRRUS_MISC_CTL_1_PULSE_MGMT_INTR;
				pcic_write(h, PCIC_CIRRUS_MISC_CTL_1, reg);

				pcic_write(h, PCIC_CSC_INTR,
				    (sc->irq << PCIC_CSC_INTR_IRQ_SHIFT) |
				    PCIC_CSC_INTR_CD_ENABLE);
			}
		}
	} else
		printf("%s: no int, ", sc->dev.dv_xname);

	printf("polling enabled\n");
	if (sc->poll_established == 0) {
		timeout_set(&sc->poll_timeout, pcic_poll_intr, sc);
		timeout_add_msec(&sc->poll_timeout, 500);
		sc->poll_established = 1;
	}
}

void *
pcic_cbus_chip_intr_establish(pcmcia_chipset_handle_t pch,
	struct pcmcia_function *pf, int ipl, int (*fcl)(void *),
	void *arg, char *xname)
{
	struct pcic_handle *h = (struct pcic_handle *)pch;
#ifdef PCICCBUSDEBUG
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);
#endif
	int intlevel, irq, reg;

#ifdef PCICCBUSDEBUG
	char buf[16];
	if (pf->cfe->flags & PCMCIA_CFE_IRQLEVEL)
		strlcpy(buf, "LEVEL", sizeof(buf));
	else if (pf->cfe->flags & PCMCIA_CFE_IRQPULSE)
		strlcpy(buf, "PULSE", sizeof(buf));
	else
		strlcpy(buf, "EDGE", sizeof(buf));
	printf("pcic_cbus_chip_intr_establish: IST_%s\n", buf);
#endif

	/*
	 * If the PC Card has level-triggered interrupt property,
	 * we use CL-PD67XX 'pulse IRQ' feature.
	 */
	if (pf->cfe->flags & PCMCIA_CFE_IRQLEVEL) {
		reg = pcic_read(h, PCIC_CIRRUS_MISC_CTL_1);
		reg |= PCIC_CIRRUS_MISC_CTL_1_PULSE_SYS_IRQ;
		pcic_write(h, PCIC_CIRRUS_MISC_CTL_1, reg);
	}

	intlevel = pcic_cbus_intlevel_find();

	if (intlevel == -1) {
		printf("pcic_cbus_chip_intr_establish: no int found\n");
		return (NULL);
	}

	irq = pcic_cbus_int2irq[intlevel];
	h->ih_irq = irq;

	DPRINTF(("%s: pcic_cbus_chip_intr_establish int %d (irq %d)\n",
	    sc->dev.dv_xname, intlevel, h->ih_irq));

	cbus_isrlink(fcl, arg, intlevel, ipl, h->pcmcia->dv_xname);

	reg = pcic_read(h, PCIC_INTR);
	reg &= ~(PCIC_INTR_IRQ_MASK | PCIC_INTR_ENABLE);
	pcic_write(h, PCIC_INTR, reg | irq);

	return (void *)fcl;
}

void
pcic_cbus_chip_intr_disestablish(pcmcia_chipset_handle_t pch, void *ih)
{
	struct pcic_handle *h = (struct pcic_handle *)pch;
#ifdef PCICCBUSDEBUG
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);
#endif
	int intlevel, reg;

	intlevel = pcic_cbus_irq2int[h->ih_irq];

	DPRINTF(("%s: pcic_cbus_chip_intr_disestablish int %d (irq %d)\n",
	    sc->dev.dv_xname, intlevel, h->ih_irq));

	if (intlevel == -1) {
		printf("pcic_cbus_chip_intr_disestablish: "
		    "strange int (irq = %d)\n", h->ih_irq);
		return;
	}

	h->ih_irq = 0;

	reg = pcic_read(h, PCIC_INTR);
	reg &= ~(PCIC_INTR_IRQ_MASK | PCIC_INTR_ENABLE);
	pcic_write(h, PCIC_INTR, reg);

	cbus_isrunlink(ih, intlevel);

	/* reset the 'pulse IRQ' mode */
	reg = pcic_read(h, PCIC_CIRRUS_MISC_CTL_1);
	reg &= ~PCIC_CIRRUS_MISC_CTL_1_PULSE_SYS_IRQ;
	pcic_write(h, PCIC_CIRRUS_MISC_CTL_1, reg);
}

const char *
pcic_cbus_chip_intr_string(pcmcia_chipset_handle_t pch, void *ih)
{
	struct pcic_handle *h = (struct pcic_handle *)pch;
	static char irqstr[64];

	if (ih == NULL)
		snprintf(irqstr, sizeof(irqstr),
		    "couldn't establish interrupt");
	else
		snprintf(irqstr, sizeof(irqstr), "int %d (irq %d)",
		    pcic_cbus_irq2int[h->ih_irq], h->ih_irq);
	return(irqstr);
}

/*
 * Find a free and pcic-compliant INT level; searching from highest
 * (=small number) to lowest.
 */
int
pcic_cbus_intlevel_find(void)
{
	int intlevel, irq;
	u_int8_t cbus_not_used = ~cbus_intr_registered();

	for (intlevel = 0; intlevel < NCBUSISR; intlevel++)
		if (cbus_not_used & (1 << (6 - intlevel))) {
			irq = pcic_cbus_int2irq[intlevel];
			if ((1 << irq) & PCIC_INTR_IRQ_VALIDMASK)
				break;
		}

	if (intlevel == NCBUSISR)
		intlevel = -1;	/* not found */

	return intlevel;
}

/*
 * LUNA specific pcic_cbus_chip_io_{alloc,free}
 */
int
pcic_cbus_chip_io_alloc(pcmcia_chipset_handle_t pch, bus_addr_t start,
    bus_size_t size, bus_size_t align, struct pcmcia_io_handle *pcihp)
{
	struct pcic_handle *h = (struct pcic_handle *) pch;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_addr_t ioaddr, beg, fin;
	int flags = 0;
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);
	struct pcic_ranges *range;

	/*
	 * Allocate some arbitrary I/O space.
	 */

	iot = sc->iot;

	if (start) {
		ioaddr = start;
		if (bus_space_map(iot, start, size, 0, &ioh))
			return (1);
		DPRINTF(("pcic_cbus_chip_io_alloc map port %lx+%lx\n",
		    (u_long)ioaddr, (u_long)size));
	} else if (sc->ranges) {
		/*
		 * In this case, we know the "size" and "align" that
		 * we want.  So we need to start walking down
		 * sc->ranges, searching for a similar space that
		 * is (1) large enough for the size and alignment
		 * (2) then we need to try to allocate
		 * (3) if it fails to allocate, we try next range.
		 *
		 * We must also check that the start/size of each
		 * allocation we are about to do is within the bounds
		 * of "sc->iobase" and "sc->iosize".
		 * (Some pcmcia controllers handle a 12 bits of addressing,
		 * but we want to use the same range structure)
		 */
		for (range = sc->ranges; range->start; range++) {
			/* Potentially trim the range because of bounds. */
			beg = max(range->start, sc->iobase);
			fin = min(range->start + range->len,
			    sc->iobase + sc->iosize);

			/* Short-circuit easy cases. */
			if (fin < beg || fin - beg < size)
				continue;

			DPRINTF(("pcic_cbus_chip_io_alloc beg-fin %lx-%lx\n",
			    (u_long)beg, (u_long)fin));
			if (bus_space_map(iot, beg, size, 0, &ioh) == 0) {
				ioaddr = beg;
				break;
			}
		}
		if (range->start == 0)
			return (1);
		DPRINTF(("pcic_cbus_chip_io_alloc alloc port %lx+%lx\n",
		    (u_long)ioaddr, (u_long)size));
	} else {
		if (bus_space_map(iot, sc->iobase, size, 0, &ioh))
			return (1);
		ioaddr = sc->iobase;
		DPRINTF(("pcic_cbus_chip_io_alloc alloc port %lx+%lx\n",
		    (u_long)ioaddr, (u_long)size));
	}

	pcihp->iot = iot;
	pcihp->ioh = ioh;
	pcihp->addr = ioaddr;
	pcihp->size = size;
	pcihp->flags = flags;

	return (0);
}

void
pcic_cbus_chip_io_free(pcmcia_chipset_handle_t pch,
    struct pcmcia_io_handle *pcihp)
{
	bus_space_tag_t iot = pcihp->iot;
	bus_space_handle_t ioh = pcihp->ioh;
	bus_size_t size = pcihp->size;

	bus_space_unmap(iot, ioh, size);
}
