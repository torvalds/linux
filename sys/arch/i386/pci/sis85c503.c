/*	$OpenBSD: sis85c503.c,v 1.9 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: sis85c503.c,v 1.2 2000/07/18 11:24:09 soda Exp $	*/

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
 * Support for the SiS 85c503 PCI-ISA bridge interrupt controller.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/pci/pcivar.h>

#include <i386/pci/pcibiosvar.h>
#include <i386/pci/sis85c503reg.h>
#include <i386/pci/piixvar.h>

int	sis85c503_getclink(pciintr_icu_handle_t, int, int *);
int	sis85c503_get_intr(pciintr_icu_handle_t, int, int *);
int	sis85c503_set_intr(pciintr_icu_handle_t, int, int);

const struct pciintr_icu sis85c503_pci_icu = {
	sis85c503_getclink,
	sis85c503_get_intr,
	sis85c503_set_intr,
	piix_get_trigger,
	piix_set_trigger,
};

int
sis85c503_init(pci_chipset_tag_t pc, bus_space_tag_t iot, pcitag_t tag,
    pciintr_icu_tag_t *ptagp, pciintr_icu_handle_t *phandp)
{

	if (piix_init(pc, iot, tag, ptagp, phandp) == 0) {
		*ptagp = &sis85c503_pci_icu;
		return (0);
	}

	return (1);
}

int
sis85c503_getclink(pciintr_icu_handle_t v, int link, int *clinkp)
{

	/* Pattern 1: simple. */
	if (link >= 1 && link <= 4) {
		*clinkp = SIS85C503_CFG_PIRQ_REGSTART + link - 1;
		return (0);
	}

	/* Pattern 2: configuration register offset */
	if (link >= SIS85C503_CFG_PIRQ_REGSTART &&
	    link <= SIS85C503_CFG_PIRQ_REGEND) {
		*clinkp = link;
		return (0);
	}

	return (1);
}

int
sis85c503_get_intr(pciintr_icu_handle_t v, int clink, int *irqp)
{
	struct piix_handle *ph = v;
	pcireg_t reg;

	if (SIS85C503_LEGAL_LINK(clink) == 0)
		return (1);

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag,
	    SIS85C503_CFG_PIRQ_REGOFS(clink));
	reg = SIS85C503_CFG_PIRQ_REG(reg, clink);

	if (reg & SIS85C503_CFG_PIRQ_ROUTE_DISABLE)
		*irqp = 0xff;
	else
		*irqp = reg & SIS85C503_CFG_PIRQ_INTR_MASK;

	return (0);
}

int
sis85c503_set_intr(pciintr_icu_handle_t v, int clink, int irq)
{
	struct piix_handle *ph = v;
	int shift;
	pcireg_t reg;

	if (SIS85C503_LEGAL_LINK(clink) == 0 || SIS85C503_LEGAL_IRQ(irq) == 0)
		return (1);

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag,
	    SIS85C503_CFG_PIRQ_REGOFS(clink));
	shift = SIS85C503_CFG_PIRQ_SHIFT(clink);
	reg &= ~((SIS85C503_CFG_PIRQ_ROUTE_DISABLE |
	    SIS85C503_CFG_PIRQ_INTR_MASK) << shift);
	reg |= (irq << shift);
	pci_conf_write(ph->ph_pc, ph->ph_tag, SIS85C503_CFG_PIRQ_REGOFS(clink),
	    reg);

	return (0);
}
