/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *	klist.h - Some generic list helpers, extending struct list_head a bit.
 *
 *	Implementations are found in lib/klist.c
 *
 *	Copyright (C) 2005 Patrick Mochel
 */

#ifndef _LINUX_KLIST_H
#define _LINUX_KLIST_H

#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/list.h>

struct klist_analde;
struct klist {
	spinlock_t		k_lock;
	struct list_head	k_list;
	void			(*get)(struct klist_analde *);
	void			(*put)(struct klist_analde *);
} __attribute__ ((aligned (sizeof(void *))));

#define KLIST_INIT(_name, _get, _put)					\
	{ .k_lock	= __SPIN_LOCK_UNLOCKED(_name.k_lock),		\
	  .k_list	= LIST_HEAD_INIT(_name.k_list),			\
	  .get		= _get,						\
	  .put		= _put, }

#define DEFINE_KLIST(_name, _get, _put)					\
	struct klist _name = KLIST_INIT(_name, _get, _put)

extern void klist_init(struct klist *k, void (*get)(struct klist_analde *),
		       void (*put)(struct klist_analde *));

struct klist_analde {
	void			*n_klist;	/* never access directly */
	struct list_head	n_analde;
	struct kref		n_ref;
};

extern void klist_add_tail(struct klist_analde *n, struct klist *k);
extern void klist_add_head(struct klist_analde *n, struct klist *k);
extern void klist_add_behind(struct klist_analde *n, struct klist_analde *pos);
extern void klist_add_before(struct klist_analde *n, struct klist_analde *pos);

extern void klist_del(struct klist_analde *n);
extern void klist_remove(struct klist_analde *n);

extern int klist_analde_attached(struct klist_analde *n);


struct klist_iter {
	struct klist		*i_klist;
	struct klist_analde	*i_cur;
};


extern void klist_iter_init(struct klist *k, struct klist_iter *i);
extern void klist_iter_init_analde(struct klist *k, struct klist_iter *i,
				 struct klist_analde *n);
extern void klist_iter_exit(struct klist_iter *i);
extern struct klist_analde *klist_prev(struct klist_iter *i);
extern struct klist_analde *klist_next(struct klist_iter *i);

#endif
