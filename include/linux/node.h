/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/yesde.h - generic yesde definition
 *
 * This is mainly for topological representation. We define the 
 * basic 'struct yesde' here, which can be embedded in per-arch 
 * definitions of processors.
 *
 * Basic handling of the devices is done in drivers/base/yesde.c
 * and system devices are handled in drivers/base/sys.c. 
 *
 * Nodes are exported via driverfs in the class/yesde/devices/
 * directory. 
 */
#ifndef _LINUX_NODE_H_
#define _LINUX_NODE_H_

#include <linux/device.h>
#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/workqueue.h>

/**
 * struct yesde_hmem_attrs - heterogeneous memory performance attributes
 *
 * @read_bandwidth:	Read bandwidth in MB/s
 * @write_bandwidth:	Write bandwidth in MB/s
 * @read_latency:	Read latency in nayesseconds
 * @write_latency:	Write latency in nayesseconds
 */
struct yesde_hmem_attrs {
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
 * struct yesde_cache_attrs - system memory caching attributes
 *
 * @indexing:		The ways memory blocks may be placed in cache
 * @write_policy:	Write back or write through policy
 * @size:		Total size of cache in bytes
 * @line_size:		Number of bytes fetched on a cache miss
 * @level:		The cache hierarchy level
 */
struct yesde_cache_attrs {
	enum cache_indexing indexing;
	enum cache_write_policy write_policy;
	u64 size;
	u16 line_size;
	u8 level;
};

#ifdef CONFIG_HMEM_REPORTING
void yesde_add_cache(unsigned int nid, struct yesde_cache_attrs *cache_attrs);
void yesde_set_perf_attrs(unsigned int nid, struct yesde_hmem_attrs *hmem_attrs,
			 unsigned access);
#else
static inline void yesde_add_cache(unsigned int nid,
				  struct yesde_cache_attrs *cache_attrs)
{
}

static inline void yesde_set_perf_attrs(unsigned int nid,
				       struct yesde_hmem_attrs *hmem_attrs,
				       unsigned access)
{
}
#endif

struct yesde {
	struct device	dev;
	struct list_head access_list;

#if defined(CONFIG_MEMORY_HOTPLUG_SPARSE) && defined(CONFIG_HUGETLBFS)
	struct work_struct	yesde_work;
#endif
#ifdef CONFIG_HMEM_REPORTING
	struct list_head cache_attrs;
	struct device *cache_dev;
#endif
};

struct memory_block;
extern struct yesde *yesde_devices[];
typedef  void (*yesde_registration_func_t)(struct yesde *);

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

extern void unregister_yesde(struct yesde *yesde);
#ifdef CONFIG_NUMA
/* Core of the yesde registration - only memory hotplug should use this */
extern int __register_one_yesde(int nid);

/* Registers an online yesde */
static inline int register_one_yesde(int nid)
{
	int error = 0;

	if (yesde_online(nid)) {
		struct pglist_data *pgdat = NODE_DATA(nid);
		unsigned long start_pfn = pgdat->yesde_start_pfn;
		unsigned long end_pfn = start_pfn + pgdat->yesde_spanned_pages;

		error = __register_one_yesde(nid);
		if (error)
			return error;
		/* link memory sections under this yesde */
		error = link_mem_sections(nid, start_pfn, end_pfn);
	}

	return error;
}

extern void unregister_one_yesde(int nid);
extern int register_cpu_under_yesde(unsigned int cpu, unsigned int nid);
extern int unregister_cpu_under_yesde(unsigned int cpu, unsigned int nid);
extern void unregister_memory_block_under_yesdes(struct memory_block *mem_blk);

extern int register_memory_yesde_under_compute_yesde(unsigned int mem_nid,
						   unsigned int cpu_nid,
						   unsigned access);

#ifdef CONFIG_HUGETLBFS
extern void register_hugetlbfs_with_yesde(yesde_registration_func_t doregister,
					 yesde_registration_func_t unregister);
#endif
#else
static inline int __register_one_yesde(int nid)
{
	return 0;
}
static inline int register_one_yesde(int nid)
{
	return 0;
}
static inline int unregister_one_yesde(int nid)
{
	return 0;
}
static inline int register_cpu_under_yesde(unsigned int cpu, unsigned int nid)
{
	return 0;
}
static inline int unregister_cpu_under_yesde(unsigned int cpu, unsigned int nid)
{
	return 0;
}
static inline void unregister_memory_block_under_yesdes(struct memory_block *mem_blk)
{
}

static inline void register_hugetlbfs_with_yesde(yesde_registration_func_t reg,
						yesde_registration_func_t unreg)
{
}
#endif

#define to_yesde(device) container_of(device, struct yesde, dev)

#endif /* _LINUX_NODE_H_ */
