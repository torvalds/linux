// SPDX-License-Identifier: GPL-2.0

/*
 * Fast, unordered lists
 *
 * Supports add, remove, and iterate
 *
 * Underneath, they're a radix tree and an IDA, with a percpu buffer for slot
 * allocation and freeing.
 *
 * This means that adding, removing, and iterating over items is lockless,
 * except when refilling/emptying the percpu slot buffers.
 */

#include "fast_list.h"

struct fast_list_pcpu {
	u32			nr;
	u32			entries[31];
};

static int fast_list_alloc_idx(struct fast_list *l, gfp_t gfp)
{
	int idx = ida_alloc_range(&l->slots_allocated, 1, INT_MAX, gfp);
	if (unlikely(idx < 0))
		return 0;

	if (unlikely(!genradix_ptr_alloc_inlined(&l->items, idx, gfp))) {
		ida_free(&l->slots_allocated, idx);
		return 0;
	}

	return idx;
}

/**
 * fast_list_get_idx - get a slot in a fast_list
 * @l:		list to get slot in
 *
 * This allocates a slot in the radix tree without storing to it, so that we can
 * take the potential memory allocation failure early and do the list add later
 * when we can't take an allocation failure.
 *
 * Returns: positive integer on success, -ENOMEM on failure
 */
int fast_list_get_idx(struct fast_list *l)
{
	unsigned long flags;
	int idx;
retry:
	local_irq_save(flags);
	struct fast_list_pcpu *lp = this_cpu_ptr(l->buffer);

	if (unlikely(!lp->nr)) {
		u32 entries[16], nr = 0;

		local_irq_restore(flags);
		while (nr < ARRAY_SIZE(entries) &&
		       (idx = fast_list_alloc_idx(l, GFP_KERNEL)))
			entries[nr++] = idx;
		local_irq_save(flags);

		lp = this_cpu_ptr(l->buffer);

		while (nr && lp->nr < ARRAY_SIZE(lp->entries))
			lp->entries[lp->nr++] = entries[--nr];

		if (unlikely(nr)) {
			local_irq_restore(flags);
			while (nr)
				ida_free(&l->slots_allocated, entries[--nr]);
			goto retry;
		}

		if (unlikely(!lp->nr)) {
			local_irq_restore(flags);
			return -ENOMEM;
		}
	}

	idx = lp->entries[--lp->nr];
	local_irq_restore(flags);

	return idx;
}

/**
 * fast_list_add - add an item to a fast_list
 * @l:		list
 * @item:	item to add
 *
 * Allocates a slot in the radix tree and stores to it and then returns the
 * slot index, which must be passed to fast_list_remove().
 *
 * Returns: positive integer on success, -ENOMEM on failure
 */
int fast_list_add(struct fast_list *l, void *item)
{
	int idx = fast_list_get_idx(l);
	if (idx < 0)
		return idx;

	*genradix_ptr_inlined(&l->items, idx) = item;
	return idx;
}

/**
 * fast_list_remove - remove an item from a fast_list
 * @l:		list
 * @idx:	item's slot index
 *
 * Zeroes out the slot in the radix tree and frees the slot for future
 * fast_list_add() operations.
 */
void fast_list_remove(struct fast_list *l, unsigned idx)
{
	u32 entries[16], nr = 0;
	unsigned long flags;

	if (!idx)
		return;

	*genradix_ptr_inlined(&l->items, idx) = NULL;

	local_irq_save(flags);
	struct fast_list_pcpu *lp = this_cpu_ptr(l->buffer);

	if (unlikely(lp->nr == ARRAY_SIZE(lp->entries)))
		while (nr < ARRAY_SIZE(entries))
			entries[nr++] = lp->entries[--lp->nr];

	lp->entries[lp->nr++] = idx;
	local_irq_restore(flags);

	if (unlikely(nr))
		while (nr)
			ida_free(&l->slots_allocated, entries[--nr]);
}

void fast_list_exit(struct fast_list *l)
{
	/* XXX: warn if list isn't empty */
	free_percpu(l->buffer);
	ida_destroy(&l->slots_allocated);
	genradix_free(&l->items);
}

int fast_list_init(struct fast_list *l)
{
	genradix_init(&l->items);
	ida_init(&l->slots_allocated);
	l->buffer = alloc_percpu(*l->buffer);
	if (!l->buffer)
		return -ENOMEM;
	return 0;
}
