/*
 * Copyright (C) 2007 Jens Axboe <jens.axboe@oracle.com>
 *
 * Scatterlist handling helpers.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2. See the file COPYING for more details.
 */
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <linux/kmemleak.h>

/**
 * sg_next - return the next scatterlist entry in a list
 * @sg:		The current sg entry
 *
 * Description:
 *   Usually the next entry will be @sg@ + 1, but if this sg element is part
 *   of a chained scatterlist, it could jump to the start of a new
 *   scatterlist array.
 *
 **/
struct scatterlist *sg_next(struct scatterlist *sg)
{
#ifdef CONFIG_DEBUG_SG
	BUG_ON(sg->sg_magic != SG_MAGIC);
#endif
	if (sg_is_last(sg))
		return NULL;

	sg++;
	if (unlikely(sg_is_chain(sg)))
		sg = sg_chain_ptr(sg);

	return sg;
}
EXPORT_SYMBOL(sg_next);

/**
 * sg_nents - return total count of entries in scatterlist
 * @sg:		The scatterlist
 *
 * Description:
 * Allows to know how many entries are in sg, taking into acount
 * chaining as well
 *
 **/
int sg_nents(struct scatterlist *sg)
{
	int nents;
	for (nents = 0; sg; sg = sg_next(sg))
		nents++;
	return nents;
}
EXPORT_SYMBOL(sg_nents);

/**
 * sg_nents_for_len - return total count of entries in scatterlist
 *                    needed to satisfy the supplied length
 * @sg:		The scatterlist
 * @len:	The total required length
 *
 * Description:
 * Determines the number of entries in sg that are required to meet
 * the supplied length, taking into acount chaining as well
 *
 * Returns:
 *   the number of sg entries needed, negative error on failure
 *
 **/
int sg_nents_for_len(struct scatterlist *sg, u64 len)
{
	int nents;
	u64 total;

	if (!len)
		return 0;

	for (nents = 0, total = 0; sg; sg = sg_next(sg)) {
		nents++;
		total += sg->length;
		if (total >= len)
			return nents;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(sg_nents_for_len);

/**
 * sg_last - return the last scatterlist entry in a list
 * @sgl:	First entry in the scatterlist
 * @nents:	Number of entries in the scatterlist
 *
 * Description:
 *   Should only be used casually, it (currently) scans the entire list
 *   to get the last entry.
 *
 *   Note that the @sgl@ pointer passed in need not be the first one,
 *   the important bit is that @nents@ denotes the number of entries that
 *   exist from @sgl@.
 *
 **/
struct scatterlist *sg_last(struct scatterlist *sgl, unsigned int nents)
{
#ifndef CONFIG_ARCH_HAS_SG_CHAIN
	struct scatterlist *ret = &sgl[nents - 1];
#else
	struct scatterlist *sg, *ret = NULL;
	unsigned int i;

	for_each_sg(sgl, sg, nents, i)
		ret = sg;

#endif
#ifdef CONFIG_DEBUG_SG
	BUG_ON(sgl[0].sg_magic != SG_MAGIC);
	BUG_ON(!sg_is_last(ret));
#endif
	return ret;
}
EXPORT_SYMBOL(sg_last);

/**
 * sg_init_table - Initialize SG table
 * @sgl:	   The SG table
 * @nents:	   Number of entries in table
 *
 * Notes:
 *   If this is part of a chained sg table, sg_mark_end() should be
 *   used only on the last table part.
 *
 **/
void sg_init_table(struct scatterlist *sgl, unsigned int nents)
{
	memset(sgl, 0, sizeof(*sgl) * nents);
#ifdef CONFIG_DEBUG_SG
	{
		unsigned int i;
		for (i = 0; i < nents; i++)
			sgl[i].sg_magic = SG_MAGIC;
	}
#endif
	sg_mark_end(&sgl[nents - 1]);
}
EXPORT_SYMBOL(sg_init_table);

/**
 * sg_init_one - Initialize a single entry sg list
 * @sg:		 SG entry
 * @buf:	 Virtual address for IO
 * @buflen:	 IO length
 *
 **/
void sg_init_one(struct scatterlist *sg, const void *buf, unsigned int buflen)
{
	sg_init_table(sg, 1);
	sg_set_buf(sg, buf, buflen);
}
EXPORT_SYMBOL(sg_init_one);

/*
 * The default behaviour of sg_alloc_table() is to use these kmalloc/kfree
 * helpers.
 */
static struct scatterlist *sg_kmalloc(unsigned int nents, gfp_t gfp_mask)
{
	if (nents == SG_MAX_SINGLE_ALLOC) {
		/*
		 * Kmemleak doesn't track page allocations as they are not
		 * commonly used (in a raw form) for kernel data structures.
		 * As we chain together a list of pages and then a normal
		 * kmalloc (tracked by kmemleak), in order to for that last
		 * allocation not to become decoupled (and thus a
		 * false-positive) we need to inform kmemleak of all the
		 * intermediate allocations.
		 */
		void *ptr = (void *) __get_free_page(gfp_mask);
		kmemleak_alloc(ptr, PAGE_SIZE, 1, gfp_mask);
		return ptr;
	} else
		return kmalloc(nents * sizeof(struct scatterlist), gfp_mask);
}

static void sg_kfree(struct scatterlist *sg, unsigned int nents)
{
	if (nents == SG_MAX_SINGLE_ALLOC) {
		kmemleak_free(sg);
		free_page((unsigned long) sg);
	} else
		kfree(sg);
}

/**
 * __sg_free_table - Free a previously mapped sg table
 * @table:	The sg table header to use
 * @max_ents:	The maximum number of entries per single scatterlist
 * @skip_first_chunk: don't free the (preallocated) first scatterlist chunk
 * @free_fn:	Free function
 *
 *  Description:
 *    Free an sg table previously allocated and setup with
 *    __sg_alloc_table().  The @max_ents value must be identical to
 *    that previously used with __sg_alloc_table().
 *
 **/
void __sg_free_table(struct sg_table *table, unsigned int max_ents,
		     bool skip_first_chunk, sg_free_fn *free_fn)
{
	struct scatterlist *sgl, *next;

	if (unlikely(!table->sgl))
		return;

	sgl = table->sgl;
	while (table->orig_nents) {
		unsigned int alloc_size = table->orig_nents;
		unsigned int sg_size;

		/*
		 * If we have more than max_ents segments left,
		 * then assign 'next' to the sg table after the current one.
		 * sg_size is then one less than alloc size, since the last
		 * element is the chain pointer.
		 */
		if (alloc_size > max_ents) {
			next = sg_chain_ptr(&sgl[max_ents - 1]);
			alloc_size = max_ents;
			sg_size = alloc_size - 1;
		} else {
			sg_size = alloc_size;
			next = NULL;
		}

		table->orig_nents -= sg_size;
		if (skip_first_chunk)
			skip_first_chunk = false;
		else
			free_fn(sgl, alloc_size);
		sgl = next;
	}

	table->sgl = NULL;
}
EXPORT_SYMBOL(__sg_free_table);

/**
 * sg_free_table - Free a previously allocated sg table
 * @table:	The mapped sg table header
 *
 **/
void sg_free_table(struct sg_table *table)
{
	__sg_free_table(table, SG_MAX_SINGLE_ALLOC, false, sg_kfree);
}
EXPORT_SYMBOL(sg_free_table);

/**
 * __sg_alloc_table - Allocate and initialize an sg table with given allocator
 * @table:	The sg table header to use
 * @nents:	Number of entries in sg list
 * @max_ents:	The maximum number of entries the allocator returns per call
 * @gfp_mask:	GFP allocation mask
 * @alloc_fn:	Allocator to use
 *
 * Description:
 *   This function returns a @table @nents long. The allocator is
 *   defined to return scatterlist chunks of maximum size @max_ents.
 *   Thus if @nents is bigger than @max_ents, the scatterlists will be
 *   chained in units of @max_ents.
 *
 * Notes:
 *   If this function returns non-0 (eg failure), the caller must call
 *   __sg_free_table() to cleanup any leftover allocations.
 *
 **/
int __sg_alloc_table(struct sg_table *table, unsigned int nents,
		     unsigned int max_ents, struct scatterlist *first_chunk,
		     gfp_t gfp_mask, sg_alloc_fn *alloc_fn)
{
	struct scatterlist *sg, *prv;
	unsigned int left;

	memset(table, 0, sizeof(*table));

	if (nents == 0)
		return -EINVAL;
#ifndef CONFIG_ARCH_HAS_SG_CHAIN
	if (WARN_ON_ONCE(nents > max_ents))
		return -EINVAL;
#endif

	left = nents;
	prv = NULL;
	do {
		unsigned int sg_size, alloc_size = left;

		if (alloc_size > max_ents) {
			alloc_size = max_ents;
			sg_size = alloc_size - 1;
		} else
			sg_size = alloc_size;

		left -= sg_size;

		if (first_chunk) {
			sg = first_chunk;
			first_chunk = NULL;
		} else {
			sg = alloc_fn(alloc_size, gfp_mask);
		}
		if (unlikely(!sg)) {
			/*
			 * Adjust entry count to reflect that the last
			 * entry of the previous table won't be used for
			 * linkage.  Without this, sg_kfree() may get
			 * confused.
			 */
			if (prv)
				table->nents = ++table->orig_nents;

 			return -ENOMEM;
		}

		sg_init_table(sg, alloc_size);
		table->nents = table->orig_nents += sg_size;

		/*
		 * If this is the first mapping, assign the sg table header.
		 * If this is not the first mapping, chain previous part.
		 */
		if (prv)
			sg_chain(prv, max_ents, sg);
		else
			table->sgl = sg;

		/*
		 * If no more entries after this one, mark the end
		 */
		if (!left)
			sg_mark_end(&sg[sg_size - 1]);

		prv = sg;
	} while (left);

	return 0;
}
EXPORT_SYMBOL(__sg_alloc_table);

/**
 * sg_alloc_table - Allocate and initialize an sg table
 * @table:	The sg table header to use
 * @nents:	Number of entries in sg list
 * @gfp_mask:	GFP allocation mask
 *
 *  Description:
 *    Allocate and initialize an sg table. If @nents@ is larger than
 *    SG_MAX_SINGLE_ALLOC a chained sg table will be setup.
 *
 **/
int sg_alloc_table(struct sg_table *table, unsigned int nents, gfp_t gfp_mask)
{
	int ret;

	ret = __sg_alloc_table(table, nents, SG_MAX_SINGLE_ALLOC,
			       NULL, gfp_mask, sg_kmalloc);
	if (unlikely(ret))
		__sg_free_table(table, SG_MAX_SINGLE_ALLOC, false, sg_kfree);

	return ret;
}
EXPORT_SYMBOL(sg_alloc_table);

/**
 * sg_alloc_table_from_pages - Allocate and initialize an sg table from
 *			       an array of pages
 * @sgt:	The sg table header to use
 * @pages:	Pointer to an array of page pointers
 * @n_pages:	Number of pages in the pages array
 * @offset:     Offset from start of the first page to the start of a buffer
 * @size:       Number of valid bytes in the buffer (after offset)
 * @gfp_mask:	GFP allocation mask
 *
 *  Description:
 *    Allocate and initialize an sg table from a list of pages. Contiguous
 *    ranges of the pages are squashed into a single scatterlist node. A user
 *    may provide an offset at a start and a size of valid data in a buffer
 *    specified by the page array. The returned sg table is released by
 *    sg_free_table.
 *
 * Returns:
 *   0 on success, negative error on failure
 */
int sg_alloc_table_from_pages(struct sg_table *sgt,
	struct page **pages, unsigned int n_pages,
	unsigned long offset, unsigned long size,
	gfp_t gfp_mask)
{
	unsigned int chunks;
	unsigned int i;
	unsigned int cur_page;
	int ret;
	struct scatterlist *s;

	/* compute number of contiguous chunks */
	chunks = 1;
	for (i = 1; i < n_pages; ++i)
		if (page_to_pfn(pages[i]) != page_to_pfn(pages[i - 1]) + 1)
			++chunks;

	ret = sg_alloc_table(sgt, chunks, gfp_mask);
	if (unlikely(ret))
		return ret;

	/* merging chunks and putting them into the scatterlist */
	cur_page = 0;
	for_each_sg(sgt->sgl, s, sgt->orig_nents, i) {
		unsigned long chunk_size;
		unsigned int j;

		/* look for the end of the current chunk */
		for (j = cur_page + 1; j < n_pages; ++j)
			if (page_to_pfn(pages[j]) !=
			    page_to_pfn(pages[j - 1]) + 1)
				break;

		chunk_size = ((j - cur_page) << PAGE_SHIFT) - offset;
		sg_set_page(s, pages[cur_page], min(size, chunk_size), offset);
		size -= chunk_size;
		offset = 0;
		cur_page = j;
	}

	return 0;
}
EXPORT_SYMBOL(sg_alloc_table_from_pages);

void __sg_page_iter_start(struct sg_page_iter *piter,
			  struct scatterlist *sglist, unsigned int nents,
			  unsigned long pgoffset)
{
	piter->__pg_advance = 0;
	piter->__nents = nents;

	piter->sg = sglist;
	piter->sg_pgoffset = pgoffset;
}
EXPORT_SYMBOL(__sg_page_iter_start);

static int sg_page_count(struct scatterlist *sg)
{
	return PAGE_ALIGN(sg->offset + sg->length) >> PAGE_SHIFT;
}

bool __sg_page_iter_next(struct sg_page_iter *piter)
{
	if (!piter->__nents || !piter->sg)
		return false;

	piter->sg_pgoffset += piter->__pg_advance;
	piter->__pg_advance = 1;

	while (piter->sg_pgoffset >= sg_page_count(piter->sg)) {
		piter->sg_pgoffset -= sg_page_count(piter->sg);
		piter->sg = sg_next(piter->sg);
		if (!--piter->__nents || !piter->sg)
			return false;
	}

	return true;
}
EXPORT_SYMBOL(__sg_page_iter_next);

/**
 * sg_miter_start - start mapping iteration over a sg list
 * @miter: sg mapping iter to be started
 * @sgl: sg list to iterate over
 * @nents: number of sg entries
 *
 * Description:
 *   Starts mapping iterator @miter.
 *
 * Context:
 *   Don't care.
 */
void sg_miter_start(struct sg_mapping_iter *miter, struct scatterlist *sgl,
		    unsigned int nents, unsigned int flags)
{
	memset(miter, 0, sizeof(struct sg_mapping_iter));

	__sg_page_iter_start(&miter->piter, sgl, nents, 0);
	WARN_ON(!(flags & (SG_MITER_TO_SG | SG_MITER_FROM_SG)));
	miter->__flags = flags;
}
EXPORT_SYMBOL(sg_miter_start);

static bool sg_miter_get_next_page(struct sg_mapping_iter *miter)
{
	if (!miter->__remaining) {
		struct scatterlist *sg;
		unsigned long pgoffset;

		if (!__sg_page_iter_next(&miter->piter))
			return false;

		sg = miter->piter.sg;
		pgoffset = miter->piter.sg_pgoffset;

		miter->__offset = pgoffset ? 0 : sg->offset;
		miter->__remaining = sg->offset + sg->length -
				(pgoffset << PAGE_SHIFT) - miter->__offset;
		miter->__remaining = min_t(unsigned long, miter->__remaining,
					   PAGE_SIZE - miter->__offset);
	}

	return true;
}

/**
 * sg_miter_skip - reposition mapping iterator
 * @miter: sg mapping iter to be skipped
 * @offset: number of bytes to plus the current location
 *
 * Description:
 *   Sets the offset of @miter to its current location plus @offset bytes.
 *   If mapping iterator @miter has been proceeded by sg_miter_next(), this
 *   stops @miter.
 *
 * Context:
 *   Don't care if @miter is stopped, or not proceeded yet.
 *   Otherwise, preemption disabled if the SG_MITER_ATOMIC is set.
 *
 * Returns:
 *   true if @miter contains the valid mapping.  false if end of sg
 *   list is reached.
 */
bool sg_miter_skip(struct sg_mapping_iter *miter, off_t offset)
{
	sg_miter_stop(miter);

	while (offset) {
		off_t consumed;

		if (!sg_miter_get_next_page(miter))
			return false;

		consumed = min_t(off_t, offset, miter->__remaining);
		miter->__offset += consumed;
		miter->__remaining -= consumed;
		offset -= consumed;
	}

	return true;
}
EXPORT_SYMBOL(sg_miter_skip);

/**
 * sg_miter_next - proceed mapping iterator to the next mapping
 * @miter: sg mapping iter to proceed
 *
 * Description:
 *   Proceeds @miter to the next mapping.  @miter should have been started
 *   using sg_miter_start().  On successful return, @miter->page,
 *   @miter->addr and @miter->length point to the current mapping.
 *
 * Context:
 *   Preemption disabled if SG_MITER_ATOMIC.  Preemption must stay disabled
 *   till @miter is stopped.  May sleep if !SG_MITER_ATOMIC.
 *
 * Returns:
 *   true if @miter contains the next mapping.  false if end of sg
 *   list is reached.
 */
bool sg_miter_next(struct sg_mapping_iter *miter)
{
	sg_miter_stop(miter);

	/*
	 * Get to the next page if necessary.
	 * __remaining, __offset is adjusted by sg_miter_stop
	 */
	if (!sg_miter_get_next_page(miter))
		return false;

	miter->page = sg_page_iter_page(&miter->piter);
	miter->consumed = miter->length = miter->__remaining;

	if (miter->__flags & SG_MITER_ATOMIC)
		miter->addr = kmap_atomic(miter->page) + miter->__offset;
	else
		miter->addr = kmap(miter->page) + miter->__offset;

	return true;
}
EXPORT_SYMBOL(sg_miter_next);

/**
 * sg_miter_stop - stop mapping iteration
 * @miter: sg mapping iter to be stopped
 *
 * Description:
 *   Stops mapping iterator @miter.  @miter should have been started
 *   started using sg_miter_start().  A stopped iteration can be
 *   resumed by calling sg_miter_next() on it.  This is useful when
 *   resources (kmap) need to be released during iteration.
 *
 * Context:
 *   Preemption disabled if the SG_MITER_ATOMIC is set.  Don't care
 *   otherwise.
 */
void sg_miter_stop(struct sg_mapping_iter *miter)
{
	WARN_ON(miter->consumed > miter->length);

	/* drop resources from the last iteration */
	if (miter->addr) {
		miter->__offset += miter->consumed;
		miter->__remaining -= miter->consumed;

		if ((miter->__flags & SG_MITER_TO_SG) &&
		    !PageSlab(miter->page))
			flush_kernel_dcache_page(miter->page);

		if (miter->__flags & SG_MITER_ATOMIC) {
			WARN_ON_ONCE(preemptible());
			kunmap_atomic(miter->addr);
		} else
			kunmap(miter->page);

		miter->page = NULL;
		miter->addr = NULL;
		miter->length = 0;
		miter->consumed = 0;
	}
}
EXPORT_SYMBOL(sg_miter_stop);

/**
 * sg_copy_buffer - Copy data between a linear buffer and an SG list
 * @sgl:		 The SG list
 * @nents:		 Number of SG entries
 * @buf:		 Where to copy from
 * @buflen:		 The number of bytes to copy
 * @skip:		 Number of bytes to skip before copying
 * @to_buffer:		 transfer direction (true == from an sg list to a
 *			 buffer, false == from a buffer to an sg list
 *
 * Returns the number of copied bytes.
 *
 **/
size_t sg_copy_buffer(struct scatterlist *sgl, unsigned int nents, void *buf,
		      size_t buflen, off_t skip, bool to_buffer)
{
	unsigned int offset = 0;
	struct sg_mapping_iter miter;
	unsigned long flags;
	unsigned int sg_flags = SG_MITER_ATOMIC;

	if (to_buffer)
		sg_flags |= SG_MITER_FROM_SG;
	else
		sg_flags |= SG_MITER_TO_SG;

	sg_miter_start(&miter, sgl, nents, sg_flags);

	if (!sg_miter_skip(&miter, skip))
		return false;

	local_irq_save(flags);

	while (sg_miter_next(&miter) && offset < buflen) {
		unsigned int len;

		len = min(miter.length, buflen - offset);

		if (to_buffer)
			memcpy(buf + offset, miter.addr, len);
		else
			memcpy(miter.addr, buf + offset, len);

		offset += len;
	}

	sg_miter_stop(&miter);

	local_irq_restore(flags);
	return offset;
}
EXPORT_SYMBOL(sg_copy_buffer);

/**
 * sg_copy_from_buffer - Copy from a linear buffer to an SG list
 * @sgl:		 The SG list
 * @nents:		 Number of SG entries
 * @buf:		 Where to copy from
 * @buflen:		 The number of bytes to copy
 *
 * Returns the number of copied bytes.
 *
 **/
size_t sg_copy_from_buffer(struct scatterlist *sgl, unsigned int nents,
			   const void *buf, size_t buflen)
{
	return sg_copy_buffer(sgl, nents, (void *)buf, buflen, 0, false);
}
EXPORT_SYMBOL(sg_copy_from_buffer);

/**
 * sg_copy_to_buffer - Copy from an SG list to a linear buffer
 * @sgl:		 The SG list
 * @nents:		 Number of SG entries
 * @buf:		 Where to copy to
 * @buflen:		 The number of bytes to copy
 *
 * Returns the number of copied bytes.
 *
 **/
size_t sg_copy_to_buffer(struct scatterlist *sgl, unsigned int nents,
			 void *buf, size_t buflen)
{
	return sg_copy_buffer(sgl, nents, buf, buflen, 0, true);
}
EXPORT_SYMBOL(sg_copy_to_buffer);

/**
 * sg_pcopy_from_buffer - Copy from a linear buffer to an SG list
 * @sgl:		 The SG list
 * @nents:		 Number of SG entries
 * @buf:		 Where to copy from
 * @buflen:		 The number of bytes to copy
 * @skip:		 Number of bytes to skip before copying
 *
 * Returns the number of copied bytes.
 *
 **/
size_t sg_pcopy_from_buffer(struct scatterlist *sgl, unsigned int nents,
			    const void *buf, size_t buflen, off_t skip)
{
	return sg_copy_buffer(sgl, nents, (void *)buf, buflen, skip, false);
}
EXPORT_SYMBOL(sg_pcopy_from_buffer);

/**
 * sg_pcopy_to_buffer - Copy from an SG list to a linear buffer
 * @sgl:		 The SG list
 * @nents:		 Number of SG entries
 * @buf:		 Where to copy to
 * @buflen:		 The number of bytes to copy
 * @skip:		 Number of bytes to skip before copying
 *
 * Returns the number of copied bytes.
 *
 **/
size_t sg_pcopy_to_buffer(struct scatterlist *sgl, unsigned int nents,
			  void *buf, size_t buflen, off_t skip)
{
	return sg_copy_buffer(sgl, nents, buf, buflen, skip, true);
}
EXPORT_SYMBOL(sg_pcopy_to_buffer);
