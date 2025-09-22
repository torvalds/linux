/*	$OpenBSD: pci_machdep.c,v 1.2 2020/06/10 16:31:27 kettenis Exp $	*/

/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

void
pci_msi_enable(pci_chipset_tag_t pc, pcitag_t tag,
    bus_addr_t addr, uint32_t data)
{
	pcireg_t reg;
	int off;

	if (pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg) == 0)
		panic("%s: no msi capability", __func__);

	if (reg & PCI_MSI_MC_C64) {
		pci_conf_write(pc, tag, off + PCI_MSI_MA, addr);
		pci_conf_write(pc, tag, off + PCI_MSI_MAU32, addr >> 32);
		pci_conf_write(pc, tag, off + PCI_MSI_MD64, data);
	} else {
		pci_conf_write(pc, tag, off + PCI_MSI_MA, addr);
		pci_conf_write(pc, tag, off + PCI_MSI_MD32, data);
	}
	pci_conf_write(pc, tag, off, reg | PCI_MSI_MC_MSIE);
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
	    bus_space_map(memt, base + offset, tblsz * 16, 0, memh))
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
	bus_space_unmap(memt, memh, tblsz * 16);
}

void
pci_msix_enable(pci_chipset_tag_t pc, pcitag_t tag, bus_space_tag_t memt,
    int vec, bus_addr_t addr, uint32_t data)
{
	bus_space_handle_t memh;
	pcireg_t reg;
	uint32_t ctrl;
	int off;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, &off, &reg) == 0)
		panic("%s: no msix capability", __func__);

	KASSERT(vec <= PCI_MSIX_MC_TBLSZ(reg));

	if (pci_msix_table_map(pc, tag, memt, &memh))
		panic("%s: cannot map registers", __func__);

	bus_space_write_4(memt, memh, PCI_MSIX_MA(vec), addr);
	bus_space_write_4(memt, memh, PCI_MSIX_MAU32(vec), addr >> 32);
	bus_space_write_4(memt, memh, PCI_MSIX_MD(vec), data);
	bus_space_barrier(memt, memh, PCI_MSIX_MA(vec), 16,
	    BUS_SPACE_BARRIER_WRITE);
	ctrl = bus_space_read_4(memt, memh, PCI_MSIX_VC(vec));
	bus_space_write_4(memt, memh, PCI_MSIX_VC(vec),
	    ctrl & ~PCI_MSIX_VC_MASK);

	pci_msix_table_unmap(pc, tag, memt, memh);

	pci_conf_write(pc, tag, off, reg | PCI_MSIX_MC_MSIXE);
}

int
_pci_intr_map_msi(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	pcireg_t reg;

	if ((pa->pa_flags & PCI_FLAGS_MSI_ENABLED) == 0 ||
	    pci_get_capability(pc, tag, PCI_CAP_MSI, NULL, &reg) == 0)
		return -1;

	ihp->ih_pc = pa->pa_pc;
	ihp->ih_tag = pa->pa_tag;
	ihp->ih_type = (reg & PCI_MSI_MC_C64) ? PCI_MSI64 : PCI_MSI32;

	return 0;
}

int
_pci_intr_map_msix(struct pci_attach_args *pa, int vec,
    pci_intr_handle_t *ihp)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	pcireg_t reg, table, type;
	int bir, off;

	if ((pa->pa_flags & PCI_FLAGS_MSI_ENABLED) == 0 ||
	    pci_get_capability(pc, tag, PCI_CAP_MSIX, &off, &reg) == 0)
		return -1;

	if (vec > PCI_MSIX_MC_TBLSZ(reg))
		return -1;

	table = pci_conf_read(pc, tag, off + PCI_MSIX_TABLE);
	bir = PCI_MAPREG_START + (table & PCI_MSIX_TABLE_BIR) * 4;
	type = pci_mapreg_type(pc, tag, bir);
	if (pci_mapreg_assign(pa, bir, type, NULL, NULL))
		return -1;

	ihp->ih_pc = pa->pa_pc;
	ihp->ih_tag = pa->pa_tag;
	ihp->ih_intrpin = vec;
	ihp->ih_type = PCI_MSIX;

	return 0;
}
