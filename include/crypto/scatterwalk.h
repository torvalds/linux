/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cryptographic scatter and gather helpers.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 Adam J. Richter <adam@yggdrasil.com>
 * Copyright (c) 2004 Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 */

#ifndef _CRYPTO_SCATTERWALK_H
#define _CRYPTO_SCATTERWALK_H

#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

struct scatter_walk {
	/* Must be the first member, see struct skcipher_walk. */
	union {
		void *const addr;

		/* Private API field, do not touch. */
		union crypto_no_such_thing *__addr;
	};
	struct scatterlist *sg;
	unsigned int offset;
};

struct skcipher_walk {
	union {
		/* Virtual address of the source. */
		struct {
			struct {
				const void *const addr;
			} virt;
		} src;

		/* Private field for the API, do not use. */
		struct scatter_walk in;
	};

	union {
		/* Virtual address of the destination. */
		struct {
			struct {
				void *const addr;
			} virt;
		} dst;

		/* Private field for the API, do not use. */
		struct scatter_walk out;
	};

	unsigned int nbytes;
	unsigned int total;

	u8 *page;
	u8 *buffer;
	u8 *oiv;
	void *iv;

	unsigned int ivsize;

	int flags;
	unsigned int blocksize;
	unsigned int stride;
	unsigned int alignmask;
};

static inline void scatterwalk_crypto_chain(struct scatterlist *head,
					    struct scatterlist *sg, int num)
{
	if (sg)
		sg_chain(head, num, sg);
	else
		sg_mark_end(head);
}

static inline void scatterwalk_start(struct scatter_walk *walk,
				     struct scatterlist *sg)
{
	walk->sg = sg;
	walk->offset = sg->offset;
}

/*
 * This is equivalent to scatterwalk_start(walk, sg) followed by
 * scatterwalk_skip(walk, pos).
 */
static inline void scatterwalk_start_at_pos(struct scatter_walk *walk,
					    struct scatterlist *sg,
					    unsigned int pos)
{
	while (pos > sg->length) {
		pos -= sg->length;
		sg = sg_next(sg);
	}
	walk->sg = sg;
	walk->offset = sg->offset + pos;
}

static inline unsigned int scatterwalk_clamp(struct scatter_walk *walk,
					     unsigned int nbytes)
{
	unsigned int len_this_sg;
	unsigned int limit;

	if (walk->offset >= walk->sg->offset + walk->sg->length)
		scatterwalk_start(walk, sg_next(walk->sg));
	len_this_sg = walk->sg->offset + walk->sg->length - walk->offset;

	/*
	 * HIGHMEM case: the page may have to be mapped into memory.  To avoid
	 * the complexity of having to map multiple pages at once per sg entry,
	 * clamp the returned length to not cross a page boundary.
	 *
	 * !HIGHMEM case: no mapping is needed; all pages of the sg entry are
	 * already mapped contiguously in the kernel's direct map.  For improved
	 * performance, allow the walker to return data segments that cross a
	 * page boundary.  Do still cap the length to PAGE_SIZE, since some
	 * users rely on that to avoid disabling preemption for too long when
	 * using SIMD.  It's also needed for when skcipher_walk uses a bounce
	 * page due to the data not being aligned to the algorithm's alignmask.
	 */
	if (IS_ENABLED(CONFIG_HIGHMEM))
		limit = PAGE_SIZE - offset_in_page(walk->offset);
	else
		limit = PAGE_SIZE;

	return min3(nbytes, len_this_sg, limit);
}

/*
 * Create a scatterlist that represents the remaining data in a walk.  Uses
 * chaining to reference the original scatterlist, so this uses at most two
 * entries in @sg_out regardless of the number of entries in the original list.
 * Assumes that sg_init_table() was already done.
 */
static inline void scatterwalk_get_sglist(struct scatter_walk *walk,
					  struct scatterlist sg_out[2])
{
	if (walk->offset >= walk->sg->offset + walk->sg->length)
		scatterwalk_start(walk, sg_next(walk->sg));
	sg_set_page(sg_out, sg_page(walk->sg),
		    walk->sg->offset + walk->sg->length - walk->offset,
		    walk->offset);
	scatterwalk_crypto_chain(sg_out, sg_next(walk->sg), 2);
}

static inline void scatterwalk_map(struct scatter_walk *walk)
{
	struct page *base_page = sg_page(walk->sg);
	unsigned int offset = walk->offset;
	void *addr;

	if (IS_ENABLED(CONFIG_HIGHMEM)) {
		struct page *page;

		page = base_page + (offset >> PAGE_SHIFT);
		offset = offset_in_page(offset);
		addr = kmap_local_page(page) + offset;
	} else {
		/*
		 * When !HIGHMEM we allow the walker to return segments that
		 * span a page boundary; see scatterwalk_clamp().  To make it
		 * clear that in this case we're working in the linear buffer of
		 * the whole sg entry in the kernel's direct map rather than
		 * within the mapped buffer of a single page, compute the
		 * address as an offset from the page_address() of the first
		 * page of the sg entry.  Either way the result is the address
		 * in the direct map, but this makes it clearer what is really
		 * going on.
		 */
		addr = page_address(base_page) + offset;
	}

	walk->__addr = addr;
}

/**
 * scatterwalk_next() - Get the next data buffer in a scatterlist walk
 * @walk: the scatter_walk
 * @total: the total number of bytes remaining, > 0
 *
 * A virtual address for the next segment of data from the scatterlist will
 * be placed into @walk->addr.  The caller must call scatterwalk_done_src()
 * or scatterwalk_done_dst() when it is done using this virtual address.
 *
 * Returns: the next number of bytes available, <= @total
 */
static inline unsigned int scatterwalk_next(struct scatter_walk *walk,
					    unsigned int total)
{
	unsigned int nbytes = scatterwalk_clamp(walk, total);

	scatterwalk_map(walk);
	return nbytes;
}

static inline void scatterwalk_unmap(struct scatter_walk *walk)
{
	if (IS_ENABLED(CONFIG_HIGHMEM))
		kunmap_local(walk->__addr);
}

static inline void scatterwalk_advance(struct scatter_walk *walk,
				       unsigned int nbytes)
{
	walk->offset += nbytes;
}

/**
 * scatterwalk_done_src() - Finish one step of a walk of source scatterlist
 * @walk: the scatter_walk
 * @nbytes: the number of bytes processed this step, less than or equal to the
 *	    number of bytes that scatterwalk_next() returned.
 *
 * Use this if the mapped address was not written to, i.e. it is source data.
 */
static inline void scatterwalk_done_src(struct scatter_walk *walk,
					unsigned int nbytes)
{
	scatterwalk_unmap(walk);
	scatterwalk_advance(walk, nbytes);
}

/**
 * scatterwalk_done_dst() - Finish one step of a walk of destination scatterlist
 * @walk: the scatter_walk
 * @nbytes: the number of bytes processed this step, less than or equal to the
 *	    number of bytes that scatterwalk_next() returned.
 *
 * Use this if the mapped address may have been written to, i.e. it is
 * destination data.
 */
static inline void scatterwalk_done_dst(struct scatter_walk *walk,
					unsigned int nbytes)
{
	scatterwalk_unmap(walk);
	/*
	 * Explicitly check ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE instead of just
	 * relying on flush_dcache_page() being a no-op when not implemented,
	 * since otherwise the BUG_ON in sg_page() does not get optimized out.
	 * This also avoids having to consider whether the loop would get
	 * reliably optimized out or not.
	 */
	if (ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE) {
		struct page *base_page;
		unsigned int offset;
		int start, end, i;

		base_page = sg_page(walk->sg);
		offset = walk->offset;
		start = offset >> PAGE_SHIFT;
		end = start + (nbytes >> PAGE_SHIFT);
		end += (offset_in_page(offset) + offset_in_page(nbytes) +
			PAGE_SIZE - 1) >> PAGE_SHIFT;
		for (i = start; i < end; i++)
			flush_dcache_page(base_page + i);
	}
	scatterwalk_advance(walk, nbytes);
}

void scatterwalk_skip(struct scatter_walk *walk, unsigned int nbytes);

void memcpy_from_scatterwalk(void *buf, struct scatter_walk *walk,
			     unsigned int nbytes);

void memcpy_to_scatterwalk(struct scatter_walk *walk, const void *buf,
			   unsigned int nbytes);

void memcpy_from_sglist(void *buf, struct scatterlist *sg,
			unsigned int start, unsigned int nbytes);

void memcpy_to_sglist(struct scatterlist *sg, unsigned int start,
		      const void *buf, unsigned int nbytes);

void memcpy_sglist(struct scatterlist *dst, struct scatterlist *src,
		   unsigned int nbytes);

/* In new code, please use memcpy_{from,to}_sglist() directly instead. */
static inline void scatterwalk_map_and_copy(void *buf, struct scatterlist *sg,
					    unsigned int start,
					    unsigned int nbytes, int out)
{
	if (out)
		memcpy_to_sglist(sg, start, buf, nbytes);
	else
		memcpy_from_sglist(buf, sg, start, nbytes);
}

struct scatterlist *scatterwalk_ffwd(struct scatterlist dst[2],
				     struct scatterlist *src,
				     unsigned int len);

int skcipher_walk_first(struct skcipher_walk *walk, bool atomic);
int skcipher_walk_done(struct skcipher_walk *walk, int res);

static inline void skcipher_walk_abort(struct skcipher_walk *walk)
{
	skcipher_walk_done(walk, -ECANCELED);
}

#endif  /* _CRYPTO_SCATTERWALK_H */
