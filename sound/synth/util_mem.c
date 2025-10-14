// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Generic memory management routines for soundcard memory allocation
 */

#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/util_mem.h>

MODULE_AUTHOR("Takashi Iwai");
MODULE_DESCRIPTION("Generic memory management routines for soundcard memory allocation");
MODULE_LICENSE("GPL");

#define get_memblk(p)	list_entry(p, struct snd_util_memblk, list)

/*
 * create a new memory manager
 */
struct snd_util_memhdr *
snd_util_memhdr_new(int memsize)
{
	struct snd_util_memhdr *hdr;

	hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
	if (hdr == NULL)
		return NULL;
	hdr->size = memsize;
	mutex_init(&hdr->block_mutex);
	INIT_LIST_HEAD(&hdr->block);

	return hdr;
}

/*
 * free a memory manager
 */
void snd_util_memhdr_free(struct snd_util_memhdr *hdr)
{
	struct list_head *p;

	if (!hdr)
		return;
	/* release all blocks */
	while ((p = hdr->block.next) != &hdr->block) {
		list_del(p);
		kfree(get_memblk(p));
	}
	kfree(hdr);
}

/*
 * allocate a memory block (without mutex)
 */
struct snd_util_memblk *
__snd_util_mem_alloc(struct snd_util_memhdr *hdr, int size)
{
	struct snd_util_memblk *blk;
	unsigned int units, prev_offset;
	struct list_head *p;

	if (snd_BUG_ON(!hdr || size <= 0))
		return NULL;

	/* word alignment */
	units = size;
	if (units & 1)
		units++;
	if (units > hdr->size)
		return NULL;

	/* look for empty block */
	prev_offset = 0;
	list_for_each(p, &hdr->block) {
		blk = get_memblk(p);
		if (blk->offset - prev_offset >= units)
			goto __found;
		prev_offset = blk->offset + blk->size;
	}
	if (hdr->size - prev_offset < units)
		return NULL;

__found:
	return __snd_util_memblk_new(hdr, units, p->prev);
}


/*
 * create a new memory block with the given size
 * the block is linked next to prev
 */
struct snd_util_memblk *
__snd_util_memblk_new(struct snd_util_memhdr *hdr, unsigned int units,
		      struct list_head *prev)
{
	struct snd_util_memblk *blk;

	blk = kmalloc(sizeof(struct snd_util_memblk) + hdr->block_extra_size,
		      GFP_KERNEL);
	if (blk == NULL)
		return NULL;

	if (prev == &hdr->block)
		blk->offset = 0;
	else {
		struct snd_util_memblk *p = get_memblk(prev);
		blk->offset = p->offset + p->size;
	}
	blk->size = units;
	list_add(&blk->list, prev);
	hdr->nblocks++;
	hdr->used += units;
	return blk;
}


/*
 * allocate a memory block (with mutex)
 */
struct snd_util_memblk *
snd_util_mem_alloc(struct snd_util_memhdr *hdr, int size)
{
	guard(mutex)(&hdr->block_mutex);
	return __snd_util_mem_alloc(hdr, size);
}


/*
 * remove the block from linked-list and free resource
 * (without mutex)
 */
void
__snd_util_mem_free(struct snd_util_memhdr *hdr, struct snd_util_memblk *blk)
{
	list_del(&blk->list);
	hdr->nblocks--;
	hdr->used -= blk->size;
	kfree(blk);
}

/*
 * free a memory block (with mutex)
 */
int snd_util_mem_free(struct snd_util_memhdr *hdr, struct snd_util_memblk *blk)
{
	if (snd_BUG_ON(!hdr || !blk))
		return -EINVAL;

	guard(mutex)(&hdr->block_mutex);
	__snd_util_mem_free(hdr, blk);
	return 0;
}

/*
 * return available memory size
 */
int snd_util_mem_avail(struct snd_util_memhdr *hdr)
{
	guard(mutex)(&hdr->block_mutex);
	return hdr->size - hdr->used;
}


EXPORT_SYMBOL(snd_util_memhdr_new);
EXPORT_SYMBOL(snd_util_memhdr_free);
EXPORT_SYMBOL(snd_util_mem_alloc);
EXPORT_SYMBOL(snd_util_mem_free);
EXPORT_SYMBOL(snd_util_mem_avail);
EXPORT_SYMBOL(__snd_util_mem_alloc);
EXPORT_SYMBOL(__snd_util_mem_free);
EXPORT_SYMBOL(__snd_util_memblk_new);
