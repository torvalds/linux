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

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/kref.h>
#include <linux/list.h>


struct klist {
	spinlock_t		k_lock;
	struct list_head	k_list;
};


extern void klist_init(struct klist * k);


struct klist_node {
	struct klist		* n_klist;
	struct list_head	n_node;
	struct kref		n_ref;
	struct completion	n_removed;
};

extern void klist_add_tail(struct klist * k, struct klist_node * n);
extern void klist_add_head(struct klist * k, struct klist_node * n);

extern void klist_del(struct klist_node * n);
extern void klist_remove(struct klist_node * n);

extern int klist_node_attached(struct klist_node * n);


struct klist_iter {
	struct klist		* i_klist;
	struct list_head	* i_head;
	struct klist_node	* i_cur;
};


extern void klist_iter_init(struct klist * k, struct klist_iter * i);
extern void klist_iter_init_node(struct klist * k, struct klist_iter * i, 
				 struct klist_node * n);
extern void klist_iter_exit(struct klist_iter * i);
extern struct klist_node * klist_next(struct klist_iter * i);

