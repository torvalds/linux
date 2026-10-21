/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef __LIBIE_PCI_H
#define __LIBIE_PCI_H

#include <linux/pci.h>

/**
 * struct libie_pci_mmio_region - structure for MMIO region info
 * @list: used to add a MMIO region to the list of MMIO regions in
 *	  libie_mmio_info
 * @addr: virtual address of MMIO region start
 * @offset: start offset of the MMIO region
 * @size: size of the MMIO region
 * @bar_idx: BAR index to which the MMIO region belongs to
 */
struct libie_pci_mmio_region {
	struct list_head	list;
	void __iomem		*addr;
	resource_size_t		offset;
	resource_size_t		size;
	u16			bar_idx;
};

/**
 * struct libie_mmio_info - contains list of MMIO regions
 * @pdev: PCI device pointer
 * @mmio_list: list of MMIO regions
 */
struct libie_mmio_info {
	struct pci_dev		*pdev;
	struct list_head	mmio_list;
};

#define libie_pci_map_mmio_region(mmio_info, offset, size, ...)	\
	__libie_pci_map_mmio_region(mmio_info, offset, size,		\
				     COUNT_ARGS(__VA_ARGS__), ##__VA_ARGS__)

#define libie_pci_get_mmio_addr(mmio_info, offset, ...)		\
	__libie_pci_get_mmio_addr(mmio_info, offset,			\
				   COUNT_ARGS(__VA_ARGS__), ##__VA_ARGS__)

bool __libie_pci_map_mmio_region(struct libie_mmio_info *mmio_info,
				 resource_size_t offset, resource_size_t size,
				 int num_args, ...);
void __iomem *__libie_pci_get_mmio_addr(struct libie_mmio_info *mmio_info,
					resource_size_t offset,
					int num_args, ...);
void libie_pci_unmap_all_mmio_regions(struct libie_mmio_info *mmio_info);
void libie_pci_unmap_fltr_regs(struct libie_mmio_info *mmio_info,
			       bool (*fltr)(struct libie_mmio_info *mmio_info,
					    struct libie_pci_mmio_region *reg));
int libie_pci_init_dev(struct pci_dev *pdev);

#endif /* __LIBIE_PCI_H */
