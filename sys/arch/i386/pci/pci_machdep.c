/*	$OpenBSD: pci_machdep.c,v 1.89 2025/01/23 11:24:34 kettenis Exp $	*/
/*	$NetBSD: pci_machdep.c,v 1.28 1997/06/06 23:29:17 thorpej Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
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
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
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
 * Machine-specific functions for PCI autoconfiguration.
 *
 * On PCs, there are two methods of generating PCI configuration cycles.
 * We try to detect the appropriate mechanism for this machine and set
 * up a few function pointers to access the correct method directly.
 *
 * The configuration method can be hard-coded in the config file by
 * using `options PCI_CONF_MODE=N', where `N' is the configuration mode
 * as defined section 3.6.4.1, `Generating Configuration Cycles'.
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/pio.h>
#include <machine/i8259.h>
#include <machine/biosvar.h>

#include "bios.h"
#if NBIOS > 0
extern bios_pciinfo_t *bios_pciinfo;
#endif

#include <dev/isa/isavar.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/ppbreg.h>

#include "ioapic.h"

#include <machine/i82093var.h>
#include <machine/i82489reg.h>
#include <machine/i82489var.h>
#if NIOAPIC > 0
#include <machine/mpbiosvar.h>
#endif

#include "pcibios.h"
#if NPCIBIOS > 0
#include <i386/pci/pcibiosvar.h>
#endif

int pci_mode = -1;

/*
 * Memory Mapped Configuration space access.
 *
 * Since mapping the whole configuration space will cost us up to
 * 256MB of kernel virtual memory, we use separate mappings per bus.
 * The mappings are created on-demand, such that we only use kernel
 * virtual memory for busses that are actually present.
 */
bus_addr_t pci_mcfg_addr;
int pci_mcfg_min_bus, pci_mcfg_max_bus;
bus_space_tag_t pci_mcfgt = I386_BUS_SPACE_MEM;
bus_space_handle_t pci_mcfgh[256];
void pci_mcfg_map_bus(int);

struct mutex pci_conf_lock = MUTEX_INITIALIZER(IPL_HIGH);

#define	PCI_CONF_LOCK()							\
do {									\
	mtx_enter(&pci_conf_lock);					\
} while (0)

#define	PCI_CONF_UNLOCK()						\
do {									\
	mtx_leave(&pci_conf_lock);					\
} while (0)

#define	PCI_MODE1_ENABLE	0x80000000UL
#define	PCI_MODE1_ADDRESS_REG	0x0cf8
#define	PCI_MODE1_DATA_REG	0x0cfc

#define	PCI_MODE2_ENABLE_REG	0x0cf8
#define	PCI_MODE2_FORWARD_REG	0x0cfa

#define _m1tag(b, d, f) \
	(PCI_MODE1_ENABLE | ((b) << 16) | ((d) << 11) | ((f) << 8))
#define _qe(bus, dev, fcn, vend, prod) \
	{_m1tag(bus, dev, fcn), PCI_ID_CODE(vend, prod)}
struct {
	u_int32_t tag;
	pcireg_t id;
} pcim1_quirk_tbl[] = {
	_qe(0, 0, 0, PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_TRIFLEX1),
	/* XXX Triflex2 not tested */
	_qe(0, 0, 0, PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_TRIFLEX2),
	_qe(0, 0, 0, PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_TRIFLEX4),
	/* Triton needed for Connectix Virtual PC */
	_qe(0, 0, 0, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82437FX),
	/* Connectix Virtual PC 5 has a 440BX */
	_qe(0, 0, 0, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82443BX_NOAGP),
	{0, 0xffffffff} /* patchable */
};
#undef _m1tag
#undef _qe

/*
 * PCI doesn't have any special needs; just use the generic versions
 * of these functions.
 */
struct bus_dma_tag pci_bus_dma_tag = {
	NULL,			/* _cookie */
	_bus_dmamap_create, 
	_bus_dmamap_destroy,
	_bus_dmamap_load,
	_bus_dmamap_load_mbuf,
	_bus_dmamap_load_uio,
	_bus_dmamap_load_raw,
	_bus_dmamap_unload,
	_bus_dmamap_sync,
	_bus_dmamem_alloc,
	_bus_dmamem_alloc_range,
	_bus_dmamem_free,
	_bus_dmamem_map,
	_bus_dmamem_unmap,
	_bus_dmamem_mmap,
};

void
pci_mcfg_init(bus_space_tag_t iot, bus_addr_t addr, int segment,
    int min_bus, int max_bus)
{
	if (segment == 0) {
		pci_mcfgt = iot;
		pci_mcfg_addr = addr;
		pci_mcfg_min_bus = min_bus;
		pci_mcfg_max_bus = max_bus;
	}
}

pci_chipset_tag_t
pci_lookup_segment(int segment, int bus)
{
	KASSERT(segment == 0);
	return NULL;
}

void
pci_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
	pci_chipset_tag_t pc = pba->pba_pc;
	pcitag_t tag;
	pcireg_t id, class;

#if NBIOS > 0
	if (pba->pba_bus == 0)
		printf(": configuration mode %d (%s)",
			pci_mode, (bios_pciinfo?"bios":"no bios"));
#else
	if (pba->pba_bus == 0)
		printf(": configuration mode %d", pci_mode);
#endif

	if (pba->pba_bus != 0)
		return;

	/*
	 * Machines that use the non-standard method of generating PCI
	 * configuration cycles are way too old to support MSI.
	 */
	if (pci_mode == 2)
		return;

	/*
	 * In order to decide whether the system supports MSI we look
	 * at the host bridge, which should be device 0 function 0 on
	 * bus 0.  It is better to not enable MSI on systems that
	 * support it than the other way around, so be conservative
	 * here.  So we don't enable MSI if we don't find a host
	 * bridge there.  We also deliberately don't enable MSI on
	 * chipsets from low-end manufacturers like VIA and SiS.
	 */
	tag = pci_make_tag(pc, 0, 0, 0);
	id = pci_conf_read(pc, tag, PCI_ID_REG);
	class = pci_conf_read(pc, tag, PCI_CLASS_REG);

	if (PCI_CLASS(class) != PCI_CLASS_BRIDGE ||
	    PCI_SUBCLASS(class) != PCI_SUBCLASS_BRIDGE_HOST)
		return;

	switch (PCI_VENDOR(id)) {
	case PCI_VENDOR_INTEL:
		/*
		 * For Intel platforms, MSI support was introduced
		 * with the new Pentium 4 processor interrupt delivery
		 * mechanism, so we blacklist all PCI chipsets that
		 * support Pentium III and earlier CPUs.
		 */
		switch (PCI_PRODUCT(id)) {
		case PCI_PRODUCT_INTEL_PCMC: /* 82434LX/NX */
		case PCI_PRODUCT_INTEL_82437FX:
		case PCI_PRODUCT_INTEL_82437MX:
		case PCI_PRODUCT_INTEL_82437VX:
		case PCI_PRODUCT_INTEL_82439HX:
		case PCI_PRODUCT_INTEL_82439TX:
		case PCI_PRODUCT_INTEL_82440BX:
		case PCI_PRODUCT_INTEL_82440BX_AGP:
		case PCI_PRODUCT_INTEL_82440MX_HB:
		case PCI_PRODUCT_INTEL_82441FX:
		case PCI_PRODUCT_INTEL_82443BX:
		case PCI_PRODUCT_INTEL_82443BX_AGP:
		case PCI_PRODUCT_INTEL_82443BX_NOAGP:
		case PCI_PRODUCT_INTEL_82443GX:
		case PCI_PRODUCT_INTEL_82443LX:
		case PCI_PRODUCT_INTEL_82443LX_AGP:
		case PCI_PRODUCT_INTEL_82810_HB:
		case PCI_PRODUCT_INTEL_82810E_HB:
		case PCI_PRODUCT_INTEL_82815_HB:
		case PCI_PRODUCT_INTEL_82820_HB:
		case PCI_PRODUCT_INTEL_82830M_HB:
		case PCI_PRODUCT_INTEL_82840_HB:
			break;
		default:
			pba->pba_flags |= PCI_FLAGS_MSI_ENABLED;
			break;
		}
		break;
	case PCI_VENDOR_NVIDIA:
		/*
		 * Since NVIDIA chipsets are completely undocumented,
		 * we have to make a guess here.  We assume that all
		 * chipsets that support PCIe include support for MSI,
		 * since support for MSI is mandated by the PCIe
		 * standard.
		 */
		switch (PCI_PRODUCT(id)) {
		case PCI_PRODUCT_NVIDIA_NFORCE_PCHB:
		case PCI_PRODUCT_NVIDIA_NFORCE2_PCHB:
			break;
		default:
			pba->pba_flags |= PCI_FLAGS_MSI_ENABLED;
			break;
		}
		break;
	case PCI_VENDOR_AMD:
		/*
		 * The AMD-750 and AMD-760 chipsets don't support MSI.
		 */
		switch (PCI_PRODUCT(id)) {
		case PCI_PRODUCT_AMD_SC751_SC:
		case PCI_PRODUCT_AMD_761_PCHB:
		case PCI_PRODUCT_AMD_762_PCHB:
			break;
		default:
			pba->pba_flags |= PCI_FLAGS_MSI_ENABLED;
			break;
		}
		break;
	}

	/* Enable MSI for QEMU */
	id = pci_conf_read(pc, tag, PCI_SUBSYS_ID_REG);
	if (PCI_VENDOR(id) == PCI_VENDOR_QUMRANET)
		pba->pba_flags |= PCI_FLAGS_MSI_ENABLED;

	/*
	 * Don't enable MSI on a HyperTransport bus.  In order to
	 * determine that bus 0 is a HyperTransport bus, we look at
	 * device 24 function 0, which is the HyperTransport
	 * host/primary interface integrated on most 64-bit AMD CPUs.
	 * If that device has a HyperTransport capability, bus 0 must
	 * be a HyperTransport bus and we disable MSI.
	 */
	tag = pci_make_tag(pc, 0, 24, 0);
	if (pci_get_capability(pc, tag, PCI_CAP_HT, NULL, NULL))
		pba->pba_flags &= ~PCI_FLAGS_MSI_ENABLED;
}

int
pci_bus_maxdevs(pci_chipset_tag_t pc, int busno)
{

	/*
	 * Bus number is irrelevant.  If Configuration Mechanism 2 is in
	 * use, can only have devices 0-15 on any bus.  If Configuration
	 * Mechanism 1 is in use, can have devices 0-31 (i.e. the `normal'
	 * range).
	 */
	if (pci_mode == 2)
		return (16);
	else
		return (32);
}

pcitag_t
pci_make_tag(pci_chipset_tag_t pc, int bus, int device, int function)
{
	pcitag_t tag;

	switch (pci_mode) {
	case 1:
		if (bus >= 256 || device >= 32 || function >= 8)
			panic("pci_make_tag: bad request");

		tag.mode1 = PCI_MODE1_ENABLE |
		    	(bus << 16) | (device << 11) | (function << 8);
		break;
	case 2:
		if (bus >= 256 || device >= 16 || function >= 8)
			panic("pci_make_tag: bad request");

		tag.mode2.port = 0xc000 | (device << 8);
		tag.mode2.enable = 0xf0 | (function << 1);
		tag.mode2.forward = bus;
		break;
	default:
		panic("pci_make_tag: mode not configured");
	}

	return tag;
}

void
pci_decompose_tag(pci_chipset_tag_t pc, pcitag_t tag, int *bp, int *dp, int *fp)
{
	switch (pci_mode) {
	case 1:
		if (bp != NULL)
			*bp = (tag.mode1 >> 16) & 0xff;
		if (dp != NULL)
			*dp = (tag.mode1 >> 11) & 0x1f;
		if (fp != NULL)
			*fp = (tag.mode1 >> 8) & 0x7;
		break;
	case 2:
		if (bp != NULL)
			*bp = tag.mode2.forward & 0xff;
		if (dp != NULL)
			*dp = (tag.mode2.port >> 8) & 0xf;
		if (fp != NULL)
			*fp = (tag.mode2.enable >> 1) & 0x7;
		break;
	default:
		panic("pci_decompose_tag: mode not configured");
	}
}

int
pci_conf_size(pci_chipset_tag_t pc, pcitag_t tag)
{
	int bus;

	if (pci_mcfg_addr) {
		pci_decompose_tag(pc, tag, &bus, NULL, NULL);
		if (bus >= pci_mcfg_min_bus && bus <= pci_mcfg_max_bus)
			return PCIE_CONFIG_SPACE_SIZE;
	}

	return PCI_CONFIG_SPACE_SIZE;
}

void
pci_mcfg_map_bus(int bus)
{
	if (pci_mcfgh[bus])
		return;

	if (bus_space_map(pci_mcfgt, pci_mcfg_addr + (bus << 20), 1 << 20,
	    0, &pci_mcfgh[bus]))
		panic("pci_conf_read: cannot map mcfg space");
}

pcireg_t
pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	pcireg_t data;
	int bus;

	KASSERT((reg & 0x3) == 0);

	if (pci_mcfg_addr && reg >= PCI_CONFIG_SPACE_SIZE) {
		pci_decompose_tag(pc, tag, &bus, NULL, NULL);
		if (bus >= pci_mcfg_min_bus && bus <= pci_mcfg_max_bus) {
			pci_mcfg_map_bus(bus);
			data = bus_space_read_4(pci_mcfgt, pci_mcfgh[bus],
			    (tag.mode1 & 0x000ff00) << 4 | reg);
			return data;
		}
	}

	PCI_CONF_LOCK();
	switch (pci_mode) {
	case 1:
		outl(PCI_MODE1_ADDRESS_REG, tag.mode1 | reg);
		data = inl(PCI_MODE1_DATA_REG);
		outl(PCI_MODE1_ADDRESS_REG, 0);
		break;
	case 2:
		outb(PCI_MODE2_ENABLE_REG, tag.mode2.enable);
		outb(PCI_MODE2_FORWARD_REG, tag.mode2.forward);
		data = inl(tag.mode2.port | reg);
		outb(PCI_MODE2_ENABLE_REG, 0);
		break;
	default:
		panic("pci_conf_read: mode not configured");
	}
	PCI_CONF_UNLOCK();

	return data;
}

void
pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t data)
{
	int bus;

	KASSERT((reg & 0x3) == 0);

	if (pci_mcfg_addr && reg >= PCI_CONFIG_SPACE_SIZE) {
		pci_decompose_tag(pc, tag, &bus, NULL, NULL);
		if (bus >= pci_mcfg_min_bus && bus <= pci_mcfg_max_bus) {
			pci_mcfg_map_bus(bus);
			bus_space_write_4(pci_mcfgt, pci_mcfgh[bus],
			    (tag.mode1 & 0x000ff00) << 4 | reg, data);
			return;
		}
	}

	PCI_CONF_LOCK();
	switch (pci_mode) {
	case 1:
		outl(PCI_MODE1_ADDRESS_REG, tag.mode1 | reg);
		outl(PCI_MODE1_DATA_REG, data);
		outl(PCI_MODE1_ADDRESS_REG, 0);
		break;
	case 2:
		outb(PCI_MODE2_ENABLE_REG, tag.mode2.enable);
		outb(PCI_MODE2_FORWARD_REG, tag.mode2.forward);
		outl(tag.mode2.port | reg, data);
		outb(PCI_MODE2_ENABLE_REG, 0);
		break;
	default:
		panic("pci_conf_write: mode not configured");
	}
	PCI_CONF_UNLOCK();
}

int
pci_mode_detect(void)
{

#ifdef PCI_CONF_MODE
#if (PCI_CONF_MODE == 1) || (PCI_CONF_MODE == 2)
	return (pci_mode = PCI_CONF_MODE);
#else
#error Invalid PCI configuration mode.
#endif
#else
	u_int32_t sav, val;
	int i;
	pcireg_t idreg;

	if (pci_mode != -1)
		return (pci_mode);

#if NBIOS > 0
	/*
	 * If we have PCI info passed from the BIOS, use the mode given there
	 * for all of this code.  If not, pass on through to the previous tests
	 * to try and divine the correct mode.
	 */
	if (bios_pciinfo != NULL) {
		if (bios_pciinfo->pci_chars & 0x2)
			return (pci_mode = 2);

		if (bios_pciinfo->pci_chars & 0x1)
			return (pci_mode = 1);

		/* We should never get here, but if we do, fall through... */
	}
#endif

	/*
	 * We try to divine which configuration mode the host bridge wants.
	 *
	 * This should really be done using the PCI BIOS.  If we get here, the
	 * PCI BIOS does not exist, or the boot blocks did not provide the
	 * information.
	 */

	sav = inl(PCI_MODE1_ADDRESS_REG);

	pci_mode = 1; /* assume this for now */
	/*
	 * catch some known buggy implementations of mode 1
	 */
	for (i = 0; i < sizeof(pcim1_quirk_tbl) / sizeof(pcim1_quirk_tbl[0]);
	     i++) {
		pcitag_t t;

		if (!pcim1_quirk_tbl[i].tag)
			break;
		t.mode1 = pcim1_quirk_tbl[i].tag;
		idreg = pci_conf_read(0, t, PCI_ID_REG); /* needs "pci_mode" */
		if (idreg == pcim1_quirk_tbl[i].id) {
#ifdef DEBUG
			printf("known mode 1 PCI chipset (%08x)\n",
			       idreg);
#endif
			return (pci_mode);
		}
	}

	/*
	 * Strong check for standard compliant mode 1:
	 * 1. bit 31 ("enable") can be set
	 * 2. byte/word access does not affect register
 	 */
	outl(PCI_MODE1_ADDRESS_REG, PCI_MODE1_ENABLE);
	outb(PCI_MODE1_ADDRESS_REG + 3, 0);
	outw(PCI_MODE1_ADDRESS_REG + 2, 0);
	val = inl(PCI_MODE1_ADDRESS_REG);
	if ((val & 0x80fffffc) != PCI_MODE1_ENABLE) {
#ifdef DEBUG
		printf("pci_mode_detect: mode 1 enable failed (%x)\n",
		       val);
#endif
		goto not1;
	}
	outl(PCI_MODE1_ADDRESS_REG, 0);
	val = inl(PCI_MODE1_ADDRESS_REG);
	if ((val & 0x80fffffc) != 0)
		goto not1;
	return (pci_mode);
not1:
	outl(PCI_MODE1_ADDRESS_REG, sav);
 
	/*
	 * This mode 2 check is quite weak (and known to give false
	 * positives on some Compaq machines).
	 * However, this doesn't matter, because this is the
	 * last test, and simply no PCI devices will be found if
	 * this happens.
	 */
	outb(PCI_MODE2_ENABLE_REG, 0);
	outb(PCI_MODE2_FORWARD_REG, 0);
	if (inb(PCI_MODE2_ENABLE_REG) != 0 ||
	    inb(PCI_MODE2_FORWARD_REG) != 0)
		goto not2;
	return (pci_mode = 2);
not2:
	return (pci_mode = 0);
#endif
}

int
pci_intr_map_msi(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;

	if ((pa->pa_flags & PCI_FLAGS_MSI_ENABLED) == 0 || mp_busses == NULL ||
	    pci_get_capability(pc, tag, PCI_CAP_MSI, NULL, NULL) == 0)
		return 1;

	ihp->tag = tag;
	ihp->line = APIC_INT_VIA_MSG;
	ihp->pin = 0;
	return 0;
}

int
pci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	int pin = pa->pa_rawintrpin;
	int line = pa->pa_intrline;
#if NIOAPIC > 0
	struct mp_intr_map *mip;
	int bus, dev, func;
#endif

	if (pin == 0) {
		/* No IRQ used. */
		goto bad;
	}

	if (pin > PCI_INTERRUPT_PIN_MAX) {
		printf("pci_intr_map: bad interrupt pin %d\n", pin);
		goto bad;
	}

	ihp->tag = pa->pa_tag;
	ihp->line = line;
	ihp->pin = pin;

#if NIOAPIC > 0
	pci_decompose_tag (pa->pa_pc, pa->pa_tag, &bus, &dev, &func);

	if (!(ihp->line & PCI_INT_VIA_ISA) && mp_busses != NULL) {
		int mpspec_pin = (dev << 2) | (pin - 1);

		if (bus < mp_nbusses) {
			for (mip = mp_busses[bus].mb_intrs;
			     mip != NULL; mip = mip->next) {
				if (&mp_busses[bus] == mp_isa_bus ||
				    &mp_busses[bus] == mp_eisa_bus)
					continue;
				if (mip->bus_pin == mpspec_pin) {
					ihp->line = mip->ioapic_ih | line;
					return 0;
				}
			}
		}

		if (pa->pa_bridgetag) {
			int swizpin = PPB_INTERRUPT_SWIZZLE(pin, dev);
			if (pa->pa_bridgeih[swizpin - 1].line != -1) {
				ihp->line = pa->pa_bridgeih[swizpin - 1].line;
				ihp->line |= line;
				return 0;
			}
		}
		/*
		 * No explicit PCI mapping found. This is not fatal,
		 * we'll try the ISA (or possibly EISA) mappings next.
		 */
	}
#endif

#if NPCIBIOS > 0
	pci_intr_header_fixup(pa->pa_pc, pa->pa_tag, ihp);
	line = ihp->line & APIC_INT_LINE_MASK;
#endif

	/*
	 * Section 6.2.4, `Miscellaneous Functions', says that 255 means
	 * `unknown' or `no connection' on a PC.  We assume that a device with
	 * `no connection' either doesn't have an interrupt (in which case the
	 * pin number should be 0, and would have been noticed above), or
	 * wasn't configured by the BIOS (in which case we punt, since there's
	 * no real way we can know how the interrupt lines are mapped in the
	 * hardware).
	 *
	 * XXX
	 * Since IRQ 0 is only used by the clock, and we can't actually be sure
	 * that the BIOS did its job, we also recognize that as meaning that
	 * the BIOS has not configured the device.
	 */
	if (line == 0 || line == I386_PCI_INTERRUPT_LINE_NO_CONNECTION)
		goto bad;

	if (line >= ICU_LEN) {
		printf("pci_intr_map: bad interrupt line %d\n", line);
		goto bad;
	}
	if (line == 2) {
		printf("pci_intr_map: changed line 2 to line 9\n");
		line = 9;
	}

#if NIOAPIC > 0
	if (!(ihp->line & PCI_INT_VIA_ISA) && mp_busses != NULL) {
		if (mip == NULL && mp_isa_bus) {
			for (mip = mp_isa_bus->mb_intrs; mip != NULL;
			    mip = mip->next) {
				if (mip->bus_pin == line) {
					ihp->line = mip->ioapic_ih | line;
					return 0;
				}
			}
		}
		if (mip == NULL && mp_eisa_bus) {
			for (mip = mp_eisa_bus->mb_intrs;  mip != NULL;
			    mip = mip->next) {
				if (mip->bus_pin == line) {
					ihp->line = mip->ioapic_ih | line;
					return 0;
				}
			}
		}
		if (mip == NULL) {
			printf("pci_intr_map: "
			    "bus %d dev %d func %d pin %d; line %d\n",
			    bus, dev, func, pin, line);
			printf("pci_intr_map: no MP mapping found\n");
		}
	}
#endif

	return 0;

bad:
	ihp->line = -1;
	return 1;
}

const char *
pci_intr_string(pci_chipset_tag_t pc, pci_intr_handle_t ih)
{
	static char irqstr[64];
	int line = ih.line & APIC_INT_LINE_MASK;

	if (ih.line & APIC_INT_VIA_MSG)
		return ("msi");

#if NIOAPIC > 0
	if (ih.line & APIC_INT_VIA_APIC) {
		snprintf(irqstr, sizeof irqstr, "apic %d int %d",
		     APIC_IRQ_APIC(ih.line), APIC_IRQ_PIN(ih.line));
		return (irqstr);
	}
#endif

	if (line == 0 || line >= ICU_LEN || line == 2)
		panic("pci_intr_string: bogus handle 0x%x", line);

	snprintf(irqstr, sizeof irqstr, "irq %d", line);
	return (irqstr);
}

#include "acpiprt.h"
#if NACPIPRT > 0
void	acpiprt_route_interrupt(int bus, int dev, int pin);
#endif

extern struct intrhand *apic_intrhand[256];
extern int apic_maxlevel[256];

void *
pci_intr_establish_cpu(pci_chipset_tag_t pc, pci_intr_handle_t ih,
    int level, struct cpu_info *ci,
    int (*func)(void *), void *arg, const char *what)
{
	if (ci != NULL && ci != &cpu_info_primary)
		return (NULL);

	return pci_intr_establish(pc, ih, level, func, arg, what);
}

void *
pci_intr_establish(pci_chipset_tag_t pc, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, const char *what)
{
	void *ret;
	int bus, dev;
	int l = ih.line & APIC_INT_LINE_MASK;
	pcitag_t tag = ih.tag;
	int irq = ih.line;

	if (ih.line & APIC_INT_VIA_MSG) {
		struct intrhand *ih;
		pcireg_t reg, addr;
		int off, vec;
		int flags;

		flags = level & IPL_MPSAFE;
		level &= ~IPL_MPSAFE;

		KASSERT(level <= IPL_TTY || flags & IPL_MPSAFE);

		if (pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg) == 0)
			panic("%s: no msi capability", __func__);

		vec = idt_vec_alloc(level, level + 15);
		if (vec == 0)
			return (NULL);

		ih = malloc(sizeof(*ih), M_DEVBUF, cold ? M_NOWAIT : M_WAITOK);
		if (ih == NULL)
			panic("%s: can't malloc handler info", __func__);

		ih->ih_fun = func;
		ih->ih_arg = arg;
		ih->ih_next = NULL;
		ih->ih_level = level;
		ih->ih_flags = flags;
		ih->ih_irq = irq;
		ih->ih_pin = tag.mode1;
		ih->ih_vec = vec;
		evcount_attach(&ih->ih_count, what, &ih->ih_vec);

		apic_maxlevel[vec] = level;
		apic_intrhand[vec] = ih;
		idt_vec_set(vec, apichandler[vec & 0xf]);

		addr = 0xfee00000UL | (cpu_info_primary.ci_apicid << 12);

		if (reg & PCI_MSI_MC_C64) {
			pci_conf_write(pc, tag, off + PCI_MSI_MA, addr);
			pci_conf_write(pc, tag, off + PCI_MSI_MAU32, 0);
			pci_conf_write(pc, tag, off + PCI_MSI_MD64, vec);
		} else {
			pci_conf_write(pc, tag, off + PCI_MSI_MA, addr);
			pci_conf_write(pc, tag, off + PCI_MSI_MD32, vec);
		}
		pci_conf_write(pc, tag, off, reg | PCI_MSI_MC_MSIE);
		return (ih);
	}

	pci_decompose_tag(pc, ih.tag, &bus, &dev, NULL);
#if NACPIPRT > 0
	acpiprt_route_interrupt(bus, dev, ih.pin);
#endif

#if NIOAPIC > 0
	if (l != -1 && ih.line & APIC_INT_VIA_APIC)
		return (apic_intr_establish(ih.line, IST_LEVEL, level, func, 
		    arg, what));
#endif
	if (l == 0 || l >= ICU_LEN || l == 2)
		panic("pci_intr_establish: bogus handle 0x%x", l);

	ret = isa_intr_establish(NULL, l, IST_LEVEL, level, func, arg, what);
#if NPCIBIOS > 0
	if (ret)
		pci_intr_route_link(pc, &ih);
#endif
	return (ret);
}

void
pci_intr_disestablish(pci_chipset_tag_t pc, void *cookie)
{
	struct intrhand *ih = cookie;

	if (ih->ih_irq & APIC_INT_VIA_MSG) {
		pcitag_t tag = { .mode1 = ih->ih_pin };
		pcireg_t reg;
		int off;
		
		if (pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg))
			pci_conf_write(pc, tag, off, reg &= ~PCI_MSI_MC_MSIE);

		apic_maxlevel[ih->ih_vec] = 0;
		apic_intrhand[ih->ih_vec] = NULL;
		idt_vec_free(ih->ih_vec);

		evcount_detach(&ih->ih_count);
		free(ih, M_DEVBUF, sizeof *ih);
		return;
	}

	/* XXX oh, unroute the pci int link? */
	isa_intr_disestablish(NULL, cookie);
}

struct extent *pciio_ex;
struct extent *pcimem_ex;
struct extent *pcibus_ex;

void
pci_init_extents(void)
{
	bios_memmap_t *bmp;
	u_int64_t size;

	if (pciio_ex == NULL) {
		/*
		 * We only have 64K of addressable I/O space.
		 * However, since BARs may contain garbage, we cover
		 * the full 32-bit address space defined by PCI of
		 * which we only make the first 64K available.
		 */
		pciio_ex = extent_create("pciio", 0, 0xffffffff, M_DEVBUF,
		    NULL, 0, EX_NOWAIT | EX_FILLED);
		if (pciio_ex == NULL)
			return;
		extent_free(pciio_ex, 0, 0x10000, EX_NOWAIT);
	}

	if (pcimem_ex == NULL) {
		pcimem_ex = extent_create("pcimem", 0, 0xffffffff, M_DEVBUF,
		    NULL, 0, EX_NOWAIT);
		if (pcimem_ex == NULL)
			return;

		for (bmp = bios_memmap; bmp->type != BIOS_MAP_END; bmp++) {
			/*
			 * Ignore address space beyond 4G.
			 */
			if (bmp->addr >= 0x100000000ULL)
				continue;
			size = bmp->size;
			if (bmp->addr + size >= 0x100000000ULL)
				size = 0x100000000ULL - bmp->addr;

			/* Ignore zero-sized regions. */
			if (size == 0)
				continue;

			if (extent_alloc_region(pcimem_ex, bmp->addr, size,
			    EX_NOWAIT))
				printf("memory map conflict 0x%llx/0x%llx\n",
				    bmp->addr, bmp->size);
		}

		/* Take out the video buffer area and BIOS areas. */
		extent_alloc_region(pcimem_ex, IOM_BEGIN, IOM_SIZE,
		    EX_CONFLICTOK | EX_NOWAIT);
	}

	if (pcibus_ex == NULL) {
		pcibus_ex = extent_create("pcibus", 0, 0xff, M_DEVBUF,
		    NULL, 0, EX_NOWAIT);
	}
}

#include "acpi.h"
#if NACPI > 0
void acpi_pci_match(struct device *, struct pci_attach_args *);
pcireg_t acpi_pci_min_powerstate(pci_chipset_tag_t, pcitag_t);
void acpi_pci_set_powerstate(pci_chipset_tag_t, pcitag_t, int, int);
#endif

void
pci_dev_postattach(struct device *dev, struct pci_attach_args *pa)
{
#if NACPI > 0
	acpi_pci_match(dev, pa);
#endif
}

pcireg_t
pci_min_powerstate(pci_chipset_tag_t pc, pcitag_t tag)
{
#if NACPI > 0
	return acpi_pci_min_powerstate(pc, tag);
#else
	return pci_get_powerstate(pc, tag);
#endif
}

void
pci_set_powerstate_md(pci_chipset_tag_t pc, pcitag_t tag, int state, int pre)
{
#if NACPI > 0
	acpi_pci_set_powerstate(pc, tag, state, pre);
#endif
}
