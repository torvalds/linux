/*
 * SLUB: A slab allocator that limits cache line use instead of queuing
 * objects in per cpu and per node lists.
 *
 * The allocator synchronizes using per slab locks or atomic operatios
 * and only uses a centralized lock to manage a pool of partial slabs.
 *
 * (C) 2007 SGI, Christoph Lameter
 * (C) 2011 Linux Foundation, Christoph Lameter
 */

#include <linux/mm.h>
#include <linux/swap.h> /* struct reclaim_state */
#include <linux/module.h>
#include <linux/bit_spinlock.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include "slab.h"
#include <linux/proc_fs.h>
#include <linux/notifier.h>
#include <linux/seq_file.h>
#include <linux/kasan.h>
#include <linux/kmemcheck.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/mempolicy.h>
#include <linux/ctype.h>
#include <linux/debugobjects.h>
#include <linux/kallsyms.h>
#include <linux/memory.h>
#include <linux/math64.h>
#include <linux/fault-inject.h>
#include <linux/stacktrace.h>
#include <linux/prefetch.h>
#include <linux/memcontrol.h>

#include <trace/events/kmem.h>

#include "internal.h"

/*
 * Lock order:
 *   1. slab_mutex (Global Mutex)
 *   2. node->list_lock
 *   3. slab_lock(page) (Only on some arches and for debugging)
 *
 *   slab_mutex
 *
 *   The role of the slab_mutex is to protect the list of all the slabs
 *   and to synchronize major metadata changes to slab cache structures.
 *
 *   The slab_lock is only used for debugging and on arches that do not
 *   have the ability to do a cmpxchg_double. It only protects the second
 *   double word in the page struct. Meaning
 *	A. page->freelist	-> List of object free in a page
 *	B. page->counters	-> Counters of objects
 *	C. page->frozen		-> frozen state
 *
 *   If a slab is frozen then it is exempt from list management. It is not
 *   on any list. The processor that froze the slab is the one who can
 *   perform list operations on the page. Other processors may put objects
 *   onto the freelist but the processor that froze the slab is the only
 *   one that can retrieve the objects from the page's freelist.
 *
 *   The list_lock protects the partial and full list on each node and
 *   the partial slab counter. If taken then no new slabs may be added or
 *   removed from the lists nor make the number of partial slabs be modified.
 *   (Note that the total number of slabs is an atomic value that may be
 *   modified without taking the list lock).
 *
 *   The list_lock is a centralized lock and thus we avoid taking it as
 *   much as possible. As long as SLUB does not have to handle partial
 *   slabs, operations can continue without any centralized lock. F.e.
 *   allocating a long series of objects that fill up slabs does not require
 *   the list lock.
 *   Interrupts are disabled during allocation and deallocation in order to
 *   make the slab allocator safe to use in the context of an irq. In addition
 *   interrupts are disabled to ensure that the processor does not change
 *   while handling per_cpu slabs, due to kernel preemption.
 *
 * SLUB assigns one slab for allocation to each processor.
 * Allocations only occur from these slabs called cpu slabs.
 *
 * Slabs with free elements are kept on a partial list and during regular
 * operations no list for full slabs is used. If an object in a full slab is
 * freed then the slab will show up again on the partial lists.
 * We track full slabs for debugging purposes though because otherwise we
 * cannot scan all objects.
 *
 * Slabs are freed when they become empty. Teardown and setup is
 * minimal so we rely on the page allocators per cpu caches for
 * fast frees and allocs.
 *
 * Overloading of page flags that are otherwise used for LRU management.
 *
 * PageActive 		The slab is frozen and exempt from list processing.
 * 			This means that the slab is dedicated to a purpose
 * 			such as satisfying allocations for a specific
 * 			processor. Objects may be freed in the slab while
 * 			it is frozen but slab_free will then skip the usual
 * 			list operations. It is up to the processor holding
 * 			the slab to integrate the slab into the slab lists
 * 			when the slab is no longer needed.
 *
 * 			One use of this flag is to mark slabs that are
 * 			used for allocations. Then such a slab becomes a cpu
 * 			slab. The cpu slab may be equipped with an additional
 * 			freelist that allows lockless access to
 * 			free objects in addition to the regular freelist
 * 			that requires the slab lock.
 *
 * PageError		Slab requires special handling due to debug
 * 			options set. This moves	slab handling out of
 * 			the fast path and disables lockless freelists.
 */

static inline int kmem_cache_debug(struct kmem_cache *s)
{
#ifdef CONFIG_SLUB_DEBUG
	return unlikely(s->flags & SLAB_DEBUG_FLAGS);
#else
	return 0;
#endif
}

static inline bool kmem_cache_has_cpu_partial(struct kmem_cache *s)
{
#ifdef CONFIG_SLUB_CPU_PARTIAL
	return !kmem_cache_debug(s);
#else
	return false;
#endif
}

/*
 * Issues still to be resolved:
 *
 * - Support PAGE_ALLOC_DEBUG. Should be easy to do.
 *
 * - Variable sizing of the per node arrays
 */

/* Enable to test recovery from slab corruption on boot */
#undef SLUB_RESILIENCY_TEST

/* Enable to log cmpxchg failures */
#undef SLUB_DEBUG_CMPXCHG

/*
 * Mininum number of partial slabs. These will be left on the partial
 * lists even if they are empty. kmem_cache_shrink may reclaim them.
 */
#define MIN_PARTIAL 5

/*
 * Maximum number of desirable partial slabs.
 * The existence of more partial slabs makes kmem_cache_shrink
 * sort the partial list by the number of objects in use.
 */
#define MAX_PARTIAL 10

#define DEBUG_DEFAULT_FLAGS (SLAB_DEBUG_FREE | SLAB_RED_ZONE | \
				SLAB_POISON | SLAB_STORE_USER)

/*
 * Debugging flags that require metadata to be stored in the slab.  These get
 * disabled when slub_debug=O is used and a cache's min order increases with
 * metadata.
 */
#define DEBUG_METADATA_FLAGS (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER)

#define OO_SHIFT	16
#define OO_MASK		((1 << OO_SHIFT) - 1)
#define MAX_OBJS_PER_PAGE	32767 /* since page.objects is u15 */

/* Internal SLUB flags */
#define __OBJECT_POISON		0x80000000UL /* Poison object */
#define __CMPXCHG_DOUBLE	0x40000000UL /* Use cmpxchg_double */

#ifdef CONFIG_SMP
static struct notifier_block slab_notifier;
#endif

/*
 * Tracking user of a slab.
 */
#define TRACK_ADDRS_COUNT 16
struct track {
	unsigned long addr;	/* Called from address */
#ifdef CONFIG_STACKTRACE
	unsigned long addrs[TRACK_ADDRS_COUNT];	/* Called from address */
#endif
	int cpu;		/* Was running on cpu */
	int pid;		/* Pid context */
	unsigned long when;	/* When did the operation occur */
};

enum track_item { TRACK_ALLOC, TRACK_FREE };

#ifdef CONFIG_SYSFS
static int sysfs_slab_add(struct kmem_cache *);
static int sysfs_slab_alias(struct kmem_cache *, const char *);
static void memcg_propagate_slab_attrs(struct kmem_cache *s);
#else
static inline int sysfs_slab_add(struct kmem_cache *s) { return 0; }
static inline int sysfs_slab_alias(struct kmem_cache *s, const char *p)
							{ return 0; }
static inline void memcg_propagate_slab_attrs(struct kmem_cache *s) { }
#endif

static inline void stat(const struct kmem_cache *s, enum stat_item si)
{
#ifdef CONFIG_SLUB_STATS
	/*
	 * The rmw is racy on a preemptible kernel but this is acceptable, so
	 * avoid this_cpu_add()'s irq-disable overhead.
	 */
	raw_cpu_inc(s->cpu_slab->stat[si]);
#endif
}

/********************************************************************
 * 			Core slab cache functions
 *******************************************************************/

/* Verify that a pointer has an address that is valid within a slab page */
static inline int check_valid_pointer(struct kmem_cache *s,
				struct page *page, const void *object)
{
	void *base;

	if (!object)
		return 1;

	base = page_address(page);
	if (object < base || object >= base + page->objects * s->size ||
		(object - base) % s->size) {
		return 0;
	}

	return 1;
}

static inline void *get_freepointer(struct kmem_cache *s, void *object)
{
	return *(void **)(object + s->offset);
}

static void prefetch_freepointer(const struct kmem_cache *s, void *object)
{
	prefetch(object + s->offset);
}

static inline void *get_freepointer_safe(struct kmem_cache *s, void *object)
{
	void *p;

#ifdef CONFIG_DEBUG_PAGEALLOC
	probe_kernel_read(&p, (void **)(object + s->offset), sizeof(p));
#else
	p = get_freepointer(s, object);
#endif
	return p;
}

static inline void set_freepointer(struct kmem_cache *s, void *object, void *fp)
{
	*(void **)(object + s->offset) = fp;
}

/* Loop over all objects in a slab */
#define for_each_object(__p, __s, __addr, __objects) \
	for (__p = (__addr); __p < (__addr) + (__objects) * (__s)->size;\
			__p += (__s)->size)

#define for_each_object_idx(__p, __idx, __s, __addr, __objects) \
	for (__p = (__addr), __idx = 1; __idx <= __objects;\
			__p += (__s)->size, __idx++)

/* Determine object index from a given position */
static inline int slab_index(void *p, struct kmem_cache *s, void *addr)
{
	return (p - addr) / s->size;
}

static inline size_t slab_ksize(const struct kmem_cache *s)
{
#ifdef CONFIG_SLUB_DEBUG
	/*
	 * Debugging requires use of the padding between object
	 * and whatever may come after it.
	 */
	if (s->flags & (SLAB_RED_ZONE | SLAB_POISON))
		return s->object_size;

#endif
	/*
	 * If we have the need to store the freelist pointer
	 * back there or track user information then we can
	 * only use the space before that information.
	 */
	if (s->flags & (SLAB_DESTROY_BY_RCU | SLAB_STORE_USER))
		return s->inuse;
	/*
	 * Else we can use all the padding etc for the allocation
	 */
	return s->size;
}

static inline int order_objects(int order, unsigned long size, int reserved)
{
	return ((PAGE_SIZE << order) - reserved) / size;
}

static inline struct kmem_cache_order_objects oo_make(int order,
		unsigned long size, int reserved)
{
	struct kmem_cache_order_objects x = {
		(order << OO_SHIFT) + order_objects(order, size, reserved)
	};

	return x;
}

static inline int oo_order(struct kmem_cache_order_objects x)
{
	return x.x >> OO_SHIFT;
}

static inline int oo_objects(struct kmem_cache_order_objects x)
{
	return x.x & OO_MASK;
}

/*
 * Per slab locking using the pagelock
 */
static __always_inline void slab_lock(struct page *page)
{
	bit_spin_lock(PG_locked, &page->flags);
}

static __always_inline void slab_unlock(struct page *page)
{
	__bit_spin_unlock(PG_locked, &page->flags);
}

static inline void set_page_slub_counters(struct page *page, unsigned long counters_new)
{
	struct page tmp;
	tmp.counters = counters_new;
	/*
	 * page->counters can cover frozen/inuse/objects as well
	 * as page->_count.  If we assign to ->counters directly
	 * we run the risk of losing updates to page->_count, so
	 * be careful and only assign to the fields we need.
	 */
	page->frozen  = tmp.frozen;
	page->inuse   = tmp.inuse;
	page->objects = tmp.objects;
}

/* Interrupts must be disabled (for the fallback code to work right) */
static inline bool __cmpxchg_double_slab(struct kmem_cache *s, struct page *page,
		void *freelist_old, unsigned long counters_old,
		void *freelist_new, unsigned long counters_new,
		const char *n)
{
	VM_BUG_ON(!irqs_disabled());
#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE) && \
    defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
	if (s->flags & __CMPXCHG_DOUBLE) {
		if (cmpxchg_double(&page->freelist, &page->counters,
				   freelist_old, counters_old,
				   freelist_new, counters_new))
			return true;
	} else
#endif
	{
		slab_lock(page);
		if (page->freelist == freelist_old &&
					page->counters == counters_old) {
			page->freelist = freelist_new;
			set_page_slub_counters(page, counters_new);
			slab_unlock(page);
			return true;
		}
		slab_unlock(page);
	}

	cpu_relax();
	stat(s, CMPXCHG_DOUBLE_FAIL);

#ifdef SLUB_DEBUG_CMPXCHG
	pr_info("%s %s: cmpxchg double redo ", n, s->name);
#endif

	return false;
}

static inline bool cmpxchg_double_slab(struct kmem_cache *s, struct page *page,
		void *freelist_old, unsigned long counters_old,
		void *freelist_new, unsigned long counters_new,
		const char *n)
{
#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE) && \
    defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
	if (s->flags & __CMPXCHG_DOUBLE) {
		if (cmpxchg_double(&page->freelist, &page->counters,
				   freelist_old, counters_old,
				   freelist_new, counters_new))
			return true;
	} else
#endif
	{
		unsigned long flags;

		local_irq_save(flags);
		slab_lock(page);
		if (page->freelist == freelist_old &&
					page->counters == counters_old) {
			page->freelist = freelist_new;
			set_page_slub_counters(page, counters_new);
			slab_unlock(page);
			local_irq_restore(flags);
			return true;
		}
		slab_unlock(page);
		local_irq_restore(flags);
	}

	cpu_relax();
	stat(s, CMPXCHG_DOUBLE_FAIL);

#ifdef SLUB_DEBUG_CMPXCHG
	pr_info("%s %s: cmpxchg double redo ", n, s->name);
#endif

	return false;
}

#ifdef CONFIG_SLUB_DEBUG
/*
 * Determine a map of object in use on a page.
 *
 * Node listlock must be held to guarantee that the page does
 * not vanish from under us.
 */
static void get_map(struct kmem_cache *s, struct page *page, unsigned long *map)
{
	void *p;
	void *addr = page_address(page);

	for (p = page->freelist; p; p = get_freepointer(s, p))
		set_bit(slab_index(p, s, addr), map);
}

/*
 * Debug settings:
 */
#if defined(CONFIG_SLUB_DEBUG_ON)
static int slub_debug = DEBUG_DEFAULT_FLAGS;
#elif defined(CONFIG_KASAN)
static int slub_debug = SLAB_STORE_USER;
#else
static int slub_debug;
#endif

static char *slub_debug_slabs;
static int disable_higher_order_debug;

/*
 * slub is about to manipulate internal object metadata.  This memory lies
 * outside the range of the allocated object, so accessing it would normally
 * be reported by kasan as a bounds error.  metadata_access_enable() is used
 * to tell kasan that these accesses are OK.
 */
static inline void metadata_access_enable(void)
{
	kasan_disable_current();
}

static inline void metadata_access_disable(void)
{
	kasan_enable_current();
}

/*
 * Object debugging
 */
static void print_section(char *text, u8 *addr, unsigned int length)
{
	metadata_access_enable();
	print_hex_dump(KERN_ERR, text, DUMP_PREFIX_ADDRESS, 16, 1, addr,
			length, 1);
	metadata_access_disable();
}

static struct track *get_track(struct kmem_cache *s, void *object,
	enum track_item alloc)
{
	struct track *p;

	if (s->offset)
		p = object + s->offset + sizeof(void *);
	else
		p = object + s->inuse;

	return p + alloc;
}

static void set_track(struct kmem_cache *s, void *object,
			enum track_item alloc, unsigned long addr)
{
	struct track *p = get_track(s, object, alloc);

	if (addr) {
#ifdef CONFIG_STACKTRACE
		struct stack_trace trace;
		int i;

		trace.nr_entries = 0;
		trace.max_entries = TRACK_ADDRS_COUNT;
		trace.entries = p->addrs;
		trace.skip = 3;
		metadata_access_enable();
		save_stack_trace(&trace);
		metadata_access_disable();

		/* See rant in lockdep.c */
		if (trace.nr_entries != 0 &&
		    trace.entries[trace.nr_entries - 1] == ULONG_MAX)
			trace.nr_entries--;

		for (i = trace.nr_entries; i < TRACK_ADDRS_COUNT; i++)
			p->addrs[i] = 0;
#endif
		p->addr = addr;
		p->cpu = smp_processor_id();
		p->pid = current->pid;
		p->when = jiffies;
	} else
		memset(p, 0, sizeof(struct track));
}

static void init_tracking(struct kmem_cache *s, void *object)
{
	if (!(s->flags & SLAB_STORE_USER))
		return;

	set_track(s, object, TRACK_FREE, 0UL);
	set_track(s, object, TRACK_ALLOC, 0UL);
}

static void print_track(const char *s, struct track *t)
{
	if (!t->addr)
		return;

	pr_err("INFO: %s in %pS age=%lu cpu=%u pid=%d\n",
	       s, (void *)t->addr, jiffies - t->when, t->cpu, t->pid);
#ifdef CONFIG_STACKTRACE
	{
		int i;
		for (i = 0; i < TRACK_ADDRS_COUNT; i++)
			if (t->addrs[i])
				pr_err("\t%pS\n", (void *)t->addrs[i]);
			else
				break;
	}
#endif
}

static void print_tracking(struct kmem_cache *s, void *object)
{
	if (!(s->flags & SLAB_STORE_USER))
		return;

	print_track("Allocated", get_track(s, object, TRACK_ALLOC));
	print_track("Freed", get_track(s, object, TRACK_FREE));
}

static void print_page_info(struct page *page)
{
	pr_err("INFO: Slab 0x%p objects=%u used=%u fp=0x%p flags=0x%04lx\n",
	       page, page->objects, page->inuse, page->freelist, page->flags);

}

static void slab_bug(struct kmem_cache *s, char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_err("=============================================================================\n");
	pr_err("BUG %s (%s): %pV\n", s->name, print_tainted(), &vaf);
	pr_err("-----------------------------------------------------------------------------\n\n");

	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);
	va_end(args);
}

static void slab_fix(struct kmem_cache *s, char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_err("FIX %s: %pV\n", s->name, &vaf);
	va_end(args);
}

static void print_trailer(struct kmem_cache *s, struct page *page, u8 *p)
{
	unsigned int off;	/* Offset of last byte */
	u8 *addr = page_address(page);

	print_tracking(s, p);

	print_page_info(page);

	pr_err("INFO: Object 0x%p @offset=%tu fp=0x%p\n\n",
	       p, p - addr, get_freepointer(s, p));

	if (p > addr + 16)
		print_section("Bytes b4 ", p - 16, 16);

	print_section("Object ", p, min_t(unsigned long, s->object_size,
				PAGE_SIZE));
	if (s->flags & SLAB_RED_ZONE)
		print_section("Redzone ", p + s->object_size,
			s->inuse - s->object_size);

	if (s->offset)
		off = s->offset + sizeof(void *);
	else
		off = s->inuse;

	if (s->flags & SLAB_STORE_USER)
		off += 2 * sizeof(struct track);

	if (off != s->size)
		/* Beginning of the filler is the free pointer */
		print_section("Padding ", p + off, s->size - off);

	dump_stack();
}

void object_err(struct kmem_cache *s, struct page *page,
			u8 *object, char *reason)
{
	slab_bug(s, "%s", reason);
	print_trailer(s, page, object);
}

static __printf(3, 4) void slab_err(struct kmem_cache *s, struct page *page,
			const char *fmt, ...)
{
	va_list args;
	char buf[100];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	slab_bug(s, "%s", buf);
	print_page_info(page);
	dump_stack();
}

static void init_object(struct kmem_cache *s, void *object, u8 val)
{
	u8 *p = object;

	if (s->flags & __OBJECT_POISON) {
		memset(p, POISON_FREE, s->object_size - 1);
		p[s->object_size - 1] = POISON_END;
	}

	if (s->flags & SLAB_RED_ZONE)
		memset(p + s->object_size, val, s->inuse - s->object_size);
}

static void restore_bytes(struct kmem_cache *s, char *message, u8 data,
						void *from, void *to)
{
	slab_fix(s, "Restoring 0x%p-0x%p=0x%x\n", from, to - 1, data);
	memset(from, data, to - from);
}

static int check_bytes_and_report(struct kmem_cache *s, struct page *page,
			u8 *object, char *what,
			u8 *start, unsigned int value, unsigned int bytes)
{
	u8 *fault;
	u8 *end;

	metadata_access_enable();
	fault = memchr_inv(start, value, bytes);
	metadata_access_disable();
	if (!fault)
		return 1;

	end = start + bytes;
	while (end > fault && end[-1] == value)
		end--;

	slab_bug(s, "%s overwritten", what);
	pr_err("INFO: 0x%p-0x%p. First byte 0x%x instead of 0x%x\n",
					fault, end - 1, fault[0], value);
	print_trailer(s, page, object);

	restore_bytes(s, what, value, fault, end);
	return 0;
}

/*
 * Object layout:
 *
 * object address
 * 	Bytes of the object to be managed.
 * 	If the freepointer may overlay the object then the free
 * 	pointer is the first word of the object.
 *
 * 	Poisoning uses 0x6b (POISON_FREE) and the last byte is
 * 	0xa5 (POISON_END)
 *
 * object + s->object_size
 * 	Padding to reach word boundary. This is also used for Redzoning.
 * 	Padding is extended by another word if Redzoning is enabled and
 * 	object_size == inuse.
 *
 * 	We fill with 0xbb (RED_INACTIVE) for inactive objects and with
 * 	0xcc (RED_ACTIVE) for objects in use.
 *
 * object + s->inuse
 * 	Meta data starts here.
 *
 * 	A. Free pointer (if we cannot overwrite object on free)
 * 	B. Tracking data for SLAB_STORE_USER
 * 	C. Padding to reach required alignment boundary or at mininum
 * 		one word if debugging is on to be able to detect writes
 * 		before the word boundary.
 *
 *	Padding is done using 0x5a (POISON_INUSE)
 *
 * object + s->size
 * 	Nothing is used beyond s->size.
 *
 * If slabcaches are merged then the object_size and inuse boundaries are mostly
 * ignored. And therefore no slab options that rely on these boundaries
 * may be used with merged slabcaches.
 */

static int check_pad_bytes(struct kmem_cache *s, struct page *page, u8 *p)
{
	unsigned long off = s->inuse;	/* The end of info */

	if (s->offset)
		/* Freepointer is placed after the object. */
		off += sizeof(void *);

	if (s->flags & SLAB_STORE_USER)
		/* We also have user information there */
		off += 2 * sizeof(struct track);

	if (s->size == off)
		return 1;

	return check_bytes_and_report(s, page, p, "Object padding",
				p + off, POISON_INUSE, s->size - off);
}

/* Check the pad bytes at the end of a slab page */
static int slab_pad_check(struct kmem_cache *s, struct page *page)
{
	u8 *start;
	u8 *fault;
	u8 *end;
	int length;
	int remainder;

	if (!(s->flags & SLAB_POISON))
		return 1;

	start = page_address(page);
	length = (PAGE_SIZE << compound_order(page)) - s->reserved;
	end = start + length;
	remainder = length % s->size;
	if (!remainder)
		return 1;

	metadata_access_enable();
	fault = memchr_inv(end - remainder, POISON_INUSE, remainder);
	metadata_access_disable();
	if (!fault)
		return 1;
	while (end > fault && end[-1] == POISON_INUSE)
		end--;

	slab_err(s, page, "Padding overwritten. 0x%p-0x%p", fault, end - 1);
	print_section("Padding ", end - remainder, remainder);

	restore_bytes(s, "slab padding", POISON_INUSE, end - remainder, end);
	return 0;
}

static int check_object(struct kmem_cache *s, struct page *page,
					void *object, u8 val)
{
	u8 *p = object;
	u8 *endobject = object + s->object_size;

	if (s->flags & SLAB_RED_ZONE) {
		if (!check_bytes_and_report(s, page, object, "Redzone",
			endobject, val, s->inuse - s->object_size))
			return 0;
	} else {
		if ((s->flags & SLAB_POISON) && s->object_size < s->inuse) {
			check_bytes_and_report(s, page, p, "Alignment padding",
				endobject, POISON_INUSE,
				s->inuse - s->object_size);
		}
	}

	if (s->flags & SLAB_POISON) {
		if (val != SLUB_RED_ACTIVE && (s->flags & __OBJECT_POISON) &&
			(!check_bytes_and_report(s, page, p, "Poison", p,
					POISON_FREE, s->object_size - 1) ||
			 !check_bytes_and_report(s, page, p, "Poison",
				p + s->object_size - 1, POISON_END, 1)))
			return 0;
		/*
		 * check_pad_bytes cleans up on its own.
		 */
		check_pad_bytes(s, page, p);
	}

	if (!s->offset && val == SLUB_RED_ACTIVE)
		/*
		 * Object and freepointer overlap. Cannot check
		 * freepointer while object is allocated.
		 */
		return 1;

	/* Check free pointer validity */
	if (!check_valid_pointer(s, page, get_freepointer(s, p))) {
		object_err(s, page, p, "Freepointer corrupt");
		/*
		 * No choice but to zap it and thus lose the remainder
		 * of the free objects in this slab. May cause
		 * another error because the object count is now wrong.
		 */
		set_freepointer(s, p, NULL);
		return 0;
	}
	return 1;
}

static int check_slab(struct kmem_cache *s, struct page *page)
{
	int maxobj;

	VM_BUG_ON(!irqs_disabled());

	if (!PageSlab(page)) {
		slab_err(s, page, "Not a valid slab page");
		return 0;
	}

	maxobj = order_objects(compound_order(page), s->size, s->reserved);
	if (page->objects > maxobj) {
		slab_err(s, page, "objects %u > max %u",
			page->objects, maxobj);
		return 0;
	}
	if (page->inuse > page->objects) {
		slab_err(s, page, "inuse %u > max %u",
			page->inuse, page->objects);
		return 0;
	}
	/* Slab_pad_check fixes things up after itself */
	slab_pad_check(s, page);
	return 1;
}

/*
 * Determine if a certain object on a page is on the freelist. Must hold the
 * slab lock to guarantee that the chains are in a consistent state.
 */
static int on_freelist(struct kmem_cache *s, struct page *page, void *search)
{
	int nr = 0;
	void *fp;
	void *object = NULL;
	int max_objects;

	fp = page->freelist;
	while (fp && nr <= page->objects) {
		if (fp == search)
			return 1;
		if (!check_valid_pointer(s, page, fp)) {
			if (object) {
				object_err(s, page, object,
					"Freechain corrupt");
				set_freepointer(s, object, NULL);
			} else {
				slab_err(s, page, "Freepointer corrupt");
				page->freelist = NULL;
				page->inuse = page->objects;
				slab_fix(s, "Freelist cleared");
				return 0;
			}
			break;
		}
		object = fp;
		fp = get_freepointer(s, object);
		nr++;
	}

	max_objects = order_objects(compound_order(page), s->size, s->reserved);
	if (max_objects > MAX_OBJS_PER_PAGE)
		max_objects = MAX_OBJS_PER_PAGE;

	if (page->objects != max_objects) {
		slab_err(s, page, "Wrong number of objects. Found %d but "
			"should be %d", page->objects, max_objects);
		page->objects = max_objects;
		slab_fix(s, "Number of objects adjusted.");
	}
	if (page->inuse != page->objects - nr) {
		slab_err(s, page, "Wrong object count. Counter is %d but "
			"counted were %d", page->inuse, page->objects - nr);
		page->inuse = page->objects - nr;
		slab_fix(s, "Object count adjusted.");
	}
	return search == NULL;
}

static void trace(struct kmem_cache *s, struct page *page, void *object,
								int alloc)
{
	if (s->flags & SLAB_TRACE) {
		pr_info("TRACE %s %s 0x%p inuse=%d fp=0x%p\n",
			s->name,
			alloc ? "alloc" : "free",
			object, page->inuse,
			page->freelist);

		if (!alloc)
			print_section("Object ", (void *)object,
					s->object_size);

		dump_stack();
	}
}

/*
 * Tracking of fully allocated slabs for debugging purposes.
 */
static void add_full(struct kmem_cache *s,
	struct kmem_cache_node *n, struct page *page)
{
	if (!(s->flags & SLAB_STORE_USER))
		return;

	lockdep_assert_held(&n->list_lock);
	list_add(&page->lru, &n->full);
}

static void remove_full(struct kmem_cache *s, struct kmem_cache_node *n, struct page *page)
{
	if (!(s->flags & SLAB_STORE_USER))
		return;

	lockdep_assert_held(&n->list_lock);
	list_del(&page->lru);
}

/* Tracking of the number of slabs for debugging purposes */
static inline unsigned long slabs_node(struct kmem_cache *s, int node)
{
	struct kmem_cache_node *n = get_node(s, node);

	return atomic_long_read(&n->nr_slabs);
}

static inline unsigned long node_nr_slabs(struct kmem_cache_node *n)
{
	return atomic_long_read(&n->nr_slabs);
}

static inline void inc_slabs_node(struct kmem_cache *s, int node, int objects)
{
	struct kmem_cache_node *n = get_node(s, node);

	/*
	 * May be called early in order to allocate a slab for the
	 * kmem_cache_node structure. Solve the chicken-egg
	 * dilemma by deferring the increment of the count during
	 * bootstrap (see early_kmem_cache_node_alloc).
	 */
	if (likely(n)) {
		atomic_long_inc(&n->nr_slabs);
		atomic_long_add(objects, &n->total_objects);
	}
}
static inline void dec_slabs_node(struct kmem_cache *s, int node, int objects)
{
	struct kmem_cache_node *n = get_node(s, node);

	atomic_long_dec(&n->nr_slabs);
	atomic_long_sub(objects, &n->total_objects);
}

/* Object debug checks for alloc/free paths */
static void setup_object_debug(struct kmem_cache *s, struct page *page,
								void *object)
{
	if (!(s->flags & (SLAB_STORE_USER|SLAB_RED_ZONE|__OBJECT_POISON)))
		return;

	init_object(s, object, SLUB_RED_INACTIVE);
	init_tracking(s, object);
}

static noinline int alloc_debug_processing(struct kmem_cache *s,
					struct page *page,
					void *object, unsigned long addr)
{
	if (!check_slab(s, page))
		goto bad;

	if (!check_valid_pointer(s, page, object)) {
		object_err(s, page, object, "Freelist Pointer check fails");
		goto bad;
	}

	if (!check_object(s, page, object, SLUB_RED_INACTIVE))
		goto bad;

	/* Success perform special debug activities for allocs */
	if (s->flags & SLAB_STORE_USER)
		set_track(s, object, TRACK_ALLOC, addr);
	trace(s, page, object, 1);
	init_object(s, object, SLUB_RED_ACTIVE);
	return 1;

bad:
	if (PageSlab(page)) {
		/*
		 * If this is a slab page then lets do the best we can
		 * to avoid issues in the future. Marking all objects
		 * as used avoids touching the remaining objects.
		 */
		slab_fix(s, "Marking all objects used");
		page->inuse = page->objects;
		page->freelist = NULL;
	}
	return 0;
}

/* Supports checking bulk free of a constructed freelist */
static noinline struct kmem_cache_node *free_debug_processing(
	struct kmem_cache *s, struct page *page,
	void *head, void *tail, int bulk_cnt,
	unsigned long addr, unsigned long *flags)
{
	struct kmem_cache_node *n = get_node(s, page_to_nid(page));
	void *object = head;
	int cnt = 0;

	spin_lock_irqsave(&n->list_lock, *flags);
	slab_lock(page);

	if (!check_slab(s, page))
		goto fail;

next_object:
	cnt++;

	if (!check_valid_pointer(s, page, object)) {
		slab_err(s, page, "Invalid object pointer 0x%p", object);
		goto fail;
	}

	if (on_freelist(s, page, object)) {
		object_err(s, page, object, "Object already free");
		goto fail;
	}

	if (!check_object(s, page, object, SLUB_RED_ACTIVE))
		goto out;

	if (unlikely(s != page->slab_cache)) {
		if (!PageSlab(page)) {
			slab_err(s, page, "Attempt to free object(0x%p) "
				"outside of slab", object);
		} else if (!page->slab_cache) {
			pr_err("SLUB <none>: no slab for object 0x%p.\n",
			       object);
			dump_stack();
		} else
			object_err(s, page, object,
					"page slab pointer corrupt.");
		goto fail;
	}

	if (s->flags & SLAB_STORE_USER)
		set_track(s, object, TRACK_FREE, addr);
	trace(s, page, object, 0);
	/* Freepointer not overwritten by init_object(), SLAB_POISON moved it */
	init_object(s, object, SLUB_RED_INACTIVE);

	/* Reached end of constructed freelist yet? */
	if (object != tail) {
		object = get_freepointer(s, object);
		goto next_object;
	}
out:
	if (cnt != bulk_cnt)
		slab_err(s, page, "Bulk freelist count(%d) invalid(%d)\n",
			 bulk_cnt, cnt);

	slab_unlock(page);
	/*
	 * Keep node_lock to preserve integrity
	 * until the object is actually freed
	 */
	return n;

fail:
	slab_unlock(page);
	spin_unlock_irqrestore(&n->list_lock, *flags);
	slab_fix(s, "Object at 0x%p not freed", object);
	return NULL;
}

static int __init setup_slub_debug(char *str)
{
	slub_debug = DEBUG_DEFAULT_FLAGS;
	if (*str++ != '=' || !*str)
		/*
		 * No options specified. Switch on full debugging.
		 */
		goto out;

	if (*str == ',')
		/*
		 * No options but restriction on slabs. This means full
		 * debugging for slabs matching a pattern.
		 */
		goto check_slabs;

	slub_debug = 0;
	if (*str == '-')
		/*
		 * Switch off all debugging measures.
		 */
		goto out;

	/*
	 * Determine which debug features should be switched on
	 */
	for (; *str && *str != ','; str++) {
		switch (tolower(*str)) {
		case 'f':
			slub_debug |= SLAB_DEBUG_FREE;
			break;
		case 'z':
			slub_debug |= SLAB_RED_ZONE;
			break;
		case 'p':
			slub_debug |= SLAB_POISON;
			break;
		case 'u':
			slub_debug |= SLAB_STORE_USER;
			break;
		case 't':
			slub_debug |= SLAB_TRACE;
			break;
		case 'a':
			slub_debug |= SLAB_FAILSLAB;
			break;
		case 'o':
			/*
			 * Avoid enabling debugging on caches if its minimum
			 * order would increase as a result.
			 */
			disable_higher_order_debug = 1;
			break;
		default:
			pr_err("slub_debug option '%c' unknown. skipped\n",
			       *str);
		}
	}

check_slabs:
	if (*str == ',')
		slub_debug_slabs = str + 1;
out:
	return 1;
}

__setup("slub_debug", setup_slub_debug);

unsigned long kmem_cache_flags(unsigned long object_size,
	unsigned long flags, const char *name,
	void (*ctor)(void *))
{
	/*
	 * Enable debugging if selected on the kernel commandline.
	 */
	if (slub_debug && (!slub_debug_slabs || (name &&
		!strncmp(slub_debug_slabs, name, strlen(slub_debug_slabs)))))
		flags |= slub_debug;

	return flags;
}
#else /* !CONFIG_SLUB_DEBUG */
static inline void setup_object_debug(struct kmem_cache *s,
			struct page *page, void *object) {}

static inline int alloc_debug_processing(struct kmem_cache *s,
	struct page *page, void *object, unsigned long addr) { return 0; }

static inline struct kmem_cache_node *free_debug_processing(
	struct kmem_cache *s, struct page *page,
	void *head, void *tail, int bulk_cnt,
	unsigned long addr, unsigned long *flags) { return NULL; }

static inline int slab_pad_check(struct kmem_cache *s, struct page *page)
			{ return 1; }
static inline int check_object(struct kmem_cache *s, struct page *page,
			void *object, u8 val) { return 1; }
static inline void add_full(struct kmem_cache *s, struct kmem_cache_node *n,
					struct page *page) {}
static inline void remove_full(struct kmem_cache *s, struct kmem_cache_node *n,
					struct page *page) {}
unsigned long kmem_cache_flags(unsigned long object_size,
	unsigned long flags, const char *name,
	void (*ctor)(void *))
{
	return flags;
}
#define slub_debug 0

#define disable_higher_order_debug 0

static inline unsigned long slabs_node(struct kmem_cache *s, int node)
							{ return 0; }
static inline unsigned long node_nr_slabs(struct kmem_cache_node *n)
							{ return 0; }
static inline void inc_slabs_node(struct kmem_cache *s, int node,
							int objects) {}
static inline void dec_slabs_node(struct kmem_cache *s, int node,
							int objects) {}

#endif /* CONFIG_SLUB_DEBUG */

/*
 * Hooks for other subsystems that check memory allocations. In a typical
 * production configuration these hooks all should produce no code at all.
 */
static inline void kmalloc_large_node_hook(void *ptr, size_t size, gfp_t flags)
{
	kmemleak_alloc(ptr, size, 1, flags);
	kasan_kmalloc_large(ptr, size);
}

static inline void kfree_hook(const void *x)
{
	kmemleak_free(x);
	kasan_kfree_large(x);
}

static inline struct kmem_cache *slab_pre_alloc_hook(struct kmem_cache *s,
						     gfp_t flags)
{
	flags &= gfp_allowed_mask;
	lockdep_trace_alloc(flags);
	might_sleep_if(gfpflags_allow_blocking(flags));

	if (should_failslab(s->object_size, flags, s->flags))
		return NULL;

	return memcg_kmem_get_cache(s, flags);
}

static inline void slab_post_alloc_hook(struct kmem_cache *s, gfp_t flags,
					size_t size, void **p)
{
	size_t i;

	flags &= gfp_allowed_mask;
	for (i = 0; i < size; i++) {
		void *object = p[i];

		kmemcheck_slab_alloc(s, flags, object, slab_ksize(s));
		kmemleak_alloc_recursive(object, s->object_size, 1,
					 s->flags, flags);
		kasan_slab_alloc(s, object);
	}
	memcg_kmem_put_cache(s);
}

static inline void slab_free_hook(struct kmem_cache *s, void *x)
{
	kmemleak_free_recursive(x, s->flags);

	/*
	 * Trouble is that we may no longer disable interrupts in the fast path
	 * So in order to make the debug calls that expect irqs to be
	 * disabled we need to disable interrupts temporarily.
	 */
#if defined(CONFIG_KMEMCHECK) || defined(CONFIG_LOCKDEP)
	{
		unsigned long flags;

		local_irq_save(flags);
		kmemcheck_slab_free(s, x, s->object_size);
		debug_check_no_locks_freed(x, s->object_size);
		local_irq_restore(flags);
	}
#endif
	if (!(s->flags & SLAB_DEBUG_OBJECTS))
		debug_check_no_obj_freed(x, s->object_size);

	kasan_slab_free(s, x);
}

static inline void slab_free_freelist_hook(struct kmem_cache *s,
					   void *head, void *tail)
{
/*
 * Compiler cannot detect this function can be removed if slab_free_hook()
 * evaluates to nothing.  Thus, catch all relevant config debug options here.
 */
#if defined(CONFIG_KMEMCHECK) ||		\
	defined(CONFIG_LOCKDEP)	||		\
	defined(CONFIG_DEBUG_KMEMLEAK) ||	\
	defined(CONFIG_DEBUG_OBJECTS_FREE) ||	\
	defined(CONFIG_KASAN)

	void *object = head;
	void *tail_obj = tail ? : head;

	do {
		slab_free_hook(s, object);
	} while ((object != tail_obj) &&
		 (object = get_freepointer(s, object)));
#endif
}

static void setup_object(struct kmem_cache *s, struct page *page,
				void *object)
{
	setup_object_debug(s, page, object);
	if (unlikely(s->ctor)) {
		kasan_unpoison_object_data(s, object);
		s->ctor(object);
		kasan_poison_object_data(s, object);
	}
}

/*
 * Slab allocation and freeing
 */
static inline struct page *alloc_slab_page(struct kmem_cache *s,
		gfp_t flags, int node, struct kmem_cache_order_objects oo)
{
	struct page *page;
	int order = oo_order(oo);

	flags |= __GFP_NOTRACK;

	if (node == NUMA_NO_NODE)
		page = alloc_pages(flags, order);
	else
		page = __alloc_pages_node(node, flags, order);

	if (page && memcg_charge_slab(page, flags, order, s)) {
		__free_pages(page, order);
		page = NULL;
	}

	return page;
}

static struct page *allocate_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	struct page *page;
	struct kmem_cache_order_objects oo = s->oo;
	gfp_t alloc_gfp;
	void *start, *p;
	int idx, order;

	flags &= gfp_allowed_mask;

	if (gfpflags_allow_blocking(flags))
		local_irq_enable();

	flags |= s->allocflags;

	/*
	 * Let the initial higher-order allocation fail under memory pressure
	 * so we fall-back to the minimum order allocation.
	 */
	alloc_gfp = (flags | __GFP_NOWARN | __GFP_NORETRY) & ~__GFP_NOFAIL;
	if ((alloc_gfp & __GFP_DIRECT_RECLAIM) && oo_order(oo) > oo_order(s->min))
		alloc_gfp = (alloc_gfp | __GFP_NOMEMALLOC) & ~__GFP_DIRECT_RECLAIM;

	page = alloc_slab_page(s, alloc_gfp, node, oo);
	if (unlikely(!page)) {
		oo = s->min;
		alloc_gfp = flags;
		/*
		 * Allocation may have failed due to fragmentation.
		 * Try a lower order alloc if possible
		 */
		page = alloc_slab_page(s, alloc_gfp, node, oo);
		if (unlikely(!page))
			goto out;
		stat(s, ORDER_FALLBACK);
	}

	if (kmemcheck_enabled &&
	    !(s->flags & (SLAB_NOTRACK | DEBUG_DEFAULT_FLAGS))) {
		int pages = 1 << oo_order(oo);

		kmemcheck_alloc_shadow(page, oo_order(oo), alloc_gfp, node);

		/*
		 * Objects from caches that have a constructor don't get
		 * cleared when they're allocated, so we need to do it here.
		 */
		if (s->ctor)
			kmemcheck_mark_uninitialized_pages(page, pages);
		else
			kmemcheck_mark_unallocated_pages(page, pages);
	}

	page->objects = oo_objects(oo);

	order = compound_order(page);
	page->slab_cache = s;
	__SetPageSlab(page);
	if (page_is_pfmemalloc(page))
		SetPageSlabPfmemalloc(page);

	start = page_address(page);

	if (unlikely(s->flags & SLAB_POISON))
		memset(start, POISON_INUSE, PAGE_SIZE << order);

	kasan_poison_slab(page);

	for_each_object_idx(p, idx, s, start, page->objects) {
		setup_object(s, page, p);
		if (likely(idx < page->objects))
			set_freepointer(s, p, p + s->size);
		else
			set_freepointer(s, p, NULL);
	}

	page->freelist = start;
	page->inuse = page->objects;
	page->frozen = 1;

out:
	if (gfpflags_allow_blocking(flags))
		local_irq_disable();
	if (!page)
		return NULL;

	mod_zone_page_state(page_zone(page),
		(s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE : NR_SLAB_UNRECLAIMABLE,
		1 << oo_order(oo));

	inc_slabs_node(s, page_to_nid(page), page->objects);

	return page;
}

static struct page *new_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	if (unlikely(flags & GFP_SLAB_BUG_MASK)) {
		pr_emerg("gfp: %u\n", flags & GFP_SLAB_BUG_MASK);
		BUG();
	}

	return allocate_slab(s,
		flags & (GFP_RECLAIM_MASK | GFP_CONSTRAINT_MASK), node);
}

static void __free_slab(struct kmem_cache *s, struct page *page)
{
	int order = compound_order(page);
	int pages = 1 << order;

	if (kmem_cache_debug(s)) {
		void *p;

		slab_pad_check(s, page);
		for_each_object(p, s, page_address(page),
						page->objects)
			check_object(s, page, p, SLUB_RED_INACTIVE);
	}

	kmemcheck_free_shadow(page, compound_order(page));

	mod_zone_page_state(page_zone(page),
		(s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE : NR_SLAB_UNRECLAIMABLE,
		-pages);

	__ClearPageSlabPfmemalloc(page);
	__ClearPageSlab(page);

	page_mapcount_reset(page);
	if (current->reclaim_state)
		current->reclaim_state->reclaimed_slab += pages;
	__free_kmem_pages(page, order);
}

#define need_reserve_slab_rcu						\
	(sizeof(((struct page *)NULL)->lru) < sizeof(struct rcu_head))

static void rcu_free_slab(struct rcu_head *h)
{
	struct page *page;

	if (need_reserve_slab_rcu)
		page = virt_to_head_page(h);
	else
		page = container_of((struct list_head *)h, struct page, lru);

	__free_slab(page->slab_cache, page);
}

static void free_slab(struct kmem_cache *s, struct page *page)
{
	if (unlikely(s->flags & SLAB_DESTROY_BY_RCU)) {
		struct rcu_head *head;

		if (need_reserve_slab_rcu) {
			int order = compound_order(page);
			int offset = (PAGE_SIZE << order) - s->reserved;

			VM_BUG_ON(s->reserved != sizeof(*head));
			head = page_address(page) + offset;
		} else {
			head = &page->rcu_head;
		}

		call_rcu(head, rcu_free_slab);
	} else
		__free_slab(s, page);
}

static void discard_slab(struct kmem_cache *s, struct page *page)
{
	dec_slabs_node(s, page_to_nid(page), page->objects);
	free_slab(s, page);
}

/*
 * Management of partially allocated slabs.
 */
static inline void
__add_partial(struct kmem_cache_node *n, struct page *page, int tail)
{
	n->nr_partial++;
	if (tail == DEACTIVATE_TO_TAIL)
		list_add_tail(&page->lru, &n->partial);
	else
		list_add(&page->lru, &n->partial);
}

static inline void add_partial(struct kmem_cache_node *n,
				struct page *page, int tail)
{
	lockdep_assert_held(&n->list_lock);
	__add_partial(n, page, tail);
}

static inline void
__remove_partial(struct kmem_cache_node *n, struct page *page)
{
	list_del(&page->lru);
	n->nr_partial--;
}

static inline void remove_partial(struct kmem_cache_node *n,
					struct page *page)
{
	lockdep_assert_held(&n->list_lock);
	__remove_partial(n, page);
}

/*
 * Remove slab from the partial list, freeze it and
 * return the pointer to the freelist.
 *
 * Returns a list of objects or NULL if it fails.
 */
static inline void *acquire_slab(struct kmem_cache *s,
		struct kmem_cache_node *n, struct page *page,
		int mode, int *objects)
{
	void *freelist;
	unsigned long counters;
	struct page new;

	lockdep_assert_held(&n->list_lock);

	/*
	 * Zap the freelist and set the frozen bit.
	 * The old freelist is the list of objects for the
	 * per cpu allocation list.
	 */
	freelist = page->freelist;
	counters = page->counters;
	new.counters = counters;
	*objects = new.objects - new.inuse;
	if (mode) {
		new.inuse = page->objects;
		new.freelist = NULL;
	} else {
		new.freelist = freelist;
	}

	VM_BUG_ON(new.frozen);
	new.frozen = 1;

	if (!__cmpxchg_double_slab(s, page,
			freelist, counters,
			new.freelist, new.counters,
			"acquire_slab"))
		return NULL;

	remove_partial(n, page);
	WARN_ON(!freelist);
	return freelist;
}

static void put_cpu_partial(struct kmem_cache *s, struct page *page, int drain);
static inline bool pfmemalloc_match(struct page *page, gfp_t gfpflags);

/*
 * Try to allocate a partial slab from a specific node.
 */
static void *get_partial_node(struct kmem_cache *s, struct kmem_cache_node *n,
				struct kmem_cache_cpu *c, gfp_t flags)
{
	struct page *page, *page2;
	void *object = NULL;
	unsigned int available = 0;
	int objects;

	/*
	 * Racy check. If we mistakenly see no partial slabs then we
	 * just allocate an empty slab. If we mistakenly try to get a
	 * partial slab and there is none available then get_partials()
	 * will return NULL.
	 */
	if (!n || !n->nr_partial)
		return NULL;

	spin_lock(&n->list_lock);
	list_for_each_entry_safe(page, page2, &n->partial, lru) {
		void *t;

		if (!pfmemalloc_match(page, flags))
			continue;

		t = acquire_slab(s, n, page, object == NULL, &objects);
		if (!t)
			break;

		available += objects;
		if (!object) {
			c->page = page;
			stat(s, ALLOC_FROM_PARTIAL);
			object = t;
		} else {
			put_cpu_partial(s, page, 0);
			stat(s, CPU_PARTIAL_NODE);
		}
		if (!kmem_cache_has_cpu_partial(s)
			|| available > s->cpu_partial / 2)
			break;

	}
	spin_unlock(&n->list_lock);
	return object;
}

/*
 * Get a page from somewhere. Search in increasing NUMA distances.
 */
static void *get_any_partial(struct kmem_cache *s, gfp_t flags,
		struct kmem_cache_cpu *c)
{
#ifdef CONFIG_NUMA
	struct zonelist *zonelist;
	struct zoneref *z;
	struct zone *zone;
	enum zone_type high_zoneidx = gfp_zone(flags);
	void *object;
	unsigned int cpuset_mems_cookie;

	/*
	 * The defrag ratio allows a configuration of the tradeoffs between
	 * inter node defragmentation and node local allocations. A lower
	 * defrag_ratio increases the tendency to do local allocations
	 * instead of attempting to obtain partial slabs from other nodes.
	 *
	 * If the defrag_ratio is set to 0 then kmalloc() always
	 * returns node local objects. If the ratio is higher then kmalloc()
	 * may return off node objects because partial slabs are obtained
	 * from other nodes and filled up.
	 *
	 * If /sys/kernel/slab/xx/defrag_ratio is set to 100 (which makes
	 * defrag_ratio = 1000) then every (well almost) allocation will
	 * first attempt to defrag slab caches on other nodes. This means
	 * scanning over all nodes to look for partial slabs which may be
	 * expensive if we do it every time we are trying to find a slab
	 * with available objects.
	 */
	if (!s->remote_node_defrag_ratio ||
			get_cycles() % 1024 > s->remote_node_defrag_ratio)
		return NULL;

	do {
		cpuset_mems_cookie = read_mems_allowed_begin();
		zonelist = node_zonelist(mempolicy_slab_node(), flags);
		for_each_zone_zonelist(zone, z, zonelist, high_zoneidx) {
			struct kmem_cache_node *n;

			n = get_node(s, zone_to_nid(zone));

			if (n && cpuset_zone_allowed(zone, flags) &&
					n->nr_partial > s->min_partial) {
				object = get_partial_node(s, n, c, flags);
				if (object) {
					/*
					 * Don't check read_mems_allowed_retry()
					 * here - if mems_allowed was updated in
					 * parallel, that was a harmless race
					 * between allocation and the cpuset
					 * update
					 */
					return object;
				}
			}
		}
	} while (read_mems_allowed_retry(cpuset_mems_cookie));
#endif
	return NULL;
}

/*
 * Get a partial page, lock it and return it.
 */
static void *get_partial(struct kmem_cache *s, gfp_t flags, int node,
		struct kmem_cache_cpu *c)
{
	void *object;
	int searchnode = node;

	if (node == NUMA_NO_NODE)
		searchnode = numa_mem_id();
	else if (!node_present_pages(node))
		searchnode = node_to_mem_node(node);

	object = get_partial_node(s, get_node(s, searchnode), c, flags);
	if (object || node != NUMA_NO_NODE)
		return object;

	return get_any_partial(s, flags, c);
}

#ifdef CONFIG_PREEMPT
/*
 * Calculate the next globally unique transaction for disambiguiation
 * during cmpxchg. The transactions start with the cpu number and are then
 * incremented by CONFIG_NR_CPUS.
 */
#define TID_STEP  roundup_pow_of_two(CONFIG_NR_CPUS)
#else
/*
 * No preemption supported therefore also no need to check for
 * different cpus.
 */
#define TID_STEP 1
#endif

static inline unsigned long next_tid(unsigned long tid)
{
	return tid + TID_STEP;
}

static inline unsigned int tid_to_cpu(unsigned long tid)
{
	return tid % TID_STEP;
}

static inline unsigned long tid_to_event(unsigned long tid)
{
	return tid / TID_STEP;
}

static inline unsigned int init_tid(int cpu)
{
	return cpu;
}

static inline void note_cmpxchg_failure(const char *n,
		const struct kmem_cache *s, unsigned long tid)
{
#ifdef SLUB_DEBUG_CMPXCHG
	unsigned long actual_tid = __this_cpu_read(s->cpu_slab->tid);

	pr_info("%s %s: cmpxchg redo ", n, s->name);

#ifdef CONFIG_PREEMPT
	if (tid_to_cpu(tid) != tid_to_cpu(actual_tid))
		pr_warn("due to cpu change %d -> %d\n",
			tid_to_cpu(tid), tid_to_cpu(actual_tid));
	else
#endif
	if (tid_to_event(tid) != tid_to_event(actual_tid))
		pr_warn("due to cpu running other code. Event %ld->%ld\n",
			tid_to_event(tid), tid_to_event(actual_tid));
	else
		pr_warn("for unknown reason: actual=%lx was=%lx target=%lx\n",
			actual_tid, tid, next_tid(tid));
#endif
	stat(s, CMPXCHG_DOUBLE_CPU_FAIL);
}

static void init_kmem_cache_cpus(struct kmem_cache *s)
{
	int cpu;

	for_each_possible_cpu(cpu)
		per_cpu_ptr(s->cpu_slab, cpu)->tid = init_tid(cpu);
}

/*
 * Remove the cpu slab
 */
static void deactivate_slab(struct kmem_cache *s, struct page *page,
				void *freelist)
{
	enum slab_modes { M_NONE, M_PARTIAL, M_FULL, M_FREE };
	struct kmem_cache_node *n = get_node(s, page_to_nid(page));
	int lock = 0;
	enum slab_modes l = M_NONE, m = M_NONE;
	void *nextfree;
	int tail = DEACTIVATE_TO_HEAD;
	struct page new;
	struct page old;

	if (page->freelist) {
		stat(s, DEACTIVATE_REMOTE_FREES);
		tail = DEACTIVATE_TO_TAIL;
	}

	/*
	 * Stage one: Free all available per cpu objects back
	 * to the page freelist while it is still frozen. Leave the
	 * last one.
	 *
	 * There is no need to take the list->lock because the page
	 * is still frozen.
	 */
	while (freelist && (nextfree = get_freepointer(s, freelist))) {
		void *prior;
		unsigned long counters;

		do {
			prior = page->freelist;
			counters = page->counters;
			set_freepointer(s, freelist, prior);
			new.counters = counters;
			new.inuse--;
			VM_BUG_ON(!new.frozen);

		} while (!__cmpxchg_double_slab(s, page,
			prior, counters,
			freelist, new.counters,
			"drain percpu freelist"));

		freelist = nextfree;
	}

	/*
	 * Stage two: Ensure that the page is unfrozen while the
	 * list presence reflects the actual number of objects
	 * during unfreeze.
	 *
	 * We setup the list membership and then perform a cmpxchg
	 * with the count. If there is a mismatch then the page
	 * is not unfrozen but the page is on the wrong list.
	 *
	 * Then we restart the process which may have to remove
	 * the page from the list that we just put it on again
	 * because the number of objects in the slab may have
	 * changed.
	 */
redo:

	old.freelist = page->freelist;
	old.counters = page->counters;
	VM_BUG_ON(!old.frozen);

	/* Determine target state of the slab */
	new.counters = old.counters;
	if (freelist) {
		new.inuse--;
		set_freepointer(s, freelist, old.freelist);
		new.freelist = freelist;
	} else
		new.freelist = old.freelist;

	new.frozen = 0;

	if (!new.inuse && n->nr_partial >= s->min_partial)
		m = M_FREE;
	else if (new.freelist) {
		m = M_PARTIAL;
		if (!lock) {
			lock = 1;
			/*
			 * Taking the spinlock removes the possiblity
			 * that acquire_slab() will see a slab page that
			 * is frozen
			 */
			spin_lock(&n->list_lock);
		}
	} else {
		m = M_FULL;
		if (kmem_cache_debug(s) && !lock) {
			lock = 1;
			/*
			 * This also ensures that the scanning of full
			 * slabs from diagnostic functions will not see
			 * any frozen slabs.
			 */
			spin_lock(&n->list_lock);
		}
	}

	if (l != m) {

		if (l == M_PARTIAL)

			remove_partial(n, page);

		else if (l == M_FULL)

			remove_full(s, n, page);

		if (m == M_PARTIAL) {

			add_partial(n, page, tail);
			stat(s, tail);

		} else if (m == M_FULL) {

			stat(s, DEACTIVATE_FULL);
			add_full(s, n, page);

		}
	}

	l = m;
	if (!__cmpxchg_double_slab(s, page,
				old.freelist, old.counters,
				new.freelist, new.counters,
				"unfreezing slab"))
		goto redo;

	if (lock)
		spin_unlock(&n->list_lock);

	if (m == M_FREE) {
		stat(s, DEACTIVATE_EMPTY);
		discard_slab(s, page);
		stat(s, FREE_SLAB);
	}
}

/*
 * Unfreeze all the cpu partial slabs.
 *
 * This function must be called with interrupts disabled
 * for the cpu using c (or some other guarantee must be there
 * to guarantee no concurrent accesses).
 */
static void unfreeze_partials(struct kmem_cache *s,
		struct kmem_cache_cpu *c)
{
#ifdef CONFIG_SLUB_CPU_PARTIAL
	struct kmem_cache_node *n = NULL, *n2 = NULL;
	struct page *page, *discard_page = NULL;

	while ((page = c->partial)) {
		struct page new;
		struct page old;

		c->partial = page->next;

		n2 = get_node(s, page_to_nid(page));
		if (n != n2) {
			if (n)
				spin_unlock(&n->list_lock);

			n = n2;
			spin_lock(&n->list_lock);
		}

		do {

			old.freelist = page->freelist;
			old.counters = page->counters;
			VM_BUG_ON(!old.frozen);

			new.counters = old.counters;
			new.freelist = old.freelist;

			new.frozen = 0;

		} while (!__cmpxchg_double_slab(s, page,
				old.freelist, old.counters,
				new.freelist, new.counters,
				"unfreezing slab"));

		if (unlikely(!new.inuse && n->nr_partial >= s->min_partial)) {
			page->next = discard_page;
			discard_page = page;
		} else {
			add_partial(n, page, DEACTIVATE_TO_TAIL);
			stat(s, FREE_ADD_PARTIAL);
		}
	}

	if (n)
		spin_unlock(&n->list_lock);

	while (discard_page) {
		page = discard_page;
		discard_page = discard_page->next;

		stat(s, DEACTIVATE_EMPTY);
		discard_slab(s, page);
		stat(s, FREE_SLAB);
	}
#endif
}

/*
 * Put a page that was just frozen (in __slab_free) into a partial page
 * slot if available. This is done without interrupts disabled and without
 * preemption disabled. The cmpxchg is racy and may put the partial page
 * onto a random cpus partial slot.
 *
 * If we did not find a slot then simply move all the partials to the
 * per node partial list.
 */
static void put_cpu_partial(struct kmem_cache *s, struct page *page, int drain)
{
#ifdef CONFIG_SLUB_CPU_PARTIAL
	struct page *oldpage;
	int pages;
	int pobjects;

	preempt_disable();
	do {
		pages = 0;
		pobjects = 0;
		oldpage = this_cpu_read(s->cpu_slab->partial);

		if (oldpage) {
			pobjects = oldpage->pobjects;
			pages = oldpage->pages;
			if (drain && pobjects > s->cpu_partial) {
				unsigned long flags;
				/*
				 * partial array is full. Move the existing
				 * set to the per node partial list.
				 */
				local_irq_save(flags);
				unfreeze_partials(s, this_cpu_ptr(s->cpu_slab));
				local_irq_restore(flags);
				oldpage = NULL;
				pobjects = 0;
				pages = 0;
				stat(s, CPU_PARTIAL_DRAIN);
			}
		}

		pages++;
		pobjects += page->objects - page->inuse;

		page->pages = pages;
		page->pobjects = pobjects;
		page->next = oldpage;

	} while (this_cpu_cmpxchg(s->cpu_slab->partial, oldpage, page)
								!= oldpage);
	if (unlikely(!s->cpu_partial)) {
		unsigned long flags;

		local_irq_save(flags);
		unfreeze_partials(s, this_cpu_ptr(s->cpu_slab));
		local_irq_restore(flags);
	}
	preempt_enable();
#endif
}

static inline void flush_slab(struct kmem_cache *s, struct kmem_cache_cpu *c)
{
	stat(s, CPUSLAB_FLUSH);
	deactivate_slab(s, c->page, c->freelist);

	c->tid = next_tid(c->tid);
	c->page = NULL;
	c->freelist = NULL;
}

/*
 * Flush cpu slab.
 *
 * Called from IPI handler with interrupts disabled.
 */
static inline void __flush_cpu_slab(struct kmem_cache *s, int cpu)
{
	struct kmem_cache_cpu *c = per_cpu_ptr(s->cpu_slab, cpu);

	if (likely(c)) {
		if (c->page)
			flush_slab(s, c);

		unfreeze_partials(s, c);
	}
}

static void flush_cpu_slab(void *d)
{
	struct kmem_cache *s = d;

	__flush_cpu_slab(s, smp_processor_id());
}

static bool has_cpu_slab(int cpu, void *info)
{
	struct kmem_cache *s = info;
	struct kmem_cache_cpu *c = per_cpu_ptr(s->cpu_slab, cpu);

	return c->page || c->partial;
}

static void flush_all(struct kmem_cache *s)
{
	on_each_cpu_cond(has_cpu_slab, flush_cpu_slab, s, 1, GFP_ATOMIC);
}

/*
 * Check if the objects in a per cpu structure fit numa
 * locality expectations.
 */
static inline int node_match(struct page *page, int node)
{
#ifdef CONFIG_NUMA
	if (!page || (node != NUMA_NO_NODE && page_to_nid(page) != node))
		return 0;
#endif
	return 1;
}

#ifdef CONFIG_SLUB_DEBUG
static int count_free(struct page *page)
{
	return page->objects - page->inuse;
}

static inline unsigned long node_nr_objs(struct kmem_cache_node *n)
{
	return atomic_long_read(&n->total_objects);
}
#endif /* CONFIG_SLUB_DEBUG */

#if defined(CONFIG_SLUB_DEBUG) || defined(CONFIG_SYSFS)
static unsigned long count_partial(struct kmem_cache_node *n,
					int (*get_count)(struct page *))
{
	unsigned long flags;
	unsigned long x = 0;
	struct page *page;

	spin_lock_irqsave(&n->list_lock, flags);
	list_for_each_entry(page, &n->partial, lru)
		x += get_count(page);
	spin_unlock_irqrestore(&n->list_lock, flags);
	return x;
}
#endif /* CONFIG_SLUB_DEBUG || CONFIG_SYSFS */

static noinline void
slab_out_of_memory(struct kmem_cache *s, gfp_t gfpflags, int nid)
{
#ifdef CONFIG_SLUB_DEBUG
	static DEFINE_RATELIMIT_STATE(slub_oom_rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	int node;
	struct kmem_cache_node *n;

	if ((gfpflags & __GFP_NOWARN) || !__ratelimit(&slub_oom_rs))
		return;

	pr_warn("SLUB: Unable to allocate memory on node %d (gfp=0x%x)\n",
		nid, gfpflags);
	pr_warn("  cache: %s, object size: %d, buffer size: %d, default order: %d, min order: %d\n",
		s->name, s->object_size, s->size, oo_order(s->oo),
		oo_order(s->min));

	if (oo_order(s->min) > get_order(s->object_size))
		pr_warn("  %s debugging increased min order, use slub_debug=O to disable.\n",
			s->name);

	for_each_kmem_cache_node(s, node, n) {
		unsigned long nr_slabs;
		unsigned long nr_objs;
		unsigned long nr_free;

		nr_free  = count_partial(n, count_free);
		nr_slabs = node_nr_slabs(n);
		nr_objs  = node_nr_objs(n);

		pr_warn("  node %d: slabs: %ld, objs: %ld, free: %ld\n",
			node, nr_slabs, nr_objs, nr_free);
	}
#endif
}

static inline void *new_slab_objects(struct kmem_cache *s, gfp_t flags,
			int node, struct kmem_cache_cpu **pc)
{
	void *freelist;
	struct kmem_cache_cpu *c = *pc;
	struct page *page;

	freelist = get_partial(s, flags, node, c);

	if (freelist)
		return freelist;

	page = new_slab(s, flags, node);
	if (page) {
		c = raw_cpu_ptr(s->cpu_slab);
		if (c->page)
			flush_slab(s, c);

		/*
		 * No other reference to the page yet so we can
		 * muck around with it freely without cmpxchg
		 */
		freelist = page->freelist;
		page->freelist = NULL;

		stat(s, ALLOC_SLAB);
		c->page = page;
		*pc = c;
	} else
		freelist = NULL;

	return freelist;
}

static inline bool pfmemalloc_match(struct page *page, gfp_t gfpflags)
{
	if (unlikely(PageSlabPfmemalloc(page)))
		return gfp_pfmemalloc_allowed(gfpflags);

	return true;
}

/*
 * Check the page->freelist of a page and either transfer the freelist to the
 * per cpu freelist or deactivate the page.
 *
 * The page is still frozen if the return value is not NULL.
 *
 * If this function returns NULL then the page has been unfrozen.
 *
 * This function must be called with interrupt disabled.
 */
static inline void *get_freelist(struct kmem_cache *s, struct page *page)
{
	struct page new;
	unsigned long counters;
	void *freelist;

	do {
		freelist = page->freelist;
		counters = page->counters;

		new.counters = counters;
		VM_BUG_ON(!new.frozen);

		new.inuse = page->objects;
		new.frozen = freelist != NULL;

	} while (!__cmpxchg_double_slab(s, page,
		freelist, counters,
		NULL, new.counters,
		"get_freelist"));

	return freelist;
}

/*
 * Slow path. The lockless freelist is empty or we need to perform
 * debugging duties.
 *
 * Processing is still very fast if new objects have been freed to the
 * regular freelist. In that case we simply take over the regular freelist
 * as the lockless freelist and zap the regular freelist.
 *
 * If that is not working then we fall back to the partial lists. We take the
 * first element of the freelist as the object to allocate now and move the
 * rest of the freelist to the lockless freelist.
 *
 * And if we were unable to get a new slab from the partial slab lists then
 * we need to allocate a new slab. This is the slowest path since it involves
 * a call to the page allocator and the setup of a new slab.
 *
 * Version of __slab_alloc to use when we know that interrupts are
 * already disabled (which is the case for bulk allocation).
 */
static void *___slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node,
			  unsigned long addr, struct kmem_cache_cpu *c)
{
	void *freelist;
	struct page *page;

	page = c->page;
	if (!page)
		goto new_slab;
redo:

	if (unlikely(!node_match(page, node))) {
		int searchnode = node;

		if (node != NUMA_NO_NODE && !node_present_pages(node))
			searchnode = node_to_mem_node(node);

		if (unlikely(!node_match(page, searchnode))) {
			stat(s, ALLOC_NODE_MISMATCH);
			deactivate_slab(s, page, c->freelist);
			c->page = NULL;
			c->freelist = NULL;
			goto new_slab;
		}
	}

	/*
	 * By rights, we should be searching for a slab page that was
	 * PFMEMALLOC but right now, we are losing the pfmemalloc
	 * information when the page leaves the per-cpu allocator
	 */
	if (unlikely(!pfmemalloc_match(page, gfpflags))) {
		deactivate_slab(s, page, c->freelist);
		c->page = NULL;
		c->freelist = NULL;
		goto new_slab;
	}

	/* must check again c->freelist in case of cpu migration or IRQ */
	freelist = c->freelist;
	if (freelist)
		goto load_freelist;

	freelist = get_freelist(s, page);

	if (!freelist) {
		c->page = NULL;
		stat(s, DEACTIVATE_BYPASS);
		goto new_slab;
	}

	stat(s, ALLOC_REFILL);

load_freelist:
	/*
	 * freelist is pointing to the list of objects to be used.
	 * page is pointing to the page from which the objects are obtained.
	 * That page must be frozen for per cpu allocations to work.
	 */
	VM_BUG_ON(!c->page->frozen);
	c->freelist = get_freepointer(s, freelist);
	c->tid = next_tid(c->tid);
	return freelist;

new_slab:

	if (c->partial) {
		page = c->page = c->partial;
		c->partial = page->next;
		stat(s, CPU_PARTIAL_ALLOC);
		c->freelist = NULL;
		goto redo;
	}

	freelist = new_slab_objects(s, gfpflags, node, &c);

	if (unlikely(!freelist)) {
		slab_out_of_memory(s, gfpflags, node);
		return NULL;
	}

	page = c->page;
	if (likely(!kmem_cache_debug(s) && pfmemalloc_match(page, gfpflags)))
		goto load_freelist;

	/* Only entered in the debug case */
	if (kmem_cache_debug(s) &&
			!alloc_debug_processing(s, page, freelist, addr))
		goto new_slab;	/* Slab failed checks. Next slab needed */

	deactivate_slab(s, page, get_freepointer(s, freelist));
	c->page = NULL;
	c->freelist = NULL;
	return freelist;
}

/*
 * Another one that disabled interrupt and compensates for possible
 * cpu changes by refetching the per cpu area pointer.
 */
static void *__slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node,
			  unsigned long addr, struct kmem_cache_cpu *c)
{
	void *p;
	unsigned long flags;

	local_irq_save(flags);
#ifdef CONFIG_PREEMPT
	/*
	 * We may have been preempted and rescheduled on a different
	 * cpu before disabling interrupts. Need to reload cpu area
	 * pointer.
	 */
	c = this_cpu_ptr(s->cpu_slab);
#endif

	p = ___slab_alloc(s, gfpflags, node, addr, c);
	local_irq_restore(flags);
	return p;
}

/*
 * Inlined fastpath so that allocation functions (kmalloc, kmem_cache_alloc)
 * have the fastpath folded into their functions. So no function call
 * overhead for requests that can be satisfied on the fastpath.
 *
 * The fastpath works by first checking if the lockless freelist can be used.
 * If not then __slab_alloc is called for slow processing.
 *
 * Otherwise we can simply pick the next object from the lockless free list.
 */
static __always_inline void *slab_alloc_node(struct kmem_cache *s,
		gfp_t gfpflags, int node, unsigned long addr)
{
	void *object;
	struct kmem_cache_cpu *c;
	struct page *page;
	unsigned long tid;

	s = slab_pre_alloc_hook(s, gfpflags);
	if (!s)
		return NULL;
redo:
	/*
	 * Must read kmem_cache cpu data via this cpu ptr. Preemption is
	 * enabled. We may switch back and forth between cpus while
	 * reading from one cpu area. That does not matter as long
	 * as we end up on the original cpu again when doing the cmpxchg.
	 *
	 * We should guarantee that tid and kmem_cache are retrieved on
	 * the same cpu. It could be different if CONFIG_PREEMPT so we need
	 * to check if it is matched or not.
	 */
	do {
		tid = this_cpu_read(s->cpu_slab->tid);
		c = raw_cpu_ptr(s->cpu_slab);
	} while (IS_ENABLED(CONFIG_PREEMPT) &&
		 unlikely(tid != READ_ONCE(c->tid)));

	/*
	 * Irqless object alloc/free algorithm used here depends on sequence
	 * of fetching cpu_slab's data. tid should be fetched before anything
	 * on c to guarantee that object and page associated with previous tid
	 * won't be used with current tid. If we fetch tid first, object and
	 * page could be one associated with next tid and our alloc/free
	 * request will be failed. In this case, we will retry. So, no problem.
	 */
	barrier();

	/*
	 * The transaction ids are globally unique per cpu and per operation on
	 * a per cpu queue. Thus they can be guarantee that the cmpxchg_double
	 * occurs on the right processor and that there was no operation on the
	 * linked list in between.
	 */

	object = c->freelist;
	page = c->page;
	if (unlikely(!object || !node_match(page, node))) {
		object = __slab_alloc(s, gfpflags, node, addr, c);
		stat(s, ALLOC_SLOWPATH);
	} else {
		void *next_object = get_freepointer_safe(s, object);

		/*
		 * The cmpxchg will only match if there was no additional
		 * operation and if we are on the right processor.
		 *
		 * The cmpxchg does the following atomically (without lock
		 * semantics!)
		 * 1. Relocate first pointer to the current per cpu area.
		 * 2. Verify that tid and freelist have not been changed
		 * 3. If they were not changed replace tid and freelist
		 *
		 * Since this is without lock semantics the protection is only
		 * against code executing on this cpu *not* from access by
		 * other cpus.
		 */
		if (unlikely(!this_cpu_cmpxchg_double(
				s->cpu_slab->freelist, s->cpu_slab->tid,
				object, tid,
				next_object, next_tid(tid)))) {

			note_cmpxchg_failure("slab_alloc", s, tid);
			goto redo;
		}
		prefetch_freepointer(s, next_object);
		stat(s, ALLOC_FASTPATH);
	}

	if (unlikely(gfpflags & __GFP_ZERO) && object)
		memset(object, 0, s->object_size);

	slab_post_alloc_hook(s, gfpflags, 1, &object);

	return object;
}

static __always_inline void *slab_alloc(struct kmem_cache *s,
		gfp_t gfpflags, unsigned long addr)
{
	return slab_alloc_node(s, gfpflags, NUMA_NO_NODE, addr);
}

void *kmem_cache_alloc(struct kmem_cache *s, gfp_t gfpflags)
{
	void *ret = slab_alloc(s, gfpflags, _RET_IP_);

	trace_kmem_cache_alloc(_RET_IP_, ret, s->object_size,
				s->size, gfpflags);

	return ret;
}
EXPORT_SYMBOL(kmem_cache_alloc);

#ifdef CONFIG_TRACING
void *kmem_cache_alloc_trace(struct kmem_cache *s, gfp_t gfpflags, size_t size)
{
	void *ret = slab_alloc(s, gfpflags, _RET_IP_);
	trace_kmalloc(_RET_IP_, ret, size, s->size, gfpflags);
	kasan_kmalloc(s, ret, size);
	return ret;
}
EXPORT_SYMBOL(kmem_cache_alloc_trace);
#endif

#ifdef CONFIG_NUMA
void *kmem_cache_alloc_node(struct kmem_cache *s, gfp_t gfpflags, int node)
{
	void *ret = slab_alloc_node(s, gfpflags, node, _RET_IP_);

	trace_kmem_cache_alloc_node(_RET_IP_, ret,
				    s->object_size, s->size, gfpflags, node);

	return ret;
}
EXPORT_SYMBOL(kmem_cache_alloc_node);

#ifdef CONFIG_TRACING
void *kmem_cache_alloc_node_trace(struct kmem_cache *s,
				    gfp_t gfpflags,
				    int node, size_t size)
{
	void *ret = slab_alloc_node(s, gfpflags, node, _RET_IP_);

	trace_kmalloc_node(_RET_IP_, ret,
			   size, s->size, gfpflags, node);

	kasan_kmalloc(s, ret, size);
	return ret;
}
EXPORT_SYMBOL(kmem_cache_alloc_node_trace);
#endif
#endif

/*
 * Slow path handling. This may still be called frequently since objects
 * have a longer lifetime than the cpu slabs in most processing loads.
 *
 * So we still attempt to reduce cache line usage. Just take the slab
 * lock and free the item. If there is no additional partial page
 * handling required then we can return immediately.
 */
static void __slab_free(struct kmem_cache *s, struct page *page,
			void *head, void *tail, int cnt,
			unsigned long addr)

{
	void *prior;
	int was_frozen;
	struct page new;
	unsigned long counters;
	struct kmem_cache_node *n = NULL;
	unsigned long uninitialized_var(flags);

	stat(s, FREE_SLOWPATH);

	if (kmem_cache_debug(s) &&
	    !(n = free_debug_processing(s, page, head, tail, cnt,
					addr, &flags)))
		return;

	do {
		if (unlikely(n)) {
			spin_unlock_irqrestore(&n->list_lock, flags);
			n = NULL;
		}
		prior = page->freelist;
		counters = page->counters;
		set_freepointer(s, tail, prior);
		new.counters = counters;
		was_frozen = new.frozen;
		new.inuse -= cnt;
		if ((!new.inuse || !prior) && !was_frozen) {

			if (kmem_cache_has_cpu_partial(s) && !prior) {

				/*
				 * Slab was on no list before and will be
				 * partially empty
				 * We can defer the list move and instead
				 * freeze it.
				 */
				new.frozen = 1;

			} else { /* Needs to be taken off a list */

				n = get_node(s, page_to_nid(page));
				/*
				 * Speculatively acquire the list_lock.
				 * If the cmpxchg does not succeed then we may
				 * drop the list_lock without any processing.
				 *
				 * Otherwise the list_lock will synchronize with
				 * other processors updating the list of slabs.
				 */
				spin_lock_irqsave(&n->list_lock, flags);

			}
		}

	} while (!cmpxchg_double_slab(s, page,
		prior, counters,
		head, new.counters,
		"__slab_free"));

	if (likely(!n)) {

		/*
		 * If we just froze the page then put it onto the
		 * per cpu partial list.
		 */
		if (new.frozen && !was_frozen) {
			put_cpu_partial(s, page, 1);
			stat(s, CPU_PARTIAL_FREE);
		}
		/*
		 * The list lock was not taken therefore no list
		 * activity can be necessary.
		 */
		if (was_frozen)
			stat(s, FREE_FROZEN);
		return;
	}

	if (unlikely(!new.inuse && n->nr_partial >= s->min_partial))
		goto slab_empty;

	/*
	 * Objects left in the slab. If it was not on the partial list before
	 * then add it.
	 */
	if (!kmem_cache_has_cpu_partial(s) && unlikely(!prior)) {
		if (kmem_cache_debug(s))
			remove_full(s, n, page);
		add_partial(n, page, DEACTIVATE_TO_TAIL);
		stat(s, FREE_ADD_PARTIAL);
	}
	spin_unlock_irqrestore(&n->list_lock, flags);
	return;

slab_empty:
	if (prior) {
		/*
		 * Slab on the partial list.
		 */
		remove_partial(n, page);
		stat(s, FREE_REMOVE_PARTIAL);
	} else {
		/* Slab must be on the full list */
		remove_full(s, n, page);
	}

	spin_unlock_irqrestore(&n->list_lock, flags);
	stat(s, FREE_SLAB);
	discard_slab(s, page);
}

/*
 * Fastpath with forced inlining to produce a kfree and kmem_cache_free that
 * can perform fastpath freeing without additional function calls.
 *
 * The fastpath is only possible if we are freeing to the current cpu slab
 * of this processor. This typically the case if we have just allocated
 * the item before.
 *
 * If fastpath is not possible then fall back to __slab_free where we deal
 * with all sorts of special processing.
 *
 * Bulk free of a freelist with several objects (all pointing to the
 * same page) possible by specifying head and tail ptr, plus objects
 * count (cnt). Bulk free indicated by tail pointer being set.
 */
static __always_inline void slab_free(struct kmem_cache *s, struct page *page,
				      void *head, void *tail, int cnt,
				      unsigned long addr)
{
	void *tail_obj = tail ? : head;
	struct kmem_cache_cpu *c;
	unsigned long tid;

	slab_free_freelist_hook(s, head, tail);

redo:
	/*
	 * Determine the currently cpus per cpu slab.
	 * The cpu may change afterward. However that does not matter since
	 * data is retrieved via this pointer. If we are on the same cpu
	 * during the cmpxchg then the free will succeed.
	 */
	do {
		tid = this_cpu_read(s->cpu_slab->tid);
		c = raw_cpu_ptr(s->cpu_slab);
	} while (IS_ENABLED(CONFIG_PREEMPT) &&
		 unlikely(tid != READ_ONCE(c->tid)));

	/* Same with comment on barrier() in slab_alloc_node() */
	barrier();

	if (likely(page == c->page)) {
		set_freepointer(s, tail_obj, c->freelist);

		if (unlikely(!this_cpu_cmpxchg_double(
				s->cpu_slab->freelist, s->cpu_slab->tid,
				c->freelist, tid,
				head, next_tid(tid)))) {

			note_cmpxchg_failure("slab_free", s, tid);
			goto redo;
		}
		stat(s, FREE_FASTPATH);
	} else
		__slab_free(s, page, head, tail_obj, cnt, addr);

}

void kmem_cache_free(struct kmem_cache *s, void *x)
{
	s = cache_from_obj(s, x);
	if (!s)
		return;
	slab_free(s, virt_to_head_page(x), x, NULL, 1, _RET_IP_);
	trace_kmem_cache_free(_RET_IP_, x);
}
EXPORT_SYMBOL(kmem_cache_free);

struct detached_freelist {
	struct page *page;
	void *tail;
	void *freelist;
	int cnt;
	struct kmem_cache *s;
};

/*
 * This function progressively scans the array with free objects (with
 * a limited look ahead) and extract objects belonging to the same
 * page.  It builds a detached freelist directly within the given
 * page/objects.  This can happen without any need for
 * synchronization, because the objects are owned by running process.
 * The freelist is build up as a single linked list in the objects.
 * The idea is, that this detached freelist can then be bulk
 * transferred to the real freelist(s), but only requiring a single
 * synchronization primitive.  Look ahead in the array is limited due
 * to performance reasons.
 */
static inline
int build_detached_freelist(struct kmem_cache *s, size_t size,
			    void **p, struct detached_freelist *df)
{
	size_t first_skipped_index = 0;
	int lookahead = 3;
	void *object;

	/* Always re-init detached_freelist */
	df->page = NULL;

	do {
		object = p[--size];
	} while (!object && size);

	if (!object)
		return 0;

	/* Support for memcg, compiler can optimize this out */
	df->s = cache_from_obj(s, object);

	/* Start new detached freelist */
	set_freepointer(df->s, object, NULL);
	df->page = virt_to_head_page(object);
	df->tail = object;
	df->freelist = object;
	p[size] = NULL; /* mark object processed */
	df->cnt = 1;

	while (size) {
		object = p[--size];
		if (!object)
			continue; /* Skip processed objects */

		/* df->page is always set at this point */
		if (df->page == virt_to_head_page(object)) {
			/* Opportunity build freelist */
			set_freepointer(df->s, object, df->freelist);
			df->freelist = object;
			df->cnt++;
			p[size] = NULL; /* mark object processed */

			continue;
		}

		/* Limit look ahead search */
		if (!--lookahead)
			break;

		if (!first_skipped_index)
			first_skipped_index = size + 1;
	}

	return first_skipped_index;
}

/* Note that interrupts must be enabled when calling this function. */
void kmem_cache_free_bulk(struct kmem_cache *s, size_t size, void **p)
{
	if (WARN_ON(!size))
		return;

	do {
		struct detached_freelist df;

		size = build_detached_freelist(s, size, p, &df);
		if (unlikely(!df.page))
			continue;

		slab_free(df.s, df.page, df.freelist, df.tail, df.cnt,_RET_IP_);
	} while (likely(size));
}
EXPORT_SYMBOL(kmem_cache_free_bulk);

/* Note that interrupts must be enabled when calling this function. */
int kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t size,
			  void **p)
{
	struct kmem_cache_cpu *c;
	int i;

	/* memcg and kmem_cache debug support */
	s = slab_pre_alloc_hook(s, flags);
	if (unlikely(!s))
		return false;
	/*
	 * Drain objects in the per cpu slab, while disabling local
	 * IRQs, which protects against PREEMPT and interrupts
	 * handlers invoking normal fastpath.
	 */
	local_irq_disable();
	c = this_cpu_ptr(s->cpu_slab);

	for (i = 0; i < size; i++) {
		void *object = c->freelist;

		if (unlikely(!object)) {
			/*
			 * Invoking slow path likely have side-effect
			 * of re-populating per CPU c->freelist
			 */
			p[i] = ___slab_alloc(s, flags, NUMA_NO_NODE,
					    _RET_IP_, c);
			if (unlikely(!p[i]))
				goto error;

			c = this_cpu_ptr(s->cpu_slab);
			continue; /* goto for-loop */
		}
		c->freelist = get_freepointer(s, object);
		p[i] = object;
	}
	c->tid = next_tid(c->tid);
	local_irq_enable();

	/* Clear memory outside IRQ disabled fastpath loop */
	if (unlikely(flags & __GFP_ZERO)) {
		int j;

		for (j = 0; j < i; j++)
			memset(p[j], 0, s->object_size);
	}

	/* memcg and kmem_cache debug support */
	slab_post_alloc_hook(s, flags, size, p);
	return i;
error:
	local_irq_enable();
	slab_post_alloc_hook(s, flags, i, p);
	__kmem_cache_free_bulk(s, i, p);
	return 0;
}
EXPORT_SYMBOL(kmem_cache_alloc_bulk);


/*
 * Object placement in a slab is made very easy because we always start at
 * offset 0. If we tune the size of the object to the alignment then we can
 * get the required alignment by putting one properly sized object after
 * another.
 *
 * Notice that the allocation order determines the sizes of the per cpu
 * caches. Each processor has always one slab available for allocations.
 * Increasing the allocation order reduces the number of times that slabs
 * must be moved on and off the partial lists and is therefore a factor in
 * locking overhead.
 */

/*
 * Mininum / Maximum order of slab pages. This influences locking overhead
 * and slab fragmentation. A higher order reduces the number of partial slabs
 * and increases the number of allocations possible without having to
 * take the list_lock.
 */
static int slub_min_order;
static int slub_max_order = PAGE_ALLOC_COSTLY_ORDER;
static int slub_min_objects;

/*
 * Calculate the order of allocation given an slab object size.
 *
 * The order of allocation has significant impact on performance and other
 * system components. Generally order 0 allocations should be preferred since
 * order 0 does not cause fragmentation in the page allocator. Larger objects
 * be problematic to put into order 0 slabs because there may be too much
 * unused space left. We go to a higher order if more than 1/16th of the slab
 * would be wasted.
 *
 * In order to reach satisfactory performance we must ensure that a minimum
 * number of objects is in one slab. Otherwise we may generate too much
 * activity on the partial lists which requires taking the list_lock. This is
 * less a concern for large slabs though which are rarely used.
 *
 * slub_max_order specifies the order where we begin to stop considering the
 * number of objects in a slab as critical. If we reach slub_max_order then
 * we try to keep the page order as low as possible. So we accept more waste
 * of space in favor of a small page order.
 *
 * Higher order allocations also allow the placement of more objects in a
 * slab and thereby reduce object handling overhead. If the user has
 * requested a higher mininum order then we start with that one instead of
 * the smallest order which will fit the object.
 */
static inline int slab_order(int size, int min_objects,
				int max_order, int fract_leftover, int reserved)
{
	int order;
	int rem;
	int min_order = slub_min_order;

	if (order_objects(min_order, size, reserved) > MAX_OBJS_PER_PAGE)
		return get_order(size * MAX_OBJS_PER_PAGE) - 1;

	for (order = max(min_order, get_order(min_objects * size + reserved));
			order <= max_order; order++) {

		unsigned long slab_size = PAGE_SIZE << order;

		rem = (slab_size - reserved) % size;

		if (rem <= slab_size / fract_leftover)
			break;
	}

	return order;
}

static inline int calculate_order(int size, int reserved)
{
	int order;
	int min_objects;
	int fraction;
	int max_objects;

	/*
	 * Attempt to find best configuration for a slab. This
	 * works by first attempting to generate a layout with
	 * the best configuration and backing off gradually.
	 *
	 * First we increase the acceptable waste in a slab. Then
	 * we reduce the minimum objects required in a slab.
	 */
	min_objects = slub_min_objects;
	if (!min_objects)
		min_objects = 4 * (fls(nr_cpu_ids) + 1);
	max_objects = order_objects(slub_max_order, size, reserved);
	min_objects = min(min_objects, max_objects);

	while (min_objects > 1) {
		fraction = 16;
		while (fraction >= 4) {
			order = slab_order(size, min_objects,
					slub_max_order, fraction, reserved);
			if (order <= slub_max_order)
				return order;
			fraction /= 2;
		}
		min_objects--;
	}

	/*
	 * We were unable to place multiple objects in a slab. Now
	 * lets see if we can place a single object there.
	 */
	order = slab_order(size, 1, slub_max_order, 1, reserved);
	if (order <= slub_max_order)
		return order;

	/*
	 * Doh this slab cannot be placed using slub_max_order.
	 */
	order = slab_order(size, 1, MAX_ORDER, 1, reserved);
	if (order < MAX_ORDER)
		return order;
	return -ENOSYS;
}

static void
init_kmem_cache_node(struct kmem_cache_node *n)
{
	n->nr_partial = 0;
	spin_lock_init(&n->list_lock);
	INIT_LIST_HEAD(&n->partial);
#ifdef CONFIG_SLUB_DEBUG
	atomic_long_set(&n->nr_slabs, 0);
	atomic_long_set(&n->total_objects, 0);
	INIT_LIST_HEAD(&n->full);
#endif
}

static inline int alloc_kmem_cache_cpus(struct kmem_cache *s)
{
	BUILD_BUG_ON(PERCPU_DYNAMIC_EARLY_SIZE <
			KMALLOC_SHIFT_HIGH * sizeof(struct kmem_cache_cpu));

	/*
	 * Must align to double word boundary for the double cmpxchg
	 * instructions to work; see __pcpu_double_call_return_bool().
	 */
	s->cpu_slab = __alloc_percpu(sizeof(struct kmem_cache_cpu),
				     2 * sizeof(void *));

	if (!s->cpu_slab)
		return 0;

	init_kmem_cache_cpus(s);

	return 1;
}

static struct kmem_cache *kmem_cache_node;

/*
 * No kmalloc_node yet so do it by hand. We know that this is the first
 * slab on the node for this slabcache. There are no concurrent accesses
 * possible.
 *
 * Note that this function only works on the kmem_cache_node
 * when allocating for the kmem_cache_node. This is used for bootstrapping
 * memory on a fresh node that has no slab structures yet.
 */
static void early_kmem_cache_node_alloc(int node)
{
	struct page *page;
	struct kmem_cache_node *n;

	BUG_ON(kmem_cache_node->size < sizeof(struct kmem_cache_node));

	page = new_slab(kmem_cache_node, GFP_NOWAIT, node);

	BUG_ON(!page);
	if (page_to_nid(page) != node) {
		pr_err("SLUB: Unable to allocate memory from node %d\n", node);
		pr_err("SLUB: Allocating a useless per node structure in order to be able to continue\n");
	}

	n = page->freelist;
	BUG_ON(!n);
	page->freelist = get_freepointer(kmem_cache_node, n);
	page->inuse = 1;
	page->frozen = 0;
	kmem_cache_node->node[node] = n;
#ifdef CONFIG_SLUB_DEBUG
	init_object(kmem_cache_node, n, SLUB_RED_ACTIVE);
	init_tracking(kmem_cache_node, n);
#endif
	kasan_kmalloc(kmem_cache_node, n, sizeof(struct kmem_cache_node));
	init_kmem_cache_node(n);
	inc_slabs_node(kmem_cache_node, node, page->objects);

	/*
	 * No locks need to be taken here as it has just been
	 * initialized and there is no concurrent access.
	 */
	__add_partial(n, page, DEACTIVATE_TO_HEAD);
}

static void free_kmem_cache_nodes(struct kmem_cache *s)
{
	int node;
	struct kmem_cache_node *n;

	for_each_kmem_cache_node(s, node, n) {
		kmem_cache_free(kmem_cache_node, n);
		s->node[node] = NULL;
	}
}

static int init_kmem_cache_nodes(struct kmem_cache *s)
{
	int node;

	for_each_node_state(node, N_NORMAL_MEMORY) {
		struct kmem_cache_node *n;

		if (slab_state == DOWN) {
			early_kmem_cache_node_alloc(node);
			continue;
		}
		n = kmem_cache_alloc_node(kmem_cache_node,
						GFP_KERNEL, node);

		if (!n) {
			free_kmem_cache_nodes(s);
			return 0;
		}

		s->node[node] = n;
		init_kmem_cache_node(n);
	}
	return 1;
}

static void set_min_partial(struct kmem_cache *s, unsigned long min)
{
	if (min < MIN_PARTIAL)
		min = MIN_PARTIAL;
	else if (min > MAX_PARTIAL)
		min = MAX_PARTIAL;
	s->min_partial = min;
}

/*
 * calculate_sizes() determines the order and the distribution of data within
 * a slab object.
 */
static int calculate_sizes(struct kmem_cache *s, int forced_order)
{
	unsigned long flags = s->flags;
	unsigned long size = s->object_size;
	int order;

	/*
	 * Round up object size to the next word boundary. We can only
	 * place the free pointer at word boundaries and this determines
	 * the possible location of the free pointer.
	 */
	size = ALIGN(size, sizeof(void *));

#ifdef CONFIG_SLUB_DEBUG
	/*
	 * Determine if we can poison the object itself. If the user of
	 * the slab may touch the object after free or before allocation
	 * then we should never poison the object itself.
	 */
	if ((flags & SLAB_POISON) && !(flags & SLAB_DESTROY_BY_RCU) &&
			!s->ctor)
		s->flags |= __OBJECT_POISON;
	else
		s->flags &= ~__OBJECT_POISON;


	/*
	 * If we are Redzoning then check if there is some space between the
	 * end of the object and the free pointer. If not then add an
	 * additional word to have some bytes to store Redzone information.
	 */
	if ((flags & SLAB_RED_ZONE) && size == s->object_size)
		size += sizeof(void *);
#endif

	/*
	 * With that we have determined the number of bytes in actual use
	 * by the object. This is the potential offset to the free pointer.
	 */
	s->inuse = size;

	if (((flags & (SLAB_DESTROY_BY_RCU | SLAB_POISON)) ||
		s->ctor)) {
		/*
		 * Relocate free pointer after the object if it is not
		 * permitted to overwrite the first word of the object on
		 * kmem_cache_free.
		 *
		 * This is the case if we do RCU, have a constructor or
		 * destructor or are poisoning the objects.
		 */
		s->offset = size;
		size += sizeof(void *);
	}

#ifdef CONFIG_SLUB_DEBUG
	if (flags & SLAB_STORE_USER)
		/*
		 * Need to store information about allocs and frees after
		 * the object.
		 */
		size += 2 * sizeof(struct track);

	if (flags & SLAB_RED_ZONE)
		/*
		 * Add some empty padding so that we can catch
		 * overwrites from earlier objects rather than let
		 * tracking information or the free pointer be
		 * corrupted if a user writes before the start
		 * of the object.
		 */
		size += sizeof(void *);
#endif

	/*
	 * SLUB stores one object immediately after another beginning from
	 * offset 0. In order to align the objects we have to simply size
	 * each object to conform to the alignment.
	 */
	size = ALIGN(size, s->align);
	s->size = size;
	if (forced_order >= 0)
		order = forced_order;
	else
		order = calculate_order(size, s->reserved);

	if (order < 0)
		return 0;

	s->allocflags = 0;
	if (order)
		s->allocflags |= __GFP_COMP;

	if (s->flags & SLAB_CACHE_DMA)
		s->allocflags |= GFP_DMA;

	if (s->flags & SLAB_RECLAIM_ACCOUNT)
		s->allocflags |= __GFP_RECLAIMABLE;

	/*
	 * Determine the number of objects per slab
	 */
	s->oo = oo_make(order, size, s->reserved);
	s->min = oo_make(get_order(size), size, s->reserved);
	if (oo_objects(s->oo) > oo_objects(s->max))
		s->max = s->oo;

	return !!oo_objects(s->oo);
}

static int kmem_cache_open(struct kmem_cache *s, unsigned long flags)
{
	s->flags = kmem_cache_flags(s->size, flags, s->name, s->ctor);
	s->reserved = 0;

	if (need_reserve_slab_rcu && (s->flags & SLAB_DESTROY_BY_RCU))
		s->reserved = sizeof(struct rcu_head);

	if (!calculate_sizes(s, -1))
		goto error;
	if (disable_higher_order_debug) {
		/*
		 * Disable debugging flags that store metadata if the min slab
		 * order increased.
		 */
		if (get_order(s->size) > get_order(s->object_size)) {
			s->flags &= ~DEBUG_METADATA_FLAGS;
			s->offset = 0;
			if (!calculate_sizes(s, -1))
				goto error;
		}
	}

#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE) && \
    defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
	if (system_has_cmpxchg_double() && (s->flags & SLAB_DEBUG_FLAGS) == 0)
		/* Enable fast mode */
		s->flags |= __CMPXCHG_DOUBLE;
#endif

	/*
	 * The larger the object size is, the more pages we want on the partial
	 * list to avoid pounding the page allocator excessively.
	 */
	set_min_partial(s, ilog2(s->size) / 2);

	/*
	 * cpu_partial determined the maximum number of objects kept in the
	 * per cpu partial lists of a processor.
	 *
	 * Per cpu partial lists mainly contain slabs that just have one
	 * object freed. If they are used for allocation then they can be
	 * filled up again with minimal effort. The slab will never hit the
	 * per node partial lists and therefore no locking will be required.
	 *
	 * This setting also determines
	 *
	 * A) The number of objects from per cpu partial slabs dumped to the
	 *    per node list when we reach the limit.
	 * B) The number of objects in cpu partial slabs to extract from the
	 *    per node list when we run out of per cpu objects. We only fetch
	 *    50% to keep some capacity around for frees.
	 */
	if (!kmem_cache_has_cpu_partial(s))
		s->cpu_partial = 0;
	else if (s->size >= PAGE_SIZE)
		s->cpu_partial = 2;
	else if (s->size >= 1024)
		s->cpu_partial = 6;
	else if (s->size >= 256)
		s->cpu_partial = 13;
	else
		s->cpu_partial = 30;

#ifdef CONFIG_NUMA
	s->remote_node_defrag_ratio = 1000;
#endif
	if (!init_kmem_cache_nodes(s))
		goto error;

	if (alloc_kmem_cache_cpus(s))
		return 0;

	free_kmem_cache_nodes(s);
error:
	if (flags & SLAB_PANIC)
		panic("Cannot create slab %s size=%lu realsize=%u "
			"order=%u offset=%u flags=%lx\n",
			s->name, (unsigned long)s->size, s->size,
			oo_order(s->oo), s->offset, flags);
	return -EINVAL;
}

static void list_slab_objects(struct kmem_cache *s, struct page *page,
							const char *text)
{
#ifdef CONFIG_SLUB_DEBUG
	void *addr = page_address(page);
	void *p;
	unsigned long *map = kzalloc(BITS_TO_LONGS(page->objects) *
				     sizeof(long), GFP_ATOMIC);
	if (!map)
		return;
	slab_err(s, page, text, s->name);
	slab_lock(page);

	get_map(s, page, map);
	for_each_object(p, s, addr, page->objects) {

		if (!test_bit(slab_index(p, s, addr), map)) {
			pr_err("INFO: Object 0x%p @offset=%tu\n", p, p - addr);
			print_tracking(s, p);
		}
	}
	slab_unlock(page);
	kfree(map);
#endif
}

/*
 * Attempt to free all partial slabs on a node.
 * This is called from kmem_cache_close(). We must be the last thread
 * using the cache and therefore we do not need to lock anymore.
 */
static void free_partial(struct kmem_cache *s, struct kmem_cache_node *n)
{
	struct page *page, *h;

	list_for_each_entry_safe(page, h, &n->partial, lru) {
		if (!page->inuse) {
			__remove_partial(n, page);
			discard_slab(s, page);
		} else {
			list_slab_objects(s, page,
			"Objects remaining in %s on kmem_cache_close()");
		}
	}
}

/*
 * Release all resources used by a slab cache.
 */
static inline int kmem_cache_close(struct kmem_cache *s)
{
	int node;
	struct kmem_cache_node *n;

	flush_all(s);
	/* Attempt to free all objects */
	for_each_kmem_cache_node(s, node, n) {
		free_partial(s, n);
		if (n->nr_partial || slabs_node(s, node))
			return 1;
	}
	free_percpu(s->cpu_slab);
	free_kmem_cache_nodes(s);
	return 0;
}

int __kmem_cache_shutdown(struct kmem_cache *s)
{
	return kmem_cache_close(s);
}

/********************************************************************
 *		Kmalloc subsystem
 *******************************************************************/

static int __init setup_slub_min_order(char *str)
{
	get_option(&str, &slub_min_order);

	return 1;
}

__setup("slub_min_order=", setup_slub_min_order);

static int __init setup_slub_max_order(char *str)
{
	get_option(&str, &slub_max_order);
	slub_max_order = min(slub_max_order, MAX_ORDER - 1);

	return 1;
}

__setup("slub_max_order=", setup_slub_max_order);

static int __init setup_slub_min_objects(char *str)
{
	get_option(&str, &slub_min_objects);

	return 1;
}

__setup("slub_min_objects=", setup_slub_min_objects);

void *__kmalloc(size_t size, gfp_t flags)
{
	struct kmem_cache *s;
	void *ret;

	if (unlikely(size > KMALLOC_MAX_CACHE_SIZE))
		return kmalloc_large(size, flags);

	s = kmalloc_slab(size, flags);

	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	ret = slab_alloc(s, flags, _RET_IP_);

	trace_kmalloc(_RET_IP_, ret, size, s->size, flags);

	kasan_kmalloc(s, ret, size);

	return ret;
}
EXPORT_SYMBOL(__kmalloc);

#ifdef CONFIG_NUMA
static void *kmalloc_large_node(size_t size, gfp_t flags, int node)
{
	struct page *page;
	void *ptr = NULL;

	flags |= __GFP_COMP | __GFP_NOTRACK;
	page = alloc_kmem_pages_node(node, flags, get_order(size));
	if (page)
		ptr = page_address(page);

	kmalloc_large_node_hook(ptr, size, flags);
	return ptr;
}

void *__kmalloc_node(size_t size, gfp_t flags, int node)
{
	struct kmem_cache *s;
	void *ret;

	if (unlikely(size > KMALLOC_MAX_CACHE_SIZE)) {
		ret = kmalloc_large_node(size, flags, node);

		trace_kmalloc_node(_RET_IP_, ret,
				   size, PAGE_SIZE << get_order(size),
				   flags, node);

		return ret;
	}

	s = kmalloc_slab(size, flags);

	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	ret = slab_alloc_node(s, flags, node, _RET_IP_);

	trace_kmalloc_node(_RET_IP_, ret, size, s->size, flags, node);

	kasan_kmalloc(s, ret, size);

	return ret;
}
EXPORT_SYMBOL(__kmalloc_node);
#endif

static size_t __ksize(const void *object)
{
	struct page *page;

	if (unlikely(object == ZERO_SIZE_PTR))
		return 0;

	page = virt_to_head_page(object);

	if (unlikely(!PageSlab(page))) {
		WARN_ON(!PageCompound(page));
		return PAGE_SIZE << compound_order(page);
	}

	return slab_ksize(page->slab_cache);
}

size_t ksize(const void *object)
{
	size_t size = __ksize(object);
	/* We assume that ksize callers could use whole allocated area,
	   so we need unpoison this area. */
	kasan_krealloc(object, size);
	return size;
}
EXPORT_SYMBOL(ksize);

void kfree(const void *x)
{
	struct page *page;
	void *object = (void *)x;

	trace_kfree(_RET_IP_, x);

	if (unlikely(ZERO_OR_NULL_PTR(x)))
		return;

	page = virt_to_head_page(x);
	if (unlikely(!PageSlab(page))) {
		BUG_ON(!PageCompound(page));
		kfree_hook(x);
		__free_kmem_pages(page, compound_order(page));
		return;
	}
	slab_free(page->slab_cache, page, object, NULL, 1, _RET_IP_);
}
EXPORT_SYMBOL(kfree);

#define SHRINK_PROMOTE_MAX 32

/*
 * kmem_cache_shrink discards empty slabs and promotes the slabs filled
 * up most to the head of the partial lists. New allocations will then
 * fill those up and thus they can be removed from the partial lists.
 *
 * The slabs with the least items are placed last. This results in them
 * being allocated from last increasing the chance that the last objects
 * are freed in them.
 */
int __kmem_cache_shrink(struct kmem_cache *s, bool deactivate)
{
	int node;
	int i;
	struct kmem_cache_node *n;
	struct page *page;
	struct page *t;
	struct list_head discard;
	struct list_head promote[SHRINK_PROMOTE_MAX];
	unsigned long flags;
	int ret = 0;

	if (deactivate) {
		/*
		 * Disable empty slabs caching. Used to avoid pinning offline
		 * memory cgroups by kmem pages that can be freed.
		 */
		s->cpu_partial = 0;
		s->min_partial = 0;

		/*
		 * s->cpu_partial is checked locklessly (see put_cpu_partial),
		 * so we have to make sure the change is visible.
		 */
		kick_all_cpus_sync();
	}

	flush_all(s);
	for_each_kmem_cache_node(s, node, n) {
		INIT_LIST_HEAD(&discard);
		for (i = 0; i < SHRINK_PROMOTE_MAX; i++)
			INIT_LIST_HEAD(promote + i);

		spin_lock_irqsave(&n->list_lock, flags);

		/*
		 * Build lists of slabs to discard or promote.
		 *
		 * Note that concurrent frees may occur while we hold the
		 * list_lock. page->inuse here is the upper limit.
		 */
		list_for_each_entry_safe(page, t, &n->partial, lru) {
			int free = page->objects - page->inuse;

			/* Do not reread page->inuse */
			barrier();

			/* We do not keep full slabs on the list */
			BUG_ON(free <= 0);

			if (free == page->objects) {
				list_move(&page->lru, &discard);
				n->nr_partial--;
			} else if (free <= SHRINK_PROMOTE_MAX)
				list_move(&page->lru, promote + free - 1);
		}

		/*
		 * Promote the slabs filled up most to the head of the
		 * partial list.
		 */
		for (i = SHRINK_PROMOTE_MAX - 1; i >= 0; i--)
			list_splice(promote + i, &n->partial);

		spin_unlock_irqrestore(&n->list_lock, flags);

		/* Release empty slabs */
		list_for_each_entry_safe(page, t, &discard, lru)
			discard_slab(s, page);

		if (slabs_node(s, node))
			ret = 1;
	}

	return ret;
}

static int slab_mem_going_offline_callback(void *arg)
{
	struct kmem_cache *s;

	mutex_lock(&slab_mutex);
	list_for_each_entry(s, &slab_caches, list)
		__kmem_cache_shrink(s, false);
	mutex_unlock(&slab_mutex);

	return 0;
}

static void slab_mem_offline_callback(void *arg)
{
	struct kmem_cache_node *n;
	struct kmem_cache *s;
	struct memory_notify *marg = arg;
	int offline_node;

	offline_node = marg->status_change_nid_normal;

	/*
	 * If the node still has available memory. we need kmem_cache_node
	 * for it yet.
	 */
	if (offline_node < 0)
		return;

	mutex_lock(&slab_mutex);
	list_for_each_entry(s, &slab_caches, list) {
		n = get_node(s, offline_node);
		if (n) {
			/*
			 * if n->nr_slabs > 0, slabs still exist on the node
			 * that is going down. We were unable to free them,
			 * and offline_pages() function shouldn't call this
			 * callback. So, we must fail.
			 */
			BUG_ON(slabs_node(s, offline_node));

			s->node[offline_node] = NULL;
			kmem_cache_free(kmem_cache_node, n);
		}
	}
	mutex_unlock(&slab_mutex);
}

static int slab_mem_going_online_callback(void *arg)
{
	struct kmem_cache_node *n;
	struct kmem_cache *s;
	struct memory_notify *marg = arg;
	int nid = marg->status_change_nid_normal;
	int ret = 0;

	/*
	 * If the node's memory is already available, then kmem_cache_node is
	 * already created. Nothing to do.
	 */
	if (nid < 0)
		return 0;

	/*
	 * We are bringing a node online. No memory is available yet. We must
	 * allocate a kmem_cache_node structure in order to bring the node
	 * online.
	 */
	mutex_lock(&slab_mutex);
	list_for_each_entry(s, &slab_caches, list) {
		/*
		 * XXX: kmem_cache_alloc_node will fallback to other nodes
		 *      since memory is not yet available from the node that
		 *      is brought up.
		 */
		n = kmem_cache_alloc(kmem_cache_node, GFP_KERNEL);
		if (!n) {
			ret = -ENOMEM;
			goto out;
		}
		init_kmem_cache_node(n);
		s->node[nid] = n;
	}
out:
	mutex_unlock(&slab_mutex);
	return ret;
}

static int slab_memory_callback(struct notifier_block *self,
				unsigned long action, void *arg)
{
	int ret = 0;

	switch (action) {
	case MEM_GOING_ONLINE:
		ret = slab_mem_going_online_callback(arg);
		break;
	case MEM_GOING_OFFLINE:
		ret = slab_mem_going_offline_callback(arg);
		break;
	case MEM_OFFLINE:
	case MEM_CANCEL_ONLINE:
		slab_mem_offline_callback(arg);
		break;
	case MEM_ONLINE:
	case MEM_CANCEL_OFFLINE:
		break;
	}
	if (ret)
		ret = notifier_from_errno(ret);
	else
		ret = NOTIFY_OK;
	return ret;
}

static struct notifier_block slab_memory_callback_nb = {
	.notifier_call = slab_memory_callback,
	.priority = SLAB_CALLBACK_PRI,
};

/********************************************************************
 *			Basic setup of slabs
 *******************************************************************/

/*
 * Used for early kmem_cache structures that were allocated using
 * the page allocator. Allocate them properly then fix up the pointers
 * that may be pointing to the wrong kmem_cache structure.
 */

static struct kmem_cache * __init bootstrap(struct kmem_cache *static_cache)
{
	int node;
	struct kmem_cache *s = kmem_cache_zalloc(kmem_cache, GFP_NOWAIT);
	struct kmem_cache_node *n;

	memcpy(s, static_cache, kmem_cache->object_size);

	/*
	 * This runs very early, and only the boot processor is supposed to be
	 * up.  Even if it weren't true, IRQs are not up so we couldn't fire
	 * IPIs around.
	 */
	__flush_cpu_slab(s, smp_processor_id());
	for_each_kmem_cache_node(s, node, n) {
		struct page *p;

		list_for_each_entry(p, &n->partial, lru)
			p->slab_cache = s;

#ifdef CONFIG_SLUB_DEBUG
		list_for_each_entry(p, &n->full, lru)
			p->slab_cache = s;
#endif
	}
	slab_init_memcg_params(s);
	list_add(&s->list, &slab_caches);
	return s;
}

void __init kmem_cache_init(void)
{
	static __initdata struct kmem_cache boot_kmem_cache,
		boot_kmem_cache_node;

	if (debug_guardpage_minorder())
		slub_max_order = 0;

	kmem_cache_node = &boot_kmem_cache_node;
	kmem_cache = &boot_kmem_cache;

	create_boot_cache(kmem_cache_node, "kmem_cache_node",
		sizeof(struct kmem_cache_node), SLAB_HWCACHE_ALIGN);

	register_hotmemory_notifier(&slab_memory_callback_nb);

	/* Able to allocate the per node structures */
	slab_state = PARTIAL;

	create_boot_cache(kmem_cache, "kmem_cache",
			offsetof(struct kmem_cache, node) +
				nr_node_ids * sizeof(struct kmem_cache_node *),
		       SLAB_HWCACHE_ALIGN);

	kmem_cache = bootstrap(&boot_kmem_cache);

	/*
	 * Allocate kmem_cache_node properly from the kmem_cache slab.
	 * kmem_cache_node is separately allocated so no need to
	 * update any list pointers.
	 */
	kmem_cache_node = bootstrap(&boot_kmem_cache_node);

	/* Now we can use the kmem_cache to allocate kmalloc slabs */
	setup_kmalloc_cache_index_table();
	create_kmalloc_caches(0);

#ifdef CONFIG_SMP
	register_cpu_notifier(&slab_notifier);
#endif

	pr_info("SLUB: HWalign=%d, Order=%d-%d, MinObjects=%d, CPUs=%d, Nodes=%d\n",
		cache_line_size(),
		slub_min_order, slub_max_order, slub_min_objects,
		nr_cpu_ids, nr_node_ids);
}

void __init kmem_cache_init_late(void)
{
}

struct kmem_cache *
__kmem_cache_alias(const char *name, size_t size, size_t align,
		   unsigned long flags, void (*ctor)(void *))
{
	struct kmem_cache *s, *c;

	s = find_mergeable(size, align, flags, name, ctor);
	if (s) {
		s->refcount++;

		/*
		 * Adjust the object sizes so that we clear
		 * the complete object on kzalloc.
		 */
		s->object_size = max(s->object_size, (int)size);
		s->inuse = max_t(int, s->inuse, ALIGN(size, sizeof(void *)));

		for_each_memcg_cache(c, s) {
			c->object_size = s->object_size;
			c->inuse = max_t(int, c->inuse,
					 ALIGN(size, sizeof(void *)));
		}

		if (sysfs_slab_alias(s, name)) {
			s->refcount--;
			s = NULL;
		}
	}

	return s;
}

int __kmem_cache_create(struct kmem_cache *s, unsigned long flags)
{
	int err;

	err = kmem_cache_open(s, flags);
	if (err)
		return err;

	/* Mutex is not taken during early boot */
	if (slab_state <= UP)
		return 0;

	memcg_propagate_slab_attrs(s);
	err = sysfs_slab_add(s);
	if (err)
		kmem_cache_close(s);

	return err;
}

#ifdef CONFIG_SMP
/*
 * Use the cpu notifier to insure that the cpu slabs are flushed when
 * necessary.
 */
static int slab_cpuup_callback(struct notifier_block *nfb,
		unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;
	struct kmem_cache *s;
	unsigned long flags;

	switch (action) {
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		mutex_lock(&slab_mutex);
		list_for_each_entry(s, &slab_caches, list) {
			local_irq_save(flags);
			__flush_cpu_slab(s, cpu);
			local_irq_restore(flags);
		}
		mutex_unlock(&slab_mutex);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block slab_notifier = {
	.notifier_call = slab_cpuup_callback
};

#endif

void *__kmalloc_track_caller(size_t size, gfp_t gfpflags, unsigned long caller)
{
	struct kmem_cache *s;
	void *ret;

	if (unlikely(size > KMALLOC_MAX_CACHE_SIZE))
		return kmalloc_large(size, gfpflags);

	s = kmalloc_slab(size, gfpflags);

	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	ret = slab_alloc(s, gfpflags, caller);

	/* Honor the call site pointer we received. */
	trace_kmalloc(caller, ret, size, s->size, gfpflags);

	return ret;
}

#ifdef CONFIG_NUMA
void *__kmalloc_node_track_caller(size_t size, gfp_t gfpflags,
					int node, unsigned long caller)
{
	struct kmem_cache *s;
	void *ret;

	if (unlikely(size > KMALLOC_MAX_CACHE_SIZE)) {
		ret = kmalloc_large_node(size, gfpflags, node);

		trace_kmalloc_node(caller, ret,
				   size, PAGE_SIZE << get_order(size),
				   gfpflags, node);

		return ret;
	}

	s = kmalloc_slab(size, gfpflags);

	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	ret = slab_alloc_node(s, gfpflags, node, caller);

	/* Honor the call site pointer we received. */
	trace_kmalloc_node(caller, ret, size, s->size, gfpflags, node);

	return ret;
}
#endif

#ifdef CONFIG_SYSFS
static int count_inuse(struct page *page)
{
	return page->inuse;
}

static int count_total(struct page *page)
{
	return page->objects;
}
#endif

#ifdef CONFIG_SLUB_DEBUG
static int validate_slab(struct kmem_cache *s, struct page *page,
						unsigned long *map)
{
	void *p;
	void *addr = page_address(page);

	if (!check_slab(s, page) ||
			!on_freelist(s, page, NULL))
		return 0;

	/* Now we know that a valid freelist exists */
	bitmap_zero(map, page->objects);

	get_map(s, page, map);
	for_each_object(p, s, addr, page->objects) {
		if (test_bit(slab_index(p, s, addr), map))
			if (!check_object(s, page, p, SLUB_RED_INACTIVE))
				return 0;
	}

	for_each_object(p, s, addr, page->objects)
		if (!test_bit(slab_index(p, s, addr), map))
			if (!check_object(s, page, p, SLUB_RED_ACTIVE))
				return 0;
	return 1;
}

static void validate_slab_slab(struct kmem_cache *s, struct page *page,
						unsigned long *map)
{
	slab_lock(page);
	validate_slab(s, page, map);
	slab_unlock(page);
}

static int validate_slab_node(struct kmem_cache *s,
		struct kmem_cache_node *n, unsigned long *map)
{
	unsigned long count = 0;
	struct page *page;
	unsigned long flags;

	spin_lock_irqsave(&n->list_lock, flags);

	list_for_each_entry(page, &n->partial, lru) {
		validate_slab_slab(s, page, map);
		count++;
	}
	if (count != n->nr_partial)
		pr_err("SLUB %s: %ld partial slabs counted but counter=%ld\n",
		       s->name, count, n->nr_partial);

	if (!(s->flags & SLAB_STORE_USER))
		goto out;

	list_for_each_entry(page, &n->full, lru) {
		validate_slab_slab(s, page, map);
		count++;
	}
	if (count != atomic_long_read(&n->nr_slabs))
		pr_err("SLUB: %s %ld slabs counted but counter=%ld\n",
		       s->name, count, atomic_long_read(&n->nr_slabs));

out:
	spin_unlock_irqrestore(&n->list_lock, flags);
	return count;
}

static long validate_slab_cache(struct kmem_cache *s)
{
	int node;
	unsigned long count = 0;
	unsigned long *map = kmalloc(BITS_TO_LONGS(oo_objects(s->max)) *
				sizeof(unsigned long), GFP_KERNEL);
	struct kmem_cache_node *n;

	if (!map)
		return -ENOMEM;

	flush_all(s);
	for_each_kmem_cache_node(s, node, n)
		count += validate_slab_node(s, n, map);
	kfree(map);
	return count;
}
/*
 * Generate lists of code addresses where slabcache objects are allocated
 * and freed.
 */

struct location {
	unsigned long count;
	unsigned long addr;
	long long sum_time;
	long min_time;
	long max_time;
	long min_pid;
	long max_pid;
	DECLARE_BITMAP(cpus, NR_CPUS);
	nodemask_t nodes;
};

struct loc_track {
	unsigned long max;
	unsigned long count;
	struct location *loc;
};

static void free_loc_track(struct loc_track *t)
{
	if (t->max)
		free_pages((unsigned long)t->loc,
			get_order(sizeof(struct location) * t->max));
}

static int alloc_loc_track(struct loc_track *t, unsigned long max, gfp_t flags)
{
	struct location *l;
	int order;

	order = get_order(sizeof(struct location) * max);

	l = (void *)__get_free_pages(flags, order);
	if (!l)
		return 0;

	if (t->count) {
		memcpy(l, t->loc, sizeof(struct location) * t->count);
		free_loc_track(t);
	}
	t->max = max;
	t->loc = l;
	return 1;
}

static int add_location(struct loc_track *t, struct kmem_cache *s,
				const struct track *track)
{
	long start, end, pos;
	struct location *l;
	unsigned long caddr;
	unsigned long age = jiffies - track->when;

	start = -1;
	end = t->count;

	for ( ; ; ) {
		pos = start + (end - start + 1) / 2;

		/*
		 * There is nothing at "end". If we end up there
		 * we need to add something to before end.
		 */
		if (pos == end)
			break;

		caddr = t->loc[pos].addr;
		if (track->addr == caddr) {

			l = &t->loc[pos];
			l->count++;
			if (track->when) {
				l->sum_time += age;
				if (age < l->min_time)
					l->min_time = age;
				if (age > l->max_time)
					l->max_time = age;

				if (track->pid < l->min_pid)
					l->min_pid = track->pid;
				if (track->pid > l->max_pid)
					l->max_pid = track->pid;

				cpumask_set_cpu(track->cpu,
						to_cpumask(l->cpus));
			}
			node_set(page_to_nid(virt_to_page(track)), l->nodes);
			return 1;
		}

		if (track->addr < caddr)
			end = pos;
		else
			start = pos;
	}

	/*
	 * Not found. Insert new tracking element.
	 */
	if (t->count >= t->max && !alloc_loc_track(t, 2 * t->max, GFP_ATOMIC))
		return 0;

	l = t->loc + pos;
	if (pos < t->count)
		memmove(l + 1, l,
			(t->count - pos) * sizeof(struct location));
	t->count++;
	l->count = 1;
	l->addr = track->addr;
	l->sum_time = age;
	l->min_time = age;
	l->max_time = age;
	l->min_pid = track->pid;
	l->max_pid = track->pid;
	cpumask_clear(to_cpumask(l->cpus));
	cpumask_set_cpu(track->cpu, to_cpumask(l->cpus));
	nodes_clear(l->nodes);
	node_set(page_to_nid(virt_to_page(track)), l->nodes);
	return 1;
}

static void process_slab(struct loc_track *t, struct kmem_cache *s,
		struct page *page, enum track_item alloc,
		unsigned long *map)
{
	void *addr = page_address(page);
	void *p;

	bitmap_zero(map, page->objects);
	get_map(s, page, map);

	for_each_object(p, s, addr, page->objects)
		if (!test_bit(slab_index(p, s, addr), map))
			add_location(t, s, get_track(s, p, alloc));
}

static int list_locations(struct kmem_cache *s, char *buf,
					enum track_item alloc)
{
	int len = 0;
	unsigned long i;
	struct loc_track t = { 0, 0, NULL };
	int node;
	unsigned long *map = kmalloc(BITS_TO_LONGS(oo_objects(s->max)) *
				     sizeof(unsigned long), GFP_KERNEL);
	struct kmem_cache_node *n;

	if (!map || !alloc_loc_track(&t, PAGE_SIZE / sizeof(struct location),
				     GFP_TEMPORARY)) {
		kfree(map);
		return sprintf(buf, "Out of memory\n");
	}
	/* Push back cpu slabs */
	flush_all(s);

	for_each_kmem_cache_node(s, node, n) {
		unsigned long flags;
		struct page *page;

		if (!atomic_long_read(&n->nr_slabs))
			continue;

		spin_lock_irqsave(&n->list_lock, flags);
		list_for_each_entry(page, &n->partial, lru)
			process_slab(&t, s, page, alloc, map);
		list_for_each_entry(page, &n->full, lru)
			process_slab(&t, s, page, alloc, map);
		spin_unlock_irqrestore(&n->list_lock, flags);
	}

	for (i = 0; i < t.count; i++) {
		struct location *l = &t.loc[i];

		if (len > PAGE_SIZE - KSYM_SYMBOL_LEN - 100)
			break;
		len += sprintf(buf + len, "%7ld ", l->count);

		if (l->addr)
			len += sprintf(buf + len, "%pS", (void *)l->addr);
		else
			len += sprintf(buf + len, "<not-available>");

		if (l->sum_time != l->min_time) {
			len += sprintf(buf + len, " age=%ld/%ld/%ld",
				l->min_time,
				(long)div_u64(l->sum_time, l->count),
				l->max_time);
		} else
			len += sprintf(buf + len, " age=%ld",
				l->min_time);

		if (l->min_pid != l->max_pid)
			len += sprintf(buf + len, " pid=%ld-%ld",
				l->min_pid, l->max_pid);
		else
			len += sprintf(buf + len, " pid=%ld",
				l->min_pid);

		if (num_online_cpus() > 1 &&
				!cpumask_empty(to_cpumask(l->cpus)) &&
				len < PAGE_SIZE - 60)
			len += scnprintf(buf + len, PAGE_SIZE - len - 50,
					 " cpus=%*pbl",
					 cpumask_pr_args(to_cpumask(l->cpus)));

		if (nr_online_nodes > 1 && !nodes_empty(l->nodes) &&
				len < PAGE_SIZE - 60)
			len += scnprintf(buf + len, PAGE_SIZE - len - 50,
					 " nodes=%*pbl",
					 nodemask_pr_args(&l->nodes));

		len += sprintf(buf + len, "\n");
	}

	free_loc_track(&t);
	kfree(map);
	if (!t.count)
		len += sprintf(buf, "No data\n");
	return len;
}
#endif

#ifdef SLUB_RESILIENCY_TEST
static void __init resiliency_test(void)
{
	u8 *p;

	BUILD_BUG_ON(KMALLOC_MIN_SIZE > 16 || KMALLOC_SHIFT_HIGH < 10);

	pr_err("SLUB resiliency testing\n");
	pr_err("-----------------------\n");
	pr_err("A. Corruption after allocation\n");

	p = kzalloc(16, GFP_KERNEL);
	p[16] = 0x12;
	pr_err("\n1. kmalloc-16: Clobber Redzone/next pointer 0x12->0x%p\n\n",
	       p + 16);

	validate_slab_cache(kmalloc_caches[4]);

	/* Hmmm... The next two are dangerous */
	p = kzalloc(32, GFP_KERNEL);
	p[32 + sizeof(void *)] = 0x34;
	pr_err("\n2. kmalloc-32: Clobber next pointer/next slab 0x34 -> -0x%p\n",
	       p);
	pr_err("If allocated object is overwritten then not detectable\n\n");

	validate_slab_cache(kmalloc_caches[5]);
	p = kzalloc(64, GFP_KERNEL);
	p += 64 + (get_cycles() & 0xff) * sizeof(void *);
	*p = 0x56;
	pr_err("\n3. kmalloc-64: corrupting random byte 0x56->0x%p\n",
	       p);
	pr_err("If allocated object is overwritten then not detectable\n\n");
	validate_slab_cache(kmalloc_caches[6]);

	pr_err("\nB. Corruption after free\n");
	p = kzalloc(128, GFP_KERNEL);
	kfree(p);
	*p = 0x78;
	pr_err("1. kmalloc-128: Clobber first word 0x78->0x%p\n\n", p);
	validate_slab_cache(kmalloc_caches[7]);

	p = kzalloc(256, GFP_KERNEL);
	kfree(p);
	p[50] = 0x9a;
	pr_err("\n2. kmalloc-256: Clobber 50th byte 0x9a->0x%p\n\n", p);
	validate_slab_cache(kmalloc_caches[8]);

	p = kzalloc(512, GFP_KERNEL);
	kfree(p);
	p[512] = 0xab;
	pr_err("\n3. kmalloc-512: Clobber redzone 0xab->0x%p\n\n", p);
	validate_slab_cache(kmalloc_caches[9]);
}
#else
#ifdef CONFIG_SYSFS
static void resiliency_test(void) {};
#endif
#endif

#ifdef CONFIG_SYSFS
enum slab_stat_type {
	SL_ALL,			/* All slabs */
	SL_PARTIAL,		/* Only partially allocated slabs */
	SL_CPU,			/* Only slabs used for cpu caches */
	SL_OBJECTS,		/* Determine allocated objects not slabs */
	SL_TOTAL		/* Determine object capacity not slabs */
};

#define SO_ALL		(1 << SL_ALL)
#define SO_PARTIAL	(1 << SL_PARTIAL)
#define SO_CPU		(1 << SL_CPU)
#define SO_OBJECTS	(1 << SL_OBJECTS)
#define SO_TOTAL	(1 << SL_TOTAL)

static ssize_t show_slab_objects(struct kmem_cache *s,
			    char *buf, unsigned long flags)
{
	unsigned long total = 0;
	int node;
	int x;
	unsigned long *nodes;

	nodes = kzalloc(sizeof(unsigned long) * nr_node_ids, GFP_KERNEL);
	if (!nodes)
		return -ENOMEM;

	if (flags & SO_CPU) {
		int cpu;

		for_each_possible_cpu(cpu) {
			struct kmem_cache_cpu *c = per_cpu_ptr(s->cpu_slab,
							       cpu);
			int node;
			struct page *page;

			page = READ_ONCE(c->page);
			if (!page)
				continue;

			node = page_to_nid(page);
			if (flags & SO_TOTAL)
				x = page->objects;
			else if (flags & SO_OBJECTS)
				x = page->inuse;
			else
				x = 1;

			total += x;
			nodes[node] += x;

			page = READ_ONCE(c->partial);
			if (page) {
				node = page_to_nid(page);
				if (flags & SO_TOTAL)
					WARN_ON_ONCE(1);
				else if (flags & SO_OBJECTS)
					WARN_ON_ONCE(1);
				else
					x = page->pages;
				total += x;
				nodes[node] += x;
			}
		}
	}

	get_online_mems();
#ifdef CONFIG_SLUB_DEBUG
	if (flags & SO_ALL) {
		struct kmem_cache_node *n;

		for_each_kmem_cache_node(s, node, n) {

			if (flags & SO_TOTAL)
				x = atomic_long_read(&n->total_objects);
			else if (flags & SO_OBJECTS)
				x = atomic_long_read(&n->total_objects) -
					count_partial(n, count_free);
			else
				x = atomic_long_read(&n->nr_slabs);
			total += x;
			nodes[node] += x;
		}

	} else
#endif
	if (flags & SO_PARTIAL) {
		struct kmem_cache_node *n;

		for_each_kmem_cache_node(s, node, n) {
			if (flags & SO_TOTAL)
				x = count_partial(n, count_total);
			else if (flags & SO_OBJECTS)
				x = count_partial(n, count_inuse);
			else
				x = n->nr_partial;
			total += x;
			nodes[node] += x;
		}
	}
	x = sprintf(buf, "%lu", total);
#ifdef CONFIG_NUMA
	for (node = 0; node < nr_node_ids; node++)
		if (nodes[node])
			x += sprintf(buf + x, " N%d=%lu",
					node, nodes[node]);
#endif
	put_online_mems();
	kfree(nodes);
	return x + sprintf(buf + x, "\n");
}

#ifdef CONFIG_SLUB_DEBUG
static int any_slab_objects(struct kmem_cache *s)
{
	int node;
	struct kmem_cache_node *n;

	for_each_kmem_cache_node(s, node, n)
		if (atomic_long_read(&n->total_objects))
			return 1;

	return 0;
}
#endif

#define to_slab_attr(n) container_of(n, struct slab_attribute, attr)
#define to_slab(n) container_of(n, struct kmem_cache, kobj)

struct slab_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kmem_cache *s, char *buf);
	ssize_t (*store)(struct kmem_cache *s, const char *x, size_t count);
};

#define SLAB_ATTR_RO(_name) \
	static struct slab_attribute _name##_attr = \
	__ATTR(_name, 0400, _name##_show, NULL)

#define SLAB_ATTR(_name) \
	static struct slab_attribute _name##_attr =  \
	__ATTR(_name, 0600, _name##_show, _name##_store)

static ssize_t slab_size_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", s->size);
}
SLAB_ATTR_RO(slab_size);

static ssize_t align_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", s->align);
}
SLAB_ATTR_RO(align);

static ssize_t object_size_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", s->object_size);
}
SLAB_ATTR_RO(object_size);

static ssize_t objs_per_slab_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", oo_objects(s->oo));
}
SLAB_ATTR_RO(objs_per_slab);

static ssize_t order_store(struct kmem_cache *s,
				const char *buf, size_t length)
{
	unsigned long order;
	int err;

	err = kstrtoul(buf, 10, &order);
	if (err)
		return err;

	if (order > slub_max_order || order < slub_min_order)
		return -EINVAL;

	calculate_sizes(s, order);
	return length;
}

static ssize_t order_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", oo_order(s->oo));
}
SLAB_ATTR(order);

static ssize_t min_partial_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%lu\n", s->min_partial);
}

static ssize_t min_partial_store(struct kmem_cache *s, const char *buf,
				 size_t length)
{
	unsigned long min;
	int err;

	err = kstrtoul(buf, 10, &min);
	if (err)
		return err;

	set_min_partial(s, min);
	return length;
}
SLAB_ATTR(min_partial);

static ssize_t cpu_partial_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%u\n", s->cpu_partial);
}

static ssize_t cpu_partial_store(struct kmem_cache *s, const char *buf,
				 size_t length)
{
	unsigned int objects;
	int err;

	err = kstrtouint(buf, 10, &objects);
	if (err)
		return err;
	if (objects && !kmem_cache_has_cpu_partial(s))
		return -EINVAL;

	s->cpu_partial = objects;
	flush_all(s);
	return length;
}
SLAB_ATTR(cpu_partial);

static ssize_t ctor_show(struct kmem_cache *s, char *buf)
{
	if (!s->ctor)
		return 0;
	return sprintf(buf, "%pS\n", s->ctor);
}
SLAB_ATTR_RO(ctor);

static ssize_t aliases_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", s->refcount < 0 ? 0 : s->refcount - 1);
}
SLAB_ATTR_RO(aliases);

static ssize_t partial_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_PARTIAL);
}
SLAB_ATTR_RO(partial);

static ssize_t cpu_slabs_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_CPU);
}
SLAB_ATTR_RO(cpu_slabs);

static ssize_t objects_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_ALL|SO_OBJECTS);
}
SLAB_ATTR_RO(objects);

static ssize_t objects_partial_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_PARTIAL|SO_OBJECTS);
}
SLAB_ATTR_RO(objects_partial);

static ssize_t slabs_cpu_partial_show(struct kmem_cache *s, char *buf)
{
	int objects = 0;
	int pages = 0;
	int cpu;
	int len;

	for_each_online_cpu(cpu) {
		struct page *page = per_cpu_ptr(s->cpu_slab, cpu)->partial;

		if (page) {
			pages += page->pages;
			objects += page->pobjects;
		}
	}

	len = sprintf(buf, "%d(%d)", objects, pages);

#ifdef CONFIG_SMP
	for_each_online_cpu(cpu) {
		struct page *page = per_cpu_ptr(s->cpu_slab, cpu) ->partial;

		if (page && len < PAGE_SIZE - 20)
			len += sprintf(buf + len, " C%d=%d(%d)", cpu,
				page->pobjects, page->pages);
	}
#endif
	return len + sprintf(buf + len, "\n");
}
SLAB_ATTR_RO(slabs_cpu_partial);

static ssize_t reclaim_account_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_RECLAIM_ACCOUNT));
}

static ssize_t reclaim_account_store(struct kmem_cache *s,
				const char *buf, size_t length)
{
	s->flags &= ~SLAB_RECLAIM_ACCOUNT;
	if (buf[0] == '1')
		s->flags |= SLAB_RECLAIM_ACCOUNT;
	return length;
}
SLAB_ATTR(reclaim_account);

static ssize_t hwcache_align_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_HWCACHE_ALIGN));
}
SLAB_ATTR_RO(hwcache_align);

#ifdef CONFIG_ZONE_DMA
static ssize_t cache_dma_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_CACHE_DMA));
}
SLAB_ATTR_RO(cache_dma);
#endif

static ssize_t destroy_by_rcu_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_DESTROY_BY_RCU));
}
SLAB_ATTR_RO(destroy_by_rcu);

static ssize_t reserved_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", s->reserved);
}
SLAB_ATTR_RO(reserved);

#ifdef CONFIG_SLUB_DEBUG
static ssize_t slabs_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_ALL);
}
SLAB_ATTR_RO(slabs);

static ssize_t total_objects_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_ALL|SO_TOTAL);
}
SLAB_ATTR_RO(total_objects);

static ssize_t sanity_checks_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_DEBUG_FREE));
}

static ssize_t sanity_checks_store(struct kmem_cache *s,
				const char *buf, size_t length)
{
	s->flags &= ~SLAB_DEBUG_FREE;
	if (buf[0] == '1') {
		s->flags &= ~__CMPXCHG_DOUBLE;
		s->flags |= SLAB_DEBUG_FREE;
	}
	return length;
}
SLAB_ATTR(sanity_checks);

static ssize_t trace_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_TRACE));
}

static ssize_t trace_store(struct kmem_cache *s, const char *buf,
							size_t length)
{
	/*
	 * Tracing a merged cache is going to give confusing results
	 * as well as cause other issues like converting a mergeable
	 * cache into an umergeable one.
	 */
	if (s->refcount > 1)
		return -EINVAL;

	s->flags &= ~SLAB_TRACE;
	if (buf[0] == '1') {
		s->flags &= ~__CMPXCHG_DOUBLE;
		s->flags |= SLAB_TRACE;
	}
	return length;
}
SLAB_ATTR(trace);

static ssize_t red_zone_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_RED_ZONE));
}

static ssize_t red_zone_store(struct kmem_cache *s,
				const char *buf, size_t length)
{
	if (any_slab_objects(s))
		return -EBUSY;

	s->flags &= ~SLAB_RED_ZONE;
	if (buf[0] == '1') {
		s->flags &= ~__CMPXCHG_DOUBLE;
		s->flags |= SLAB_RED_ZONE;
	}
	calculate_sizes(s, -1);
	return length;
}
SLAB_ATTR(red_zone);

static ssize_t poison_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_POISON));
}

static ssize_t poison_store(struct kmem_cache *s,
				const char *buf, size_t length)
{
	if (any_slab_objects(s))
		return -EBUSY;

	s->flags &= ~SLAB_POISON;
	if (buf[0] == '1') {
		s->flags &= ~__CMPXCHG_DOUBLE;
		s->flags |= SLAB_POISON;
	}
	calculate_sizes(s, -1);
	return length;
}
SLAB_ATTR(poison);

static ssize_t store_user_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_STORE_USER));
}

static ssize_t store_user_store(struct kmem_cache *s,
				const char *buf, size_t length)
{
	if (any_slab_objects(s))
		return -EBUSY;

	s->flags &= ~SLAB_STORE_USER;
	if (buf[0] == '1') {
		s->flags &= ~__CMPXCHG_DOUBLE;
		s->flags |= SLAB_STORE_USER;
	}
	calculate_sizes(s, -1);
	return length;
}
SLAB_ATTR(store_user);

static ssize_t validate_show(struct kmem_cache *s, char *buf)
{
	return 0;
}

static ssize_t validate_store(struct kmem_cache *s,
			const char *buf, size_t length)
{
	int ret = -EINVAL;

	if (buf[0] == '1') {
		ret = validate_slab_cache(s);
		if (ret >= 0)
			ret = length;
	}
	return ret;
}
SLAB_ATTR(validate);

static ssize_t alloc_calls_show(struct kmem_cache *s, char *buf)
{
	if (!(s->flags & SLAB_STORE_USER))
		return -ENOSYS;
	return list_locations(s, buf, TRACK_ALLOC);
}
SLAB_ATTR_RO(alloc_calls);

static ssize_t free_calls_show(struct kmem_cache *s, char *buf)
{
	if (!(s->flags & SLAB_STORE_USER))
		return -ENOSYS;
	return list_locations(s, buf, TRACK_FREE);
}
SLAB_ATTR_RO(free_calls);
#endif /* CONFIG_SLUB_DEBUG */

#ifdef CONFIG_FAILSLAB
static ssize_t failslab_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_FAILSLAB));
}

static ssize_t failslab_store(struct kmem_cache *s, const char *buf,
							size_t length)
{
	if (s->refcount > 1)
		return -EINVAL;

	s->flags &= ~SLAB_FAILSLAB;
	if (buf[0] == '1')
		s->flags |= SLAB_FAILSLAB;
	return length;
}
SLAB_ATTR(failslab);
#endif

static ssize_t shrink_show(struct kmem_cache *s, char *buf)
{
	return 0;
}

static ssize_t shrink_store(struct kmem_cache *s,
			const char *buf, size_t length)
{
	if (buf[0] == '1')
		kmem_cache_shrink(s);
	else
		return -EINVAL;
	return length;
}
SLAB_ATTR(shrink);

#ifdef CONFIG_NUMA
static ssize_t remote_node_defrag_ratio_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", s->remote_node_defrag_ratio / 10);
}

static ssize_t remote_node_defrag_ratio_store(struct kmem_cache *s,
				const char *buf, size_t length)
{
	unsigned long ratio;
	int err;

	err = kstrtoul(buf, 10, &ratio);
	if (err)
		return err;

	if (ratio <= 100)
		s->remote_node_defrag_ratio = ratio * 10;

	return length;
}
SLAB_ATTR(remote_node_defrag_ratio);
#endif

#ifdef CONFIG_SLUB_STATS
static int show_stat(struct kmem_cache *s, char *buf, enum stat_item si)
{
	unsigned long sum  = 0;
	int cpu;
	int len;
	int *data = kmalloc(nr_cpu_ids * sizeof(int), GFP_KERNEL);

	if (!data)
		return -ENOMEM;

	for_each_online_cpu(cpu) {
		unsigned x = per_cpu_ptr(s->cpu_slab, cpu)->stat[si];

		data[cpu] = x;
		sum += x;
	}

	len = sprintf(buf, "%lu", sum);

#ifdef CONFIG_SMP
	for_each_online_cpu(cpu) {
		if (data[cpu] && len < PAGE_SIZE - 20)
			len += sprintf(buf + len, " C%d=%u", cpu, data[cpu]);
	}
#endif
	kfree(data);
	return len + sprintf(buf + len, "\n");
}

static void clear_stat(struct kmem_cache *s, enum stat_item si)
{
	int cpu;

	for_each_online_cpu(cpu)
		per_cpu_ptr(s->cpu_slab, cpu)->stat[si] = 0;
}

#define STAT_ATTR(si, text) 					\
static ssize_t text##_show(struct kmem_cache *s, char *buf)	\
{								\
	return show_stat(s, buf, si);				\
}								\
static ssize_t text##_store(struct kmem_cache *s,		\
				const char *buf, size_t length)	\
{								\
	if (buf[0] != '0')					\
		return -EINVAL;					\
	clear_stat(s, si);					\
	return length;						\
}								\
SLAB_ATTR(text);						\

STAT_ATTR(ALLOC_FASTPATH, alloc_fastpath);
STAT_ATTR(ALLOC_SLOWPATH, alloc_slowpath);
STAT_ATTR(FREE_FASTPATH, free_fastpath);
STAT_ATTR(FREE_SLOWPATH, free_slowpath);
STAT_ATTR(FREE_FROZEN, free_frozen);
STAT_ATTR(FREE_ADD_PARTIAL, free_add_partial);
STAT_ATTR(FREE_REMOVE_PARTIAL, free_remove_partial);
STAT_ATTR(ALLOC_FROM_PARTIAL, alloc_from_partial);
STAT_ATTR(ALLOC_SLAB, alloc_slab);
STAT_ATTR(ALLOC_REFILL, alloc_refill);
STAT_ATTR(ALLOC_NODE_MISMATCH, alloc_node_mismatch);
STAT_ATTR(FREE_SLAB, free_slab);
STAT_ATTR(CPUSLAB_FLUSH, cpuslab_flush);
STAT_ATTR(DEACTIVATE_FULL, deactivate_full);
STAT_ATTR(DEACTIVATE_EMPTY, deactivate_empty);
STAT_ATTR(DEACTIVATE_TO_HEAD, deactivate_to_head);
STAT_ATTR(DEACTIVATE_TO_TAIL, deactivate_to_tail);
STAT_ATTR(DEACTIVATE_REMOTE_FREES, deactivate_remote_frees);
STAT_ATTR(DEACTIVATE_BYPASS, deactivate_bypass);
STAT_ATTR(ORDER_FALLBACK, order_fallback);
STAT_ATTR(CMPXCHG_DOUBLE_CPU_FAIL, cmpxchg_double_cpu_fail);
STAT_ATTR(CMPXCHG_DOUBLE_FAIL, cmpxchg_double_fail);
STAT_ATTR(CPU_PARTIAL_ALLOC, cpu_partial_alloc);
STAT_ATTR(CPU_PARTIAL_FREE, cpu_partial_free);
STAT_ATTR(CPU_PARTIAL_NODE, cpu_partial_node);
STAT_ATTR(CPU_PARTIAL_DRAIN, cpu_partial_drain);
#endif

static struct attribute *slab_attrs[] = {
	&slab_size_attr.attr,
	&object_size_attr.attr,
	&objs_per_slab_attr.attr,
	&order_attr.attr,
	&min_partial_attr.attr,
	&cpu_partial_attr.attr,
	&objects_attr.attr,
	&objects_partial_attr.attr,
	&partial_attr.attr,
	&cpu_slabs_attr.attr,
	&ctor_attr.attr,
	&aliases_attr.attr,
	&align_attr.attr,
	&hwcache_align_attr.attr,
	&reclaim_account_attr.attr,
	&destroy_by_rcu_attr.attr,
	&shrink_attr.attr,
	&reserved_attr.attr,
	&slabs_cpu_partial_attr.attr,
#ifdef CONFIG_SLUB_DEBUG
	&total_objects_attr.attr,
	&slabs_attr.attr,
	&sanity_checks_attr.attr,
	&trace_attr.attr,
	&red_zone_attr.attr,
	&poison_attr.attr,
	&store_user_attr.attr,
	&validate_attr.attr,
	&alloc_calls_attr.attr,
	&free_calls_attr.attr,
#endif
#ifdef CONFIG_ZONE_DMA
	&cache_dma_attr.attr,
#endif
#ifdef CONFIG_NUMA
	&remote_node_defrag_ratio_attr.attr,
#endif
#ifdef CONFIG_SLUB_STATS
	&alloc_fastpath_attr.attr,
	&alloc_slowpath_attr.attr,
	&free_fastpath_attr.attr,
	&free_slowpath_attr.attr,
	&free_frozen_attr.attr,
	&free_add_partial_attr.attr,
	&free_remove_partial_attr.attr,
	&alloc_from_partial_attr.attr,
	&alloc_slab_attr.attr,
	&alloc_refill_attr.attr,
	&alloc_node_mismatch_attr.attr,
	&free_slab_attr.attr,
	&cpuslab_flush_attr.attr,
	&deactivate_full_attr.attr,
	&deactivate_empty_attr.attr,
	&deactivate_to_head_attr.attr,
	&deactivate_to_tail_attr.attr,
	&deactivate_remote_frees_attr.attr,
	&deactivate_bypass_attr.attr,
	&order_fallback_attr.attr,
	&cmpxchg_double_fail_attr.attr,
	&cmpxchg_double_cpu_fail_attr.attr,
	&cpu_partial_alloc_attr.attr,
	&cpu_partial_free_attr.attr,
	&cpu_partial_node_attr.attr,
	&cpu_partial_drain_attr.attr,
#endif
#ifdef CONFIG_FAILSLAB
	&failslab_attr.attr,
#endif

	NULL
};

static struct attribute_group slab_attr_group = {
	.attrs = slab_attrs,
};

static ssize_t slab_attr_show(struct kobject *kobj,
				struct attribute *attr,
				char *buf)
{
	struct slab_attribute *attribute;
	struct kmem_cache *s;
	int err;

	attribute = to_slab_attr(attr);
	s = to_slab(kobj);

	if (!attribute->show)
		return -EIO;

	err = attribute->show(s, buf);

	return err;
}

static ssize_t slab_attr_store(struct kobject *kobj,
				struct attribute *attr,
				const char *buf, size_t len)
{
	struct slab_attribute *attribute;
	struct kmem_cache *s;
	int err;

	attribute = to_slab_attr(attr);
	s = to_slab(kobj);

	if (!attribute->store)
		return -EIO;

	err = attribute->store(s, buf, len);
#ifdef CONFIG_MEMCG_KMEM
	if (slab_state >= FULL && err >= 0 && is_root_cache(s)) {
		struct kmem_cache *c;

		mutex_lock(&slab_mutex);
		if (s->max_attr_size < len)
			s->max_attr_size = len;

		/*
		 * This is a best effort propagation, so this function's return
		 * value will be determined by the parent cache only. This is
		 * basically because not all attributes will have a well
		 * defined semantics for rollbacks - most of the actions will
		 * have permanent effects.
		 *
		 * Returning the error value of any of the children that fail
		 * is not 100 % defined, in the sense that users seeing the
		 * error code won't be able to know anything about the state of
		 * the cache.
		 *
		 * Only returning the error code for the parent cache at least
		 * has well defined semantics. The cache being written to
		 * directly either failed or succeeded, in which case we loop
		 * through the descendants with best-effort propagation.
		 */
		for_each_memcg_cache(c, s)
			attribute->store(c, buf, len);
		mutex_unlock(&slab_mutex);
	}
#endif
	return err;
}

static void memcg_propagate_slab_attrs(struct kmem_cache *s)
{
#ifdef CONFIG_MEMCG_KMEM
	int i;
	char *buffer = NULL;
	struct kmem_cache *root_cache;

	if (is_root_cache(s))
		return;

	root_cache = s->memcg_params.root_cache;

	/*
	 * This mean this cache had no attribute written. Therefore, no point
	 * in copying default values around
	 */
	if (!root_cache->max_attr_size)
		return;

	for (i = 0; i < ARRAY_SIZE(slab_attrs); i++) {
		char mbuf[64];
		char *buf;
		struct slab_attribute *attr = to_slab_attr(slab_attrs[i]);
		ssize_t len;

		if (!attr || !attr->store || !attr->show)
			continue;

		/*
		 * It is really bad that we have to allocate here, so we will
		 * do it only as a fallback. If we actually allocate, though,
		 * we can just use the allocated buffer until the end.
		 *
		 * Most of the slub attributes will tend to be very small in
		 * size, but sysfs allows buffers up to a page, so they can
		 * theoretically happen.
		 */
		if (buffer)
			buf = buffer;
		else if (root_cache->max_attr_size < ARRAY_SIZE(mbuf))
			buf = mbuf;
		else {
			buffer = (char *) get_zeroed_page(GFP_KERNEL);
			if (WARN_ON(!buffer))
				continue;
			buf = buffer;
		}

		len = attr->show(root_cache, buf);
		if (len > 0)
			attr->store(s, buf, len);
	}

	if (buffer)
		free_page((unsigned long)buffer);
#endif
}

static void kmem_cache_release(struct kobject *k)
{
	slab_kmem_cache_release(to_slab(k));
}

static const struct sysfs_ops slab_sysfs_ops = {
	.show = slab_attr_show,
	.store = slab_attr_store,
};

static struct kobj_type slab_ktype = {
	.sysfs_ops = &slab_sysfs_ops,
	.release = kmem_cache_release,
};

static int uevent_filter(struct kset *kset, struct kobject *kobj)
{
	struct kobj_type *ktype = get_ktype(kobj);

	if (ktype == &slab_ktype)
		return 1;
	return 0;
}

static const struct kset_uevent_ops slab_uevent_ops = {
	.filter = uevent_filter,
};

static struct kset *slab_kset;

static inline struct kset *cache_kset(struct kmem_cache *s)
{
#ifdef CONFIG_MEMCG_KMEM
	if (!is_root_cache(s))
		return s->memcg_params.root_cache->memcg_kset;
#endif
	return slab_kset;
}

#define ID_STR_LENGTH 64

/* Create a unique string id for a slab cache:
 *
 * Format	:[flags-]size
 */
static char *create_unique_id(struct kmem_cache *s)
{
	char *name = kmalloc(ID_STR_LENGTH, GFP_KERNEL);
	char *p = name;

	BUG_ON(!name);

	*p++ = ':';
	/*
	 * First flags affecting slabcache operations. We will only
	 * get here for aliasable slabs so we do not need to support
	 * too many flags. The flags here must cover all flags that
	 * are matched during merging to guarantee that the id is
	 * unique.
	 */
	if (s->flags & SLAB_CACHE_DMA)
		*p++ = 'd';
	if (s->flags & SLAB_RECLAIM_ACCOUNT)
		*p++ = 'a';
	if (s->flags & SLAB_DEBUG_FREE)
		*p++ = 'F';
	if (!(s->flags & SLAB_NOTRACK))
		*p++ = 't';
	if (p != name + 1)
		*p++ = '-';
	p += sprintf(p, "%07d", s->size);

	BUG_ON(p > name + ID_STR_LENGTH - 1);
	return name;
}

static int sysfs_slab_add(struct kmem_cache *s)
{
	int err;
	const char *name;
	int unmergeable = slab_unmergeable(s);

	if (unmergeable) {
		/*
		 * Slabcache can never be merged so we can use the name proper.
		 * This is typically the case for debug situations. In that
		 * case we can catch duplicate names easily.
		 */
		sysfs_remove_link(&slab_kset->kobj, s->name);
		name = s->name;
	} else {
		/*
		 * Create a unique name for the slab as a target
		 * for the symlinks.
		 */
		name = create_unique_id(s);
	}

	s->kobj.kset = cache_kset(s);
	err = kobject_init_and_add(&s->kobj, &slab_ktype, NULL, "%s", name);
	if (err)
		goto out;

	err = sysfs_create_group(&s->kobj, &slab_attr_group);
	if (err)
		goto out_del_kobj;

#ifdef CONFIG_MEMCG_KMEM
	if (is_root_cache(s)) {
		s->memcg_kset = kset_create_and_add("cgroup", NULL, &s->kobj);
		if (!s->memcg_kset) {
			err = -ENOMEM;
			goto out_del_kobj;
		}
	}
#endif

	kobject_uevent(&s->kobj, KOBJ_ADD);
	if (!unmergeable) {
		/* Setup first alias */
		sysfs_slab_alias(s, s->name);
	}
out:
	if (!unmergeable)
		kfree(name);
	return err;
out_del_kobj:
	kobject_del(&s->kobj);
	goto out;
}

void sysfs_slab_remove(struct kmem_cache *s)
{
	if (slab_state < FULL)
		/*
		 * Sysfs has not been setup yet so no need to remove the
		 * cache from sysfs.
		 */
		return;

#ifdef CONFIG_MEMCG_KMEM
	kset_unregister(s->memcg_kset);
#endif
	kobject_uevent(&s->kobj, KOBJ_REMOVE);
	kobject_del(&s->kobj);
	kobject_put(&s->kobj);
}

/*
 * Need to buffer aliases during bootup until sysfs becomes
 * available lest we lose that information.
 */
struct saved_alias {
	struct kmem_cache *s;
	const char *name;
	struct saved_alias *next;
};

static struct saved_alias *alias_list;

static int sysfs_slab_alias(struct kmem_cache *s, const char *name)
{
	struct saved_alias *al;

	if (slab_state == FULL) {
		/*
		 * If we have a leftover link then remove it.
		 */
		sysfs_remove_link(&slab_kset->kobj, name);
		return sysfs_create_link(&slab_kset->kobj, &s->kobj, name);
	}

	al = kmalloc(sizeof(struct saved_alias), GFP_KERNEL);
	if (!al)
		return -ENOMEM;

	al->s = s;
	al->name = name;
	al->next = alias_list;
	alias_list = al;
	return 0;
}

static int __init slab_sysfs_init(void)
{
	struct kmem_cache *s;
	int err;

	mutex_lock(&slab_mutex);

	slab_kset = kset_create_and_add("slab", &slab_uevent_ops, kernel_kobj);
	if (!slab_kset) {
		mutex_unlock(&slab_mutex);
		pr_err("Cannot register slab subsystem.\n");
		return -ENOSYS;
	}

	slab_state = FULL;

	list_for_each_entry(s, &slab_caches, list) {
		err = sysfs_slab_add(s);
		if (err)
			pr_err("SLUB: Unable to add boot slab %s to sysfs\n",
			       s->name);
	}

	while (alias_list) {
		struct saved_alias *al = alias_list;

		alias_list = alias_list->next;
		err = sysfs_slab_alias(al->s, al->name);
		if (err)
			pr_err("SLUB: Unable to add boot slab alias %s to sysfs\n",
			       al->name);
		kfree(al);
	}

	mutex_unlock(&slab_mutex);
	resiliency_test();
	return 0;
}

__initcall(slab_sysfs_init);
#endif /* CONFIG_SYSFS */

/*
 * The /proc/slabinfo ABI
 */
#ifdef CONFIG_SLABINFO
void get_slabinfo(struct kmem_cache *s, struct slabinfo *sinfo)
{
	unsigned long nr_slabs = 0;
	unsigned long nr_objs = 0;
	unsigned long nr_free = 0;
	int node;
	struct kmem_cache_node *n;

	for_each_kmem_cache_node(s, node, n) {
		nr_slabs += node_nr_slabs(n);
		nr_objs += node_nr_objs(n);
		nr_free += count_partial(n, count_free);
	}

	sinfo->active_objs = nr_objs - nr_free;
	sinfo->num_objs = nr_objs;
	sinfo->active_slabs = nr_slabs;
	sinfo->num_slabs = nr_slabs;
	sinfo->objects_per_slab = oo_objects(s->oo);
	sinfo->cache_order = oo_order(s->oo);
}

void slabinfo_show_stats(struct seq_file *m, struct kmem_cache *s)
{
}

ssize_t slabinfo_write(struct file *file, const char __user *buffer,
		       size_t count, loff_t *ppos)
{
	return -EIO;
}
#endif /* CONFIG_SLABINFO */
