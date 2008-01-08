/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *
 *  EMU10K1 memory page allocation (PTB area)
 *
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
 *
 */

#include <linux/pci.h>
#include <linux/time.h>
#include <linux/mutex.h>

#include <sound/core.h>
#include <sound/emu10k1.h>

/* page arguments of these two macros are Emu page (4096 bytes), not like
 * aligned pages in others
 */
#define __set_ptb_entry(emu,page,addr) \
	(((u32 *)(emu)->ptb_pages.area)[page] = cpu_to_le32(((addr) << 1) | (page)))

#define UNIT_PAGES		(PAGE_SIZE / EMUPAGESIZE)
#define MAX_ALIGN_PAGES		(MAXPAGES / UNIT_PAGES)
/* get aligned page from offset address */
#define get_aligned_page(offset)	((offset) >> PAGE_SHIFT)
/* get offset address from aligned page */
#define aligned_page_offset(page)	((page) << PAGE_SHIFT)

#if PAGE_SIZE == 4096
/* page size == EMUPAGESIZE */
/* fill PTB entrie(s) corresponding to page with addr */
#define set_ptb_entry(emu,page,addr)	__set_ptb_entry(emu,page,addr)
/* fill PTB entrie(s) corresponding to page with silence pointer */
#define set_silent_ptb(emu,page)	__set_ptb_entry(emu,page,emu->silent_page.addr)
#else
/* fill PTB entries -- we need to fill UNIT_PAGES entries */
static inline void set_ptb_entry(struct snd_emu10k1 *emu, int page, dma_addr_t addr)
{
	int i;
	page *= UNIT_PAGES;
	for (i = 0; i < UNIT_PAGES; i++, page++) {
		__set_ptb_entry(emu, page, addr);
		addr += EMUPAGESIZE;
	}
}
static inline void set_silent_ptb(struct snd_emu10k1 *emu, int page)
{
	int i;
	page *= UNIT_PAGES;
	for (i = 0; i < UNIT_PAGES; i++, page++)
		/* do not increment ptr */
		__set_ptb_entry(emu, page, emu->silent_page.addr);
}
#endif /* PAGE_SIZE */


/*
 */
static int synth_alloc_pages(struct snd_emu10k1 *hw, struct snd_emu10k1_memblk *blk);
static int synth_free_pages(struct snd_emu10k1 *hw, struct snd_emu10k1_memblk *blk);

#define get_emu10k1_memblk(l,member)	list_entry(l, struct snd_emu10k1_memblk, member)


/* initialize emu10k1 part */
static void emu10k1_memblk_init(struct snd_emu10k1_memblk *blk)
{
	blk->mapped_page = -1;
	INIT_LIST_HEAD(&blk->mapped_link);
	INIT_LIST_HEAD(&blk->mapped_order_link);
	blk->map_locked = 0;

	blk->first_page = get_aligned_page(blk->mem.offset);
	blk->last_page = get_aligned_page(blk->mem.offset + blk->mem.size - 1);
	blk->pages = blk->last_page - blk->first_page + 1;
}

/*
 * search empty region on PTB with the given size
 *
 * if an empty region is found, return the page and store the next mapped block
 * in nextp
 * if not found, return a negative error code.
 */
static int search_empty_map_area(struct snd_emu10k1 *emu, int npages, struct list_head **nextp)
{
	int page = 0, found_page = -ENOMEM;
	int max_size = npages;
	int size;
	struct list_head *candidate = &emu->mapped_link_head;
	struct list_head *pos;

	list_for_each (pos, &emu->mapped_link_head) {
		struct snd_emu10k1_memblk *blk = get_emu10k1_memblk(pos, mapped_link);
		snd_assert(blk->mapped_page >= 0, continue);
		size = blk->mapped_page - page;
		if (size == npages) {
			*nextp = pos;
			return page;
		}
		else if (size > max_size) {
			/* we look for the maximum empty hole */
			max_size = size;
			candidate = pos;
			found_page = page;
		}
		page = blk->mapped_page + blk->pages;
	}
	size = MAX_ALIGN_PAGES - page;
	if (size >= max_size) {
		*nextp = pos;
		return page;
	}
	*nextp = candidate;
	return found_page;
}

/*
 * map a memory block onto emu10k1's PTB
 *
 * call with memblk_lock held
 */
static int map_memblk(struct snd_emu10k1 *emu, struct snd_emu10k1_memblk *blk)
{
	int page, pg;
	struct list_head *next;

	page = search_empty_map_area(emu, blk->pages, &next);
	if (page < 0) /* not found */
		return page;
	/* insert this block in the proper position of mapped list */
	list_add_tail(&blk->mapped_link, next);
	/* append this as a newest block in order list */
	list_add_tail(&blk->mapped_order_link, &emu->mapped_order_link_head);
	blk->mapped_page = page;
	/* fill PTB */
	for (pg = blk->first_page; pg <= blk->last_page; pg++) {
		set_ptb_entry(emu, page, emu->page_addr_table[pg]);
		page++;
	}
	return 0;
}

/*
 * unmap the block
 * return the size of resultant empty pages
 *
 * call with memblk_lock held
 */
static int unmap_memblk(struct snd_emu10k1 *emu, struct snd_emu10k1_memblk *blk)
{
	int start_page, end_page, mpage, pg;
	struct list_head *p;
	struct snd_emu10k1_memblk *q;

	/* calculate the expected size of empty region */
	if ((p = blk->mapped_link.prev) != &emu->mapped_link_head) {
		q = get_emu10k1_memblk(p, mapped_link);
		start_page = q->mapped_page + q->pages;
	} else
		start_page = 0;
	if ((p = blk->mapped_link.next) != &emu->mapped_link_head) {
		q = get_emu10k1_memblk(p, mapped_link);
		end_page = q->mapped_page;
	} else
		end_page = MAX_ALIGN_PAGES;

	/* remove links */
	list_del(&blk->mapped_link);
	list_del(&blk->mapped_order_link);
	/* clear PTB */
	mpage = blk->mapped_page;
	for (pg = blk->first_page; pg <= blk->last_page; pg++) {
		set_silent_ptb(emu, mpage);
		mpage++;
	}
	blk->mapped_page = -1;
	return end_page - start_page; /* return the new empty size */
}

/*
 * search empty pages with the given size, and create a memory block
 *
 * unlike synth_alloc the memory block is aligned to the page start
 */
static struct snd_emu10k1_memblk *
search_empty(struct snd_emu10k1 *emu, int size)
{
	struct list_head *p;
	struct snd_emu10k1_memblk *blk;
	int page, psize;

	psize = get_aligned_page(size + PAGE_SIZE -1);
	page = 0;
	list_for_each(p, &emu->memhdr->block) {
		blk = get_emu10k1_memblk(p, mem.list);
		if (page + psize <= blk->first_page)
			goto __found_pages;
		page = blk->last_page + 1;
	}
	if (page + psize > emu->max_cache_pages)
		return NULL;

__found_pages:
	/* create a new memory block */
	blk = (struct snd_emu10k1_memblk *)__snd_util_memblk_new(emu->memhdr, psize << PAGE_SHIFT, p->prev);
	if (blk == NULL)
		return NULL;
	blk->mem.offset = aligned_page_offset(page); /* set aligned offset */
	emu10k1_memblk_init(blk);
	return blk;
}


/*
 * check if the given pointer is valid for pages
 */
static int is_valid_page(struct snd_emu10k1 *emu, dma_addr_t addr)
{
	if (addr & ~emu->dma_mask) {
		snd_printk(KERN_ERR "max memory size is 0x%lx (addr = 0x%lx)!!\n", emu->dma_mask, (unsigned long)addr);
		return 0;
	}
	if (addr & (EMUPAGESIZE-1)) {
		snd_printk(KERN_ERR "page is not aligned\n");
		return 0;
	}
	return 1;
}

/*
 * map the given memory block on PTB.
 * if the block is already mapped, update the link order.
 * if no empty pages are found, tries to release unsed memory blocks
 * and retry the mapping.
 */
int snd_emu10k1_memblk_map(struct snd_emu10k1 *emu, struct snd_emu10k1_memblk *blk)
{
	int err;
	int size;
	struct list_head *p, *nextp;
	struct snd_emu10k1_memblk *deleted;
	unsigned long flags;

	spin_lock_irqsave(&emu->memblk_lock, flags);
	if (blk->mapped_page >= 0) {
		/* update order link */
		list_del(&blk->mapped_order_link);
		list_add_tail(&blk->mapped_order_link, &emu->mapped_order_link_head);
		spin_unlock_irqrestore(&emu->memblk_lock, flags);
		return 0;
	}
	if ((err = map_memblk(emu, blk)) < 0) {
		/* no enough page - try to unmap some blocks */
		/* starting from the oldest block */
		p = emu->mapped_order_link_head.next;
		for (; p != &emu->mapped_order_link_head; p = nextp) {
			nextp = p->next;
			deleted = get_emu10k1_memblk(p, mapped_order_link);
			if (deleted->map_locked)
				continue;
			size = unmap_memblk(emu, deleted);
			if (size >= blk->pages) {
				/* ok the empty region is enough large */
				err = map_memblk(emu, blk);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&emu->memblk_lock, flags);
	return err;
}

EXPORT_SYMBOL(snd_emu10k1_memblk_map);

/*
 * page allocation for DMA
 */
struct snd_util_memblk *
snd_emu10k1_alloc_pages(struct snd_emu10k1 *emu, struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_sg_buf *sgbuf = snd_pcm_substream_sgbuf(substream);
	struct snd_util_memhdr *hdr;
	struct snd_emu10k1_memblk *blk;
	int page, err, idx;

	snd_assert(emu, return NULL);
	snd_assert(runtime->dma_bytes > 0 && runtime->dma_bytes < MAXPAGES * EMUPAGESIZE, return NULL);
	hdr = emu->memhdr;
	snd_assert(hdr, return NULL);

	mutex_lock(&hdr->block_mutex);
	blk = search_empty(emu, runtime->dma_bytes);
	if (blk == NULL) {
		mutex_unlock(&hdr->block_mutex);
		return NULL;
	}
	/* fill buffer addresses but pointers are not stored so that
	 * snd_free_pci_page() is not called in in synth_free()
	 */
	idx = 0;
	for (page = blk->first_page; page <= blk->last_page; page++, idx++) {
		dma_addr_t addr;
#ifdef CONFIG_SND_DEBUG
		if (idx >= sgbuf->pages) {
			printk(KERN_ERR "emu: pages overflow! (%d-%d) for %d\n",
			       blk->first_page, blk->last_page, sgbuf->pages);
			mutex_unlock(&hdr->block_mutex);
			return NULL;
		}
#endif
		addr = sgbuf->table[idx].addr;
		if (! is_valid_page(emu, addr)) {
			printk(KERN_ERR "emu: failure page = %d\n", idx);
			mutex_unlock(&hdr->block_mutex);
			return NULL;
		}
		emu->page_addr_table[page] = addr;
		emu->page_ptr_table[page] = NULL;
	}

	/* set PTB entries */
	blk->map_locked = 1; /* do not unmap this block! */
	err = snd_emu10k1_memblk_map(emu, blk);
	if (err < 0) {
		__snd_util_mem_free(hdr, (struct snd_util_memblk *)blk);
		mutex_unlock(&hdr->block_mutex);
		return NULL;
	}
	mutex_unlock(&hdr->block_mutex);
	return (struct snd_util_memblk *)blk;
}


/*
 * release DMA buffer from page table
 */
int snd_emu10k1_free_pages(struct snd_emu10k1 *emu, struct snd_util_memblk *blk)
{
	snd_assert(emu && blk, return -EINVAL);
	return snd_emu10k1_synth_free(emu, blk);
}


/*
 * memory allocation using multiple pages (for synth)
 * Unlike the DMA allocation above, non-contiguous pages are assined.
 */

/*
 * allocate a synth sample area
 */
struct snd_util_memblk *
snd_emu10k1_synth_alloc(struct snd_emu10k1 *hw, unsigned int size)
{
	struct snd_emu10k1_memblk *blk;
	struct snd_util_memhdr *hdr = hw->memhdr; 

	mutex_lock(&hdr->block_mutex);
	blk = (struct snd_emu10k1_memblk *)__snd_util_mem_alloc(hdr, size);
	if (blk == NULL) {
		mutex_unlock(&hdr->block_mutex);
		return NULL;
	}
	if (synth_alloc_pages(hw, blk)) {
		__snd_util_mem_free(hdr, (struct snd_util_memblk *)blk);
		mutex_unlock(&hdr->block_mutex);
		return NULL;
	}
	snd_emu10k1_memblk_map(hw, blk);
	mutex_unlock(&hdr->block_mutex);
	return (struct snd_util_memblk *)blk;
}

EXPORT_SYMBOL(snd_emu10k1_synth_alloc);

/*
 * free a synth sample area
 */
int
snd_emu10k1_synth_free(struct snd_emu10k1 *emu, struct snd_util_memblk *memblk)
{
	struct snd_util_memhdr *hdr = emu->memhdr; 
	struct snd_emu10k1_memblk *blk = (struct snd_emu10k1_memblk *)memblk;
	unsigned long flags;

	mutex_lock(&hdr->block_mutex);
	spin_lock_irqsave(&emu->memblk_lock, flags);
	if (blk->mapped_page >= 0)
		unmap_memblk(emu, blk);
	spin_unlock_irqrestore(&emu->memblk_lock, flags);
	synth_free_pages(emu, blk);
	 __snd_util_mem_free(hdr, memblk);
	mutex_unlock(&hdr->block_mutex);
	return 0;
}

EXPORT_SYMBOL(snd_emu10k1_synth_free);

/* check new allocation range */
static void get_single_page_range(struct snd_util_memhdr *hdr,
				  struct snd_emu10k1_memblk *blk,
				  int *first_page_ret, int *last_page_ret)
{
	struct list_head *p;
	struct snd_emu10k1_memblk *q;
	int first_page, last_page;
	first_page = blk->first_page;
	if ((p = blk->mem.list.prev) != &hdr->block) {
		q = get_emu10k1_memblk(p, mem.list);
		if (q->last_page == first_page)
			first_page++;  /* first page was already allocated */
	}
	last_page = blk->last_page;
	if ((p = blk->mem.list.next) != &hdr->block) {
		q = get_emu10k1_memblk(p, mem.list);
		if (q->first_page == last_page)
			last_page--; /* last page was already allocated */
	}
	*first_page_ret = first_page;
	*last_page_ret = last_page;
}

/*
 * allocate kernel pages
 */
static int synth_alloc_pages(struct snd_emu10k1 *emu, struct snd_emu10k1_memblk *blk)
{
	int page, first_page, last_page;
	struct snd_dma_buffer dmab;

	emu10k1_memblk_init(blk);
	get_single_page_range(emu->memhdr, blk, &first_page, &last_page);
	/* allocate kernel pages */
	for (page = first_page; page <= last_page; page++) {
		if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(emu->pci),
					PAGE_SIZE, &dmab) < 0)
			goto __fail;
		if (! is_valid_page(emu, dmab.addr)) {
			snd_dma_free_pages(&dmab);
			goto __fail;
		}
		emu->page_addr_table[page] = dmab.addr;
		emu->page_ptr_table[page] = dmab.area;
	}
	return 0;

__fail:
	/* release allocated pages */
	last_page = page - 1;
	for (page = first_page; page <= last_page; page++) {
		dmab.area = emu->page_ptr_table[page];
		dmab.addr = emu->page_addr_table[page];
		dmab.bytes = PAGE_SIZE;
		snd_dma_free_pages(&dmab);
		emu->page_addr_table[page] = 0;
		emu->page_ptr_table[page] = NULL;
	}

	return -ENOMEM;
}

/*
 * free pages
 */
static int synth_free_pages(struct snd_emu10k1 *emu, struct snd_emu10k1_memblk *blk)
{
	int page, first_page, last_page;
	struct snd_dma_buffer dmab;

	get_single_page_range(emu->memhdr, blk, &first_page, &last_page);
	dmab.dev.type = SNDRV_DMA_TYPE_DEV;
	dmab.dev.dev = snd_dma_pci_data(emu->pci);
	for (page = first_page; page <= last_page; page++) {
		if (emu->page_ptr_table[page] == NULL)
			continue;
		dmab.area = emu->page_ptr_table[page];
		dmab.addr = emu->page_addr_table[page];
		dmab.bytes = PAGE_SIZE;
		snd_dma_free_pages(&dmab);
		emu->page_addr_table[page] = 0;
		emu->page_ptr_table[page] = NULL;
	}

	return 0;
}

/* calculate buffer pointer from offset address */
static inline void *offset_ptr(struct snd_emu10k1 *emu, int page, int offset)
{
	char *ptr;
	snd_assert(page >= 0 && page < emu->max_cache_pages, return NULL);
	ptr = emu->page_ptr_table[page];
	if (! ptr) {
		printk(KERN_ERR "emu10k1: access to NULL ptr: page = %d\n", page);
		return NULL;
	}
	ptr += offset & (PAGE_SIZE - 1);
	return (void*)ptr;
}

/*
 * bzero(blk + offset, size)
 */
int snd_emu10k1_synth_bzero(struct snd_emu10k1 *emu, struct snd_util_memblk *blk,
			    int offset, int size)
{
	int page, nextofs, end_offset, temp, temp1;
	void *ptr;
	struct snd_emu10k1_memblk *p = (struct snd_emu10k1_memblk *)blk;

	offset += blk->offset & (PAGE_SIZE - 1);
	end_offset = offset + size;
	page = get_aligned_page(offset);
	do {
		nextofs = aligned_page_offset(page + 1);
		temp = nextofs - offset;
		temp1 = end_offset - offset;
		if (temp1 < temp)
			temp = temp1;
		ptr = offset_ptr(emu, page + p->first_page, offset);
		if (ptr)
			memset(ptr, 0, temp);
		offset = nextofs;
		page++;
	} while (offset < end_offset);
	return 0;
}

EXPORT_SYMBOL(snd_emu10k1_synth_bzero);

/*
 * copy_from_user(blk + offset, data, size)
 */
int snd_emu10k1_synth_copy_from_user(struct snd_emu10k1 *emu, struct snd_util_memblk *blk,
				     int offset, const char __user *data, int size)
{
	int page, nextofs, end_offset, temp, temp1;
	void *ptr;
	struct snd_emu10k1_memblk *p = (struct snd_emu10k1_memblk *)blk;

	offset += blk->offset & (PAGE_SIZE - 1);
	end_offset = offset + size;
	page = get_aligned_page(offset);
	do {
		nextofs = aligned_page_offset(page + 1);
		temp = nextofs - offset;
		temp1 = end_offset - offset;
		if (temp1 < temp)
			temp = temp1;
		ptr = offset_ptr(emu, page + p->first_page, offset);
		if (ptr && copy_from_user(ptr, data, temp))
			return -EFAULT;
		offset = nextofs;
		data += temp;
		page++;
	} while (offset < end_offset);
	return 0;
}

EXPORT_SYMBOL(snd_emu10k1_synth_copy_from_user);
