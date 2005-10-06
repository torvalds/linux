/*
 *  Copyright (C) 2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Generic memory management routines for soundcard memory allocation
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/util_mem.h>

MODULE_AUTHOR("Takashi Iwai");
MODULE_DESCRIPTION("Generic memory management routines for soundcard memory allocation");
MODULE_LICENSE("GPL");

#define get_memblk(p)	list_entry(p, snd_util_memblk_t, list)

/*
 * create a new memory manager
 */
snd_util_memhdr_t *
snd_util_memhdr_new(int memsize)
{
	snd_util_memhdr_t *hdr;

	hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
	if (hdr == NULL)
		return NULL;
	hdr->size = memsize;
	init_MUTEX(&hdr->block_mutex);
	INIT_LIST_HEAD(&hdr->block);

	return hdr;
}

/*
 * free a memory manager
 */
void snd_util_memhdr_free(snd_util_memhdr_t *hdr)
{
	struct list_head *p;

	snd_assert(hdr != NULL, return);
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
snd_util_memblk_t *
__snd_util_mem_alloc(snd_util_memhdr_t *hdr, int size)
{
	snd_util_memblk_t *blk;
	snd_util_unit_t units, prev_offset;
	struct list_head *p;

	snd_assert(hdr != NULL, return NULL);
	snd_assert(size > 0, return NULL);

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
snd_util_memblk_t *
__snd_util_memblk_new(snd_util_memhdr_t *hdr, snd_util_unit_t units,
		      struct list_head *prev)
{
	snd_util_memblk_t *blk;

	blk = kmalloc(sizeof(snd_util_memblk_t) + hdr->block_extra_size, GFP_KERNEL);
	if (blk == NULL)
		return NULL;

	if (! prev || prev == &hdr->block)
		blk->offset = 0;
	else {
		snd_util_memblk_t *p = get_memblk(prev);
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
snd_util_memblk_t *
snd_util_mem_alloc(snd_util_memhdr_t *hdr, int size)
{
	snd_util_memblk_t *blk;
	down(&hdr->block_mutex);
	blk = __snd_util_mem_alloc(hdr, size);
	up(&hdr->block_mutex);
	return blk;
}


/*
 * remove the block from linked-list and free resource
 * (without mutex)
 */
void
__snd_util_mem_free(snd_util_memhdr_t *hdr, snd_util_memblk_t *blk)
{
	list_del(&blk->list);
	hdr->nblocks--;
	hdr->used -= blk->size;
	kfree(blk);
}

/*
 * free a memory block (with mutex)
 */
int snd_util_mem_free(snd_util_memhdr_t *hdr, snd_util_memblk_t *blk)
{
	snd_assert(hdr && blk, return -EINVAL);

	down(&hdr->block_mutex);
	__snd_util_mem_free(hdr, blk);
	up(&hdr->block_mutex);
	return 0;
}

/*
 * return available memory size
 */
int snd_util_mem_avail(snd_util_memhdr_t *hdr)
{
	unsigned int size;
	down(&hdr->block_mutex);
	size = hdr->size - hdr->used;
	up(&hdr->block_mutex);
	return size;
}


EXPORT_SYMBOL(snd_util_memhdr_new);
EXPORT_SYMBOL(snd_util_memhdr_free);
EXPORT_SYMBOL(snd_util_mem_alloc);
EXPORT_SYMBOL(snd_util_mem_free);
EXPORT_SYMBOL(snd_util_mem_avail);
EXPORT_SYMBOL(__snd_util_mem_alloc);
EXPORT_SYMBOL(__snd_util_mem_free);
EXPORT_SYMBOL(__snd_util_memblk_new);

/*
 *  INIT part
 */

static int __init alsa_util_mem_init(void)
{
	return 0;
}

static void __exit alsa_util_mem_exit(void)
{
}

module_init(alsa_util_mem_init)
module_exit(alsa_util_mem_exit)
