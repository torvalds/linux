/*
 * include/asm-arm/plat-orion/pcie.h
 *
 * Marvell Orion SoC PCIe handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_PLAT_ORION_PCIE_H
#define __ASM_PLAT_ORION_PCIE_H

u32 orion_pcie_dev_id(void __iomem *base);
u32 orion_pcie_rev(void __iomem *base);
int orion_pcie_link_up(void __iomem *base);
int orion_pcie_get_local_bus_nr(void __iomem *base);
void orion_pcie_set_local_bus_nr(void __iomem *base, int nr);
void orion_pcie_setup(void __iomem *base,
		      struct mbus_dram_target_info *dram);
int orion_pcie_rd_conf(void __iomem *base, struct pci_bus *bus,
		       u32 devfn, int where, int size, u32 *val);
int orion_pcie_rd_conf_tlp(void __iomem *base, struct pci_bus *bus,
			   u32 devfn, int where, int size, u32 *val);
int orion_pcie_rd_conf_wa(void __iomem *wa_base, struct pci_bus *bus,
			  u32 devfn, int where, int size, u32 *val);
int orion_pcie_wr_conf(void __iomem *base, struct pci_bus *bus,
		       u32 devfn, int where, int size, u32 val);


#endif
