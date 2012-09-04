#ifndef __LINUX_CPU_RMAP_H
#define __LINUX_CPU_RMAP_H

/*
 * cpu_rmap.c: CPU affinity reverse-map support
 * Copyright 2011 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/cpumask.h>
#include <linux/gfp.h>
#include <linux/slab.h>

/**
 * struct cpu_rmap - CPU affinity reverse-map
 * @size: Number of objects to be reverse-mapped
 * @used: Number of objects added
 * @obj: Pointer to array of object pointers
 * @near: For each CPU, the index and distance to the nearest object,
 *      based on affinity masks
 */
struct cpu_rmap {
	u16		size, used;
	void		**obj;
	struct {
		u16	index;
		u16	dist;
	}		near[0];
};
#define CPU_RMAP_DIST_INF 0xffff

extern struct cpu_rmap *alloc_cpu_rmap(unsigned int size, gfp_t flags);

/**
 * free_cpu_rmap - free CPU affinity reverse-map
 * @rmap: Reverse-map allocated with alloc_cpu_rmap(), or %NULL
 */
static inline void free_cpu_rmap(struct cpu_rmap *rmap)
{
	kfree(rmap);
}

extern int cpu_rmap_add(struct cpu_rmap *rmap, void *obj);
extern int cpu_rmap_update(struct cpu_rmap *rmap, u16 index,
			   const struct cpumask *affinity);

static inline u16 cpu_rmap_lookup_index(struct cpu_rmap *rmap, unsigned int cpu)
{
	return rmap->near[cpu].index;
}

static inline void *cpu_rmap_lookup_obj(struct cpu_rmap *rmap, unsigned int cpu)
{
	return rmap->obj[rmap->near[cpu].index];
}

#ifdef CONFIG_GENERIC_HARDIRQS

/**
 * alloc_irq_cpu_rmap - allocate CPU affinity reverse-map for IRQs
 * @size: Number of objects to be mapped
 *
 * Must be called in process context.
 */
static inline struct cpu_rmap *alloc_irq_cpu_rmap(unsigned int size)
{
	return alloc_cpu_rmap(size, GFP_KERNEL);
}
extern void free_irq_cpu_rmap(struct cpu_rmap *rmap);

extern int irq_cpu_rmap_add(struct cpu_rmap *rmap, int irq);

#endif
#endif /* __LINUX_CPU_RMAP_H */
