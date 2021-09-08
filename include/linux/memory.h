/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/memory.h - generic memory definition
 *
 * This is mainly for topological representation. We define the
 * basic "struct memory_block" here, which can be embedded in per-arch
 * definitions or NUMA information.
 *
 * Basic handling of the devices is done in drivers/base/memory.c
 * and system devices are handled in drivers/base/sys.c.
 *
 * Memory block are exported via sysfs in the class/memory/devices/
 * directory.
 *
 */
#ifndef _LINUX_MEMORY_H_
#define _LINUX_MEMORY_H_

#include <linux/node.h>
#include <linux/compiler.h>
#include <linux/mutex.h>
#include <linux/notifier.h>

#define MIN_MEMORY_BLOCK_SIZE     (1UL << SECTION_SIZE_BITS)

/**
 * struct memory_group - a logical group of memory blocks
 * @nid: The node id for all memory blocks inside the memory group.
 * @blocks: List of all memory blocks belonging to this memory group.
 * @present_kernel_pages: Present (online) memory outside ZONE_MOVABLE of this
 *			  memory group.
 * @present_movable_pages: Present (online) memory in ZONE_MOVABLE of this
 *			   memory group.
 * @is_dynamic: The memory group type: static vs. dynamic
 * @s.max_pages: Valid with &memory_group.is_dynamic == false. The maximum
 *		 number of pages we'll have in this static memory group.
 * @d.unit_pages: Valid with &memory_group.is_dynamic == true. Unit in pages
 *		  in which memory is added/removed in this dynamic memory group.
 *		  This granularity defines the alignment of a unit in physical
 *		  address space; it has to be at least as big as a single
 *		  memory block.
 *
 * A memory group logically groups memory blocks; each memory block
 * belongs to at most one memory group. A memory group corresponds to
 * a memory device, such as a DIMM or a NUMA node, which spans multiple
 * memory blocks and might even span multiple non-contiguous physical memory
 * ranges.
 *
 * Modification of members after registration is serialized by memory
 * hot(un)plug code.
 */
struct memory_group {
	int nid;
	struct list_head memory_blocks;
	unsigned long present_kernel_pages;
	unsigned long present_movable_pages;
	bool is_dynamic;
	union {
		struct {
			unsigned long max_pages;
		} s;
		struct {
			unsigned long unit_pages;
		} d;
	};
};

struct memory_block {
	unsigned long start_section_nr;
	unsigned long state;		/* serialized by the dev->lock */
	int online_type;		/* for passing data to online routine */
	int nid;			/* NID for this memory block */
	struct device dev;
	/*
	 * Number of vmemmap pages. These pages
	 * lay at the beginning of the memory block.
	 */
	unsigned long nr_vmemmap_pages;
	struct memory_group *group;	/* group (if any) for this block */
	struct list_head group_next;	/* next block inside memory group */
};

int arch_get_memory_phys_device(unsigned long start_pfn);
unsigned long memory_block_size_bytes(void);
int set_memory_block_size_order(unsigned int order);

/* These states are exposed to userspace as text strings in sysfs */
#define	MEM_ONLINE		(1<<0) /* exposed to userspace */
#define	MEM_GOING_OFFLINE	(1<<1) /* exposed to userspace */
#define	MEM_OFFLINE		(1<<2) /* exposed to userspace */
#define	MEM_GOING_ONLINE	(1<<3)
#define	MEM_CANCEL_ONLINE	(1<<4)
#define	MEM_CANCEL_OFFLINE	(1<<5)

struct memory_notify {
	unsigned long start_pfn;
	unsigned long nr_pages;
	int status_change_nid_normal;
	int status_change_nid_high;
	int status_change_nid;
};

struct notifier_block;
struct mem_section;

/*
 * Priorities for the hotplug memory callback routines (stored in decreasing
 * order in the callback chain)
 */
#define SLAB_CALLBACK_PRI       1
#define IPC_CALLBACK_PRI        10

#ifndef CONFIG_MEMORY_HOTPLUG_SPARSE
static inline void memory_dev_init(void)
{
	return;
}
static inline int register_memory_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline void unregister_memory_notifier(struct notifier_block *nb)
{
}
static inline int memory_notify(unsigned long val, void *v)
{
	return 0;
}
#else
extern int register_memory_notifier(struct notifier_block *nb);
extern void unregister_memory_notifier(struct notifier_block *nb);
int create_memory_block_devices(unsigned long start, unsigned long size,
				unsigned long vmemmap_pages,
				struct memory_group *group);
void remove_memory_block_devices(unsigned long start, unsigned long size);
extern void memory_dev_init(void);
extern int memory_notify(unsigned long val, void *v);
extern struct memory_block *find_memory_block(struct mem_section *);
typedef int (*walk_memory_blocks_func_t)(struct memory_block *, void *);
extern int walk_memory_blocks(unsigned long start, unsigned long size,
			      void *arg, walk_memory_blocks_func_t func);
extern int for_each_memory_block(void *arg, walk_memory_blocks_func_t func);
#define CONFIG_MEM_BLOCK_SIZE	(PAGES_PER_SECTION<<PAGE_SHIFT)

extern int memory_group_register_static(int nid, unsigned long max_pages);
extern int memory_group_register_dynamic(int nid, unsigned long unit_pages);
extern int memory_group_unregister(int mgid);
struct memory_group *memory_group_find_by_id(int mgid);
typedef int (*walk_memory_groups_func_t)(struct memory_group *, void *);
int walk_dynamic_memory_groups(int nid, walk_memory_groups_func_t func,
			       struct memory_group *excluded, void *arg);
#endif /* CONFIG_MEMORY_HOTPLUG_SPARSE */

#ifdef CONFIG_MEMORY_HOTPLUG
#define hotplug_memory_notifier(fn, pri) ({		\
	static __meminitdata struct notifier_block fn##_mem_nb =\
		{ .notifier_call = fn, .priority = pri };\
	register_memory_notifier(&fn##_mem_nb);			\
})
#define register_hotmemory_notifier(nb)		register_memory_notifier(nb)
#define unregister_hotmemory_notifier(nb) 	unregister_memory_notifier(nb)
#else
#define hotplug_memory_notifier(fn, pri)	({ 0; })
/* These aren't inline functions due to a GCC bug. */
#define register_hotmemory_notifier(nb)    ({ (void)(nb); 0; })
#define unregister_hotmemory_notifier(nb)  ({ (void)(nb); })
#endif

/*
 * Kernel text modification mutex, used for code patching. Users of this lock
 * can sleep.
 */
extern struct mutex text_mutex;

#endif /* _LINUX_MEMORY_H_ */
