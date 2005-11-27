/*
 *	klist.c - Routines for manipulating klists.
 *
 *
 *	This klist interface provides a couple of structures that wrap around 
 *	struct list_head to provide explicit list "head" (struct klist) and 
 *	list "node" (struct klist_node) objects. For struct klist, a spinlock
 *	is included that protects access to the actual list itself. struct 
 *	klist_node provides a pointer to the klist that owns it and a kref
 *	reference count that indicates the number of current users of that node
 *	in the list.
 *
 *	The entire point is to provide an interface for iterating over a list
 *	that is safe and allows for modification of the list during the
 *	iteration (e.g. insertion and removal), including modification of the
 *	current node on the list.
 *
 *	It works using a 3rd object type - struct klist_iter - that is declared
 *	and initialized before an iteration. klist_next() is used to acquire the
 *	next element in the list. It returns NULL if there are no more items.
 *	Internally, that routine takes the klist's lock, decrements the reference
 *	count of the previous klist_node and increments the count of the next
 *	klist_node. It then drops the lock and returns.
 *
 *	There are primitives for adding and removing nodes to/from a klist. 
 *	When deleting, klist_del() will simply decrement the reference count. 
 *	Only when the count goes to 0 is the node removed from the list. 
 *	klist_remove() will try to delete the node from the list and block
 *	until it is actually removed. This is useful for objects (like devices)
 *	that have been removed from the system and must be freed (but must wait
 *	until all accessors have finished).
 *
 *	Copyright (C) 2005 Patrick Mochel
 *
 *	This file is released under the GPL v2.
 */

#include <linux/klist.h>
#include <linux/module.h>


/**
 *	klist_init - Initialize a klist structure. 
 *	@k:	The klist we're initializing.
 *	@get:	The get function for the embedding object (NULL if none)
 *	@put:	The put function for the embedding object (NULL if none)
 *
 * Initialises the klist structure.  If the klist_node structures are
 * going to be embedded in refcounted objects (necessary for safe
 * deletion) then the get/put arguments are used to initialise
 * functions that take and release references on the embedding
 * objects.
 */

void klist_init(struct klist * k, void (*get)(struct klist_node *),
		void (*put)(struct klist_node *))
{
	INIT_LIST_HEAD(&k->k_list);
	spin_lock_init(&k->k_lock);
	k->get = get;
	k->put = put;
}

EXPORT_SYMBOL_GPL(klist_init);


static void add_head(struct klist * k, struct klist_node * n)
{
	spin_lock(&k->k_lock);
	list_add(&n->n_node, &k->k_list);
	spin_unlock(&k->k_lock);
}

static void add_tail(struct klist * k, struct klist_node * n)
{
	spin_lock(&k->k_lock);
	list_add_tail(&n->n_node, &k->k_list);
	spin_unlock(&k->k_lock);
}


static void klist_node_init(struct klist * k, struct klist_node * n)
{
	INIT_LIST_HEAD(&n->n_node);
	init_completion(&n->n_removed);
	kref_init(&n->n_ref);
	n->n_klist = k;
	if (k->get)
		k->get(n);
}


/**
 *	klist_add_head - Initialize a klist_node and add it to front.
 *	@n:	node we're adding.
 *	@k:	klist it's going on.
 */

void klist_add_head(struct klist_node * n, struct klist * k)
{
	klist_node_init(k, n);
	add_head(k, n);
}

EXPORT_SYMBOL_GPL(klist_add_head);


/**
 *	klist_add_tail - Initialize a klist_node and add it to back.
 *	@n:	node we're adding.
 *	@k:	klist it's going on.
 */

void klist_add_tail(struct klist_node * n, struct klist * k)
{
	klist_node_init(k, n);
	add_tail(k, n);
}

EXPORT_SYMBOL_GPL(klist_add_tail);


static void klist_release(struct kref * kref)
{
	struct klist_node * n = container_of(kref, struct klist_node, n_ref);
	void (*put)(struct klist_node *) = n->n_klist->put;
	list_del(&n->n_node);
	complete(&n->n_removed);
	n->n_klist = NULL;
	if (put)
		put(n);
}

static int klist_dec_and_del(struct klist_node * n)
{
	return kref_put(&n->n_ref, klist_release);
}


/**
 *	klist_del - Decrement the reference count of node and try to remove.
 *	@n:	node we're deleting.
 */

void klist_del(struct klist_node * n)
{
	struct klist * k = n->n_klist;

	spin_lock(&k->k_lock);
	klist_dec_and_del(n);
	spin_unlock(&k->k_lock);
}

EXPORT_SYMBOL_GPL(klist_del);


/**
 *	klist_remove - Decrement the refcount of node and wait for it to go away.
 *	@n:	node we're removing.
 */

void klist_remove(struct klist_node * n)
{
	struct klist * k = n->n_klist;
	spin_lock(&k->k_lock);
	klist_dec_and_del(n);
	spin_unlock(&k->k_lock);
	wait_for_completion(&n->n_removed);
}

EXPORT_SYMBOL_GPL(klist_remove);


/**
 *	klist_node_attached - Say whether a node is bound to a list or not.
 *	@n:	Node that we're testing.
 */

int klist_node_attached(struct klist_node * n)
{
	return (n->n_klist != NULL);
}

EXPORT_SYMBOL_GPL(klist_node_attached);


/**
 *	klist_iter_init_node - Initialize a klist_iter structure.
 *	@k:	klist we're iterating.
 *	@i:	klist_iter we're filling.
 *	@n:	node to start with.
 *
 *	Similar to klist_iter_init(), but starts the action off with @n, 
 *	instead of with the list head.
 */

void klist_iter_init_node(struct klist * k, struct klist_iter * i, struct klist_node * n)
{
	i->i_klist = k;
	i->i_head = &k->k_list;
	i->i_cur = n;
	if (n)
		kref_get(&n->n_ref);
}

EXPORT_SYMBOL_GPL(klist_iter_init_node);


/**
 *	klist_iter_init - Iniitalize a klist_iter structure.
 *	@k:	klist we're iterating.
 *	@i:	klist_iter structure we're filling.
 *
 *	Similar to klist_iter_init_node(), but start with the list head.
 */

void klist_iter_init(struct klist * k, struct klist_iter * i)
{
	klist_iter_init_node(k, i, NULL);
}

EXPORT_SYMBOL_GPL(klist_iter_init);


/**
 *	klist_iter_exit - Finish a list iteration.
 *	@i:	Iterator structure.
 *
 *	Must be called when done iterating over list, as it decrements the 
 *	refcount of the current node. Necessary in case iteration exited before
 *	the end of the list was reached, and always good form.
 */

void klist_iter_exit(struct klist_iter * i)
{
	if (i->i_cur) {
		klist_del(i->i_cur);
		i->i_cur = NULL;
	}
}

EXPORT_SYMBOL_GPL(klist_iter_exit);


static struct klist_node * to_klist_node(struct list_head * n)
{
	return container_of(n, struct klist_node, n_node);
}


/**
 *	klist_next - Ante up next node in list.
 *	@i:	Iterator structure.
 *
 *	First grab list lock. Decrement the reference count of the previous
 *	node, if there was one. Grab the next node, increment its reference 
 *	count, drop the lock, and return that next node.
 */

struct klist_node * klist_next(struct klist_iter * i)
{
	struct list_head * next;
	struct klist_node * knode = NULL;

	spin_lock(&i->i_klist->k_lock);
	if (i->i_cur) {
		next = i->i_cur->n_node.next;
		klist_dec_and_del(i->i_cur);
	} else
		next = i->i_head->next;

	if (next != i->i_head) {
		knode = to_klist_node(next);
		kref_get(&knode->n_ref);
	}
	i->i_cur = knode;
	spin_unlock(&i->i_klist->k_lock);
	return knode;
}

EXPORT_SYMBOL_GPL(klist_next);


