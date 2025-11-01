/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DMA_BUF_HEAP_CMA_H_
#define DMA_BUF_HEAP_CMA_H_

struct cma;

#ifdef CONFIG_DMABUF_HEAPS_CMA
int dma_heap_cma_register_heap(struct cma *cma);
#else
static inline int dma_heap_cma_register_heap(struct cma *cma)
{
	return 0;
}
#endif // CONFIG_DMABUF_HEAPS_CMA

#endif // DMA_BUF_HEAP_CMA_H_
