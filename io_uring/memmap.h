/* SPDX-License-Identifier: GPL-2.0 */
#ifndef IO_URING_MEMMAP_H
#define IO_URING_MEMMAP_H

#define IORING_MAP_OFF_PARAM_REGION		0x20000000ULL
#define IORING_MAP_OFF_ZCRX_REGION		0x30000000ULL

#define IORING_OFF_ZCRX_SHIFT		16

struct page **io_pin_pages(unsigned long uaddr, unsigned long len, int *npages);

#ifndef CONFIG_MMU
unsigned int io_uring_nommu_mmap_capabilities(struct file *file);
#endif
unsigned long io_uring_get_unmapped_area(struct file *file, unsigned long addr,
					 unsigned long len, unsigned long pgoff,
					 unsigned long flags);
int io_uring_mmap(struct file *file, struct vm_area_struct *vma);

void io_free_region(struct user_struct *user, struct io_mapped_region *mr);
int io_create_region(struct io_ring_ctx *ctx, struct io_mapped_region *mr,
		     struct io_uring_region_desc *reg,
		     unsigned long mmap_offset);

static inline void *io_region_get_ptr(struct io_mapped_region *mr)
{
	return mr->ptr;
}

static inline bool io_region_is_set(struct io_mapped_region *mr)
{
	return !!mr->nr_pages;
}

static inline void io_region_publish(struct io_ring_ctx *ctx,
				     struct io_mapped_region *src_region,
				     struct io_mapped_region *dst_region)
{
	/*
	 * Once published mmap can find it without holding only the ->mmap_lock
	 * and not ->uring_lock.
	 */
	guard(mutex)(&ctx->mmap_lock);
	*dst_region = *src_region;
}

static inline size_t io_region_size(struct io_mapped_region *mr)
{
	return (size_t) mr->nr_pages << PAGE_SHIFT;
}

#endif
