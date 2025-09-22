/*	$OpenBSD: agp_sis.c,v 1.21 2024/05/24 06:02:53 jsg Exp $	*/
/*	$NetBSD: agp_sis.c,v 1.2 2001/09/15 00:25:00 thorpej Exp $	*/

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
 *	$FreeBSD: src/sys/pci/agp_sis.c,v 1.3 2001/07/05 21:28:47 jhb Exp $
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

struct agp_sis_softc {
	struct device		 dev;
	struct agp_softc	*agpdev;
	struct agp_gatt		*gatt;
	pci_chipset_tag_t	 ssc_pc;
	pcitag_t		 ssc_tag;
	bus_addr_t		 ssc_apaddr;
	bus_size_t		 ssc_apsize;
	pcireg_t		 ssc_winctrl; /* saved over suspend/resume */
};

void	agp_sis_attach(struct device *, struct device *, void *);
int	agp_sis_activate(struct device *, int);
void	agp_sis_save(struct agp_sis_softc *);
void	agp_sis_restore(struct agp_sis_softc *);
int	agp_sis_probe(struct device *, void *, void *);
bus_size_t agp_sis_get_aperture(void *);
int	agp_sis_set_aperture(void *, bus_size_t);
void	agp_sis_bind_page(void *, bus_addr_t, paddr_t, int);
void	agp_sis_unbind_page(void *, bus_addr_t);
void	agp_sis_flush_tlb(void *);

const struct cfattach sisagp_ca = {
	sizeof(struct agp_sis_softc), agp_sis_probe, agp_sis_attach,
	NULL, agp_sis_activate
};

struct cfdriver sisagp_cd = {
	NULL, "sisagp", DV_DULL
};

const struct agp_methods agp_sis_methods = {
	agp_sis_bind_page,
	agp_sis_unbind_page,
	agp_sis_flush_tlb,
};

int
agp_sis_probe(struct device *parent, void *match, void *aux)
{
	struct agp_attach_args	*aa = aux;
	struct pci_attach_args	*pa = aa->aa_pa;

	/* Must be a pchb, don't attach to iommu-style agp devs */
	if (agpbus_probe(aa) == 1 && PCI_VENDOR(pa->pa_id) == PCI_VENDOR_SIS &&
	   PCI_PRODUCT(pa->pa_id) != PCI_PRODUCT_SIS_755 &&
	   PCI_PRODUCT(pa->pa_id) != PCI_PRODUCT_SIS_760)
		return (1);
	return (0);
}


void
agp_sis_attach(struct device *parent, struct device *self, void *aux)
{
	struct agp_sis_softc	*ssc = (struct agp_sis_softc *)self;
	struct agp_attach_args	*aa = aux;
	struct pci_attach_args	*pa = aa->aa_pa;
	struct agp_gatt		*gatt;
	pcireg_t		 reg;

	if (pci_mapreg_info(pa->pa_pc, pa->pa_tag, AGP_APBASE,
	    PCI_MAPREG_TYPE_MEM, &ssc->ssc_apaddr, NULL, NULL) != 0) {
		printf(": can't get aperture info\n");
		return;
	}

	ssc->ssc_pc = pa->pa_pc;
	ssc->ssc_tag = pa->pa_tag;
	ssc->ssc_apsize = agp_sis_get_aperture(ssc);

	for (;;) {
		gatt = agp_alloc_gatt(pa->pa_dmat, ssc->ssc_apsize);
		if (gatt != NULL)
			break;

		/*
		 * Probably failed to alloc contiguous memory. Try reducing the
		 * aperture so that the gatt size reduces.
		 */
		ssc->ssc_apsize /= 2;
		if (agp_sis_set_aperture(ssc, ssc->ssc_apsize)) {
			printf("can't set aperture size\n");
			return;
		}
	}
	ssc->gatt = gatt;

	/* Install the gatt. */
	pci_conf_write(ssc->ssc_pc, ssc->ssc_tag, AGP_SIS_ATTBASE,
	    gatt->ag_physical);
	
	/* Enable the aperture and auto-tlb-inval */
	reg = pci_conf_read(ssc->ssc_pc, ssc->ssc_tag, AGP_SIS_WINCTRL);
	reg |= (0x05 << 24) | 3;
	pci_conf_write(ssc->ssc_pc, ssc->ssc_tag, AGP_SIS_WINCTRL, reg);

	ssc->agpdev = (struct agp_softc *)agp_attach_bus(pa, &agp_sis_methods,
	    ssc->ssc_apaddr, ssc->ssc_apsize, &ssc->dev);
	return;
}

int
agp_sis_activate(struct device *arg, int act)
{
	struct agp_sis_softc *ssc = (struct agp_sis_softc *)arg;

	switch (act) {
	case DVACT_SUSPEND:
		agp_sis_save(ssc);
		break;
	case DVACT_RESUME:
		agp_sis_restore(ssc);
		break;
	}

	return (0);
}

void
agp_sis_save(struct agp_sis_softc *ssc)
{
	ssc->ssc_winctrl = pci_conf_read(ssc->ssc_pc, ssc->ssc_tag,
	    AGP_SIS_WINCTRL);
}

void
agp_sis_restore(struct agp_sis_softc *ssc)
{
	/* Install the gatt. */
	pci_conf_write(ssc->ssc_pc, ssc->ssc_tag, AGP_SIS_ATTBASE,
	    ssc->gatt->ag_physical);
	
	/*
	 * Enable the aperture, reset the aperture size and enable and
	 * auto-tlb-inval.
	 */
	pci_conf_write(ssc->ssc_pc, ssc->ssc_tag, AGP_SIS_WINCTRL,
	    ssc->ssc_winctrl);
}

bus_size_t
agp_sis_get_aperture(void *sc)
{
	struct agp_sis_softc	*ssc = sc;
	int			 gws;

	/*
	 * The aperture size is equal to 4M<<gws.
	 */
	gws = (pci_conf_read(ssc->ssc_pc, ssc->ssc_tag,
	    AGP_SIS_WINCTRL)&0x70) >> 4;
	return ((4 * 1024 * 1024) << gws);
}

int
agp_sis_set_aperture(void *sc, bus_size_t aperture)
{
	struct agp_sis_softc	*ssc = sc;
	int gws;
	pcireg_t reg;

	/*
	 * Check for a power of two and make sure its within the
	 * programmable range.
	 */
	if (aperture & (aperture - 1)
	    || aperture < 4*1024*1024
	    || aperture > 256*1024*1024)
		return (EINVAL);

	gws = ffs(aperture / 4*1024*1024) - 1;

	reg = pci_conf_read(ssc->ssc_pc, ssc->ssc_tag, AGP_SIS_WINCTRL);	
	reg &= ~0x00000070;
	reg |= gws << 4;
	pci_conf_write(ssc->ssc_pc, ssc->ssc_tag, AGP_SIS_WINCTRL, reg);

	return (0);
}

void
agp_sis_bind_page(void *sc, bus_addr_t offset, paddr_t physical, int flags)
{
	struct agp_sis_softc	*ssc = sc;

	ssc->gatt->ag_virtual[(offset - ssc->ssc_apaddr) >> AGP_PAGE_SHIFT] =
	    physical;
}

void
agp_sis_unbind_page(void *sc, bus_addr_t offset)
{
	struct agp_sis_softc	*ssc = sc;

	ssc->gatt->ag_virtual[(offset - ssc->ssc_apaddr) >> AGP_PAGE_SHIFT] = 0;
}

void
agp_sis_flush_tlb(void *sc)
{
	struct agp_sis_softc	*ssc = sc;
	pcireg_t		 reg;

	reg = pci_conf_read(ssc->ssc_pc, ssc->ssc_tag, AGP_SIS_TLBFLUSH);
	reg &= 0xffffff00;
	reg |= 0x02;
	pci_conf_write(ssc->ssc_pc, ssc->ssc_tag, AGP_SIS_TLBFLUSH, reg);
}
