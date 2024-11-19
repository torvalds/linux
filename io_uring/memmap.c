// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/io_uring.h>
#include <linux/io_uring_types.h>
#include <asm/shmparam.h>

#include "memmap.h"
#include "kbuf.h"
#include "rsrc.h"

static void *io_mem_alloc_compound(struct page **pages, int nr_pages,
				   size_t size, gfp_t gfp)
{
	struct page *page;
	int i, order;

	order = get_order(size);
	if (order > MAX_PAGE_ORDER)
		return ERR_PTR(-ENOMEM);
	else if (order)
		gfp |= __GFP_COMP;

	page = alloc_pages(gfp, order);
	if (!page)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nr_pages; i++)
		pages[i] = page + i;

	return page_address(page);
}

static void *io_mem_alloc_single(struct page **pages, int nr_pages, size_t size,
				 gfp_t gfp)
{
	void *ret;
	int i;

	for (i = 0; i < nr_pages; i++) {
		pages[i] = alloc_page(gfp);
		if (!pages[i])
			goto err;
	}

	ret = vmap(pages, nr_pages, VM_MAP, PAGE_KERNEL);
	if (ret)
		return ret;
err:
	while (i--)
		put_page(pages[i]);
	return ERR_PTR(-ENOMEM);
}

void *io_pages_map(struct page ***out_pages, unsigned short *npages,
		   size_t size)
{
	gfp_t gfp = GFP_KERNEL_ACCOUNT | __GFP_ZERO | __GFP_NOWARN;
	struct page **pages;
	int nr_pages;
	void *ret;

	nr_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	pages = kvmalloc_array(nr_pages, sizeof(struct page *), gfp);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	ret = io_mem_alloc_compound(pages, nr_pages, size, gfp);
	if (!IS_ERR(ret))
		goto done;

	ret = io_mem_alloc_single(pages, nr_pages, size, gfp);
	if (!IS_ERR(ret)) {
done:
		*out_pages = pages;
		*npages = nr_pages;
		return ret;
	}

	kvfree(pages);
	*out_pages = NULL;
	*npages = 0;
	return ret;
}

void io_pages_unmap(void *ptr, struct page ***pages, unsigned short *npages,
		    bool put_pages)
{
	bool do_vunmap = false;

	if (!ptr)
		return;

	if (put_pages && *npages) {
		struct page **to_free = *pages;
		int i;

		/*
		 * Only did vmap for the non-compound multiple page case.
		 * For the compound page, we just need to put the head.
		 */
		if (PageCompound(to_free[0]))
			*npages = 1;
		else if (*npages > 1)
			do_vunmap = true;
		for (i = 0; i < *npages; i++)
			put_page(to_free[i]);
	}
	if (do_vunmap)
		vunmap(ptr);
	kvfree(*pages);
	*pages = NULL;
	*npages = 0;
}

void io_pages_free(struct page ***pages, int npages)
{
	struct page **page_array = *pages;

	if (!page_array)
		return;

	unpin_user_pages(page_array, npages);
	kvfree(page_array);
	*pages = NULL;
}

struct page **io_pin_pages(unsigned long uaddr, unsigned long len, int *npages)
{
	unsigned long start, end, nr_pages;
	struct page **pages;
	int ret;

	end = (uaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	start = uaddr >> PAGE_SHIFT;
	nr_pages = end - start;
	if (WARN_ON_ONCE(!nr_pages))
		return ERR_PTR(-EINVAL);
	if (WARN_ON_ONCE(nr_pages > INT_MAX))
		return ERR_PTR(-EOVERFLOW);

	pages = kvmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	ret = pin_user_pages_fast(uaddr, nr_pages, FOLL_WRITE | FOLL_LONGTERM,
					pages);
	/* success, mapped all pages */
	if (ret == nr_pages) {
		*npages = nr_pages;
		return pages;
	}

	/* partial map, or didn't map anything */
	if (ret >= 0) {
		/* if we did partial map, release any pages we did get */
		if (ret)
			unpin_user_pages(pages, ret);
		ret = -EFAULT;
	}
	kvfree(pages);
	return ERR_PTR(ret);
}

void *__io_uaddr_map(struct page ***pages, unsigned short *npages,
		     unsigned long uaddr, size_t size)
{
	struct page **page_array;
	unsigned int nr_pages;
	void *page_addr;

	*npages = 0;

	if (uaddr & (PAGE_SIZE - 1) || !size)
		return ERR_PTR(-EINVAL);

	nr_pages = 0;
	page_array = io_pin_pages(uaddr, size, &nr_pages);
	if (IS_ERR(page_array))
		return page_array;

	page_addr = vmap(page_array, nr_pages, VM_MAP, PAGE_KERNEL);
	if (page_addr) {
		*pages = page_array;
		*npages = nr_pages;
		return page_addr;
	}

	io_pages_free(&page_array, nr_pages);
	return ERR_PTR(-ENOMEM);
}

void io_free_region(struct io_ring_ctx *ctx, struct io_mapped_region *mr)
{
	if (mr->pages) {
		unpin_user_pages(mr->pages, mr->nr_pages);
		kvfree(mr->pages);
	}
	if (mr->vmap_ptr)
		vunmap(mr->vmap_ptr);
	if (mr->nr_pages && ctx->user)
		__io_unaccount_mem(ctx->user, mr->nr_pages);

	memset(mr, 0, sizeof(*mr));
}

int io_create_region(struct io_ring_ctx *ctx, struct io_mapped_region *mr,
		     struct io_uring_region_desc *reg)
{
	int pages_accounted = 0;
	struct page **pages;
	int nr_pages, ret;
	void *vptr;
	u64 end;

	if (WARN_ON_ONCE(mr->pages || mr->vmap_ptr || mr->nr_pages))
		return -EFAULT;
	if (memchr_inv(&reg->__resv, 0, sizeof(reg->__resv)))
		return -EINVAL;
	if (reg->flags != IORING_MEM_REGION_TYPE_USER)
		return -EINVAL;
	if (!reg->user_addr)
		return -EFAULT;
	if (!reg->size || reg->mmap_offset || reg->id)
		return -EINVAL;
	if ((reg->size >> PAGE_SHIFT) > INT_MAX)
		return E2BIG;
	if ((reg->user_addr | reg->size) & ~PAGE_MASK)
		return -EINVAL;
	if (check_add_overflow(reg->user_addr, reg->size, &end))
		return -EOVERFLOW;

	pages = io_pin_pages(reg->user_addr, reg->size, &nr_pages);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	if (ctx->user) {
		ret = __io_account_mem(ctx->user, nr_pages);
		if (ret)
			goto out_free;
		pages_accounted = nr_pages;
	}

	vptr = vmap(pages, nr_pages, VM_MAP, PAGE_KERNEL);
	if (!vptr) {
		ret = -ENOMEM;
		goto out_free;
	}

	mr->pages = pages;
	mr->vmap_ptr = vptr;
	mr->nr_pages = nr_pages;
	return 0;
out_free:
	if (pages_accounted)
		__io_unaccount_mem(ctx->user, pages_accounted);
	io_pages_free(&pages, nr_pages);
	return ret;
}

static void *io_uring_validate_mmap_request(struct file *file, loff_t pgoff,
					    size_t sz)
{
	struct io_ring_ctx *ctx = file->private_data;
	loff_t offset = pgoff << PAGE_SHIFT;

	switch ((pgoff << PAGE_SHIFT) & IORING_OFF_MMAP_MASK) {
	case IORING_OFF_SQ_RING:
	case IORING_OFF_CQ_RING:
		/* Don't allow mmap if the ring was setup without it */
		if (ctx->flags & IORING_SETUP_NO_MMAP)
			return ERR_PTR(-EINVAL);
		if (!ctx->rings)
			return ERR_PTR(-EFAULT);
		return ctx->rings;
	case IORING_OFF_SQES:
		/* Don't allow mmap if the ring was setup without it */
		if (ctx->flags & IORING_SETUP_NO_MMAP)
			return ERR_PTR(-EINVAL);
		if (!ctx->sq_sqes)
			return ERR_PTR(-EFAULT);
		return ctx->sq_sqes;
	case IORING_OFF_PBUF_RING: {
		struct io_buffer_list *bl;
		unsigned int bgid;
		void *ptr;

		bgid = (offset & ~IORING_OFF_MMAP_MASK) >> IORING_OFF_PBUF_SHIFT;
		bl = io_pbuf_get_bl(ctx, bgid);
		if (IS_ERR(bl))
			return bl;
		ptr = bl->buf_ring;
		io_put_bl(ctx, bl);
		return ptr;
		}
	}

	return ERR_PTR(-EINVAL);
}

int io_uring_mmap_pages(struct io_ring_ctx *ctx, struct vm_area_struct *vma,
			struct page **pages, int npages)
{
	unsigned long nr_pages = npages;

	vm_flags_set(vma, VM_DONTEXPAND);
	return vm_insert_pages(vma, vma->vm_start, pages, &nr_pages);
}

#ifdef CONFIG_MMU

__cold int io_uring_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct io_ring_ctx *ctx = file->private_data;
	size_t sz = vma->vm_end - vma->vm_start;
	long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned int npages;
	void *ptr;

	guard(mutex)(&ctx->resize_lock);

	ptr = io_uring_validate_mmap_request(file, vma->vm_pgoff, sz);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	switch (offset & IORING_OFF_MMAP_MASK) {
	case IORING_OFF_SQ_RING:
	case IORING_OFF_CQ_RING:
		npages = min(ctx->n_ring_pages, (sz + PAGE_SIZE - 1) >> PAGE_SHIFT);
		return io_uring_mmap_pages(ctx, vma, ctx->ring_pages, npages);
	case IORING_OFF_SQES:
		return io_uring_mmap_pages(ctx, vma, ctx->sqe_pages,
						ctx->n_sqe_pages);
	case IORING_OFF_PBUF_RING:
		return io_pbuf_mmap(file, vma);
	}

	return -EINVAL;
}

unsigned long io_uring_get_unmapped_area(struct file *filp, unsigned long addr,
					 unsigned long len, unsigned long pgoff,
					 unsigned long flags)
{
	struct io_ring_ctx *ctx = filp->private_data;
	void *ptr;

	/*
	 * Do not allow to map to user-provided address to avoid breaking the
	 * aliasing rules. Userspace is not able to guess the offset address of
	 * kernel kmalloc()ed memory area.
	 */
	if (addr)
		return -EINVAL;

	guard(mutex)(&ctx->resize_lock);

	ptr = io_uring_validate_mmap_request(filp, pgoff, len);
	if (IS_ERR(ptr))
		return -ENOMEM;

	/*
	 * Some architectures have strong cache aliasing requirements.
	 * For such architectures we need a coherent mapping which aliases
	 * kernel memory *and* userspace memory. To achieve that:
	 * - use a NULL file pointer to reference physical memory, and
	 * - use the kernel virtual address of the shared io_uring context
	 *   (instead of the userspace-provided address, which has to be 0UL
	 *   anyway).
	 * - use the same pgoff which the get_unmapped_area() uses to
	 *   calculate the page colouring.
	 * For architectures without such aliasing requirements, the
	 * architecture will return any suitable mapping because addr is 0.
	 */
	filp = NULL;
	flags |= MAP_SHARED;
	pgoff = 0;	/* has been translated to ptr above */
#ifdef SHM_COLOUR
	addr = (uintptr_t) ptr;
	pgoff = addr >> PAGE_SHIFT;
#else
	addr = 0UL;
#endif
	return mm_get_unmapped_area(current->mm, filp, addr, len, pgoff, flags);
}

#else /* !CONFIG_MMU */

int io_uring_mmap(struct file *file, struct vm_area_struct *vma)
{
	return is_nommu_shared_mapping(vma->vm_flags) ? 0 : -EINVAL;
}

unsigned int io_uring_nommu_mmap_capabilities(struct file *file)
{
	return NOMMU_MAP_DIRECT | NOMMU_MAP_READ | NOMMU_MAP_WRITE;
}

unsigned long io_uring_get_unmapped_area(struct file *file, unsigned long addr,
					 unsigned long len, unsigned long pgoff,
					 unsigned long flags)
{
	struct io_ring_ctx *ctx = file->private_data;
	void *ptr;

	guard(mutex)(&ctx->resize_lock);

	ptr = io_uring_validate_mmap_request(file, pgoff, len);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	return (unsigned long) ptr;
}

#endif /* !CONFIG_MMU */
