/*	$OpenBSD: piix.c,v 1.12 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: piix.c,v 1.1 1999/11/17 01:21:20 thorpej Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

/*
 * Copyright (c) 1999, by UCHIYAMA Yasushi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

/*
 * Support for the Intel PIIX PCI-ISA bridge interrupt controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/pci/pcivar.h>

#include <i386/pci/pcibiosvar.h>
#include <i386/pci/piixreg.h>
#include <i386/pci/piixvar.h>

#ifdef PIIX_DEBUG
#define	DPRINTF(arg) printf arg
#else
#define	DPRINTF(arg)
#endif

int	piix_getclink(pciintr_icu_handle_t, int, int *);
int	piix_get_intr(pciintr_icu_handle_t, int, int *);
int	piix_set_intr(pciintr_icu_handle_t, int, int);
#ifdef PIIX_DEBUG
void	piix_pir_dump(struct piix_handle *);
#endif

const struct pciintr_icu piix_pci_icu = {
	piix_getclink,
	piix_get_intr,
	piix_set_intr,
	piix_get_trigger,
	piix_set_trigger,
};

int
piix_init(pci_chipset_tag_t pc, bus_space_tag_t iot, pcitag_t tag,
    pciintr_icu_tag_t *ptagp, pciintr_icu_handle_t *phandp)
{
	struct piix_handle *ph;

	ph = malloc(sizeof(*ph), M_DEVBUF, M_NOWAIT);
	if (ph == NULL)
		return (1);

	ph->ph_iot = iot;
	ph->ph_pc = pc;
	ph->ph_tag = tag;

	if (bus_space_map(iot, PIIX_REG_ELCR, PIIX_REG_ELCR_SIZE, 0,
	    &ph->ph_elcr_ioh) != 0) {
		free(ph, M_DEVBUF, sizeof *ph);
		return (1);
	}

#ifdef PIIX_DEBUG
	piix_pir_dump(ph);
#endif
	*ptagp = &piix_pci_icu;
	*phandp = ph;
	return (0);
}

int
piix_getclink(pciintr_icu_handle_t v, int link, int *clinkp)
{
	DPRINTF(("PIIX link value 0x%x: ", link));

	/* Pattern 1: simple. */
	if (PIIX_LEGAL_LINK(link - 1)) {
		*clinkp = link - 1;
		DPRINTF(("PIRQ %d (simple)\n", *clinkp));
		return (0);
	}

	/* Pattern 2: configuration register offset */
	if (link >= 0x60 && link <= 0x63) {
		*clinkp = link - 0x60;
		DPRINTF(("PIRQ %d (register offset)\n", *clinkp));
		return (0);
	}

	/* Pattern 3: configuration register offset, PIRQE# - PIRQH# */
	if (link >= 0x68 && link <= 0x6b) {
		*clinkp = link - 0x64;
		DPRINTF(("PIRQ %d (high register offset)\n", *clinkp));
		return (0);
	}

	DPRINTF(("bogus IRQ selection source\n"));
	return (1);
}

int
piix_get_intr(pciintr_icu_handle_t v, int clink, int *irqp)
{
	struct piix_handle *ph = v;
	int shift, off;
	pcireg_t reg;

	if (PIIX_LEGAL_LINK(clink) == 0)
		return (1);

	off = PIIX_CFG_PIRQ;
	if (clink > 3) {
		off += 8;
		clink -= 4;
	}

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, off);
	shift = clink << 3;
	if ((reg >> shift) & PIIX_CFG_PIRQ_NONE)
		*irqp = I386_PCI_INTERRUPT_LINE_NO_CONNECTION;
	else
		*irqp = PIIX_PIRQ(reg, clink);

	return (0);
}

int
piix_set_intr(pciintr_icu_handle_t v, int clink, int irq)
{
	struct piix_handle *ph = v;
	int shift, off;
	pcireg_t reg;

	if (PIIX_LEGAL_LINK(clink) == 0 || PIIX_LEGAL_IRQ(irq) == 0)
		return (1);

	off = PIIX_CFG_PIRQ;
	if (clink > 3) {
		off += 8;
		clink -= 4;
	}

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, off);
	shift = clink << 3;
	reg &= ~((PIIX_CFG_PIRQ_NONE | PIIX_CFG_PIRQ_MASK) << shift);
	reg |= irq << shift;
	pci_conf_write(ph->ph_pc, ph->ph_tag, off, reg);

	return (0);
}

int
piix_get_trigger(pciintr_icu_handle_t v, int irq, int *triggerp)
{
	struct piix_handle *ph = v;
	int off, bit;
	u_int8_t elcr;

	if (PIIX_LEGAL_IRQ(irq) == 0)
		return (1);

	off = (irq > 7) ? 1 : 0;
	bit = irq & 7;

	elcr = bus_space_read_1(ph->ph_iot, ph->ph_elcr_ioh, off);
	if (elcr & (1 << bit))
		*triggerp = IST_LEVEL;
	else
		*triggerp = IST_EDGE;

	return (0);
}

int
piix_set_trigger(pciintr_icu_handle_t v, int irq, int trigger)
{
	struct piix_handle *ph = v;
	int off, bit;
	u_int8_t elcr;

	if (PIIX_LEGAL_IRQ(irq) == 0)
		return (1);

	off = (irq > 7) ? 1 : 0;
	bit = irq & 7;

	elcr = bus_space_read_1(ph->ph_iot, ph->ph_elcr_ioh, off);
	if (trigger == IST_LEVEL)
		elcr |= (1 << bit);
	else
		elcr &= ~(1 << bit);
	bus_space_write_1(ph->ph_iot, ph->ph_elcr_ioh, off, elcr);

	return (0);
}

#ifdef PIIX_DEBUG
void
piix_pir_dump(struct piix_handle *ph)
{
	int i, irq;
	pcireg_t irqs = pci_conf_read(ph->ph_pc, ph->ph_tag, PIIX_CFG_PIRQ);
	u_int8_t elcr[2];

	elcr[0] = bus_space_read_1(ph->ph_iot, ph->ph_elcr_ioh, 0);
	elcr[1] = bus_space_read_1(ph->ph_iot, ph->ph_elcr_ioh, 1);

	for (i = 0; i < 8; i++) {
		if (i == 4)
			irqs = pci_conf_read(ph->ph_pc, ph->ph_tag,
			    PIIX_CFG_PIRQH);

		irq = PIIX_PIRQ(irqs, i);
		if (irq & PIIX_CFG_PIRQ_NONE)
			printf("PIIX PIRQ %d: irq none (0x%x)\n", i, irq);
		else
			printf("PIIX PIRQ %d: irq %d\n", i, irq);
	}

	printf("PIIX irq:");
	for (i = 0; i < 16; i++)
		printf(" %2d", i);
	printf("\n");
	printf(" trigger:");
	for (i = 0; i < 16; i++)
		printf("  %c", (elcr[(i & 8) ? 1 : 0] & (1 << (i & 7))) ?
		       'L' : 'E');
	printf("\n");
}
#endif /* PIIX_DEBUG */
