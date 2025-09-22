/*	$OpenBSD: opti82c558.c,v 1.9 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: opti82c558.c,v 1.2 2000/07/18 11:24:09 soda Exp $	*/

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
 * Support for the Opti 82c558 PCI-ISA bridge interrupt controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/pci/pcivar.h>

#include <i386/pci/pcibiosvar.h>
#include <i386/pci/opti82c558reg.h>

int	opti82c558_getclink(pciintr_icu_handle_t, int, int *);
int	opti82c558_get_intr(pciintr_icu_handle_t, int, int *);
int	opti82c558_set_intr(pciintr_icu_handle_t, int, int);
int	opti82c558_get_trigger(pciintr_icu_handle_t, int, int *);
int	opti82c558_set_trigger(pciintr_icu_handle_t, int, int);

const struct pciintr_icu opti82c558_pci_icu = {
	opti82c558_getclink,
	opti82c558_get_intr,
	opti82c558_set_intr,
	opti82c558_get_trigger,
	opti82c558_set_trigger,
};

struct opti82c558_handle {
	pci_chipset_tag_t ph_pc;
	pcitag_t ph_tag;
};

static const int viper_pirq_decode[] = {
	-1, 5, 9, 10, 11, 12, 14, 15
};

static const int viper_pirq_encode[] = {
	-1,		/* 0 */
	-1,		/* 1 */
	-1,		/* 2 */
	-1,		/* 3 */
	-1,		/* 4 */
	VIPER_PIRQ_5,	/* 5 */
	-1,		/* 6 */
	-1,		/* 7 */
	-1,		/* 8 */
	VIPER_PIRQ_9,	/* 9 */
	VIPER_PIRQ_10,	/* 10 */
	VIPER_PIRQ_11,	/* 11 */
	VIPER_PIRQ_12,	/* 12 */
	-1,		/* 13 */
	VIPER_PIRQ_14,	/* 14 */
	VIPER_PIRQ_15,	/* 15 */
};

int
opti82c558_init(pci_chipset_tag_t pc, bus_space_tag_t iot, pcitag_t tag,
    pciintr_icu_tag_t *ptagp, pciintr_icu_handle_t *phandp)
{
	struct opti82c558_handle *ph;

	ph = malloc(sizeof(*ph), M_DEVBUF, M_NOWAIT);
	if (ph == NULL)
		return (1);

	ph->ph_pc = pc;
	ph->ph_tag = tag;

	*ptagp = &opti82c558_pci_icu;
	*phandp = ph;
	return (0);
}

int
opti82c558_getclink(pciintr_icu_handle_t v, int link, int *clinkp)
{

	if (VIPER_LEGAL_LINK(link - 1)) {
		*clinkp = link - 1;
		return (0);
	}

	return (1);
}

int
opti82c558_get_intr(pciintr_icu_handle_t v, int clink, int *irqp)
{
	struct opti82c558_handle *ph = v;
	pcireg_t reg;
	int val;

	if (VIPER_LEGAL_LINK(clink) == 0)
		return (1);

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, VIPER_CFG_PIRQ);
	val = VIPER_PIRQ(reg, clink);
	*irqp = (val == VIPER_PIRQ_NONE) ? 0xff : viper_pirq_decode[val];

	return (0);
}

int
opti82c558_set_intr(pciintr_icu_handle_t v, int clink, int irq)
{
	struct opti82c558_handle *ph = v;
	int shift;
	pcireg_t reg;

	if (VIPER_LEGAL_LINK(clink) == 0 || VIPER_LEGAL_IRQ(irq) == 0)
		return (1);

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, VIPER_CFG_PIRQ);
	shift = VIPER_PIRQ_SELECT_SHIFT * clink;
	reg &= ~(VIPER_PIRQ_SELECT_MASK << shift);
	reg |= (viper_pirq_encode[irq] << shift);
	pci_conf_write(ph->ph_pc, ph->ph_tag, VIPER_CFG_PIRQ, reg);

	return (0);
}

int
opti82c558_get_trigger(pciintr_icu_handle_t v, int irq, int *triggerp)
{
	struct opti82c558_handle *ph = v;
	pcireg_t reg;

	if (VIPER_LEGAL_IRQ(irq) == 0) {
		/* ISA IRQ? */
		*triggerp = IST_EDGE;
		return (0);
	}

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, VIPER_CFG_PIRQ);
	if ((reg >> (VIPER_CFG_TRIGGER_SHIFT + viper_pirq_encode[irq])) & 1)
		*triggerp = IST_LEVEL;
	else
		*triggerp = IST_EDGE;

	return (0);
}

int
opti82c558_set_trigger(pciintr_icu_handle_t v, int irq, int trigger)
{
	struct opti82c558_handle *ph = v;
	int shift;
	pcireg_t reg;

	if (VIPER_LEGAL_IRQ(irq) == 0) {
		/* ISA IRQ? */
		return ((trigger != IST_LEVEL) ? 0 : 1);
	}

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, VIPER_CFG_PIRQ);
	shift = (VIPER_CFG_TRIGGER_SHIFT + viper_pirq_encode[irq]);
	if (trigger == IST_LEVEL)
		reg |= (1 << shift);
	else
		reg &= ~(1 << shift);
	pci_conf_write(ph->ph_pc, ph->ph_tag, VIPER_CFG_PIRQ, reg);

	return (0);
}
