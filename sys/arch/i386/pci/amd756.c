/*	$OpenBSD: amd756.c,v 1.6 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD$	*/

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
 * Support for the Advanced Micro Devices AMD756 Peripheral Bus Controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/pci/pcivar.h>

#include <i386/pci/pcibiosvar.h>
#include <i386/pci/amd756reg.h>

struct viper_handle {
	bus_space_tag_t ph_iot;
	bus_space_handle_t ph_regs_ioh;
	pci_chipset_tag_t ph_pc;
	pcitag_t ph_tag;
};

int amd756_getclink(pciintr_icu_handle_t, int, int *);
int amd756_get_intr(pciintr_icu_handle_t, int, int *);
int amd756_set_intr(pciintr_icu_handle_t, int, int);
int amd756_get_trigger(pciintr_icu_handle_t, int, int *);
int amd756_set_trigger(pciintr_icu_handle_t, int, int);
#ifdef VIPER_DEBUG
static void amd756_pir_dump(struct viper_handle *);
#endif

const struct pciintr_icu amd756_pci_icu = {
	amd756_getclink,
	amd756_get_intr,
	amd756_set_intr,
	amd756_get_trigger,
	amd756_set_trigger,
};


int
amd756_init(pci_chipset_tag_t pc, bus_space_tag_t iot, pcitag_t tag,
    pciintr_icu_tag_t *ptagp, pciintr_icu_handle_t *phandp)
{
	struct viper_handle *ph;

	ph = malloc(sizeof(*ph), M_DEVBUF, M_NOWAIT);
	if (ph == NULL)
		return (1);

	ph->ph_iot = iot;
	ph->ph_pc = pc;
	ph->ph_tag = tag;

	*ptagp = &amd756_pci_icu;
	*phandp = ph;

#ifdef VIPER_DEBUG
	amd756_pir_dump(ph);
#endif

	return 0;
}

int
amd756_getclink(pciintr_icu_handle_t v, int link, int *clinkp)
{
	if (AMD756_LEGAL_LINK(link - 1) == 0)
		return (1);

	*clinkp = link - 1;
	return (0);
}

int
amd756_get_intr(pciintr_icu_handle_t v, int clink, int *irqp)
{
	struct viper_handle *ph = v;
	pcireg_t reg;
	int val;

	if (AMD756_LEGAL_LINK(clink) == 0)
		return (1);

	reg = AMD756_GET_PIIRQSEL(ph);
	val = (reg >> (4*clink)) & 0x0f;
	*irqp = (val == 0) ?
	    I386_PCI_INTERRUPT_LINE_NO_CONNECTION : val;

	return (0);
}

int
amd756_set_intr(pciintr_icu_handle_t v, int clink, int irq)
{
	struct viper_handle *ph = v;
	int val;
	pcireg_t reg;

	if (AMD756_LEGAL_LINK(clink) == 0 || AMD756_LEGAL_IRQ(irq) == 0)
		return (1);

	reg = AMD756_GET_PIIRQSEL(ph);
	amd756_get_intr(v, clink, &val);
	reg &= ~(0x000f << (4*clink));
	reg |= irq << (4*clink);
	AMD756_SET_PIIRQSEL(ph, reg);

	return 0;
}

int
amd756_get_trigger(pciintr_icu_handle_t v, int irq, int *triggerp)
{
	struct viper_handle *ph = v;
	int i, pciirq;
	pcireg_t reg;

	if (AMD756_LEGAL_IRQ(irq) == 0)
		return (1);

	for (i = 0; i <= 3; i++) {
		amd756_get_intr(v, i, &pciirq);
		if (pciirq == irq) {
			reg = AMD756_GET_EDGESEL(ph);
			if (reg & (1 << i))
				*triggerp = IST_EDGE;
			else
				*triggerp = IST_LEVEL;
			break;
		}
	}

	return 0;
}

int
amd756_set_trigger(pciintr_icu_handle_t v, int irq, int trigger)
{
	struct viper_handle *ph = v;
	int i, pciirq;
	pcireg_t reg;

	if (AMD756_LEGAL_IRQ(irq) == 0)
		return (1);

	for (i = 0; i <= 3; i++) {
		amd756_get_intr(v, i, &pciirq);
		if (pciirq == irq) {
			reg = AMD756_GET_PIIRQSEL(ph);
			if (trigger == IST_LEVEL)
				reg &= ~(1 << (4*i));
			else
				reg |= 1 << (4*i);
			AMD756_SET_PIIRQSEL(ph, reg);
			break;
		}
	}

	return (0);
}

#ifdef VIPER_DEBUG
static void
amd756_pir_dump(struct viper_handle *ph)
{
	int a, b;

	printf ("VIPER PCI INTERRUPT ROUTING REGISTERS:\n");

	a = AMD756_GET_EDGESEL(ph);
	b = AMD756_GET_PIIRQSEL(ph);

	printf ("TRIGGER: %02x, ROUTING: %04x\n", a, b);
}
#endif
