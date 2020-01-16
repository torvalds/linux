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

struct klist_yesde;
struct klist {
	spinlock_t		k_lock;
	struct list_head	k_list;
	void			(*get)(struct klist_yesde *);
	void			(*put)(struct klist_yesde *);
} __attribute__ ((aligned (sizeof(void *))));

#define KLIST_INIT(_name, _get, _put)					\
	{ .k_lock	= __SPIN_LOCK_UNLOCKED(_name.k_lock),		\
	  .k_list	= LIST_HEAD_INIT(_name.k_list),			\
	  .get		= _get,						\
	  .put		= _put, }

#define DEFINE_KLIST(_name, _get, _put)					\
	struct klist _name = KLIST_INIT(_name, _get, _put)

extern void klist_init(struct klist *k, void (*get)(struct klist_yesde *),
		       void (*put)(struct klist_yesde *));

struct klist_yesde {
	void			*n_klist;	/* never access directly */
	struct list_head	n_yesde;
	struct kref		n_ref;
};

extern void klist_add_tail(struct klist_yesde *n, struct klist *k);
extern void klist_add_head(struct klist_yesde *n, struct klist *k);
extern void klist_add_behind(struct klist_yesde *n, struct klist_yesde *pos);
extern void klist_add_before(struct klist_yesde *n, struct klist_yesde *pos);

extern void klist_del(struct klist_yesde *n);
extern void klist_remove(struct klist_yesde *n);

extern int klist_yesde_attached(struct klist_yesde *n);


struct klist_iter {
	struct klist		*i_klist;
	struct klist_yesde	*i_cur;
};


extern void klist_iter_init(struct klist *k, struct klist_iter *i);
extern void klist_iter_init_yesde(struct klist *k, struct klist_iter *i,
				 struct klist_yesde *n);
extern void klist_iter_exit(struct klist_iter *i);
extern struct klist_yesde *klist_prev(struct klist_iter *i);
extern struct klist_yesde *klist_next(struct klist_iter *i);

#endif
