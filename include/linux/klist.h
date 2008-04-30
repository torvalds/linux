/*
 *	klist.h - Some generic list helpers, extending struct list_head a bit.
 *
 *	Implementations are found in lib/klist.c
 *
 *
 *	Copyright (C) 2005 Patrick Mochel
 *
 *	This file is rleased under the GPL v2.
 */

#ifndef _LINUX_KLIST_H
#define _LINUX_KLIST_H

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/kref.h>
#include <linux/list.h>

struct klist_node;
struct klist {
	spinlock_t		k_lock;
	struct list_head	k_list;
	void			(*get)(struct klist_node *);
	void			(*put)(struct klist_node *);
};

#define KLIST_INIT(_name, _get, _put)					\
	{ .k_lock	= __SPIN_LOCK_UNLOCKED(_name.k_lock),		\
	  .k_list	= LIST_HEAD_INIT(_name.k_list),			\
	  .get		= _get,						\
	  .put		= _put, }

#define DEFINE_KLIST(_name, _get, _put)					\
	struct klist _name = KLIST_INIT(_name, _get, _put)

extern void klist_init(struct klist *k, void (*get)(struct klist_node *),
		       void (*put)(struct klist_node *));

struct klist_node {
	struct klist		*n_klist;
	struct list_head	n_node;
	struct kref		n_ref;
	struct completion	n_removed;
};

extern void klist_add_tail(struct klist_node *n, struct klist *k);
extern void klist_add_head(struct klist_node *n, struct klist *k);
extern void klist_add_after(struct klist_node *n, struct klist_node *pos);
extern void klist_add_before(struct klist_node *n, struct klist_node *pos);

extern void klist_del(struct klist_node *n);
extern void klist_remove(struct klist_node *n);

extern int klist_node_attached(struct klist_node *n);


struct klist_iter {
	struct klist		*i_klist;
	struct list_head	*i_head;
	struct klist_node	*i_cur;
};


extern void klist_iter_init(struct klist *k, struct klist_iter *i);
extern void klist_iter_init_node(struct klist *k, struct klist_iter *i,
				 struct klist_node *n);
extern void klist_iter_exit(struct klist_iter *i);
extern struct klist_node *klist_next(struct klist_iter *i);

#endif
