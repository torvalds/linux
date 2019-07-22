/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/node.h - generic node definition
 *
 * This is mainly for topological representation. We define the 
 * basic 'struct node' here, which can be embedded in per-arch 
 * definitions of processors.
 *
 * Basic handling of the devices is done in drivers/base/node.c
 * and system devices are handled in drivers/base/sys.c. 
 *
 * Nodes are exported via driverfs in the class/node/devices/
 * directory. 
 */
#ifndef _LINUX_NODE_H_
#define _LINUX_NODE_H_

#include <linux/device.h>
#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/workqueue.h>

/**
 * struct node_hmem_attrs - heterogeneous memory performance attributes
 *
 * @read_bandwidth:	Read bandwidth in MB/s
 * @write_bandwidth:	Write bandwidth in MB/s
 * @read_latency:	Read latency in nanoseconds
 * @write_latency:	Write latency in nanoseconds
 */
struct node_hmem_attrs {
	unsigned int read_bandwidth;
	unsigned int write_bandwidth;
	unsigned int read_latency;
	unsigned int write_latency;
};

enum cache_indexing {
	NODE_CACHE_DIRECT_MAP,
	NODE_CACHE_INDEXED,
	NODE_CACHE_OTHER,
};

enum cache_write_policy {
	NODE_CACHE_WRITE_BACK,
	NODE_CACHE_WRITE_THROUGH,
	NODE_CACHE_WRITE_OTHER,
};

/**
 * struct node_cache_attrs - system memory caching attributes
 *
 * @indexing:		The ways memory blocks may be placed in cache
 * @write_policy:	Write back or write through policy
 * @size:		Total size of cache in bytes
 * @line_size:		Number of bytes fetched on a cache miss
 * @level:		The cache hierarchy level
 */
struct node_cache_attrs {
	enum cache_indexing indexing;
	enum cache_write_policy write_policy;
	u64 size;
	u16 line_size;
	u8 level;
};

#ifdef CONFIG_HMEM_REPORTING
void node_add_cache(unsigned int nid, struct node_cache_attrs *cache_attrs);
void node_set_perf_attrs(unsigned int nid, struct node_hmem_attrs *hmem_attrs,
			 unsigned access);
#else
static inline void node_add_cache(unsigned int nid,
				  struct node_cache_attrs *cache_attrs)
{
}

static inline void node_set_perf_attrs(unsigned int nid,
				       struct node_hmem_attrs *hmem_attrs,
				       unsigned access)
{
}
#endif

struct node {
	struct device	dev;
	struct list_head access_list;

#if defined(CONFIG_MEMORY_HOTPLUG_SPARSE) && defined(CONFIG_HUGETLBFS)
	struct work_struct	node_work;
#endif
#ifdef CONFIG_HMEM_REPORTING
	struct list_head cache_attrs;
	struct device *cache_dev;
#endif
};

struct memory_block;
extern struct node *node_devices[];
typedef  void (*node_registration_func_t)(struct node *);

#if defined(CONFIG_MEMORY_HOTPLUG_SPARSE) && defined(CONFIG_NUMA)
extern int link_mem_sections(int nid, unsigned long start_pfn,
			     unsigned long end_pfn);
#else
static inline int link_mem_sections(int nid, unsigned long start_pfn,
				    unsigned long end_pfn)
{
	return 0;
}
#endif

extern void unregister_node(struct node *node);
#ifdef CONFIG_NUMA
/* Core of the node registration - only memory hotplug should use this */
extern int __register_one_node(int nid);

/* Registers an online node */
static inline int register_one_node(int nid)
{
	int error = 0;

	if (node_online(nid)) {
		struct pglist_data *pgdat = NODE_DATA(nid);
		unsigned long start_pfn = pgdat->node_start_pfn;
		unsigned long end_pfn = start_pfn + pgdat->node_spanned_pages;

		error = __register_one_node(nid);
		if (error)
			return error;
		/* link memory sections under this node */
		error = link_mem_sections(nid, start_pfn, end_pfn);
	}

	return error;
}

extern void unregister_one_node(int nid);
extern int register_cpu_under_node(unsigned int cpu, unsigned int nid);
extern int unregister_cpu_under_node(unsigned int cpu, unsigned int nid);
extern void unregister_memory_block_under_nodes(struct memory_block *mem_blk);

extern int register_memory_node_under_compute_node(unsigned int mem_nid,
						   unsigned int cpu_nid,
						   unsigned access);

#ifdef CONFIG_HUGETLBFS
extern void register_hugetlbfs_with_node(node_registration_func_t doregister,
					 node_registration_func_t unregister);
#endif
#else
static inline int __register_one_node(int nid)
{
	return 0;
}
static inline int register_one_node(int nid)
{
	return 0;
}
static inline int unregister_one_node(int nid)
{
	return 0;
}
static inline int register_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	return 0;
}
static inline int unregister_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	return 0;
}
static inline void unregister_memory_block_under_nodes(struct memory_block *mem_blk)
{
}

static inline void register_hugetlbfs_with_node(node_registration_func_t reg,
						node_registration_func_t unreg)
{
}
#endif

#define to_node(device) container_of(device, struct node, dev)

#endif /* _LINUX_NODE_H_ */
