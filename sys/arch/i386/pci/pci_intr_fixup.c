/*	$OpenBSD: pci_intr_fixup.c,v 1.64 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: pci_intr_fixup.c,v 1.10 2000/08/10 21:18:27 soda Exp $	*/

/*
 * Copyright (c) 2001 Michael Shalayeff
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
 * PCI Interrupt Router support.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/i82093var.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <i386/pci/pcibiosvar.h>

struct pciintr_link_map {
	int link, clink, irq, fixup_stage;
	u_int16_t bitmap;
	SIMPLEQ_ENTRY(pciintr_link_map) list;
};

pciintr_icu_tag_t pciintr_icu_tag = NULL;
pciintr_icu_handle_t pciintr_icu_handle;

#ifdef PCIBIOS_IRQS_HINT
int pcibios_irqs_hint = PCIBIOS_IRQS_HINT;
#endif

struct pciintr_link_map *pciintr_link_lookup(int);
struct pcibios_intr_routing *pciintr_pir_lookup(int, int);
int	pciintr_bitmap_count_irq(int, int *);

SIMPLEQ_HEAD(, pciintr_link_map) pciintr_link_map_list;

const struct pciintr_icu_table {
	pci_vendor_id_t	piit_vendor;
	pci_product_id_t piit_product;
	int (*piit_init)(pci_chipset_tag_t,
		bus_space_tag_t, pcitag_t, pciintr_icu_tag_t *,
		pciintr_icu_handle_t *);
} pciintr_icu_table[] = {
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_6300ESB_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_6321ESB_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82371MX,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82371AB_ISA,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82371FB_ISA,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82371SB_ISA,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82440MX_ISA,
	  piix_init },
	{ PCI_VENDOR_INTEL,     PCI_PRODUCT_INTEL_82801AA_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801AB_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,     PCI_PRODUCT_INTEL_82801BA_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,     PCI_PRODUCT_INTEL_82801BAM_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,     PCI_PRODUCT_INTEL_82801CA_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,     PCI_PRODUCT_INTEL_82801CAM_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,     PCI_PRODUCT_INTEL_82801DB_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801DBM_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801E_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801EB_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801FB_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801FBM_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801GB_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801GBM_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801GH_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801GHM_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801IB_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801IH_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801IO_LPC,
	  piix_init },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801IR_LPC,
	  piix_init },

	{ PCI_VENDOR_OPTI,	PCI_PRODUCT_OPTI_82C558,
	  opti82c558_init },
	{ PCI_VENDOR_OPTI,	PCI_PRODUCT_OPTI_82C700,
	  opti82c700_init },

	{ PCI_VENDOR_RCC,	PCI_PRODUCT_RCC_OSB4,
	  osb4_init },
	{ PCI_VENDOR_RCC,	PCI_PRODUCT_RCC_CSB5,
	  osb4_init },

	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT82C586_ISA,
	  via82c586_init, },
	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT82C596A,
	  via82c586_init, },
	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT82C686A_ISA,
	  via82c586_init },

	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT8231_ISA,
	  via8231_init },
	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT8233_ISA,
	  via8231_init },
	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT8233A_ISA,
	  via8231_init },
	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT8235_ISA,
	  via8231_init },
	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT8237_ISA,
	  via8231_init },
	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT8237A_ISA,
	  via8231_init },
	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT8237S_ISA,
	  via8231_init },

	{ PCI_VENDOR_SIS,	PCI_PRODUCT_SIS_85C503,
	  sis85c503_init },
	{ PCI_VENDOR_SIS,	PCI_PRODUCT_SIS_962,
	  sis85c503_init },
	{ PCI_VENDOR_SIS,	PCI_PRODUCT_SIS_963,
	  sis85c503_init },

	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_PBC756_PMC,
	  amd756_init },
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_766_PMC,
	  amd756_init },
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_PBC768_PMC,
	  amd756_init },

	{ PCI_VENDOR_ALI,	PCI_PRODUCT_ALI_M1533,
	  ali1543_init },

	{ PCI_VENDOR_ALI,	PCI_PRODUCT_ALI_M1543,
	  ali1543_init },

	{ 0,			0,
	  NULL },
};

const struct pciintr_icu_table *pciintr_icu_lookup(pcireg_t);

const struct pciintr_icu_table *
pciintr_icu_lookup(pcireg_t id)
{
	const struct pciintr_icu_table *piit;

	for (piit = pciintr_icu_table; piit->piit_init != NULL; piit++)
		if (PCI_VENDOR(id) == piit->piit_vendor &&
		    PCI_PRODUCT(id) == piit->piit_product)
			return (piit);

	return (NULL);
}

struct pciintr_link_map *
pciintr_link_lookup(int link)
{
	struct pciintr_link_map *l;

	for (l = SIMPLEQ_FIRST(&pciintr_link_map_list); l != NULL;
	     l = SIMPLEQ_NEXT(l, list))
		if (l->link == link)
			return (l);

	return (NULL);
}

static __inline struct pciintr_link_map *
pciintr_link_alloc(pci_chipset_tag_t pc, struct pcibios_intr_routing *pir, int pin)
{
	int link = pir->linkmap[pin].link, clink, irq;
	struct pciintr_link_map *l, *lstart;

	if (pciintr_icu_tag != NULL) {
		/*
		 * Get the canonical link value for this entry.
		 */
		if (pciintr_icu_getclink(pciintr_icu_tag, pciintr_icu_handle,
		    link, &clink) != 0) {
			/*
			 * ICU doesn't understand the link value.
			 * Just ignore this PIR entry.
			 */
			PCIBIOS_PRINTV(("pciintr_link_alloc: bus %d device %d: "
			    "ignoring link 0x%02x\n", pir->bus,
			    PIR_DEVFUNC_DEVICE(pir->device), link));
			return (NULL);
		}

		/*
		 * Check the link value by asking the ICU for the
		 * canonical link value.
		 * Also, determine if this PIRQ is mapped to an IRQ.
		 */
		if (pciintr_icu_get_intr(pciintr_icu_tag, pciintr_icu_handle,
		    clink, &irq) != 0) {
			/*
			 * ICU doesn't understand the canonical link value.
			 * Just ignore this PIR entry.
			 */
			PCIBIOS_PRINTV(("pciintr_link_alloc: "
			    "bus %d device %d link 0x%02x: "
			    "ignoring PIRQ 0x%02x\n", pir->bus,
			    PIR_DEVFUNC_DEVICE(pir->device), link, clink));
			return (NULL);
		}
	}

	if ((l = malloc(sizeof(*l), M_DEVBUF, M_NOWAIT|M_ZERO)) == NULL)
		return (NULL);

	l->link = link;
	l->bitmap = pir->linkmap[pin].bitmap;
	if (pciintr_icu_tag != NULL) { /* compatible PCI ICU found */
		l->clink = clink;
		l->irq = irq; /* maybe I386_PCI_INTERRUPT_LINE_NO_CONNECTION */
	} else {
		l->clink = link;
		l->irq = I386_PCI_INTERRUPT_LINE_NO_CONNECTION;
	}

	lstart = SIMPLEQ_FIRST(&pciintr_link_map_list);
	if (lstart == NULL || lstart->link < l->link)
		SIMPLEQ_INSERT_TAIL(&pciintr_link_map_list, l, list);
	else
		SIMPLEQ_INSERT_HEAD(&pciintr_link_map_list, l, list);

	return (l);
}

struct pcibios_intr_routing *
pciintr_pir_lookup(int bus, int device)
{
	struct pcibios_intr_routing *pir;
	int entry;

	if (pcibios_pir_table == NULL)
		return (NULL);

	for (entry = 0; entry < pcibios_pir_table_nentries; entry++) {
		pir = &pcibios_pir_table[entry];
		if (pir->bus == bus &&
		    PIR_DEVFUNC_DEVICE(pir->device) == device)
			return (pir);
	}

	return (NULL);
}

int
pciintr_bitmap_count_irq(int irq_bitmap, int *irqp)
{
	int i, bit, count = 0, irq = I386_PCI_INTERRUPT_LINE_NO_CONNECTION;

	if (irq_bitmap != 0)
		for (i = 0, bit = 1; i < 16; i++, bit <<= 1)
			if (irq_bitmap & bit) {
				irq = i;
				count++;
			}

	*irqp = irq;
	return (count);
}

static __inline int
pciintr_link_init(pci_chipset_tag_t pc)
{
	int entry, pin, link;
	struct pcibios_intr_routing *pir;
	struct pciintr_link_map *l;

	if (pcibios_pir_table == NULL) {
		/* No PIR table; can't do anything. */
		printf("pciintr_link_init: no PIR table\n");
		return (1);
	}

	SIMPLEQ_INIT(&pciintr_link_map_list);

	for (entry = 0; entry < pcibios_pir_table_nentries; entry++) {
		pir = &pcibios_pir_table[entry];
		for (pin = 0; pin < PCI_INTERRUPT_PIN_MAX; pin++) {
			if ((link = pir->linkmap[pin].link) == 0)
				/* No connection for this pin. */
				continue;

			/*
			 * Multiple devices may be wired to the same
			 * interrupt; check to see if we've seen this
			 * one already.  If not, allocate a new link
			 * map entry and stuff it in the map.
			 */
			if ((l = pciintr_link_lookup(link)) == NULL)
				pciintr_link_alloc(pc, pir, pin);
			else if (pir->linkmap[pin].bitmap != l->bitmap) {
				/*
				 * violates PCI IRQ Routing Table Specification
				 */
				PCIBIOS_PRINTV(("pciintr_link_init: "
				    "bus %d device %d link 0x%02x: "
				    "bad irq bitmap 0x%04x, "
				    "should be 0x%04x\n", pir->bus,
				    PIR_DEVFUNC_DEVICE(pir->device), link,
				    pir->linkmap[pin].bitmap, l->bitmap));
				/* safer value. */
				l->bitmap &= pir->linkmap[pin].bitmap;
				/* XXX - or, should ignore this entry? */
			}
		}
	}

	return (0);
}

/*
 * No compatible PCI ICU found.
 * Hopes the BIOS already setup the ICU.
 */
static __inline int
pciintr_guess_irq(void)
{
	struct pciintr_link_map *l;
	int irq, guessed = 0;

	/*
	 * Stage 1: If only one IRQ is available for the link, use it.
	 */
	for (l = SIMPLEQ_FIRST(&pciintr_link_map_list); l != NULL;
	     l = SIMPLEQ_NEXT(l, list)) {
		if (l->irq != I386_PCI_INTERRUPT_LINE_NO_CONNECTION)
			continue;
		if (pciintr_bitmap_count_irq(l->bitmap, &irq) == 1) {
			l->irq = irq;
			l->fixup_stage = 1;
			if (pcibios_flags & PCIBIOS_INTRDEBUG)
				printf("pciintr_guess_irq (stage 1): "
				    "guessing PIRQ 0x%02x to be IRQ %d\n",
				    l->clink, l->irq);
			guessed = 1;
		}
	}

	return (guessed ? 0 : -1);
}

static __inline int
pciintr_link_fixup(void)
{
	struct pciintr_link_map *l;
	u_int16_t pciirq = 0;
	int irq;

	/*
	 * First stage: Attempt to connect PIRQs which aren't
	 * yet connected.
	 */
	for (l = SIMPLEQ_FIRST(&pciintr_link_map_list); l != NULL;
	     l = SIMPLEQ_NEXT(l, list)) {
		if (l->irq != I386_PCI_INTERRUPT_LINE_NO_CONNECTION) {
			/*
			 * Interrupt is already connected.  Don't do
			 * anything to it.
			 * In this case, l->fixup_stage == 0.
			 */
			pciirq |= 1 << l->irq;
			if (pcibios_flags & PCIBIOS_INTRDEBUG)
				printf("pciintr_link_fixup: PIRQ 0x%02x is "
				    "already connected to IRQ %d\n",
				    l->clink, l->irq);
			continue;
		}
		/*
		 * Interrupt isn't connected.  Attempt to assign it to an IRQ.
		 */
		if (pcibios_flags & PCIBIOS_INTRDEBUG)
			printf("pciintr_link_fixup: PIRQ 0x%02x not connected",
			    l->clink);

		/*
		 * Just do the easy case now; we'll defer the harder ones
		 * to Stage 2.
		 */
		if (pciintr_bitmap_count_irq(l->bitmap, &irq) == 1) {
			l->irq = irq;
			l->fixup_stage = 1;
			pciirq |= 1 << irq;
			if (pcibios_flags & PCIBIOS_INTRDEBUG)
				printf(", assigning IRQ %d", l->irq);
		}
		if (pcibios_flags & PCIBIOS_INTRDEBUG)
			printf("\n");
	}

	/*
	 * Stage 2: Attempt to connect PIRQs which we didn't
	 * connect in Stage 1.
	 */
	for (l = SIMPLEQ_FIRST(&pciintr_link_map_list); l != NULL;
	     l = SIMPLEQ_NEXT(l, list))
		if (l->irq == I386_PCI_INTERRUPT_LINE_NO_CONNECTION &&
		    (irq = ffs(l->bitmap & pciirq)) > 0) {
			/*
			 * This IRQ is a valid PCI IRQ already
			 * connected to another PIRQ, and also an
			 * IRQ our PIRQ can use; connect it up!
			 */
			l->fixup_stage = 2;
			l->irq = irq - 1;
			if (pcibios_flags & PCIBIOS_INTRDEBUG)
				printf("pciintr_link_fixup (stage 2): "
				       "assigning IRQ %d to PIRQ 0x%02x\n",
				       l->irq, l->clink);
		}

#ifdef PCIBIOS_IRQS_HINT
	/*
	 * Stage 3: The worst case. I need configuration hint that
	 * user supplied a mask for the PCI irqs
	 */
	for (l = SIMPLEQ_FIRST(&pciintr_link_map_list); l != NULL;
	     l = SIMPLEQ_NEXT(l, list)) {
		if (l->irq == I386_PCI_INTERRUPT_LINE_NO_CONNECTION &&
		    (irq = ffs(l->bitmap & pcibios_irqs_hint)) > 0) {
			l->fixup_stage = 3;
			l->irq = irq - 1;
			if (pcibios_flags & PCIBIOS_INTRDEBUG)
				printf("pciintr_link_fixup (stage 3): "
				    "assigning IRQ %d to PIRQ 0x%02x\n",
				    l->irq, l->clink);
		}
	}
#endif /* PCIBIOS_IRQS_HINT */

	if (pcibios_flags & PCIBIOS_INTRDEBUG)
		printf("pciintr_link_fixup: piirq 0x%04x\n", pciirq);

	return (0);
}

int
pci_intr_route_link(pci_chipset_tag_t pc, pci_intr_handle_t *ihp)
{
	struct pciintr_link_map *l;
	pcireg_t intr;
	int irq, rv = 1;
	char *p = NULL;

	if (pcibios_flags & PCIBIOS_INTR_FIXUP)
		return 1;

	irq = ihp->line & APIC_INT_LINE_MASK;
	if (irq != 0 && irq != I386_PCI_INTERRUPT_LINE_NO_CONNECTION)
		pcibios_pir_header.exclusive_irq |= 1 << irq;

	l = ihp->link;
	if (!l || pciintr_icu_tag == NULL)
		return (1);

	if (l->fixup_stage == 0) {
		if (l->irq == I386_PCI_INTERRUPT_LINE_NO_CONNECTION) {
			/* Appropriate interrupt was not found. */
			if (pcibios_flags & PCIBIOS_INTRDEBUG)
				printf("pci_intr_route_link: PIRQ 0x%02x: "
				    "no IRQ, try "
				    "\"option PCIBIOS_IRQS_HINT=0x%04x\"\n",
				    l->clink,
				    /* suggest irq 9/10/11, if possible */
				    (l->bitmap & 0x0e00) ? (l->bitmap & 0x0e00)
				    : l->bitmap);
		} else
			p = " preserved BIOS setting";
	} else {

		if (pciintr_icu_set_intr(pciintr_icu_tag, pciintr_icu_handle,
		    l->clink, l->irq) != 0 ||
		    pciintr_icu_set_trigger(pciintr_icu_tag, pciintr_icu_handle,
		    l->irq, IST_LEVEL) != 0) {
			p = " failed";
			rv = 0;
		} else
			p = "";
	}
	if (p && pcibios_flags & PCIBIOS_INTRDEBUG)
		printf("pci_intr_route_link: route PIRQ 0x%02x -> IRQ %d%s\n",
		    l->clink, l->irq, p);

	if (!rv)
		return (0);

	/*
	 * IRQs 14 and 15 are reserved for PCI IDE interrupts; don't muck
	 * with them.
	 */
	if (irq == 14 || irq == 15)
		return (1);

	intr = pci_conf_read(pc, ihp->tag, PCI_INTERRUPT_REG);
	if (irq != PCI_INTERRUPT_LINE(intr)) {
		intr &= ~(PCI_INTERRUPT_LINE_MASK << PCI_INTERRUPT_LINE_SHIFT);
		intr |= irq << PCI_INTERRUPT_LINE_SHIFT;
		pci_conf_write(pc, ihp->tag, PCI_INTERRUPT_REG, intr);
	}

	return (1);
}

int
pci_intr_post_fixup(void)
{
	struct pciintr_link_map *l;
	int i, pciirq;

	if (pcibios_flags & PCIBIOS_INTR_FIXUP)
		return 1;

	if (!pciintr_icu_handle)
		return 0;

	pciirq = pcibios_pir_header.exclusive_irq;
	if (pcibios_flags & PCIBIOS_INTRDEBUG)
		printf("pci_intr_post_fixup: PCI IRQs:");
	for (l = SIMPLEQ_FIRST(&pciintr_link_map_list);
	    l != NULL; l = SIMPLEQ_NEXT(l, list))
		if (l->fixup_stage == 0 && l->irq != 0 &&
		    l->irq != I386_PCI_INTERRUPT_LINE_NO_CONNECTION) {
			if (pcibios_flags & PCIBIOS_INTRDEBUG)
				printf(" %d", l->irq);
			pciirq |= (1 << l->irq);
		}

	if (pcibios_flags & PCIBIOS_INTRDEBUG)
		printf("; ISA IRQs:");
	for (i = 0; i < 16; i++)
		if (!(pciirq & (1 << i))) {
			if (pcibios_flags & PCIBIOS_INTRDEBUG)
				printf(" %d", i);
			pciintr_icu_set_trigger(pciintr_icu_tag,
			    pciintr_icu_handle, i, IST_EDGE);
		}

	if (pcibios_flags & PCIBIOS_INTRDEBUG)
		printf("\n");

	return (0);
}

int
pci_intr_header_fixup(pci_chipset_tag_t pc, pcitag_t tag,
    pci_intr_handle_t *ihp)
{
	struct pcibios_intr_routing *pir;
	struct pciintr_link_map *l;
	int irq, link, bus, device, function;
	char *p = NULL;

	if (pcibios_flags & PCIBIOS_INTR_FIXUP)
		return 1;

	irq = ihp->line & APIC_INT_LINE_MASK;
	ihp->link = NULL;
	pci_decompose_tag(pc, tag, &bus, &device, &function);

	if ((pir = pciintr_pir_lookup(bus, device)) == NULL ||
	    (link = pir->linkmap[ihp->pin - 1].link) == 0) {
		PCIBIOS_PRINTV(("Interrupt not connected; no need to change."));
		return 1;
	}

	if ((l = pciintr_link_lookup(link)) == NULL) {
		/*
		 * No link map entry.
		 * Probably pciintr_icu_getclink() or pciintr_icu_get_intr()
		 * has failed.
		 */
		if (pcibios_flags & PCIBIOS_INTRDEBUG)
			printf("pci_intr_header_fixup: no entry for link "
			    "0x%02x (%d:%d:%d:%c)\n",
			    link, bus, device, function, '@' + ihp->pin);
		return 1;
	}

	ihp->link = l;
	if (irq == 14 || irq == 15) {
		p = " WARNING: ignored";
		ihp->link = NULL;
	} else if (l->irq == I386_PCI_INTERRUPT_LINE_NO_CONNECTION) {

		/* Appropriate interrupt was not found. */
		if (pciintr_icu_tag == NULL && irq != 0 &&
		    irq != I386_PCI_INTERRUPT_LINE_NO_CONNECTION)
			/*
			 * Do not print warning,
			 * if no compatible PCI ICU found,
			 * but the irq is already assigned by BIOS.
			 */
			p = "";
		else
			p = " WARNING: missing";
	} else if (irq == 0 || irq == I386_PCI_INTERRUPT_LINE_NO_CONNECTION) {

		p = " fixed up";
		ihp->line = irq = l->irq;

	} else if (pcibios_flags & PCIBIOS_FIXUP_FORCE) {
		/* routed by BIOS, but inconsistent */
		/* believe PCI IRQ Routing table */
		p = " WARNING: overriding";
		ihp->line = irq = l->irq;
	} else {
		/* believe PCI Interrupt Configuration Register (default) */
		p = " WARNING: preserving";
		ihp->line = (l->irq = irq) | (l->clink & PCI_INT_VIA_ISA);
	}

	if (pcibios_flags & PCIBIOS_INTRDEBUG) {
		pcireg_t id = pci_conf_read(pc, tag, PCI_ID_REG);

		printf("\n%d:%d:%d %04x:%04x pin %c clink 0x%02x irq %d "
		    "stage %d %s irq %d\n", bus, device, function,
		    PCI_VENDOR(id), PCI_PRODUCT(id), '@' + ihp->pin, l->clink,
		    ((l->irq == I386_PCI_INTERRUPT_LINE_NO_CONNECTION)?
		    -1 : l->irq), l->fixup_stage, p, irq);
	}

	return (1);
}

int
pci_intr_fixup(struct pcibios_softc *sc, pci_chipset_tag_t pc,
    bus_space_tag_t iot)
{
	struct pcibios_pir_header *pirh = &pcibios_pir_header;
	const struct pciintr_icu_table *piit = NULL;
	pcitag_t icutag;

	/*
	 * Attempt to initialize our PCI interrupt router.  If
	 * the PIR Table is present in ROM, use the location
	 * specified by the PIR Table, and use the compat ID,
	 * if present.  Otherwise, we have to look for the router
	 * ourselves (the PCI-ISA bridge).
	 *
	 * A number of buggy BIOS implementations leave the router
	 * entry as 000:00:0, which is typically not the correct
	 * device/function.  If the router device address is set to
	 * this value, and the compatible router entry is undefined
	 * (zero is the correct value to indicate undefined), then we
	 * work on the basis it is most likely an error, and search
	 * the entire device-space of bus 0 (but obviously starting
	 * with 000:00:0, in case that really is the right one).
	 */
	if (pirh->signature != 0 && (pirh->router_bus != 0 ||
	    pirh->router_devfunc != 0 || pirh->compat_router != 0)) {

		icutag = pci_make_tag(pc, pirh->router_bus,
		    PIR_DEVFUNC_DEVICE(pirh->router_devfunc),
		    PIR_DEVFUNC_FUNCTION(pirh->router_devfunc));
		if (pirh->compat_router == 0 ||
		    (piit = pciintr_icu_lookup(pirh->compat_router)) == NULL) {
			/*
			 * No compat ID, or don't know the compat ID?  Read
			 * it from the configuration header.
			 */
			pirh->compat_router = pci_conf_read(pc, icutag,
			    PCI_ID_REG);
		}
		if (piit == NULL)
			piit = pciintr_icu_lookup(pirh->compat_router);
	} else {
		int device, maxdevs = pci_bus_maxdevs(pc, 0);

		/*
		 * Search configuration space for a known interrupt
		 * router.
		 */
		for (device = 0; device < maxdevs; device++) {
			const struct pci_quirkdata *qd;
			int function, nfuncs;
			pcireg_t icuid;
			pcireg_t bhlcr;

			icutag = pci_make_tag(pc, 0, device, 0);
			icuid = pci_conf_read(pc, icutag, PCI_ID_REG);

			/* Invalid vendor ID value? */
			if (PCI_VENDOR(icuid) == PCI_VENDOR_INVALID)
				continue;
			/* XXX Not invalid, but we've done this ~forever. */
			if (PCI_VENDOR(icuid) == 0)
				continue;

			qd = pci_lookup_quirkdata(PCI_VENDOR(icuid),
			    PCI_PRODUCT(icuid));

			bhlcr = pci_conf_read(pc, icutag, PCI_BHLC_REG);
			if (PCI_HDRTYPE_MULTIFN(bhlcr) || (qd != NULL &&
			    (qd->quirks & PCI_QUIRK_MULTIFUNCTION) != 0))
				nfuncs = 8;
			else
				nfuncs = 1;

			for (function = 0; function < nfuncs; function++) {
				icutag = pci_make_tag(pc, 0, device, function);
				icuid = pci_conf_read(pc, icutag, PCI_ID_REG);

				/* Invalid vendor ID value? */
				if (PCI_VENDOR(icuid) == PCI_VENDOR_INVALID)
					continue;
				/* Not invalid, but we've done this ~forever. */
				if (PCI_VENDOR(icuid) == 0)
					continue;

				if ((piit = pciintr_icu_lookup(icuid))) {
					pirh->compat_router = icuid;
					pirh->router_bus = 0;
					pirh->router_devfunc =
					    PIR_DEVFUNC_COMPOSE(device, 0);
					break;
				}
			}

			if (piit != NULL)
				break;
		}
	}

	if (piit == NULL) {
		printf("%s: no compatible PCI ICU found", sc->sc_dev.dv_xname);
		if (pirh->signature != 0 && pirh->compat_router != 0)
			printf(": ICU vendor 0x%04x product 0x%04x",
			    PCI_VENDOR(pirh->compat_router),
			    PCI_PRODUCT(pirh->compat_router));
		printf("\n");
		if (!(pcibios_flags & PCIBIOS_INTR_GUESS)) {
			if (pciintr_link_init(pc))
				return (-1);	/* non-fatal */
			if (pciintr_guess_irq())
				return (-1);	/* non-fatal */
		}
		return (0);
	} else {
		char devinfo[256];

		printf("%s: PCI Interrupt Router at %03d:%02d:%01d",
		    sc->sc_dev.dv_xname, pirh->router_bus,
		    PIR_DEVFUNC_DEVICE(pirh->router_devfunc),
		    PIR_DEVFUNC_FUNCTION(pirh->router_devfunc));
		if (pirh->compat_router != 0) {
			pci_devinfo(pirh->compat_router, 0, 0, devinfo,
			    sizeof devinfo);
			printf(" (%s)", devinfo);
		}
		printf("\n");
	}

	/*
	 * Initialize the PCI ICU.
	 */
	if ((*piit->piit_init)(pc, iot, icutag, &pciintr_icu_tag,
	    &pciintr_icu_handle) != 0)
		return (-1);		/* non-fatal */

	/*
	 * Initialize the PCI interrupt link map.
	 */
	if (pciintr_link_init(pc))
		return (-1);		/* non-fatal */

	/*
	 * Fix up the link->IRQ mappings.
	 */
	if (pciintr_link_fixup() != 0)
		return (-1);		/* non-fatal */

	return (0);
}
