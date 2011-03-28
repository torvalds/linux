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
#include <linux/mutex.h>

struct memory_block {
	unsigned long start_section_nr;
	unsigned long end_section_nr;
	unsigned long state;
	int section_count;

	/*
	 * This serializes all state change requests.  It isn't
	 * held during creation because the control files are
	 * created long after the critical areas during
	 * initialization.
	 */
	struct mutex state_mutex;
	int phys_device;		/* to which fru does this belong? */
	void *hw;			/* optional pointer to fw/hw data */
	int (*phys_callback)(struct memory_block *);
	struct sys_device sysdev;
};

int arch_get_memory_phys_device(unsigned long start_pfn);

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
	int status_change_nid;
};

/*
 * During pageblock isolation, count the number of pages within the
 * range [start_pfn, start_pfn + nr_pages) which are owned by code
 * in the notifier chain.
 */
#define MEM_ISOLATE_COUNT	(1<<0)

struct memory_isolate_notify {
	unsigned long start_pfn;	/* Start of range to check */
	unsigned int nr_pages;		/* # pages in range to check */
	unsigned int pages_found;	/* # pages owned found by callbacks */
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
static inline int memory_notify(unsigned long val, void *v)
{
	return 0;
}
static inline int register_memory_isolate_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline void unregister_memory_isolate_notifier(struct notifier_block *nb)
{
}
static inline int memory_isolate_notify(unsigned long val, void *v)
{
	return 0;
}
#else
extern int register_memory_notifier(struct notifier_block *nb);
extern void unregister_memory_notifier(struct notifier_block *nb);
extern int register_memory_isolate_notifier(struct notifier_block *nb);
extern void unregister_memory_isolate_notifier(struct notifier_block *nb);
extern int register_new_memory(int, struct mem_section *);
extern int unregister_memory_section(struct mem_section *);
extern int memory_dev_init(void);
extern int remove_memory_block(unsigned long, struct mem_section *, int);
extern int memory_notify(unsigned long val, void *v);
extern int memory_isolate_notify(unsigned long val, void *v);
extern struct memory_block *find_memory_block_hinted(struct mem_section *,
							struct memory_block *);
extern struct memory_block *find_memory_block(struct mem_section *);
#define CONFIG_MEM_BLOCK_SIZE	(PAGES_PER_SECTION<<PAGE_SHIFT)
enum mem_add_context { BOOT, HOTPLUG };
#endif /* CONFIG_MEMORY_HOTPLUG_SPARSE */

#ifdef CONFIG_MEMORY_HOTPLUG
#define hotplug_memory_notifier(fn, pri) {			\
	static __meminitdata struct notifier_block fn##_mem_nb =\
		{ .notifier_call = fn, .priority = pri };	\
	register_memory_notifier(&fn##_mem_nb);			\
}
#else
#define hotplug_memory_notifier(fn, pri) do { } while (0)
#endif

/*
 * 'struct memory_accessor' is a generic interface to provide
 * in-kernel access to persistent memory such as i2c or SPI EEPROMs
 */
struct memory_accessor {
	ssize_t (*read)(struct memory_accessor *, char *buf, off_t offset,
			size_t count);
	ssize_t (*write)(struct memory_accessor *, const char *buf,
			 off_t offset, size_t count);
};

/*
 * Kernel text modification mutex, used for code patching. Users of this lock
 * can sleep.
 */
extern struct mutex text_mutex;

#endif /* _LINUX_MEMORY_H_ */
