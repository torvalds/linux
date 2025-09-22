/*	$OpenBSD: pci_machdep.c,v 1.81 2025/01/23 11:24:34 kettenis Exp $	*/
/*	$NetBSD: pci_machdep.c,v 1.3 2003/05/07 21:33:58 fvdl Exp $	*/

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
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
 * Copyright (c) 1994 Charles M. Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles M. Hannum.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/extent.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <machine/pio.h>
#include <machine/intr.h>
#include <machine/biosvar.h>

#include <dev/isa/isareg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/ppbreg.h>

#include "ioapic.h"

#if NIOAPIC > 0
#include <machine/i82093var.h>
#include <machine/mpbiosvar.h>
#endif

#include "acpi.h"

#include "acpidmar.h"
#if NACPIDMAR > 0
#include <dev/acpi/acpidmar.h>
#endif

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
bus_space_tag_t pci_mcfgt;
bus_space_handle_t pci_mcfgh[256];

struct mutex pci_conf_lock = MUTEX_INITIALIZER(IPL_HIGH);

#define	PCI_CONF_LOCK()						\
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

/*
 * PCI doesn't have any special needs; just use the generic versions
 * of these functions.
 */
struct bus_dma_tag pci_bus_dma_tag = {
	NULL,			/* _may_bounce */
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
}

int
pci_bus_maxdevs(pci_chipset_tag_t pc, int busno)
{
	return (32);
}

pcitag_t
pci_make_tag(pci_chipset_tag_t pc, int bus, int device, int function)
{
	if (bus >= 256 || device >= 32 || function >= 8)
		panic("pci_make_tag: bad request");

	return (PCI_MODE1_ENABLE |
	    (bus << 16) | (device << 11) | (function << 8));
}

void
pci_decompose_tag(pci_chipset_tag_t pc, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 16) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 11) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 8) & 0x7;
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
			    (tag & 0x000ff00) << 4 | reg);
			return data;
		}
	}

	PCI_CONF_LOCK();
	outl(PCI_MODE1_ADDRESS_REG, tag | reg);
	data = inl(PCI_MODE1_DATA_REG);
	outl(PCI_MODE1_ADDRESS_REG, 0);
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
			    (tag & 0x000ff00) << 4 | reg, data);
			return;
		}
	}

	PCI_CONF_LOCK();
	outl(PCI_MODE1_ADDRESS_REG, tag | reg);
	outl(PCI_MODE1_DATA_REG, data);
	outl(PCI_MODE1_ADDRESS_REG, 0);
	PCI_CONF_UNLOCK();
}

int
pci_msix_table_map(pci_chipset_tag_t pc, pcitag_t tag,
    bus_space_tag_t memt, bus_space_handle_t *memh)
{
	bus_addr_t base;
	pcireg_t reg, table, type;
	int bir, offset;
	int off, tblsz;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, &off, &reg) == 0)
		panic("%s: no msix capability", __func__);

	table = pci_conf_read(pc, tag, off + PCI_MSIX_TABLE);
	bir = (table & PCI_MSIX_TABLE_BIR);
	offset = (table & PCI_MSIX_TABLE_OFF);
	tblsz = PCI_MSIX_MC_TBLSZ(reg) + 1;

	bir = PCI_MAPREG_START + bir * 4;
	type = pci_mapreg_type(pc, tag, bir);
	if (pci_mapreg_info(pc, tag, bir, type, &base, NULL, NULL) ||
	    _bus_space_map(memt, base + offset, tblsz * 16, 0, memh))
		return -1;

	return 0;
}

void
pci_msix_table_unmap(pci_chipset_tag_t pc, pcitag_t tag,
    bus_space_tag_t memt, bus_space_handle_t memh)
{
	pcireg_t reg;
	int tblsz;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, NULL, &reg) == 0)
		panic("%s: no msix capability", __func__);

	tblsz = PCI_MSIX_MC_TBLSZ(reg) + 1;
	_bus_space_unmap(memt, memh, tblsz * 16, NULL);
}

/*
 * We pack the MSI vector number into the lower 8 bits of the PCI tag
 * and use that as the MSI/MSI-X "PIC" pin number.  This allows us to
 * address 256 MSI vectors which ought to be enough for anybody.
 */
#define PCI_MSI_VEC_MASK	0xff
#define PCI_MSI_VEC(pin)	((pin) & PCI_MSI_VEC_MASK)
#define PCI_MSI_TAG(pin)	((pin) & ~PCI_MSI_VEC_MASK)
#define PCI_MSI_PIN(tag, vec)	((tag) | (vec))

void msi_hwmask(struct pic *, int);
void msi_hwunmask(struct pic *, int);
void msi_addroute(struct pic *, struct cpu_info *, int, int, int);
void msi_delroute(struct pic *, struct cpu_info *, int, int, int);
int msi_allocidtvec(struct pic *, int, int, int);

struct pic msi_pic = {
	{0, {NULL}, NULL, 0, "msi", NULL, 0, 0},
	PIC_MSI,
#ifdef MULTIPROCESSOR
	{},
#endif
	msi_hwmask,
	msi_hwunmask,
	msi_addroute,
	msi_delroute,
	msi_allocidtvec,
	NULL,
	ioapic_edge_stubs
};

void
msi_hwmask(struct pic *pic, int pin)
{
	pci_chipset_tag_t pc = NULL; /* XXX */
	pcitag_t tag = PCI_MSI_TAG(pin);
	int vec = PCI_MSI_VEC(pin);
	pcireg_t reg, mask;
	int off;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, &off, &reg) == 0)
		return;

	/* We can't mask if per-vector masking isn't implemented. */
	if ((reg & PCI_MSI_MC_PVMASK) == 0)
		return;

	if (reg & PCI_MSI_MC_C64) {
		mask = pci_conf_read(pc, tag, off + PCI_MSI_MASK64);
		pci_conf_write(pc, tag, off + PCI_MSI_MASK64,
		    mask | (1U << vec));
	} else {
		mask = pci_conf_read(pc, tag, off + PCI_MSI_MASK32);
		pci_conf_write(pc, tag, off + PCI_MSI_MASK32,
		    mask | (1U << vec));
	}
}

void
msi_hwunmask(struct pic *pic, int pin)
{
	pci_chipset_tag_t pc = NULL; /* XXX */
	pcitag_t tag = PCI_MSI_TAG(pin);
	int vec = PCI_MSI_VEC(pin);
	pcireg_t reg, mask;
	int off;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, &off, &reg) == 0)
		return;

	/* We can't mask if per-vector masking isn't implemented. */
	if ((reg & PCI_MSI_MC_PVMASK) == 0)
		return;

	if (reg & PCI_MSI_MC_C64) {
		mask = pci_conf_read(pc, tag, off + PCI_MSI_MASK64);
		pci_conf_write(pc, tag, off + PCI_MSI_MASK64,
		    mask & ~(1U << vec));
	} else {
		mask = pci_conf_read(pc, tag, off + PCI_MSI_MASK32);
		pci_conf_write(pc, tag, off + PCI_MSI_MASK32,
		    mask & ~(1U << vec));
	}
}

void
msi_addroute(struct pic *pic, struct cpu_info *ci, int pin, int idtvec,
    int type)
{
	pci_chipset_tag_t pc = NULL; /* XXX */
	pcitag_t tag = PCI_MSI_TAG(pin);
	int vec = PCI_MSI_VEC(pin);
	pcireg_t reg, addr;
	int off;

	if (pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg) == 0)
		panic("%s: no msi capability", __func__);

	if (vec != 0)
		return;

	addr = 0xfee00000UL | (ci->ci_apicid << 12);

	if (reg & PCI_MSI_MC_C64) {
		pci_conf_write(pc, tag, off + PCI_MSI_MA, addr);
		pci_conf_write(pc, tag, off + PCI_MSI_MAU32, 0);
		pci_conf_write(pc, tag, off + PCI_MSI_MD64, idtvec);
	} else {
		pci_conf_write(pc, tag, off + PCI_MSI_MA, addr);
		pci_conf_write(pc, tag, off + PCI_MSI_MD32, idtvec);
	}
	pci_conf_write(pc, tag, off, reg | PCI_MSI_MC_MSIE);
}

void
msi_delroute(struct pic *pic, struct cpu_info *ci, int pin, int idtvec,
    int type)
{
	pci_chipset_tag_t pc = NULL; /* XXX */
	pcitag_t tag = PCI_MSI_TAG(pin);
	int vec = PCI_MSI_VEC(pin);
	pcireg_t reg;
	int off;

	if (vec != 0)
		return;

	if (pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg))
		pci_conf_write(pc, tag, off, reg & ~PCI_MSI_MC_MSIE);
}

int
msi_allocidtvec(struct pic *pic, int pin, int low, int high)
{
	pci_chipset_tag_t pc = NULL; /* XXX */
	pcitag_t tag = PCI_MSI_TAG(pin);
	int vec = PCI_MSI_VEC(pin);
	int idtvec, mme, off;
	pcireg_t reg;

	if (pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg) == 0)
		panic("%s: no msi capability", __func__);

	reg = pci_conf_read(pc, tag, off);
	mme = ((reg & PCI_MSI_MC_MME_MASK) >> PCI_MSI_MC_MME_SHIFT);
	if (vec >= (1 << mme))
		return 0;

	if (vec == 0) {
		idtvec = idt_vec_alloc_range(low, high, (1 << mme));
		if (reg & PCI_MSI_MC_C64)
			pci_conf_write(pc, tag, off + PCI_MSI_MD64, idtvec);
		else
			pci_conf_write(pc, tag, off + PCI_MSI_MD32, idtvec);
	} else {
		if (reg & PCI_MSI_MC_C64)
			reg = pci_conf_read(pc, tag, off + PCI_MSI_MD64);
		else
			reg = pci_conf_read(pc, tag, off + PCI_MSI_MD32);
		KASSERT(reg > 0);
		idtvec = reg + vec;
	}

	return idtvec;
}

int
pci_intr_enable_msivec(struct pci_attach_args *pa, int num_vec)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	pcireg_t reg;
	int mmc, mme, off;

	if ((pa->pa_flags & PCI_FLAGS_MSI_ENABLED) == 0 || mp_busses == NULL ||
	    pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg) == 0)
		return 1;

	mmc = ((reg & PCI_MSI_MC_MMC_MASK) >> PCI_MSI_MC_MMC_SHIFT);
	if (num_vec > (1 << mmc))
		return 1;

	mme = ((reg & PCI_MSI_MC_MME_MASK) >> PCI_MSI_MC_MME_SHIFT);
	while ((1 << mme) < num_vec)
		mme++;
	reg &= ~PCI_MSI_MC_MME_MASK;
	reg |= (mme << PCI_MSI_MC_MME_SHIFT);
	pci_conf_write(pc, tag, off, reg);

	return 0;
}

int
pci_intr_map_msi(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	pcireg_t reg;
	int off;

	if ((pa->pa_flags & PCI_FLAGS_MSI_ENABLED) == 0 || mp_busses == NULL ||
	    pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg) == 0)
		return 1;

	/* Make sure we only enable one MSI vector. */
	reg &= ~PCI_MSI_MC_MME_MASK;
	pci_conf_write(pc, tag, off, reg);

	ihp->tag = tag;
	ihp->line = APIC_INT_VIA_MSG;
	ihp->pin = 0;
	return 0;
}

int
pci_intr_map_msivec(struct pci_attach_args *pa, int vec,
    pci_intr_handle_t *ihp)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	pcireg_t reg;
	int mme, off;

	if ((pa->pa_flags & PCI_FLAGS_MSI_ENABLED) == 0 || mp_busses == NULL ||
	    pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg) == 0)
		return 1;

	mme = ((reg & PCI_MSI_MC_MME_MASK) >> PCI_MSI_MC_MME_SHIFT);
	if (vec >= (1 << mme))
		return 1;

	ihp->tag = PCI_MSI_PIN(tag, vec);
	ihp->line = APIC_INT_VIA_MSG;
	ihp->pin = 0;
	return 0;
}

void msix_hwmask(struct pic *, int);
void msix_hwunmask(struct pic *, int);
void msix_addroute(struct pic *, struct cpu_info *, int, int, int);
void msix_delroute(struct pic *, struct cpu_info *, int, int, int);

struct pic msix_pic = {
	{0, {NULL}, NULL, 0, "msix", NULL, 0, 0},
	PIC_MSI,
#ifdef MULTIPROCESSOR
	{},
#endif
	msix_hwmask,
	msix_hwunmask,
	msix_addroute,
	msix_delroute,
	NULL,
	NULL,
	ioapic_edge_stubs
};

void
msix_hwmask(struct pic *pic, int pin)
{
	pci_chipset_tag_t pc = NULL; /* XXX */
	bus_space_tag_t memt = X86_BUS_SPACE_MEM; /* XXX */
	bus_space_handle_t memh;
	pcitag_t tag = PCI_MSI_TAG(pin);
	int entry = PCI_MSI_VEC(pin);
	pcireg_t reg;
	uint32_t ctrl;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, NULL, &reg) == 0)
		return;

	KASSERT(entry <= PCI_MSIX_MC_TBLSZ(reg));

	if (pci_msix_table_map(pc, tag, memt, &memh))
		panic("%s: cannot map registers", __func__);

	ctrl = bus_space_read_4(memt, memh, PCI_MSIX_VC(entry));
	bus_space_write_4(memt, memh, PCI_MSIX_VC(entry),
	    ctrl | PCI_MSIX_VC_MASK);

	pci_msix_table_unmap(pc, tag, memt, memh);
}

void
msix_hwunmask(struct pic *pic, int pin)
{
	pci_chipset_tag_t pc = NULL; /* XXX */
	bus_space_tag_t memt = X86_BUS_SPACE_MEM; /* XXX */
	bus_space_handle_t memh;
	pcitag_t tag = PCI_MSI_TAG(pin);
	int entry = PCI_MSI_VEC(pin);
	pcireg_t reg;
	uint32_t ctrl;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, NULL, &reg) == 0) 
		return;

	if (pci_msix_table_map(pc, tag, memt, &memh))
		panic("%s: cannot map registers", __func__);

	ctrl = bus_space_read_4(memt, memh, PCI_MSIX_VC(entry));
	bus_space_write_4(memt, memh, PCI_MSIX_VC(entry),
	    ctrl & ~PCI_MSIX_VC_MASK);

	pci_msix_table_unmap(pc, tag, memt, memh);
}

void
msix_addroute(struct pic *pic, struct cpu_info *ci, int pin, int idtvec,
    int type)
{
	pci_chipset_tag_t pc = NULL; /* XXX */
	bus_space_tag_t memt = X86_BUS_SPACE_MEM; /* XXX */
	bus_space_handle_t memh;
	pcitag_t tag = PCI_MSI_TAG(pin);
	int entry = PCI_MSI_VEC(pin);
	pcireg_t reg, addr;
	uint32_t ctrl;
	int off;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, &off, &reg) == 0)
		panic("%s: no msix capability", __func__);

	KASSERT(entry <= PCI_MSIX_MC_TBLSZ(reg));

	if (pci_msix_table_map(pc, tag, memt, &memh))
		panic("%s: cannot map registers", __func__);

	addr = 0xfee00000UL | (ci->ci_apicid << 12);

	bus_space_write_4(memt, memh, PCI_MSIX_MA(entry), addr);
	bus_space_write_4(memt, memh, PCI_MSIX_MAU32(entry), 0);
	bus_space_write_4(memt, memh, PCI_MSIX_MD(entry), idtvec);
	bus_space_barrier(memt, memh, PCI_MSIX_MA(entry), 16,
	    BUS_SPACE_BARRIER_WRITE);
	ctrl = bus_space_read_4(memt, memh, PCI_MSIX_VC(entry));
	bus_space_write_4(memt, memh, PCI_MSIX_VC(entry),
	    ctrl & ~PCI_MSIX_VC_MASK);

	pci_msix_table_unmap(pc, tag, memt, memh);

	pci_conf_write(pc, tag, off, reg | PCI_MSIX_MC_MSIXE);
}

void
msix_delroute(struct pic *pic, struct cpu_info *ci, int pin, int idtvec,
    int type)
{
	pci_chipset_tag_t pc = NULL; /* XXX */
	bus_space_tag_t memt = X86_BUS_SPACE_MEM; /* XXX */
	bus_space_handle_t memh;
	pcitag_t tag = PCI_MSI_TAG(pin);
	int entry = PCI_MSI_VEC(pin);
	pcireg_t reg;
	uint32_t ctrl;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, NULL, &reg) == 0)
		return;

	KASSERT(entry <= PCI_MSIX_MC_TBLSZ(reg));

	if (pci_msix_table_map(pc, tag, memt, &memh))
		return;

	ctrl = bus_space_read_4(memt, memh, PCI_MSIX_VC(entry));
	bus_space_write_4(memt, memh, PCI_MSIX_VC(entry),
	    ctrl | PCI_MSIX_VC_MASK);

	pci_msix_table_unmap(pc, tag, memt, memh);
}

int
pci_intr_map_msix(struct pci_attach_args *pa, int vec, pci_intr_handle_t *ihp)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	pcireg_t reg;

	KASSERT(PCI_MSI_VEC(vec) == vec);

	if ((pa->pa_flags & PCI_FLAGS_MSI_ENABLED) == 0 || mp_busses == NULL ||
	    pci_get_capability(pc, tag, PCI_CAP_MSIX, NULL, &reg) == 0)
		return 1;

	if (vec > PCI_MSIX_MC_TBLSZ(reg))
		return 1;

	ihp->tag = PCI_MSI_PIN(tag, vec);
	ihp->line = APIC_INT_VIA_MSGX;
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
	pci_decompose_tag(pa->pa_pc, pa->pa_tag, &bus, &dev, &func);

	if (mp_busses != NULL) {
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
	if (line == 0 || line == X86_PCI_INTERRUPT_LINE_NO_CONNECTION)
		goto bad;

	if (line >= NUM_LEGACY_IRQS) {
		printf("pci_intr_map: bad interrupt line %d\n", line);
		goto bad;
	}
	if (line == 2) {
		printf("pci_intr_map: changed line 2 to line 9\n");
		line = 9;
	}

#if NIOAPIC > 0
	if (mp_busses != NULL) {
		if (mip == NULL && mp_isa_bus) {
			for (mip = mp_isa_bus->mb_intrs; mip != NULL;
			    mip = mip->next) {
				if (mip->bus_pin == line) {
					ihp->line = mip->ioapic_ih | line;
					return 0;
				}
			}
		}
#if NEISA > 0
		if (mip == NULL && mp_eisa_bus) {
			for (mip = mp_eisa_bus->mb_intrs;  mip != NULL;
			    mip = mip->next) {
				if (mip->bus_pin == line) {
					ihp->line = mip->ioapic_ih | line;
					return 0;
				}
			}
		}
#endif
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

	if (ih.line == 0)
		panic("pci_intr_string: bogus handle 0x%x", ih.line);

	if (ih.line & APIC_INT_VIA_MSG)
		return ("msi");
	if (ih.line & APIC_INT_VIA_MSGX)
		return ("msix");

#if NIOAPIC > 0
	if (ih.line & APIC_INT_VIA_APIC)
		snprintf(irqstr, sizeof(irqstr), "apic %d int %d",
		    APIC_IRQ_APIC(ih.line), APIC_IRQ_PIN(ih.line));
	else
		snprintf(irqstr, sizeof(irqstr), "irq %d",
		    pci_intr_line(pc, ih));
#else
	snprintf(irqstr, sizeof(irqstr), "irq %d", pci_intr_line(pc, ih));
#endif
	return (irqstr);
}

#include "acpiprt.h"
#if NACPIPRT > 0
void	acpiprt_route_interrupt(int bus, int dev, int pin);
#endif

void *
pci_intr_establish(pci_chipset_tag_t pc, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, const char *what)
{
	return pci_intr_establish_cpu(pc, ih, level, NULL, func, arg, what);
}

void *
pci_intr_establish_cpu(pci_chipset_tag_t pc, pci_intr_handle_t ih,
    int level, struct cpu_info *ci,
    int (*func)(void *), void *arg, const char *what)
{
	int pin, irq;
	int bus, dev;
	pcitag_t tag = ih.tag;
	struct pic *pic;

	if (ih.line & APIC_INT_VIA_MSG) {
		return intr_establish(-1, &msi_pic, tag, IST_PULSE, level,
		    ci, func, arg, what);
	}
	if (ih.line & APIC_INT_VIA_MSGX) {
		return intr_establish(-1, &msix_pic, tag, IST_PULSE, level,
		    ci, func, arg, what);
	}

	pci_decompose_tag(pc, ih.tag, &bus, &dev, NULL);
#if NACPIPRT > 0
	acpiprt_route_interrupt(bus, dev, ih.pin);
#endif

	pic = &i8259_pic;
	pin = irq = ih.line;

#if NIOAPIC > 0
	if (ih.line & APIC_INT_VIA_APIC) {
		pic = (struct pic *)ioapic_find(APIC_IRQ_APIC(ih.line));
		if (pic == NULL) {
			printf("pci_intr_establish: bad ioapic %d\n",
			    APIC_IRQ_APIC(ih.line));
			return NULL;
		}
		pin = APIC_IRQ_PIN(ih.line);
		irq = APIC_IRQ_LEGACY_IRQ(ih.line);
		if (irq < 0 || irq >= NUM_LEGACY_IRQS)
			irq = -1;
	}
#endif

	return intr_establish(irq, pic, pin, IST_LEVEL, level, ci,
	    func, arg, what);
}

void
pci_intr_disestablish(pci_chipset_tag_t pc, void *cookie)
{
	intr_disestablish(cookie);
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
		/*
		 * Cover the 36-bit address space addressable by PAE
		 * here.  As long as vendors continue to support
		 * 32-bit operating systems, we should never see BARs
		 * outside that region.
		 *
		 * Dell 13G servers have important devices outside the
		 * 36-bit address space.  Until we can extract the address
		 * ranges from ACPI, expand the allowed range to suit.
		 */
		pcimem_ex = extent_create("pcimem", 0, 0xffffffffffffffffUL,
		    M_DEVBUF, NULL, 0, EX_NOWAIT);
		if (pcimem_ex == NULL)
			return;
		extent_alloc_region(pcimem_ex, 0x40000000000UL,
		    0xfffffc0000000000UL, EX_NOWAIT);

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

int
pci_probe_device_hook(pci_chipset_tag_t pc, struct pci_attach_args *pa)
{
#if NACPIDMAR > 0
	acpidmar_pci_hook(pc, pa);
#endif
	return 0;
}

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
