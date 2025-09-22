/*	$OpenBSD: agp_amd.c,v 1.24 2024/05/24 06:02:53 jsg Exp $	*/
/*	$NetBSD: agp_amd.c,v 1.6 2001/10/06 02:48:50 thorpej Exp $	*/

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
 *	$FreeBSD: src/sys/pci/agp_amd.c,v 1.6 2001/07/05 21:28:46 jhb Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/agpvar.h>
#include <dev/pci/agpreg.h>

#include <dev/pci/pcidevs.h>

#define READ2(off)	bus_space_read_2(asc->iot, asc->ioh, off)
#define READ4(off)	bus_space_read_4(asc->iot, asc->ioh, off)
#define WRITE2(off,v)	bus_space_write_2(asc->iot, asc->ioh, off, v)
#define WRITE4(off,v)	bus_space_write_4(asc->iot, asc->ioh, off, v)

struct agp_amd_gatt {
	bus_dmamap_t	ag_dmamap;
	bus_dma_segment_t ag_dmaseg;
	int		ag_nseg;
	u_int32_t	ag_entries;
	u_int32_t      *ag_vdir;	/* virtual address of page dir */
	bus_addr_t	ag_pdir;	/* bus address of page dir */
	u_int32_t      *ag_virtual;	/* virtual address of gatt */
	bus_addr_t	ag_physical;	/* bus address of gatt */
	size_t		ag_size;
};

struct agp_amd_softc {
	struct device		 dev;
	struct agp_softc	*agpdev;
	struct agp_amd_gatt	*gatt;
	pci_chipset_tag_t	 asc_pc;
	pcitag_t		 asc_tag;
	bus_space_handle_t	 ioh;
	bus_space_tag_t		 iot;
	bus_addr_t		 asc_apaddr;
	bus_size_t		 asc_apsize;
	pcireg_t		 asc_apctrl;
	pcireg_t		 asc_modectrl;
	u_int16_t		 asc_status;
};

void	agp_amd_attach(struct device *, struct device *, void *);
int	agp_amd_activate(struct device *, int);
void	agp_amd_save(struct agp_amd_softc *);
void	agp_amd_restore(struct agp_amd_softc *);
int	agp_amd_probe(struct device *, void *, void *);
bus_size_t agp_amd_get_aperture(void *);
struct agp_amd_gatt *agp_amd_alloc_gatt(bus_dma_tag_t, bus_size_t);
int	agp_amd_set_aperture(void *, bus_size_t);
void	agp_amd_bind_page(void *, bus_size_t, paddr_t, int);
void	agp_amd_unbind_page(void *, bus_size_t);
void	agp_amd_flush_tlb(void *);

const struct cfattach amdagp_ca = {
	sizeof(struct agp_amd_softc), agp_amd_probe, agp_amd_attach, NULL,
	agp_amd_activate
};

struct cfdriver amdagp_cd = {
	NULL, "amdagp", DV_DULL
};

const struct agp_methods agp_amd_methods = {
	agp_amd_bind_page,
	agp_amd_unbind_page,
	agp_amd_flush_tlb,
};


struct agp_amd_gatt *
agp_amd_alloc_gatt(bus_dma_tag_t dmat, bus_size_t apsize)
{
	bus_size_t entries = apsize >> AGP_PAGE_SHIFT;
	struct agp_amd_gatt *gatt;
	int i, npages;
	caddr_t vdir;

	gatt = malloc(sizeof(struct agp_amd_gatt), M_AGP, M_NOWAIT);
	if (!gatt)
		return (0);
	gatt->ag_size = AGP_PAGE_SIZE + entries * sizeof(u_int32_t);

	if (agp_alloc_dmamem(dmat, gatt->ag_size, &gatt->ag_dmamap,
	    &gatt->ag_pdir, &gatt->ag_dmaseg) != 0) {
		printf("failed to allocate GATT\n");
		free(gatt, M_AGP, sizeof *gatt);
		return (NULL);
	}

	if (bus_dmamem_map(dmat, &gatt->ag_dmaseg, 1, gatt->ag_size,
	    &vdir, BUS_DMA_NOWAIT) != 0) {
		printf("failed to map GATT\n");
		agp_free_dmamem(dmat, gatt->ag_size, gatt->ag_dmamap,
		    &gatt->ag_dmaseg);
		free(gatt, M_AGP, sizeof *gatt);
		return (NULL);
	}

	gatt->ag_vdir = (u_int32_t *)vdir;
	gatt->ag_entries = entries;
	gatt->ag_virtual = (u_int32_t *)(vdir + AGP_PAGE_SIZE);
	gatt->ag_physical = gatt->ag_pdir + AGP_PAGE_SIZE;

	/*
	 * Map the pages of the GATT into the page directory.
	 */
	npages = ((gatt->ag_size - 1) >> AGP_PAGE_SHIFT);

	for (i = 0; i < npages; i++)
		gatt->ag_vdir[i] = (gatt->ag_physical + i * AGP_PAGE_SIZE) | 1;

	/*
	 * Make sure the chipset can see everything.
	 */
	agp_flush_cache();

	return (gatt);
}

int
agp_amd_probe(struct device *parent, void *match, void *aux)
{
	struct agp_attach_args	*aa = aux;
	struct pci_attach_args	*pa = aa->aa_pa;

	/* Must be a pchb */
	if (agpbus_probe(aa) == 1 && PCI_VENDOR(pa->pa_id) == PCI_VENDOR_AMD &&
	    (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_SC751_SC ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_761_PCHB ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_762_PCHB))
			return (1);
	return (0);
}

void
agp_amd_attach(struct device *parent, struct device *self, void *aux)
{
	struct agp_amd_softc	*asc = (struct agp_amd_softc *)self;
	struct agp_attach_args	*aa = aux;
	struct pci_attach_args	*pa = aa->aa_pa;
	struct agp_amd_gatt	*gatt;
	pcireg_t		 reg;
	int			 error;

	asc->asc_pc = pa->pa_pc;
	asc->asc_tag = pa->pa_tag;

	if (pci_mapreg_info(pa->pa_pc, pa->pa_tag, AGP_APBASE,
	    PCI_MAPREG_TYPE_MEM, &asc->asc_apaddr, NULL, NULL) != 0) {
		printf(": can't get aperture info\n");
		return;
	}

	error = pci_mapreg_map(pa, AGP_AMD751_REGISTERS,
	     PCI_MAPREG_TYPE_MEM, 0, &asc->iot, &asc->ioh, NULL, NULL, 0);
	if (error != 0) {
		printf("can't map AGP registers\n");
		return;
	}

	asc->asc_apsize = agp_amd_get_aperture(asc);

	for (;;) {
		gatt = agp_amd_alloc_gatt(pa->pa_dmat, asc->asc_apsize);
		if (gatt != NULL)
			break;

		/*
		 * almost certainly error allocating contiguous dma memory
		 * so reduce aperture so that the gatt size reduces.
		 */
		asc->asc_apsize /= 2;
		if (agp_amd_set_aperture(asc, asc->asc_apsize)) {
			printf(": failed to set aperture\n");
			return;
		}
	}
	asc->gatt = gatt;

	/* Install the gatt. */
	WRITE4(AGP_AMD751_ATTBASE, gatt->ag_physical);

	/* Enable synchronisation between host and agp. */
	reg = pci_conf_read(asc->asc_pc, asc->asc_tag, AGP_AMD751_MODECTRL);
	reg &= ~0x00ff00ff;
	reg |= (AGP_AMD751_MODECTRL_SYNEN) | (AGP_AMD751_MODECTRL2_GPDCE << 16);
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_AMD751_MODECTRL, reg);
	/* Enable the TLB and flush */
	WRITE2(AGP_AMD751_STATUS,
	    READ2(AGP_AMD751_STATUS) | AGP_AMD751_STATUS_GCE);
	agp_amd_flush_tlb(asc);

	asc->agpdev = (struct agp_softc *)agp_attach_bus(pa, &agp_amd_methods,
	    asc->asc_apaddr, asc->asc_apsize, &asc->dev);
	return;
}

int
agp_amd_activate(struct device *arg, int act)
{
	struct agp_amd_softc *asc = (struct agp_amd_softc *)arg;

	switch (act) {
	case DVACT_SUSPEND:
		agp_amd_save(asc);
		break;
	case DVACT_RESUME:
		agp_amd_restore(asc);
		break;
	}

	return (0);
}

void
agp_amd_save(struct agp_amd_softc *asc)
{
	asc->asc_apctrl = pci_conf_read(asc->asc_pc, asc->asc_tag,
	    AGP_AMD751_APCTRL);
	asc->asc_modectrl = pci_conf_read(asc->asc_pc, asc->asc_tag,
	    AGP_AMD751_MODECTRL);
	asc->asc_status = READ2(AGP_AMD751_STATUS);
}

void
agp_amd_restore(struct agp_amd_softc *asc)
{

	/* restore aperture size */
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_AMD751_APCTRL,
	    asc->asc_apctrl);

	/* Install the gatt. */
	WRITE4(AGP_AMD751_ATTBASE, asc->gatt->ag_physical);

	/* Reenable synchronisation between host and agp. */
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_AMD751_MODECTRL,
	    asc->asc_modectrl);
	/* Enable the TLB and flush */
	WRITE2(AGP_AMD751_STATUS, asc->asc_status);
	agp_amd_flush_tlb(asc);
}

bus_size_t
agp_amd_get_aperture(void *sc)
{
	struct agp_amd_softc	*asc = sc;
	int			 vas;

	vas = (pci_conf_read(asc->asc_pc, asc->asc_tag,
	    AGP_AMD751_APCTRL) & 0x06);
	vas >>= 1;
	/*
	 * The aperture size is equal to 32M<<vas.
	 */
	return ((32 * 1024 * 1024) << vas);
}

int
agp_amd_set_aperture(void *sc, bus_size_t aperture)
{
	struct agp_amd_softc	*asc = sc;
	int			 vas;
	pcireg_t		 reg;

	/*
	 * Check for a power of two and make sure its within the
	 * programmable range.
	 */
	if (aperture & (aperture - 1)
	    || aperture < 32*1024*1024
	    || aperture > 2U*1024*1024*1024)
		return (EINVAL);

	vas = ffs(aperture / 32*1024*1024) - 1;

	reg = pci_conf_read(asc->asc_pc, asc->asc_tag, AGP_AMD751_APCTRL);
	reg = (reg & ~0x06) | (vas << 1);
	pci_conf_write(asc->asc_pc, asc->asc_tag, AGP_AMD751_APCTRL, reg);

	return (0);
}

void
agp_amd_bind_page(void *sc, bus_size_t offset, paddr_t physical, int flags)
{
	struct agp_amd_softc	*asc = sc;

	asc->gatt->ag_virtual[(offset - asc->asc_apaddr) >> AGP_PAGE_SHIFT] =
	    physical | 1;
}

void
agp_amd_unbind_page(void *sc, bus_size_t offset)
{
	struct agp_amd_softc	*asc = sc;

	asc->gatt->ag_virtual[(offset - asc->asc_apaddr) >> AGP_PAGE_SHIFT] = 0;
}

void
agp_amd_flush_tlb(void *sc)
{
	struct agp_amd_softc	*asc = sc;

	/* Set the cache invalidate bit and wait for the chipset to clear */
	WRITE4(AGP_AMD751_TLBCTRL, 1);
	do {
		DELAY(1);
	} while (READ4(AGP_AMD751_TLBCTRL));
}
