/*	$OpenBSD: mpbios_intr_fixup.c,v 1.6 2015/08/10 12:12:42 deraadt Exp $	*/

/*
 * Copyright (c) 2006 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <machine/i82093var.h>
#include <machine/mpbiosvar.h>

void mpbios_pin_fixup(int, int, int, int);
const struct mpbios_icu_table *mpbios_icu_lookup(pcireg_t);

void via8237_mpbios_fixup(pci_chipset_tag_t, pcitag_t);
void nforce4_mpbios_fixup(pci_chipset_tag_t, pcitag_t);
void mcp04_mpbios_fixup(pci_chipset_tag_t, pcitag_t);

const struct mpbios_icu_table {
	pci_vendor_id_t mpit_vendor;
	pci_product_id_t mpit_product;
	void (*mpit_mpbios_fixup)(pci_chipset_tag_t, pcitag_t);
} mpbios_icu_table[] = {
	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT8237_ISA,
	  via8237_mpbios_fixup },
	{ PCI_VENDOR_NVIDIA,	PCI_PRODUCT_NVIDIA_NFORCE4_ISA1,
	  nforce4_mpbios_fixup },
	{ PCI_VENDOR_NVIDIA,	PCI_PRODUCT_NVIDIA_NFORCE4_ISA2,
	  nforce4_mpbios_fixup },
	{ PCI_VENDOR_NVIDIA,	PCI_PRODUCT_NVIDIA_MCP04_ISA,
	  mcp04_mpbios_fixup },
	{ 0,			0,
	  NULL},
};

const struct mpbios_icu_table *
mpbios_icu_lookup(pcireg_t id)
{
	const struct mpbios_icu_table *mpit;

	for (mpit = mpbios_icu_table; mpit->mpit_mpbios_fixup != NULL; mpit++)
		if (PCI_VENDOR(id) == mpit->mpit_vendor &&
		    PCI_PRODUCT(id) == mpit->mpit_product)
			return (mpit);

	return (NULL);
}

/*
 * NVIDIA nForce4 PCI-ISA bridge.
 */

#define NFORCE4_PNPIRQ1	0x7c
#define NFORCE4_PNPIRQ2	0x80
#define  NFORCE4_USB2_SHIFT	12
#define  NFORCE4_USB2_MASK	(0xf << NFORCE4_USB2_SHIFT)
#define  NFORCE4_SATA1_SHIFT	28
#define  NFORCE4_SATA1_MASK	(0xf << NFORCE4_SATA1_SHIFT)
#define  NFORCE4_SATA2_SHIFT	24
#define  NFORCE4_SATA2_MASK	(0xf << NFORCE4_SATA2_SHIFT)
#define NFORCE4_PNPIRQ3	0x84
#define  NFORCE4_USB1_SHIFT	0
#define  NFORCE4_USB1_MASK	(0xf << NFORCE4_USB1_SHIFT)
#define  NFORCE4_LAN_SHIFT	8
#define  NFORCE4_LAN_MASK	(0xf << NFORCE4_LAN_SHIFT)

void
nforce4_mpbios_fixup(pci_chipset_tag_t pc, pcitag_t tag)
{
	pcireg_t reg;
	int bus, pin;

	pci_decompose_tag (pc, tag, &bus, NULL, NULL);

	reg = pci_conf_read(pc, tag, NFORCE4_PNPIRQ2);
	pin = (reg & NFORCE4_USB2_MASK) >> NFORCE4_USB2_SHIFT;
	if (pin != 0)
		mpbios_pin_fixup(bus, 2, PCI_INTERRUPT_PIN_B, pin);
	pin = (reg & NFORCE4_SATA1_MASK) >> NFORCE4_SATA1_SHIFT;
	if (pin != 0)
		mpbios_pin_fixup(bus, 7, PCI_INTERRUPT_PIN_A, pin);
	pin = (reg & NFORCE4_SATA2_MASK) >> NFORCE4_SATA2_SHIFT;
	if (pin != 0)
		mpbios_pin_fixup(bus, 8, PCI_INTERRUPT_PIN_A, pin);

	reg = pci_conf_read(pc, tag, NFORCE4_PNPIRQ3);
	pin = (reg & NFORCE4_USB1_MASK) >> NFORCE4_USB1_SHIFT;
	if (pin != 0)
		mpbios_pin_fixup(bus, 2, PCI_INTERRUPT_PIN_A, pin);
	pin = (reg & NFORCE4_LAN_MASK) >> NFORCE4_LAN_SHIFT;
	if (pin != 0)
		mpbios_pin_fixup(bus, 10, PCI_INTERRUPT_PIN_A, pin);
}

/*
 * NVIDIA MCP04 PCI-ISA bridge.
 */

void
mcp04_mpbios_fixup(pci_chipset_tag_t pc, pcitag_t tag)
{
	pcireg_t reg;
	int bus, pin;

	pci_decompose_tag (pc, tag, &bus, NULL, NULL);

	reg = pci_conf_read(pc, tag, NFORCE4_PNPIRQ2);
	pin = (reg & NFORCE4_SATA1_MASK) >> NFORCE4_SATA1_SHIFT;
	if (pin != 0)
		mpbios_pin_fixup(bus, 16, PCI_INTERRUPT_PIN_A, pin);
	pin = (reg & NFORCE4_SATA2_MASK) >> NFORCE4_SATA2_SHIFT;
	if (pin != 0)
		mpbios_pin_fixup(bus, 17, PCI_INTERRUPT_PIN_A, pin);
}

/*
 * VIA VT8237 PCI-ISA bridge.
 */

void
via8237_mpbios_fixup(pci_chipset_tag_t pc, pcitag_t tag)
{
	int bus;

	pci_decompose_tag (pc, tag, &bus, NULL, NULL);

	/* SATA is hardwired to APIC pin 20. */
	mpbios_pin_fixup(bus, 15, 2, 20);
}

void
mpbios_pin_fixup(int bus, int dev, int rawpin, int pin)
{
	struct mp_bus *mpb = &mp_busses[bus];
	struct mp_intr_map *mip;

	for (mip = mpb->mb_intrs; mip != NULL; mip = mip->next) {
		if (mip->bus_pin == ((dev << 2) | (rawpin - 1)) &&
		    mip->ioapic_pin != pin) {

			if (mp_verbose) {

				printf("%s: int%d attached to %s",
				    mip->ioapic->sc_pic.pic_name,
				    pin, mpb->mb_name);

				if (mpb->mb_idx != -1)
					printf("%d", mpb->mb_idx);

				(*(mpb->mb_intr_print))(mip->bus_pin);

				printf(" (fixup)\n");
			}

			mip->ioapic_pin = pin;
			mip->ioapic_ih &= ~APIC_INT_PIN_MASK;
			mip->ioapic_ih |= (pin << APIC_INT_PIN_SHIFT);
			if (mip->ioapic->sc_pins[pin].ip_map == NULL)
				mip->ioapic->sc_pins[pin].ip_map = mip;
		}
	}
}

void
mpbios_intr_fixup(void)
{
	const struct mpbios_icu_table *mpit = NULL;
	pci_chipset_tag_t pc = NULL;
	pcitag_t icutag;
	int device, maxdevs = pci_bus_maxdevs(pc, 0);

	/* Search configuration space for a known interrupt router. */
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

			if ((mpit = mpbios_icu_lookup(icuid)))
				break;
		}

		if (mpit != NULL)
			break;
	}

	if (mpit)
		mpit->mpit_mpbios_fixup(pc, icutag);
}
