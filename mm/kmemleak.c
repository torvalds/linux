// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/kmemleak.c
 *
 * Copyright (C) 2008 ARM Limited
 * Written by Catalin Marinas <catalin.marinas@arm.com>
 *
 * For more information on the algorithm and kmemleak usage, please see
 * Documentation/dev-tools/kmemleak.rst.
 *
 * Notes on locking
 * ----------------
 *
 * The following locks and mutexes are used by kmemleak:
 *
 * - kmemleak_lock (raw_spinlock_t): protects the object_list modifications and
 *   accesses to the object_tree_root. The object_list is the main list
 *   holding the metadata (struct kmemleak_object) for the allocated memory
 *   blocks. The object_tree_root is a red black tree used to look-up
 *   metadata based on a pointer to the corresponding memory block.  The
 *   kmemleak_object structures are added to the object_list and
 *   object_tree_root in the create_object() function called from the
 *   kmemleak_alloc() callback and removed in delete_object() called from the
 *   kmemleak_free() callback
 * - kmemleak_object.lock (raw_spinlock_t): protects a kmemleak_object.
 *   Accesses to the metadata (e.g. count) are protected by this lock. Note
 *   that some members of this structure may be protected by other means
 *   (atomic or kmemleak_lock). This lock is also held when scanning the
 *   corresponding memory block to avoid the kernel freeing it via the
 *   kmemleak_free() callback. This is less heavyweight than holding a global
 *   lock like kmemleak_lock during scanning.
 * - scan_mutex (mutex): ensures that only one thread may scan the memory for
 *   unreferenced objects at a time. The gray_list contains the objects which
 *   are already referenced or marked as false positives and need to be
 *   scanned. This list is only modified during a scanning episode when the
 *   scan_mutex is held. At the end of a scan, the gray_list is always empty.
 *   Note that the kmemleak_object.use_count is incremented when an object is
 *   added to the gray_list and therefore cannot be freed. This mutex also
 *   prevents multiple users of the "kmemleak" debugfs file together with
 *   modifications to the memory scanning parameters including the scan_thread
 *   pointer
 *
 * Locks and mutexes are acquired/nested in the following order:
 *
 *   scan_mutex [-> object->lock] -> kmemleak_lock -> other_object->lock (SINGLE_DEPTH_NESTING)
 *
 * No kmemleak_lock and object->lock nesting is allowed outside scan_mutex
 * regions.
 *
 * The kmemleak_object structures have a use_count incremented or decremented
 * using the get_object()/put_object() functions. When the use_count becomes
 * 0, this count can no longer be incremented and put_object() schedules the
 * kmemleak_object freeing via an RCU callback. All calls to the get_object()
 * function must be protected by rcu_read_lock() to avoid accessing a freed
 * structure.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/kthread.h>
#include <linux/rbtree.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/stacktrace.h>
#include <linux/cache.h>
#include <linux/percpu.h>
#include <linux/memblock.h>
#include <linux/pfn.h>
#include <linux/mmzone.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/nodemask.h>
#include <linux/mm.h>
#include <linux/workqueue.h>
#include <linux/crc32.h>

#include <asm/sections.h>
#include <asm/processor.h>
#include <linux/atomic.h>

#include <linux/kasan.h>
#include <linux/kfence.h>
#include <linux/kmemleak.h>
#include <linux/memory_hotplug.h>

/*
 * Kmemleak configuration and common defines.
 */
#define MAX_TRACE		16	/* stack trace length */
#define MSECS_MIN_AGE		5000	/* minimum object age for reporting */
#define SECS_FIRST_SCAN		60	/* delay before the first scan */
#define SECS_SCAN_WAIT		600	/* subsequent auto scanning delay */
#define MAX_SCAN_SIZE		4096	/* maximum size of a scanned block */

#define BYTES_PER_POINTER	sizeof(void *)

/* GFP bitmask for kmemleak internal allocations */
#define gfp_kmemleak_mask(gfp)	(((gfp) & (GFP_KERNEL | GFP_ATOMIC | \
					   __GFP_NOLOCKDEP)) | \
				 __GFP_NORETRY | __GFP_NOMEMALLOC | \
				 __GFP_NOWARN)

/* scanning area inside a memory block */
struct kmemleak_scan_area {
	struct hlist_node node;
	unsigned long start;
	size_t size;
};

#define KMEMLEAK_GREY	0
#define KMEMLEAK_BLACK	-1

/*
 * Structure holding the metadata for each allocated memory block.
 * Modifications to such objects should be made while holding the
 * object->lock. Insertions or deletions from object_list, gray_list or
 * rb_node are already protected by the corresponding locks or mutex (see
 * the notes on locking above). These objects are reference-counted
 * (use_count) and freed using the RCU mechanism.
 */
struct kmemleak_object {
	raw_spinlock_t lock;
	unsigned int flags;		/* object status flags */
	struct list_head object_list;
	struct list_head gray_list;
	struct rb_node rb_node;
	struct rcu_head rcu;		/* object_list lockless traversal */
	/* object usage count; object freed when use_count == 0 */
	atomic_t use_count;
	unsigned long pointer;
	size_t size;
	/* pass surplus references to this pointer */
	unsigned long excess_ref;
	/* minimum number of a pointers found before it is considered leak */
	int min_count;
	/* the total number of pointers found pointing to this object */
	int count;
	/* checksum for detecting modified objects */
	u32 checksum;
	/* memory ranges to be scanned inside an object (empty for all) */
	struct hlist_head area_list;
	unsigned long trace[MAX_TRACE];
	unsigned int trace_len;
	unsigned long jiffies;		/* creation timestamp */
	pid_t pid;			/* pid of the current task */
	char comm[TASK_COMM_LEN];	/* executable name */
};

/* flag representing the memory block allocation status */
#define OBJECT_ALLOCATED	(1 << 0)
/* flag set after the first reporting of an unreference object */
#define OBJECT_REPORTED		(1 << 1)
/* flag set to not scan the object */
#define OBJECT_NO_SCAN		(1 << 2)
/* flag set to fully scan the object when scan_area allocation failed */
#define OBJECT_FULL_SCAN	(1 << 3)

#define HEX_PREFIX		"    "
/* number of bytes to print per line; must be 16 or 32 */
#define HEX_ROW_SIZE		16
/* number of bytes to print at a time (1, 2, 4, 8) */
#define HEX_GROUP_SIZE		1
/* include ASCII after the hex output */
#define HEX_ASCII		1
/* max number of lines to be printed */
#define HEX_MAX_LINES		2

/* the list of all allocated objects */
static LIST_HEAD(object_list);
/* the list of gray-colored objects (see color_gray comment below) */
static LIST_HEAD(gray_list);
/* memory pool allocation */
static struct kmemleak_object mem_pool[CONFIG_DEBUG_KMEMLEAK_MEM_POOL_SIZE];
static int mem_pool_free_count = ARRAY_SIZE(mem_pool);
static LIST_HEAD(mem_pool_free_list);
/* search tree for object boundaries */
static struct rb_root object_tree_root = RB_ROOT;
/* protecting the access to object_list and object_tree_root */
static DEFINE_RAW_SPINLOCK(kmemleak_lock);

/* allocation caches for kmemleak internal data */
static struct kmem_cache *object_cache;
static struct kmem_cache *scan_area_cache;

/* set if tracing memory operations is enabled */
static int kmemleak_enabled = 1;
/* same as above but only for the kmemleak_free() callback */
static int kmemleak_free_enabled = 1;
/* set in the late_initcall if there were no errors */
static int kmemleak_initialized;
/* set if a kmemleak warning was issued */
static int kmemleak_warning;
/* set if a fatal kmemleak error has occurred */
static int kmemleak_error;

/* minimum and maximum address that may be valid pointers */
static unsigned long min_addr = ULONG_MAX;
static unsigned long max_addr;

static struct task_struct *scan_thread;
/* used to avoid reporting of recently allocated objects */
static unsigned long jiffies_min_age;
static unsigned long jiffies_last_scan;
/* delay between automatic memory scannings */
static unsigned long jiffies_scan_wait;
/* enables or disables the task stacks scanning */
static int kmemleak_stack_scan = 1;
/* protects the memory scanning, parameters and debug/kmemleak file access */
static DEFINE_MUTEX(scan_mutex);
/* setting kmemleak=on, will set this var, skipping the disable */
static int kmemleak_skip_disable;
/* If there are leaks that can be reported */
static bool kmemleak_found_leaks;

static bool kmemleak_verbose;
module_param_named(verbose, kmemleak_verbose, bool, 0600);

static void kmemleak_disable(void);

/*
 * Print a warning and dump the stack trace.
 */
#define kmemleak_warn(x...)	do {		\
	pr_warn(x);				\
	dump_stack();				\
	kmemleak_warning = 1;			\
} while (0)

/*
 * Macro invoked when a serious kmemleak condition occurred and cannot be
 * recovered from. Kmemleak will be disabled and further allocation/freeing
 * tracing no longer available.
 */
#define kmemleak_stop(x...)	do {	\
	kmemleak_warn(x);		\
	kmemleak_disable();		\
} while (0)

#define warn_or_seq_printf(seq, fmt, ...)	do {	\
	if (seq)					\
		seq_printf(seq, fmt, ##__VA_ARGS__);	\
	else						\
		pr_warn(fmt, ##__VA_ARGS__);		\
} while (0)

static void warn_or_seq_hex_dump(struct seq_file *seq, int prefix_type,
				 int rowsize, int groupsize, const void *buf,
				 size_t len, bool ascii)
{
	if (seq)
		seq_hex_dump(seq, HEX_PREFIX, prefix_type, rowsize, groupsize,
			     buf, len, ascii);
	else
		print_hex_dump(KERN_WARNING, pr_fmt(HEX_PREFIX), prefix_type,
			       rowsize, groupsize, buf, len, ascii);
}

/*
 * Printing of the objects hex dump to the seq file. The number of lines to be
 * printed is limited to HEX_MAX_LINES to prevent seq file spamming. The
 * actual number of printed bytes depends on HEX_ROW_SIZE. It must be called
 * with the object->lock held.
 */
static void hex_dump_object(struct seq_file *seq,
			    struct kmemleak_object *object)
{
	const u8 *ptr = (const u8 *)object->pointer;
	size_t len;

	/* limit the number of lines to HEX_MAX_LINES */
	len = min_t(size_t, object->size, HEX_MAX_LINES * HEX_ROW_SIZE);

	warn_or_seq_printf(seq, "  hex dump (first %zu bytes):\n", len);
	kasan_disable_current();
	warn_or_seq_hex_dump(seq, DUMP_PREFIX_NONE, HEX_ROW_SIZE,
			     HEX_GROUP_SIZE, kasan_reset_tag((void *)ptr), len, HEX_ASCII);
	kasan_enable_current();
}

/*
 * Object colors, encoded with count and min_count:
 * - white - orphan object, not enough references to it (count < min_count)
 * - gray  - not orphan, not marked as false positive (min_count == 0) or
 *		sufficient references to it (count >= min_count)
 * - black - ignore, it doesn't contain references (e.g. text section)
 *		(min_count == -1). No function defined for this color.
 * Newly created objects don't have any color assigned (object->count == -1)
 * before the next memory scan when they become white.
 */
static bool color_white(const struct kmemleak_object *object)
{
	return object->count != KMEMLEAK_BLACK &&
		object->count < object->min_count;
}

static bool color_gray(const struct kmemleak_object *object)
{
	return object->min_count != KMEMLEAK_BLACK &&
		object->count >= object->min_count;
}

/*
 * Objects are considered unreferenced only if their color is white, they have
 * not be deleted and have a minimum age to avoid false positives caused by
 * pointers temporarily stored in CPU registers.
 */
static bool unreferenced_object(struct kmemleak_object *object)
{
	return (color_white(object) && object->flags & OBJECT_ALLOCATED) &&
		time_before_eq(object->jiffies + jiffies_min_age,
			       jiffies_last_scan);
}

/*
 * Printing of the unreferenced objects information to the seq file. The
 * print_unreferenced function must be called with the object->lock held.
 */
static void print_unreferenced(struct seq_file *seq,
			       struct kmemleak_object *object)
{
	int i;
	unsigned int msecs_age = jiffies_to_msecs(jiffies - object->jiffies);

	warn_or_seq_printf(seq, "unreferenced object 0x%08lx (size %zu):\n",
		   object->pointer, object->size);
	warn_or_seq_printf(seq, "  comm \"%s\", pid %d, jiffies %lu (age %d.%03ds)\n",
		   object->comm, object->pid, object->jiffies,
		   msecs_age / 1000, msecs_age % 1000);
	hex_dump_object(seq, object);
	warn_or_seq_printf(seq, "  backtrace:\n");

	for (i = 0; i < object->trace_len; i++) {
		void *ptr = (void *)object->trace[i];
		warn_or_seq_printf(seq, "    [<%p>] %pS\n", ptr, ptr);
	}
}

/*
 * Print the kmemleak_object information. This function is used mainly for
 * debugging special cases when kmemleak operations. It must be called with
 * the object->lock held.
 */
static void dump_object_info(struct kmemleak_object *object)
{
	pr_notice("Object 0x%08lx (size %zu):\n",
		  object->pointer, object->size);
	pr_notice("  comm \"%s\", pid %d, jiffies %lu\n",
		  object->comm, object->pid, object->jiffies);
	pr_notice("  min_count = %d\n", object->min_count);
	pr_notice("  count = %d\n", object->count);
	pr_notice("  flags = 0x%x\n", object->flags);
	pr_notice("  checksum = %u\n", object->checksum);
	pr_notice("  backtrace:\n");
	stack_trace_print(object->trace, object->trace_len, 4);
}

/*
 * Look-up a memory block metadata (kmemleak_object) in the object search
 * tree based on a pointer value. If alias is 0, only values pointing to the
 * beginning of the memory block are allowed. The kmemleak_lock must be held
 * when calling this function.
 */
static struct kmemleak_object *lookup_object(unsigned long ptr, int alias)
{
	struct rb_node *rb = object_tree_root.rb_node;

	while (rb) {
		struct kmemleak_object *object =
			rb_entry(rb, struct kmemleak_object, rb_node);
		if (ptr < object->pointer)
			rb = object->rb_node.rb_left;
		else if (object->pointer + object->size <= ptr)
			rb = object->rb_node.rb_right;
		else if (object->pointer == ptr || alias)
			return object;
		else {
			kmemleak_warn("Found object by alias at 0x%08lx\n",
				      ptr);
			dump_object_info(object);
			break;
		}
	}
	return NULL;
}

/*
 * Increment the object use_count. Return 1 if successful or 0 otherwise. Note
 * that once an object's use_count reached 0, the RCU freeing was already
 * registered and the object should no longer be used. This function must be
 * called under the protection of rcu_read_lock().
 */
static int get_object(struct kmemleak_object *object)
{
	return atomic_inc_not_zero(&object->use_count);
}

/*
 * Memory pool allocation and freeing. kmemleak_lock must not be held.
 */
static struct kmemleak_object *mem_pool_alloc(gfp_t gfp)
{
	unsigned long flags;
	struct kmemleak_object *object;

	/* try the slab allocator first */
	if (object_cache) {
		object = kmem_cache_alloc(object_cache, gfp_kmemleak_mask(gfp));
		if (object)
			return object;
	}

	/* slab allocation failed, try the memory pool */
	raw_spin_lock_irqsave(&kmemleak_lock, flags);
	object = list_first_entry_or_null(&mem_pool_free_list,
					  typeof(*object), object_list);
	if (object)
		list_del(&object->object_list);
	else if (mem_pool_free_count)
		object = &mem_pool[--mem_pool_free_count];
	else
		pr_warn_once("Memory pool empty, consider increasing CONFIG_DEBUG_KMEMLEAK_MEM_POOL_SIZE\n");
	raw_spin_unlock_irqrestore(&kmemleak_lock, flags);

	return object;
}

/*
 * Return the object to either the slab allocator or the memory pool.
 */
static void mem_pool_free(struct kmemleak_object *object)
{
	unsigned long flags;

	if (object < mem_pool || object >= mem_pool + ARRAY_SIZE(mem_pool)) {
		kmem_cache_free(object_cache, object);
		return;
	}

	/* add the object to the memory pool free list */
	raw_spin_lock_irqsave(&kmemleak_lock, flags);
	list_add(&object->object_list, &mem_pool_free_list);
	raw_spin_unlock_irqrestore(&kmemleak_lock, flags);
}

/*
 * RCU callback to free a kmemleak_object.
 */
static void free_object_rcu(struct rcu_head *rcu)
{
	struct hlist_node *tmp;
	struct kmemleak_scan_area *area;
	struct kmemleak_object *object =
		container_of(rcu, struct kmemleak_object, rcu);

	/*
	 * Once use_count is 0 (guaranteed by put_object), there is no other
	 * code accessing this object, hence no need for locking.
	 */
	hlist_for_each_entry_safe(area, tmp, &object->area_list, node) {
		hlist_del(&area->node);
		kmem_cache_free(scan_area_cache, area);
	}
	mem_pool_free(object);
}

/*
 * Decrement the object use_count. Once the count is 0, free the object using
 * an RCU callback. Since put_object() may be called via the kmemleak_free() ->
 * delete_object() path, the delayed RCU freeing ensures that there is no
 * recursive call to the kernel allocator. Lock-less RCU object_list traversal
 * is also possible.
 */
static void put_object(struct kmemleak_object *object)
{
	if (!atomic_dec_and_test(&object->use_count))
		return;

	/* should only get here after delete_object was called */
	WARN_ON(object->flags & OBJECT_ALLOCATED);

	/*
	 * It may be too early for the RCU callbacks, however, there is no
	 * concurrent object_list traversal when !object_cache and all objects
	 * came from the memory pool. Free the object directly.
	 */
	if (object_cache)
		call_rcu(&object->rcu, free_object_rcu);
	else
		free_object_rcu(&object->rcu);
}

/*
 * Look up an object in the object search tree and increase its use_count.
 */
static struct kmemleak_object *find_and_get_object(unsigned long ptr, int alias)
{
	unsigned long flags;
	struct kmemleak_object *object;

	rcu_read_lock();
	raw_spin_lock_irqsave(&kmemleak_lock, flags);
	object = lookup_object(ptr, alias);
	raw_spin_unlock_irqrestore(&kmemleak_lock, flags);

	/* check whether the object is still available */
	if (object && !get_object(object))
		object = NULL;
	rcu_read_unlock();

	return object;
}

/*
 * Remove an object from the object_tree_root and object_list. Must be called
 * with the kmemleak_lock held _if_ kmemleak is still enabled.
 */
static void __remove_object(struct kmemleak_object *object)
{
	rb_erase(&object->rb_node, &object_tree_root);
	list_del_rcu(&object->object_list);
}

/*
 * Look up an object in the object search tree and remove it from both
 * object_tree_root and object_list. The returned object's use_count should be
 * at least 1, as initially set by create_object().
 */
static struct kmemleak_object *find_and_remove_object(unsigned long ptr, int alias)
{
	unsigned long flags;
	struct kmemleak_object *object;

	raw_spin_lock_irqsave(&kmemleak_lock, flags);
	object = lookup_object(ptr, alias);
	if (object)
		__remove_object(object);
	raw_spin_unlock_irqrestore(&kmemleak_lock, flags);

	return object;
}

/*
 * Save stack trace to the given array of MAX_TRACE size.
 */
static int __save_stack_trace(unsigned long *trace)
{
	return stack_trace_save(trace, MAX_TRACE, 2);
}

/*
 * Create the metadata (struct kmemleak_object) corresponding to an allocated
 * memory block and add it to the object_list and object_tree_root.
 */
static struct kmemleak_object *create_object(unsigned long ptr, size_t size,
					     int min_count, gfp_t gfp)
{
	unsigned long flags;
	struct kmemleak_object *object, *parent;
	struct rb_node **link, *rb_parent;
	unsigned long untagged_ptr;

	object = mem_pool_alloc(gfp);
	if (!object) {
		pr_warn("Cannot allocate a kmemleak_object structure\n");
		kmemleak_disable();
		return NULL;
	}

	INIT_LIST_HEAD(&object->object_list);
	INIT_LIST_HEAD(&object->gray_list);
	INIT_HLIST_HEAD(&object->area_list);
	raw_spin_lock_init(&object->lock);
	atomic_set(&object->use_count, 1);
	object->flags = OBJECT_ALLOCATED;
	object->pointer = ptr;
	object->size = kfence_ksize((void *)ptr) ?: size;
	object->excess_ref = 0;
	object->min_count = min_count;
	object->count = 0;			/* white color initially */
	object->jiffies = jiffies;
	object->checksum = 0;

	/* task information */
	if (in_hardirq()) {
		object->pid = 0;
		strncpy(object->comm, "hardirq", sizeof(object->comm));
	} else if (in_serving_softirq()) {
		object->pid = 0;
		strncpy(object->comm, "softirq", sizeof(object->comm));
	} else {
		object->pid = current->pid;
		/*
		 * There is a small chance of a race with set_task_comm(),
		 * however using get_task_comm() here may cause locking
		 * dependency issues with current->alloc_lock. In the worst
		 * case, the command line is not correct.
		 */
		strncpy(object->comm, current->comm, sizeof(object->comm));
	}

	/* kernel backtrace */
	object->trace_len = __save_stack_trace(object->trace);

	raw_spin_lock_irqsave(&kmemleak_lock, flags);

	untagged_ptr = (unsigned long)kasan_reset_tag((void *)ptr);
	min_addr = min(min_addr, untagged_ptr);
	max_addr = max(max_addr, untagged_ptr + size);
	link = &object_tree_root.rb_node;
	rb_parent = NULL;
	while (*link) {
		rb_parent = *link;
		parent = rb_entry(rb_parent, struct kmemleak_object, rb_node);
		if (ptr + size <= parent->pointer)
			link = &parent->rb_node.rb_left;
		else if (parent->pointer + parent->size <= ptr)
			link = &parent->rb_node.rb_right;
		else {
			kmemleak_stop("Cannot insert 0x%lx into the object search tree (overlaps existing)\n",
				      ptr);
			/*
			 * No need for parent->lock here since "parent" cannot
			 * be freed while the kmemleak_lock is held.
			 */
			dump_object_info(parent);
			kmem_cache_free(object_cache, object);
			object = NULL;
			goto out;
		}
	}
	rb_link_node(&object->rb_node, rb_parent, link);
	rb_insert_color(&object->rb_node, &object_tree_root);

	list_add_tail_rcu(&object->object_list, &object_list);
out:
	raw_spin_unlock_irqrestore(&kmemleak_lock, flags);
	return object;
}

/*
 * Mark the object as not allocated and schedule RCU freeing via put_object().
 */
static void __delete_object(struct kmemleak_object *object)
{
	unsigned long flags;

	WARN_ON(!(object->flags & OBJECT_ALLOCATED));
	WARN_ON(atomic_read(&object->use_count) < 1);

	/*
	 * Locking here also ensures that the corresponding memory block
	 * cannot be freed when it is being scanned.
	 */
	raw_spin_lock_irqsave(&object->lock, flags);
	object->flags &= ~OBJECT_ALLOCATED;
	raw_spin_unlock_irqrestore(&object->lock, flags);
	put_object(object);
}

/*
 * Look up the metadata (struct kmemleak_object) corresponding to ptr and
 * delete it.
 */
static void delete_object_full(unsigned long ptr)
{
	struct kmemleak_object *object;

	object = find_and_remove_object(ptr, 0);
	if (!object) {
#ifdef DEBUG
		kmemleak_warn("Freeing unknown object at 0x%08lx\n",
			      ptr);
#endif
		return;
	}
	__delete_object(object);
}

/*
 * Look up the metadata (struct kmemleak_object) corresponding to ptr and
 * delete it. If the memory block is partially freed, the function may create
 * additional metadata for the remaining parts of the block.
 */
static void delete_object_part(unsigned long ptr, size_t size)
{
	struct kmemleak_object *object;
	unsigned long start, end;

	object = find_and_remove_object(ptr, 1);
	if (!object) {
#ifdef DEBUG
		kmemleak_warn("Partially freeing unknown object at 0x%08lx (size %zu)\n",
			      ptr, size);
#endif
		return;
	}

	/*
	 * Create one or two objects that may result from the memory block
	 * split. Note that partial freeing is only done by free_bootmem() and
	 * this happens before kmemleak_init() is called.
	 */
	start = object->pointer;
	end = object->pointer + object->size;
	if (ptr > start)
		create_object(start, ptr - start, object->min_count,
			      GFP_KERNEL);
	if (ptr + size < end)
		create_object(ptr + size, end - ptr - size, object->min_count,
			      GFP_KERNEL);

	__delete_object(object);
}

static void __paint_it(struct kmemleak_object *object, int color)
{
	object->min_count = color;
	if (color == KMEMLEAK_BLACK)
		object->flags |= OBJECT_NO_SCAN;
}

static void paint_it(struct kmemleak_object *object, int color)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&object->lock, flags);
	__paint_it(object, color);
	raw_spin_unlock_irqrestore(&object->lock, flags);
}

static void paint_ptr(unsigned long ptr, int color)
{
	struct kmemleak_object *object;

	object = find_and_get_object(ptr, 0);
	if (!object) {
		kmemleak_warn("Trying to color unknown object at 0x%08lx as %s\n",
			      ptr,
			      (color == KMEMLEAK_GREY) ? "Grey" :
			      (color == KMEMLEAK_BLACK) ? "Black" : "Unknown");
		return;
	}
	paint_it(object, color);
	put_object(object);
}

/*
 * Mark an object permanently as gray-colored so that it can no longer be
 * reported as a leak. This is used in general to mark a false positive.
 */
static void make_gray_object(unsigned long ptr)
{
	paint_ptr(ptr, KMEMLEAK_GREY);
}

/*
 * Mark the object as black-colored so that it is ignored from scans and
 * reporting.
 */
static void make_black_object(unsigned long ptr)
{
	paint_ptr(ptr, KMEMLEAK_BLACK);
}

/*
 * Add a scanning area to the object. If at least one such area is added,
 * kmemleak will only scan these ranges rather than the whole memory block.
 */
static void add_scan_area(unsigned long ptr, size_t size, gfp_t gfp)
{
	unsigned long flags;
	struct kmemleak_object *object;
	struct kmemleak_scan_area *area = NULL;
	unsigned long untagged_ptr;
	unsigned long untagged_objp;

	object = find_and_get_object(ptr, 1);
	if (!object) {
		kmemleak_warn("Adding scan area to unknown object at 0x%08lx\n",
			      ptr);
		return;
	}

	untagged_ptr = (unsigned long)kasan_reset_tag((void *)ptr);
	untagged_objp = (unsigned long)kasan_reset_tag((void *)object->pointer);

	if (scan_area_cache)
		area = kmem_cache_alloc(scan_area_cache, gfp_kmemleak_mask(gfp));

	raw_spin_lock_irqsave(&object->lock, flags);
	if (!area) {
		pr_warn_once("Cannot allocate a scan area, scanning the full object\n");
		/* mark the object for full scan to avoid false positives */
		object->flags |= OBJECT_FULL_SCAN;
		goto out_unlock;
	}
	if (size == SIZE_MAX) {
		size = untagged_objp + object->size - untagged_ptr;
	} else if (untagged_ptr + size > untagged_objp + object->size) {
		kmemleak_warn("Scan area larger than object 0x%08lx\n", ptr);
		dump_object_info(object);
		kmem_cache_free(scan_area_cache, area);
		goto out_unlock;
	}

	INIT_HLIST_NODE(&area->node);
	area->start = ptr;
	area->size = size;

	hlist_add_head(&area->node, &object->area_list);
out_unlock:
	raw_spin_unlock_irqrestore(&object->lock, flags);
	put_object(object);
}

/*
 * Any surplus references (object already gray) to 'ptr' are passed to
 * 'excess_ref'. This is used in the vmalloc() case where a pointer to
 * vm_struct may be used as an alternative reference to the vmalloc'ed object
 * (see free_thread_stack()).
 */
static void object_set_excess_ref(unsigned long ptr, unsigned long excess_ref)
{
	unsigned long flags;
	struct kmemleak_object *object;

	object = find_and_get_object(ptr, 0);
	if (!object) {
		kmemleak_warn("Setting excess_ref on unknown object at 0x%08lx\n",
			      ptr);
		return;
	}

	raw_spin_lock_irqsave(&object->lock, flags);
	object->excess_ref = excess_ref;
	raw_spin_unlock_irqrestore(&object->lock, flags);
	put_object(object);
}

/*
 * Set the OBJECT_NO_SCAN flag for the object corresponding to the give
 * pointer. Such object will not be scanned by kmemleak but references to it
 * are searched.
 */
static void object_no_scan(unsigned long ptr)
{
	unsigned long flags;
	struct kmemleak_object *object;

	object = find_and_get_object(ptr, 0);
	if (!object) {
		kmemleak_warn("Not scanning unknown object at 0x%08lx\n", ptr);
		return;
	}

	raw_spin_lock_irqsave(&object->lock, flags);
	object->flags |= OBJECT_NO_SCAN;
	raw_spin_unlock_irqrestore(&object->lock, flags);
	put_object(object);
}

/**
 * kmemleak_alloc - register a newly allocated object
 * @ptr:	pointer to beginning of the object
 * @size:	size of the object
 * @min_count:	minimum number of references to this object. If during memory
 *		scanning a number of references less than @min_count is found,
 *		the object is reported as a memory leak. If @min_count is 0,
 *		the object is never reported as a leak. If @min_count is -1,
 *		the object is ignored (not scanned and not reported as a leak)
 * @gfp:	kmalloc() flags used for kmemleak internal memory allocations
 *
 * This function is called from the kernel allocators when a new object
 * (memory block) is allocated (kmem_cache_alloc, kmalloc etc.).
 */
void __ref kmemleak_alloc(const void *ptr, size_t size, int min_count,
			  gfp_t gfp)
{
	pr_debug("%s(0x%p, %zu, %d)\n", __func__, ptr, size, min_count);

	if (kmemleak_enabled && ptr && !IS_ERR(ptr))
		create_object((unsigned long)ptr, size, min_count, gfp);
}
EXPORT_SYMBOL_GPL(kmemleak_alloc);

/**
 * kmemleak_alloc_percpu - register a newly allocated __percpu object
 * @ptr:	__percpu pointer to beginning of the object
 * @size:	size of the object
 * @gfp:	flags used for kmemleak internal memory allocations
 *
 * This function is called from the kernel percpu allocator when a new object
 * (memory block) is allocated (alloc_percpu).
 */
void __ref kmemleak_alloc_percpu(const void __percpu *ptr, size_t size,
				 gfp_t gfp)
{
	unsigned int cpu;

	pr_debug("%s(0x%p, %zu)\n", __func__, ptr, size);

	/*
	 * Percpu allocations are only scanned and not reported as leaks
	 * (min_count is set to 0).
	 */
	if (kmemleak_enabled && ptr && !IS_ERR(ptr))
		for_each_possible_cpu(cpu)
			create_object((unsigned long)per_cpu_ptr(ptr, cpu),
				      size, 0, gfp);
}
EXPORT_SYMBOL_GPL(kmemleak_alloc_percpu);

/**
 * kmemleak_vmalloc - register a newly vmalloc'ed object
 * @area:	pointer to vm_struct
 * @size:	size of the object
 * @gfp:	__vmalloc() flags used for kmemleak internal memory allocations
 *
 * This function is called from the vmalloc() kernel allocator when a new
 * object (memory block) is allocated.
 */
void __ref kmemleak_vmalloc(const struct vm_struct *area, size_t size, gfp_t gfp)
{
	pr_debug("%s(0x%p, %zu)\n", __func__, area, size);

	/*
	 * A min_count = 2 is needed because vm_struct contains a reference to
	 * the virtual address of the vmalloc'ed block.
	 */
	if (kmemleak_enabled) {
		create_object((unsigned long)area->addr, size, 2, gfp);
		object_set_excess_ref((unsigned long)area,
				      (unsigned long)area->addr);
	}
}
EXPORT_SYMBOL_GPL(kmemleak_vmalloc);

/**
 * kmemleak_free - unregister a previously registered object
 * @ptr:	pointer to beginning of the object
 *
 * This function is called from the kernel allocators when an object (memory
 * block) is freed (kmem_cache_free, kfree, vfree etc.).
 */
void __ref kmemleak_free(const void *ptr)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (kmemleak_free_enabled && ptr && !IS_ERR(ptr))
		delete_object_full((unsigned long)ptr);
}
EXPORT_SYMBOL_GPL(kmemleak_free);

/**
 * kmemleak_free_part - partially unregister a previously registered object
 * @ptr:	pointer to the beginning or inside the object. This also
 *		represents the start of the range to be freed
 * @size:	size to be unregistered
 *
 * This function is called when only a part of a memory block is freed
 * (usually from the bootmem allocator).
 */
void __ref kmemleak_free_part(const void *ptr, size_t size)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (kmemleak_enabled && ptr && !IS_ERR(ptr))
		delete_object_part((unsigned long)ptr, size);
}
EXPORT_SYMBOL_GPL(kmemleak_free_part);

/**
 * kmemleak_free_percpu - unregister a previously registered __percpu object
 * @ptr:	__percpu pointer to beginning of the object
 *
 * This function is called from the kernel percpu allocator when an object
 * (memory block) is freed (free_percpu).
 */
void __ref kmemleak_free_percpu(const void __percpu *ptr)
{
	unsigned int cpu;

	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (kmemleak_free_enabled && ptr && !IS_ERR(ptr))
		for_each_possible_cpu(cpu)
			delete_object_full((unsigned long)per_cpu_ptr(ptr,
								      cpu));
}
EXPORT_SYMBOL_GPL(kmemleak_free_percpu);

/**
 * kmemleak_update_trace - update object allocation stack trace
 * @ptr:	pointer to beginning of the object
 *
 * Override the object allocation stack trace for cases where the actual
 * allocation place is not always useful.
 */
void __ref kmemleak_update_trace(const void *ptr)
{
	struct kmemleak_object *object;
	unsigned long flags;

	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (!kmemleak_enabled || IS_ERR_OR_NULL(ptr))
		return;

	object = find_and_get_object((unsigned long)ptr, 1);
	if (!object) {
#ifdef DEBUG
		kmemleak_warn("Updating stack trace for unknown object at %p\n",
			      ptr);
#endif
		return;
	}

	raw_spin_lock_irqsave(&object->lock, flags);
	object->trace_len = __save_stack_trace(object->trace);
	raw_spin_unlock_irqrestore(&object->lock, flags);

	put_object(object);
}
EXPORT_SYMBOL(kmemleak_update_trace);

/**
 * kmemleak_not_leak - mark an allocated object as false positive
 * @ptr:	pointer to beginning of the object
 *
 * Calling this function on an object will cause the memory block to no longer
 * be reported as leak and always be scanned.
 */
void __ref kmemleak_not_leak(const void *ptr)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (kmemleak_enabled && ptr && !IS_ERR(ptr))
		make_gray_object((unsigned long)ptr);
}
EXPORT_SYMBOL(kmemleak_not_leak);

/**
 * kmemleak_ignore - ignore an allocated object
 * @ptr:	pointer to beginning of the object
 *
 * Calling this function on an object will cause the memory block to be
 * ignored (not scanned and not reported as a leak). This is usually done when
 * it is known that the corresponding block is not a leak and does not contain
 * any references to other allocated memory blocks.
 */
void __ref kmemleak_ignore(const void *ptr)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (kmemleak_enabled && ptr && !IS_ERR(ptr))
		make_black_object((unsigned long)ptr);
}
EXPORT_SYMBOL(kmemleak_ignore);

/**
 * kmemleak_scan_area - limit the range to be scanned in an allocated object
 * @ptr:	pointer to beginning or inside the object. This also
 *		represents the start of the scan area
 * @size:	size of the scan area
 * @gfp:	kmalloc() flags used for kmemleak internal memory allocations
 *
 * This function is used when it is known that only certain parts of an object
 * contain references to other objects. Kmemleak will only scan these areas
 * reducing the number false negatives.
 */
void __ref kmemleak_scan_area(const void *ptr, size_t size, gfp_t gfp)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (kmemleak_enabled && ptr && size && !IS_ERR(ptr))
		add_scan_area((unsigned long)ptr, size, gfp);
}
EXPORT_SYMBOL(kmemleak_scan_area);

/**
 * kmemleak_no_scan - do not scan an allocated object
 * @ptr:	pointer to beginning of the object
 *
 * This function notifies kmemleak not to scan the given memory block. Useful
 * in situations where it is known that the given object does not contain any
 * references to other objects. Kmemleak will not scan such objects reducing
 * the number of false negatives.
 */
void __ref kmemleak_no_scan(const void *ptr)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (kmemleak_enabled && ptr && !IS_ERR(ptr))
		object_no_scan((unsigned long)ptr);
}
EXPORT_SYMBOL(kmemleak_no_scan);

/**
 * kmemleak_alloc_phys - similar to kmemleak_alloc but taking a physical
 *			 address argument
 * @phys:	physical address of the object
 * @size:	size of the object
 * @min_count:	minimum number of references to this object.
 *              See kmemleak_alloc()
 * @gfp:	kmalloc() flags used for kmemleak internal memory allocations
 */
void __ref kmemleak_alloc_phys(phys_addr_t phys, size_t size, int min_count,
			       gfp_t gfp)
{
	if (PHYS_PFN(phys) >= min_low_pfn && PHYS_PFN(phys) < max_low_pfn)
		kmemleak_alloc(__va(phys), size, min_count, gfp);
}
EXPORT_SYMBOL(kmemleak_alloc_phys);

/**
 * kmemleak_free_part_phys - similar to kmemleak_free_part but taking a
 *			     physical address argument
 * @phys:	physical address if the beginning or inside an object. This
 *		also represents the start of the range to be freed
 * @size:	size to be unregistered
 */
void __ref kmemleak_free_part_phys(phys_addr_t phys, size_t size)
{
	if (PHYS_PFN(phys) >= min_low_pfn && PHYS_PFN(phys) < max_low_pfn)
		kmemleak_free_part(__va(phys), size);
}
EXPORT_SYMBOL(kmemleak_free_part_phys);

/**
 * kmemleak_not_leak_phys - similar to kmemleak_not_leak but taking a physical
 *			    address argument
 * @phys:	physical address of the object
 */
void __ref kmemleak_not_leak_phys(phys_addr_t phys)
{
	if (PHYS_PFN(phys) >= min_low_pfn && PHYS_PFN(phys) < max_low_pfn)
		kmemleak_not_leak(__va(phys));
}
EXPORT_SYMBOL(kmemleak_not_leak_phys);

/**
 * kmemleak_ignore_phys - similar to kmemleak_ignore but taking a physical
 *			  address argument
 * @phys:	physical address of the object
 */
void __ref kmemleak_ignore_phys(phys_addr_t phys)
{
	if (PHYS_PFN(phys) >= min_low_pfn && PHYS_PFN(phys) < max_low_pfn)
		kmemleak_ignore(__va(phys));
}
EXPORT_SYMBOL(kmemleak_ignore_phys);

/*
 * Update an object's checksum and return true if it was modified.
 */
static bool update_checksum(struct kmemleak_object *object)
{
	u32 old_csum = object->checksum;

	kasan_disable_current();
	kcsan_disable_current();
	object->checksum = crc32(0, kasan_reset_tag((void *)object->pointer), object->size);
	kasan_enable_current();
	kcsan_enable_current();

	return object->checksum != old_csum;
}

/*
 * Update an object's references. object->lock must be held by the caller.
 */
static void update_refs(struct kmemleak_object *object)
{
	if (!color_white(object)) {
		/* non-orphan, ignored or new */
		return;
	}

	/*
	 * Increase the object's reference count (number of pointers to the
	 * memory block). If this count reaches the required minimum, the
	 * object's color will become gray and it will be added to the
	 * gray_list.
	 */
	object->count++;
	if (color_gray(object)) {
		/* put_object() called when removing from gray_list */
		WARN_ON(!get_object(object));
		list_add_tail(&object->gray_list, &gray_list);
	}
}

/*
 * Memory scanning is a long process and it needs to be interruptible. This
 * function checks whether such interrupt condition occurred.
 */
static int scan_should_stop(void)
{
	if (!kmemleak_enabled)
		return 1;

	/*
	 * This function may be called from either process or kthread context,
	 * hence the need to check for both stop conditions.
	 */
	if (current->mm)
		return signal_pending(current);
	else
		return kthread_should_stop();

	return 0;
}

/*
 * Scan a memory block (exclusive range) for valid pointers and add those
 * found to the gray list.
 */
static void scan_block(void *_start, void *_end,
		       struct kmemleak_object *scanned)
{
	unsigned long *ptr;
	unsigned long *start = PTR_ALIGN(_start, BYTES_PER_POINTER);
	unsigned long *end = _end - (BYTES_PER_POINTER - 1);
	unsigned long flags;
	unsigned long untagged_ptr;

	raw_spin_lock_irqsave(&kmemleak_lock, flags);
	for (ptr = start; ptr < end; ptr++) {
		struct kmemleak_object *object;
		unsigned long pointer;
		unsigned long excess_ref;

		if (scan_should_stop())
			break;

		kasan_disable_current();
		pointer = *(unsigned long *)kasan_reset_tag((void *)ptr);
		kasan_enable_current();

		untagged_ptr = (unsigned long)kasan_reset_tag((void *)pointer);
		if (untagged_ptr < min_addr || untagged_ptr >= max_addr)
			continue;

		/*
		 * No need for get_object() here since we hold kmemleak_lock.
		 * object->use_count cannot be dropped to 0 while the object
		 * is still present in object_tree_root and object_list
		 * (with updates protected by kmemleak_lock).
		 */
		object = lookup_object(pointer, 1);
		if (!object)
			continue;
		if (object == scanned)
			/* self referenced, ignore */
			continue;

		/*
		 * Avoid the lockdep recursive warning on object->lock being
		 * previously acquired in scan_object(). These locks are
		 * enclosed by scan_mutex.
		 */
		raw_spin_lock_nested(&object->lock, SINGLE_DEPTH_NESTING);
		/* only pass surplus references (object already gray) */
		if (color_gray(object)) {
			excess_ref = object->excess_ref;
			/* no need for update_refs() if object already gray */
		} else {
			excess_ref = 0;
			update_refs(object);
		}
		raw_spin_unlock(&object->lock);

		if (excess_ref) {
			object = lookup_object(excess_ref, 0);
			if (!object)
				continue;
			if (object == scanned)
				/* circular reference, ignore */
				continue;
			raw_spin_lock_nested(&object->lock, SINGLE_DEPTH_NESTING);
			update_refs(object);
			raw_spin_unlock(&object->lock);
		}
	}
	raw_spin_unlock_irqrestore(&kmemleak_lock, flags);
}

/*
 * Scan a large memory block in MAX_SCAN_SIZE chunks to reduce the latency.
 */
#ifdef CONFIG_SMP
static void scan_large_block(void *start, void *end)
{
	void *next;

	while (start < end) {
		next = min(start + MAX_SCAN_SIZE, end);
		scan_block(start, next, NULL);
		start = next;
		cond_resched();
	}
}
#endif

/*
 * Scan a memory block corresponding to a kmemleak_object. A condition is
 * that object->use_count >= 1.
 */
static void scan_object(struct kmemleak_object *object)
{
	struct kmemleak_scan_area *area;
	unsigned long flags;

	/*
	 * Once the object->lock is acquired, the corresponding memory block
	 * cannot be freed (the same lock is acquired in delete_object).
	 */
	raw_spin_lock_irqsave(&object->lock, flags);
	if (object->flags & OBJECT_NO_SCAN)
		goto out;
	if (!(object->flags & OBJECT_ALLOCATED))
		/* already freed object */
		goto out;
	if (hlist_empty(&object->area_list) ||
	    object->flags & OBJECT_FULL_SCAN) {
		void *start = (void *)object->pointer;
		void *end = (void *)(object->pointer + object->size);
		void *next;

		do {
			next = min(start + MAX_SCAN_SIZE, end);
			scan_block(start, next, object);

			start = next;
			if (start >= end)
				break;

			raw_spin_unlock_irqrestore(&object->lock, flags);
			cond_resched();
			raw_spin_lock_irqsave(&object->lock, flags);
		} while (object->flags & OBJECT_ALLOCATED);
	} else
		hlist_for_each_entry(area, &object->area_list, node)
			scan_block((void *)area->start,
				   (void *)(area->start + area->size),
				   object);
out:
	raw_spin_unlock_irqrestore(&object->lock, flags);
}

/*
 * Scan the objects already referenced (gray objects). More objects will be
 * referenced and, if there are no memory leaks, all the objects are scanned.
 */
static void scan_gray_list(void)
{
	struct kmemleak_object *object, *tmp;

	/*
	 * The list traversal is safe for both tail additions and removals
	 * from inside the loop. The kmemleak objects cannot be freed from
	 * outside the loop because their use_count was incremented.
	 */
	object = list_entry(gray_list.next, typeof(*object), gray_list);
	while (&object->gray_list != &gray_list) {
		cond_resched();

		/* may add new objects to the list */
		if (!scan_should_stop())
			scan_object(object);

		tmp = list_entry(object->gray_list.next, typeof(*object),
				 gray_list);

		/* remove the object from the list and release it */
		list_del(&object->gray_list);
		put_object(object);

		object = tmp;
	}
	WARN_ON(!list_empty(&gray_list));
}

/*
 * Scan data sections and all the referenced memory blocks allocated via the
 * kernel's standard allocators. This function must be called with the
 * scan_mutex held.
 */
static void kmemleak_scan(void)
{
	unsigned long flags;
	struct kmemleak_object *object;
	struct zone *zone;
	int __maybe_unused i;
	int new_leaks = 0;

	jiffies_last_scan = jiffies;

	/* prepare the kmemleak_object's */
	rcu_read_lock();
	list_for_each_entry_rcu(object, &object_list, object_list) {
		raw_spin_lock_irqsave(&object->lock, flags);
#ifdef DEBUG
		/*
		 * With a few exceptions there should be a maximum of
		 * 1 reference to any object at this point.
		 */
		if (atomic_read(&object->use_count) > 1) {
			pr_debug("object->use_count = %d\n",
				 atomic_read(&object->use_count));
			dump_object_info(object);
		}
#endif
		/* reset the reference count (whiten the object) */
		object->count = 0;
		if (color_gray(object) && get_object(object))
			list_add_tail(&object->gray_list, &gray_list);

		raw_spin_unlock_irqrestore(&object->lock, flags);
	}
	rcu_read_unlock();

#ifdef CONFIG_SMP
	/* per-cpu sections scanning */
	for_each_possible_cpu(i)
		scan_large_block(__per_cpu_start + per_cpu_offset(i),
				 __per_cpu_end + per_cpu_offset(i));
#endif

	/*
	 * Struct page scanning for each node.
	 */
	get_online_mems();
	for_each_populated_zone(zone) {
		unsigned long start_pfn = zone->zone_start_pfn;
		unsigned long end_pfn = zone_end_pfn(zone);
		unsigned long pfn;

		for (pfn = start_pfn; pfn < end_pfn; pfn++) {
			struct page *page = pfn_to_online_page(pfn);

			if (!page)
				continue;

			/* only scan pages belonging to this zone */
			if (page_zone(page) != zone)
				continue;
			/* only scan if page is in use */
			if (page_count(page) == 0)
				continue;
			scan_block(page, page + 1, NULL);
			if (!(pfn & 63))
				cond_resched();
		}
	}
	put_online_mems();

	/*
	 * Scanning the task stacks (may introduce false negatives).
	 */
	if (kmemleak_stack_scan) {
		struct task_struct *p, *g;

		rcu_read_lock();
		for_each_process_thread(g, p) {
			void *stack = try_get_task_stack(p);
			if (stack) {
				scan_block(stack, stack + THREAD_SIZE, NULL);
				put_task_stack(p);
			}
		}
		rcu_read_unlock();
	}

	/*
	 * Scan the objects already referenced from the sections scanned
	 * above.
	 */
	scan_gray_list();

	/*
	 * Check for new or unreferenced objects modified since the previous
	 * scan and color them gray until the next scan.
	 */
	rcu_read_lock();
	list_for_each_entry_rcu(object, &object_list, object_list) {
		raw_spin_lock_irqsave(&object->lock, flags);
		if (color_white(object) && (object->flags & OBJECT_ALLOCATED)
		    && update_checksum(object) && get_object(object)) {
			/* color it gray temporarily */
			object->count = object->min_count;
			list_add_tail(&object->gray_list, &gray_list);
		}
		raw_spin_unlock_irqrestore(&object->lock, flags);
	}
	rcu_read_unlock();

	/*
	 * Re-scan the gray list for modified unreferenced objects.
	 */
	scan_gray_list();

	/*
	 * If scanning was stopped do not report any new unreferenced objects.
	 */
	if (scan_should_stop())
		return;

	/*
	 * Scanning result reporting.
	 */
	rcu_read_lock();
	list_for_each_entry_rcu(object, &object_list, object_list) {
		raw_spin_lock_irqsave(&object->lock, flags);
		if (unreferenced_object(object) &&
		    !(object->flags & OBJECT_REPORTED)) {
			object->flags |= OBJECT_REPORTED;

			if (kmemleak_verbose)
				print_unreferenced(NULL, object);

			new_leaks++;
		}
		raw_spin_unlock_irqrestore(&object->lock, flags);
	}
	rcu_read_unlock();

	if (new_leaks) {
		kmemleak_found_leaks = true;

		pr_info("%d new suspected memory leaks (see /sys/kernel/debug/kmemleak)\n",
			new_leaks);
	}

}

/*
 * Thread function performing automatic memory scanning. Unreferenced objects
 * at the end of a memory scan are reported but only the first time.
 */
static int kmemleak_scan_thread(void *arg)
{
	static int first_run = IS_ENABLED(CONFIG_DEBUG_KMEMLEAK_AUTO_SCAN);

	pr_info("Automatic memory scanning thread started\n");
	set_user_nice(current, 10);

	/*
	 * Wait before the first scan to allow the system to fully initialize.
	 */
	if (first_run) {
		signed long timeout = msecs_to_jiffies(SECS_FIRST_SCAN * 1000);
		first_run = 0;
		while (timeout && !kthread_should_stop())
			timeout = schedule_timeout_interruptible(timeout);
	}

	while (!kthread_should_stop()) {
		signed long timeout = READ_ONCE(jiffies_scan_wait);

		mutex_lock(&scan_mutex);
		kmemleak_scan();
		mutex_unlock(&scan_mutex);

		/* wait before the next scan */
		while (timeout && !kthread_should_stop())
			timeout = schedule_timeout_interruptible(timeout);
	}

	pr_info("Automatic memory scanning thread ended\n");

	return 0;
}

/*
 * Start the automatic memory scanning thread. This function must be called
 * with the scan_mutex held.
 */
static void start_scan_thread(void)
{
	if (scan_thread)
		return;
	scan_thread = kthread_run(kmemleak_scan_thread, NULL, "kmemleak");
	if (IS_ERR(scan_thread)) {
		pr_warn("Failed to create the scan thread\n");
		scan_thread = NULL;
	}
}

/*
 * Stop the automatic memory scanning thread.
 */
static void stop_scan_thread(void)
{
	if (scan_thread) {
		kthread_stop(scan_thread);
		scan_thread = NULL;
	}
}

/*
 * Iterate over the object_list and return the first valid object at or after
 * the required position with its use_count incremented. The function triggers
 * a memory scanning when the pos argument points to the first position.
 */
static void *kmemleak_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct kmemleak_object *object;
	loff_t n = *pos;
	int err;

	err = mutex_lock_interruptible(&scan_mutex);
	if (err < 0)
		return ERR_PTR(err);

	rcu_read_lock();
	list_for_each_entry_rcu(object, &object_list, object_list) {
		if (n-- > 0)
			continue;
		if (get_object(object))
			goto out;
	}
	object = NULL;
out:
	return object;
}

/*
 * Return the next object in the object_list. The function decrements the
 * use_count of the previous object and increases that of the next one.
 */
static void *kmemleak_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct kmemleak_object *prev_obj = v;
	struct kmemleak_object *next_obj = NULL;
	struct kmemleak_object *obj = prev_obj;

	++(*pos);

	list_for_each_entry_continue_rcu(obj, &object_list, object_list) {
		if (get_object(obj)) {
			next_obj = obj;
			break;
		}
	}

	put_object(prev_obj);
	return next_obj;
}

/*
 * Decrement the use_count of the last object required, if any.
 */
static void kmemleak_seq_stop(struct seq_file *seq, void *v)
{
	if (!IS_ERR(v)) {
		/*
		 * kmemleak_seq_start may return ERR_PTR if the scan_mutex
		 * waiting was interrupted, so only release it if !IS_ERR.
		 */
		rcu_read_unlock();
		mutex_unlock(&scan_mutex);
		if (v)
			put_object(v);
	}
}

/*
 * Print the information for an unreferenced object to the seq file.
 */
static int kmemleak_seq_show(struct seq_file *seq, void *v)
{
	struct kmemleak_object *object = v;
	unsigned long flags;

	raw_spin_lock_irqsave(&object->lock, flags);
	if ((object->flags & OBJECT_REPORTED) && unreferenced_object(object))
		print_unreferenced(seq, object);
	raw_spin_unlock_irqrestore(&object->lock, flags);
	return 0;
}

static const struct seq_operations kmemleak_seq_ops = {
	.start = kmemleak_seq_start,
	.next  = kmemleak_seq_next,
	.stop  = kmemleak_seq_stop,
	.show  = kmemleak_seq_show,
};

static int kmemleak_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &kmemleak_seq_ops);
}

static int dump_str_object_info(const char *str)
{
	unsigned long flags;
	struct kmemleak_object *object;
	unsigned long addr;

	if (kstrtoul(str, 0, &addr))
		return -EINVAL;
	object = find_and_get_object(addr, 0);
	if (!object) {
		pr_info("Unknown object at 0x%08lx\n", addr);
		return -EINVAL;
	}

	raw_spin_lock_irqsave(&object->lock, flags);
	dump_object_info(object);
	raw_spin_unlock_irqrestore(&object->lock, flags);

	put_object(object);
	return 0;
}

/*
 * We use grey instead of black to ensure we can do future scans on the same
 * objects. If we did not do future scans these black objects could
 * potentially contain references to newly allocated objects in the future and
 * we'd end up with false positives.
 */
static void kmemleak_clear(void)
{
	struct kmemleak_object *object;
	unsigned long flags;

	rcu_read_lock();
	list_for_each_entry_rcu(object, &object_list, object_list) {
		raw_spin_lock_irqsave(&object->lock, flags);
		if ((object->flags & OBJECT_REPORTED) &&
		    unreferenced_object(object))
			__paint_it(object, KMEMLEAK_GREY);
		raw_spin_unlock_irqrestore(&object->lock, flags);
	}
	rcu_read_unlock();

	kmemleak_found_leaks = false;
}

static void __kmemleak_do_cleanup(void);

/*
 * File write operation to configure kmemleak at run-time. The following
 * commands can be written to the /sys/kernel/debug/kmemleak file:
 *   off	- disable kmemleak (irreversible)
 *   stack=on	- enable the task stacks scanning
 *   stack=off	- disable the tasks stacks scanning
 *   scan=on	- start the automatic memory scanning thread
 *   scan=off	- stop the automatic memory scanning thread
 *   scan=...	- set the automatic memory scanning period in seconds (0 to
 *		  disable it)
 *   scan	- trigger a memory scan
 *   clear	- mark all current reported unreferenced kmemleak objects as
 *		  grey to ignore printing them, or free all kmemleak objects
 *		  if kmemleak has been disabled.
 *   dump=...	- dump information about the object found at the given address
 */
static ssize_t kmemleak_write(struct file *file, const char __user *user_buf,
			      size_t size, loff_t *ppos)
{
	char buf[64];
	int buf_size;
	int ret;

	buf_size = min(size, (sizeof(buf) - 1));
	if (strncpy_from_user(buf, user_buf, buf_size) < 0)
		return -EFAULT;
	buf[buf_size] = 0;

	ret = mutex_lock_interruptible(&scan_mutex);
	if (ret < 0)
		return ret;

	if (strncmp(buf, "clear", 5) == 0) {
		if (kmemleak_enabled)
			kmemleak_clear();
		else
			__kmemleak_do_cleanup();
		goto out;
	}

	if (!kmemleak_enabled) {
		ret = -EPERM;
		goto out;
	}

	if (strncmp(buf, "off", 3) == 0)
		kmemleak_disable();
	else if (strncmp(buf, "stack=on", 8) == 0)
		kmemleak_stack_scan = 1;
	else if (strncmp(buf, "stack=off", 9) == 0)
		kmemleak_stack_scan = 0;
	else if (strncmp(buf, "scan=on", 7) == 0)
		start_scan_thread();
	else if (strncmp(buf, "scan=off", 8) == 0)
		stop_scan_thread();
	else if (strncmp(buf, "scan=", 5) == 0) {
		unsigned secs;
		unsigned long msecs;

		ret = kstrtouint(buf + 5, 0, &secs);
		if (ret < 0)
			goto out;

		msecs = secs * MSEC_PER_SEC;
		if (msecs > UINT_MAX)
			msecs = UINT_MAX;

		stop_scan_thread();
		if (msecs) {
			WRITE_ONCE(jiffies_scan_wait, msecs_to_jiffies(msecs));
			start_scan_thread();
		}
	} else if (strncmp(buf, "scan", 4) == 0)
		kmemleak_scan();
	else if (strncmp(buf, "dump=", 5) == 0)
		ret = dump_str_object_info(buf + 5);
	else
		ret = -EINVAL;

out:
	mutex_unlock(&scan_mutex);
	if (ret < 0)
		return ret;

	/* ignore the rest of the buffer, only one command at a time */
	*ppos += size;
	return size;
}

static const struct file_operations kmemleak_fops = {
	.owner		= THIS_MODULE,
	.open		= kmemleak_open,
	.read		= seq_read,
	.write		= kmemleak_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static void __kmemleak_do_cleanup(void)
{
	struct kmemleak_object *object, *tmp;

	/*
	 * Kmemleak has already been disabled, no need for RCU list traversal
	 * or kmemleak_lock held.
	 */
	list_for_each_entry_safe(object, tmp, &object_list, object_list) {
		__remove_object(object);
		__delete_object(object);
	}
}

/*
 * Stop the memory scanning thread and free the kmemleak internal objects if
 * no previous scan thread (otherwise, kmemleak may still have some useful
 * information on memory leaks).
 */
static void kmemleak_do_cleanup(struct work_struct *work)
{
	stop_scan_thread();

	mutex_lock(&scan_mutex);
	/*
	 * Once it is made sure that kmemleak_scan has stopped, it is safe to no
	 * longer track object freeing. Ordering of the scan thread stopping and
	 * the memory accesses below is guaranteed by the kthread_stop()
	 * function.
	 */
	kmemleak_free_enabled = 0;
	mutex_unlock(&scan_mutex);

	if (!kmemleak_found_leaks)
		__kmemleak_do_cleanup();
	else
		pr_info("Kmemleak disabled without freeing internal data. Reclaim the memory with \"echo clear > /sys/kernel/debug/kmemleak\".\n");
}

static DECLARE_WORK(cleanup_work, kmemleak_do_cleanup);

/*
 * Disable kmemleak. No memory allocation/freeing will be traced once this
 * function is called. Disabling kmemleak is an irreversible operation.
 */
static void kmemleak_disable(void)
{
	/* atomically check whether it was already invoked */
	if (cmpxchg(&kmemleak_error, 0, 1))
		return;

	/* stop any memory operation tracing */
	kmemleak_enabled = 0;

	/* check whether it is too early for a kernel thread */
	if (kmemleak_initialized)
		schedule_work(&cleanup_work);
	else
		kmemleak_free_enabled = 0;

	pr_info("Kernel memory leak detector disabled\n");
}

/*
 * Allow boot-time kmemleak disabling (enabled by default).
 */
static int __init kmemleak_boot_config(char *str)
{
	if (!str)
		return -EINVAL;
	if (strcmp(str, "off") == 0)
		kmemleak_disable();
	else if (strcmp(str, "on") == 0)
		kmemleak_skip_disable = 1;
	else
		return -EINVAL;
	return 0;
}
early_param("kmemleak", kmemleak_boot_config);

/*
 * Kmemleak initialization.
 */
void __init kmemleak_init(void)
{
#ifdef CONFIG_DEBUG_KMEMLEAK_DEFAULT_OFF
	if (!kmemleak_skip_disable) {
		kmemleak_disable();
		return;
	}
#endif

	if (kmemleak_error)
		return;

	jiffies_min_age = msecs_to_jiffies(MSECS_MIN_AGE);
	jiffies_scan_wait = msecs_to_jiffies(SECS_SCAN_WAIT * 1000);

	object_cache = KMEM_CACHE(kmemleak_object, SLAB_NOLEAKTRACE);
	scan_area_cache = KMEM_CACHE(kmemleak_scan_area, SLAB_NOLEAKTRACE);

	/* register the data/bss sections */
	create_object((unsigned long)_sdata, _edata - _sdata,
		      KMEMLEAK_GREY, GFP_ATOMIC);
	create_object((unsigned long)__bss_start, __bss_stop - __bss_start,
		      KMEMLEAK_GREY, GFP_ATOMIC);
	/* only register .data..ro_after_init if not within .data */
	if (&__start_ro_after_init < &_sdata || &__end_ro_after_init > &_edata)
		create_object((unsigned long)__start_ro_after_init,
			      __end_ro_after_init - __start_ro_after_init,
			      KMEMLEAK_GREY, GFP_ATOMIC);
}

/*
 * Late initialization function.
 */
static int __init kmemleak_late_init(void)
{
	kmemleak_initialized = 1;

	debugfs_create_file("kmemleak", 0644, NULL, NULL, &kmemleak_fops);

	if (kmemleak_error) {
		/*
		 * Some error occurred and kmemleak was disabled. There is a
		 * small chance that kmemleak_disable() was called immediately
		 * after setting kmemleak_initialized and we may end up with
		 * two clean-up threads but serialized by scan_mutex.
		 */
		schedule_work(&cleanup_work);
		return -ENOMEM;
	}

	if (IS_ENABLED(CONFIG_DEBUG_KMEMLEAK_AUTO_SCAN)) {
		mutex_lock(&scan_mutex);
		start_scan_thread();
		mutex_unlock(&scan_mutex);
	}

	pr_info("Kernel memory leak detector initialized (mem pool available: %d)\n",
		mem_pool_free_count);

	return 0;
}
late_initcall(kmemleak_late_init);
