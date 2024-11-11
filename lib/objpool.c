// SPDX-License-Identifier: GPL-2.0

#include <linux/objpool.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/atomic.h>
#include <linux/irqflags.h>
#include <linux/cpumask.h>
#include <linux/log2.h>

/*
 * objpool: ring-array based lockless MPMC/FIFO queues
 *
 * Copyright: wuqiang.matt@bytedance.com,mhiramat@kernel.org
 */

/* initialize percpu objpool_slot */
static int
objpool_init_percpu_slot(struct objpool_head *pool,
			 struct objpool_slot *slot,
			 int nodes, void *context,
			 objpool_init_obj_cb objinit)
{
	void *obj = (void *)&slot->entries[pool->capacity];
	int i;

	/* initialize elements of percpu objpool_slot */
	slot->mask = pool->capacity - 1;

	for (i = 0; i < nodes; i++) {
		if (objinit) {
			int rc = objinit(obj, context);
			if (rc)
				return rc;
		}
		slot->entries[slot->tail & slot->mask] = obj;
		obj = obj + pool->obj_size;
		slot->tail++;
		slot->last = slot->tail;
		pool->nr_objs++;
	}

	return 0;
}

/* allocate and initialize percpu slots */
static int
objpool_init_percpu_slots(struct objpool_head *pool, int nr_objs,
			  void *context, objpool_init_obj_cb objinit)
{
	int i, cpu_count = 0;

	for (i = 0; i < nr_cpu_ids; i++) {

		struct objpool_slot *slot;
		int nodes, size, rc;

		/* skip the cpu node which could never be present */
		if (!cpu_possible(i))
			continue;

		/* compute how many objects to be allocated with this slot */
		nodes = nr_objs / pool->nr_possible_cpus;
		if (cpu_count < (nr_objs % pool->nr_possible_cpus))
			nodes++;
		cpu_count++;

		size = struct_size(slot, entries, pool->capacity) +
			pool->obj_size * nodes;

		/*
		 * here we allocate percpu-slot & objs together in a single
		 * allocation to make it more compact, taking advantage of
		 * warm caches and TLB hits. in default vmalloc is used to
		 * reduce the pressure of kernel slab system. as we know,
		 * mimimal size of vmalloc is one page since vmalloc would
		 * always align the requested size to page size.
		 * but if vmalloc fails or it is not available (e.g. GFP_ATOMIC)
		 * allocate percpu slot with kmalloc.
		 */
		slot = NULL;

		if ((pool->gfp & (GFP_ATOMIC | GFP_KERNEL)) != GFP_ATOMIC)
			slot = __vmalloc_node(size, sizeof(void *), pool->gfp,
				cpu_to_node(i), __builtin_return_address(0));

		if (!slot) {
			slot = kmalloc_node(size, pool->gfp, cpu_to_node(i));
			if (!slot)
				return -ENOMEM;
		}
		memset(slot, 0, size);
		pool->cpu_slots[i] = slot;

		/* initialize the objpool_slot of cpu node i */
		rc = objpool_init_percpu_slot(pool, slot, nodes, context, objinit);
		if (rc)
			return rc;
	}

	return 0;
}

/* cleanup all percpu slots of the object pool */
static void objpool_fini_percpu_slots(struct objpool_head *pool)
{
	int i;

	if (!pool->cpu_slots)
		return;

	for (i = 0; i < nr_cpu_ids; i++)
		kvfree(pool->cpu_slots[i]);
	kfree(pool->cpu_slots);
}

/* initialize object pool and pre-allocate objects */
int objpool_init(struct objpool_head *pool, int nr_objs, int object_size,
		gfp_t gfp, void *context, objpool_init_obj_cb objinit,
		objpool_fini_cb release)
{
	int rc, capacity, slot_size;

	/* check input parameters */
	if (nr_objs <= 0 || nr_objs > OBJPOOL_NR_OBJECT_MAX ||
	    object_size <= 0 || object_size > OBJPOOL_OBJECT_SIZE_MAX)
		return -EINVAL;

	/* align up to unsigned long size */
	object_size = ALIGN(object_size, sizeof(long));

	/* calculate capacity of percpu objpool_slot */
	capacity = roundup_pow_of_two(nr_objs);
	if (!capacity)
		return -EINVAL;

	/* initialize objpool pool */
	memset(pool, 0, sizeof(struct objpool_head));
	pool->nr_possible_cpus = num_possible_cpus();
	pool->obj_size = object_size;
	pool->capacity = capacity;
	pool->gfp = gfp & ~__GFP_ZERO;
	pool->context = context;
	pool->release = release;
	slot_size = nr_cpu_ids * sizeof(struct objpool_slot);
	pool->cpu_slots = kzalloc(slot_size, pool->gfp);
	if (!pool->cpu_slots)
		return -ENOMEM;

	/* initialize per-cpu slots */
	rc = objpool_init_percpu_slots(pool, nr_objs, context, objinit);
	if (rc)
		objpool_fini_percpu_slots(pool);
	else
		refcount_set(&pool->ref, pool->nr_objs + 1);

	return rc;
}
EXPORT_SYMBOL_GPL(objpool_init);

/* release whole objpool forcely */
void objpool_free(struct objpool_head *pool)
{
	if (!pool->cpu_slots)
		return;

	/* release percpu slots */
	objpool_fini_percpu_slots(pool);

	/* call user's cleanup callback if provided */
	if (pool->release)
		pool->release(pool, pool->context);
}
EXPORT_SYMBOL_GPL(objpool_free);

/* drop the allocated object, rather reclaim it to objpool */
int objpool_drop(void *obj, struct objpool_head *pool)
{
	if (!obj || !pool)
		return -EINVAL;

	if (refcount_dec_and_test(&pool->ref)) {
		objpool_free(pool);
		return 0;
	}

	return -EAGAIN;
}
EXPORT_SYMBOL_GPL(objpool_drop);

/* drop unused objects and defref objpool for releasing */
void objpool_fini(struct objpool_head *pool)
{
	int count = 1; /* extra ref for objpool itself */

	/* drop all remained objects from objpool */
	while (objpool_pop(pool))
		count++;

	if (refcount_sub_and_test(count, &pool->ref))
		objpool_free(pool);
}
EXPORT_SYMBOL_GPL(objpool_fini);
