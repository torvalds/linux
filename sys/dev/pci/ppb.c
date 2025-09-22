/*	$OpenBSD: ppb.c,v 1.73 2025/09/16 12:18:10 hshoexer Exp $	*/
/*	$NetBSD: ppb.c,v 1.16 1997/06/06 23:48:05 thorpej Exp $	*/

/*
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/task.h>
#include <sys/timeout.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/ppbreg.h>

#ifdef __HAVE_FDT
#include <machine/fdt.h>
#include <dev/ofw/openfirm.h>
#endif

#ifndef PCI_IO_START
#define PCI_IO_START	0
#endif

#ifndef PCI_IO_END
#define PCI_IO_END	0xffffffff
#endif

#ifndef PCI_MEM_START
#define PCI_MEM_START	0
#endif

#ifndef PCI_MEM_END
#define PCI_MEM_END	0xffffffff
#endif

#define PPB_EXNAMLEN	32

struct ppb_softc {
	struct device sc_dev;		/* generic device glue */
	pci_chipset_tag_t sc_pc;	/* our PCI chipset... */
	pcitag_t sc_tag;		/* ...and tag. */
	pci_intr_handle_t sc_ih[4];
	void *sc_intrhand;
	struct extent *sc_parent_busex;
	struct extent *sc_busex;
	struct extent *sc_ioex;
	struct extent *sc_memex;
	struct extent *sc_pmemex;
	struct device *sc_psc;
	int sc_cap_off;
	struct task sc_insert_task;
	struct task sc_rescan_task;
	struct task sc_remove_task;
	struct timeout sc_to;

	u_long sc_busnum;
	u_long sc_busrange;

	bus_addr_t sc_iobase, sc_iolimit;
	bus_addr_t sc_membase, sc_memlimit;
	bus_addr_t sc_pmembase, sc_pmemlimit;

	pcireg_t sc_csr;
	pcireg_t sc_bhlcr;
	pcireg_t sc_bir;
	pcireg_t sc_bcr;
	pcireg_t sc_int;
	pcireg_t sc_slcsr;
	pcireg_t sc_msi_mc;
	pcireg_t sc_msi_ma;
	pcireg_t sc_msi_mau32;
	pcireg_t sc_msi_md;
	int sc_pmcsr_state;
};

int	ppbmatch(struct device *, void *, void *);
void	ppbattach(struct device *, struct device *, void *);
int	ppbdetach(struct device *self, int flags);
int	ppbactivate(struct device *self, int act);

const struct cfattach ppb_ca = {
	sizeof(struct ppb_softc), ppbmatch, ppbattach, ppbdetach, ppbactivate
};

struct cfdriver ppb_cd = {
	NULL, "ppb", DV_DULL, CD_COCOVM
};

void	ppb_alloc_busrange(struct ppb_softc *, struct pci_attach_args *,
	    pcireg_t *);
void	ppb_alloc_resources(struct ppb_softc *, struct pci_attach_args *);
int	ppb_intr(void *);
void	ppb_hotplug_insert(void *);
void	ppb_hotplug_insert_finish(void *);
void	ppb_hotplug_rescan(void *);
void	ppb_hotplug_remove(void *);
int	ppbprint(void *, const char *pnp);

int
ppbmatch(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	/*
	 * This device is mislabeled.  It is not a PCI bridge.
	 */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_VIATECH &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_VIATECH_VT82C586_PWR)
		return (0);
	/*
	 * Check the ID register to see that it's a PCI bridge.
	 * If it is, we assume that we can deal with it; it _should_
	 * work in a standardized way...
	 */
	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_BRIDGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_BRIDGE_PCI)
		return (1);

	return (0);
}

void
ppbattach(struct device *parent, struct device *self, void *aux)
{
	struct ppb_softc *sc = (struct ppb_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	struct pcibus_attach_args pba;
	pci_interface_t interface;
	pci_intr_handle_t ih;
	pcireg_t busdata, reg, blr;
	char *name;
	int sec, sub;
	int pin;

	sc->sc_pc = pc;
	sc->sc_tag = pa->pa_tag;

	busdata = pci_conf_read(pc, pa->pa_tag, PPB_REG_BUSINFO);

	/*
	 * When the bus number isn't configured, try to allocate one
	 * ourselves.
	 */
	if (busdata == 0 && pa->pa_busex)
		ppb_alloc_busrange(sc, pa, &busdata);

	/*
	 * When the bus number still isn't set correctly, give up.
	 */
	if (PPB_BUSINFO_SECONDARY(busdata) == 0) {
		printf(": not configured by system firmware\n");
		return;
	}

#if 0
	/*
	 * XXX can't do this, because we're not given our bus number
	 * (we shouldn't need it), and because we've no way to
	 * decompose our tag.
	 */
	/* sanity check. */
	if (pa->pa_bus != PPB_BUSINFO_PRIMARY(busdata))
		panic("ppbattach: bus in tag (%d) != bus in reg (%d)",
		    pa->pa_bus, PPB_BUSINFO_PRIMARY(busdata));
#endif

	sec = PPB_BUSINFO_SECONDARY(busdata);
	sub = PPB_BUSINFO_SUBORDINATE(busdata);
	if (sub > sec) {
		name = malloc(PPB_EXNAMLEN, M_DEVBUF, M_NOWAIT);
		if (name) {
			snprintf(name, PPB_EXNAMLEN, "%s pcibus", sc->sc_dev.dv_xname);
			sc->sc_busex = extent_create(name, 0, 0xff,
			    M_DEVBUF, NULL, 0, EX_NOWAIT | EX_FILLED);
			extent_free(sc->sc_busex, sec + 1,
			    sub - sec, EX_NOWAIT);
		}
	}

	sc->sc_parent_busex = pa->pa_busex;
	sc->sc_busnum = sec;
	sc->sc_busrange = sub - sec + 1;

	/* Check for PCI Express capabilities and setup hotplug support. */
	if (pci_get_capability(pc, pa->pa_tag, PCI_CAP_PCIEXPRESS,
	    &sc->sc_cap_off, &reg) && (reg & PCI_PCIE_XCAP_SI)) {
		task_set(&sc->sc_insert_task, ppb_hotplug_insert, sc);
		task_set(&sc->sc_rescan_task, ppb_hotplug_rescan, sc);
		task_set(&sc->sc_remove_task, ppb_hotplug_remove, sc);
		timeout_set(&sc->sc_to, ppb_hotplug_insert_finish, sc);

#ifdef __i386__
		if (pci_intr_map(pa, &ih) == 0)
			sc->sc_intrhand = pci_intr_establish(pc, ih, IPL_BIO,
			    ppb_intr, sc, self->dv_xname);
#else
		if (pci_intr_map_msi(pa, &ih) == 0 ||
		    pci_intr_map(pa, &ih) == 0)
			sc->sc_intrhand = pci_intr_establish(pc, ih, IPL_BIO,
			    ppb_intr, sc, self->dv_xname);
#endif

		if (sc->sc_intrhand) {
			printf(": %s", pci_intr_string(pc, ih));

			/* Enable hotplug interrupt. */
			reg = pci_conf_read(pc, pa->pa_tag,
			    sc->sc_cap_off + PCI_PCIE_SLCSR);
			reg |= (PCI_PCIE_SLCSR_HPE | PCI_PCIE_SLCSR_PDE);
			pci_conf_write(pc, pa->pa_tag,
			    sc->sc_cap_off + PCI_PCIE_SLCSR, reg);
		}
	}

	printf("\n");

	interface = PCI_INTERFACE(pa->pa_class);

	/*
	 * The Intel 82801BAM Hub-to-PCI can decode subtractively but
	 * doesn't advertise itself as such.
	 */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INTEL &&
	    (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801BA_HPB ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_82801BAM_HPB))
		interface = PPB_INTERFACE_SUBTRACTIVE;

	if (interface != PPB_INTERFACE_SUBTRACTIVE)
		ppb_alloc_resources(sc, pa);

	for (pin = PCI_INTERRUPT_PIN_A; pin <= PCI_INTERRUPT_PIN_D; pin++) {
		pa->pa_intrpin = pa->pa_rawintrpin = pin;
		pa->pa_intrline = 0;
		pci_intr_map(pa, &sc->sc_ih[pin - PCI_INTERRUPT_PIN_A]);
	}

	/*
	 * The UltraSPARC-IIi APB doesn't implement the standard
	 * address range registers.
	 */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_SUN &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_SUN_SIMBA)
		goto attach;

	/* Figure out the I/O address range of the bridge. */
	blr = pci_conf_read(pc, pa->pa_tag, PPB_REG_IOSTATUS);
	sc->sc_iobase = (blr & 0x000000f0) << 8;
	sc->sc_iolimit = (blr & 0x000f000) | 0x00000fff;
	blr = pci_conf_read(pc, pa->pa_tag, PPB_REG_IO_HI);
	sc->sc_iobase |= (blr & 0x0000ffff) << 16;
	sc->sc_iolimit |= (blr & 0xffff0000);
	if (sc->sc_iolimit > sc->sc_iobase) {
		name = malloc(PPB_EXNAMLEN, M_DEVBUF, M_NOWAIT);
		if (name) {
			snprintf(name, PPB_EXNAMLEN, "%s pciio", sc->sc_dev.dv_xname);
			sc->sc_ioex = extent_create(name, 0, 0xffffffff,
			    M_DEVBUF, NULL, 0, EX_NOWAIT | EX_FILLED);
			extent_free(sc->sc_ioex, sc->sc_iobase,
			    sc->sc_iolimit - sc->sc_iobase + 1, EX_NOWAIT);
		}
	}

	/* Figure out the memory mapped I/O address range of the bridge. */
	blr = pci_conf_read(pc, pa->pa_tag, PPB_REG_MEM);
	sc->sc_membase = (blr & 0x0000fff0) << 16;
	sc->sc_memlimit = (blr & 0xfff00000) | 0x000fffff;
	if (sc->sc_memlimit > sc->sc_membase) {
		name = malloc(PPB_EXNAMLEN, M_DEVBUF, M_NOWAIT);
		if (name) {
			snprintf(name, PPB_EXNAMLEN, "%s pcimem", sc->sc_dev.dv_xname);
			sc->sc_memex = extent_create(name, 0, (u_long)-1L,
			    M_DEVBUF, NULL, 0, EX_NOWAIT | EX_FILLED);
			extent_free(sc->sc_memex, sc->sc_membase,
			    sc->sc_memlimit - sc->sc_membase + 1,
			    EX_NOWAIT);
		}
	}

	/* Figure out the prefetchable MMI/O address range of the bridge. */
	blr = pci_conf_read(pc, pa->pa_tag, PPB_REG_PREFMEM);
	sc->sc_pmembase = (blr & 0x0000fff0) << 16;
	sc->sc_pmemlimit = (blr & 0xfff00000) | 0x000fffff;
#ifdef __LP64__
	blr = pci_conf_read(pc, pa->pa_tag, PPB_REG_PREFBASE_HI32);
	sc->sc_pmembase |= ((uint64_t)blr) << 32;
	blr = pci_conf_read(pc, pa->pa_tag, PPB_REG_PREFLIM_HI32);
	sc->sc_pmemlimit |= ((uint64_t)blr) << 32;
#endif
	if (sc->sc_pmemlimit > sc->sc_pmembase) {
		name = malloc(PPB_EXNAMLEN, M_DEVBUF, M_NOWAIT);
		if (name) {
			snprintf(name, PPB_EXNAMLEN, "%s pcipmem", sc->sc_dev.dv_xname);
			sc->sc_pmemex = extent_create(name, 0, (u_long)-1L,
			    M_DEVBUF, NULL, 0, EX_NOWAIT | EX_FILLED);
			extent_free(sc->sc_pmemex, sc->sc_pmembase,
			    sc->sc_pmemlimit - sc->sc_pmembase + 1,
			    EX_NOWAIT);
		}
	}

	if (interface == PPB_INTERFACE_SUBTRACTIVE) {
		if (sc->sc_ioex == NULL)
			sc->sc_ioex = pa->pa_ioex;
		if (sc->sc_memex == NULL)
			sc->sc_memex = pa->pa_memex;
	}

 attach:
	/*
	 * Attach the PCI bus that hangs off of it.
	 *
	 * XXX Don't pass-through Memory Read Multiple.  Should we?
	 * XXX Consult the spec...
	 */
	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = pa->pa_iot;
	pba.pba_memt = pa->pa_memt;
	pba.pba_dmat = pa->pa_dmat;
	pba.pba_pc = pc;
	pba.pba_flags = pa->pa_flags & ~PCI_FLAGS_MRM_OKAY;
	pba.pba_busex = sc->sc_busex;
	pba.pba_ioex = sc->sc_ioex;
	pba.pba_memex = sc->sc_memex;
	pba.pba_pmemex = sc->sc_pmemex;
	pba.pba_domain = pa->pa_domain;
	pba.pba_bus = PPB_BUSINFO_SECONDARY(busdata);
	pba.pba_bridgeih = sc->sc_ih;
	pba.pba_bridgetag = &sc->sc_tag;
	pba.pba_intrswiz = pa->pa_intrswiz;
	pba.pba_intrtag = pa->pa_intrtag;

	sc->sc_psc = config_found(self, &pba, ppbprint);
}

int
ppbdetach(struct device *self, int flags)
{
	struct ppb_softc *sc = (struct ppb_softc *)self;
	char *name;
	int rv;

	if (sc->sc_intrhand)
		pci_intr_disestablish(sc->sc_pc, sc->sc_intrhand);

	rv = config_detach_children(self, flags);

	if (sc->sc_busex) {
		name = sc->sc_busex->ex_name;
		extent_destroy(sc->sc_busex);
		free(name, M_DEVBUF, PPB_EXNAMLEN);
	}

	if (sc->sc_ioex) {
		name = sc->sc_ioex->ex_name;
		extent_destroy(sc->sc_ioex);
		free(name, M_DEVBUF, PPB_EXNAMLEN);
	}

	if (sc->sc_memex) {
		name = sc->sc_memex->ex_name;
		extent_destroy(sc->sc_memex);
		free(name, M_DEVBUF, PPB_EXNAMLEN);
	}

	if (sc->sc_pmemex) {
		name = sc->sc_pmemex->ex_name;
		extent_destroy(sc->sc_pmemex);
		free(name, M_DEVBUF, PPB_EXNAMLEN);
	}

	if (sc->sc_parent_busex && sc->sc_busrange > 0)
		extent_free(sc->sc_parent_busex, sc->sc_busnum,
		    sc->sc_busrange, EX_NOWAIT);

	return (rv);
}

int
ppbactivate(struct device *self, int act)
{
	struct ppb_softc *sc = (void *)self;
	pci_chipset_tag_t pc = sc->sc_pc;
	pcitag_t tag = sc->sc_tag;
	pcireg_t blr, reg;
	int off, rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);

		/* Save registers that may get lost. */
		sc->sc_csr = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
		sc->sc_bhlcr = pci_conf_read(pc, tag, PCI_BHLC_REG);
		sc->sc_bir = pci_conf_read(pc, tag, PPB_REG_BUSINFO);
		sc->sc_bcr = pci_conf_read(pc, tag, PPB_REG_BRIDGECONTROL);
		sc->sc_int = pci_conf_read(pc, tag, PCI_INTERRUPT_REG);
		if (sc->sc_cap_off)
			sc->sc_slcsr = pci_conf_read(pc, tag,
			    sc->sc_cap_off + PCI_PCIE_SLCSR);

		if (pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg)) {
			sc->sc_msi_ma = pci_conf_read(pc, tag,
			    off + PCI_MSI_MA);
			if (reg & PCI_MSI_MC_C64) {
				sc->sc_msi_mau32 = pci_conf_read(pc, tag,
				    off + PCI_MSI_MAU32);
				sc->sc_msi_md = pci_conf_read(pc, tag,
				    off + PCI_MSI_MD64);
			} else {
				sc->sc_msi_md = pci_conf_read(pc, tag,
				    off + PCI_MSI_MD32);
			}
			sc->sc_msi_mc = reg;
		}
		break;
	case DVACT_RESUME:
		if (pci_dopm) {
			/* Restore power. */
			pci_set_powerstate(pc, tag, sc->sc_pmcsr_state);
		}

		/* Restore the registers saved above. */
		pci_conf_write(pc, tag, PCI_BHLC_REG, sc->sc_bhlcr);
		pci_conf_write(pc, tag, PPB_REG_BUSINFO, sc->sc_bir);
		pci_conf_write(pc, tag, PPB_REG_BRIDGECONTROL, sc->sc_bcr);
		pci_conf_write(pc, tag, PCI_INTERRUPT_REG, sc->sc_int);
		if (sc->sc_cap_off)
			pci_conf_write(pc, tag,
			    sc->sc_cap_off + PCI_PCIE_SLCSR, sc->sc_slcsr);

		/* Restore I/O window. */
		blr = pci_conf_read(pc, tag, PPB_REG_IOSTATUS);
		blr &= 0xffff0000;
		blr |= sc->sc_iolimit & PPB_IO_MASK;
		blr |= (sc->sc_iobase >> PPB_IO_SHIFT);
		pci_conf_write(pc, tag, PPB_REG_IOSTATUS, blr);
		blr = (sc->sc_iobase & 0xffff0000) >> 16;
		blr |= sc->sc_iolimit & 0xffff0000;
		pci_conf_write(pc, tag, PPB_REG_IO_HI, blr);

		/* Restore memory mapped I/O window. */
		blr = sc->sc_memlimit & PPB_MEM_MASK;
		blr |= (sc->sc_membase >> PPB_MEM_SHIFT);
		pci_conf_write(pc, tag, PPB_REG_MEM, blr);

		/* Restore prefetchable MMI/O window. */
		blr = sc->sc_pmemlimit & PPB_MEM_MASK;
		blr |= ((sc->sc_pmembase & PPB_MEM_MASK) >> PPB_MEM_SHIFT);
		pci_conf_write(pc, tag, PPB_REG_PREFMEM, blr);
#ifdef __LP64__
		pci_conf_write(pc, tag, PPB_REG_PREFBASE_HI32,
		    sc->sc_pmembase >> 32);
		pci_conf_write(pc, tag, PPB_REG_PREFLIM_HI32,
		    sc->sc_pmemlimit >> 32);
#endif

		if (pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg)) {
			pci_conf_write(pc, tag, off + PCI_MSI_MA,
			    sc->sc_msi_ma);
			if (reg & PCI_MSI_MC_C64) {
				pci_conf_write(pc, tag, off + PCI_MSI_MAU32,
				    sc->sc_msi_mau32);
				pci_conf_write(pc, tag, off + PCI_MSI_MD64,
				    sc->sc_msi_md);
			} else {
				pci_conf_write(pc, tag, off + PCI_MSI_MD32,
				    sc->sc_msi_md);
			}
			pci_conf_write(pc, tag, off + PCI_MSI_MC,
			    sc->sc_msi_mc);
		}

		/*
		 * Restore command register last to avoid exposing
		 * uninitialised windows.
		 */
		reg = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
		pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG,
		    (reg & 0xffff0000) | (sc->sc_csr & 0x0000ffff));

		rv = config_activate_children(self, act);
		break;
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
		
		if (pci_dopm) {	
			/*
			 * Place the bridge into the lowest possible
			 * power state.
			 */
			sc->sc_pmcsr_state = pci_get_powerstate(pc, tag);
			pci_set_powerstate(pc, tag,
			    pci_min_powerstate(pc, tag));
		}
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

void
ppb_alloc_busrange(struct ppb_softc *sc, struct pci_attach_args *pa,
    pcireg_t *busdata)
{
	pci_chipset_tag_t pc = sc->sc_pc;
	u_long busnum, busrange = 0;

#ifdef __HAVE_FDT
	int node = PCITAG_NODE(pa->pa_tag);
	uint32_t bus_range[2];

	if (node && OF_getpropintarray(node, "bus-range", bus_range,
	    sizeof(bus_range)) == sizeof(bus_range)) {
		if (extent_alloc_region(pa->pa_busex, bus_range[0],
		    bus_range[1] - bus_range[0] + 1, EX_NOWAIT) == 0) {
			busnum = bus_range[0];
			busrange = bus_range[1] - bus_range[0] + 1;
		}
	}
#endif

	if (busrange == 0) {
		for (busrange = 16; busrange > 0; busrange >>= 1) {
			if (extent_alloc(pa->pa_busex, busrange, 1, 0, 0, 
			    EX_NOWAIT, &busnum) == 0)
				break;
		}
	}

	if (busrange > 0) {
		*busdata |= pa->pa_bus;
		*busdata |= (busnum << 8);
		*busdata |= ((busnum + busrange - 1) << 16);
		pci_conf_write(pc, pa->pa_tag, PPB_REG_BUSINFO, *busdata);
	}
}

void
ppb_alloc_resources(struct ppb_softc *sc, struct pci_attach_args *pa)
{
	pci_chipset_tag_t pc = sc->sc_pc;
	pcireg_t id, busdata, blr, bhlcr, type, csr;
	pcireg_t addr, mask;
	pcitag_t tag;
	int bus, dev;
	int reg, reg_start, reg_end, reg_rom;
	int io_count = 0;
	int mem_count = 0;
	bus_addr_t start, end;
	u_long base, size;

	if (pa->pa_memex == NULL)
		return;

	busdata = pci_conf_read(pc, sc->sc_tag, PPB_REG_BUSINFO);
	bus = PPB_BUSINFO_SECONDARY(busdata);
	if (bus == 0)
		return;

	/*
	 * Count number of devices.  If there are no devices behind
	 * this bridge, there's no point in allocating any address
	 * space.
	 */
	for (dev = 0; dev < pci_bus_maxdevs(pc, bus); dev++) {
		tag = pci_make_tag(pc, bus, dev, 0);
		id = pci_conf_read(pc, tag, PCI_ID_REG);

		if (PCI_VENDOR(id) == PCI_VENDOR_INVALID ||
		    PCI_VENDOR(id) == 0)
			continue;

		bhlcr = pci_conf_read(pc, tag, PCI_BHLC_REG);
		switch (PCI_HDRTYPE_TYPE(bhlcr)) {
		case 0:
			reg_start = PCI_MAPREG_START;
			reg_end = PCI_MAPREG_END;
			reg_rom = PCI_ROM_REG;
			break;
		case 1:	/* PCI-PCI bridge */
			reg_start = PCI_MAPREG_START;
			reg_end = PCI_MAPREG_PPB_END;
			reg_rom = 0;	/* 0x38 */
			io_count++;
			mem_count++;
			break;
		case 2:	/* PCI-Cardbus bridge */
			reg_start = PCI_MAPREG_START;
			reg_end = PCI_MAPREG_PCB_END;
			reg_rom = 0;
			io_count++;
			mem_count++;
			break;
		default:
			return;
		}

		for (reg = reg_start; reg < reg_end; reg += 4) {
			if (pci_mapreg_probe(pc, tag, reg, &type) == 0)
				continue;

			if (type == PCI_MAPREG_TYPE_IO)
				io_count++;
			else
				mem_count++;

			if (type == (PCI_MAPREG_TYPE_MEM |
			    PCI_MAPREG_MEM_TYPE_64BIT))
				reg += 4;
		}

		if (reg_rom != 0) {
			addr = pci_conf_read(pc, tag, reg_rom);
			pci_conf_write(pc, tag, reg_rom, ~PCI_ROM_ENABLE);
			mask = pci_conf_read(pc, tag, reg_rom);
			pci_conf_write(pc, tag, reg_rom, addr);
			if (PCI_ROM_SIZE(mask))
				mem_count++;
		}
	}

	csr = pci_conf_read(pc, sc->sc_tag, PCI_COMMAND_STATUS_REG);

	/*
	 * Get the bridge in a consistent state.  If memory mapped I/O or
	 * port I/O is disabled, disabled the associated windows as well.
	 */
	if ((csr & PCI_COMMAND_MEM_ENABLE) == 0) {
		pci_conf_write(pc, sc->sc_tag, PPB_REG_MEM, 0x0000ffff);
		pci_conf_write(pc, sc->sc_tag, PPB_REG_PREFMEM, 0x0000ffff);
		pci_conf_write(pc, sc->sc_tag, PPB_REG_PREFBASE_HI32, 0);
		pci_conf_write(pc, sc->sc_tag, PPB_REG_PREFLIM_HI32, 0);
	}
	if ((csr & PCI_COMMAND_IO_ENABLE) == 0) {
		pci_conf_write(pc, sc->sc_tag, PPB_REG_IOSTATUS, 0x000000ff);
		pci_conf_write(pc, sc->sc_tag, PPB_REG_IO_HI, 0x0000ffff);
	}

	/* Allocate I/O address space if necessary. */
	if (io_count > 0 && pa->pa_ioex) {
		blr = pci_conf_read(pc, sc->sc_tag, PPB_REG_IOSTATUS);
		sc->sc_iobase = (blr << PPB_IO_SHIFT) & PPB_IO_MASK;
		sc->sc_iolimit = (blr & PPB_IO_MASK) | 0x00000fff;
		blr = pci_conf_read(pc, sc->sc_tag, PPB_REG_IO_HI);
		sc->sc_iobase |= (blr & 0x0000ffff) << 16;
		sc->sc_iolimit |= (blr & 0xffff0000);
		if (sc->sc_iolimit < sc->sc_iobase || sc->sc_iobase == 0) {
			start = max(PCI_IO_START, pa->pa_ioex->ex_start);
			end = min(PCI_IO_END, pa->pa_ioex->ex_end);
			for (size = 0x2000; size >= PPB_IO_MIN; size >>= 1)
				if (extent_alloc_subregion(pa->pa_ioex, start,
				    end, size, size, 0, 0, 0, &base) == 0)
					break;
			if (size >= PPB_IO_MIN) {
				sc->sc_iobase = base;
				sc->sc_iolimit = base + size - 1;
				blr = pci_conf_read(pc, sc->sc_tag,
				    PPB_REG_IOSTATUS);
				blr &= 0xffff0000;
				blr |= sc->sc_iolimit & PPB_IO_MASK;
				blr |= (sc->sc_iobase >> PPB_IO_SHIFT);
				pci_conf_write(pc, sc->sc_tag,
				    PPB_REG_IOSTATUS, blr);
				blr = (sc->sc_iobase & 0xffff0000) >> 16;
				blr |= sc->sc_iolimit & 0xffff0000;
				pci_conf_write(pc, sc->sc_tag,
				    PPB_REG_IO_HI, blr);

				csr |= PCI_COMMAND_IO_ENABLE;
			}
		}
	}

	/* Allocate memory mapped I/O address space if necessary. */
	if (mem_count > 0 && pa->pa_memex) {
		blr = pci_conf_read(pc, sc->sc_tag, PPB_REG_MEM);
		sc->sc_membase = (blr << PPB_MEM_SHIFT) & PPB_MEM_MASK;
		sc->sc_memlimit = (blr & PPB_MEM_MASK) | 0x000fffff;
		if (sc->sc_memlimit < sc->sc_membase || sc->sc_membase == 0) {
			start = max(PCI_MEM_START, pa->pa_memex->ex_start);
			end = min(PCI_MEM_END, pa->pa_memex->ex_end);
			for (size = 0x2000000; size >= PPB_MEM_MIN; size >>= 1)
				if (extent_alloc_subregion(pa->pa_memex, start,
				    end, size, size, 0, 0, 0, &base) == 0)
					break;
			if (size >= PPB_MEM_MIN) {
				sc->sc_membase = base;
				sc->sc_memlimit = base + size - 1;
				blr = sc->sc_memlimit & PPB_MEM_MASK;
				blr |= (sc->sc_membase >> PPB_MEM_SHIFT);
				pci_conf_write(pc, sc->sc_tag,
				    PPB_REG_MEM, blr);

				csr |= PCI_COMMAND_MEM_ENABLE;
			}
		}
	}

	/* Enable bus master. */
	csr |= PCI_COMMAND_MASTER_ENABLE;

	pci_conf_write(pc, sc->sc_tag, PCI_COMMAND_STATUS_REG, csr);
}

int
ppb_intr(void *arg)
{
	struct ppb_softc *sc = arg;
	pcireg_t reg;

	/*
	 * XXX ignore hotplug events while in autoconf.  On some
	 * machines with onboard re(4), we get a bogus hotplug remove
	 * event when we reset that device.  Ignoring that event makes
	 * sure we will not try to forcibly detach re(4) when it isn't
	 * ready to deal with that.
	 */
	if (cold)
		return (0);

	reg = pci_conf_read(sc->sc_pc, sc->sc_tag,
	    sc->sc_cap_off + PCI_PCIE_SLCSR);
	if (reg & PCI_PCIE_SLCSR_PDC) {
		if (reg & PCI_PCIE_SLCSR_PDS)
			task_add(systq, &sc->sc_insert_task);
		else
			task_add(systq, &sc->sc_remove_task);

		/* Clear interrupts. */
		pci_conf_write(sc->sc_pc, sc->sc_tag,
		    sc->sc_cap_off + PCI_PCIE_SLCSR, reg);
		return (1);
	}

	return (0);
}

#ifdef PCI_MACHDEP_ENUMERATE_BUS
#define pci_enumerate_bus PCI_MACHDEP_ENUMERATE_BUS
#else
extern int pci_enumerate_bus(struct pci_softc *,
    int (*)(struct pci_attach_args *), struct pci_attach_args *);
#endif

void
ppb_hotplug_insert(void *xsc)
{
	struct ppb_softc *sc = xsc;
	struct pci_softc *psc = (struct pci_softc *)sc->sc_psc;

	if (!LIST_EMPTY(&psc->sc_devs))
		return;

	/* XXX Powerup the card. */

	/* XXX Turn on LEDs. */

	/* Wait a second for things to settle. */
	timeout_add_sec(&sc->sc_to, 1);
}

void
ppb_hotplug_insert_finish(void *arg)
{
	struct ppb_softc *sc = arg;

	task_add(systq, &sc->sc_rescan_task);
}

void
ppb_hotplug_rescan(void *xsc)
{
	struct ppb_softc *sc = xsc;
	struct pci_softc *psc = (struct pci_softc *)sc->sc_psc;

	if (psc)
		pci_enumerate_bus(psc, NULL, NULL);
}

void
ppb_hotplug_remove(void *xsc)
{
	struct ppb_softc *sc = xsc;
	struct pci_softc *psc = (struct pci_softc *)sc->sc_psc;

	if (psc) {
		pci_detach_devices(psc, DETACH_FORCE);

		/*
		 * XXX Allocate the entire window with EX_CONFLICTOK
		 * such that we can easily free it.
		 */
		if (sc->sc_ioex != NULL) {
			extent_alloc_region(sc->sc_ioex, sc->sc_iobase,
			    sc->sc_iolimit - sc->sc_iobase + 1,
			    EX_NOWAIT | EX_CONFLICTOK);
			extent_free(sc->sc_ioex, sc->sc_iobase,
			    sc->sc_iolimit - sc->sc_iobase + 1, EX_NOWAIT);
		}

		if (sc->sc_memex != NULL) {
			extent_alloc_region(sc->sc_memex, sc->sc_membase,
			    sc->sc_memlimit - sc->sc_membase + 1,
			    EX_NOWAIT | EX_CONFLICTOK);
			extent_free(sc->sc_memex, sc->sc_membase,
			    sc->sc_memlimit - sc->sc_membase + 1, EX_NOWAIT);
		}

		if (sc->sc_pmemex != NULL) {
			extent_alloc_region(sc->sc_pmemex, sc->sc_pmembase,
			    sc->sc_pmemlimit - sc->sc_pmembase + 1,
			    EX_NOWAIT | EX_CONFLICTOK);
			extent_free(sc->sc_pmemex, sc->sc_pmembase,
			    sc->sc_pmemlimit - sc->sc_pmembase + 1, EX_NOWAIT);
		}
	}
}

int
ppbprint(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	/* only PCIs can attach to PPBs; easy. */
	if (pnp)
		printf("pci at %s", pnp);
	printf(" bus %d", pba->pba_bus);
	return (UNCONF);
}
