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
