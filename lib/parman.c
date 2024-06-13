/*
 * lib/parman.c - Manager for linear priority array areas
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/parman.h>

struct parman_algo {
	int (*item_add)(struct parman *parman, struct parman_prio *prio,
			struct parman_item *item);
	void (*item_remove)(struct parman *parman, struct parman_prio *prio,
			    struct parman_item *item);
};

struct parman {
	const struct parman_ops *ops;
	void *priv;
	const struct parman_algo *algo;
	unsigned long count;
	unsigned long limit_count;
	struct list_head prio_list;
};

static int parman_enlarge(struct parman *parman)
{
	unsigned long new_count = parman->limit_count +
				  parman->ops->resize_step;
	int err;

	err = parman->ops->resize(parman->priv, new_count);
	if (err)
		return err;
	parman->limit_count = new_count;
	return 0;
}

static int parman_shrink(struct parman *parman)
{
	unsigned long new_count = parman->limit_count -
				  parman->ops->resize_step;
	int err;

	if (new_count < parman->ops->base_count)
		return 0;
	err = parman->ops->resize(parman->priv, new_count);
	if (err)
		return err;
	parman->limit_count = new_count;
	return 0;
}

static bool parman_prio_used(struct parman_prio *prio)
{
	return !list_empty(&prio->item_list);
}

static struct parman_item *parman_prio_first_item(struct parman_prio *prio)
{
	return list_first_entry(&prio->item_list,
				typeof(struct parman_item), list);
}

static unsigned long parman_prio_first_index(struct parman_prio *prio)
{
	return parman_prio_first_item(prio)->index;
}

static struct parman_item *parman_prio_last_item(struct parman_prio *prio)
{
	return list_last_entry(&prio->item_list,
			       typeof(struct parman_item), list);
}

static unsigned long parman_prio_last_index(struct parman_prio *prio)
{
	return parman_prio_last_item(prio)->index;
}

static unsigned long parman_lsort_new_index_find(struct parman *parman,
						 struct parman_prio *prio)
{
	list_for_each_entry_from_reverse(prio, &parman->prio_list, list) {
		if (!parman_prio_used(prio))
			continue;
		return parman_prio_last_index(prio) + 1;
	}
	return 0;
}

static void __parman_prio_move(struct parman *parman, struct parman_prio *prio,
			       struct parman_item *item, unsigned long to_index,
			       unsigned long count)
{
	parman->ops->move(parman->priv, item->index, to_index, count);
}

static void parman_prio_shift_down(struct parman *parman,
				   struct parman_prio *prio)
{
	struct parman_item *item;
	unsigned long to_index;

	if (!parman_prio_used(prio))
		return;
	item = parman_prio_first_item(prio);
	to_index = parman_prio_last_index(prio) + 1;
	__parman_prio_move(parman, prio, item, to_index, 1);
	list_move_tail(&item->list, &prio->item_list);
	item->index = to_index;
}

static void parman_prio_shift_up(struct parman *parman,
				 struct parman_prio *prio)
{
	struct parman_item *item;
	unsigned long to_index;

	if (!parman_prio_used(prio))
		return;
	item = parman_prio_last_item(prio);
	to_index = parman_prio_first_index(prio) - 1;
	__parman_prio_move(parman, prio, item, to_index, 1);
	list_move(&item->list, &prio->item_list);
	item->index = to_index;
}

static void parman_prio_item_remove(struct parman *parman,
				    struct parman_prio *prio,
				    struct parman_item *item)
{
	struct parman_item *last_item;
	unsigned long to_index;

	last_item = parman_prio_last_item(prio);
	if (last_item == item) {
		list_del(&item->list);
		return;
	}
	to_index = item->index;
	__parman_prio_move(parman, prio, last_item, to_index, 1);
	list_del(&last_item->list);
	list_replace(&item->list, &last_item->list);
	last_item->index = to_index;
}

static int parman_lsort_item_add(struct parman *parman,
				 struct parman_prio *prio,
				 struct parman_item *item)
{
	struct parman_prio *prio2;
	unsigned long new_index;
	int err;

	if (parman->count + 1 > parman->limit_count) {
		err = parman_enlarge(parman);
		if (err)
			return err;
	}

	new_index = parman_lsort_new_index_find(parman, prio);
	list_for_each_entry_reverse(prio2, &parman->prio_list, list) {
		if (prio2 == prio)
			break;
		parman_prio_shift_down(parman, prio2);
	}
	item->index = new_index;
	list_add_tail(&item->list, &prio->item_list);
	parman->count++;
	return 0;
}

static void parman_lsort_item_remove(struct parman *parman,
				     struct parman_prio *prio,
				     struct parman_item *item)
{
	parman_prio_item_remove(parman, prio, item);
	list_for_each_entry_continue(prio, &parman->prio_list, list)
		parman_prio_shift_up(parman, prio);
	parman->count--;
	if (parman->limit_count - parman->count >= parman->ops->resize_step)
		parman_shrink(parman);
}

static const struct parman_algo parman_lsort = {
	.item_add	= parman_lsort_item_add,
	.item_remove	= parman_lsort_item_remove,
};

static const struct parman_algo *parman_algos[] = {
	&parman_lsort,
};

/**
 * parman_create - creates a new parman instance
 * @ops:	caller-specific callbacks
 * @priv:	pointer to a private data passed to the ops
 *
 * Note: all locking must be provided by the caller.
 *
 * Each parman instance manages an array area with chunks of entries
 * with the same priority. Consider following example:
 *
 * item 1 with prio 10
 * item 2 with prio 10
 * item 3 with prio 10
 * item 4 with prio 20
 * item 5 with prio 20
 * item 6 with prio 30
 * item 7 with prio 30
 * item 8 with prio 30
 *
 * In this example, there are 3 priority chunks. The order of the priorities
 * matters, however the order of items within a single priority chunk does not
 * matter. So the same array could be ordered as follows:
 *
 * item 2 with prio 10
 * item 3 with prio 10
 * item 1 with prio 10
 * item 5 with prio 20
 * item 4 with prio 20
 * item 7 with prio 30
 * item 8 with prio 30
 * item 6 with prio 30
 *
 * The goal of parman is to maintain the priority ordering. The caller
 * provides @ops with callbacks parman uses to move the items
 * and resize the array area.
 *
 * Returns a pointer to newly created parman instance in case of success,
 * otherwise it returns NULL.
 */
struct parman *parman_create(const struct parman_ops *ops, void *priv)
{
	struct parman *parman;

	parman = kzalloc(sizeof(*parman), GFP_KERNEL);
	if (!parman)
		return NULL;
	INIT_LIST_HEAD(&parman->prio_list);
	parman->ops = ops;
	parman->priv = priv;
	parman->limit_count = ops->base_count;
	parman->algo = parman_algos[ops->algo];
	return parman;
}
EXPORT_SYMBOL(parman_create);

/**
 * parman_destroy - destroys existing parman instance
 * @parman:	parman instance
 *
 * Note: all locking must be provided by the caller.
 */
void parman_destroy(struct parman *parman)
{
	WARN_ON(!list_empty(&parman->prio_list));
	kfree(parman);
}
EXPORT_SYMBOL(parman_destroy);

/**
 * parman_prio_init - initializes a parman priority chunk
 * @parman:	parman instance
 * @prio:	parman prio structure to be initialized
 * @priority:	desired priority of the chunk
 *
 * Note: all locking must be provided by the caller.
 *
 * Before caller could add an item with certain priority, he has to
 * initialize a priority chunk for it using this function.
 */
void parman_prio_init(struct parman *parman, struct parman_prio *prio,
		      unsigned long priority)
{
	struct parman_prio *prio2;
	struct list_head *pos;

	INIT_LIST_HEAD(&prio->item_list);
	prio->priority = priority;

	/* Position inside the list according to priority */
	list_for_each(pos, &parman->prio_list) {
		prio2 = list_entry(pos, typeof(*prio2), list);
		if (prio2->priority > prio->priority)
			break;
	}
	list_add_tail(&prio->list, pos);
}
EXPORT_SYMBOL(parman_prio_init);

/**
 * parman_prio_fini - finalizes use of parman priority chunk
 * @prio:	parman prio structure
 *
 * Note: all locking must be provided by the caller.
 */
void parman_prio_fini(struct parman_prio *prio)
{
	WARN_ON(parman_prio_used(prio));
	list_del(&prio->list);
}
EXPORT_SYMBOL(parman_prio_fini);

/**
 * parman_item_add - adds a parman item under defined priority
 * @parman:	parman instance
 * @prio:	parman prio instance to add the item to
 * @item:	parman item instance
 *
 * Note: all locking must be provided by the caller.
 *
 * Adds item to a array managed by parman instance under the specified priority.
 *
 * Returns 0 in case of success, negative number to indicate an error.
 */
int parman_item_add(struct parman *parman, struct parman_prio *prio,
		    struct parman_item *item)
{
	return parman->algo->item_add(parman, prio, item);
}
EXPORT_SYMBOL(parman_item_add);

/**
 * parman_item_remove - deletes parman item
 * @parman:	parman instance
 * @prio:	parman prio instance to delete the item from
 * @item:	parman item instance
 *
 * Note: all locking must be provided by the caller.
 */
void parman_item_remove(struct parman *parman, struct parman_prio *prio,
			struct parman_item *item)
{
	parman->algo->item_remove(parman, prio, item);
}
EXPORT_SYMBOL(parman_item_remove);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Priority-based array manager");
