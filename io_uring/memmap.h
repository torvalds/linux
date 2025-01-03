#ifndef IO_URING_MEMMAP_H
#define IO_URING_MEMMAP_H

struct page **io_pin_pages(unsigned long ubuf, unsigned long len, int *npages);
void io_pages_free(struct page ***pages, int npages);
int io_uring_mmap_pages(struct io_ring_ctx *ctx, struct vm_area_struct *vma,
			struct page **pages, int npages);

void *io_pages_map(struct page ***out_pages, unsigned short *npages,
		   size_t size);
void io_pages_unmap(void *ptr, struct page ***pages, unsigned short *npages,
		    bool put_pages);

void *__io_uaddr_map(struct page ***pages, unsigned short *npages,
		     unsigned long uaddr, size_t size);

#ifndef CONFIG_MMU
unsigned int io_uring_nommu_mmap_capabilities(struct file *file);
#endif
unsigned long io_uring_get_unmapped_area(struct file *file, unsigned long addr,
					 unsigned long len, unsigned long pgoff,
					 unsigned long flags);
int io_uring_mmap(struct file *file, struct vm_area_struct *vma);

void io_free_region(struct io_ring_ctx *ctx, struct io_mapped_region *mr);
int io_create_region(struct io_ring_ctx *ctx, struct io_mapped_region *mr,
		     struct io_uring_region_desc *reg);

static inline void *io_region_get_ptr(struct io_mapped_region *mr)
{
	return mr->vmap_ptr;
}

static inline bool io_region_is_set(struct io_mapped_region *mr)
{
	return !!mr->nr_pages;
}

#endif
