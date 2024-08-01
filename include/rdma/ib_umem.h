/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2007 Cisco Systems.  All rights reserved.
 * Copyright (c) 2020 Intel Corporation.  All rights reserved.
 */

#ifndef IB_UMEM_H
#define IB_UMEM_H

#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <rdma/ib_verbs.h>

struct ib_ucontext;
struct ib_umem_odp;
struct dma_buf_attach_ops;

struct ib_umem {
	struct ib_device       *ibdev;
	struct mm_struct       *owning_mm;
	u64 iova;
	size_t			length;
	unsigned long		address;
	u32 writable : 1;
	u32 is_odp : 1;
	u32 is_dmabuf : 1;
	struct sg_append_table sgt_append;
};

struct ib_umem_dmabuf {
	struct ib_umem umem;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct scatterlist *first_sg;
	struct scatterlist *last_sg;
	unsigned long first_sg_offset;
	unsigned long last_sg_trim;
	void *private;
	u8 pinned : 1;
	u8 revoked : 1;
};

static inline struct ib_umem_dmabuf *to_ib_umem_dmabuf(struct ib_umem *umem)
{
	return container_of(umem, struct ib_umem_dmabuf, umem);
}

/* Returns the offset of the umem start relative to the first page. */
static inline int ib_umem_offset(struct ib_umem *umem)
{
	return umem->address & ~PAGE_MASK;
}

static inline unsigned long ib_umem_dma_offset(struct ib_umem *umem,
					       unsigned long pgsz)
{
	return (sg_dma_address(umem->sgt_append.sgt.sgl) + ib_umem_offset(umem)) &
	       (pgsz - 1);
}

static inline size_t ib_umem_num_dma_blocks(struct ib_umem *umem,
					    unsigned long pgsz)
{
	return (size_t)((ALIGN(umem->iova + umem->length, pgsz) -
			 ALIGN_DOWN(umem->iova, pgsz))) /
	       pgsz;
}

static inline size_t ib_umem_num_pages(struct ib_umem *umem)
{
	return ib_umem_num_dma_blocks(umem, PAGE_SIZE);
}

static inline void __rdma_umem_block_iter_start(struct ib_block_iter *biter,
						struct ib_umem *umem,
						unsigned long pgsz)
{
	__rdma_block_iter_start(biter, umem->sgt_append.sgt.sgl,
				umem->sgt_append.sgt.nents, pgsz);
	biter->__sg_advance = ib_umem_offset(umem) & ~(pgsz - 1);
	biter->__sg_numblocks = ib_umem_num_dma_blocks(umem, pgsz);
}

static inline bool __rdma_umem_block_iter_next(struct ib_block_iter *biter)
{
	return __rdma_block_iter_next(biter) && biter->__sg_numblocks--;
}

/**
 * rdma_umem_for_each_dma_block - iterate over contiguous DMA blocks of the umem
 * @umem: umem to iterate over
 * @pgsz: Page size to split the list into
 *
 * pgsz must be <= PAGE_SIZE or computed by ib_umem_find_best_pgsz(). The
 * returned DMA blocks will be aligned to pgsz and span the range:
 * ALIGN_DOWN(umem->address, pgsz) to ALIGN(umem->address + umem->length, pgsz)
 *
 * Performs exactly ib_umem_num_dma_blocks() iterations.
 */
#define rdma_umem_for_each_dma_block(umem, biter, pgsz)                        \
	for (__rdma_umem_block_iter_start(biter, umem, pgsz);                  \
	     __rdma_umem_block_iter_next(biter);)

#ifdef CONFIG_INFINIBAND_USER_MEM

struct ib_umem *ib_umem_get(struct ib_device *device, unsigned long addr,
			    size_t size, int access);
void ib_umem_release(struct ib_umem *umem);
int ib_umem_copy_from(void *dst, struct ib_umem *umem, size_t offset,
		      size_t length);
unsigned long ib_umem_find_best_pgsz(struct ib_umem *umem,
				     unsigned long pgsz_bitmap,
				     unsigned long virt);

/**
 * ib_umem_find_best_pgoff - Find best HW page size
 *
 * @umem: umem struct
 * @pgsz_bitmap bitmap of HW supported page sizes
 * @pgoff_bitmask: Mask of bits that can be represented with an offset
 *
 * This is very similar to ib_umem_find_best_pgsz() except instead of accepting
 * an IOVA it accepts a bitmask specifying what address bits can be represented
 * with a page offset.
 *
 * For instance if the HW has multiple page sizes, requires 64 byte alignemnt,
 * and can support aligned offsets up to 4032 then pgoff_bitmask would be
 * "111111000000".
 *
 * If the pgoff_bitmask requires either alignment in the low bit or an
 * unavailable page size for the high bits, this function returns 0.
 */
static inline unsigned long ib_umem_find_best_pgoff(struct ib_umem *umem,
						    unsigned long pgsz_bitmap,
						    u64 pgoff_bitmask)
{
	struct scatterlist *sg = umem->sgt_append.sgt.sgl;
	dma_addr_t dma_addr;

	dma_addr = sg_dma_address(sg) + (umem->address & ~PAGE_MASK);
	return ib_umem_find_best_pgsz(umem, pgsz_bitmap,
				      dma_addr & pgoff_bitmask);
}

struct ib_umem_dmabuf *ib_umem_dmabuf_get(struct ib_device *device,
					  unsigned long offset, size_t size,
					  int fd, int access,
					  const struct dma_buf_attach_ops *ops);
struct ib_umem_dmabuf *ib_umem_dmabuf_get_pinned(struct ib_device *device,
						 unsigned long offset,
						 size_t size, int fd,
						 int access);
struct ib_umem_dmabuf *
ib_umem_dmabuf_get_pinned_with_dma_device(struct ib_device *device,
					  struct device *dma_device,
					  unsigned long offset, size_t size,
					  int fd, int access);
int ib_umem_dmabuf_map_pages(struct ib_umem_dmabuf *umem_dmabuf);
void ib_umem_dmabuf_unmap_pages(struct ib_umem_dmabuf *umem_dmabuf);
void ib_umem_dmabuf_release(struct ib_umem_dmabuf *umem_dmabuf);
void ib_umem_dmabuf_revoke(struct ib_umem_dmabuf *umem_dmabuf);

#else /* CONFIG_INFINIBAND_USER_MEM */

#include <linux/err.h>

static inline struct ib_umem *ib_umem_get(struct ib_device *device,
					  unsigned long addr, size_t size,
					  int access)
{
	return ERR_PTR(-EOPNOTSUPP);
}
static inline void ib_umem_release(struct ib_umem *umem) { }
static inline int ib_umem_copy_from(void *dst, struct ib_umem *umem, size_t offset,
		      		    size_t length) {
	return -EOPNOTSUPP;
}
static inline unsigned long ib_umem_find_best_pgsz(struct ib_umem *umem,
						   unsigned long pgsz_bitmap,
						   unsigned long virt)
{
	return 0;
}
static inline unsigned long ib_umem_find_best_pgoff(struct ib_umem *umem,
						    unsigned long pgsz_bitmap,
						    u64 pgoff_bitmask)
{
	return 0;
}
static inline
struct ib_umem_dmabuf *ib_umem_dmabuf_get(struct ib_device *device,
					  unsigned long offset,
					  size_t size, int fd,
					  int access,
					  struct dma_buf_attach_ops *ops)
{
	return ERR_PTR(-EOPNOTSUPP);
}
static inline struct ib_umem_dmabuf *
ib_umem_dmabuf_get_pinned(struct ib_device *device, unsigned long offset,
			  size_t size, int fd, int access)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct ib_umem_dmabuf *
ib_umem_dmabuf_get_pinned_with_dma_device(struct ib_device *device,
					  struct device *dma_device,
					  unsigned long offset, size_t size,
					  int fd, int access)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int ib_umem_dmabuf_map_pages(struct ib_umem_dmabuf *umem_dmabuf)
{
	return -EOPNOTSUPP;
}
static inline void ib_umem_dmabuf_unmap_pages(struct ib_umem_dmabuf *umem_dmabuf) { }
static inline void ib_umem_dmabuf_release(struct ib_umem_dmabuf *umem_dmabuf) { }
static inline void ib_umem_dmabuf_revoke(struct ib_umem_dmabuf *umem_dmabuf) {}

#endif /* CONFIG_INFINIBAND_USER_MEM */
#endif /* IB_UMEM_H */
