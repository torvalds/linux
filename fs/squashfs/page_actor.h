/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PAGE_ACTOR_H
#define PAGE_ACTOR_H
/*
 * Copyright (c) 2013
 * Phillip Lougher <phillip@squashfs.org.uk>
 */

struct squashfs_page_actor {
	union {
		void		**buffer;
		struct page	**page;
	};
	void	*pageaddr;
	void	*tmp_buffer;
	void    *(*squashfs_first_page)(struct squashfs_page_actor *);
	void    *(*squashfs_next_page)(struct squashfs_page_actor *);
	void    (*squashfs_finish_page)(struct squashfs_page_actor *);
	struct page *last_page;
	int	pages;
	int	length;
	int	next_page;
	int	alloc_buffer;
	int	returned_pages;
	pgoff_t	next_index;
};

extern struct squashfs_page_actor *squashfs_page_actor_init(void **buffer,
				int pages, int length);
extern struct squashfs_page_actor *squashfs_page_actor_init_special(
				struct squashfs_sb_info *msblk,
				struct page **page, int pages, int length,
				loff_t start_index);
static inline struct page *squashfs_page_actor_free(struct squashfs_page_actor *actor)
{
	struct page *last_page = actor->next_page == actor->pages ? actor->last_page : ERR_PTR(-EIO);

	kfree(actor->tmp_buffer);
	kfree(actor);

	return last_page;
}
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
static inline void squashfs_actor_nobuff(struct squashfs_page_actor *actor)
{
	actor->alloc_buffer = 0;
}
#endif
