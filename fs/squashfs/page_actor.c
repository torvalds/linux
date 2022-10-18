// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013
 * Phillip Lougher <phillip@squashfs.org.uk>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include "squashfs_fs_sb.h"
#include "decompressor.h"
#include "page_actor.h"

/*
 * This file contains implementations of page_actor for decompressing into
 * an intermediate buffer, and for decompressing directly into the
 * page cache.
 *
 * Calling code should avoid sleeping between calls to squashfs_first_page()
 * and squashfs_finish_page().
 */

/* Implementation of page_actor for decompressing into intermediate buffer */
static void *cache_first_page(struct squashfs_page_actor *actor)
{
	actor->next_page = 1;
	return actor->buffer[0];
}

static void *cache_next_page(struct squashfs_page_actor *actor)
{
	if (actor->next_page == actor->pages)
		return NULL;

	return actor->buffer[actor->next_page++];
}

static void cache_finish_page(struct squashfs_page_actor *actor)
{
	/* empty */
}

struct squashfs_page_actor *squashfs_page_actor_init(void **buffer,
	int pages, int length)
{
	struct squashfs_page_actor *actor = kmalloc(sizeof(*actor), GFP_KERNEL);

	if (actor == NULL)
		return NULL;

	actor->length = length ? : pages * PAGE_SIZE;
	actor->buffer = buffer;
	actor->pages = pages;
	actor->next_page = 0;
	actor->tmp_buffer = NULL;
	actor->squashfs_first_page = cache_first_page;
	actor->squashfs_next_page = cache_next_page;
	actor->squashfs_finish_page = cache_finish_page;
	return actor;
}

/* Implementation of page_actor for decompressing directly into page cache. */
static void *handle_next_page(struct squashfs_page_actor *actor)
{
	int max_pages = (actor->length + PAGE_SIZE - 1) >> PAGE_SHIFT;

	if (actor->returned_pages == max_pages)
		return NULL;

	if ((actor->next_page == actor->pages) ||
			(actor->next_index != actor->page[actor->next_page]->index)) {
		actor->next_index++;
		actor->returned_pages++;
		return actor->alloc_buffer ? actor->tmp_buffer : ERR_PTR(-ENOMEM);
	}

	actor->next_index++;
	actor->returned_pages++;
	return actor->pageaddr = kmap_local_page(actor->page[actor->next_page++]);
}

static void *direct_first_page(struct squashfs_page_actor *actor)
{
	return handle_next_page(actor);
}

static void *direct_next_page(struct squashfs_page_actor *actor)
{
	if (actor->pageaddr) {
		kunmap_local(actor->pageaddr);
		actor->pageaddr = NULL;
	}

	return handle_next_page(actor);
}

static void direct_finish_page(struct squashfs_page_actor *actor)
{
	if (actor->pageaddr)
		kunmap_local(actor->pageaddr);
}

struct squashfs_page_actor *squashfs_page_actor_init_special(struct squashfs_sb_info *msblk,
	struct page **page, int pages, int length)
{
	struct squashfs_page_actor *actor = kmalloc(sizeof(*actor), GFP_KERNEL);

	if (actor == NULL)
		return NULL;

	if (msblk->decompressor->alloc_buffer) {
		actor->tmp_buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);

		if (actor->tmp_buffer == NULL) {
			kfree(actor);
			return NULL;
		}
	} else
		actor->tmp_buffer = NULL;

	actor->length = length ? : pages * PAGE_SIZE;
	actor->page = page;
	actor->pages = pages;
	actor->next_page = 0;
	actor->returned_pages = 0;
	actor->next_index = page[0]->index & ~((1 << (msblk->block_log - PAGE_SHIFT)) - 1);
	actor->pageaddr = NULL;
	actor->alloc_buffer = msblk->decompressor->alloc_buffer;
	actor->squashfs_first_page = direct_first_page;
	actor->squashfs_next_page = direct_next_page;
	actor->squashfs_finish_page = direct_finish_page;
	return actor;
}
