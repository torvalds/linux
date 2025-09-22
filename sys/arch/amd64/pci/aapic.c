/*	$OpenBSD: aapic.c,v 1.8 2024/11/05 18:58:59 miod Exp $	*/
/* 	$NetBSD: aapic.c,v 1.3 2005/01/13 23:40:01 fvdl Exp $	*/

/*
 * The AMD 8131 IO APIC can hang the box when an APIC IRQ is masked.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include "ioapic.h"

#if NIOAPIC > 0
extern int nioapics;
#endif

#define AMD8131_PCIX_MISC	0x40
#define AMD8131_NIOAMODE	0x00000001

#define AMD8131_IOAPIC_CTL	0x44
#define AMD8131_IOAEN		0x00000002

int	aapic_match(struct device *, void *, void *);
void	aapic_attach(struct device *, struct device *, void *);

struct aapic_softc {
	struct device sc_dev;
};

const struct cfattach aapic_ca = {
	sizeof(struct aapic_softc), aapic_match, aapic_attach
};

struct cfdriver aapic_cd = {
	NULL, "aapic", DV_DULL
};

int
aapic_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_AMD &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_8131_PCIX_IOAPIC)
		return (1);

	return (0);
}

void
aapic_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	int bus, dev, func;
	pcitag_t tag;
	pcireg_t reg;

	printf("\n");

#if NIOAPIC > 0
	if (nioapics == 0)
		return;
#else
	return;
#endif
	
	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, AMD8131_IOAPIC_CTL);
	reg |= AMD8131_IOAEN;
	pci_conf_write(pa->pa_pc, pa->pa_tag, AMD8131_IOAPIC_CTL, reg);

	pci_decompose_tag(pa->pa_pc, pa->pa_tag, &bus, &dev, &func);
	func = 0;
	tag = pci_make_tag(pa->pa_pc, bus, dev, func);
	reg = pci_conf_read(pa->pa_pc, tag, AMD8131_PCIX_MISC);
	reg &= ~AMD8131_NIOAMODE;
	pci_conf_write(pa->pa_pc, tag, AMD8131_PCIX_MISC, reg);
}
