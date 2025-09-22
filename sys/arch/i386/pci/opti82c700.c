/*	$OpenBSD: opti82c700.c,v 1.9 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: opti82c700.c,v 1.2 2000/07/18 11:07:20 soda Exp $	*/

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
 * Support for the Opti 82c700 FireStar PCI-ISA bridge interrupt controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/pci/pcivar.h>

#include <i386/pci/pcibiosvar.h>
#include <i386/pci/opti82c700reg.h>

#ifdef FIRESTARDEBUG
#define	DPRINTF(arg)	printf arg
#else
#define	DPRINTF(arg)
#endif

int	opti82c700_getclink(pciintr_icu_handle_t, int, int *);
int	opti82c700_get_intr(pciintr_icu_handle_t, int, int *);
int	opti82c700_set_intr(pciintr_icu_handle_t, int, int);
int	opti82c700_get_trigger(pciintr_icu_handle_t, int, int *);
int	opti82c700_set_trigger(pciintr_icu_handle_t, int, int);

const struct pciintr_icu opti82c700_pci_icu = {
	opti82c700_getclink,
	opti82c700_get_intr,
	opti82c700_set_intr,
	opti82c700_get_trigger,
	opti82c700_set_trigger,
};

struct opti82c700_handle {
	pci_chipset_tag_t ph_pc;
	pcitag_t ph_tag;
};

int	opti82c700_addr(int, int *, int *);
#ifdef FIRESTARDEBUG
void	opti82c700_pir_dump(struct opti82c700_handle *);
#endif

int
opti82c700_init(pci_chipset_tag_t pc, bus_space_tag_t iot, pcitag_t tag,
    pciintr_icu_tag_t *ptagp, pciintr_icu_handle_t *phandp)
{
	struct opti82c700_handle *ph;

	ph = malloc(sizeof(*ph), M_DEVBUF, M_NOWAIT);
	if (ph == NULL)
		return (1);

	ph->ph_pc = pc;
	ph->ph_tag = tag;
#ifdef FIRESTARDEBUG
	opti82c700_pir_dump(ph);
#endif
	*ptagp = &opti82c700_pci_icu;
	*phandp = ph;
	return (0);
}

int
opti82c700_addr(int link, int *addrofs, int *ofs)
{
	int regofs, src;

	regofs = FIRESTAR_PIR_REGOFS(link);
	src = FIRESTAR_PIR_SELECTSRC(link);

	switch (src) {
	case FIRESTAR_PIR_SELECT_NONE:
		return (1);

	case FIRESTAR_PIR_SELECT_IRQ:
		if (regofs < 0 || regofs > 7)
			return (1);
		*addrofs = FIRESTAR_CFG_INTR_IRQ + (regofs >> 2);
		*ofs = (regofs & 3) << 3;
		break;

	case FIRESTAR_PIR_SELECT_PIRQ:
		/* FALLTHROUGH */
	case FIRESTAR_PIR_SELECT_BRIDGE:
		if (regofs < 0 || regofs > 3)
			return (1);
		*addrofs = FIRESTAR_CFG_INTR_PIRQ;
		*ofs = regofs << 2;
		break;

	default:
		return (1);
	}

	return (0);
}

int
opti82c700_getclink(pciintr_icu_handle_t v, int link, int *clinkp)
{
	DPRINTF(("FireStar link value 0x%x: ", link));

	switch (FIRESTAR_PIR_SELECTSRC(link)) {
	default:
		DPRINTF(("bogus IRQ selection source\n"));
		return (1);
	case FIRESTAR_PIR_SELECT_NONE:
		DPRINTF(("No interrupt connection\n"));
		return (1);
	case FIRESTAR_PIR_SELECT_IRQ:
		DPRINTF(("FireStar IRQ pin"));
		break;
	case FIRESTAR_PIR_SELECT_PIRQ:
		DPRINTF(("FireStar PIO pin or Serial IRQ PIRQ#"));
		break;
	case FIRESTAR_PIR_SELECT_BRIDGE:
		DPRINTF(("FireBridge 1 INTx# pin"));
		break;
	}

	DPRINTF((" REGOFST:%#x\n", FIRESTAR_PIR_REGOFS(link)));
	*clinkp = link;

	return (0);
}

int
opti82c700_get_intr(pciintr_icu_handle_t v, int clink, int *irqp)
{
	struct opti82c700_handle *ph = v;
	pcireg_t reg;
	int val, addrofs, ofs;

	if (opti82c700_addr(clink, &addrofs, &ofs))
		return (1);

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, addrofs);
	val = (reg >> ofs) & FIRESTAR_CFG_PIRQ_MASK;

	*irqp = (val == FIRESTAR_PIRQ_NONE) ?
	    I386_PCI_INTERRUPT_LINE_NO_CONNECTION : val;

	return (0);
}

int
opti82c700_set_intr(pciintr_icu_handle_t v, int clink, int irq)
{
	struct opti82c700_handle *ph = v;
	int addrofs, ofs;
	pcireg_t reg;

	if (FIRESTAR_LEGAL_IRQ(irq) == 0)
		return (1);

	if (opti82c700_addr(clink, &addrofs, &ofs))
		return (1);

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, addrofs);
	reg &= ~(FIRESTAR_CFG_PIRQ_MASK << ofs);
	reg |= (irq << ofs);
	pci_conf_write(ph->ph_pc, ph->ph_tag, addrofs, reg);

	return (0);
}

int
opti82c700_get_trigger(pciintr_icu_handle_t v, int irq, int *triggerp)
{
	struct opti82c700_handle *ph = v;
	int i, val, addrofs, ofs;
	pcireg_t reg;

	if (FIRESTAR_LEGAL_IRQ(irq) == 0) {
		/* ISA IRQ? */
		*triggerp = IST_EDGE;
		return (0);
	}

	/*
	 * Search PCIDV1 registers.
	 */
	for (i = 0; i < 8; i++) {
		opti82c700_addr(FIRESTAR_PIR_MAKELINK(FIRESTAR_PIR_SELECT_IRQ,
		    i), &addrofs, &ofs);
		reg = pci_conf_read(ph->ph_pc, ph->ph_tag, addrofs);
		val = (reg >> ofs) & FIRESTAR_CFG_PIRQ_MASK;
		if (val != irq)
			continue;
		val = ((reg >> ofs) >> FIRESTAR_TRIGGER_SHIFT) &
		    FIRESTAR_TRIGGER_MASK;
		*triggerp = val ? IST_LEVEL : IST_EDGE;
		return (0);
	}

	/*
	 * Search PIO PCIIRQ.
	 */
	for (i = 0; i < 4; i++) {
		opti82c700_addr(FIRESTAR_PIR_MAKELINK(FIRESTAR_PIR_SELECT_PIRQ,
		    i), &addrofs, &ofs);
		reg = pci_conf_read(ph->ph_pc, ph->ph_tag, addrofs);
		val = (reg >> ofs) & FIRESTAR_CFG_PIRQ_MASK;
		if (val != irq)
			continue;
		*triggerp = IST_LEVEL;
		return (0);
	}

	return (1);
}

int
opti82c700_set_trigger(pciintr_icu_handle_t v, int irq, int trigger)
{
	struct opti82c700_handle *ph = v;
	int i, val, addrofs, ofs;
	pcireg_t reg;

	if (FIRESTAR_LEGAL_IRQ(irq) == 0) {
		/* ISA IRQ? */
		return ((trigger != IST_LEVEL) ? 0 : 1);
	}

	/*
	 * Search PCIDV1 registers.
	 */
	for (i = 0; i < 8; i++) {
		opti82c700_addr(FIRESTAR_PIR_MAKELINK(FIRESTAR_PIR_SELECT_IRQ,
		    i), &addrofs, &ofs);
		reg = pci_conf_read(ph->ph_pc, ph->ph_tag, addrofs);
		val = (reg >> ofs) & FIRESTAR_CFG_PIRQ_MASK;
		if (val != irq)
			continue;
		if (trigger == IST_LEVEL)
			reg |= (FIRESTAR_TRIGGER_MASK <<
			    (FIRESTAR_TRIGGER_SHIFT + ofs));
		else
			reg &= ~(FIRESTAR_TRIGGER_MASK <<
			    (FIRESTAR_TRIGGER_SHIFT + ofs));
		pci_conf_write(ph->ph_pc, ph->ph_tag, addrofs, reg);
		return (0);
	}

	/*
	 * Search PIO PCIIRQ.
	 */
	for (i = 0; i < 4; i++) {
		opti82c700_addr(FIRESTAR_PIR_MAKELINK(FIRESTAR_PIR_SELECT_PIRQ,
		    i), &addrofs, &ofs);
		reg = pci_conf_read(ph->ph_pc, ph->ph_tag, addrofs);
		val = (reg >> ofs) & FIRESTAR_CFG_PIRQ_MASK;
		if (val != irq)
			continue;
		return (trigger == IST_LEVEL ? 0 : 1);
	}

	return (1);
}
