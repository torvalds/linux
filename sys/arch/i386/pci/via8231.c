/*	$OpenBSD: via8231.c,v 1.9 2023/01/30 10:49:05 jsg Exp $	*/

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
 * Copyright (c) 2005, by Michael Shalayeff
 * Copyright (c) 2003, by Matthew Gream
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
 * Support for the VIA Technologies Inc. VIA8231 PCI to ISA Bridge
 * Based upon documentation:
 * 1. VIA VT8231 South Bridge, Revision 1.85 (March 11, 2002), pg 73
 * 2. VIA VT8237R South Bridge, Revision 2.06 (December 15, 2004), pg 100
 * Derived from amd756.c
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <i386/pci/pcibiosvar.h>
#include <i386/pci/via8231reg.h>

struct via8231_handle {
	bus_space_tag_t ph_iot;
	bus_space_handle_t ph_regs_ioh;
	pci_chipset_tag_t ph_pc;
	pcitag_t ph_tag;
	int flags;
#define	VT8237	0x0001
};

int via8231_getclink(pciintr_icu_handle_t, int, int *);
int via8231_get_intr(pciintr_icu_handle_t, int, int *);
int via8231_set_intr(pciintr_icu_handle_t, int, int);
int via8231_get_trigger(pciintr_icu_handle_t, int, int *);
int via8231_set_trigger(pciintr_icu_handle_t, int, int);
#ifdef VIA8231_DEBUG
static void via8231_pir_dump(const char*, struct via8231_handle *);
#endif

const struct pciintr_icu via8231_pci_icu = {
	via8231_getclink,
	via8231_get_intr,
	via8231_set_intr,
	via8231_get_trigger,
	via8231_set_trigger,
};

struct mask_shft_pair {
	int mask;
	int shft;
};

static const struct mask_shft_pair via8231_routing_cnfg[VIA8231_LINK_MAX+1] = {
	{ 0x0f,  0+4 }, /*PINTA#*/
	{ 0x0f,  8+0 }, /*PINTB#*/
	{ 0x0f,  8+4 }, /*PINTC#*/
	{ 0x0f, 16+4 }  /*PINTD#*/
};

#define VIA8231_GET_TRIGGER_CNFG(reg, pirq) \
	((reg) & (1 << (3 - (clink & 3))))
#define VIA8231_SET_TRIGGER_CNFG(reg, clink, cfg) \
	(((reg) & ~(1 << (3 - (clink & 3)))) | ((cfg) << (3 - (clink & 3))))

#define VIA8231_GET_ROUTING_CNFG(reg, pirq) \
	(((reg) >> via8231_routing_cnfg[(pirq)].shft) & \
	    via8231_routing_cnfg[(pirq)].mask)

#define VIA8231_SET_ROUTING_CNFG(reg, pirq, cfg) \
	(((reg) & ~(via8231_routing_cnfg[(pirq)].mask << \
	    via8231_routing_cnfg[(pirq)].shft)) | \
	    (((cfg) & via8231_routing_cnfg[(pirq)].mask) << \
	    via8231_routing_cnfg[(pirq)].shft))

int
via8231_init(pci_chipset_tag_t pc, bus_space_tag_t iot, pcitag_t tag,
    pciintr_icu_tag_t *ptagp, pciintr_icu_handle_t *phandp)
{
	struct via8231_handle *ph;
	pcireg_t id;

	ph = malloc(sizeof(*ph), M_DEVBUF, M_NOWAIT);
	if (ph == NULL)
		return (1);

	ph->ph_iot = iot;
	ph->ph_pc = pc;
	ph->ph_tag = tag;
	id = pci_conf_read(pc, tag, PCI_ID_REG);
	ph->flags = PCI_VENDOR(id) == PCI_VENDOR_VIATECH &&
	     PCI_PRODUCT(id) == PCI_PRODUCT_VIATECH_VT8231_ISA? 0 : VT8237;

	*ptagp = &via8231_pci_icu;
	*phandp = ph;

#ifdef VIA8231_DEBUG
	via8231_pir_dump("via8231_init", ph);
#endif

	return 0;
}

int
via8231_getclink(pciintr_icu_handle_t v, int link, int *clinkp)
{
	struct via8231_handle *ph = v;

	if ((ph->flags & VT8237) && !VIA8237_LINK_LEGAL(link - 1))
		return (1);

	if (!(ph->flags & VT8237) && !VIA8231_LINK_LEGAL(link - 1))
		return (1);

	*clinkp = link - 1;
	return (0);
}

int
via8231_get_intr(pciintr_icu_handle_t v, int clink, int *irqp)
{
	struct via8231_handle *ph = v;
	int reg, val;

	if (VIA8237_LINK_LEGAL(clink) == 0)
		return (1);

	if (VIA8231_LINK_LEGAL(clink)) {
		reg = VIA8231_GET_ROUTING(ph);
		val = VIA8231_GET_ROUTING_CNFG(reg, clink);
	} else {
		reg = VIA8237_GET_ROUTING(ph);
		val = (reg >> ((clink & 3) * 4)) & 0xf;
	}

	*irqp = (val == VIA8231_ROUTING_CNFG_DISABLED) ?
	    I386_PCI_INTERRUPT_LINE_NO_CONNECTION : val;

	return (0);
}

int
via8231_set_intr(pciintr_icu_handle_t v, int clink, int irq)
{
	struct via8231_handle *ph = v;
	int reg;

	if (VIA8237_LINK_LEGAL(clink) == 0 || VIA8231_PIRQ_LEGAL(irq) == 0)
		return (1);

#ifdef VIA8231_DEBUG
	printf("via8231_set_intr: link(%02x) --> irq(%02x)\n", clink, irq);
	via8231_pir_dump("via8231_set_intr: ", ph);
#endif

	if (VIA8231_LINK_LEGAL(clink)) {
		reg = VIA8231_GET_ROUTING(ph);
		VIA8231_SET_ROUTING(ph,
		    VIA8231_SET_ROUTING_CNFG(reg, clink, irq));
	} else {
		reg = VIA8237_GET_ROUTING(ph);
		VIA8237_SET_ROUTING(ph, (reg & ~(0xf << (clink & 3))) |
		    ((irq & 0xf) << (clink & 3)));
	}

	return (0);
}

int
via8231_get_trigger(pciintr_icu_handle_t v, int irq, int *triggerp)
{
	struct via8231_handle *ph = v;
	int reg, clink, max, pciirq;

	if (VIA8231_PIRQ_LEGAL(irq) == 0)
		return (1);

	max = ph->flags & VT8237? VIA8237_LINK_MAX : VIA8231_LINK_MAX;
	for (clink = 0; clink <= max; clink++) {
		via8231_get_intr(v, clink, &pciirq);
		if (pciirq == irq) {
			reg = VIA8231_LINK_LEGAL(clink)?
			    VIA8231_GET_TRIGGER(ph):
			    VIA8237_GET_TRIGGER(ph);
			*triggerp = VIA8231_GET_TRIGGER_CNFG(reg, clink)?
			    IST_EDGE : IST_LEVEL;
			return (0);
		}
	}

	return (1);
}

int
via8231_set_trigger(pciintr_icu_handle_t v, int irq, int trigger)
{
	struct via8231_handle *ph = v;
	int reg, clink, max, pciirq;

	if (VIA8231_PIRQ_LEGAL(irq) == 0 || VIA8231_TRIG_LEGAL(trigger) == 0)
		return (1);

#ifdef VIA8231_DEBUG
	printf("via8231_set_trig: irq(%02x) --> trig(%02x)\n", irq, trigger);
	via8231_pir_dump("via8231_set_trig: ", ph);
#endif

	max = ph->flags & VT8237? VIA8237_LINK_MAX : VIA8231_LINK_MAX;
	for (clink = 0; clink <= VIA8231_LINK_MAX; clink++) {
		via8231_get_intr(v, clink, &pciirq);
		if (pciirq == irq) {
			reg = VIA8231_LINK_LEGAL(clink)?
			    VIA8231_GET_TRIGGER(ph):
			    VIA8237_GET_TRIGGER(ph);
			switch (trigger) {
			case IST_LEVEL:
				reg = VIA8231_SET_TRIGGER_CNFG(reg, clink,
					VIA8231_TRIGGER_CNFG_LEVEL);
				break;
			case IST_EDGE:
				reg = VIA8231_SET_TRIGGER_CNFG(reg, clink,
					VIA8231_TRIGGER_CNFG_EDGE);
				break;
			default:
				return (1);
			}
			if (VIA8231_LINK_LEGAL(clink))
				VIA8231_SET_TRIGGER(ph, reg);
			else
				VIA8237_SET_TRIGGER(ph, reg);
			return (0);
		}
	}

	return (1);
}

#ifdef VIA8231_DEBUG
static void
via8231_pir_dump(const char *m, struct via8231_handle *ph)
{
	int a, b;

	a = VIA8231_GET_TRIGGER(ph);
	b = VIA8231_GET_ROUTING(ph);

	printf("%s STATE: trigger(%02x), routing(%08x)\n", m, a, b);
}
#endif
