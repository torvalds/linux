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

#include <linux/sysdev.h>
#include <linux/node.h>
#include <linux/compiler.h>

#include <asm/semaphore.h>

struct memory_block {
	unsigned long phys_index;
	unsigned long state;
	/*
	 * This serializes all state change requests.  It isn't
	 * held during creation because the control files are
	 * created long after the critical areas during
	 * initialization.
	 */
	struct semaphore state_sem;
	int phys_device;		/* to which fru does this belong? */
	void *hw;			/* optional pointer to fw/hw data */
	int (*phys_callback)(struct memory_block *);
	struct sys_device sysdev;
};

/* These states are exposed to userspace as text strings in sysfs */
#define	MEM_ONLINE		(1<<0) /* exposed to userspace */
#define	MEM_GOING_OFFLINE	(1<<1) /* exposed to userspace */
#define	MEM_OFFLINE		(1<<2) /* exposed to userspace */

/*
 * All of these states are currently kernel-internal for notifying
 * kernel components and architectures.
 *
 * For MEM_MAPPING_INVALID, all notifier chains with priority >0
 * are called before pfn_to_page() becomes invalid.  The priority=0
 * entry is reserved for the function that actually makes
 * pfn_to_page() stop working.  Any notifiers that want to be called
 * after that should have priority <0.
 */
#define	MEM_MAPPING_INVALID	(1<<3)

struct notifier_block;
struct mem_section;

#ifndef CONFIG_MEMORY_HOTPLUG_SPARSE
static inline int memory_dev_init(void)
{
	return 0;
}
static inline int register_memory_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline void unregister_memory_notifier(struct notifier_block *nb)
{
}
#else
extern int register_new_memory(struct mem_section *);
extern int unregister_memory_section(struct mem_section *);
extern int memory_dev_init(void);
extern int remove_memory_block(unsigned long, struct mem_section *, int);

#define CONFIG_MEM_BLOCK_SIZE	(PAGES_PER_SECTION<<PAGE_SHIFT)


#endif /* CONFIG_MEMORY_HOTPLUG_SPARSE */

#define hotplug_memory_notifier(fn, pri) {			\
	static struct notifier_block fn##_mem_nb =		\
		{ .notifier_call = fn, .priority = pri };	\
	register_memory_notifier(&fn##_mem_nb);			\
}

#endif /* _LINUX_MEMORY_H_ */
