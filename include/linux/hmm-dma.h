/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */
#ifndef LINUX_HMM_DMA_H
#define LINUX_HMM_DMA_H

#include <linux/dma-mapping.h>

struct dma_iova_state;
struct pci_p2pdma_map_state;

/*
 * struct hmm_dma_map - array of PFNs and DMA addresses
 *
 * @state: DMA IOVA state
 * @pfns: array of PFNs
 * @dma_list: array of DMA addresses
 * @dma_entry_size: size of each DMA entry in the array
 */
struct hmm_dma_map {
	struct dma_iova_state state;
	unsigned long *pfn_list;
	dma_addr_t *dma_list;
	size_t dma_entry_size;
};

int hmm_dma_map_alloc(struct device *dev, struct hmm_dma_map *map,
		      size_t nr_entries, size_t dma_entry_size);
void hmm_dma_map_free(struct device *dev, struct hmm_dma_map *map);
dma_addr_t hmm_dma_map_pfn(struct device *dev, struct hmm_dma_map *map,
			   size_t idx,
			   struct pci_p2pdma_map_state *p2pdma_state);
bool hmm_dma_unmap_pfn(struct device *dev, struct hmm_dma_map *map, size_t idx);
#endif /* LINUX_HMM_DMA_H */
