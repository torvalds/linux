/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PAGE_ACTOR_H
#define PAGE_ACTOR_H
/*
 * Copyright (c) 2013
 * Phillip Lougher <phillip@squashfs.org.uk>
 */

#ifndef CONFIG_SQUASHFS_FILE_DIRECT
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

	actor->length = length ? : pages * PAGE_SIZE;
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
#else
struct squashfs_page_actor {
	union {
		void		**buffer;
		struct page	**page;
	};
	void	*pageaddr;
	void    *(*squashfs_first_page)(struct squashfs_page_actor *);
	void    *(*squashfs_next_page)(struct squashfs_page_actor *);
	void    (*squashfs_finish_page)(struct squashfs_page_actor *);
	int	pages;
	int	length;
	int	next_page;
};

extern struct squashfs_page_actor *squashfs_page_actor_init(void **, int, int);
extern struct squashfs_page_actor *squashfs_page_actor_init_special(struct page
							 **, int, int);
static inline void *squashfs_first_page(struct squashfs_page_actor *actor)
{
	return actor->squashfs_first_page(actor);
}
static inline void *squashfs_next_page(struct squashfs_page_actor *actor)
{
	return actor->squashfs_next_page(actor);
}
static inline void squashfs_finish_page(struct squashfs_page_actor *actor)
{
	actor->squashfs_finish_page(actor);
}
#endif
#endif
