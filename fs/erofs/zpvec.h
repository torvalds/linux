/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 */
#ifndef __EROFS_FS_ZPVEC_H
#define __EROFS_FS_ZPVEC_H

#include "tagptr.h"

/* page type in pagevec for decompress subsystem */
enum z_erofs_page_type {
	/* including Z_EROFS_VLE_PAGE_TAIL_EXCLUSIVE */
	Z_EROFS_PAGE_TYPE_EXCLUSIVE,

	Z_EROFS_VLE_PAGE_TYPE_TAIL_SHARED,

	Z_EROFS_VLE_PAGE_TYPE_HEAD,
	Z_EROFS_VLE_PAGE_TYPE_MAX
};

extern void __compiletime_error("Z_EROFS_PAGE_TYPE_EXCLUSIVE != 0")
	__bad_page_type_exclusive(void);

/* pagevec tagged pointer */
typedef tagptr2_t	erofs_vtptr_t;

/* pagevec collector */
struct z_erofs_pagevec_ctor {
	struct page *curr, *next;
	erofs_vtptr_t *pages;

	unsigned int nr, index;
};

static inline void z_erofs_pagevec_ctor_exit(struct z_erofs_pagevec_ctor *ctor,
					     bool atomic)
{
	if (!ctor->curr)
		return;

	if (atomic)
		kunmap_atomic(ctor->pages);
	else
		kunmap(ctor->curr);
}

static inline struct page *
z_erofs_pagevec_ctor_next_page(struct z_erofs_pagevec_ctor *ctor,
			       unsigned int nr)
{
	unsigned int index;

	/* keep away from occupied pages */
	if (ctor->next)
		return ctor->next;

	for (index = 0; index < nr; ++index) {
		const erofs_vtptr_t t = ctor->pages[index];
		const unsigned int tags = tagptr_unfold_tags(t);

		if (tags == Z_EROFS_PAGE_TYPE_EXCLUSIVE)
			return tagptr_unfold_ptr(t);
	}
	DBG_BUGON(nr >= ctor->nr);
	return NULL;
}

static inline void
z_erofs_pagevec_ctor_pagedown(struct z_erofs_pagevec_ctor *ctor,
			      bool atomic)
{
	struct page *next = z_erofs_pagevec_ctor_next_page(ctor, ctor->nr);

	z_erofs_pagevec_ctor_exit(ctor, atomic);

	ctor->curr = next;
	ctor->next = NULL;
	ctor->pages = atomic ?
		kmap_atomic(ctor->curr) : kmap(ctor->curr);

	ctor->nr = PAGE_SIZE / sizeof(struct page *);
	ctor->index = 0;
}

static inline void z_erofs_pagevec_ctor_init(struct z_erofs_pagevec_ctor *ctor,
					     unsigned int nr,
					     erofs_vtptr_t *pages,
					     unsigned int i)
{
	ctor->nr = nr;
	ctor->curr = ctor->next = NULL;
	ctor->pages = pages;

	if (i >= nr) {
		i -= nr;
		z_erofs_pagevec_ctor_pagedown(ctor, false);
		while (i > ctor->nr) {
			i -= ctor->nr;
			z_erofs_pagevec_ctor_pagedown(ctor, false);
		}
	}
	ctor->next = z_erofs_pagevec_ctor_next_page(ctor, i);
	ctor->index = i;
}

static inline bool z_erofs_pagevec_enqueue(struct z_erofs_pagevec_ctor *ctor,
					   struct page *page,
					   enum z_erofs_page_type type,
					   bool pvec_safereuse)
{
	if (!ctor->next) {
		/* some pages cannot be reused as pvec safely without I/O */
		if (type == Z_EROFS_PAGE_TYPE_EXCLUSIVE && !pvec_safereuse)
			type = Z_EROFS_VLE_PAGE_TYPE_TAIL_SHARED;

		if (type != Z_EROFS_PAGE_TYPE_EXCLUSIVE &&
		    ctor->index + 1 == ctor->nr)
			return false;
	}

	if (ctor->index >= ctor->nr)
		z_erofs_pagevec_ctor_pagedown(ctor, false);

	/* exclusive page type must be 0 */
	if (Z_EROFS_PAGE_TYPE_EXCLUSIVE != (uintptr_t)NULL)
		__bad_page_type_exclusive();

	/* should remind that collector->next never equal to 1, 2 */
	if (type == (uintptr_t)ctor->next) {
		ctor->next = page;
	}
	ctor->pages[ctor->index++] = tagptr_fold(erofs_vtptr_t, page, type);
	return true;
}

static inline struct page *
z_erofs_pagevec_dequeue(struct z_erofs_pagevec_ctor *ctor,
			enum z_erofs_page_type *type)
{
	erofs_vtptr_t t;

	if (ctor->index >= ctor->nr) {
		DBG_BUGON(!ctor->next);
		z_erofs_pagevec_ctor_pagedown(ctor, true);
	}

	t = ctor->pages[ctor->index];

	*type = tagptr_unfold_tags(t);

	/* should remind that collector->next never equal to 1, 2 */
	if (*type == (uintptr_t)ctor->next)
		ctor->next = tagptr_unfold_ptr(t);

	ctor->pages[ctor->index++] = tagptr_fold(erofs_vtptr_t, NULL, 0);
	return tagptr_unfold_ptr(t);
}
#endif
