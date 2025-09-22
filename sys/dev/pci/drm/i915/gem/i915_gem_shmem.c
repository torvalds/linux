/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include <linux/pagevec.h>
#include <linux/shmem_fs.h>
#include <linux/swap.h>

#include <drm/drm_cache.h>

#include "gem/i915_gem_region.h"
#include "i915_drv.h"
#include "i915_gem_object.h"
#include "i915_gem_tiling.h"
#include "i915_gemfs.h"
#include "i915_scatterlist.h"
#include "i915_trace.h"

/*
 * Move folios to appropriate lru and release the batch, decrementing the
 * ref count of those folios.
 */
static void check_release_folio_batch(struct folio_batch *fbatch)
{
	STUB();
#ifdef notyet
	check_move_unevictable_folios(fbatch);
	__folio_batch_release(fbatch);
#endif
	cond_resched();
}

void shmem_sg_free_table(struct sg_table *st, struct address_space *mapping,
			 bool dirty, bool backup,
			 struct drm_i915_gem_object *obj)
{
	struct sgt_iter sgt_iter;
	struct folio_batch fbatch;
	struct folio *last = NULL;
	struct vm_page *page;

#ifdef __linux__
	mapping_clear_unevictable(mapping);

	folio_batch_init(&fbatch);
#endif
	for_each_sgt_page(page, sgt_iter, st) {
#ifdef __linux__
		struct folio *folio = page_folio(page);

		if (folio == last)
			continue;
		last = folio;
		if (dirty)
			folio_mark_dirty(folio);
		if (backup)
			folio_mark_accessed(folio);

		if (!folio_batch_add(&fbatch, folio))
			check_release_folio_batch(&fbatch);
#else
		if (dirty)
			set_page_dirty(page);
#endif
	}
#ifdef __linux__
	if (fbatch.nr)
		check_release_folio_batch(&fbatch);
#else
	uvm_obj_unwire(obj->base.uao, 0, obj->base.size);
#endif

	sg_free_table(st);
}

int shmem_sg_alloc_table(struct drm_i915_private *i915, struct sg_table *st,
			 size_t size, struct intel_memory_region *mr,
			 struct address_space *mapping,
			 unsigned int max_segment,
			 struct drm_i915_gem_object *obj)
{
	unsigned int page_count; /* restricted by sg_alloc_table */
	unsigned long i;
	struct scatterlist *sg;
	unsigned long next_pfn = 0;	/* suppress gcc warning */
	gfp_t noreclaim;
	int ret;
	struct pglist plist;
	struct vm_page *page;

	if (overflows_type(size / PAGE_SIZE, page_count))
		return -E2BIG;

	page_count = size / PAGE_SIZE;
	/*
	 * If there's no chance of allocating enough pages for the whole
	 * object, bail early.
	 */
	if (size > resource_size(&mr->region))
		return -ENOMEM;

	if (sg_alloc_table(st, page_count, GFP_KERNEL | __GFP_NOWARN))
		return -ENOMEM;
#ifdef __linux__

	/*
	 * Get the list of pages out of our struct file.  They'll be pinned
	 * at this point until we release them.
	 *
	 * Fail silently without starting the shrinker
	 */
	mapping_set_unevictable(mapping);
	noreclaim = mapping_gfp_constraint(mapping, ~__GFP_RECLAIM);
	noreclaim |= __GFP_NORETRY | __GFP_NOWARN;

	sg = st->sgl;
	st->nents = 0;
	for (i = 0; i < page_count; i++) {
		struct folio *folio;
		unsigned long nr_pages;
		const unsigned int shrink[] = {
			I915_SHRINK_BOUND | I915_SHRINK_UNBOUND,
			0,
		}, *s = shrink;
		gfp_t gfp = noreclaim;

		do {
			cond_resched();
			folio = shmem_read_folio_gfp(mapping, i, gfp);
			if (!IS_ERR(folio))
				break;

			if (!*s) {
				ret = PTR_ERR(folio);
				goto err_sg;
			}

			i915_gem_shrink(NULL, i915, 2 * page_count, NULL, *s++);

			/*
			 * We've tried hard to allocate the memory by reaping
			 * our own buffer, now let the real VM do its job and
			 * go down in flames if truly OOM.
			 *
			 * However, since graphics tend to be disposable,
			 * defer the oom here by reporting the ENOMEM back
			 * to userspace.
			 */
			if (!*s) {
				/* reclaim and warn, but no oom */
				gfp = mapping_gfp_mask(mapping);

				/*
				 * Our bo are always dirty and so we require
				 * kswapd to reclaim our pages (direct reclaim
				 * does not effectively begin pageout of our
				 * buffers on its own). However, direct reclaim
				 * only waits for kswapd when under allocation
				 * congestion. So as a result __GFP_RECLAIM is
				 * unreliable and fails to actually reclaim our
				 * dirty pages -- unless you try over and over
				 * again with !__GFP_NORETRY. However, we still
				 * want to fail this allocation rather than
				 * trigger the out-of-memory killer and for
				 * this we want __GFP_RETRY_MAYFAIL.
				 */
				gfp |= __GFP_RETRY_MAYFAIL | __GFP_NOWARN;
			}
		} while (1);

		nr_pages = min_t(unsigned long,
				folio_nr_pages(folio), page_count - i);
		if (!i ||
		    sg->length >= max_segment ||
		    folio_pfn(folio) != next_pfn) {
			if (i)
				sg = sg_next(sg);

			st->nents++;
			sg_set_folio(sg, folio, nr_pages * PAGE_SIZE, 0);
		} else {
			/* XXX: could overflow? */
			sg->length += nr_pages * PAGE_SIZE;
		}
		next_pfn = folio_pfn(folio) + nr_pages;
		i += nr_pages - 1;

		/* Check that the i965g/gm workaround works. */
		GEM_BUG_ON(gfp & __GFP_DMA32 && next_pfn >= 0x00100000UL);
	}
#else
	sg = st->sgl;
	st->nents = 0;

	TAILQ_INIT(&plist);
	if (uvm_obj_wire(obj->base.uao, 0, obj->base.size, &plist)) {
		sg_free_table(st);
		kfree(st);
		return -ENOMEM;
	}

	i = 0;
	TAILQ_FOREACH(page, &plist, pageq) {
		if (i)
			sg = sg_next(sg);
		st->nents++;
		sg_set_page(sg, page, PAGE_SIZE, 0);
		i++;
	}
#endif
	if (sg) /* loop terminated early; short sg table */
		sg_mark_end(sg);

	/* Trim unused sg entries to avoid wasting memory. */
	i915_sg_trim(st);

	return 0;
#ifdef notyet
err_sg:
	sg_mark_end(sg);
	if (sg != st->sgl) {
		shmem_sg_free_table(st, mapping, false, false);
	} else {
		mapping_clear_unevictable(mapping);
		sg_free_table(st);
	}

	/*
	 * shmemfs first checks if there is enough memory to allocate the page
	 * and reports ENOSPC should there be insufficient, along with the usual
	 * ENOMEM for a genuine allocation failure.
	 *
	 * We use ENOSPC in our driver to mean that we have run out of aperture
	 * space and so want to translate the error from shmemfs back to our
	 * usual understanding of ENOMEM.
	 */
	if (ret == -ENOSPC)
		ret = -ENOMEM;

	return ret;
#endif
}

static int shmem_get_pages(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct intel_memory_region *mem = obj->mm.region;
#ifdef __linux__
	struct address_space *mapping = obj->base.filp->f_mapping;
#endif
	unsigned int max_segment = i915_sg_segment_size(i915->drm.dev);
	struct sg_table *st;
	int ret;

	/*
	 * Assert that the object is not currently in any GPU domain. As it
	 * wasn't in the GTT, there shouldn't be any way it could have been in
	 * a GPU cache
	 */
	GEM_BUG_ON(obj->read_domains & I915_GEM_GPU_DOMAINS);
	GEM_BUG_ON(obj->write_domain & I915_GEM_GPU_DOMAINS);

rebuild_st:
	st = kmalloc(sizeof(*st), GFP_KERNEL | __GFP_NOWARN);
	if (!st)
		return -ENOMEM;

#ifdef __linux__
	ret = shmem_sg_alloc_table(i915, st, obj->base.size, mem, mapping,
				   max_segment);
#else
	ret = shmem_sg_alloc_table(i915, st, obj->base.size, mem, NULL,
				   max_segment, obj);
#endif
	if (ret)
		goto err_st;

	ret = i915_gem_gtt_prepare_pages(obj, st);
	if (ret) {
		/*
		 * DMA remapping failed? One possible cause is that
		 * it could not reserve enough large entries, asking
		 * for PAGE_SIZE chunks instead may be helpful.
		 */
		if (max_segment > PAGE_SIZE) {
#ifdef __linux__
			shmem_sg_free_table(st, mapping, false, false);
#else
			shmem_sg_free_table(st, NULL, false, false, obj);
#endif
			sg_free_table(st);
			kfree(st);

			max_segment = PAGE_SIZE;
			goto rebuild_st;
		} else {
			dev_warn(i915->drm.dev,
				 "Failed to DMA remap %zu pages\n",
				 obj->base.size >> PAGE_SHIFT);
			goto err_pages;
		}
	}

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_do_bit_17_swizzle(obj, st);

	if (i915_gem_object_can_bypass_llc(obj))
		obj->cache_dirty = true;

	__i915_gem_object_set_pages(obj, st);

	return 0;

err_pages:
#ifdef __linux__
	shmem_sg_free_table(st, mapping, false, false);
#else
	shmem_sg_free_table(st, NULL, false, false, obj);
#endif
	/*
	 * shmemfs first checks if there is enough memory to allocate the page
	 * and reports ENOSPC should there be insufficient, along with the usual
	 * ENOMEM for a genuine allocation failure.
	 *
	 * We use ENOSPC in our driver to mean that we have run out of aperture
	 * space and so want to translate the error from shmemfs back to our
	 * usual understanding of ENOMEM.
	 */
err_st:
	if (ret == -ENOSPC)
		ret = -ENOMEM;

	kfree(st);

	return ret;
}

static int
shmem_truncate(struct drm_i915_gem_object *obj)
{
	/*
	 * Our goal here is to return as much of the memory as
	 * is possible back to the system as we are called from OOM.
	 * To do this we must instruct the shmfs to drop all of its
	 * backing pages, *now*.
	 */
#ifdef __linux__
	shmem_truncate_range(file_inode(obj->base.filp), 0, (loff_t)-1);
#else
	rw_enter(obj->base.uao->vmobjlock, RW_WRITE);
	obj->base.uao->pgops->pgo_flush(obj->base.uao, 0, obj->base.size,
	    PGO_ALLPAGES | PGO_FREE);
	rw_exit(obj->base.uao->vmobjlock);
#endif
	obj->mm.madv = __I915_MADV_PURGED;
	obj->mm.pages = ERR_PTR(-EFAULT);

	return 0;
}

void __shmem_writeback(size_t size, struct address_space *mapping)
{
	STUB();
#ifdef notyet
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_NONE,
		.nr_to_write = SWAP_CLUSTER_MAX,
		.range_start = 0,
		.range_end = LLONG_MAX,
		.for_reclaim = 1,
	};
	unsigned long i;

	/*
	 * Leave mmapings intact (GTT will have been revoked on unbinding,
	 * leaving only CPU mmapings around) and add those pages to the LRU
	 * instead of invoking writeback so they are aged and paged out
	 * as normal.
	 */

	/* Begin writeback on each dirty page */
	for (i = 0; i < size >> PAGE_SHIFT; i++) {
		struct vm_page *page;

		page = find_lock_page(mapping, i);
		if (!page)
			continue;

		if (!page_mapped(page) && clear_page_dirty_for_io(page)) {
			int ret;

			SetPageReclaim(page);
			ret = mapping->a_ops->writepage(page, &wbc);
			if (!PageWriteback(page))
				ClearPageReclaim(page);
			if (!ret)
				goto put;
		}
		unlock_page(page);
put:
		put_page(page);
	}
#endif
}

static void
shmem_writeback(struct drm_i915_gem_object *obj)
{
	STUB();
#ifdef notyet
	__shmem_writeback(obj->base.size, obj->base.filp->f_mapping);
#endif
}

static int shmem_shrink(struct drm_i915_gem_object *obj, unsigned int flags)
{
	switch (obj->mm.madv) {
	case I915_MADV_DONTNEED:
		return i915_gem_object_truncate(obj);
	case __I915_MADV_PURGED:
		return 0;
	}

	if (flags & I915_GEM_OBJECT_SHRINK_WRITEBACK)
		shmem_writeback(obj);

	return 0;
}

void
__i915_gem_object_release_shmem(struct drm_i915_gem_object *obj,
				struct sg_table *pages,
				bool needs_clflush)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	GEM_BUG_ON(obj->mm.madv == __I915_MADV_PURGED);

	if (obj->mm.madv == I915_MADV_DONTNEED)
		obj->mm.dirty = false;

	if (needs_clflush &&
	    (obj->read_domains & I915_GEM_DOMAIN_CPU) == 0 &&
	    !(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ))
		drm_clflush_sg(pages);

	__start_cpu_write(obj);
	/*
	 * On non-LLC igfx platforms, force the flush-on-acquire if this is ever
	 * swapped-in. Our async flush path is not trust worthy enough yet(and
	 * happens in the wrong order), and with some tricks it's conceivable
	 * for userspace to change the cache-level to I915_CACHE_NONE after the
	 * pages are swapped-in, and since execbuf binds the object before doing
	 * the async flush, we have a race window.
	 */
	if (!HAS_LLC(i915) && !IS_DGFX(i915))
		obj->cache_dirty = true;
}

void i915_gem_object_put_pages_shmem(struct drm_i915_gem_object *obj, struct sg_table *pages)
{
	__i915_gem_object_release_shmem(obj, pages, true);

	i915_gem_gtt_finish_pages(obj, pages);

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_save_bit_17_swizzle(obj, pages);

#ifdef __linux__
	shmem_sg_free_table(pages, file_inode(obj->base.filp)->i_mapping,
			    obj->mm.dirty, obj->mm.madv == I915_MADV_WILLNEED);
#else
	shmem_sg_free_table(pages, NULL,
			    obj->mm.dirty, obj->mm.madv == I915_MADV_WILLNEED, obj);
#endif
	kfree(pages);
	obj->mm.dirty = false;
}

static void
shmem_put_pages(struct drm_i915_gem_object *obj, struct sg_table *pages)
{
	if (likely(i915_gem_object_has_struct_page(obj)))
		i915_gem_object_put_pages_shmem(obj, pages);
	else
		i915_gem_object_put_pages_phys(obj, pages);
}

static int
shmem_pwrite(struct drm_i915_gem_object *obj,
	     const struct drm_i915_gem_pwrite *arg)
{
#ifdef __linux__
	struct address_space *mapping = obj->base.filp->f_mapping;
	const struct address_space_operations *aops = mapping->a_ops;
#endif
	char __user *user_data = u64_to_user_ptr(arg->data_ptr);
	u64 remain;
	loff_t pos;
	unsigned int pg;

	/* Caller already validated user args */
	GEM_BUG_ON(!access_ok(user_data, arg->size));

	if (!i915_gem_object_has_struct_page(obj))
		return i915_gem_object_pwrite_phys(obj, arg);

	/*
	 * Before we instantiate/pin the backing store for our use, we
	 * can prepopulate the shmemfs filp efficiently using a write into
	 * the pagecache. We avoid the penalty of instantiating all the
	 * pages, important if the user is just writing to a few and never
	 * uses the object on the GPU, and using a direct write into shmemfs
	 * allows it to avoid the cost of retrieving a page (either swapin
	 * or clearing-before-use) before it is overwritten.
	 */
	if (i915_gem_object_has_pages(obj))
		return -ENODEV;

	if (obj->mm.madv != I915_MADV_WILLNEED)
		return -EFAULT;

	/*
	 * Before the pages are instantiated the object is treated as being
	 * in the CPU domain. The pages will be clflushed as required before
	 * use, and we can freely write into the pages directly. If userspace
	 * races pwrite with any other operation; corruption will ensue -
	 * that is userspace's prerogative!
	 */

	remain = arg->size;
	pos = arg->offset;
	pg = offset_in_page(pos);

	do {
		unsigned int len, unwritten;
#ifdef __linux__
		struct folio *folio;
#else
		struct vm_page *page;
#endif
		void *data, *vaddr;
		int err;
		char __maybe_unused c;

		len = PAGE_SIZE - pg;
		if (len > remain)
			len = remain;

		/* Prefault the user page to reduce potential recursion */
		err = __get_user(c, user_data);
		if (err)
			return err;

		err = __get_user(c, user_data + len - 1);
		if (err)
			return err;

#ifdef __linux__
		err = aops->write_begin(obj->base.filp, mapping, pos, len,
					&folio, &data);
		if (err < 0)
			return err;
#else
		struct pglist plist;
		TAILQ_INIT(&plist);
		if (uvm_obj_wire(obj->base.uao, trunc_page(pos),
		    trunc_page(pos) + PAGE_SIZE, &plist)) {
			return -ENOMEM;
		}
		page = TAILQ_FIRST(&plist);
#endif

#ifdef __linux__
		vaddr = kmap_local_folio(folio, offset_in_folio(folio, pos));
		pagefault_disable();
		unwritten = __copy_from_user_inatomic(vaddr, user_data, len);
		pagefault_enable();
		kunmap_local(vaddr);
#else
		vaddr = kmap_atomic(page);
		unwritten = __copy_from_user_inatomic(vaddr + pg,
		    user_data, len);
		kunmap_atomic(vaddr);
#endif

#ifdef __linux__
		err = aops->write_end(obj->base.filp, mapping, pos, len,
				      len - unwritten, folio, data);
		if (err < 0)
			return err;
#else
		uvm_obj_unwire(obj->base.uao, trunc_page(pos),
		    trunc_page(pos) + PAGE_SIZE);
#endif

		/* We don't handle -EFAULT, leave it to the caller to check */
		if (unwritten)
			return -ENODEV;

		remain -= len;
		user_data += len;
		pos += len;
		pg = 0;
	} while (remain);

	return 0;
}

static int
shmem_pread(struct drm_i915_gem_object *obj,
	    const struct drm_i915_gem_pread *arg)
{
	if (!i915_gem_object_has_struct_page(obj))
		return i915_gem_object_pread_phys(obj, arg);

	return -ENODEV;
}

static void shmem_release(struct drm_i915_gem_object *obj)
{
	if (i915_gem_object_has_struct_page(obj))
		i915_gem_object_release_memory_region(obj);

#ifdef __linux__
	fput(obj->base.filp);
#endif
}

const struct drm_i915_gem_object_ops i915_gem_shmem_ops = {
	.name = "i915_gem_object_shmem",
	.flags = I915_GEM_OBJECT_IS_SHRINKABLE,

	.get_pages = shmem_get_pages,
	.put_pages = shmem_put_pages,
	.truncate = shmem_truncate,
	.shrink = shmem_shrink,

	.pwrite = shmem_pwrite,
	.pread = shmem_pread,

	.release = shmem_release,
};

#ifdef __linux__
static int __create_shmem(struct drm_i915_private *i915,
			  struct drm_gem_object *obj,
			  resource_size_t size)
{
	unsigned long flags = VM_NORESERVE;
	struct file *filp;

	drm_gem_private_object_init(&i915->drm, obj, size);

	/* XXX: The __shmem_file_setup() function returns -EINVAL if size is
	 * greater than MAX_LFS_FILESIZE.
	 * To handle the same error as other code that returns -E2BIG when
	 * the size is too large, we add a code that returns -E2BIG when the
	 * size is larger than the size that can be handled.
	 * If BITS_PER_LONG is 32, size > MAX_LFS_FILESIZE is always false,
	 * so we only needs to check when BITS_PER_LONG is 64.
	 * If BITS_PER_LONG is 32, E2BIG checks are processed when
	 * i915_gem_object_size_2big() is called before init_object() callback
	 * is called.
	 */
	if (BITS_PER_LONG == 64 && size > MAX_LFS_FILESIZE)
		return -E2BIG;

	if (i915->mm.gemfs)
		filp = shmem_file_setup_with_mnt(i915->mm.gemfs, "i915", size,
						 flags);
	else
		filp = shmem_file_setup("i915", size, flags);
	if (IS_ERR(filp))
		return PTR_ERR(filp);

	obj->filp = filp;
	return 0;
}
#endif

static int shmem_object_init(struct intel_memory_region *mem,
			     struct drm_i915_gem_object *obj,
			     resource_size_t offset,
			     resource_size_t size,
			     resource_size_t page_size,
			     unsigned int flags)
{
	static struct lock_class_key lock_class;
	struct drm_i915_private *i915 = mem->i915;
	struct address_space *mapping;
	unsigned int cache_level;
	gfp_t mask;
	int ret;

#ifdef __linux__
	ret = __create_shmem(i915, &obj->base, size);
#else
	ret = drm_gem_object_init(&i915->drm, &obj->base, size);
#endif
	if (ret)
		return ret;

	mask = GFP_HIGHUSER | __GFP_RECLAIMABLE;
	if (IS_I965GM(i915) || IS_I965G(i915)) {
		/* 965gm cannot relocate objects above 4GiB. */
		mask &= ~__GFP_HIGHMEM;
		mask |= __GFP_DMA32;
	}

#ifdef __linux__
	mapping = obj->base.filp->f_mapping;
	mapping_set_gfp_mask(mapping, mask);
	GEM_BUG_ON(!(mapping_gfp_mask(mapping) & __GFP_RECLAIM));
#endif

	i915_gem_object_init(obj, &i915_gem_shmem_ops, &lock_class, flags);
	obj->mem_flags |= I915_BO_FLAG_STRUCT_PAGE;
	obj->write_domain = I915_GEM_DOMAIN_CPU;
	obj->read_domains = I915_GEM_DOMAIN_CPU;

	/*
	 * MTL doesn't snoop CPU cache by default for GPU access (namely
	 * 1-way coherency). However some UMD's are currently depending on
	 * that. Make 1-way coherent the default setting for MTL. A follow
	 * up patch will extend the GEM_CREATE uAPI to allow UMD's specify
	 * caching mode at BO creation time
	 */
	if (HAS_LLC(i915) || (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 70)))
		/* On some devices, we can have the GPU use the LLC (the CPU
		 * cache) for about a 10% performance improvement
		 * compared to uncached.  Graphics requests other than
		 * display scanout are coherent with the CPU in
		 * accessing this cache.  This means in this mode we
		 * don't need to clflush on the CPU side, and on the
		 * GPU side we only need to flush internal caches to
		 * get data visible to the CPU.
		 *
		 * However, we maintain the display planes as UC, and so
		 * need to rebind when first used as such.
		 */
		cache_level = I915_CACHE_LLC;
	else
		cache_level = I915_CACHE_NONE;

	i915_gem_object_set_cache_coherency(obj, cache_level);

	i915_gem_object_init_memory_region(obj, mem);

	return 0;
}

struct drm_i915_gem_object *
i915_gem_object_create_shmem(struct drm_i915_private *i915,
			     resource_size_t size)
{
	return i915_gem_object_create_region(i915->mm.regions[INTEL_REGION_SMEM],
					     size, 0, 0);
}

/* Allocate a new GEM object and fill it with the supplied data */
#ifdef __linux__
struct drm_i915_gem_object *
i915_gem_object_create_shmem_from_data(struct drm_i915_private *i915,
				       const void *data, resource_size_t size)
{
	struct drm_i915_gem_object *obj;
	struct file *file;
	const struct address_space_operations *aops;
	loff_t pos;
	int err;

	GEM_WARN_ON(IS_DGFX(i915));
	obj = i915_gem_object_create_shmem(i915, round_up(size, PAGE_SIZE));
	if (IS_ERR(obj))
		return obj;

	GEM_BUG_ON(obj->write_domain != I915_GEM_DOMAIN_CPU);

	file = obj->base.filp;
	aops = file->f_mapping->a_ops;
	pos = 0;
	do {
		unsigned int len = min_t(typeof(size), size, PAGE_SIZE);
		struct folio *folio;
		void *fsdata;

		err = aops->write_begin(file, file->f_mapping, pos, len,
					&folio, &fsdata);
		if (err < 0)
			goto fail;

		memcpy_to_folio(folio, offset_in_folio(folio, pos), data, len);

		err = aops->write_end(file, file->f_mapping, pos, len, len,
				      folio, fsdata);
		if (err < 0)
			goto fail;

		size -= len;
		data += len;
		pos += len;
	} while (size);

	return obj;

fail:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}
#else /* !__linux__ */
struct drm_i915_gem_object *
i915_gem_object_create_shmem_from_data(struct drm_i915_private *dev_priv,
				       const void *data, resource_size_t size)
{
	struct drm_i915_gem_object *obj;
	struct uvm_object *uao;
	resource_size_t offset;
	int err;

	GEM_WARN_ON(IS_DGFX(dev_priv));
	obj = i915_gem_object_create_shmem(dev_priv, round_up(size, PAGE_SIZE));
	if (IS_ERR(obj))
		return obj;

	GEM_BUG_ON(obj->write_domain != I915_GEM_DOMAIN_CPU);

	uao = obj->base.uao;
	offset = 0;
	do {
		unsigned int len = min_t(typeof(size), size, PAGE_SIZE);
		struct vm_page *page;
		void *pgdata, *vaddr;
		struct pglist plist;

		TAILQ_INIT(&plist);
		if (uvm_obj_wire(uao, trunc_page(offset),
		    trunc_page(offset) + PAGE_SIZE, &plist)) {
			err = -ENOMEM;
			goto fail;
		}
		page = TAILQ_FIRST(&plist);

		vaddr = kmap(page);
		memcpy(vaddr, data, len);
		kunmap_va(vaddr);

		uvm_obj_unwire(uao, trunc_page(offset),
		    trunc_page(offset) + PAGE_SIZE);

		size -= len;
		data += len;
		offset += len;
	} while (size);

	return obj;

fail:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}
#endif

static int init_shmem(struct intel_memory_region *mem)
{
	i915_gemfs_init(mem->i915);
	intel_memory_region_set_name(mem, "system");

	return 0; /* We have fallback to the kernel mnt if gemfs init failed. */
}

static int release_shmem(struct intel_memory_region *mem)
{
	i915_gemfs_fini(mem->i915);
	return 0;
}

static const struct intel_memory_region_ops shmem_region_ops = {
	.init = init_shmem,
	.release = release_shmem,
	.init_object = shmem_object_init,
};

struct intel_memory_region *i915_gem_shmem_setup(struct drm_i915_private *i915,
						 u16 type, u16 instance)
{
	return intel_memory_region_create(i915, 0,
					  totalram_pages() << PAGE_SHIFT,
					  PAGE_SIZE, 0, 0,
					  type, instance,
					  &shmem_region_ops);
}

bool i915_gem_object_is_shmem(const struct drm_i915_gem_object *obj)
{
	return obj->ops == &i915_gem_shmem_ops;
}
