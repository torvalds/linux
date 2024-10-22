// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007 Jens Axboe <jens.axboe@oracle.com>
 *
 * Scatterlist handling helpers.
 */
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <linux/kmemleak.h>
#include <linux/bvec.h>
#include <linux/uio.h>
#include <linux/folio_queue.h>

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
 * Allows to know how many entries are in sg, taking into account
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
 * the supplied length, taking into account chaining as well
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
	struct scatterlist *sg, *ret = NULL;
	unsigned int i;

	for_each_sg(sgl, sg, nents, i)
		ret = sg;

	BUG_ON(!sg_is_last(ret));
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
	sg_init_marker(sgl, nents);
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
		return kmalloc_array(nents, sizeof(struct scatterlist),
				     gfp_mask);
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
 * @nents_first_chunk: Number of entries int the (preallocated) first
 * 	scatterlist chunk, 0 means no such preallocated first chunk
 * @free_fn:	Free function
 * @num_ents:	Number of entries in the table
 *
 *  Description:
 *    Free an sg table previously allocated and setup with
 *    __sg_alloc_table().  The @max_ents value must be identical to
 *    that previously used with __sg_alloc_table().
 *
 **/
void __sg_free_table(struct sg_table *table, unsigned int max_ents,
		     unsigned int nents_first_chunk, sg_free_fn *free_fn,
		     unsigned int num_ents)
{
	struct scatterlist *sgl, *next;
	unsigned curr_max_ents = nents_first_chunk ?: max_ents;

	if (unlikely(!table->sgl))
		return;

	sgl = table->sgl;
	while (num_ents) {
		unsigned int alloc_size = num_ents;
		unsigned int sg_size;

		/*
		 * If we have more than max_ents segments left,
		 * then assign 'next' to the sg table after the current one.
		 * sg_size is then one less than alloc size, since the last
		 * element is the chain pointer.
		 */
		if (alloc_size > curr_max_ents) {
			next = sg_chain_ptr(&sgl[curr_max_ents - 1]);
			alloc_size = curr_max_ents;
			sg_size = alloc_size - 1;
		} else {
			sg_size = alloc_size;
			next = NULL;
		}

		num_ents -= sg_size;
		if (nents_first_chunk)
			nents_first_chunk = 0;
		else
			free_fn(sgl, alloc_size);
		sgl = next;
		curr_max_ents = max_ents;
	}

	table->sgl = NULL;
}
EXPORT_SYMBOL(__sg_free_table);

/**
 * sg_free_append_table - Free a previously allocated append sg table.
 * @table:	 The mapped sg append table header
 *
 **/
void sg_free_append_table(struct sg_append_table *table)
{
	__sg_free_table(&table->sgt, SG_MAX_SINGLE_ALLOC, 0, sg_kfree,
			table->total_nents);
}
EXPORT_SYMBOL(sg_free_append_table);


/**
 * sg_free_table - Free a previously allocated sg table
 * @table:	The mapped sg table header
 *
 **/
void sg_free_table(struct sg_table *table)
{
	__sg_free_table(table, SG_MAX_SINGLE_ALLOC, 0, sg_kfree,
			table->orig_nents);
}
EXPORT_SYMBOL(sg_free_table);

/**
 * __sg_alloc_table - Allocate and initialize an sg table with given allocator
 * @table:	The sg table header to use
 * @nents:	Number of entries in sg list
 * @max_ents:	The maximum number of entries the allocator returns per call
 * @first_chunk: first SGL if preallocated (may be %NULL)
 * @nents_first_chunk: Number of entries in the (preallocated) first
 * 	scatterlist chunk, 0 means no such preallocated chunk provided by user
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
		     unsigned int nents_first_chunk, gfp_t gfp_mask,
		     sg_alloc_fn *alloc_fn)
{
	struct scatterlist *sg, *prv;
	unsigned int left;
	unsigned curr_max_ents = nents_first_chunk ?: max_ents;
	unsigned prv_max_ents;

	memset(table, 0, sizeof(*table));

	if (nents == 0)
		return -EINVAL;
#ifdef CONFIG_ARCH_NO_SG_CHAIN
	if (WARN_ON_ONCE(nents > max_ents))
		return -EINVAL;
#endif

	left = nents;
	prv = NULL;
	do {
		unsigned int sg_size, alloc_size = left;

		if (alloc_size > curr_max_ents) {
			alloc_size = curr_max_ents;
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
			sg_chain(prv, prv_max_ents, sg);
		else
			table->sgl = sg;

		/*
		 * If no more entries after this one, mark the end
		 */
		if (!left)
			sg_mark_end(&sg[sg_size - 1]);

		prv = sg;
		prv_max_ents = curr_max_ents;
		curr_max_ents = max_ents;
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
			       NULL, 0, gfp_mask, sg_kmalloc);
	if (unlikely(ret))
		sg_free_table(table);
	return ret;
}
EXPORT_SYMBOL(sg_alloc_table);

static struct scatterlist *get_next_sg(struct sg_append_table *table,
				       struct scatterlist *cur,
				       unsigned long needed_sges,
				       gfp_t gfp_mask)
{
	struct scatterlist *new_sg, *next_sg;
	unsigned int alloc_size;

	if (cur) {
		next_sg = sg_next(cur);
		/* Check if last entry should be keeped for chainning */
		if (!sg_is_last(next_sg) || needed_sges == 1)
			return next_sg;
	}

	alloc_size = min_t(unsigned long, needed_sges, SG_MAX_SINGLE_ALLOC);
	new_sg = sg_kmalloc(alloc_size, gfp_mask);
	if (!new_sg)
		return ERR_PTR(-ENOMEM);
	sg_init_table(new_sg, alloc_size);
	if (cur) {
		table->total_nents += alloc_size - 1;
		__sg_chain(next_sg, new_sg);
	} else {
		table->sgt.sgl = new_sg;
		table->total_nents = alloc_size;
	}
	return new_sg;
}

static bool pages_are_mergeable(struct page *a, struct page *b)
{
	if (page_to_pfn(a) != page_to_pfn(b) + 1)
		return false;
	if (!zone_device_pages_have_same_pgmap(a, b))
		return false;
	return true;
}

/**
 * sg_alloc_append_table_from_pages - Allocate and initialize an append sg
 *                                    table from an array of pages
 * @sgt_append:  The sg append table to use
 * @pages:       Pointer to an array of page pointers
 * @n_pages:     Number of pages in the pages array
 * @offset:      Offset from start of the first page to the start of a buffer
 * @size:        Number of valid bytes in the buffer (after offset)
 * @max_segment: Maximum size of a scatterlist element in bytes
 * @left_pages:  Left pages caller have to set after this call
 * @gfp_mask:	 GFP allocation mask
 *
 * Description:
 *    In the first call it allocate and initialize an sg table from a list of
 *    pages, else reuse the scatterlist from sgt_append. Contiguous ranges of
 *    the pages are squashed into a single scatterlist entry up to the maximum
 *    size specified in @max_segment.  A user may provide an offset at a start
 *    and a size of valid data in a buffer specified by the page array. The
 *    returned sg table is released by sg_free_append_table
 *
 * Returns:
 *   0 on success, negative error on failure
 *
 * Notes:
 *   If this function returns non-0 (eg failure), the caller must call
 *   sg_free_append_table() to cleanup any leftover allocations.
 *
 *   In the fist call, sgt_append must by initialized.
 */
int sg_alloc_append_table_from_pages(struct sg_append_table *sgt_append,
		struct page **pages, unsigned int n_pages, unsigned int offset,
		unsigned long size, unsigned int max_segment,
		unsigned int left_pages, gfp_t gfp_mask)
{
	unsigned int chunks, cur_page, seg_len, i, prv_len = 0;
	unsigned int added_nents = 0;
	struct scatterlist *s = sgt_append->prv;
	struct page *last_pg;

	/*
	 * The algorithm below requires max_segment to be aligned to PAGE_SIZE
	 * otherwise it can overshoot.
	 */
	max_segment = ALIGN_DOWN(max_segment, PAGE_SIZE);
	if (WARN_ON(max_segment < PAGE_SIZE))
		return -EINVAL;

	if (IS_ENABLED(CONFIG_ARCH_NO_SG_CHAIN) && sgt_append->prv)
		return -EOPNOTSUPP;

	if (sgt_append->prv) {
		unsigned long next_pfn = (page_to_phys(sg_page(sgt_append->prv)) +
			sgt_append->prv->offset + sgt_append->prv->length) / PAGE_SIZE;

		if (WARN_ON(offset))
			return -EINVAL;

		/* Merge contiguous pages into the last SG */
		prv_len = sgt_append->prv->length;
		if (page_to_pfn(pages[0]) == next_pfn) {
			last_pg = pfn_to_page(next_pfn - 1);
			while (n_pages && pages_are_mergeable(pages[0], last_pg)) {
				if (sgt_append->prv->length + PAGE_SIZE > max_segment)
					break;
				sgt_append->prv->length += PAGE_SIZE;
				last_pg = pages[0];
				pages++;
				n_pages--;
			}
			if (!n_pages)
				goto out;
		}
	}

	/* compute number of contiguous chunks */
	chunks = 1;
	seg_len = 0;
	for (i = 1; i < n_pages; i++) {
		seg_len += PAGE_SIZE;
		if (seg_len >= max_segment ||
		    !pages_are_mergeable(pages[i], pages[i - 1])) {
			chunks++;
			seg_len = 0;
		}
	}

	/* merging chunks and putting them into the scatterlist */
	cur_page = 0;
	for (i = 0; i < chunks; i++) {
		unsigned int j, chunk_size;

		/* look for the end of the current chunk */
		seg_len = 0;
		for (j = cur_page + 1; j < n_pages; j++) {
			seg_len += PAGE_SIZE;
			if (seg_len >= max_segment ||
			    !pages_are_mergeable(pages[j], pages[j - 1]))
				break;
		}

		/* Pass how many chunks might be left */
		s = get_next_sg(sgt_append, s, chunks - i + left_pages,
				gfp_mask);
		if (IS_ERR(s)) {
			/*
			 * Adjust entry length to be as before function was
			 * called.
			 */
			if (sgt_append->prv)
				sgt_append->prv->length = prv_len;
			return PTR_ERR(s);
		}
		chunk_size = ((j - cur_page) << PAGE_SHIFT) - offset;
		sg_set_page(s, pages[cur_page],
			    min_t(unsigned long, size, chunk_size), offset);
		added_nents++;
		size -= chunk_size;
		offset = 0;
		cur_page = j;
	}
	sgt_append->sgt.nents += added_nents;
	sgt_append->sgt.orig_nents = sgt_append->sgt.nents;
	sgt_append->prv = s;
out:
	if (!left_pages)
		sg_mark_end(s);
	return 0;
}
EXPORT_SYMBOL(sg_alloc_append_table_from_pages);

/**
 * sg_alloc_table_from_pages_segment - Allocate and initialize an sg table from
 *                                     an array of pages and given maximum
 *                                     segment.
 * @sgt:	 The sg table header to use
 * @pages:	 Pointer to an array of page pointers
 * @n_pages:	 Number of pages in the pages array
 * @offset:      Offset from start of the first page to the start of a buffer
 * @size:        Number of valid bytes in the buffer (after offset)
 * @max_segment: Maximum size of a scatterlist element in bytes
 * @gfp_mask:	 GFP allocation mask
 *
 *  Description:
 *    Allocate and initialize an sg table from a list of pages. Contiguous
 *    ranges of the pages are squashed into a single scatterlist node up to the
 *    maximum size specified in @max_segment. A user may provide an offset at a
 *    start and a size of valid data in a buffer specified by the page array.
 *
 *    The returned sg table is released by sg_free_table.
 *
 *  Returns:
 *   0 on success, negative error on failure
 */
int sg_alloc_table_from_pages_segment(struct sg_table *sgt, struct page **pages,
				unsigned int n_pages, unsigned int offset,
				unsigned long size, unsigned int max_segment,
				gfp_t gfp_mask)
{
	struct sg_append_table append = {};
	int err;

	err = sg_alloc_append_table_from_pages(&append, pages, n_pages, offset,
					       size, max_segment, 0, gfp_mask);
	if (err) {
		sg_free_append_table(&append);
		return err;
	}
	memcpy(sgt, &append.sgt, sizeof(*sgt));
	WARN_ON(append.total_nents != sgt->orig_nents);
	return 0;
}
EXPORT_SYMBOL(sg_alloc_table_from_pages_segment);

#ifdef CONFIG_SGL_ALLOC

/**
 * sgl_alloc_order - allocate a scatterlist and its pages
 * @length: Length in bytes of the scatterlist. Must be at least one
 * @order: Second argument for alloc_pages()
 * @chainable: Whether or not to allocate an extra element in the scatterlist
 *	for scatterlist chaining purposes
 * @gfp: Memory allocation flags
 * @nent_p: [out] Number of entries in the scatterlist that have pages
 *
 * Returns: A pointer to an initialized scatterlist or %NULL upon failure.
 */
struct scatterlist *sgl_alloc_order(unsigned long long length,
				    unsigned int order, bool chainable,
				    gfp_t gfp, unsigned int *nent_p)
{
	struct scatterlist *sgl, *sg;
	struct page *page;
	unsigned int nent, nalloc;
	u32 elem_len;

	nent = round_up(length, PAGE_SIZE << order) >> (PAGE_SHIFT + order);
	/* Check for integer overflow */
	if (length > (nent << (PAGE_SHIFT + order)))
		return NULL;
	nalloc = nent;
	if (chainable) {
		/* Check for integer overflow */
		if (nalloc + 1 < nalloc)
			return NULL;
		nalloc++;
	}
	sgl = kmalloc_array(nalloc, sizeof(struct scatterlist),
			    gfp & ~GFP_DMA);
	if (!sgl)
		return NULL;

	sg_init_table(sgl, nalloc);
	sg = sgl;
	while (length) {
		elem_len = min_t(u64, length, PAGE_SIZE << order);
		page = alloc_pages(gfp, order);
		if (!page) {
			sgl_free_order(sgl, order);
			return NULL;
		}

		sg_set_page(sg, page, elem_len, 0);
		length -= elem_len;
		sg = sg_next(sg);
	}
	WARN_ONCE(length, "length = %lld\n", length);
	if (nent_p)
		*nent_p = nent;
	return sgl;
}
EXPORT_SYMBOL(sgl_alloc_order);

/**
 * sgl_alloc - allocate a scatterlist and its pages
 * @length: Length in bytes of the scatterlist
 * @gfp: Memory allocation flags
 * @nent_p: [out] Number of entries in the scatterlist
 *
 * Returns: A pointer to an initialized scatterlist or %NULL upon failure.
 */
struct scatterlist *sgl_alloc(unsigned long long length, gfp_t gfp,
			      unsigned int *nent_p)
{
	return sgl_alloc_order(length, 0, false, gfp, nent_p);
}
EXPORT_SYMBOL(sgl_alloc);

/**
 * sgl_free_n_order - free a scatterlist and its pages
 * @sgl: Scatterlist with one or more elements
 * @nents: Maximum number of elements to free
 * @order: Second argument for __free_pages()
 *
 * Notes:
 * - If several scatterlists have been chained and each chain element is
 *   freed separately then it's essential to set nents correctly to avoid that a
 *   page would get freed twice.
 * - All pages in a chained scatterlist can be freed at once by setting @nents
 *   to a high number.
 */
void sgl_free_n_order(struct scatterlist *sgl, int nents, int order)
{
	struct scatterlist *sg;
	struct page *page;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		if (!sg)
			break;
		page = sg_page(sg);
		if (page)
			__free_pages(page, order);
	}
	kfree(sgl);
}
EXPORT_SYMBOL(sgl_free_n_order);

/**
 * sgl_free_order - free a scatterlist and its pages
 * @sgl: Scatterlist with one or more elements
 * @order: Second argument for __free_pages()
 */
void sgl_free_order(struct scatterlist *sgl, int order)
{
	sgl_free_n_order(sgl, INT_MAX, order);
}
EXPORT_SYMBOL(sgl_free_order);

/**
 * sgl_free - free a scatterlist and its pages
 * @sgl: Scatterlist with one or more elements
 */
void sgl_free(struct scatterlist *sgl)
{
	sgl_free_order(sgl, 0);
}
EXPORT_SYMBOL(sgl_free);

#endif /* CONFIG_SGL_ALLOC */

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

static int sg_dma_page_count(struct scatterlist *sg)
{
	return PAGE_ALIGN(sg->offset + sg_dma_len(sg)) >> PAGE_SHIFT;
}

bool __sg_page_iter_dma_next(struct sg_dma_page_iter *dma_iter)
{
	struct sg_page_iter *piter = &dma_iter->base;

	if (!piter->__nents || !piter->sg)
		return false;

	piter->sg_pgoffset += piter->__pg_advance;
	piter->__pg_advance = 1;

	while (piter->sg_pgoffset >= sg_dma_page_count(piter->sg)) {
		piter->sg_pgoffset -= sg_dma_page_count(piter->sg);
		piter->sg = sg_next(piter->sg);
		if (!--piter->__nents || !piter->sg)
			return false;
	}

	return true;
}
EXPORT_SYMBOL(__sg_page_iter_dma_next);

/**
 * sg_miter_start - start mapping iteration over a sg list
 * @miter: sg mapping iter to be started
 * @sgl: sg list to iterate over
 * @nents: number of sg entries
 * @flags: sg iterator flags
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

		if (!__sg_page_iter_next(&miter->piter))
			return false;

		sg = miter->piter.sg;

		miter->__offset = miter->piter.sg_pgoffset ? 0 : sg->offset;
		miter->piter.sg_pgoffset += miter->__offset >> PAGE_SHIFT;
		miter->__offset &= PAGE_SIZE - 1;
		miter->__remaining = sg->offset + sg->length -
				     (miter->piter.sg_pgoffset << PAGE_SHIFT) -
				     miter->__offset;
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
 *   Don't care.
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
 *   May sleep if !SG_MITER_ATOMIC.
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
 *   using sg_miter_start().  A stopped iteration can be resumed by
 *   calling sg_miter_next() on it.  This is useful when resources (kmap)
 *   need to be released during iteration.
 *
 * Context:
 *   Don't care otherwise.
 */
void sg_miter_stop(struct sg_mapping_iter *miter)
{
	WARN_ON(miter->consumed > miter->length);

	/* drop resources from the last iteration */
	if (miter->addr) {
		miter->__offset += miter->consumed;
		miter->__remaining -= miter->consumed;

		if (miter->__flags & SG_MITER_TO_SG)
			flush_dcache_page(miter->page);

		if (miter->__flags & SG_MITER_ATOMIC) {
			WARN_ON_ONCE(!pagefault_disabled());
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
 *			 buffer, false == from a buffer to an sg list)
 *
 * Returns the number of copied bytes.
 *
 **/
size_t sg_copy_buffer(struct scatterlist *sgl, unsigned int nents, void *buf,
		      size_t buflen, off_t skip, bool to_buffer)
{
	unsigned int offset = 0;
	struct sg_mapping_iter miter;
	unsigned int sg_flags = SG_MITER_ATOMIC;

	if (to_buffer)
		sg_flags |= SG_MITER_FROM_SG;
	else
		sg_flags |= SG_MITER_TO_SG;

	sg_miter_start(&miter, sgl, nents, sg_flags);

	if (!sg_miter_skip(&miter, skip))
		return 0;

	while ((offset < buflen) && sg_miter_next(&miter)) {
		unsigned int len;

		len = min(miter.length, buflen - offset);

		if (to_buffer)
			memcpy(buf + offset, miter.addr, len);
		else
			memcpy(miter.addr, buf + offset, len);

		offset += len;
	}

	sg_miter_stop(&miter);

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

/**
 * sg_zero_buffer - Zero-out a part of a SG list
 * @sgl:		 The SG list
 * @nents:		 Number of SG entries
 * @buflen:		 The number of bytes to zero out
 * @skip:		 Number of bytes to skip before zeroing
 *
 * Returns the number of bytes zeroed.
 **/
size_t sg_zero_buffer(struct scatterlist *sgl, unsigned int nents,
		       size_t buflen, off_t skip)
{
	unsigned int offset = 0;
	struct sg_mapping_iter miter;
	unsigned int sg_flags = SG_MITER_ATOMIC | SG_MITER_TO_SG;

	sg_miter_start(&miter, sgl, nents, sg_flags);

	if (!sg_miter_skip(&miter, skip))
		return false;

	while (offset < buflen && sg_miter_next(&miter)) {
		unsigned int len;

		len = min(miter.length, buflen - offset);
		memset(miter.addr, 0, len);

		offset += len;
	}

	sg_miter_stop(&miter);
	return offset;
}
EXPORT_SYMBOL(sg_zero_buffer);

/*
 * Extract and pin a list of up to sg_max pages from UBUF- or IOVEC-class
 * iterators, and add them to the scatterlist.
 */
static ssize_t extract_user_to_sg(struct iov_iter *iter,
				  ssize_t maxsize,
				  struct sg_table *sgtable,
				  unsigned int sg_max,
				  iov_iter_extraction_t extraction_flags)
{
	struct scatterlist *sg = sgtable->sgl + sgtable->nents;
	struct page **pages;
	unsigned int npages;
	ssize_t ret = 0, res;
	size_t len, off;

	/* We decant the page list into the tail of the scatterlist */
	pages = (void *)sgtable->sgl +
		array_size(sg_max, sizeof(struct scatterlist));
	pages -= sg_max;

	do {
		res = iov_iter_extract_pages(iter, &pages, maxsize, sg_max,
					     extraction_flags, &off);
		if (res <= 0)
			goto failed;

		len = res;
		maxsize -= len;
		ret += len;
		npages = DIV_ROUND_UP(off + len, PAGE_SIZE);
		sg_max -= npages;

		for (; npages > 0; npages--) {
			struct page *page = *pages;
			size_t seg = min_t(size_t, PAGE_SIZE - off, len);

			*pages++ = NULL;
			sg_set_page(sg, page, seg, off);
			sgtable->nents++;
			sg++;
			len -= seg;
			off = 0;
		}
	} while (maxsize > 0 && sg_max > 0);

	return ret;

failed:
	while (sgtable->nents > sgtable->orig_nents)
		unpin_user_page(sg_page(&sgtable->sgl[--sgtable->nents]));
	return res;
}

/*
 * Extract up to sg_max pages from a BVEC-type iterator and add them to the
 * scatterlist.  The pages are not pinned.
 */
static ssize_t extract_bvec_to_sg(struct iov_iter *iter,
				  ssize_t maxsize,
				  struct sg_table *sgtable,
				  unsigned int sg_max,
				  iov_iter_extraction_t extraction_flags)
{
	const struct bio_vec *bv = iter->bvec;
	struct scatterlist *sg = sgtable->sgl + sgtable->nents;
	unsigned long start = iter->iov_offset;
	unsigned int i;
	ssize_t ret = 0;

	for (i = 0; i < iter->nr_segs; i++) {
		size_t off, len;

		len = bv[i].bv_len;
		if (start >= len) {
			start -= len;
			continue;
		}

		len = min_t(size_t, maxsize, len - start);
		off = bv[i].bv_offset + start;

		sg_set_page(sg, bv[i].bv_page, len, off);
		sgtable->nents++;
		sg++;
		sg_max--;

		ret += len;
		maxsize -= len;
		if (maxsize <= 0 || sg_max == 0)
			break;
		start = 0;
	}

	if (ret > 0)
		iov_iter_advance(iter, ret);
	return ret;
}

/*
 * Extract up to sg_max pages from a KVEC-type iterator and add them to the
 * scatterlist.  This can deal with vmalloc'd buffers as well as kmalloc'd or
 * static buffers.  The pages are not pinned.
 */
static ssize_t extract_kvec_to_sg(struct iov_iter *iter,
				  ssize_t maxsize,
				  struct sg_table *sgtable,
				  unsigned int sg_max,
				  iov_iter_extraction_t extraction_flags)
{
	const struct kvec *kv = iter->kvec;
	struct scatterlist *sg = sgtable->sgl + sgtable->nents;
	unsigned long start = iter->iov_offset;
	unsigned int i;
	ssize_t ret = 0;

	for (i = 0; i < iter->nr_segs; i++) {
		struct page *page;
		unsigned long kaddr;
		size_t off, len, seg;

		len = kv[i].iov_len;
		if (start >= len) {
			start -= len;
			continue;
		}

		kaddr = (unsigned long)kv[i].iov_base + start;
		off = kaddr & ~PAGE_MASK;
		len = min_t(size_t, maxsize, len - start);
		kaddr &= PAGE_MASK;

		maxsize -= len;
		ret += len;
		do {
			seg = min_t(size_t, len, PAGE_SIZE - off);
			if (is_vmalloc_or_module_addr((void *)kaddr))
				page = vmalloc_to_page((void *)kaddr);
			else
				page = virt_to_page((void *)kaddr);

			sg_set_page(sg, page, len, off);
			sgtable->nents++;
			sg++;
			sg_max--;

			len -= seg;
			kaddr += PAGE_SIZE;
			off = 0;
		} while (len > 0 && sg_max > 0);

		if (maxsize <= 0 || sg_max == 0)
			break;
		start = 0;
	}

	if (ret > 0)
		iov_iter_advance(iter, ret);
	return ret;
}

/*
 * Extract up to sg_max folios from an FOLIOQ-type iterator and add them to
 * the scatterlist.  The pages are not pinned.
 */
static ssize_t extract_folioq_to_sg(struct iov_iter *iter,
				   ssize_t maxsize,
				   struct sg_table *sgtable,
				   unsigned int sg_max,
				   iov_iter_extraction_t extraction_flags)
{
	const struct folio_queue *folioq = iter->folioq;
	struct scatterlist *sg = sgtable->sgl + sgtable->nents;
	unsigned int slot = iter->folioq_slot;
	ssize_t ret = 0;
	size_t offset = iter->iov_offset;

	BUG_ON(!folioq);

	if (slot >= folioq_nr_slots(folioq)) {
		folioq = folioq->next;
		if (WARN_ON_ONCE(!folioq))
			return 0;
		slot = 0;
	}

	do {
		struct folio *folio = folioq_folio(folioq, slot);
		size_t fsize = folioq_folio_size(folioq, slot);

		if (offset < fsize) {
			size_t part = umin(maxsize - ret, fsize - offset);

			sg_set_page(sg, folio_page(folio, 0), part, offset);
			sgtable->nents++;
			sg++;
			sg_max--;
			offset += part;
			ret += part;
		}

		if (offset >= fsize) {
			offset = 0;
			slot++;
			if (slot >= folioq_nr_slots(folioq)) {
				if (!folioq->next) {
					WARN_ON_ONCE(ret < iter->count);
					break;
				}
				folioq = folioq->next;
				slot = 0;
			}
		}
	} while (sg_max > 0 && ret < maxsize);

	iter->folioq = folioq;
	iter->folioq_slot = slot;
	iter->iov_offset = offset;
	iter->count -= ret;
	return ret;
}

/*
 * Extract up to sg_max folios from an XARRAY-type iterator and add them to
 * the scatterlist.  The pages are not pinned.
 */
static ssize_t extract_xarray_to_sg(struct iov_iter *iter,
				    ssize_t maxsize,
				    struct sg_table *sgtable,
				    unsigned int sg_max,
				    iov_iter_extraction_t extraction_flags)
{
	struct scatterlist *sg = sgtable->sgl + sgtable->nents;
	struct xarray *xa = iter->xarray;
	struct folio *folio;
	loff_t start = iter->xarray_start + iter->iov_offset;
	pgoff_t index = start / PAGE_SIZE;
	ssize_t ret = 0;
	size_t offset, len;
	XA_STATE(xas, xa, index);

	rcu_read_lock();

	xas_for_each(&xas, folio, ULONG_MAX) {
		if (xas_retry(&xas, folio))
			continue;
		if (WARN_ON(xa_is_value(folio)))
			break;
		if (WARN_ON(folio_test_hugetlb(folio)))
			break;

		offset = offset_in_folio(folio, start);
		len = min_t(size_t, maxsize, folio_size(folio) - offset);

		sg_set_page(sg, folio_page(folio, 0), len, offset);
		sgtable->nents++;
		sg++;
		sg_max--;

		maxsize -= len;
		ret += len;
		if (maxsize <= 0 || sg_max == 0)
			break;
	}

	rcu_read_unlock();
	if (ret > 0)
		iov_iter_advance(iter, ret);
	return ret;
}

/**
 * extract_iter_to_sg - Extract pages from an iterator and add to an sglist
 * @iter: The iterator to extract from
 * @maxsize: The amount of iterator to copy
 * @sgtable: The scatterlist table to fill in
 * @sg_max: Maximum number of elements in @sgtable that may be filled
 * @extraction_flags: Flags to qualify the request
 *
 * Extract the page fragments from the given amount of the source iterator and
 * add them to a scatterlist that refers to all of those bits, to a maximum
 * addition of @sg_max elements.
 *
 * The pages referred to by UBUF- and IOVEC-type iterators are extracted and
 * pinned; BVEC-, KVEC-, FOLIOQ- and XARRAY-type are extracted but aren't
 * pinned; DISCARD-type is not supported.
 *
 * No end mark is placed on the scatterlist; that's left to the caller.
 *
 * @extraction_flags can have ITER_ALLOW_P2PDMA set to request peer-to-peer DMA
 * be allowed on the pages extracted.
 *
 * If successful, @sgtable->nents is updated to include the number of elements
 * added and the number of bytes added is returned.  @sgtable->orig_nents is
 * left unaltered.
 *
 * The iov_iter_extract_mode() function should be used to query how cleanup
 * should be performed.
 */
ssize_t extract_iter_to_sg(struct iov_iter *iter, size_t maxsize,
			   struct sg_table *sgtable, unsigned int sg_max,
			   iov_iter_extraction_t extraction_flags)
{
	if (maxsize == 0)
		return 0;

	switch (iov_iter_type(iter)) {
	case ITER_UBUF:
	case ITER_IOVEC:
		return extract_user_to_sg(iter, maxsize, sgtable, sg_max,
					  extraction_flags);
	case ITER_BVEC:
		return extract_bvec_to_sg(iter, maxsize, sgtable, sg_max,
					  extraction_flags);
	case ITER_KVEC:
		return extract_kvec_to_sg(iter, maxsize, sgtable, sg_max,
					  extraction_flags);
	case ITER_FOLIOQ:
		return extract_folioq_to_sg(iter, maxsize, sgtable, sg_max,
					    extraction_flags);
	case ITER_XARRAY:
		return extract_xarray_to_sg(iter, maxsize, sgtable, sg_max,
					    extraction_flags);
	default:
		pr_err("%s(%u) unsupported\n", __func__, iov_iter_type(iter));
		WARN_ON_ONCE(1);
		return -EIO;
	}
}
EXPORT_SYMBOL_GPL(extract_iter_to_sg);
