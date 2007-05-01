/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006  Ralf Baechle <ralf@linux-mips.org>
 *
 */
#ifndef __ASM_MACH_IP27_DMA_COHERENCE_H
#define __ASM_MACH_IP27_DMA_COHERENCE_H

#include <asm/pci/bridge.h>

#define pdev_to_baddr(pdev, addr) \
	(BRIDGE_CONTROLLER(pdev->bus)->baddr + (addr))
#define dev_to_baddr(dev, addr) \
	pdev_to_baddr(to_pci_dev(dev), (addr))

struct device;

static inline dma_addr_t plat_map_dma_mem(struct device *dev, void *addr,
	size_t size)
{
	dma_addr_t pa = dev_to_baddr(dev, virt_to_phys(addr));

	return pa;
}

static dma_addr_t plat_map_dma_mem_page(struct device *dev, struct page *page)
{
	dma_addr_t pa = dev_to_baddr(dev, page_to_phys(page));

	return pa;
}

static unsigned long plat_dma_addr_to_phys(dma_addr_t dma_addr)
{
	return dma_addr & (0xffUL << 56);
}

static inline void plat_unmap_dma_mem(dma_addr_t dma_addr)
{
}

static inline int plat_device_is_coherent(struct device *dev)
{
	return 1;		/* IP27 non-cohernet mode is unsupported */
}

#endif /* __ASM_MACH_IP27_DMA_COHERENCE_H */
