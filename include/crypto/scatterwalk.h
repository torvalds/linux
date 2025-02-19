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

#include <crypto/algapi.h>

#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>

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

static inline unsigned int scatterwalk_pagelen(struct scatter_walk *walk)
{
	unsigned int len = walk->sg->offset + walk->sg->length - walk->offset;
	unsigned int len_this_page = offset_in_page(~walk->offset) + 1;
	return len_this_page > len ? len : len_this_page;
}

static inline unsigned int scatterwalk_clamp(struct scatter_walk *walk,
					     unsigned int nbytes)
{
	if (walk->offset >= walk->sg->offset + walk->sg->length)
		scatterwalk_start(walk, sg_next(walk->sg));
	return min(nbytes, scatterwalk_pagelen(walk));
}

static inline struct page *scatterwalk_page(struct scatter_walk *walk)
{
	return sg_page(walk->sg) + (walk->offset >> PAGE_SHIFT);
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

static inline void scatterwalk_unmap(void *vaddr)
{
	kunmap_local(vaddr);
}

static inline void *scatterwalk_map(struct scatter_walk *walk)
{
	return kmap_local_page(scatterwalk_page(walk)) +
	       offset_in_page(walk->offset);
}

/**
 * scatterwalk_next() - Get the next data buffer in a scatterlist walk
 * @walk: the scatter_walk
 * @total: the total number of bytes remaining, > 0
 * @nbytes_ret: (out) the next number of bytes available, <= @total
 *
 * Return: A virtual address for the next segment of data from the scatterlist.
 *	   The caller must call scatterwalk_done_src() or scatterwalk_done_dst()
 *	   when it is done using this virtual address.
 */
static inline void *scatterwalk_next(struct scatter_walk *walk,
				     unsigned int total,
				     unsigned int *nbytes_ret)
{
	*nbytes_ret = scatterwalk_clamp(walk, total);
	return scatterwalk_map(walk);
}

static inline void scatterwalk_pagedone(struct scatter_walk *walk, int out,
					unsigned int more)
{
	if (out) {
		struct page *page;

		page = sg_page(walk->sg) + ((walk->offset - 1) >> PAGE_SHIFT);
		flush_dcache_page(page);
	}

	if (more && walk->offset >= walk->sg->offset + walk->sg->length)
		scatterwalk_start(walk, sg_next(walk->sg));
}

static inline void scatterwalk_done(struct scatter_walk *walk, int out,
				    int more)
{
	if (!more || walk->offset >= walk->sg->offset + walk->sg->length ||
	    !(walk->offset & (PAGE_SIZE - 1)))
		scatterwalk_pagedone(walk, out, more);
}

static inline void scatterwalk_advance(struct scatter_walk *walk,
				       unsigned int nbytes)
{
	walk->offset += nbytes;
}

/**
 * scatterwalk_done_src() - Finish one step of a walk of source scatterlist
 * @walk: the scatter_walk
 * @vaddr: the address returned by scatterwalk_next()
 * @nbytes: the number of bytes processed this step, less than or equal to the
 *	    number of bytes that scatterwalk_next() returned.
 *
 * Use this if the @vaddr was not written to, i.e. it is source data.
 */
static inline void scatterwalk_done_src(struct scatter_walk *walk,
					const void *vaddr, unsigned int nbytes)
{
	scatterwalk_unmap((void *)vaddr);
	scatterwalk_advance(walk, nbytes);
}

/**
 * scatterwalk_done_dst() - Finish one step of a walk of destination scatterlist
 * @walk: the scatter_walk
 * @vaddr: the address returned by scatterwalk_next()
 * @nbytes: the number of bytes processed this step, less than or equal to the
 *	    number of bytes that scatterwalk_next() returned.
 *
 * Use this if the @vaddr may have been written to, i.e. it is destination data.
 */
static inline void scatterwalk_done_dst(struct scatter_walk *walk,
					void *vaddr, unsigned int nbytes)
{
	scatterwalk_unmap(vaddr);
	/*
	 * Explicitly check ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE instead of just
	 * relying on flush_dcache_page() being a no-op when not implemented,
	 * since otherwise the BUG_ON in sg_page() does not get optimized out.
	 */
	if (ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE)
		flush_dcache_page(scatterwalk_page(walk));
	scatterwalk_advance(walk, nbytes);
}

void scatterwalk_skip(struct scatter_walk *walk, unsigned int nbytes);

void scatterwalk_copychunks(void *buf, struct scatter_walk *walk,
			    size_t nbytes, int out);

void memcpy_from_scatterwalk(void *buf, struct scatter_walk *walk,
			     unsigned int nbytes);

void memcpy_to_scatterwalk(struct scatter_walk *walk, const void *buf,
			   unsigned int nbytes);

void memcpy_from_sglist(void *buf, struct scatterlist *sg,
			unsigned int start, unsigned int nbytes);

void memcpy_to_sglist(struct scatterlist *sg, unsigned int start,
		      const void *buf, unsigned int nbytes);

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

#endif  /* _CRYPTO_SCATTERWALK_H */
