/*	$OpenBSD: via82c586.c,v 1.12 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: via82c586.c,v 1.2 2000/07/18 11:24:09 soda Exp $	*/

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
 * Support for the VIA 82c586 PCI-ISA bridge interrupt controller.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/pci/pcivar.h>

#include <i386/pci/pcibiosvar.h>
#include <i386/pci/via82c586reg.h>
#include <i386/pci/piixvar.h>

int	via82c586_getclink(pciintr_icu_handle_t, int, int *);
int	via82c586_get_intr(pciintr_icu_handle_t, int, int *);
int	via82c586_set_intr(pciintr_icu_handle_t, int, int);
int	via82c586_get_trigger(pciintr_icu_handle_t, int, int *);
int	via82c586_set_trigger(pciintr_icu_handle_t, int, int);

const struct pciintr_icu via82c586_pci_icu = {
	via82c586_getclink,
	via82c586_get_intr,
	via82c586_set_intr,
	via82c586_get_trigger,
	via82c586_set_trigger,
};

const int vp3_cfg_trigger_shift[] = {
	VP3_CFG_TRIGGER_SHIFT_PIRQA,
	VP3_CFG_TRIGGER_SHIFT_PIRQB,
	VP3_CFG_TRIGGER_SHIFT_PIRQC,
	VP3_CFG_TRIGGER_SHIFT_PIRQD,
};

#define	VP3_TRIGGER(reg, pirq)	(((reg) >> vp3_cfg_trigger_shift[(pirq)]) & \
				 VP3_CFG_TRIGGER_MASK)

const int vp3_cfg_intr_shift[] = {
	VP3_CFG_INTR_SHIFT_PIRQA,
	VP3_CFG_INTR_SHIFT_PIRQB,
	VP3_CFG_INTR_SHIFT_PIRQC,
	VP3_CFG_INTR_SHIFT_PIRQD,
	VP3_CFG_INTR_SHIFT_PIRQ0,
	VP3_CFG_INTR_SHIFT_PIRQ1,
	VP3_CFG_INTR_SHIFT_PIRQ2,
};

#define	VP3_PIRQ(reg, pirq)	(((reg) >> vp3_cfg_intr_shift[(pirq)]) & \
				 VP3_CFG_INTR_MASK)

int
via82c586_init(pci_chipset_tag_t pc, bus_space_tag_t iot, pcitag_t tag,
    pciintr_icu_tag_t *ptagp, pciintr_icu_handle_t *phandp)
{
	pcireg_t reg;

	if (piix_init(pc, iot, tag, ptagp, phandp) == 0) {
		*ptagp = &via82c586_pci_icu;
		
		/*
		 * Enable EISA ELCR.
		 */
		reg = pci_conf_read(pc, tag, VP3_CFG_KBDMISCCTRL12_REG);
		reg |= VP3_CFG_MISCCTRL2_EISA4D04D1PORT_ENABLE <<
		    VP3_CFG_MISCCTRL2_SHIFT;
		pci_conf_write(pc, tag, VP3_CFG_KBDMISCCTRL12_REG, reg);

		return (0);
	}

	return (1);
}

int
via82c586_getclink(pciintr_icu_handle_t v, int link, int *clinkp)
{

	if (VP3_LEGAL_LINK(link - 1)) {
		*clinkp = link - 1;
		return (0);
	}

	return (1);
}

int
via82c586_get_intr(pciintr_icu_handle_t v, int clink, int *irqp)
{
	struct piix_handle *ph = v;
	pcireg_t reg;
	int val;

	if (VP3_LEGAL_LINK(clink) == 0)
		return (1);

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, VP3_CFG_PIRQ_REG);
	val = VP3_PIRQ(reg, clink);
	*irqp = (val == VP3_PIRQ_NONE)?
	    I386_PCI_INTERRUPT_LINE_NO_CONNECTION : val;

	return (0);
}

int
via82c586_set_intr(pciintr_icu_handle_t v, int clink, int irq)
{
	struct piix_handle *ph = v;
	int shift, val;
	pcireg_t reg;

	if (VP3_LEGAL_LINK(clink) == 0 || VP3_LEGAL_IRQ(irq) == 0)
		return (1);

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, VP3_CFG_PIRQ_REG);
	via82c586_get_intr(v, clink, &val);
	shift = vp3_cfg_intr_shift[clink];
	reg &= ~(VP3_CFG_INTR_MASK << shift);
	reg |= (irq << shift);
	pci_conf_write(ph->ph_pc, ph->ph_tag, VP3_CFG_PIRQ_REG, reg);
	if (via82c586_get_intr(v, clink, &val) != 0 ||
	    val != irq)
		return (1);

	return (0);
}

int
via82c586_get_trigger(pciintr_icu_handle_t v, int irq, int *triggerp)
{
	struct piix_handle *ph = v;
	int i, error, check_consistency, pciirq, pcitrigger = IST_NONE;
	pcireg_t reg;

	if (VP3_LEGAL_IRQ(irq) == 0)
		return (1);

	check_consistency = 0;
	for (i = 0; i <= 3; i++) {
		via82c586_get_intr(v, i, &pciirq);
		if (pciirq == irq) {
			reg = pci_conf_read(ph->ph_pc, ph->ph_tag,
			    VP3_CFG_PIRQ_REG);
			if (VP3_TRIGGER(reg, i) == VP3_CFG_TRIGGER_EDGE)
				pcitrigger = IST_EDGE;
			else
				pcitrigger = IST_LEVEL;
			check_consistency = 1;
			break;
		}
	}

	error = piix_get_trigger(v, irq, triggerp);
	if (error == 0 && check_consistency && pcitrigger != *triggerp)
		return (1);
	return (error);
}

int
via82c586_set_trigger(pciintr_icu_handle_t v, int irq, int trigger)
{
	struct piix_handle *ph = v;
	int i, pciirq, shift, testtrig;
	pcireg_t reg;

	if (VP3_LEGAL_IRQ(irq) == 0)
		return (1);

	for (i = 0; i <= 3; i++) {
		via82c586_get_intr(v, i, &pciirq);
		if (pciirq == irq) {
			reg = pci_conf_read(ph->ph_pc, ph->ph_tag,
			    VP3_CFG_PIRQ_REG);
			shift = vp3_cfg_trigger_shift[i];
			/* XXX we only upgrade the trigger here */
			if (trigger == IST_LEVEL)
				reg &= ~(VP3_CFG_TRIGGER_MASK << shift);
			pci_conf_write(ph->ph_pc, ph->ph_tag,
			    VP3_CFG_PIRQ_REG, reg);
			break;
		}
	}

	if (piix_set_trigger(v, irq, trigger) != 0 ||
	    via82c586_get_trigger(v, irq, &testtrig) != 0 ||
	    testtrig != trigger)
		return (1);

	return (0);
}
