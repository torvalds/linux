/*	$OpenBSD: agp_ali.c,v 1.18 2024/05/24 06:02:53 jsg Exp $	*/
/*	$NetBSD: agp_ali.c,v 1.2 2001/09/15 00:25:00 thorpej Exp $	*/


/*-
 * Copyright (c) 2000 Doug Rabson
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
 *
 *	$FreeBSD: src/sys/pci/agp_ali.c,v 1.3 2001/07/05 21:28:46 jhb Exp $
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

struct agp_ali_softc {
	struct device		 dev;
	struct agp_softc	*agpdev;
	struct agp_gatt		*gatt;
	pci_chipset_tag_t	 asc_pc;
	pcitag_t		 asc_tag;
	bus_addr_t		 asc_apaddr;
	bus_size_t		 asc_apsize;
	pcireg_t		 asc_attbase;
	pcireg_t		 asc_tlbctrl;
};

void	agp_ali_attach(struct device *, struct device *, void *);
int	agp_ali_activate(struct device *, int);
void	agp_ali_save(struct agp_ali_softc *);
void	agp_ali_restore(struct agp_ali_softc *);
int	agp_ali_probe(struct device *, void *, void *);
bus_size_t agp_ali_get_aperture(void *);
int	agp_ali_set_aperture(void *sc, bus_size_t);
void	agp_ali_bind_page(void *, bus_addr_t, paddr_t, int);
void	agp_ali_unbind_page(void *, bus_addr_t);
void	agp_ali_flush_tlb(void *);

const struct cfattach aliagp_ca = {
	sizeof(struct agp_ali_softc), agp_ali_probe, agp_ali_attach,
	NULL, agp_ali_activate
};

struct cfdriver aliagp_cd = {
	NULL, "aliagp", DV_DULL
};

const struct agp_methods agp_ali_methods = {
	agp_ali_bind_page,
	agp_ali_unbind_page,
	agp_ali_flush_tlb,
};

int
agp_ali_probe(struct device *parent, void *match, void *aux)
{
	struct agp_attach_args	*aa = aux;
	struct pci_attach_args	*pa = aa->aa_pa;

	/* Must be a pchb, don't attach to iommu-style agp devs */
	if (agpbus_probe(aa) == 1 && PCI_VENDOR(pa->pa_id) == PCI_VENDOR_ALI &&
	    PCI_PRODUCT(pa->pa_id) != PCI_PRODUCT_ALI_M1689)
		return (1);
	return (0);
}

void 
agp_ali_attach(struct device *parent, struct device *self, void *aux)
{
	struct agp_ali_softc	*asc = (struct agp_ali_softc *)self;
	struct agp_gatt		*gatt;
	struct agp_attach_args	*aa = aux;
	struct pci_attach_args	*pa = aa->aa_pa;
	pcireg_t		 reg;

	asc->asc_tag = pa->pa_tag;
	asc->asc_pc = pa->pa_pc;
	asc->asc_apsize = agp_ali_get_aperture(asc);

	if (pci_mapreg_info(pa->pa_pc, pa->pa_tag, AGP_APBASE,
	    PCI_MAPREG_TYPE_MEM, &asc->asc_apaddr, NULL, NULL) != 0) {
		printf(": can't get aperture info\n");
		return;
	}

	for (;;) {
		gatt = agp_alloc_gatt(pa->pa_dmat, asc->asc_apsize);
		if (gatt != NULL)
			break;
		/*
		 * almost certainly error allocating contiguous dma memory
		 * so reduce aperture so that the gatt size reduces.
		 */
		asc->asc_apsize /= 2;
		if (agp_ali_set_aperture(asc, asc->asc_apsize)) {
			printf("failed to set aperture\n");
			return;
		}
	}
	asc->gatt = gatt;

	/* Install the gatt. */
	reg = pci_conf_read(asc->asc_pc, asc->asc_tag, AGP_ALI_ATTBASE);
	reg = (reg & 0xff) | gatt->ag_physical;
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_ALI_ATTBASE, reg);
	
	/* Enable the TLB. */
	reg = pci_conf_read(asc->asc_pc, asc->asc_tag, AGP_ALI_TLBCTRL);
	reg = (reg & ~0xff) | 0x10;
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_ALI_TLBCTRL, reg);

	asc->agpdev = (struct agp_softc *)agp_attach_bus(pa, &agp_ali_methods,
	    asc->asc_apaddr, asc->asc_apsize, &asc->dev);
	return;
}

int
agp_ali_activate(struct device *arg, int act)
{
	struct agp_ali_softc *asc = (struct agp_ali_softc *)arg;

	switch (act) {
	case DVACT_SUSPEND:
		agp_ali_save(asc);
		break;
	case DVACT_RESUME:
		agp_ali_restore(asc);
		break;
	}

	return (0);
}

void
agp_ali_save(struct agp_ali_softc *asc)
{
	asc->asc_attbase = pci_conf_read(asc->asc_pc, asc->asc_tag,
	    AGP_ALI_ATTBASE);
	asc->asc_tlbctrl = pci_conf_read(asc->asc_pc, asc->asc_tag,
	    AGP_ALI_TLBCTRL);
}

void
agp_ali_restore(struct agp_ali_softc *asc)
{

	/* Install the gatt and aperture size. */
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_ALI_ATTBASE,
	    asc->asc_attbase);
	
	/* Enable the TLB. */
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_ALI_TLBCTRL,
	    asc->asc_tlbctrl);
}

#define M 1024*1024

static const u_int32_t agp_ali_table[] = {
	0,			/* 0 - invalid */
	1,			/* 1 - invalid */
	2,			/* 2 - invalid */
	4*M,			/* 3 - invalid */
	8*M,			/* 4 - invalid */
	0,			/* 5 - invalid */
	16*M,			/* 6 - invalid */
	32*M,			/* 7 - invalid */
	64*M,			/* 8 - invalid */
	128*M,			/* 9 - invalid */
	256*M,			/* 10 - invalid */
};
#define agp_ali_table_size (sizeof(agp_ali_table) / sizeof(agp_ali_table[0]))

bus_size_t
agp_ali_get_aperture(void *sc)
{
	struct agp_ali_softc	*asc = sc;
	int			 i;

	/*
	 * The aperture size is derived from the low bits of attbase.
	 * I'm not sure this is correct..
	 */
	i = (int)pci_conf_read(asc->asc_pc, asc->asc_tag,
	    AGP_ALI_ATTBASE) & 0xff;
	if (i >= agp_ali_table_size)
		return (0);
	return (agp_ali_table[i]);
}

int
agp_ali_set_aperture(void *sc, bus_size_t aperture)
{
	struct agp_ali_softc	*asc = sc;
	int			 i;
	pcireg_t		 reg;

	for (i = 0; i < agp_ali_table_size; i++)
		if (agp_ali_table[i] == aperture)
			break;
	if (i == agp_ali_table_size)
		return (EINVAL);

	reg = pci_conf_read(asc->asc_pc, asc->asc_tag, AGP_ALI_ATTBASE);
	reg &= ~0xff;
	reg |= i;
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_ALI_ATTBASE, reg);
	return (0);
}

void
agp_ali_bind_page(void *sc, bus_addr_t offset, paddr_t physical, int flags)
{
	struct agp_ali_softc *asc = sc;

	asc->gatt->ag_virtual[(offset - asc->asc_apaddr) >> AGP_PAGE_SHIFT] =
	    physical;
}

void
agp_ali_unbind_page(void *sc, bus_size_t offset)
{
	struct agp_ali_softc *asc = sc;

	asc->gatt->ag_virtual[(offset - asc->asc_apaddr) >> AGP_PAGE_SHIFT] = 0;
}

void
agp_ali_flush_tlb(void *sc)
{
	struct agp_ali_softc	*asc = sc;
	pcireg_t		reg;

	reg = pci_conf_read(asc->asc_pc, asc->asc_tag, AGP_ALI_TLBCTRL);
	reg &= ~0xff;
	reg |= 0x90;
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_ALI_TLBCTRL, reg);
	reg &= ~0xff;
	reg |= 0x10;
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_ALI_TLBCTRL, reg);
}

