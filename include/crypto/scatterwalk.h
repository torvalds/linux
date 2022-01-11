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

static inline unsigned int scatterwalk_pagelen(struct scatter_walk *walk)
{
	unsigned int len = walk->sg->offset + walk->sg->length - walk->offset;
	unsigned int len_this_page = offset_in_page(~walk->offset) + 1;
	return len_this_page > len ? len : len_this_page;
}

static inline unsigned int scatterwalk_clamp(struct scatter_walk *walk,
					     unsigned int nbytes)
{
	unsigned int len_this_page = scatterwalk_pagelen(walk);
	return nbytes > len_this_page ? len_this_page : nbytes;
}

static inline void scatterwalk_advance(struct scatter_walk *walk,
				       unsigned int nbytes)
{
	walk->offset += nbytes;
}

static inline unsigned int scatterwalk_aligned(struct scatter_walk *walk,
					       unsigned int alignmask)
{
	return !(walk->offset & alignmask);
}

static inline struct page *scatterwalk_page(struct scatter_walk *walk)
{
	return sg_page(walk->sg) + (walk->offset >> PAGE_SHIFT);
}

static inline void scatterwalk_unmap(void *vaddr)
{
	kunmap_atomic(vaddr);
}

static inline void scatterwalk_start(struct scatter_walk *walk,
				     struct scatterlist *sg)
{
	walk->sg = sg;
	walk->offset = sg->offset;
}

static inline void *scatterwalk_map(struct scatter_walk *walk)
{
	return kmap_atomic(scatterwalk_page(walk)) +
	       offset_in_page(walk->offset);
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

void scatterwalk_copychunks(void *buf, struct scatter_walk *walk,
			    size_t nbytes, int out);
void *scatterwalk_map(struct scatter_walk *walk);

void scatterwalk_map_and_copy(void *buf, struct scatterlist *sg,
			      unsigned int start, unsigned int nbytes, int out);

struct scatterlist *scatterwalk_ffwd(struct scatterlist dst[2],
				     struct scatterlist *src,
				     unsigned int len);

#endif  /* _CRYPTO_SCATTERWALK_H */
