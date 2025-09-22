/*	$OpenBSD: rccosb4.c,v 1.7 2023/01/30 10:49:05 jsg Exp $	*/

/*
 * Copyright (c) 2004,2005 Michael Shalayeff
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Support for RCC South Bridge interrupt controller
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/pci/pcivar.h>

#include <i386/pci/pcibiosvar.h>
#include <i386/pci/piixvar.h>
#include <i386/pci/rccosb4reg.h>

struct osb4_handle {
	struct piix_handle piix;

#define	osb4_iot	piix.ph_iot
	bus_space_handle_t osb4_ioh;
};

int	osb4_getclink(pciintr_icu_handle_t, int, int *);
int	osb4_get_intr(pciintr_icu_handle_t, int, int *);
int	osb4_set_intr(pciintr_icu_handle_t, int, int);

const struct pciintr_icu osb4_pci_icu = {
	osb4_getclink,
	osb4_get_intr,
	osb4_set_intr,
	piix_get_trigger,
	piix_set_trigger,
};

int
osb4_init(pci_chipset_tag_t pc, bus_space_tag_t iot, pcitag_t tag,
    pciintr_icu_tag_t *ptagp, pciintr_icu_handle_t *phandp)
{
	struct osb4_handle *ph;

	ph = malloc(sizeof(*ph), M_DEVBUF, M_NOWAIT);
	if (ph == NULL)
		return (1);

	ph->piix.ph_iot = iot;
	ph->piix.ph_pc = pc;
	ph->piix.ph_tag = tag;

	if (bus_space_map(iot, OSB4_PIAIR, 2, 0, &ph->osb4_ioh)) {
		free(ph, M_DEVBUF, sizeof *ph);
		return (1);
	}

	if (bus_space_map(iot, OSB4_REG_ELCR, 2, 0, &ph->piix.ph_elcr_ioh)) {
		free(ph, M_DEVBUF, sizeof *ph);
		return (1);
	}

	*ptagp = &osb4_pci_icu;
	*phandp = ph;
	return (0);
}

int
osb4_getclink(pciintr_icu_handle_t v, int link, int *clinkp)
{
	if (OSB4_LEGAL_LINK(link - 1)) {
		*clinkp = link;
 		if (link <= OSB4_PISP)
			*clinkp |= PCI_INT_VIA_ISA;
		return (0);
	}

	return (1);
}

int
osb4_get_intr(pciintr_icu_handle_t v, int clink, int *irqp)
{
	struct osb4_handle *ph = v;
	int link = clink & 0xff;

	if (!OSB4_LEGAL_LINK(link))
		return (1);

	bus_space_write_1(ph->osb4_iot, ph->osb4_ioh, 0, link);
	*irqp = bus_space_read_1(ph->osb4_iot, ph->osb4_ioh, 1) & 0xf;
	if (*irqp == 0)
		*irqp = I386_PCI_INTERRUPT_LINE_NO_CONNECTION;

	return (0);
}

int
osb4_set_intr(pciintr_icu_handle_t v, int clink, int irq)
{
	struct osb4_handle *ph = v;
	int link = clink & 0xff;

	if (!OSB4_LEGAL_LINK(link) || !OSB4_LEGAL_IRQ(irq & 0xf))
		return (1);

	bus_space_write_1(ph->osb4_iot, ph->osb4_ioh, 0, link);
	bus_space_write_1(ph->osb4_iot, ph->osb4_ioh, 1, irq & 0xf);

	return (0);
}
