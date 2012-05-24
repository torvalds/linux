/*
 * cpu_rmap.c: CPU affinity reverse-map support
 * Copyright 2011 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/cpu_rmap.h>
#ifdef CONFIG_GENERIC_HARDIRQS
#include <linux/interrupt.h>
#endif
#include <linux/export.h>

/*
 * These functions maintain a mapping from CPUs to some ordered set of
 * objects with CPU affinities.  This can be seen as a reverse-map of
 * CPU affinity.  However, we do not assume that the object affinities
 * cover all CPUs in the system.  For those CPUs not directly covered
 * by object affinities, we attempt to find a nearest object based on
 * CPU topology.
 */

/**
 * alloc_cpu_rmap - allocate CPU affinity reverse-map
 * @size: Number of objects to be mapped
 * @flags: Allocation flags e.g. %GFP_KERNEL
 */
struct cpu_rmap *alloc_cpu_rmap(unsigned int size, gfp_t flags)
{
	struct cpu_rmap *rmap;
	unsigned int cpu;
	size_t obj_offset;

	/* This is a silly number of objects, and we use u16 indices. */
	if (size > 0xffff)
		return NULL;

	/* Offset of object pointer array from base structure */
	obj_offset = ALIGN(offsetof(struct cpu_rmap, near[nr_cpu_ids]),
			   sizeof(void *));

	rmap = kzalloc(obj_offset + size * sizeof(rmap->obj[0]), flags);
	if (!rmap)
		return NULL;

	rmap->obj = (void **)((char *)rmap + obj_offset);

	/* Initially assign CPUs to objects on a rota, since we have
	 * no idea where the objects are.  Use infinite distance, so
	 * any object with known distance is preferable.  Include the
	 * CPUs that are not present/online, since we definitely want
	 * any newly-hotplugged CPUs to have some object assigned.
	 */
	for_each_possible_cpu(cpu) {
		rmap->near[cpu].index = cpu % size;
		rmap->near[cpu].dist = CPU_RMAP_DIST_INF;
	}

	rmap->size = size;
	return rmap;
}
EXPORT_SYMBOL(alloc_cpu_rmap);

/* Reevaluate nearest object for given CPU, comparing with the given
 * neighbours at the given distance.
 */
static bool cpu_rmap_copy_neigh(struct cpu_rmap *rmap, unsigned int cpu,
				const struct cpumask *mask, u16 dist)
{
	int neigh;

	for_each_cpu(neigh, mask) {
		if (rmap->near[cpu].dist > dist &&
		    rmap->near[neigh].dist <= dist) {
			rmap->near[cpu].index = rmap->near[neigh].index;
			rmap->near[cpu].dist = dist;
			return true;
		}
	}
	return false;
}

#ifdef DEBUG
static void debug_print_rmap(const struct cpu_rmap *rmap, const char *prefix)
{
	unsigned index;
	unsigned int cpu;

	pr_info("cpu_rmap %p, %s:\n", rmap, prefix);

	for_each_possible_cpu(cpu) {
		index = rmap->near[cpu].index;
		pr_info("cpu %d -> obj %u (distance %u)\n",
			cpu, index, rmap->near[cpu].dist);
	}
}
#else
static inline void
debug_print_rmap(const struct cpu_rmap *rmap, const char *prefix)
{
}
#endif

/**
 * cpu_rmap_add - add object to a rmap
 * @rmap: CPU rmap allocated with alloc_cpu_rmap()
 * @obj: Object to add to rmap
 *
 * Return index of object.
 */
int cpu_rmap_add(struct cpu_rmap *rmap, void *obj)
{
	u16 index;

	BUG_ON(rmap->used >= rmap->size);
	index = rmap->used++;
	rmap->obj[index] = obj;
	return index;
}
EXPORT_SYMBOL(cpu_rmap_add);

/**
 * cpu_rmap_update - update CPU rmap following a change of object affinity
 * @rmap: CPU rmap to update
 * @index: Index of object whose affinity changed
 * @affinity: New CPU affinity of object
 */
int cpu_rmap_update(struct cpu_rmap *rmap, u16 index,
		    const struct cpumask *affinity)
{
	cpumask_var_t update_mask;
	unsigned int cpu;

	if (unlikely(!zalloc_cpumask_var(&update_mask, GFP_KERNEL)))
		return -ENOMEM;

	/* Invalidate distance for all CPUs for which this used to be
	 * the nearest object.  Mark those CPUs for update.
	 */
	for_each_online_cpu(cpu) {
		if (rmap->near[cpu].index == index) {
			rmap->near[cpu].dist = CPU_RMAP_DIST_INF;
			cpumask_set_cpu(cpu, update_mask);
		}
	}

	debug_print_rmap(rmap, "after invalidating old distances");

	/* Set distance to 0 for all CPUs in the new affinity mask.
	 * Mark all CPUs within their NUMA nodes for update.
	 */
	for_each_cpu(cpu, affinity) {
		rmap->near[cpu].index = index;
		rmap->near[cpu].dist = 0;
		cpumask_or(update_mask, update_mask,
			   cpumask_of_node(cpu_to_node(cpu)));
	}

	debug_print_rmap(rmap, "after updating neighbours");

	/* Update distances based on topology */
	for_each_cpu(cpu, update_mask) {
		if (cpu_rmap_copy_neigh(rmap, cpu,
					topology_thread_cpumask(cpu), 1))
			continue;
		if (cpu_rmap_copy_neigh(rmap, cpu,
					topology_core_cpumask(cpu), 2))
			continue;
		if (cpu_rmap_copy_neigh(rmap, cpu,
					cpumask_of_node(cpu_to_node(cpu)), 3))
			continue;
		/* We could continue into NUMA node distances, but for now
		 * we give up.
		 */
	}

	debug_print_rmap(rmap, "after copying neighbours");

	free_cpumask_var(update_mask);
	return 0;
}
EXPORT_SYMBOL(cpu_rmap_update);

#ifdef CONFIG_GENERIC_HARDIRQS

/* Glue between IRQ affinity notifiers and CPU rmaps */

struct irq_glue {
	struct irq_affinity_notify notify;
	struct cpu_rmap *rmap;
	u16 index;
};

/**
 * free_irq_cpu_rmap - free a CPU affinity reverse-map used for IRQs
 * @rmap: Reverse-map allocated with alloc_irq_cpu_map(), or %NULL
 *
 * Must be called in process context, before freeing the IRQs, and
 * without holding any locks required by global workqueue items.
 */
void free_irq_cpu_rmap(struct cpu_rmap *rmap)
{
	struct irq_glue *glue;
	u16 index;

	if (!rmap)
		return;

	for (index = 0; index < rmap->used; index++) {
		glue = rmap->obj[index];
		irq_set_affinity_notifier(glue->notify.irq, NULL);
	}
	irq_run_affinity_notifiers();

	kfree(rmap);
}
EXPORT_SYMBOL(free_irq_cpu_rmap);

static void
irq_cpu_rmap_notify(struct irq_affinity_notify *notify, const cpumask_t *mask)
{
	struct irq_glue *glue =
		container_of(notify, struct irq_glue, notify);
	int rc;

	rc = cpu_rmap_update(glue->rmap, glue->index, mask);
	if (rc)
		pr_warning("irq_cpu_rmap_notify: update failed: %d\n", rc);
}

static void irq_cpu_rmap_release(struct kref *ref)
{
	struct irq_glue *glue =
		container_of(ref, struct irq_glue, notify.kref);
	kfree(glue);
}

/**
 * irq_cpu_rmap_add - add an IRQ to a CPU affinity reverse-map
 * @rmap: The reverse-map
 * @irq: The IRQ number
 *
 * This adds an IRQ affinity notifier that will update the reverse-map
 * automatically.
 *
 * Must be called in process context, after the IRQ is allocated but
 * before it is bound with request_irq().
 */
int irq_cpu_rmap_add(struct cpu_rmap *rmap, int irq)
{
	struct irq_glue *glue = kzalloc(sizeof(*glue), GFP_KERNEL);
	int rc;

	if (!glue)
		return -ENOMEM;
	glue->notify.notify = irq_cpu_rmap_notify;
	glue->notify.release = irq_cpu_rmap_release;
	glue->rmap = rmap;
	glue->index = cpu_rmap_add(rmap, glue);
	rc = irq_set_affinity_notifier(irq, &glue->notify);
	if (rc)
		kfree(glue);
	return rc;
}
EXPORT_SYMBOL(irq_cpu_rmap_add);

#endif /* CONFIG_GENERIC_HARDIRQS */
