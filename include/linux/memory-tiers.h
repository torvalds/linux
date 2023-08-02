/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MEMORY_TIERS_H
#define _LINUX_MEMORY_TIERS_H

#include <linux/types.h>
#include <linux/nodemask.h>
#include <linux/kref.h>
#include <linux/mmzone.h>
/*
 * Each tier cover a abstrace distance chunk size of 128
 */
#define MEMTIER_CHUNK_BITS	7
#define MEMTIER_CHUNK_SIZE	(1 << MEMTIER_CHUNK_BITS)
/*
 * Smaller abstract distance values imply faster (higher) memory tiers. Offset
 * the DRAM adistance so that we can accommodate devices with a slightly lower
 * adistance value (slightly faster) than default DRAM adistance to be part of
 * the same memory tier.
 */
#define MEMTIER_ADISTANCE_DRAM	((4 * MEMTIER_CHUNK_SIZE) + (MEMTIER_CHUNK_SIZE >> 1))

struct memory_tier;
struct memory_dev_type {
	/* list of memory types that are part of same tier as this type */
	struct list_head tier_sibling;
	/* abstract distance for this specific memory type */
	int adistance;
	/* Nodes of same abstract distance */
	nodemask_t nodes;
	struct kref kref;
};

#ifdef CONFIG_NUMA
extern bool numa_demotion_enabled;
struct memory_dev_type *alloc_memory_type(int adistance);
void put_memory_type(struct memory_dev_type *memtype);
void init_node_memory_type(int node, struct memory_dev_type *default_type);
void clear_node_memory_type(int node, struct memory_dev_type *memtype);
#ifdef CONFIG_MIGRATION
int next_demotion_node(int node);
void node_get_allowed_targets(pg_data_t *pgdat, nodemask_t *targets);
bool node_is_toptier(int node);
#else
static inline int next_demotion_node(int node)
{
	return NUMA_NO_NODE;
}

static inline void node_get_allowed_targets(pg_data_t *pgdat, nodemask_t *targets)
{
	*targets = NODE_MASK_NONE;
}

static inline bool node_is_toptier(int node)
{
	return true;
}
#endif

#else

#define numa_demotion_enabled	false
/*
 * CONFIG_NUMA implementation returns non NULL error.
 */
static inline struct memory_dev_type *alloc_memory_type(int adistance)
{
	return NULL;
}

static inline void put_memory_type(struct memory_dev_type *memtype)
{

}

static inline void init_node_memory_type(int node, struct memory_dev_type *default_type)
{

}

static inline void clear_node_memory_type(int node, struct memory_dev_type *memtype)
{

}

static inline int next_demotion_node(int node)
{
	return NUMA_NO_NODE;
}

static inline void node_get_allowed_targets(pg_data_t *pgdat, nodemask_t *targets)
{
	*targets = NODE_MASK_NONE;
}

static inline bool node_is_toptier(int node)
{
	return true;
}
#endif	/* CONFIG_NUMA */
#endif  /* _LINUX_MEMORY_TIERS_H */
