/*	$OpenBSD: scatterlist.h,v 1.8 2025/06/13 07:01:38 jsg Exp $	*/
/*
 * Copyright (c) 2013, 2014, 2015 Mark Kettenis
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

#ifndef _LINUX_SCATTERLIST_H
#define _LINUX_SCATTERLIST_H

#include <sys/types.h>
#include <sys/param.h>
#include <uvm/uvm_extern.h>

#include <linux/mm.h>
#include <linux/fwnode.h> /* via asm/io.h -> logic_pio.h */

struct scatterlist {
	struct vm_page *__page;
	dma_addr_t dma_address;
	unsigned int offset;
	unsigned int length;
	bool end;
};

struct sg_table {
	struct scatterlist *sgl;
	unsigned int nents;
	unsigned int orig_nents;
	bus_dmamap_t dmamap;
};

struct sg_page_iter {
	struct scatterlist *sg;
	unsigned int sg_pgoffset;
	unsigned int __nents;
};

#define sg_is_chain(sg)		false
#define sg_is_last(sg)		((sg)->end)
#define sg_chain_ptr(sg)	NULL

static inline struct scatterlist *
sg_next(struct scatterlist *sgl)
{
	return sg_is_last(sgl) ? NULL : ++sgl;
}

int sg_alloc_table(struct sg_table *, unsigned int, gfp_t);
void sg_free_table(struct sg_table *);

static inline void
sg_mark_end(struct scatterlist *sgl)
{
	sgl->end = true;
}

static inline void
__sg_page_iter_start(struct sg_page_iter *iter, struct scatterlist *sgl,
    unsigned int nents, unsigned long pgoffset)
{
	iter->sg = sgl;
	iter->sg_pgoffset = pgoffset - 1;
	iter->__nents = nents;
}

static inline bool
__sg_page_iter_next(struct sg_page_iter *iter)
{
	iter->sg_pgoffset++;
	while (iter->__nents > 0 && 
	    iter->sg_pgoffset >= (iter->sg->length / PAGE_SIZE)) {
		iter->sg_pgoffset -= (iter->sg->length / PAGE_SIZE);
		iter->sg++;
		iter->__nents--;
	}

	return (iter->__nents > 0);
}

static inline paddr_t
sg_page_iter_dma_address(struct sg_page_iter *iter)
{
	return iter->sg->dma_address + (iter->sg_pgoffset << PAGE_SHIFT);
}

static inline struct vm_page *
sg_page_iter_page(struct sg_page_iter *iter)
{
	return PHYS_TO_VM_PAGE(sg_page_iter_dma_address(iter));
}

static inline struct vm_page *
sg_page(struct scatterlist *sgl)
{
	return sgl->__page;
}

static inline void
sg_assign_page(struct scatterlist *sgl, struct vm_page *page)
{
	sgl->__page = page;
}

static inline void
sg_set_page(struct scatterlist *sgl, struct vm_page *page,
    unsigned int length, unsigned int offset)
{
	sgl->__page = page;
	sgl->dma_address = page ? VM_PAGE_TO_PHYS(page) : 0;
	sgl->offset = offset;
	sgl->length = length;
}

#define sg_dma_address(sg)	((sg)->dma_address)
#define sg_dma_len(sg)		((sg)->length)

#define for_each_sg(sgl, sg, nents, i) \
  for (i = 0, sg = (sgl); i < (nents); i++, sg = sg_next(sg))

#define for_each_sg_page(sgl, iter, nents, pgoffset) \
  __sg_page_iter_start((iter), (sgl), (nents), (pgoffset)); \
  while (__sg_page_iter_next(iter))

#define for_each_sgtable_page(st, iter, pgoffset) \
	for_each_sg_page((st)->sgl, iter, (st)->orig_nents, pgoffset)

#endif
