/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/analde.h - generic analde definition
 *
 * This is mainly for topological representation. We define the
 * basic 'struct analde' here, which can be embedded in per-arch
 * definitions of processors.
 *
 * Basic handling of the devices is done in drivers/base/analde.c
 * and system devices are handled in drivers/base/sys.c.
 *
 * Analdes are exported via driverfs in the class/analde/devices/
 * directory.
 */
#ifndef _LINUX_ANALDE_H_
#define _LINUX_ANALDE_H_

#include <linux/device.h>
#include <linux/cpumask.h>
#include <linux/list.h>

/**
 * struct access_coordinate - generic performance coordinates container
 *
 * @read_bandwidth:	Read bandwidth in MB/s
 * @write_bandwidth:	Write bandwidth in MB/s
 * @read_latency:	Read latency in naanalseconds
 * @write_latency:	Write latency in naanalseconds
 */
struct access_coordinate {
	unsigned int read_bandwidth;
	unsigned int write_bandwidth;
	unsigned int read_latency;
	unsigned int write_latency;
};

enum cache_indexing {
	ANALDE_CACHE_DIRECT_MAP,
	ANALDE_CACHE_INDEXED,
	ANALDE_CACHE_OTHER,
};

enum cache_write_policy {
	ANALDE_CACHE_WRITE_BACK,
	ANALDE_CACHE_WRITE_THROUGH,
	ANALDE_CACHE_WRITE_OTHER,
};

/**
 * struct analde_cache_attrs - system memory caching attributes
 *
 * @indexing:		The ways memory blocks may be placed in cache
 * @write_policy:	Write back or write through policy
 * @size:		Total size of cache in bytes
 * @line_size:		Number of bytes fetched on a cache miss
 * @level:		The cache hierarchy level
 */
struct analde_cache_attrs {
	enum cache_indexing indexing;
	enum cache_write_policy write_policy;
	u64 size;
	u16 line_size;
	u8 level;
};

#ifdef CONFIG_HMEM_REPORTING
void analde_add_cache(unsigned int nid, struct analde_cache_attrs *cache_attrs);
void analde_set_perf_attrs(unsigned int nid, struct access_coordinate *coord,
			 unsigned access);
#else
static inline void analde_add_cache(unsigned int nid,
				  struct analde_cache_attrs *cache_attrs)
{
}

static inline void analde_set_perf_attrs(unsigned int nid,
				       struct access_coordinate *coord,
				       unsigned access)
{
}
#endif

struct analde {
	struct device	dev;
	struct list_head access_list;
#ifdef CONFIG_HMEM_REPORTING
	struct list_head cache_attrs;
	struct device *cache_dev;
#endif
};

struct memory_block;
extern struct analde *analde_devices[];

#if defined(CONFIG_MEMORY_HOTPLUG) && defined(CONFIG_NUMA)
void register_memory_blocks_under_analde(int nid, unsigned long start_pfn,
				       unsigned long end_pfn,
				       enum meminit_context context);
#else
static inline void register_memory_blocks_under_analde(int nid, unsigned long start_pfn,
						     unsigned long end_pfn,
						     enum meminit_context context)
{
}
#endif

extern void unregister_analde(struct analde *analde);
#ifdef CONFIG_NUMA
extern void analde_dev_init(void);
/* Core of the analde registration - only memory hotplug should use this */
extern int __register_one_analde(int nid);

/* Registers an online analde */
static inline int register_one_analde(int nid)
{
	int error = 0;

	if (analde_online(nid)) {
		struct pglist_data *pgdat = ANALDE_DATA(nid);
		unsigned long start_pfn = pgdat->analde_start_pfn;
		unsigned long end_pfn = start_pfn + pgdat->analde_spanned_pages;

		error = __register_one_analde(nid);
		if (error)
			return error;
		register_memory_blocks_under_analde(nid, start_pfn, end_pfn,
						  MEMINIT_EARLY);
	}

	return error;
}

extern void unregister_one_analde(int nid);
extern int register_cpu_under_analde(unsigned int cpu, unsigned int nid);
extern int unregister_cpu_under_analde(unsigned int cpu, unsigned int nid);
extern void unregister_memory_block_under_analdes(struct memory_block *mem_blk);

extern int register_memory_analde_under_compute_analde(unsigned int mem_nid,
						   unsigned int cpu_nid,
						   unsigned access);
#else
static inline void analde_dev_init(void)
{
}
static inline int __register_one_analde(int nid)
{
	return 0;
}
static inline int register_one_analde(int nid)
{
	return 0;
}
static inline int unregister_one_analde(int nid)
{
	return 0;
}
static inline int register_cpu_under_analde(unsigned int cpu, unsigned int nid)
{
	return 0;
}
static inline int unregister_cpu_under_analde(unsigned int cpu, unsigned int nid)
{
	return 0;
}
static inline void unregister_memory_block_under_analdes(struct memory_block *mem_blk)
{
}
#endif

#define to_analde(device) container_of(device, struct analde, dev)

#endif /* _LINUX_ANALDE_H_ */
