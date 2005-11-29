/*
 * Basic general purpose allocator for managing special purpose memory
 * not managed by the regular kmalloc/kfree interface.
 * Uses for this includes on-device special memory, uncached memory
 * etc.
 *
 * This code is based on the buddy allocator found in the sym53c8xx_2
 * driver Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>,
 * and adapted for general purpose use.
 *
 * Copyright 2005 (C) Jes Sorensen <jes@trained-monkey.org>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/genalloc.h>

#include <asm/page.h>


struct gen_pool *gen_pool_create(int nr_chunks, int max_chunk_shift,
				 unsigned long (*fp)(struct gen_pool *),
				 unsigned long data)
{
	struct gen_pool *poolp;
	unsigned long tmp;
	int i;

	/*
	 * This is really an arbitrary limit, +10 is enough for
	 * IA64_GRANULE_SHIFT, aka 16MB. If anyone needs a large limit
	 * this can be increased without problems.
	 */
	if ((max_chunk_shift > (PAGE_SHIFT + 10)) ||
	    ((max_chunk_shift < ALLOC_MIN_SHIFT) && max_chunk_shift))
		return NULL;

	if (!max_chunk_shift)
		max_chunk_shift = PAGE_SHIFT;

	poolp = kmalloc(sizeof(struct gen_pool), GFP_KERNEL);
	if (!poolp)
		return NULL;
	memset(poolp, 0, sizeof(struct gen_pool));
	poolp->h = kmalloc(sizeof(struct gen_pool_link) *
			   (max_chunk_shift - ALLOC_MIN_SHIFT + 1),
			   GFP_KERNEL);
	if (!poolp->h) {
		printk(KERN_WARNING "gen_pool_alloc() failed to allocate\n");
		kfree(poolp);
		return NULL;
	}
	memset(poolp->h, 0, sizeof(struct gen_pool_link) *
	       (max_chunk_shift - ALLOC_MIN_SHIFT + 1));

	spin_lock_init(&poolp->lock);
	poolp->get_new_chunk = fp;
	poolp->max_chunk_shift = max_chunk_shift;
	poolp->private = data;

	for (i = 0; i < nr_chunks; i++) {
		tmp = poolp->get_new_chunk(poolp);
		printk(KERN_INFO "allocated %lx\n", tmp);
		if (!tmp)
			break;
		gen_pool_free(poolp, tmp, (1 << poolp->max_chunk_shift));
	}

	return poolp;
}
EXPORT_SYMBOL(gen_pool_create);


/*
 *  Simple power of two buddy-like generic allocator.
 *  Provides naturally aligned memory chunks.
 */
unsigned long gen_pool_alloc(struct gen_pool *poolp, int size)
{
	int j, i, s, max_chunk_size;
	unsigned long a, flags;
	struct gen_pool_link *h = poolp->h;

	max_chunk_size = 1 << poolp->max_chunk_shift;

	if (size > max_chunk_size)
		return 0;

	size = max(size, 1 << ALLOC_MIN_SHIFT);
	i = fls(size - 1);
	s = 1 << i;
	j = i -= ALLOC_MIN_SHIFT;

	spin_lock_irqsave(&poolp->lock, flags);
	while (!h[j].next) {
		if (s == max_chunk_size) {
			struct gen_pool_link *ptr;
			spin_unlock_irqrestore(&poolp->lock, flags);
			ptr = (struct gen_pool_link *)poolp->get_new_chunk(poolp);
			spin_lock_irqsave(&poolp->lock, flags);
			h[j].next = ptr;
			if (h[j].next)
				h[j].next->next = NULL;
			break;
		}
		j++;
		s <<= 1;
	}
	a = (unsigned long) h[j].next;
	if (a) {
		h[j].next = h[j].next->next;
		/*
		 * This should be split into a seperate function doing
		 * the chunk split in order to support custom
		 * handling memory not physically accessible by host
		 */
		while (j > i) {
			j -= 1;
			s >>= 1;
			h[j].next = (struct gen_pool_link *) (a + s);
			h[j].next->next = NULL;
		}
	}
	spin_unlock_irqrestore(&poolp->lock, flags);
	return a;
}
EXPORT_SYMBOL(gen_pool_alloc);


/*
 *  Counter-part of the generic allocator.
 */
void gen_pool_free(struct gen_pool *poolp, unsigned long ptr, int size)
{
	struct gen_pool_link *q;
	struct gen_pool_link *h = poolp->h;
	unsigned long a, b, flags;
	int i, s, max_chunk_size;

	max_chunk_size = 1 << poolp->max_chunk_shift;

	if (size > max_chunk_size)
		return;

	size = max(size, 1 << ALLOC_MIN_SHIFT);
	i = fls(size - 1);
	s = 1 << i;
	i -= ALLOC_MIN_SHIFT;

	a = ptr;

	spin_lock_irqsave(&poolp->lock, flags);
	while (1) {
		if (s == max_chunk_size) {
			((struct gen_pool_link *)a)->next = h[i].next;
			h[i].next = (struct gen_pool_link *)a;
			break;
		}
		b = a ^ s;
		q = &h[i];

		while (q->next && q->next != (struct gen_pool_link *)b)
			q = q->next;

		if (!q->next) {
			((struct gen_pool_link *)a)->next = h[i].next;
			h[i].next = (struct gen_pool_link *)a;
			break;
		}
		q->next = q->next->next;
		a = a & b;
		s <<= 1;
		i++;
	}
	spin_unlock_irqrestore(&poolp->lock, flags);
}
EXPORT_SYMBOL(gen_pool_free);
