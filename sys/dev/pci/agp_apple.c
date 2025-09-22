/*	$OpenBSD: agp_apple.c,v 1.9 2024/05/24 06:02:53 jsg Exp $	*/

/*
 * Copyright (c) 2012 Martin Pieuchot <mpi@openbsd.org>
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
#include <sys/device.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/agpvar.h>
#include <dev/pci/agpreg.h>

#include <machine/bus.h>

#define M (1024 * 1024)

struct agp_apple_softc {
	struct device		 dev;
	struct agp_softc	*agpdev;
	struct agp_gatt		*gatt;
	pci_chipset_tag_t	 asc_pc;
	pcitag_t		 asc_tag;
	bus_addr_t		 asc_apaddr;
	bus_size_t		 asc_apsize;
	int			 asc_flags;
#define	AGP_APPLE_ISU3 (1 << 0)
};

int	agp_apple_match(struct device *, void *, void *);
void	agp_apple_attach(struct device *, struct device *, void *);
bus_size_t agp_apple_get_aperture(void *);
int	agp_apple_set_aperture(void *sc, bus_size_t);
void	agp_apple_bind_page(void *, bus_addr_t, paddr_t, int);
void	agp_apple_unbind_page(void *, bus_addr_t);
void	agp_apple_flush_tlb(void *);
int	agp_apple_enable(void *, uint32_t);

const struct cfattach appleagp_ca = {
	sizeof(struct agp_apple_softc), agp_apple_match, agp_apple_attach,
};

struct cfdriver appleagp_cd = {
	NULL, "appleagp", DV_DULL
};

const struct agp_methods agp_apple_methods = {
	agp_apple_bind_page,
	agp_apple_unbind_page,
	agp_apple_flush_tlb,
	agp_apple_enable,
};

int
agp_apple_match(struct device *parent, void *match, void *aux)
{
	struct agp_attach_args *aa = aux;
	struct pci_attach_args *pa = aa->aa_pa;

	if (agpbus_probe(aa) != 1 || PCI_VENDOR(pa->pa_id) != PCI_VENDOR_APPLE)
		return (0);

	switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_APPLE_UNINORTH_AGP:
		case PCI_PRODUCT_APPLE_PANGEA_AGP:
		case PCI_PRODUCT_APPLE_UNINORTH2_AGP:
		case PCI_PRODUCT_APPLE_UNINORTH_AGP3:
		case PCI_PRODUCT_APPLE_INTREPID2_AGP:
			/* XXX until KMS works with these bridges */
			return (0);
		case PCI_PRODUCT_APPLE_U3_AGP:
		case PCI_PRODUCT_APPLE_U3L_AGP:
		case PCI_PRODUCT_APPLE_K2_AGP:
			return (1);
	}

	return (0);
}

void
agp_apple_attach(struct device *parent, struct device *self, void *aux)
{
	struct agp_apple_softc *asc = (struct agp_apple_softc *)self;
	struct agp_attach_args *aa = aux;
	struct pci_attach_args *pa = aa->aa_pa;
	struct agp_gatt *gatt;

	asc->asc_tag = pa->pa_tag;
	asc->asc_pc = pa->pa_pc;

	switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_APPLE_U3_AGP:
		case PCI_PRODUCT_APPLE_U3L_AGP:
		case PCI_PRODUCT_APPLE_K2_AGP:
			asc->asc_flags |= AGP_APPLE_ISU3;
			break;
		default:
			break;
	}

	/*
	 * XXX It looks like UniNorth GART only accepts an aperture
	 * base address of 0x00 certainly because it does not perform
	 * any physical-to-physical address translation.
	 */
	asc->asc_apaddr = 0x00;
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_APPLE_APBASE,
	    asc->asc_apaddr);

	/*
	 * There's no way to read the aperture size but all UniNorth
	 * chips seem to support an aperture of 256M (and 512M for U3).
	 */
	asc->asc_apsize = 256 * M;
	for (;;) {
		gatt = agp_alloc_gatt(pa->pa_dmat, asc->asc_apsize);
		if (gatt != NULL)
			break;

		asc->asc_apsize /= 2;
	}
	asc->gatt = gatt;

	if (agp_apple_set_aperture(asc, asc->asc_apsize)) {
		printf("failed to set aperture\n");
		return;
	}

	agp_apple_flush_tlb(asc);

	asc->agpdev = (struct agp_softc *)agp_attach_bus(pa, &agp_apple_methods,
	    asc->asc_apaddr, asc->asc_apsize, &asc->dev);
}

bus_size_t
agp_apple_get_aperture(void *dev)
{
	struct agp_apple_softc *asc = dev;

	return (asc->asc_apsize);
}

int
agp_apple_set_aperture(void *dev, bus_size_t aperture)
{
	struct agp_apple_softc *asc = dev;

	if (aperture % (4 * M) || aperture < (4 * M) || aperture > (256 * M))
		return (EINVAL);

	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_APPLE_ATTBASE,
	    (asc->gatt->ag_physical & 0xfffff000) | (aperture >> 22));

	return (0);
}

#define flushd(p) __asm volatile("dcbst 0,%0; sync" ::"r"(p) : "memory")

void
agp_apple_bind_page(void *v, bus_addr_t off, paddr_t pa, int flags)
{
	struct agp_apple_softc *asc = v;
	uint32_t entry;

	if (off >= (asc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return;

	if (asc->asc_flags & AGP_APPLE_ISU3)
		entry = (pa >> PAGE_SHIFT | 0x80000000);
	else
		entry = htole32(pa | 0x01);

	flushdcache((void *)pa, PAGE_SIZE);

	asc->gatt->ag_virtual[off >> AGP_PAGE_SHIFT] = entry;
	flushd(&asc->gatt->ag_virtual[off >> AGP_PAGE_SHIFT]);
}

void
agp_apple_unbind_page(void *v, bus_size_t off)
{
	struct agp_apple_softc *asc = v;

	if (off >= (asc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return;

	asc->gatt->ag_virtual[off >> AGP_PAGE_SHIFT] = 0;

	flushd(&asc->gatt->ag_virtual[off >> AGP_PAGE_SHIFT]);
}

#undef flushd

void
agp_apple_flush_tlb(void *v)
{
	struct agp_apple_softc *asc = v;

	if (asc->asc_flags & AGP_APPLE_ISU3) {
		pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_APPLE_GARTCTRL,
		    AGP_APPLE_GART_ENABLE | AGP_APPLE_GART_PERFRD |
		    AGP_APPLE_GART_INVALIDATE);
		pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_APPLE_GARTCTRL,
		    AGP_APPLE_GART_ENABLE | AGP_APPLE_GART_PERFRD);
	} else {
		pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_APPLE_GARTCTRL,
		    AGP_APPLE_GART_ENABLE | AGP_APPLE_GART_INVALIDATE);
		pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_APPLE_GARTCTRL,
		    AGP_APPLE_GART_ENABLE);
	}
}

int
agp_apple_enable(void *v, uint32_t mode)
{
	struct agp_apple_softc *asc = v;

	if ((asc->asc_flags & AGP_APPLE_ISU3) == 0) {
		/*
		 * GART invalidate/SBA reset?  Linux and Darwin do something
		 * similar and it prevents GPU lockups with KMS.
		 */
		pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_APPLE_GARTCTRL,
		    AGP_APPLE_GART_ENABLE | AGP_APPLE_GART_2XRESET);
		pci_conf_write(asc->asc_pc, asc->asc_tag,
		    AGP_APPLE_GARTCTRL, AGP_APPLE_GART_ENABLE);
	}

	return (agp_generic_enable(asc->agpdev, mode));
}
