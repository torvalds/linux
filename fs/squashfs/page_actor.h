#ifndef PAGE_ACTOR_H
#define PAGE_ACTOR_H
/*
 * Copyright (c) 2013
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
 * the COPYING file in the top-level directory.
 */

struct squashfs_page_actor {
	void	**page;
	int	pages;
	int	length;
	int	next_page;
};

static inline struct squashfs_page_actor *squashfs_page_actor_init(void **page,
	int pages, int length)
{
	struct squashfs_page_actor *actor = kmalloc(sizeof(*actor), GFP_KERNEL);

	if (actor == NULL)
		return NULL;

	actor->length = length ? : pages * PAGE_CACHE_SIZE;
	actor->page = page;
	actor->pages = pages;
	actor->next_page = 0;
	return actor;
}

static inline void *squashfs_first_page(struct squashfs_page_actor *actor)
{
	actor->next_page = 1;
	return actor->page[0];
}

static inline void *squashfs_next_page(struct squashfs_page_actor *actor)
{
	return actor->next_page == actor->pages ? NULL :
		actor->page[actor->next_page++];
}

static inline void squashfs_finish_page(struct squashfs_page_actor *actor)
{
	/* empty */
}
#endif
