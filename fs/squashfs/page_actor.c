// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013
 * Phillip Lougher <phillip@squashfs.org.uk>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
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
	actor->squashfs_first_page = cache_first_page;
	actor->squashfs_next_page = cache_next_page;
	actor->squashfs_finish_page = cache_finish_page;
	return actor;
}

/* Implementation of page_actor for decompressing directly into page cache. */
static void *direct_first_page(struct squashfs_page_actor *actor)
{
	actor->next_page = 1;
	return actor->pageaddr = kmap_atomic(actor->page[0]);
}

static void *direct_next_page(struct squashfs_page_actor *actor)
{
	if (actor->pageaddr)
		kunmap_atomic(actor->pageaddr);

	return actor->pageaddr = actor->next_page == actor->pages ? NULL :
		kmap_atomic(actor->page[actor->next_page++]);
}

static void direct_finish_page(struct squashfs_page_actor *actor)
{
	if (actor->pageaddr)
		kunmap_atomic(actor->pageaddr);
}

struct squashfs_page_actor *squashfs_page_actor_init_special(struct page **page,
	int pages, int length)
{
	struct squashfs_page_actor *actor = kmalloc(sizeof(*actor), GFP_KERNEL);

	if (actor == NULL)
		return NULL;

	actor->length = length ? : pages * PAGE_SIZE;
	actor->page = page;
	actor->pages = pages;
	actor->next_page = 0;
	actor->pageaddr = NULL;
	actor->squashfs_first_page = direct_first_page;
	actor->squashfs_next_page = direct_next_page;
	actor->squashfs_finish_page = direct_finish_page;
	return actor;
}
