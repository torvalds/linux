/*	$OpenBSD: pci.c,v 1.131 2025/09/16 12:18:10 hshoexer Exp $	*/
/*	$NetBSD: pci.c,v 1.31 1997/06/06 23:48:04 thorpej Exp $	*/

/*
 * Copyright (c) 1995, 1996 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1994 Charles Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * PCI bus autoconfiguration.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/ppbreg.h>

int pcimatch(struct device *, void *, void *);
void pciattach(struct device *, struct device *, void *);
int pcidetach(struct device *, int);
int pciactivate(struct device *, int);
void pci_suspend(struct pci_softc *);
void pci_powerdown(struct pci_softc *);
void pci_resume(struct pci_softc *);

struct msix_vector {
	uint32_t mv_ma;
	uint32_t mv_mau32;
	uint32_t mv_md;
	uint32_t mv_vc;
};

#define NMAPREG			((PCI_MAPREG_END - PCI_MAPREG_START) / \
				    sizeof(pcireg_t))
struct pci_dev {
	struct device *pd_dev;
	LIST_ENTRY(pci_dev) pd_next;
	pcitag_t pd_tag;        /* pci register tag */
	pcireg_t pd_csr;
	pcireg_t pd_bhlc;
	pcireg_t pd_int;
	pcireg_t pd_map[NMAPREG];
	pcireg_t pd_mask[NMAPREG];
	pcireg_t pd_msi_mc;
	pcireg_t pd_msi_ma;
	pcireg_t pd_msi_mau32;
	pcireg_t pd_msi_md;
	pcireg_t pd_msix_mc;
	struct msix_vector *pd_msix_table;
	int pd_pmcsr_state;
	int pd_vga_decode;
};

#ifdef APERTURE
extern int allowaperture;
#endif

const struct cfattach pci_ca = {
	sizeof(struct pci_softc), pcimatch, pciattach, pcidetach, pciactivate
};

struct cfdriver pci_cd = {
	NULL, "pci", DV_DULL, CD_COCOVM
};

int	pci_ndomains;

struct proc *pci_vga_proc;
struct pci_softc *pci_vga_pci;
pcitag_t pci_vga_tag;

int	pci_dopm;

int	pciprint(void *, const char *);
int	pcisubmatch(struct device *, void *, void *);

#ifdef PCI_MACHDEP_ENUMERATE_BUS
#define pci_enumerate_bus PCI_MACHDEP_ENUMERATE_BUS
#else
int pci_enumerate_bus(struct pci_softc *,
    int (*)(struct pci_attach_args *), struct pci_attach_args *);
#endif
int	pci_reserve_resources(struct pci_attach_args *);
int	pci_primary_vga(struct pci_attach_args *);

/*
 * Important note about PCI-ISA bridges:
 *
 * Callbacks are used to configure these devices so that ISA/EISA bridges
 * can attach their child busses after PCI configuration is done.
 *
 * This works because:
 *	(1) there can be at most one ISA/EISA bridge per PCI bus, and
 *	(2) any ISA/EISA bridges must be attached to primary PCI
 *	    busses (i.e. bus zero).
 *
 * That boils down to: there can only be one of these outstanding
 * at a time, it is cleared when configuring PCI bus 0 before any
 * subdevices have been found, and it is run after all subdevices
 * of PCI bus 0 have been found.
 *
 * This is needed because there are some (legacy) PCI devices which
 * can show up as ISA/EISA devices as well (the prime example of which
 * are VGA controllers).  If you attach ISA from a PCI-ISA/EISA bridge,
 * and the bridge is seen before the video board is, the board can show
 * up as an ISA device, and that can (bogusly) complicate the PCI device's
 * attach code, or make the PCI device not be properly attached at all.
 *
 * We use the generic config_defer() facility to achieve this.
 */

int
pcimatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct pcibus_attach_args *pba = aux;

	if (strcmp(pba->pba_busname, cf->cf_driver->cd_name))
		return (0);

	/* Check the locators */
	if (cf->pcibuscf_bus != PCIBUS_UNK_BUS &&
	    cf->pcibuscf_bus != pba->pba_bus)
		return (0);

	/* sanity */
	if (pba->pba_bus < 0 || pba->pba_bus > 255)
		return (0);

	/*
	 * XXX check other (hardware?) indicators
	 */

	return (1);
}

void
pciattach(struct device *parent, struct device *self, void *aux)
{
	struct pcibus_attach_args *pba = aux;
	struct pci_softc *sc = (struct pci_softc *)self;

	pci_attach_hook(parent, self, pba);

	printf("\n");

	LIST_INIT(&sc->sc_devs);

	sc->sc_iot = pba->pba_iot;
	sc->sc_memt = pba->pba_memt;
	sc->sc_dmat = pba->pba_dmat;
	sc->sc_pc = pba->pba_pc;
	sc->sc_flags = pba->pba_flags;
	sc->sc_ioex = pba->pba_ioex;
	sc->sc_memex = pba->pba_memex;
	sc->sc_pmemex = pba->pba_pmemex;
	sc->sc_busex = pba->pba_busex;
	sc->sc_domain = pba->pba_domain;
	sc->sc_bus = pba->pba_bus;
	sc->sc_bridgetag = pba->pba_bridgetag;
	sc->sc_bridgeih = pba->pba_bridgeih;
	sc->sc_maxndevs = pci_bus_maxdevs(pba->pba_pc, pba->pba_bus);
	sc->sc_intrswiz = pba->pba_intrswiz;
	sc->sc_intrtag = pba->pba_intrtag;

	/* Reserve our own bus number. */
	if (sc->sc_busex)
		extent_alloc_region(sc->sc_busex, sc->sc_bus, 1, EX_NOWAIT);

	pci_enumerate_bus(sc, pci_reserve_resources, NULL);

	/* Find the VGA device that's currently active. */
	if (pci_enumerate_bus(sc, pci_primary_vga, NULL))
		pci_vga_pci = sc;

	pci_enumerate_bus(sc, NULL, NULL);
}

int
pcidetach(struct device *self, int flags)
{
	return pci_detach_devices((struct pci_softc *)self, flags);
}

int
pciactivate(struct device *self, int act)
{
	int rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		pci_suspend((struct pci_softc *)self);
		break;
	case DVACT_RESUME:
		pci_resume((struct pci_softc *)self);
		rv = config_activate_children(self, act);
		break;
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
		pci_powerdown((struct pci_softc *)self);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

void
pci_suspend(struct pci_softc *sc)
{
	struct pci_dev *pd;
	pcireg_t bhlc, reg;
	int off, i;

	LIST_FOREACH(pd, &sc->sc_devs, pd_next) {
		/*
		 * Only handle header type 0 here; PCI-PCI bridges and
		 * CardBus bridges need special handling, which will
		 * be done in their specific drivers.
		 */
		bhlc = pci_conf_read(sc->sc_pc, pd->pd_tag, PCI_BHLC_REG);
		if (PCI_HDRTYPE_TYPE(bhlc) != 0)
			continue;

		/* Save registers that may get lost. */
		for (i = 0; i < NMAPREG; i++)
			pd->pd_map[i] = pci_conf_read(sc->sc_pc, pd->pd_tag,
			    PCI_MAPREG_START + (i * 4));
		pd->pd_csr = pci_conf_read(sc->sc_pc, pd->pd_tag,
		    PCI_COMMAND_STATUS_REG);
		pd->pd_bhlc = pci_conf_read(sc->sc_pc, pd->pd_tag,
		    PCI_BHLC_REG);
		pd->pd_int = pci_conf_read(sc->sc_pc, pd->pd_tag,
		    PCI_INTERRUPT_REG);

		if (pci_get_capability(sc->sc_pc, pd->pd_tag,
		    PCI_CAP_MSI, &off, &reg)) {
			pd->pd_msi_ma = pci_conf_read(sc->sc_pc, pd->pd_tag,
			    off + PCI_MSI_MA);
			if (reg & PCI_MSI_MC_C64) {
				pd->pd_msi_mau32 = pci_conf_read(sc->sc_pc,
				    pd->pd_tag, off + PCI_MSI_MAU32);
				pd->pd_msi_md = pci_conf_read(sc->sc_pc,
				    pd->pd_tag, off + PCI_MSI_MD64);
			} else {
				pd->pd_msi_md = pci_conf_read(sc->sc_pc,
				    pd->pd_tag, off + PCI_MSI_MD32);
			}
			pd->pd_msi_mc = reg;
		}

		pci_suspend_msix(sc->sc_pc, pd->pd_tag, sc->sc_memt,
		    &pd->pd_msix_mc, pd->pd_msix_table);
	}
}

void
pci_powerdown(struct pci_softc *sc)
{
	struct pci_dev *pd;
	pcireg_t bhlc;

	LIST_FOREACH(pd, &sc->sc_devs, pd_next) {
		/*
		 * Only handle header type 0 here; PCI-PCI bridges and
		 * CardBus bridges need special handling, which will
		 * be done in their specific drivers.
		 */
		bhlc = pci_conf_read(sc->sc_pc, pd->pd_tag, PCI_BHLC_REG);
		if (PCI_HDRTYPE_TYPE(bhlc) != 0)
			continue;

		if (pci_dopm) {
			/*
			 * Place the device into the lowest possible
			 * power state.
			 */
			pd->pd_pmcsr_state = pci_get_powerstate(sc->sc_pc,
			    pd->pd_tag);
			pci_set_powerstate(sc->sc_pc, pd->pd_tag,
			    pci_min_powerstate(sc->sc_pc, pd->pd_tag));
		}
	}
}

void
pci_resume(struct pci_softc *sc)
{
	struct pci_dev *pd;
	pcireg_t bhlc, reg;
	int off, i;

	LIST_FOREACH(pd, &sc->sc_devs, pd_next) {
		/*
		 * Only handle header type 0 here; PCI-PCI bridges and
		 * CardBus bridges need special handling, which will
		 * be done in their specific drivers.
		 */
		bhlc = pci_conf_read(sc->sc_pc, pd->pd_tag, PCI_BHLC_REG);
		if (PCI_HDRTYPE_TYPE(bhlc) != 0)
			continue;

		/* Restore power. */
		if (pci_dopm)
			pci_set_powerstate(sc->sc_pc, pd->pd_tag,
			    pd->pd_pmcsr_state);

		/* Restore the registers saved above. */
		for (i = 0; i < NMAPREG; i++)
			pci_conf_write(sc->sc_pc, pd->pd_tag,
			    PCI_MAPREG_START + (i * 4), pd->pd_map[i]);
		reg = pci_conf_read(sc->sc_pc, pd->pd_tag,
		    PCI_COMMAND_STATUS_REG);
		pci_conf_write(sc->sc_pc, pd->pd_tag, PCI_COMMAND_STATUS_REG,
		    (reg & 0xffff0000) | (pd->pd_csr & 0x0000ffff));
		pci_conf_write(sc->sc_pc, pd->pd_tag, PCI_BHLC_REG,
		    pd->pd_bhlc);
		pci_conf_write(sc->sc_pc, pd->pd_tag, PCI_INTERRUPT_REG,
		    pd->pd_int);

		if (pci_get_capability(sc->sc_pc, pd->pd_tag,
		    PCI_CAP_MSI, &off, &reg)) {
			pci_conf_write(sc->sc_pc, pd->pd_tag,
			    off + PCI_MSI_MA, pd->pd_msi_ma);
			if (reg & PCI_MSI_MC_C64) {
				pci_conf_write(sc->sc_pc, pd->pd_tag,
				    off + PCI_MSI_MAU32, pd->pd_msi_mau32);
				pci_conf_write(sc->sc_pc, pd->pd_tag,
				    off + PCI_MSI_MD64, pd->pd_msi_md);
			} else {
				pci_conf_write(sc->sc_pc, pd->pd_tag,
				    off + PCI_MSI_MD32, pd->pd_msi_md);
			}
			pci_conf_write(sc->sc_pc, pd->pd_tag,
			    off + PCI_MSI_MC, pd->pd_msi_mc);
		}

		pci_resume_msix(sc->sc_pc, pd->pd_tag, sc->sc_memt,
		    pd->pd_msix_mc, pd->pd_msix_table);
	}
}

int
pciprint(void *aux, const char *pnp)
{
	struct pci_attach_args *pa = aux;
	char devinfo[256];

	if (pnp) {
		pci_devinfo(pa->pa_id, pa->pa_class, 1, devinfo,
		    sizeof devinfo);
		printf("%s at %s", devinfo, pnp);
	}
	printf(" dev %d function %d", pa->pa_device, pa->pa_function);
	if (!pnp) {
		pci_devinfo(pa->pa_id, pa->pa_class, 0, devinfo,
		    sizeof devinfo);
		printf(" %s", devinfo);
	}

	return (UNCONF);
}

int
pcisubmatch(struct device *parent, void *match,  void *aux)
{
	struct cfdata *cf = match;
	struct pci_attach_args *pa = aux;

	if (cf->pcicf_dev != PCI_UNK_DEV &&
	    cf->pcicf_dev != pa->pa_device)
		return (0);
	if (cf->pcicf_function != PCI_UNK_FUNCTION &&
	    cf->pcicf_function != pa->pa_function)
		return (0);

	return ((*cf->cf_attach->ca_match)(parent, match, aux));
}

int
pci_probe_device(struct pci_softc *sc, pcitag_t tag,
    int (*match)(struct pci_attach_args *), struct pci_attach_args *pap)
{
	pci_chipset_tag_t pc = sc->sc_pc;
	struct pci_attach_args pa;
	struct pci_dev *pd;
	pcireg_t id, class, intr, bhlcr, cap;
	int pin, bus, device, function;
	int off, ret = 0;
	uint64_t addr;

	pci_decompose_tag(pc, tag, &bus, &device, &function);

	bhlcr = pci_conf_read(pc, tag, PCI_BHLC_REG);
	if (PCI_HDRTYPE_TYPE(bhlcr) > 2)
		return (0);

	id = pci_conf_read(pc, tag, PCI_ID_REG);
	class = pci_conf_read(pc, tag, PCI_CLASS_REG);

	/* Invalid vendor ID value? */
	if (PCI_VENDOR(id) == PCI_VENDOR_INVALID)
		return (0);
	/* XXX Not invalid, but we've done this ~forever. */
	if (PCI_VENDOR(id) == 0)
		return (0);

	pa.pa_iot = sc->sc_iot;
	pa.pa_memt = sc->sc_memt;
	pa.pa_dmat = sc->sc_dmat;
	pa.pa_pc = pc;
	pa.pa_ioex = sc->sc_ioex;
	pa.pa_memex = sc->sc_memex;
	pa.pa_pmemex = sc->sc_pmemex;
	pa.pa_busex = sc->sc_busex;
	pa.pa_domain = sc->sc_domain;
	pa.pa_bus = bus;
	pa.pa_device = device;
	pa.pa_function = function;
	pa.pa_tag = tag;
	pa.pa_id = id;
	pa.pa_class = class;
	pa.pa_bridgetag = sc->sc_bridgetag;
	pa.pa_bridgeih = sc->sc_bridgeih;

	/* This is a simplification of the NetBSD code.
	   We don't support turning off I/O or memory
	   on broken hardware. <csapuntz@stanford.edu> */
	pa.pa_flags = sc->sc_flags;
	pa.pa_flags |= PCI_FLAGS_IO_ENABLED | PCI_FLAGS_MEM_ENABLED;

	if (sc->sc_bridgetag == NULL) {
		pa.pa_intrswiz = 0;
		pa.pa_intrtag = tag;
	} else {
		pa.pa_intrswiz = sc->sc_intrswiz + device;
		pa.pa_intrtag = sc->sc_intrtag;
	}

	intr = pci_conf_read(pc, tag, PCI_INTERRUPT_REG);

	pin = PCI_INTERRUPT_PIN(intr);
	pa.pa_rawintrpin = pin;
	if (pin == PCI_INTERRUPT_PIN_NONE) {
		/* no interrupt */
		pa.pa_intrpin = 0;
	} else {
		/*
		 * swizzle it based on the number of busses we're
		 * behind and our device number.
		 */
		pa.pa_intrpin = 	/* XXX */
		    ((pin + pa.pa_intrswiz - 1) % 4) + 1;
	}
	pa.pa_intrline = PCI_INTERRUPT_LINE(intr);

	if (pci_get_ht_capability(pc, tag, PCI_HT_CAP_MSI, &off, &cap)) {
		/*
		 * XXX Should we enable MSI mapping ourselves on
		 * systems that have it disabled?
		 */
		if (cap & PCI_HT_MSI_ENABLED) {
			if ((cap & PCI_HT_MSI_FIXED) == 0) {
				addr = pci_conf_read(pc, tag,
				    off + PCI_HT_MSI_ADDR);
				addr |= (uint64_t)pci_conf_read(pc, tag,
				    off + PCI_HT_MSI_ADDR_HI32) << 32;
			} else
				addr = PCI_HT_MSI_FIXED_ADDR;

			/* 
			 * XXX This will fail to enable MSI on systems
			 * that don't use the canonical address.
			 */
			if (addr == PCI_HT_MSI_FIXED_ADDR)
				pa.pa_flags |= PCI_FLAGS_MSI_ENABLED;
		}
	}

	/*
	 * Give the MD code a chance to alter pci_attach_args and/or
	 * skip devices.
	 */
	if (pci_probe_device_hook(pc, &pa) != 0)
		return (0);

	if (match != NULL) {
		ret = (*match)(&pa);
		if (ret != 0 && pap != NULL)
			*pap = pa;
	} else {
		pcireg_t address, csr;
		int i, reg, reg_start, reg_end;
		int s;

		pd = malloc(sizeof *pd, M_DEVBUF, M_ZERO | M_WAITOK);
		pd->pd_tag = tag;
		LIST_INSERT_HEAD(&sc->sc_devs, pd, pd_next);

		switch (PCI_HDRTYPE_TYPE(bhlcr)) {
		case 0:
			reg_start = PCI_MAPREG_START;
			reg_end = PCI_MAPREG_END;
			break;
		case 1: /* PCI-PCI bridge */
			reg_start = PCI_MAPREG_START;
			reg_end = PCI_MAPREG_PPB_END;
			break;
		case 2: /* PCI-CardBus bridge */
			reg_start = PCI_MAPREG_START;
			reg_end = PCI_MAPREG_PCB_END;
			break;
		default:
			return (0);
		}

		pd->pd_msix_table = pci_alloc_msix_table(sc->sc_pc, pd->pd_tag);

		s = splhigh();
		csr = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
		if (csr & (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE))
			pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG, csr &
			    ~(PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE));

		for (reg = reg_start, i = 0; reg < reg_end; reg += 4, i++) {
			address = pci_conf_read(pc, tag, reg);
			pci_conf_write(pc, tag, reg, 0xffffffff);
			pd->pd_mask[i] = pci_conf_read(pc, tag, reg);
			pci_conf_write(pc, tag, reg, address);
		}

		if (csr & (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE))
			pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG, csr);
		splx(s);

		if ((PCI_CLASS(class) == PCI_CLASS_DISPLAY &&
		    PCI_SUBCLASS(class) == PCI_SUBCLASS_DISPLAY_VGA) ||
		    (PCI_CLASS(class) == PCI_CLASS_PREHISTORIC &&
		    PCI_SUBCLASS(class) == PCI_SUBCLASS_PREHISTORIC_VGA))
			pd->pd_vga_decode = 1;

		pd->pd_dev = config_found_sm(&sc->sc_dev, &pa, pciprint,
		    pcisubmatch);
		if (pd->pd_dev)
			pci_dev_postattach(pd->pd_dev, &pa);
	}

	return (ret);
}

int
pci_detach_devices(struct pci_softc *sc, int flags)
{
	struct pci_dev *pd, *next;
	int ret;

	ret = config_detach_children(&sc->sc_dev, flags);
	if (ret != 0)
		return (ret);

	for (pd = LIST_FIRST(&sc->sc_devs); pd != NULL; pd = next) {
		pci_free_msix_table(sc->sc_pc, pd->pd_tag, pd->pd_msix_table);
		next = LIST_NEXT(pd, pd_next);
		free(pd, M_DEVBUF, sizeof *pd);
	}
	LIST_INIT(&sc->sc_devs);

	return (0);
}

int
pci_get_capability(pci_chipset_tag_t pc, pcitag_t tag, int capid,
    int *offset, pcireg_t *value)
{
	pcireg_t reg;
	unsigned int ofs;

	reg = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
	if (!(reg & PCI_STATUS_CAPLIST_SUPPORT))
		return (0);

	/* Determine the Capability List Pointer register to start with. */
	reg = pci_conf_read(pc, tag, PCI_BHLC_REG);
	switch (PCI_HDRTYPE_TYPE(reg)) {
	case 0:	/* standard device header */
	case 1: /* PCI-PCI bridge header */
		ofs = PCI_CAPLISTPTR_REG;
		break;
	case 2:	/* PCI-CardBus bridge header */
		ofs = PCI_CARDBUS_CAPLISTPTR_REG;
		break;
	default:
		return (0);
	}

	ofs = PCI_CAPLIST_PTR(pci_conf_read(pc, tag, ofs));
	while (ofs != 0) {
		/*
		 * Some devices, like parts of the NVIDIA C51 chipset,
		 * have a broken Capabilities List.  So we need to do
		 * a sanity check here.
		 */
		if ((ofs & 3) || (ofs < 0x40))
			return (0);
		reg = pci_conf_read(pc, tag, ofs);
		if (PCI_CAPLIST_CAP(reg) == capid) {
			if (offset)
				*offset = ofs;
			if (value)
				*value = reg;
			return (1);
		}
		ofs = PCI_CAPLIST_NEXT(reg);
	}

	return (0);
}

int
pci_get_ht_capability(pci_chipset_tag_t pc, pcitag_t tag, int capid,
    int *offset, pcireg_t *value)
{
	pcireg_t reg;
	unsigned int ofs;

	if (pci_get_capability(pc, tag, PCI_CAP_HT, &ofs, NULL) == 0)
		return (0);

	while (ofs != 0) {
#ifdef DIAGNOSTIC
		if ((ofs & 3) || (ofs < 0x40))
			panic("pci_get_ht_capability");
#endif
		reg = pci_conf_read(pc, tag, ofs);
		if (PCI_HT_CAP(reg) == capid) {
			if (offset)
				*offset = ofs;
			if (value)
				*value = reg;
			return (1);
		}
		ofs = PCI_CAPLIST_NEXT(reg);
	}

	return (0);
}

int
pci_get_ext_capability(pci_chipset_tag_t pc, pcitag_t tag, int capid,
    int *offset, pcireg_t *value)
{
	pcireg_t reg;
	unsigned int ofs;

	/* Make sure this is a PCI Express device. */
	if (pci_get_capability(pc, tag, PCI_CAP_PCIEXPRESS, NULL, NULL) == 0)
		return (0);

	/* Scan PCI Express extended capabilities. */
	ofs = PCI_PCIE_ECAP;
	while (ofs != 0) {
#ifdef DIAGNOSTIC
		if ((ofs & 3) || (ofs < PCI_PCIE_ECAP))
			panic("pci_get_ext_capability");
#endif
		reg = pci_conf_read(pc, tag, ofs);
		if (PCI_PCIE_ECAP_ID(reg) == capid) {
			if (offset)
				*offset = ofs;
			if (value)
				*value = reg;
			return (1);
		}
		ofs = PCI_PCIE_ECAP_NEXT(reg);
	}

	return (0);
}

uint16_t
pci_requester_id(pci_chipset_tag_t pc, pcitag_t tag)
{
	int bus, dev, func;

	pci_decompose_tag(pc, tag, &bus, &dev, &func);
	return ((bus << 8) | (dev << 3) | func);
}

int
pci_find_device(struct pci_attach_args *pa,
    int (*match)(struct pci_attach_args *))
{
	extern struct cfdriver pci_cd;
	struct device *pcidev;
	int i;

	for (i = 0; i < pci_cd.cd_ndevs; i++) {
		pcidev = pci_cd.cd_devs[i];
		if (pcidev != NULL &&
		    pci_enumerate_bus((struct pci_softc *)pcidev,
		    		      match, pa) != 0)
			return (1);
	}
	return (0);
}

int
pci_get_powerstate(pci_chipset_tag_t pc, pcitag_t tag)
{
	pcireg_t reg;
	int offset;

	if (pci_get_capability(pc, tag, PCI_CAP_PWRMGMT, &offset, 0)) {
		reg = pci_conf_read(pc, tag, offset + PCI_PMCSR);
		return (reg & PCI_PMCSR_STATE_MASK);
	}
	return (PCI_PMCSR_STATE_D0);
}

int
pci_set_powerstate(pci_chipset_tag_t pc, pcitag_t tag, int state)
{
	pcireg_t id, reg;
	int offset, ostate = state;
	int d3_delay = 10 * 1000;

	/* Some AMD Ryzen xHCI controllers need a bit more time to wake up. */
	id = pci_conf_read(pc, tag, PCI_ID_REG);
	if (PCI_VENDOR(id) == PCI_VENDOR_AMD) {
		switch (PCI_PRODUCT(id)) {
		case PCI_PRODUCT_AMD_17_1X_XHCI_1:
		case PCI_PRODUCT_AMD_17_1X_XHCI_2:
		case PCI_PRODUCT_AMD_17_6X_XHCI:
			d3_delay = 20 * 1000;
		default:
			break;
		}
	}

	/*
	 * Warn the firmware that we are going to put the device
	 * into the given state.
	 */
	pci_set_powerstate_md(pc, tag, state, 1);

	if (pci_get_capability(pc, tag, PCI_CAP_PWRMGMT, &offset, 0)) {
		if (state == PCI_PMCSR_STATE_D3) {
			/*
			 * The PCI Power Management spec says we
			 * should disable I/O and memory space as well
			 * as bus mastering before we place the device
			 * into D3.
			 */
			reg = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
			reg &= ~PCI_COMMAND_IO_ENABLE;
			reg &= ~PCI_COMMAND_MEM_ENABLE;
			reg &= ~PCI_COMMAND_MASTER_ENABLE;
			pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG, reg);
		}
		reg = pci_conf_read(pc, tag, offset + PCI_PMCSR);
		if ((reg & PCI_PMCSR_STATE_MASK) != state) {
			ostate = reg & PCI_PMCSR_STATE_MASK;

			pci_conf_write(pc, tag, offset + PCI_PMCSR,
			    (reg & ~PCI_PMCSR_STATE_MASK) | state);
			if (state == PCI_PMCSR_STATE_D3 ||
			    ostate == PCI_PMCSR_STATE_D3)
				delay(d3_delay);
		}
	}

	/*
	 * Warn the firmware that the device is now in the given
	 * state.
	 */
	pci_set_powerstate_md(pc, tag, state, 0);

	return (ostate);
}

#ifndef PCI_MACHDEP_ENUMERATE_BUS
/*
 * Generic PCI bus enumeration routine.  Used unless machine-dependent
 * code needs to provide something else.
 */
int
pci_enumerate_bus(struct pci_softc *sc,
    int (*match)(struct pci_attach_args *), struct pci_attach_args *pap)
{
	pci_chipset_tag_t pc = sc->sc_pc;
	int device, function, nfunctions, ret;
	int maxndevs = sc->sc_maxndevs;
	const struct pci_quirkdata *qd;
	pcireg_t id, bhlcr, cap;
	pcitag_t tag;

	/*
	 * PCIe downstream ports and root ports should only forward
	 * configuration requests for device number 0.  However, not
	 * all hardware implements this correctly, and some devices
	 * will respond to other device numbers making the device show
	 * up 32 times.  Prevent this by only scanning a single
	 * device.
	 */
	if (sc->sc_bridgetag && pci_get_capability(pc, *sc->sc_bridgetag,
	    PCI_CAP_PCIEXPRESS, NULL, &cap)) {
		switch (PCI_PCIE_XCAP_TYPE(cap)) {
		case PCI_PCIE_XCAP_TYPE_RP:
		case PCI_PCIE_XCAP_TYPE_DOWN:
		case PCI_PCIE_XCAP_TYPE_PCI2PCIE:
			maxndevs = 1;
			break;
		}
	}

	for (device = 0; device < maxndevs; device++) {
		tag = pci_make_tag(pc, sc->sc_bus, device, 0);

		bhlcr = pci_conf_read(pc, tag, PCI_BHLC_REG);
		if (PCI_HDRTYPE_TYPE(bhlcr) > 2)
			continue;

		id = pci_conf_read(pc, tag, PCI_ID_REG);

		/* Invalid vendor ID value? */
		if (PCI_VENDOR(id) == PCI_VENDOR_INVALID)
			continue;
		/* XXX Not invalid, but we've done this ~forever. */
		if (PCI_VENDOR(id) == 0)
			continue;

		qd = pci_lookup_quirkdata(PCI_VENDOR(id), PCI_PRODUCT(id));

		if (qd != NULL &&
		      (qd->quirks & PCI_QUIRK_MULTIFUNCTION) != 0)
			nfunctions = 8;
		else if (qd != NULL &&
		      (qd->quirks & PCI_QUIRK_MONOFUNCTION) != 0)
			nfunctions = 1;
		else
			nfunctions = PCI_HDRTYPE_MULTIFN(bhlcr) ? 8 : 1;

		for (function = 0; function < nfunctions; function++) {
			tag = pci_make_tag(pc, sc->sc_bus, device, function);
			ret = pci_probe_device(sc, tag, match, pap);
			if (match != NULL && ret != 0)
				return (ret);
		}
 	}

	return (0);
}
#endif /* PCI_MACHDEP_ENUMERATE_BUS */

int
pci_reserve_resources(struct pci_attach_args *pa)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	pcireg_t bhlc, blr, bir, csr;
	pcireg_t addr, mask, type;
	bus_addr_t base, limit;
	bus_size_t size;
	int reg, reg_start, reg_end, reg_rom;
	int bus, dev, func;
	int sec, sub;
	int flags;
	int s;

	pci_decompose_tag(pc, tag, &bus, &dev, &func);

	bhlc = pci_conf_read(pc, tag, PCI_BHLC_REG);
	switch (PCI_HDRTYPE_TYPE(bhlc)) {
	case 0:
		reg_start = PCI_MAPREG_START;
		reg_end = PCI_MAPREG_END;
		reg_rom = PCI_ROM_REG;
		break;
	case 1: /* PCI-PCI bridge */
		reg_start = PCI_MAPREG_START;
		reg_end = PCI_MAPREG_PPB_END;
		reg_rom = 0;	/* 0x38 */
		break;
	case 2: /* PCI-CardBus bridge */
		reg_start = PCI_MAPREG_START;
		reg_end = PCI_MAPREG_PCB_END;
		reg_rom = 0;
		break;
	default:
		return (0);
	}

	csr = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
	for (reg = reg_start; reg < reg_end; reg += 4) {
		if (!pci_mapreg_probe(pc, tag, reg, &type))
			continue;

		if (pci_mapreg_info(pc, tag, reg, type, &base, &size, &flags))
			continue;

		if (base == 0)
			continue;

		switch (type) {
		case PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT:
		case PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_64BIT:
			if (ISSET(flags, BUS_SPACE_MAP_PREFETCHABLE) &&
			    pa->pa_pmemex && extent_alloc_region(pa->pa_pmemex,
			    base, size, EX_NOWAIT) == 0) {
				break;
			}
#ifdef __sparc64__
			/*
			 * Certain SPARC T5 systems assign
			 * non-prefetchable 64-bit BARs of their onboard
			 * mpii(4) controllers addresses in the
			 * prefetchable memory range.  This is
			 * (probably) safe, as reads from the device
			 * registers mapped by these BARs are
			 * side-effect free.  So assume the firmware
			 * knows what it is doing.
			 */
			if (base >= 0x100000000 &&
			    pa->pa_pmemex && extent_alloc_region(pa->pa_pmemex,
			    base, size, EX_NOWAIT) == 0) {
				break;
			}
#endif
			if (pa->pa_memex && extent_alloc_region(pa->pa_memex,
			    base, size, EX_NOWAIT)) {
				if (csr & PCI_COMMAND_MEM_ENABLE) {
					printf("%d:%d:%d: mem address conflict"
					    " 0x%lx/0x%lx\n", bus, dev, func,
					    base, size);
				}
				pci_conf_write(pc, tag, reg, 0);
				if (type & PCI_MAPREG_MEM_TYPE_64BIT)
					pci_conf_write(pc, tag, reg + 4, 0);
			}
			break;
		case PCI_MAPREG_TYPE_IO:
			if (pa->pa_ioex && extent_alloc_region(pa->pa_ioex,
			    base, size, EX_NOWAIT)) {
				if (csr & PCI_COMMAND_IO_ENABLE) {
					printf("%d:%d:%d: io address conflict"
					    " 0x%lx/0x%lx\n", bus, dev, func,
					    base, size);
				}
				pci_conf_write(pc, tag, reg, 0);
			}
			break;
		}

		if (type & PCI_MAPREG_MEM_TYPE_64BIT)
			reg += 4;
	}

	if (reg_rom != 0) {
		s = splhigh();
		addr = pci_conf_read(pc, tag, PCI_ROM_REG);
		pci_conf_write(pc, tag, PCI_ROM_REG, ~PCI_ROM_ENABLE);
		mask = pci_conf_read(pc, tag, PCI_ROM_REG);
		pci_conf_write(pc, tag, PCI_ROM_REG, addr);
		splx(s);

		base = PCI_ROM_ADDR(addr);
		size = PCI_ROM_SIZE(mask);
		if (base != 0 && size != 0) {
			if (pa->pa_pmemex && extent_alloc_region(pa->pa_pmemex,
			    base, size, EX_NOWAIT) &&
			    pa->pa_memex && extent_alloc_region(pa->pa_memex,
			    base, size, EX_NOWAIT)) {
				if (addr & PCI_ROM_ENABLE) {
					printf("%d:%d:%d: rom address conflict"
					    " 0x%lx/0x%lx\n", bus, dev, func,
					    base, size);
				}
				pci_conf_write(pc, tag, PCI_ROM_REG, 0);
			}
		}
	}

	if (PCI_HDRTYPE_TYPE(bhlc) != 1)
		return (0);

	/* Figure out the I/O address range of the bridge. */
	blr = pci_conf_read(pc, tag, PPB_REG_IOSTATUS);
	base = (blr & 0x000000f0) << 8;
	limit = (blr & 0x000f000) | 0x00000fff;
	blr = pci_conf_read(pc, tag, PPB_REG_IO_HI);
	base |= (blr & 0x0000ffff) << 16;
	limit |= (blr & 0xffff0000);
	if (limit > base)
		size = (limit - base + 1);
	else
		size = 0;
	if (pa->pa_ioex && base > 0 && size > 0) {
		if (extent_alloc_region(pa->pa_ioex, base, size, EX_NOWAIT)) {
			printf("%d:%d:%d: bridge io address conflict 0x%lx/0x%lx\n",
			    bus, dev, func, base, size);
			blr &= 0xffff0000;
			blr |= 0x000000f0;
			pci_conf_write(pc, tag, PPB_REG_IOSTATUS, blr);
		}
	}

	/* Figure out the memory mapped I/O address range of the bridge. */
	blr = pci_conf_read(pc, tag, PPB_REG_MEM);
	base = (blr & 0x0000fff0) << 16;
	limit = (blr & 0xfff00000) | 0x000fffff;
	if (limit > base)
		size = (limit - base + 1);
	else
		size = 0;
	if (pa->pa_memex && base > 0 && size > 0) {
		if (extent_alloc_region(pa->pa_memex, base, size, EX_NOWAIT)) {
			printf("%d:%d:%d: bridge mem address conflict 0x%lx/0x%lx\n",
			    bus, dev, func, base, size);
			pci_conf_write(pc, tag, PPB_REG_MEM, 0x0000fff0);
		}
	}

	/* Figure out the prefetchable memory address range of the bridge. */
	blr = pci_conf_read(pc, tag, PPB_REG_PREFMEM);
	base = (blr & 0x0000fff0) << 16;
	limit = (blr & 0xfff00000) | 0x000fffff;
#ifdef __LP64__
	blr = pci_conf_read(pc, pa->pa_tag, PPB_REG_PREFBASE_HI32);
	base |= ((uint64_t)blr) << 32;
	blr = pci_conf_read(pc, pa->pa_tag, PPB_REG_PREFLIM_HI32);
	limit |= ((uint64_t)blr) << 32;
#endif
	if (limit > base)
		size = (limit - base + 1);
	else
		size = 0;
	if (pa->pa_pmemex && base > 0 && size > 0) {
		if (extent_alloc_region(pa->pa_pmemex, base, size, EX_NOWAIT)) {
			printf("%d:%d:%d: bridge mem address conflict 0x%lx/0x%lx\n",
			    bus, dev, func, base, size);
			pci_conf_write(pc, tag, PPB_REG_PREFMEM, 0x0000fff0);
		}
	} else if (pa->pa_memex && base > 0 && size > 0) {
		if (extent_alloc_region(pa->pa_memex, base, size, EX_NOWAIT)) {
			printf("%d:%d:%d: bridge mem address conflict 0x%lx/0x%lx\n",
			    bus, dev, func, base, size);
			pci_conf_write(pc, tag, PPB_REG_PREFMEM, 0x0000fff0);
		}
	}

	/* Figure out the bus range handled by the bridge. */
	bir = pci_conf_read(pc, tag, PPB_REG_BUSINFO);
	sec = PPB_BUSINFO_SECONDARY(bir);
	sub = PPB_BUSINFO_SUBORDINATE(bir);
	if (pa->pa_busex && sub >= sec && sub > 0) {
		if (extent_alloc_region(pa->pa_busex, sec, sub - sec + 1,
		    EX_NOWAIT)) {
			printf("%d:%d:%d: bridge bus conflict %d-%d\n",
			    bus, dev, func, sec, sub);
		}
	}

	return (0);
}

/*
 * Vital Product Data (PCI 2.2)
 */

int
pci_vpd_read(pci_chipset_tag_t pc, pcitag_t tag, int offset, int count,
    pcireg_t *data)
{
	uint32_t reg;
	int ofs, i, j;

	KASSERT(data != NULL);
	if ((offset + count) >= PCI_VPD_ADDRESS_MASK)
		return (EINVAL);

	if (pci_get_capability(pc, tag, PCI_CAP_VPD, &ofs, &reg) == 0)
		return (ENXIO);

	for (i = 0; i < count; offset += sizeof(*data), i++) {
		reg &= 0x0000ffff;
		reg &= ~PCI_VPD_OPFLAG;
		reg |= PCI_VPD_ADDRESS(offset);
		pci_conf_write(pc, tag, ofs, reg);

		/*
		 * PCI 2.2 does not specify how long we should poll
		 * for completion nor whether the operation can fail.
		 */
		j = 0;
		do {
			if (j++ == 20)
				return (EIO);
			delay(4);
			reg = pci_conf_read(pc, tag, ofs);
		} while ((reg & PCI_VPD_OPFLAG) == 0);
		data[i] = pci_conf_read(pc, tag, PCI_VPD_DATAREG(ofs));
	}

	return (0);
}

int
pci_matchbyid(struct pci_attach_args *pa, const struct pci_matchid *ids,
    int nent)
{
	const struct pci_matchid *pm;
	int i;

	for (i = 0, pm = ids; i < nent; i++, pm++)
		if (PCI_VENDOR(pa->pa_id) == pm->pm_vid &&
		    PCI_PRODUCT(pa->pa_id) == pm->pm_pid)
			return (1);
	return (0);
}

void
pci_disable_legacy_vga(struct device *dev)
{
	struct pci_softc *pci;
	struct pci_dev *pd;

	/* XXX Until we attach the drm drivers directly to pci. */
	while (dev->dv_parent->dv_cfdata->cf_driver != &pci_cd)
		dev = dev->dv_parent;

	pci = (struct pci_softc *)dev->dv_parent;
	LIST_FOREACH(pd, &pci->sc_devs, pd_next) {
		if (pd->pd_dev == dev) {
			pd->pd_vga_decode = 0;
			break;
		}
	}
}

#ifdef USER_PCICONF
/*
 * This is the user interface to PCI configuration space.
 */
  
#include <sys/pciio.h>
#include <sys/fcntl.h>

#ifdef DEBUG
#define PCIDEBUG(x) printf x
#else
#define PCIDEBUG(x)
#endif

void pci_disable_vga(pci_chipset_tag_t, pcitag_t);
void pci_enable_vga(pci_chipset_tag_t, pcitag_t);
void pci_route_vga(struct pci_softc *);
void pci_unroute_vga(struct pci_softc *);

int pciopen(dev_t dev, int oflags, int devtype, struct proc *p);
int pciclose(dev_t dev, int flag, int devtype, struct proc *p);
int pciioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p);

int
pciopen(dev_t dev, int oflags, int devtype, struct proc *p) 
{
	PCIDEBUG(("pciopen ndevs: %d\n" , pci_cd.cd_ndevs));

	if (minor(dev) >= pci_ndomains) {
		return ENXIO;
	}

#ifndef APERTURE
	if ((oflags & FWRITE) && securelevel > 0) {
		return EPERM;
	}
#else
	if ((oflags & FWRITE) && securelevel > 0 && allowaperture == 0) {
		return EPERM;
	}
#endif
	return (0);
}

int
pciclose(dev_t dev, int flag, int devtype, struct proc *p)
{
	PCIDEBUG(("pciclose\n"));

	pci_vga_proc = NULL;
	return (0);
}

int
pciioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct pcisel *sel = (struct pcisel *)data;
	struct pci_io *io;
	struct pci_dev *pd;
	struct pci_rom *rom;
	int i, error;
	pcitag_t tag;
	struct pci_softc *pci;
	pci_chipset_tag_t pc;

	switch (cmd) {
	case PCIOCREAD:
	case PCIOCREADMASK:
		break;
	case PCIOCWRITE:
		if (!(flag & FWRITE))
			return EPERM;
		break;
	case PCIOCGETROMLEN:
	case PCIOCGETROM:
	case PCIOCGETVPD:
		break;
	case PCIOCGETVGA:
	case PCIOCSETVGA:
		if (pci_vga_pci == NULL)
			return EINVAL;
		break;
	default:
		return ENOTTY;
	}

	for (i = 0; i < pci_cd.cd_ndevs; i++) {
		pci = pci_cd.cd_devs[i];
		if (pci != NULL && pci->sc_domain == minor(dev) &&
		    pci->sc_bus == sel->pc_bus)
			break;
	}
	if (i >= pci_cd.cd_ndevs)
		return ENXIO;

	/* Check bounds */
	if (pci->sc_bus >= 256 || 
	    sel->pc_dev >= pci_bus_maxdevs(pci->sc_pc, pci->sc_bus) ||
	    sel->pc_func >= 8)
		return EINVAL;

	pc = pci->sc_pc;
	LIST_FOREACH(pd, &pci->sc_devs, pd_next) {
		int bus, dev, func;

		pci_decompose_tag(pc, pd->pd_tag, &bus, &dev, &func);

		if (bus == sel->pc_bus && dev == sel->pc_dev &&
		    func == sel->pc_func)
			break;
	}
	if (pd == NULL)
		return ENXIO;

	tag = pci_make_tag(pc, sel->pc_bus, sel->pc_dev, sel->pc_func);

	switch (cmd) {
	case PCIOCREAD:
		io = (struct pci_io *)data;
		switch (io->pi_width) {
		case 4:
			/* Configuration space bounds check */
			if (io->pi_reg < 0 ||
			    io->pi_reg >= pci_conf_size(pc, tag))
				return EINVAL;
			/* Make sure the register is properly aligned */
			if (io->pi_reg & 0x3) 
				return EINVAL;
			io->pi_data = pci_conf_read(pc, tag, io->pi_reg);
			error = 0;
			break;
		default:
			error = EINVAL;
			break;
		}
		break;

	case PCIOCWRITE:
		io = (struct pci_io *)data;
		switch (io->pi_width) {
		case 4:
			/* Configuration space bounds check */
			if (io->pi_reg < 0 ||
			    io->pi_reg >= pci_conf_size(pc, tag))
				return EINVAL;
			/* Make sure the register is properly aligned */
			if (io->pi_reg & 0x3)
				return EINVAL;
			pci_conf_write(pc, tag, io->pi_reg, io->pi_data);
			error = 0;
			break;
		default:
			error = EINVAL;
			break;
		}
		break;

	case PCIOCREADMASK:
		io = (struct pci_io *)data;

		if (io->pi_width != 4 || io->pi_reg & 0x3 ||
		    io->pi_reg < PCI_MAPREG_START ||
		    io->pi_reg >= PCI_MAPREG_END)
			return (EINVAL);

		i = (io->pi_reg - PCI_MAPREG_START) / 4;
		io->pi_data = pd->pd_mask[i];
		error = 0;
		break;

	case PCIOCGETROMLEN:
	case PCIOCGETROM:
	{
		pcireg_t addr, mask, bhlc;
		bus_space_handle_t h;
		bus_size_t len, off;
		char buf[256];
		int s;

		rom = (struct pci_rom *)data;

		bhlc = pci_conf_read(pc, tag, PCI_BHLC_REG);
		if (PCI_HDRTYPE_TYPE(bhlc) != 0)
			return (ENODEV);

		s = splhigh();
		addr = pci_conf_read(pc, tag, PCI_ROM_REG);
		pci_conf_write(pc, tag, PCI_ROM_REG, ~PCI_ROM_ENABLE);
		mask = pci_conf_read(pc, tag, PCI_ROM_REG);
		pci_conf_write(pc, tag, PCI_ROM_REG, addr);
		splx(s);

		/*
		 * Section 6.2.5.2 `Expansion ROM Base Address Register',
		 *
		 * tells us that only the upper 21 bits are writable.
		 * This means that the size of a ROM must be a
		 * multiple of 2 KB.  So reading the ROM in chunks of
		 * 256 bytes should work just fine.
		 */
		if ((PCI_ROM_ADDR(addr) == 0 ||
		     PCI_ROM_SIZE(mask) % sizeof(buf)) != 0)
			return (ENODEV);

		/* If we're just after the size, skip reading the ROM. */
		if (cmd == PCIOCGETROMLEN) {
			error = 0;
			goto fail;
		}

		if (rom->pr_romlen < PCI_ROM_SIZE(mask)) {
			error = ENOMEM;
			goto fail;
		}

		error = bus_space_map(pci->sc_memt, PCI_ROM_ADDR(addr),
		    PCI_ROM_SIZE(mask), 0, &h);
		if (error)
			goto fail;

		off = 0;
		len = PCI_ROM_SIZE(mask);
		while (len > 0 && error == 0) {
			s = splhigh();
			pci_conf_write(pc, tag, PCI_ROM_REG,
			    addr | PCI_ROM_ENABLE);
			bus_space_read_region_1(pci->sc_memt, h, off,
			    buf, sizeof(buf));
			pci_conf_write(pc, tag, PCI_ROM_REG, addr);
			splx(s);

			error = copyout(buf, rom->pr_rom + off, sizeof(buf));
			off += sizeof(buf);
			len -= sizeof(buf);
		}

		bus_space_unmap(pci->sc_memt, h, PCI_ROM_SIZE(mask));

	fail:
		rom->pr_romlen = PCI_ROM_SIZE(mask);
		break;
	}

	case PCIOCGETVPD: {
		struct pci_vpd_req *pv = (struct pci_vpd_req *)data;
		pcireg_t *data;
		size_t len;
		unsigned int i;
		int s;

		CTASSERT(sizeof(*data) == sizeof(*pv->pv_data));

		data = mallocarray(pv->pv_count, sizeof(*data), M_TEMP,
		    M_WAITOK|M_CANFAIL);
		if (data == NULL) {
			error = ENOMEM;
			break;
		}

		s = splhigh();
		error = pci_vpd_read(pc, tag, pv->pv_offset, pv->pv_count,
		    data);
		splx(s);

		len = pv->pv_count * sizeof(*pv->pv_data);

		if (error == 0) {
			for (i = 0; i < pv->pv_count; i++)
				data[i] = letoh32(data[i]);

			error = copyout(data, pv->pv_data, len);
		}

		free(data, M_TEMP, len);
		break;
	}

	case PCIOCGETVGA:
	{
		struct pci_vga *vga = (struct pci_vga *)data;
		struct pci_dev *pd;
		int bus, dev, func;

		vga->pv_decode = 0;
		LIST_FOREACH(pd, &pci->sc_devs, pd_next) {
			pci_decompose_tag(pc, pd->pd_tag, NULL, &dev, &func);
			if (dev == sel->pc_dev && func == sel->pc_func) {
				if (pd->pd_vga_decode)
					vga->pv_decode = PCI_VGA_IO_ENABLE |
					    PCI_VGA_MEM_ENABLE;
				break;
			}
		}

		pci_decompose_tag(pci_vga_pci->sc_pc,
		    pci_vga_tag, &bus, &dev, &func);
		vga->pv_sel.pc_bus = bus;
		vga->pv_sel.pc_dev = dev;
		vga->pv_sel.pc_func = func;
		error = 0;
		break;
	}
	case PCIOCSETVGA:
	{
		struct pci_vga *vga = (struct pci_vga *)data;
		int bus, dev, func;

		switch (vga->pv_lock) {
		case PCI_VGA_UNLOCK:
		case PCI_VGA_LOCK:
		case PCI_VGA_TRYLOCK:
			break;
		default:
			return (EINVAL);
		}

		if (vga->pv_lock == PCI_VGA_UNLOCK) {
			if (pci_vga_proc != p)
				return (EINVAL);
			pci_vga_proc = NULL;
			wakeup(&pci_vga_proc);
			return (0);
		}

		while (pci_vga_proc != p && pci_vga_proc != NULL) {
			if (vga->pv_lock == PCI_VGA_TRYLOCK)
				return (EBUSY);
			error = tsleep_nsec(&pci_vga_proc, PLOCK | PCATCH,
			    "vgalk", INFSLP);
			if (error)
				return (error);
		}
		pci_vga_proc = p;

		pci_decompose_tag(pci_vga_pci->sc_pc,
		    pci_vga_tag, &bus, &dev, &func);
		if (bus != vga->pv_sel.pc_bus || dev != vga->pv_sel.pc_dev ||
		    func != vga->pv_sel.pc_func) {
			pci_disable_vga(pci_vga_pci->sc_pc, pci_vga_tag);
			if (pci != pci_vga_pci) {
				pci_unroute_vga(pci_vga_pci);
				pci_route_vga(pci);
				pci_vga_pci = pci;
			}
			pci_enable_vga(pc, tag);
			pci_vga_tag = tag;
		}

		error = 0;
		break;
	}

	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

void
pci_disable_vga(pci_chipset_tag_t pc, pcitag_t tag)
{
	pcireg_t csr;

	csr = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
	csr &= ~(PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE);
	pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG, csr);
}

void
pci_enable_vga(pci_chipset_tag_t pc, pcitag_t tag)
{
	pcireg_t csr;

	csr = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
	csr |= PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE;
	pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG, csr);
}

void
pci_route_vga(struct pci_softc *sc)
{
	pci_chipset_tag_t pc = sc->sc_pc;
	pcireg_t bc;

	if (sc->sc_bridgetag == NULL)
		return;

	bc = pci_conf_read(pc, *sc->sc_bridgetag, PPB_REG_BRIDGECONTROL);
	bc |= PPB_BC_VGA_ENABLE;
	pci_conf_write(pc, *sc->sc_bridgetag, PPB_REG_BRIDGECONTROL, bc);

	pci_route_vga((struct pci_softc *)sc->sc_dev.dv_parent->dv_parent);
}

void
pci_unroute_vga(struct pci_softc *sc)
{
	pci_chipset_tag_t pc = sc->sc_pc;
	pcireg_t bc;

	if (sc->sc_bridgetag == NULL)
		return;

	bc = pci_conf_read(pc, *sc->sc_bridgetag, PPB_REG_BRIDGECONTROL);
	bc &= ~PPB_BC_VGA_ENABLE;
	pci_conf_write(pc, *sc->sc_bridgetag, PPB_REG_BRIDGECONTROL, bc);

	pci_unroute_vga((struct pci_softc *)sc->sc_dev.dv_parent->dv_parent);
}
#endif /* USER_PCICONF */

int
pci_primary_vga(struct pci_attach_args *pa)
{
	/* XXX For now, only handle the first PCI domain. */
	if (pa->pa_domain != 0)
		return (0);

	if ((PCI_CLASS(pa->pa_class) != PCI_CLASS_DISPLAY ||
	    PCI_SUBCLASS(pa->pa_class) != PCI_SUBCLASS_DISPLAY_VGA) &&
	    (PCI_CLASS(pa->pa_class) != PCI_CLASS_PREHISTORIC ||
	    PCI_SUBCLASS(pa->pa_class) != PCI_SUBCLASS_PREHISTORIC_VGA))
		return (0);

	if ((pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG)
	    & (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE))
	    != (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE))
		return (0);

	pci_vga_tag = pa->pa_tag;

	return (1);
}

#ifdef __HAVE_PCI_MSIX

struct msix_vector *
pci_alloc_msix_table(pci_chipset_tag_t pc, pcitag_t tag)
{
	struct msix_vector *table;
	pcireg_t reg;
	int tblsz;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, NULL, &reg) == 0)
		return NULL;

	tblsz = PCI_MSIX_MC_TBLSZ(reg) + 1;
	table = mallocarray(tblsz, sizeof(*table), M_DEVBUF, M_WAITOK);

	return table;
}

void
pci_free_msix_table(pci_chipset_tag_t pc, pcitag_t tag,
    struct msix_vector *table)
{
	pcireg_t reg;
	int tblsz;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, NULL, &reg) == 0)
		return;

	tblsz = PCI_MSIX_MC_TBLSZ(reg) + 1;
	free(table, M_DEVBUF, tblsz * sizeof(*table));
}

void
pci_suspend_msix(pci_chipset_tag_t pc, pcitag_t tag,
    bus_space_tag_t memt, pcireg_t *mc, struct msix_vector *table)
{
	bus_space_handle_t memh;
	pcireg_t reg;
	int tblsz, i;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, NULL, &reg) == 0)
		return;

	KASSERT(table != NULL);

	if (pci_msix_table_map(pc, tag, memt, &memh))
		return;

	tblsz = PCI_MSIX_MC_TBLSZ(reg) + 1;
	for (i = 0; i < tblsz; i++) {
		table[i].mv_ma = bus_space_read_4(memt, memh, PCI_MSIX_MA(i));
		table[i].mv_mau32 = bus_space_read_4(memt, memh,
		    PCI_MSIX_MAU32(i));
		table[i].mv_md = bus_space_read_4(memt, memh, PCI_MSIX_MD(i));
		table[i].mv_vc = bus_space_read_4(memt, memh, PCI_MSIX_VC(i));
	}

	pci_msix_table_unmap(pc, tag, memt, memh);
	
	*mc = reg;
}

void
pci_resume_msix(pci_chipset_tag_t pc, pcitag_t tag,
    bus_space_tag_t memt, pcireg_t mc, struct msix_vector *table)
{
	bus_space_handle_t memh;
	pcireg_t reg;
	int tblsz, i;
	int off;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, &off, &reg) == 0)
		return;

	KASSERT(table != NULL);

	if (pci_msix_table_map(pc, tag, memt, &memh))
		return;

	tblsz = PCI_MSIX_MC_TBLSZ(reg) + 1;
	for (i = 0; i < tblsz; i++) {
		bus_space_write_4(memt, memh, PCI_MSIX_MA(i), table[i].mv_ma);
		bus_space_write_4(memt, memh, PCI_MSIX_MAU32(i),
		    table[i].mv_mau32);
		bus_space_write_4(memt, memh, PCI_MSIX_MD(i), table[i].mv_md);
		bus_space_barrier(memt, memh, PCI_MSIX_MA(i), 16,
		    BUS_SPACE_BARRIER_WRITE);
		bus_space_write_4(memt, memh, PCI_MSIX_VC(i), table[i].mv_vc);
		bus_space_barrier(memt, memh, PCI_MSIX_VC(i), 4,
		    BUS_SPACE_BARRIER_WRITE);
	}

	pci_msix_table_unmap(pc, tag, memt, memh);

	pci_conf_write(pc, tag, off, mc);
}

int
pci_intr_msix_count(struct pci_attach_args *pa)
{
	pcireg_t reg;

	if ((pa->pa_flags & PCI_FLAGS_MSI_ENABLED) == 0)
		return (0);

	if (pci_get_capability(pa->pa_pc, pa->pa_tag, PCI_CAP_MSIX, NULL,
	    &reg) == 0)
		return (0);

	return (PCI_MSIX_MC_TBLSZ(reg) + 1);
}

#else /* __HAVE_PCI_MSIX */

struct msix_vector *
pci_alloc_msix_table(pci_chipset_tag_t pc, pcitag_t tag)
{
	return NULL;
}

void
pci_free_msix_table(pci_chipset_tag_t pc, pcitag_t tag,
    struct msix_vector *table)
{
}

void
pci_suspend_msix(pci_chipset_tag_t pc, pcitag_t tag,
    bus_space_tag_t memt, pcireg_t *mc, struct msix_vector *table)
{
}

void
pci_resume_msix(pci_chipset_tag_t pc, pcitag_t tag,
    bus_space_tag_t memt, pcireg_t mc, struct msix_vector *table)
{
}

int
pci_intr_msix_count(struct pci_attach_args *pa)
{
	return (0);
}

#endif /* __HAVE_PCI_MSIX */
