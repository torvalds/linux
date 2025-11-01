/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCI Peer 2 Peer DMA support.
 *
 * Copyright (c) 2016-2018, Logan Gunthorpe
 * Copyright (c) 2016-2017, Microsemi Corporation
 * Copyright (c) 2017, Christoph Hellwig
 * Copyright (c) 2018, Eideticom Inc.
 */

#ifndef _LINUX_PCI_P2PDMA_H
#define _LINUX_PCI_P2PDMA_H

#include <linux/pci.h>

struct block_device;
struct scatterlist;

#ifdef CONFIG_PCI_P2PDMA
int pci_p2pdma_add_resource(struct pci_dev *pdev, int bar, size_t size,
		u64 offset);
int pci_p2pdma_distance_many(struct pci_dev *provider, struct device **clients,
			     int num_clients, bool verbose);
struct pci_dev *pci_p2pmem_find_many(struct device **clients, int num_clients);
void *pci_alloc_p2pmem(struct pci_dev *pdev, size_t size);
void pci_free_p2pmem(struct pci_dev *pdev, void *addr, size_t size);
pci_bus_addr_t pci_p2pmem_virt_to_bus(struct pci_dev *pdev, void *addr);
struct scatterlist *pci_p2pmem_alloc_sgl(struct pci_dev *pdev,
					 unsigned int *nents, u32 length);
void pci_p2pmem_free_sgl(struct pci_dev *pdev, struct scatterlist *sgl);
void pci_p2pmem_publish(struct pci_dev *pdev, bool publish);
int pci_p2pdma_enable_store(const char *page, struct pci_dev **p2p_dev,
			    bool *use_p2pdma);
ssize_t pci_p2pdma_enable_show(char *page, struct pci_dev *p2p_dev,
			       bool use_p2pdma);
#else /* CONFIG_PCI_P2PDMA */
static inline int pci_p2pdma_add_resource(struct pci_dev *pdev, int bar,
		size_t size, u64 offset)
{
	return -EOPNOTSUPP;
}
static inline int pci_p2pdma_distance_many(struct pci_dev *provider,
	struct device **clients, int num_clients, bool verbose)
{
	return -1;
}
static inline struct pci_dev *pci_p2pmem_find_many(struct device **clients,
						   int num_clients)
{
	return NULL;
}
static inline void *pci_alloc_p2pmem(struct pci_dev *pdev, size_t size)
{
	return NULL;
}
static inline void pci_free_p2pmem(struct pci_dev *pdev, void *addr,
		size_t size)
{
}
static inline pci_bus_addr_t pci_p2pmem_virt_to_bus(struct pci_dev *pdev,
						    void *addr)
{
	return 0;
}
static inline struct scatterlist *pci_p2pmem_alloc_sgl(struct pci_dev *pdev,
		unsigned int *nents, u32 length)
{
	return NULL;
}
static inline void pci_p2pmem_free_sgl(struct pci_dev *pdev,
		struct scatterlist *sgl)
{
}
static inline void pci_p2pmem_publish(struct pci_dev *pdev, bool publish)
{
}
static inline int pci_p2pdma_enable_store(const char *page,
		struct pci_dev **p2p_dev, bool *use_p2pdma)
{
	*use_p2pdma = false;
	return 0;
}
static inline ssize_t pci_p2pdma_enable_show(char *page,
		struct pci_dev *p2p_dev, bool use_p2pdma)
{
	return sprintf(page, "none\n");
}
#endif /* CONFIG_PCI_P2PDMA */


static inline int pci_p2pdma_distance(struct pci_dev *provider,
	struct device *client, bool verbose)
{
	return pci_p2pdma_distance_many(provider, &client, 1, verbose);
}

static inline struct pci_dev *pci_p2pmem_find(struct device *client)
{
	return pci_p2pmem_find_many(&client, 1);
}

enum pci_p2pdma_map_type {
	/*
	 * PCI_P2PDMA_MAP_UNKNOWN: Used internally as an initial state before
	 * the mapping type has been calculated. Exported routines for the API
	 * will never return this value.
	 */
	PCI_P2PDMA_MAP_UNKNOWN = 0,

	/*
	 * Not a PCI P2PDMA transfer.
	 */
	PCI_P2PDMA_MAP_NONE,

	/*
	 * PCI_P2PDMA_MAP_NOT_SUPPORTED: Indicates the transaction will
	 * traverse the host bridge and the host bridge is not in the
	 * allowlist. DMA Mapping routines should return an error when
	 * this is returned.
	 */
	PCI_P2PDMA_MAP_NOT_SUPPORTED,

	/*
	 * PCI_P2PDMA_MAP_BUS_ADDR: Indicates that two devices can talk to
	 * each other directly through a PCI switch and the transaction will
	 * not traverse the host bridge. Such a mapping should program
	 * the DMA engine with PCI bus addresses.
	 */
	PCI_P2PDMA_MAP_BUS_ADDR,

	/*
	 * PCI_P2PDMA_MAP_THRU_HOST_BRIDGE: Indicates two devices can talk
	 * to each other, but the transaction traverses a host bridge on the
	 * allowlist. In this case, a normal mapping either with CPU physical
	 * addresses (in the case of dma-direct) or IOVA addresses (in the
	 * case of IOMMUs) should be used to program the DMA engine.
	 */
	PCI_P2PDMA_MAP_THRU_HOST_BRIDGE,
};

struct pci_p2pdma_map_state {
	struct dev_pagemap *pgmap;
	enum pci_p2pdma_map_type map;
	u64 bus_off;
};

/* helper for pci_p2pdma_state(), do not use directly */
void __pci_p2pdma_update_state(struct pci_p2pdma_map_state *state,
		struct device *dev, struct page *page);

/**
 * pci_p2pdma_state - check the P2P transfer state of a page
 * @state:	P2P state structure
 * @dev:	device to transfer to/from
 * @page:	page to map
 *
 * Check if @page is a PCI P2PDMA page, and if yes of what kind.  Returns the
 * map type, and updates @state with all information needed for a P2P transfer.
 */
static inline enum pci_p2pdma_map_type
pci_p2pdma_state(struct pci_p2pdma_map_state *state, struct device *dev,
		struct page *page)
{
	if (IS_ENABLED(CONFIG_PCI_P2PDMA) && is_pci_p2pdma_page(page)) {
		if (state->pgmap != page_pgmap(page))
			__pci_p2pdma_update_state(state, dev, page);
		return state->map;
	}
	return PCI_P2PDMA_MAP_NONE;
}

/**
 * pci_p2pdma_bus_addr_map - Translate a physical address to a bus address
 *			     for a PCI_P2PDMA_MAP_BUS_ADDR transfer.
 * @state:	P2P state structure
 * @paddr:	physical address to map
 *
 * Map a physically contiguous PCI_P2PDMA_MAP_BUS_ADDR transfer.
 */
static inline dma_addr_t
pci_p2pdma_bus_addr_map(struct pci_p2pdma_map_state *state, phys_addr_t paddr)
{
	WARN_ON_ONCE(state->map != PCI_P2PDMA_MAP_BUS_ADDR);
	return paddr + state->bus_off;
}

#endif /* _LINUX_PCI_P2P_H */
