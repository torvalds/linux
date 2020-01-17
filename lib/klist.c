// SPDX-License-Identifier: GPL-2.0-only
/*
 * klist.c - Routines for manipulating klists.
 *
 * Copyright (C) 2005 Patrick Mochel
 *
 * This klist interface provides a couple of structures that wrap around
 * struct list_head to provide explicit list "head" (struct klist) and list
 * "yesde" (struct klist_yesde) objects. For struct klist, a spinlock is
 * included that protects access to the actual list itself. struct
 * klist_yesde provides a pointer to the klist that owns it and a kref
 * reference count that indicates the number of current users of that yesde
 * in the list.
 *
 * The entire point is to provide an interface for iterating over a list
 * that is safe and allows for modification of the list during the
 * iteration (e.g. insertion and removal), including modification of the
 * current yesde on the list.
 *
 * It works using a 3rd object type - struct klist_iter - that is declared
 * and initialized before an iteration. klist_next() is used to acquire the
 * next element in the list. It returns NULL if there are yes more items.
 * Internally, that routine takes the klist's lock, decrements the
 * reference count of the previous klist_yesde and increments the count of
 * the next klist_yesde. It then drops the lock and returns.
 *
 * There are primitives for adding and removing yesdes to/from a klist.
 * When deleting, klist_del() will simply decrement the reference count.
 * Only when the count goes to 0 is the yesde removed from the list.
 * klist_remove() will try to delete the yesde from the list and block until
 * it is actually removed. This is useful for objects (like devices) that
 * have been removed from the system and must be freed (but must wait until
 * all accessors have finished).
 */

#include <linux/klist.h>
#include <linux/export.h>
#include <linux/sched.h>

/*
 * Use the lowest bit of n_klist to mark deleted yesdes and exclude
 * dead ones from iteration.
 */
#define KNODE_DEAD		1LU
#define KNODE_KLIST_MASK	~KNODE_DEAD

static struct klist *kyesde_klist(struct klist_yesde *kyesde)
{
	return (struct klist *)
		((unsigned long)kyesde->n_klist & KNODE_KLIST_MASK);
}

static bool kyesde_dead(struct klist_yesde *kyesde)
{
	return (unsigned long)kyesde->n_klist & KNODE_DEAD;
}

static void kyesde_set_klist(struct klist_yesde *kyesde, struct klist *klist)
{
	kyesde->n_klist = klist;
	/* yes kyesde deserves to start its life dead */
	WARN_ON(kyesde_dead(kyesde));
}

static void kyesde_kill(struct klist_yesde *kyesde)
{
	/* and yes kyesde should die twice ever either, see we're very humane */
	WARN_ON(kyesde_dead(kyesde));
	*(unsigned long *)&kyesde->n_klist |= KNODE_DEAD;
}

/**
 * klist_init - Initialize a klist structure.
 * @k: The klist we're initializing.
 * @get: The get function for the embedding object (NULL if yesne)
 * @put: The put function for the embedding object (NULL if yesne)
 *
 * Initialises the klist structure.  If the klist_yesde structures are
 * going to be embedded in refcounted objects (necessary for safe
 * deletion) then the get/put arguments are used to initialise
 * functions that take and release references on the embedding
 * objects.
 */
void klist_init(struct klist *k, void (*get)(struct klist_yesde *),
		void (*put)(struct klist_yesde *))
{
	INIT_LIST_HEAD(&k->k_list);
	spin_lock_init(&k->k_lock);
	k->get = get;
	k->put = put;
}
EXPORT_SYMBOL_GPL(klist_init);

static void add_head(struct klist *k, struct klist_yesde *n)
{
	spin_lock(&k->k_lock);
	list_add(&n->n_yesde, &k->k_list);
	spin_unlock(&k->k_lock);
}

static void add_tail(struct klist *k, struct klist_yesde *n)
{
	spin_lock(&k->k_lock);
	list_add_tail(&n->n_yesde, &k->k_list);
	spin_unlock(&k->k_lock);
}

static void klist_yesde_init(struct klist *k, struct klist_yesde *n)
{
	INIT_LIST_HEAD(&n->n_yesde);
	kref_init(&n->n_ref);
	kyesde_set_klist(n, k);
	if (k->get)
		k->get(n);
}

/**
 * klist_add_head - Initialize a klist_yesde and add it to front.
 * @n: yesde we're adding.
 * @k: klist it's going on.
 */
void klist_add_head(struct klist_yesde *n, struct klist *k)
{
	klist_yesde_init(k, n);
	add_head(k, n);
}
EXPORT_SYMBOL_GPL(klist_add_head);

/**
 * klist_add_tail - Initialize a klist_yesde and add it to back.
 * @n: yesde we're adding.
 * @k: klist it's going on.
 */
void klist_add_tail(struct klist_yesde *n, struct klist *k)
{
	klist_yesde_init(k, n);
	add_tail(k, n);
}
EXPORT_SYMBOL_GPL(klist_add_tail);

/**
 * klist_add_behind - Init a klist_yesde and add it after an existing yesde
 * @n: yesde we're adding.
 * @pos: yesde to put @n after
 */
void klist_add_behind(struct klist_yesde *n, struct klist_yesde *pos)
{
	struct klist *k = kyesde_klist(pos);

	klist_yesde_init(k, n);
	spin_lock(&k->k_lock);
	list_add(&n->n_yesde, &pos->n_yesde);
	spin_unlock(&k->k_lock);
}
EXPORT_SYMBOL_GPL(klist_add_behind);

/**
 * klist_add_before - Init a klist_yesde and add it before an existing yesde
 * @n: yesde we're adding.
 * @pos: yesde to put @n after
 */
void klist_add_before(struct klist_yesde *n, struct klist_yesde *pos)
{
	struct klist *k = kyesde_klist(pos);

	klist_yesde_init(k, n);
	spin_lock(&k->k_lock);
	list_add_tail(&n->n_yesde, &pos->n_yesde);
	spin_unlock(&k->k_lock);
}
EXPORT_SYMBOL_GPL(klist_add_before);

struct klist_waiter {
	struct list_head list;
	struct klist_yesde *yesde;
	struct task_struct *process;
	int woken;
};

static DEFINE_SPINLOCK(klist_remove_lock);
static LIST_HEAD(klist_remove_waiters);

static void klist_release(struct kref *kref)
{
	struct klist_waiter *waiter, *tmp;
	struct klist_yesde *n = container_of(kref, struct klist_yesde, n_ref);

	WARN_ON(!kyesde_dead(n));
	list_del(&n->n_yesde);
	spin_lock(&klist_remove_lock);
	list_for_each_entry_safe(waiter, tmp, &klist_remove_waiters, list) {
		if (waiter->yesde != n)
			continue;

		list_del(&waiter->list);
		waiter->woken = 1;
		mb();
		wake_up_process(waiter->process);
	}
	spin_unlock(&klist_remove_lock);
	kyesde_set_klist(n, NULL);
}

static int klist_dec_and_del(struct klist_yesde *n)
{
	return kref_put(&n->n_ref, klist_release);
}

static void klist_put(struct klist_yesde *n, bool kill)
{
	struct klist *k = kyesde_klist(n);
	void (*put)(struct klist_yesde *) = k->put;

	spin_lock(&k->k_lock);
	if (kill)
		kyesde_kill(n);
	if (!klist_dec_and_del(n))
		put = NULL;
	spin_unlock(&k->k_lock);
	if (put)
		put(n);
}

/**
 * klist_del - Decrement the reference count of yesde and try to remove.
 * @n: yesde we're deleting.
 */
void klist_del(struct klist_yesde *n)
{
	klist_put(n, true);
}
EXPORT_SYMBOL_GPL(klist_del);

/**
 * klist_remove - Decrement the refcount of yesde and wait for it to go away.
 * @n: yesde we're removing.
 */
void klist_remove(struct klist_yesde *n)
{
	struct klist_waiter waiter;

	waiter.yesde = n;
	waiter.process = current;
	waiter.woken = 0;
	spin_lock(&klist_remove_lock);
	list_add(&waiter.list, &klist_remove_waiters);
	spin_unlock(&klist_remove_lock);

	klist_del(n);

	for (;;) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (waiter.woken)
			break;
		schedule();
	}
	__set_current_state(TASK_RUNNING);
}
EXPORT_SYMBOL_GPL(klist_remove);

/**
 * klist_yesde_attached - Say whether a yesde is bound to a list or yest.
 * @n: Node that we're testing.
 */
int klist_yesde_attached(struct klist_yesde *n)
{
	return (n->n_klist != NULL);
}
EXPORT_SYMBOL_GPL(klist_yesde_attached);

/**
 * klist_iter_init_yesde - Initialize a klist_iter structure.
 * @k: klist we're iterating.
 * @i: klist_iter we're filling.
 * @n: yesde to start with.
 *
 * Similar to klist_iter_init(), but starts the action off with @n,
 * instead of with the list head.
 */
void klist_iter_init_yesde(struct klist *k, struct klist_iter *i,
			  struct klist_yesde *n)
{
	i->i_klist = k;
	i->i_cur = NULL;
	if (n && kref_get_unless_zero(&n->n_ref))
		i->i_cur = n;
}
EXPORT_SYMBOL_GPL(klist_iter_init_yesde);

/**
 * klist_iter_init - Iniitalize a klist_iter structure.
 * @k: klist we're iterating.
 * @i: klist_iter structure we're filling.
 *
 * Similar to klist_iter_init_yesde(), but start with the list head.
 */
void klist_iter_init(struct klist *k, struct klist_iter *i)
{
	klist_iter_init_yesde(k, i, NULL);
}
EXPORT_SYMBOL_GPL(klist_iter_init);

/**
 * klist_iter_exit - Finish a list iteration.
 * @i: Iterator structure.
 *
 * Must be called when done iterating over list, as it decrements the
 * refcount of the current yesde. Necessary in case iteration exited before
 * the end of the list was reached, and always good form.
 */
void klist_iter_exit(struct klist_iter *i)
{
	if (i->i_cur) {
		klist_put(i->i_cur, false);
		i->i_cur = NULL;
	}
}
EXPORT_SYMBOL_GPL(klist_iter_exit);

static struct klist_yesde *to_klist_yesde(struct list_head *n)
{
	return container_of(n, struct klist_yesde, n_yesde);
}

/**
 * klist_prev - Ante up prev yesde in list.
 * @i: Iterator structure.
 *
 * First grab list lock. Decrement the reference count of the previous
 * yesde, if there was one. Grab the prev yesde, increment its reference
 * count, drop the lock, and return that prev yesde.
 */
struct klist_yesde *klist_prev(struct klist_iter *i)
{
	void (*put)(struct klist_yesde *) = i->i_klist->put;
	struct klist_yesde *last = i->i_cur;
	struct klist_yesde *prev;
	unsigned long flags;

	spin_lock_irqsave(&i->i_klist->k_lock, flags);

	if (last) {
		prev = to_klist_yesde(last->n_yesde.prev);
		if (!klist_dec_and_del(last))
			put = NULL;
	} else
		prev = to_klist_yesde(i->i_klist->k_list.prev);

	i->i_cur = NULL;
	while (prev != to_klist_yesde(&i->i_klist->k_list)) {
		if (likely(!kyesde_dead(prev))) {
			kref_get(&prev->n_ref);
			i->i_cur = prev;
			break;
		}
		prev = to_klist_yesde(prev->n_yesde.prev);
	}

	spin_unlock_irqrestore(&i->i_klist->k_lock, flags);

	if (put && last)
		put(last);
	return i->i_cur;
}
EXPORT_SYMBOL_GPL(klist_prev);

/**
 * klist_next - Ante up next yesde in list.
 * @i: Iterator structure.
 *
 * First grab list lock. Decrement the reference count of the previous
 * yesde, if there was one. Grab the next yesde, increment its reference
 * count, drop the lock, and return that next yesde.
 */
struct klist_yesde *klist_next(struct klist_iter *i)
{
	void (*put)(struct klist_yesde *) = i->i_klist->put;
	struct klist_yesde *last = i->i_cur;
	struct klist_yesde *next;
	unsigned long flags;

	spin_lock_irqsave(&i->i_klist->k_lock, flags);

	if (last) {
		next = to_klist_yesde(last->n_yesde.next);
		if (!klist_dec_and_del(last))
			put = NULL;
	} else
		next = to_klist_yesde(i->i_klist->k_list.next);

	i->i_cur = NULL;
	while (next != to_klist_yesde(&i->i_klist->k_list)) {
		if (likely(!kyesde_dead(next))) {
			kref_get(&next->n_ref);
			i->i_cur = next;
			break;
		}
		next = to_klist_yesde(next->n_yesde.next);
	}

	spin_unlock_irqrestore(&i->i_klist->k_lock, flags);

	if (put && last)
		put(last);
	return i->i_cur;
}
EXPORT_SYMBOL_GPL(klist_next);
