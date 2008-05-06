/*
 * Cryptographic scatter and gather helpers.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 Adam J. Richter <adam@yggdrasil.com>
 * Copyright (c) 2004 Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef _CRYPTO_SCATTERWALK_H
#define _CRYPTO_SCATTERWALK_H

#include <asm/kmap_types.h>
#include <crypto/algapi.h>
#include <linux/hardirq.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>

static inline enum km_type crypto_kmap_type(int out)
{
	enum km_type type;

	if (in_softirq())
		type = out * (KM_SOFTIRQ1 - KM_SOFTIRQ0) + KM_SOFTIRQ0;
	else
		type = out * (KM_USER1 - KM_USER0) + KM_USER0;

	return type;
}

static inline void *crypto_kmap(struct page *page, int out)
{
	return kmap_atomic(page, crypto_kmap_type(out));
}

static inline void crypto_kunmap(void *vaddr, int out)
{
	kunmap_atomic(vaddr, crypto_kmap_type(out));
}

static inline void crypto_yield(u32 flags)
{
	if (flags & CRYPTO_TFM_REQ_MAY_SLEEP)
		cond_resched();
}

static inline void scatterwalk_sg_chain(struct scatterlist *sg1, int num,
					struct scatterlist *sg2)
{
	sg_set_page(&sg1[num - 1], (void *)sg2, 0, 0);
	sg1[num - 1].page_link &= ~0x02;
}

static inline struct scatterlist *scatterwalk_sg_next(struct scatterlist *sg)
{
	if (sg_is_last(sg))
		return NULL;

	return (++sg)->length ? sg : (void *)sg_page(sg);
}

static inline unsigned long scatterwalk_samebuf(struct scatter_walk *walk_in,
						struct scatter_walk *walk_out)
{
	return !(((sg_page(walk_in->sg) - sg_page(walk_out->sg)) << PAGE_SHIFT) +
		 (int)(walk_in->offset - walk_out->offset));
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

static inline void scatterwalk_unmap(void *vaddr, int out)
{
	crypto_kunmap(vaddr, out);
}

void scatterwalk_start(struct scatter_walk *walk, struct scatterlist *sg);
void scatterwalk_copychunks(void *buf, struct scatter_walk *walk,
			    size_t nbytes, int out);
void *scatterwalk_map(struct scatter_walk *walk, int out);
void scatterwalk_done(struct scatter_walk *walk, int out, int more);

void scatterwalk_map_and_copy(void *buf, struct scatterlist *sg,
			      unsigned int start, unsigned int nbytes, int out);

#endif  /* _CRYPTO_SCATTERWALK_H */
