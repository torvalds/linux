/*
 * mm/kmemleak.c
 *
 * Copyright (C) 2008 ARM Limited
 * Written by Catalin Marinas <catalin.marinas@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *
 * For more information on the algorithm and kmemleak usage, please see
 * Documentation/kmemleak.txt.
 *
 * Notes on locking
 * ----------------
 *
 * The following locks and mutexes are used by kmemleak:
 *
 * - kmemleak_lock (rwlock): protects the object_list modifications and
 *   accesses to the object_tree_root. The object_list is the main list
 *   holding the metadata (struct kmemleak_object) for the allocated memory
 *   blocks. The object_tree_root is a priority search tree used to look-up
 *   metadata based on a pointer to the corresponding memory block.  The
 *   kmemleak_object structures are added to the object_list and
 *   object_tree_root in the create_object() function called from the
 *   kmemleak_alloc() callback and removed in delete_object() called from the
 *   kmemleak_free() callback
 * - kmemleak_object.lock (spinlock): protects a kmemleak_object. Accesses to
 *   the metadata (e.g. count) are protected by this lock. Note that some
 *   members of this structure may be protected by other means (atomic or
 *   kmemleak_lock). This lock is also held when scanning the corresponding
 *   memory block to avoid the kernel freeing it via the kmemleak_free()
 *   callback. This is less heavyweight than holding a global lock like
 *   kmemleak_lock during scanning
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
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/prio_tree.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/stacktrace.h>
#include <linux/cache.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
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
#include <asm/atomic.h>

#include <linux/kmemcheck.h>
#include <linux/kmemleak.h>

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
#define GFP_KMEMLEAK_MASK	(GFP_KERNEL | GFP_ATOMIC)

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
 * tree_node are already protected by the corresponding locks or mutex (see
 * the notes on locking above). These objects are reference-counted
 * (use_count) and freed using the RCU mechanism.
 */
struct kmemleak_object {
	spinlock_t lock;
	unsigned long flags;		/* object status flags */
	struct list_head object_list;
	struct list_head gray_list;
	struct prio_tree_node tree_node;
	struct rcu_head rcu;		/* object_list lockless traversal */
	/* object usage count; object freed when use_count == 0 */
	atomic_t use_count;
	unsigned long pointer;
	size_t size;
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
/* prio search tree for object boundaries */
static struct prio_tree_root object_tree_root;
/* rw_lock protecting the access to object_list and prio_tree_root */
static DEFINE_RWLOCK(kmemleak_lock);

/* allocation caches for kmemleak internal data */
static struct kmem_cache *object_cache;
static struct kmem_cache *scan_area_cache;

/* set if tracing memory operations is enabled */
static atomic_t kmemleak_enabled = ATOMIC_INIT(0);
/* set in the late_initcall if there were no errors */
static atomic_t kmemleak_initialized = ATOMIC_INIT(0);
/* enables or disables early logging of the memory operations */
static atomic_t kmemleak_early_log = ATOMIC_INIT(1);
/* set if a fata kmemleak error has occurred */
static atomic_t kmemleak_error = ATOMIC_INIT(0);

/* minimum and maximum address that may be valid pointers */
static unsigned long min_addr = ULONG_MAX;
static unsigned long max_addr;

static struct task_struct *scan_thread;
/* used to avoid reporting of recently allocated objects */
static unsigned long jiffies_min_age;
static unsigned long jiffies_last_scan;
/* delay between automatic memory scannings */
static signed long jiffies_scan_wait;
/* enables or disables the task stacks scanning */
static int kmemleak_stack_scan = 1;
/* protects the memory scanning, parameters and debug/kmemleak file access */
static DEFINE_MUTEX(scan_mutex);

/*
 * Early object allocation/freeing logging. Kmemleak is initialized after the
 * kernel allocator. However, both the kernel allocator and kmemleak may
 * allocate memory blocks which need to be tracked. Kmemleak defines an
 * arbitrary buffer to hold the allocation/freeing information before it is
 * fully initialized.
 */

/* kmemleak operation type for early logging */
enum {
	KMEMLEAK_ALLOC,
	KMEMLEAK_FREE,
	KMEMLEAK_FREE_PART,
	KMEMLEAK_NOT_LEAK,
	KMEMLEAK_IGNORE,
	KMEMLEAK_SCAN_AREA,
	KMEMLEAK_NO_SCAN
};

/*
 * Structure holding the information passed to kmemleak callbacks during the
 * early logging.
 */
struct early_log {
	int op_type;			/* kmemleak operation type */
	const void *ptr;		/* allocated/freed memory block */
	size_t size;			/* memory block size */
	int min_count;			/* minimum reference count */
	unsigned long trace[MAX_TRACE];	/* stack trace */
	unsigned int trace_len;		/* stack trace length */
};

/* early logging buffer and current position */
static struct early_log
	early_log[CONFIG_DEBUG_KMEMLEAK_EARLY_LOG_SIZE] __initdata;
static int crt_early_log __initdata;

static void kmemleak_disable(void);

/*
 * Print a warning and dump the stack trace.
 */
#define kmemleak_warn(x...)	do {	\
	pr_warning(x);			\
	dump_stack();			\
} while (0)

/*
 * Macro invoked when a serious kmemleak condition occured and cannot be
 * recovered from. Kmemleak will be disabled and further allocation/freeing
 * tracing no longer available.
 */
#define kmemleak_stop(x...)	do {	\
	kmemleak_warn(x);		\
	kmemleak_disable();		\
} while (0)

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
	int i, len, remaining;
	unsigned char linebuf[HEX_ROW_SIZE * 5];

	/* limit the number of lines to HEX_MAX_LINES */
	remaining = len =
		min(object->size, (size_t)(HEX_MAX_LINES * HEX_ROW_SIZE));

	seq_printf(seq, "  hex dump (first %d bytes):\n", len);
	for (i = 0; i < len; i += HEX_ROW_SIZE) {
		int linelen = min(remaining, HEX_ROW_SIZE);

		remaining -= HEX_ROW_SIZE;
		hex_dump_to_buffer(ptr + i, linelen, HEX_ROW_SIZE,
				   HEX_GROUP_SIZE, linebuf, sizeof(linebuf),
				   HEX_ASCII);
		seq_printf(seq, "    %s\n", linebuf);
	}
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

	seq_printf(seq, "unreferenced object 0x%08lx (size %zu):\n",
		   object->pointer, object->size);
	seq_printf(seq, "  comm \"%s\", pid %d, jiffies %lu (age %d.%03ds)\n",
		   object->comm, object->pid, object->jiffies,
		   msecs_age / 1000, msecs_age % 1000);
	hex_dump_object(seq, object);
	seq_printf(seq, "  backtrace:\n");

	for (i = 0; i < object->trace_len; i++) {
		void *ptr = (void *)object->trace[i];
		seq_printf(seq, "    [<%p>] %pS\n", ptr, ptr);
	}
}

/*
 * Print the kmemleak_object information. This function is used mainly for
 * debugging special cases when kmemleak operations. It must be called with
 * the object->lock held.
 */
static void dump_object_info(struct kmemleak_object *object)
{
	struct stack_trace trace;

	trace.nr_entries = object->trace_len;
	trace.entries = object->trace;

	pr_notice("Object 0x%08lx (size %zu):\n",
		  object->tree_node.start, object->size);
	pr_notice("  comm \"%s\", pid %d, jiffies %lu\n",
		  object->comm, object->pid, object->jiffies);
	pr_notice("  min_count = %d\n", object->min_count);
	pr_notice("  count = %d\n", object->count);
	pr_notice("  flags = 0x%lx\n", object->flags);
	pr_notice("  checksum = %d\n", object->checksum);
	pr_notice("  backtrace:\n");
	print_stack_trace(&trace, 4);
}

/*
 * Look-up a memory block metadata (kmemleak_object) in the priority search
 * tree based on a pointer value. If alias is 0, only values pointing to the
 * beginning of the memory block are allowed. The kmemleak_lock must be held
 * when calling this function.
 */
static struct kmemleak_object *lookup_object(unsigned long ptr, int alias)
{
	struct prio_tree_node *node;
	struct prio_tree_iter iter;
	struct kmemleak_object *object;

	prio_tree_iter_init(&iter, &object_tree_root, ptr, ptr);
	node = prio_tree_next(&iter);
	if (node) {
		object = prio_tree_entry(node, struct kmemleak_object,
					 tree_node);
		if (!alias && object->pointer != ptr) {
			kmemleak_warn("Found object by alias");
			object = NULL;
		}
	} else
		object = NULL;

	return object;
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
 * RCU callback to free a kmemleak_object.
 */
static void free_object_rcu(struct rcu_head *rcu)
{
	struct hlist_node *elem, *tmp;
	struct kmemleak_scan_area *area;
	struct kmemleak_object *object =
		container_of(rcu, struct kmemleak_object, rcu);

	/*
	 * Once use_count is 0 (guaranteed by put_object), there is no other
	 * code accessing this object, hence no need for locking.
	 */
	hlist_for_each_entry_safe(area, elem, tmp, &object->area_list, node) {
		hlist_del(elem);
		kmem_cache_free(scan_area_cache, area);
	}
	kmem_cache_free(object_cache, object);
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

	call_rcu(&object->rcu, free_object_rcu);
}

/*
 * Look up an object in the prio search tree and increase its use_count.
 */
static struct kmemleak_object *find_and_get_object(unsigned long ptr, int alias)
{
	unsigned long flags;
	struct kmemleak_object *object = NULL;

	rcu_read_lock();
	read_lock_irqsave(&kmemleak_lock, flags);
	if (ptr >= min_addr && ptr < max_addr)
		object = lookup_object(ptr, alias);
	read_unlock_irqrestore(&kmemleak_lock, flags);

	/* check whether the object is still available */
	if (object && !get_object(object))
		object = NULL;
	rcu_read_unlock();

	return object;
}

/*
 * Save stack trace to the given array of MAX_TRACE size.
 */
static int __save_stack_trace(unsigned long *trace)
{
	struct stack_trace stack_trace;

	stack_trace.max_entries = MAX_TRACE;
	stack_trace.nr_entries = 0;
	stack_trace.entries = trace;
	stack_trace.skip = 2;
	save_stack_trace(&stack_trace);

	return stack_trace.nr_entries;
}

/*
 * Create the metadata (struct kmemleak_object) corresponding to an allocated
 * memory block and add it to the object_list and object_tree_root.
 */
static struct kmemleak_object *create_object(unsigned long ptr, size_t size,
					     int min_count, gfp_t gfp)
{
	unsigned long flags;
	struct kmemleak_object *object;
	struct prio_tree_node *node;

	object = kmem_cache_alloc(object_cache, gfp & GFP_KMEMLEAK_MASK);
	if (!object) {
		kmemleak_stop("Cannot allocate a kmemleak_object structure\n");
		return NULL;
	}

	INIT_LIST_HEAD(&object->object_list);
	INIT_LIST_HEAD(&object->gray_list);
	INIT_HLIST_HEAD(&object->area_list);
	spin_lock_init(&object->lock);
	atomic_set(&object->use_count, 1);
	object->flags = OBJECT_ALLOCATED;
	object->pointer = ptr;
	object->size = size;
	object->min_count = min_count;
	object->count = 0;			/* white color initially */
	object->jiffies = jiffies;
	object->checksum = 0;

	/* task information */
	if (in_irq()) {
		object->pid = 0;
		strncpy(object->comm, "hardirq", sizeof(object->comm));
	} else if (in_softirq()) {
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

	INIT_PRIO_TREE_NODE(&object->tree_node);
	object->tree_node.start = ptr;
	object->tree_node.last = ptr + size - 1;

	write_lock_irqsave(&kmemleak_lock, flags);

	min_addr = min(min_addr, ptr);
	max_addr = max(max_addr, ptr + size);
	node = prio_tree_insert(&object_tree_root, &object->tree_node);
	/*
	 * The code calling the kernel does not yet have the pointer to the
	 * memory block to be able to free it.  However, we still hold the
	 * kmemleak_lock here in case parts of the kernel started freeing
	 * random memory blocks.
	 */
	if (node != &object->tree_node) {
		kmemleak_stop("Cannot insert 0x%lx into the object search tree "
			      "(already existing)\n", ptr);
		object = lookup_object(ptr, 1);
		spin_lock(&object->lock);
		dump_object_info(object);
		spin_unlock(&object->lock);

		goto out;
	}
	list_add_tail_rcu(&object->object_list, &object_list);
out:
	write_unlock_irqrestore(&kmemleak_lock, flags);
	return object;
}

/*
 * Remove the metadata (struct kmemleak_object) for a memory block from the
 * object_list and object_tree_root and decrement its use_count.
 */
static void __delete_object(struct kmemleak_object *object)
{
	unsigned long flags;

	write_lock_irqsave(&kmemleak_lock, flags);
	prio_tree_remove(&object_tree_root, &object->tree_node);
	list_del_rcu(&object->object_list);
	write_unlock_irqrestore(&kmemleak_lock, flags);

	WARN_ON(!(object->flags & OBJECT_ALLOCATED));
	WARN_ON(atomic_read(&object->use_count) < 2);

	/*
	 * Locking here also ensures that the corresponding memory block
	 * cannot be freed when it is being scanned.
	 */
	spin_lock_irqsave(&object->lock, flags);
	object->flags &= ~OBJECT_ALLOCATED;
	spin_unlock_irqrestore(&object->lock, flags);
	put_object(object);
}

/*
 * Look up the metadata (struct kmemleak_object) corresponding to ptr and
 * delete it.
 */
static void delete_object_full(unsigned long ptr)
{
	struct kmemleak_object *object;

	object = find_and_get_object(ptr, 0);
	if (!object) {
#ifdef DEBUG
		kmemleak_warn("Freeing unknown object at 0x%08lx\n",
			      ptr);
#endif
		return;
	}
	__delete_object(object);
	put_object(object);
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

	object = find_and_get_object(ptr, 1);
	if (!object) {
#ifdef DEBUG
		kmemleak_warn("Partially freeing unknown object at 0x%08lx "
			      "(size %zu)\n", ptr, size);
#endif
		return;
	}
	__delete_object(object);

	/*
	 * Create one or two objects that may result from the memory block
	 * split. Note that partial freeing is only done by free_bootmem() and
	 * this happens before kmemleak_init() is called. The path below is
	 * only executed during early log recording in kmemleak_init(), so
	 * GFP_KERNEL is enough.
	 */
	start = object->pointer;
	end = object->pointer + object->size;
	if (ptr > start)
		create_object(start, ptr - start, object->min_count,
			      GFP_KERNEL);
	if (ptr + size < end)
		create_object(ptr + size, end - ptr - size, object->min_count,
			      GFP_KERNEL);

	put_object(object);
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

	spin_lock_irqsave(&object->lock, flags);
	__paint_it(object, color);
	spin_unlock_irqrestore(&object->lock, flags);
}

static void paint_ptr(unsigned long ptr, int color)
{
	struct kmemleak_object *object;

	object = find_and_get_object(ptr, 0);
	if (!object) {
		kmemleak_warn("Trying to color unknown object "
			      "at 0x%08lx as %s\n", ptr,
			      (color == KMEMLEAK_GREY) ? "Grey" :
			      (color == KMEMLEAK_BLACK) ? "Black" : "Unknown");
		return;
	}
	paint_it(object, color);
	put_object(object);
}

/*
 * Make a object permanently as gray-colored so that it can no longer be
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
	struct kmemleak_scan_area *area;

	object = find_and_get_object(ptr, 1);
	if (!object) {
		kmemleak_warn("Adding scan area to unknown object at 0x%08lx\n",
			      ptr);
		return;
	}

	area = kmem_cache_alloc(scan_area_cache, gfp & GFP_KMEMLEAK_MASK);
	if (!area) {
		kmemleak_warn("Cannot allocate a scan area\n");
		goto out;
	}

	spin_lock_irqsave(&object->lock, flags);
	if (ptr + size > object->pointer + object->size) {
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
	spin_unlock_irqrestore(&object->lock, flags);
out:
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

	spin_lock_irqsave(&object->lock, flags);
	object->flags |= OBJECT_NO_SCAN;
	spin_unlock_irqrestore(&object->lock, flags);
	put_object(object);
}

/*
 * Log an early kmemleak_* call to the early_log buffer. These calls will be
 * processed later once kmemleak is fully initialized.
 */
static void __init log_early(int op_type, const void *ptr, size_t size,
			     int min_count)
{
	unsigned long flags;
	struct early_log *log;

	if (crt_early_log >= ARRAY_SIZE(early_log)) {
		pr_warning("Early log buffer exceeded, "
			   "please increase DEBUG_KMEMLEAK_EARLY_LOG_SIZE\n");
		kmemleak_disable();
		return;
	}

	/*
	 * There is no need for locking since the kernel is still in UP mode
	 * at this stage. Disabling the IRQs is enough.
	 */
	local_irq_save(flags);
	log = &early_log[crt_early_log];
	log->op_type = op_type;
	log->ptr = ptr;
	log->size = size;
	log->min_count = min_count;
	if (op_type == KMEMLEAK_ALLOC)
		log->trace_len = __save_stack_trace(log->trace);
	crt_early_log++;
	local_irq_restore(flags);
}

/*
 * Log an early allocated block and populate the stack trace.
 */
static void early_alloc(struct early_log *log)
{
	struct kmemleak_object *object;
	unsigned long flags;
	int i;

	if (!atomic_read(&kmemleak_enabled) || !log->ptr || IS_ERR(log->ptr))
		return;

	/*
	 * RCU locking needed to ensure object is not freed via put_object().
	 */
	rcu_read_lock();
	object = create_object((unsigned long)log->ptr, log->size,
			       log->min_count, GFP_ATOMIC);
	if (!object)
		goto out;
	spin_lock_irqsave(&object->lock, flags);
	for (i = 0; i < log->trace_len; i++)
		object->trace[i] = log->trace[i];
	object->trace_len = log->trace_len;
	spin_unlock_irqrestore(&object->lock, flags);
out:
	rcu_read_unlock();
}

/*
 * Memory allocation function callback. This function is called from the
 * kernel allocators when a new block is allocated (kmem_cache_alloc, kmalloc,
 * vmalloc etc.).
 */
void __ref kmemleak_alloc(const void *ptr, size_t size, int min_count,
			  gfp_t gfp)
{
	pr_debug("%s(0x%p, %zu, %d)\n", __func__, ptr, size, min_count);

	if (atomic_read(&kmemleak_enabled) && ptr && !IS_ERR(ptr))
		create_object((unsigned long)ptr, size, min_count, gfp);
	else if (atomic_read(&kmemleak_early_log))
		log_early(KMEMLEAK_ALLOC, ptr, size, min_count);
}
EXPORT_SYMBOL_GPL(kmemleak_alloc);

/*
 * Memory freeing function callback. This function is called from the kernel
 * allocators when a block is freed (kmem_cache_free, kfree, vfree etc.).
 */
void __ref kmemleak_free(const void *ptr)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (atomic_read(&kmemleak_enabled) && ptr && !IS_ERR(ptr))
		delete_object_full((unsigned long)ptr);
	else if (atomic_read(&kmemleak_early_log))
		log_early(KMEMLEAK_FREE, ptr, 0, 0);
}
EXPORT_SYMBOL_GPL(kmemleak_free);

/*
 * Partial memory freeing function callback. This function is usually called
 * from bootmem allocator when (part of) a memory block is freed.
 */
void __ref kmemleak_free_part(const void *ptr, size_t size)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (atomic_read(&kmemleak_enabled) && ptr && !IS_ERR(ptr))
		delete_object_part((unsigned long)ptr, size);
	else if (atomic_read(&kmemleak_early_log))
		log_early(KMEMLEAK_FREE_PART, ptr, size, 0);
}
EXPORT_SYMBOL_GPL(kmemleak_free_part);

/*
 * Mark an already allocated memory block as a false positive. This will cause
 * the block to no longer be reported as leak and always be scanned.
 */
void __ref kmemleak_not_leak(const void *ptr)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (atomic_read(&kmemleak_enabled) && ptr && !IS_ERR(ptr))
		make_gray_object((unsigned long)ptr);
	else if (atomic_read(&kmemleak_early_log))
		log_early(KMEMLEAK_NOT_LEAK, ptr, 0, 0);
}
EXPORT_SYMBOL(kmemleak_not_leak);

/*
 * Ignore a memory block. This is usually done when it is known that the
 * corresponding block is not a leak and does not contain any references to
 * other allocated memory blocks.
 */
void __ref kmemleak_ignore(const void *ptr)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (atomic_read(&kmemleak_enabled) && ptr && !IS_ERR(ptr))
		make_black_object((unsigned long)ptr);
	else if (atomic_read(&kmemleak_early_log))
		log_early(KMEMLEAK_IGNORE, ptr, 0, 0);
}
EXPORT_SYMBOL(kmemleak_ignore);

/*
 * Limit the range to be scanned in an allocated memory block.
 */
void __ref kmemleak_scan_area(const void *ptr, size_t size, gfp_t gfp)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (atomic_read(&kmemleak_enabled) && ptr && !IS_ERR(ptr))
		add_scan_area((unsigned long)ptr, size, gfp);
	else if (atomic_read(&kmemleak_early_log))
		log_early(KMEMLEAK_SCAN_AREA, ptr, size, 0);
}
EXPORT_SYMBOL(kmemleak_scan_area);

/*
 * Inform kmemleak not to scan the given memory block.
 */
void __ref kmemleak_no_scan(const void *ptr)
{
	pr_debug("%s(0x%p)\n", __func__, ptr);

	if (atomic_read(&kmemleak_enabled) && ptr && !IS_ERR(ptr))
		object_no_scan((unsigned long)ptr);
	else if (atomic_read(&kmemleak_early_log))
		log_early(KMEMLEAK_NO_SCAN, ptr, 0, 0);
}
EXPORT_SYMBOL(kmemleak_no_scan);

/*
 * Update an object's checksum and return true if it was modified.
 */
static bool update_checksum(struct kmemleak_object *object)
{
	u32 old_csum = object->checksum;

	if (!kmemcheck_is_obj_initialized(object->pointer, object->size))
		return false;

	object->checksum = crc32(0, (void *)object->pointer, object->size);
	return object->checksum != old_csum;
}

/*
 * Memory scanning is a long process and it needs to be interruptable. This
 * function checks whether such interrupt condition occured.
 */
static int scan_should_stop(void)
{
	if (!atomic_read(&kmemleak_enabled))
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
		       struct kmemleak_object *scanned, int allow_resched)
{
	unsigned long *ptr;
	unsigned long *start = PTR_ALIGN(_start, BYTES_PER_POINTER);
	unsigned long *end = _end - (BYTES_PER_POINTER - 1);

	for (ptr = start; ptr < end; ptr++) {
		struct kmemleak_object *object;
		unsigned long flags;
		unsigned long pointer;

		if (allow_resched)
			cond_resched();
		if (scan_should_stop())
			break;

		/* don't scan uninitialized memory */
		if (!kmemcheck_is_obj_initialized((unsigned long)ptr,
						  BYTES_PER_POINTER))
			continue;

		pointer = *ptr;

		object = find_and_get_object(pointer, 1);
		if (!object)
			continue;
		if (object == scanned) {
			/* self referenced, ignore */
			put_object(object);
			continue;
		}

		/*
		 * Avoid the lockdep recursive warning on object->lock being
		 * previously acquired in scan_object(). These locks are
		 * enclosed by scan_mutex.
		 */
		spin_lock_irqsave_nested(&object->lock, flags,
					 SINGLE_DEPTH_NESTING);
		if (!color_white(object)) {
			/* non-orphan, ignored or new */
			spin_unlock_irqrestore(&object->lock, flags);
			put_object(object);
			continue;
		}

		/*
		 * Increase the object's reference count (number of pointers
		 * to the memory block). If this count reaches the required
		 * minimum, the object's color will become gray and it will be
		 * added to the gray_list.
		 */
		object->count++;
		if (color_gray(object)) {
			list_add_tail(&object->gray_list, &gray_list);
			spin_unlock_irqrestore(&object->lock, flags);
			continue;
		}

		spin_unlock_irqrestore(&object->lock, flags);
		put_object(object);
	}
}

/*
 * Scan a memory block corresponding to a kmemleak_object. A condition is
 * that object->use_count >= 1.
 */
static void scan_object(struct kmemleak_object *object)
{
	struct kmemleak_scan_area *area;
	struct hlist_node *elem;
	unsigned long flags;

	/*
	 * Once the object->lock is acquired, the corresponding memory block
	 * cannot be freed (the same lock is acquired in delete_object).
	 */
	spin_lock_irqsave(&object->lock, flags);
	if (object->flags & OBJECT_NO_SCAN)
		goto out;
	if (!(object->flags & OBJECT_ALLOCATED))
		/* already freed object */
		goto out;
	if (hlist_empty(&object->area_list)) {
		void *start = (void *)object->pointer;
		void *end = (void *)(object->pointer + object->size);

		while (start < end && (object->flags & OBJECT_ALLOCATED) &&
		       !(object->flags & OBJECT_NO_SCAN)) {
			scan_block(start, min(start + MAX_SCAN_SIZE, end),
				   object, 0);
			start += MAX_SCAN_SIZE;

			spin_unlock_irqrestore(&object->lock, flags);
			cond_resched();
			spin_lock_irqsave(&object->lock, flags);
		}
	} else
		hlist_for_each_entry(area, elem, &object->area_list, node)
			scan_block((void *)area->start,
				   (void *)(area->start + area->size),
				   object, 0);
out:
	spin_unlock_irqrestore(&object->lock, flags);
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
	int i;
	int new_leaks = 0;

	jiffies_last_scan = jiffies;

	/* prepare the kmemleak_object's */
	rcu_read_lock();
	list_for_each_entry_rcu(object, &object_list, object_list) {
		spin_lock_irqsave(&object->lock, flags);
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

		spin_unlock_irqrestore(&object->lock, flags);
	}
	rcu_read_unlock();

	/* data/bss scanning */
	scan_block(_sdata, _edata, NULL, 1);
	scan_block(__bss_start, __bss_stop, NULL, 1);

#ifdef CONFIG_SMP
	/* per-cpu sections scanning */
	for_each_possible_cpu(i)
		scan_block(__per_cpu_start + per_cpu_offset(i),
			   __per_cpu_end + per_cpu_offset(i), NULL, 1);
#endif

	/*
	 * Struct page scanning for each node. The code below is not yet safe
	 * with MEMORY_HOTPLUG.
	 */
	for_each_online_node(i) {
		pg_data_t *pgdat = NODE_DATA(i);
		unsigned long start_pfn = pgdat->node_start_pfn;
		unsigned long end_pfn = start_pfn + pgdat->node_spanned_pages;
		unsigned long pfn;

		for (pfn = start_pfn; pfn < end_pfn; pfn++) {
			struct page *page;

			if (!pfn_valid(pfn))
				continue;
			page = pfn_to_page(pfn);
			/* only scan if page is in use */
			if (page_count(page) == 0)
				continue;
			scan_block(page, page + 1, NULL, 1);
		}
	}

	/*
	 * Scanning the task stacks (may introduce false negatives).
	 */
	if (kmemleak_stack_scan) {
		struct task_struct *p, *g;

		read_lock(&tasklist_lock);
		do_each_thread(g, p) {
			scan_block(task_stack_page(p), task_stack_page(p) +
				   THREAD_SIZE, NULL, 0);
		} while_each_thread(g, p);
		read_unlock(&tasklist_lock);
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
		spin_lock_irqsave(&object->lock, flags);
		if (color_white(object) && (object->flags & OBJECT_ALLOCATED)
		    && update_checksum(object) && get_object(object)) {
			/* color it gray temporarily */
			object->count = object->min_count;
			list_add_tail(&object->gray_list, &gray_list);
		}
		spin_unlock_irqrestore(&object->lock, flags);
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
		spin_lock_irqsave(&object->lock, flags);
		if (unreferenced_object(object) &&
		    !(object->flags & OBJECT_REPORTED)) {
			object->flags |= OBJECT_REPORTED;
			new_leaks++;
		}
		spin_unlock_irqrestore(&object->lock, flags);
	}
	rcu_read_unlock();

	if (new_leaks)
		pr_info("%d new suspected memory leaks (see "
			"/sys/kernel/debug/kmemleak)\n", new_leaks);

}

/*
 * Thread function performing automatic memory scanning. Unreferenced objects
 * at the end of a memory scan are reported but only the first time.
 */
static int kmemleak_scan_thread(void *arg)
{
	static int first_run = 1;

	pr_info("Automatic memory scanning thread started\n");
	set_user_nice(current, 10);

	/*
	 * Wait before the first scan to allow the system to fully initialize.
	 */
	if (first_run) {
		first_run = 0;
		ssleep(SECS_FIRST_SCAN);
	}

	while (!kthread_should_stop()) {
		signed long timeout = jiffies_scan_wait;

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
		pr_warning("Failed to create the scan thread\n");
		scan_thread = NULL;
	}
}

/*
 * Stop the automatic memory scanning thread. This function must be called
 * with the scan_mutex held.
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
	struct list_head *n = &prev_obj->object_list;

	++(*pos);

	list_for_each_continue_rcu(n, &object_list) {
		next_obj = list_entry(n, struct kmemleak_object, object_list);
		if (get_object(next_obj))
			break;
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

	spin_lock_irqsave(&object->lock, flags);
	if ((object->flags & OBJECT_REPORTED) && unreferenced_object(object))
		print_unreferenced(seq, object);
	spin_unlock_irqrestore(&object->lock, flags);
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
	if (!atomic_read(&kmemleak_enabled))
		return -EBUSY;

	return seq_open(file, &kmemleak_seq_ops);
}

static int kmemleak_release(struct inode *inode, struct file *file)
{
	return seq_release(inode, file);
}

static int dump_str_object_info(const char *str)
{
	unsigned long flags;
	struct kmemleak_object *object;
	unsigned long addr;

	addr= simple_strtoul(str, NULL, 0);
	object = find_and_get_object(addr, 0);
	if (!object) {
		pr_info("Unknown object at 0x%08lx\n", addr);
		return -EINVAL;
	}

	spin_lock_irqsave(&object->lock, flags);
	dump_object_info(object);
	spin_unlock_irqrestore(&object->lock, flags);

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
		spin_lock_irqsave(&object->lock, flags);
		if ((object->flags & OBJECT_REPORTED) &&
		    unreferenced_object(object))
			__paint_it(object, KMEMLEAK_GREY);
		spin_unlock_irqrestore(&object->lock, flags);
	}
	rcu_read_unlock();
}

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
 *		  grey to ignore printing them
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
		unsigned long secs;

		ret = strict_strtoul(buf + 5, 0, &secs);
		if (ret < 0)
			goto out;
		stop_scan_thread();
		if (secs) {
			jiffies_scan_wait = msecs_to_jiffies(secs * 1000);
			start_scan_thread();
		}
	} else if (strncmp(buf, "scan", 4) == 0)
		kmemleak_scan();
	else if (strncmp(buf, "clear", 5) == 0)
		kmemleak_clear();
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
	.release	= kmemleak_release,
};

/*
 * Perform the freeing of the kmemleak internal objects after waiting for any
 * current memory scan to complete.
 */
static void kmemleak_do_cleanup(struct work_struct *work)
{
	struct kmemleak_object *object;

	mutex_lock(&scan_mutex);
	stop_scan_thread();

	rcu_read_lock();
	list_for_each_entry_rcu(object, &object_list, object_list)
		delete_object_full(object->pointer);
	rcu_read_unlock();
	mutex_unlock(&scan_mutex);
}

static DECLARE_WORK(cleanup_work, kmemleak_do_cleanup);

/*
 * Disable kmemleak. No memory allocation/freeing will be traced once this
 * function is called. Disabling kmemleak is an irreversible operation.
 */
static void kmemleak_disable(void)
{
	/* atomically check whether it was already invoked */
	if (atomic_cmpxchg(&kmemleak_error, 0, 1))
		return;

	/* stop any memory operation tracing */
	atomic_set(&kmemleak_early_log, 0);
	atomic_set(&kmemleak_enabled, 0);

	/* check whether it is too early for a kernel thread */
	if (atomic_read(&kmemleak_initialized))
		schedule_work(&cleanup_work);

	pr_info("Kernel memory leak detector disabled\n");
}

/*
 * Allow boot-time kmemleak disabling (enabled by default).
 */
static int kmemleak_boot_config(char *str)
{
	if (!str)
		return -EINVAL;
	if (strcmp(str, "off") == 0)
		kmemleak_disable();
	else if (strcmp(str, "on") != 0)
		return -EINVAL;
	return 0;
}
early_param("kmemleak", kmemleak_boot_config);

/*
 * Kmemleak initialization.
 */
void __init kmemleak_init(void)
{
	int i;
	unsigned long flags;

	jiffies_min_age = msecs_to_jiffies(MSECS_MIN_AGE);
	jiffies_scan_wait = msecs_to_jiffies(SECS_SCAN_WAIT * 1000);

	object_cache = KMEM_CACHE(kmemleak_object, SLAB_NOLEAKTRACE);
	scan_area_cache = KMEM_CACHE(kmemleak_scan_area, SLAB_NOLEAKTRACE);
	INIT_PRIO_TREE_ROOT(&object_tree_root);

	/* the kernel is still in UP mode, so disabling the IRQs is enough */
	local_irq_save(flags);
	if (!atomic_read(&kmemleak_error)) {
		atomic_set(&kmemleak_enabled, 1);
		atomic_set(&kmemleak_early_log, 0);
	}
	local_irq_restore(flags);

	/*
	 * This is the point where tracking allocations is safe. Automatic
	 * scanning is started during the late initcall. Add the early logged
	 * callbacks to the kmemleak infrastructure.
	 */
	for (i = 0; i < crt_early_log; i++) {
		struct early_log *log = &early_log[i];

		switch (log->op_type) {
		case KMEMLEAK_ALLOC:
			early_alloc(log);
			break;
		case KMEMLEAK_FREE:
			kmemleak_free(log->ptr);
			break;
		case KMEMLEAK_FREE_PART:
			kmemleak_free_part(log->ptr, log->size);
			break;
		case KMEMLEAK_NOT_LEAK:
			kmemleak_not_leak(log->ptr);
			break;
		case KMEMLEAK_IGNORE:
			kmemleak_ignore(log->ptr);
			break;
		case KMEMLEAK_SCAN_AREA:
			kmemleak_scan_area(log->ptr, log->size, GFP_KERNEL);
			break;
		case KMEMLEAK_NO_SCAN:
			kmemleak_no_scan(log->ptr);
			break;
		default:
			WARN_ON(1);
		}
	}
}

/*
 * Late initialization function.
 */
static int __init kmemleak_late_init(void)
{
	struct dentry *dentry;

	atomic_set(&kmemleak_initialized, 1);

	if (atomic_read(&kmemleak_error)) {
		/*
		 * Some error occured and kmemleak was disabled. There is a
		 * small chance that kmemleak_disable() was called immediately
		 * after setting kmemleak_initialized and we may end up with
		 * two clean-up threads but serialized by scan_mutex.
		 */
		schedule_work(&cleanup_work);
		return -ENOMEM;
	}

	dentry = debugfs_create_file("kmemleak", S_IRUGO, NULL, NULL,
				     &kmemleak_fops);
	if (!dentry)
		pr_warning("Failed to create the debugfs kmemleak file\n");
	mutex_lock(&scan_mutex);
	start_scan_thread();
	mutex_unlock(&scan_mutex);

	pr_info("Kernel memory leak detector initialized\n");

	return 0;
}
late_initcall(kmemleak_late_init);
