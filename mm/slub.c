/*
 * SLUB: A slab allocator that limits cache line use instead of queuing
 * objects in per cpu and per node lists.
 *
 * The allocator synchronizes using per slab locks and only
 * uses a centralized lock to manage a pool of partial slabs.
 *
 * (C) 2007 SGI, Christoph Lameter
 */

#include <linux/mm.h>
#include <linux/swap.h> /* struct reclaim_state */
#include <linux/module.h>
#include <linux/bit_spinlock.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
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

/*
 * Lock order:
 *   1. slab_lock(page)
 *   2. slab->list_lock
 *
 *   The slab_lock protects operations on the object of a particular
 *   slab and its metadata in the page struct. If the slab lock
 *   has been taken then no allocations nor frees can be performed
 *   on the objects in the slab nor can the slab be added or removed
 *   from the partial or full lists since this would mean modifying
 *   the page_struct of the slab.
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
 *
 *   The lock order is sometimes inverted when we are trying to get a slab
 *   off a list. We take the list_lock and then look for a page on the list
 *   to use. While we do that objects in the slabs may be freed. We can
 *   only operate on the slab if we have also taken the slab_lock. So we use
 *   a slab_trylock() on the slab. If trylock was successful then no frees
 *   can occur anymore and we can use the slab for allocations etc. If the
 *   slab_trylock() does not succeed then frees are in progress in the slab and
 *   we must stay away from it for a while since we may cause a bouncing
 *   cacheline if we try to acquire the lock. So go onto the next slab.
 *   If all pages are busy then we may allocate a new slab instead of reusing
 *   a partial slab. A new slab has noone operating on it and thus there is
 *   no danger of cacheline contention.
 *
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

#define SLAB_DEBUG_FLAGS (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER | \
		SLAB_TRACE | SLAB_DEBUG_FREE)

static inline int kmem_cache_debug(struct kmem_cache *s)
{
#ifdef CONFIG_SLUB_DEBUG
	return unlikely(s->flags & SLAB_DEBUG_FLAGS);
#else
	return 0;
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

/*
 * Mininum number of partial slabs. These will be left on the partial
 * lists even if they are empty. kmem_cache_shrink may reclaim them.
 */
#define MIN_PARTIAL 5

/*
 * Maximum number of desirable partial slabs.
 * The existence of more partial slabs makes kmem_cache_shrink
 * sort the partial list by the number of objects in the.
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

/*
 * Set of flags that will prevent slab merging
 */
#define SLUB_NEVER_MERGE (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER | \
		SLAB_TRACE | SLAB_DESTROY_BY_RCU | SLAB_NOLEAKTRACE | \
		SLAB_FAILSLAB)

#define SLUB_MERGE_SAME (SLAB_DEBUG_FREE | SLAB_RECLAIM_ACCOUNT | \
		SLAB_CACHE_DMA | SLAB_NOTRACK)

#define OO_SHIFT	16
#define OO_MASK		((1 << OO_SHIFT) - 1)
#define MAX_OBJS_PER_PAGE	65535 /* since page.objects is u16 */

/* Internal SLUB flags */
#define __OBJECT_POISON		0x80000000UL /* Poison object */

static int kmem_size = sizeof(struct kmem_cache);

#ifdef CONFIG_SMP
static struct notifier_block slab_notifier;
#endif

static enum {
	DOWN,		/* No slab functionality available */
	PARTIAL,	/* Kmem_cache_node works */
	UP,		/* Everything works but does not show up in sysfs */
	SYSFS		/* Sysfs up */
} slab_state = DOWN;

/* A list of all slab caches on the system */
static DECLARE_RWSEM(slub_lock);
static LIST_HEAD(slab_caches);

/*
 * Tracking user of a slab.
 */
struct track {
	unsigned long addr;	/* Called from address */
	int cpu;		/* Was running on cpu */
	int pid;		/* Pid context */
	unsigned long when;	/* When did the operation occur */
};

enum track_item { TRACK_ALLOC, TRACK_FREE };

#ifdef CONFIG_SLUB_DEBUG
static int sysfs_slab_add(struct kmem_cache *);
static int sysfs_slab_alias(struct kmem_cache *, const char *);
static void sysfs_slab_remove(struct kmem_cache *);

#else
static inline int sysfs_slab_add(struct kmem_cache *s) { return 0; }
static inline int sysfs_slab_alias(struct kmem_cache *s, const char *p)
							{ return 0; }
static inline void sysfs_slab_remove(struct kmem_cache *s)
{
	kfree(s);
}

#endif

static inline void stat(struct kmem_cache *s, enum stat_item si)
{
#ifdef CONFIG_SLUB_STATS
	__this_cpu_inc(s->cpu_slab->stat[si]);
#endif
}

/********************************************************************
 * 			Core slab cache functions
 *******************************************************************/

int slab_is_available(void)
{
	return slab_state >= UP;
}

static inline struct kmem_cache_node *get_node(struct kmem_cache *s, int node)
{
#ifdef CONFIG_NUMA
	return s->node[node];
#else
	return &s->local_node;
#endif
}

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

static inline void set_freepointer(struct kmem_cache *s, void *object, void *fp)
{
	*(void **)(object + s->offset) = fp;
}

/* Loop over all objects in a slab */
#define for_each_object(__p, __s, __addr, __objects) \
	for (__p = (__addr); __p < (__addr) + (__objects) * (__s)->size;\
			__p += (__s)->size)

/* Scan freelist */
#define for_each_free_object(__p, __s, __free) \
	for (__p = (__free); __p; __p = get_freepointer((__s), __p))

/* Determine object index from a given position */
static inline int slab_index(void *p, struct kmem_cache *s, void *addr)
{
	return (p - addr) / s->size;
}

static inline struct kmem_cache_order_objects oo_make(int order,
						unsigned long size)
{
	struct kmem_cache_order_objects x = {
		(order << OO_SHIFT) + (PAGE_SIZE << order) / size
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

#ifdef CONFIG_SLUB_DEBUG
/*
 * Debug settings:
 */
#ifdef CONFIG_SLUB_DEBUG_ON
static int slub_debug = DEBUG_DEFAULT_FLAGS;
#else
static int slub_debug;
#endif

static char *slub_debug_slabs;
static int disable_higher_order_debug;

/*
 * Object debugging
 */
static void print_section(char *text, u8 *addr, unsigned int length)
{
	int i, offset;
	int newline = 1;
	char ascii[17];

	ascii[16] = 0;

	for (i = 0; i < length; i++) {
		if (newline) {
			printk(KERN_ERR "%8s 0x%p: ", text, addr + i);
			newline = 0;
		}
		printk(KERN_CONT " %02x", addr[i]);
		offset = i % 16;
		ascii[offset] = isgraph(addr[i]) ? addr[i] : '.';
		if (offset == 15) {
			printk(KERN_CONT " %s\n", ascii);
			newline = 1;
		}
	}
	if (!newline) {
		i %= 16;
		while (i < 16) {
			printk(KERN_CONT "   ");
			ascii[i] = ' ';
			i++;
		}
		printk(KERN_CONT " %s\n", ascii);
	}
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

	printk(KERN_ERR "INFO: %s in %pS age=%lu cpu=%u pid=%d\n",
		s, (void *)t->addr, jiffies - t->when, t->cpu, t->pid);
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
	printk(KERN_ERR "INFO: Slab 0x%p objects=%u used=%u fp=0x%p flags=0x%04lx\n",
		page, page->objects, page->inuse, page->freelist, page->flags);

}

static void slab_bug(struct kmem_cache *s, char *fmt, ...)
{
	va_list args;
	char buf[100];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	printk(KERN_ERR "========================================"
			"=====================================\n");
	printk(KERN_ERR "BUG %s: %s\n", s->name, buf);
	printk(KERN_ERR "----------------------------------------"
			"-------------------------------------\n\n");
}

static void slab_fix(struct kmem_cache *s, char *fmt, ...)
{
	va_list args;
	char buf[100];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	printk(KERN_ERR "FIX %s: %s\n", s->name, buf);
}

static void print_trailer(struct kmem_cache *s, struct page *page, u8 *p)
{
	unsigned int off;	/* Offset of last byte */
	u8 *addr = page_address(page);

	print_tracking(s, p);

	print_page_info(page);

	printk(KERN_ERR "INFO: Object 0x%p @offset=%tu fp=0x%p\n\n",
			p, p - addr, get_freepointer(s, p));

	if (p > addr + 16)
		print_section("Bytes b4", p - 16, 16);

	print_section("Object", p, min_t(unsigned long, s->objsize, PAGE_SIZE));

	if (s->flags & SLAB_RED_ZONE)
		print_section("Redzone", p + s->objsize,
			s->inuse - s->objsize);

	if (s->offset)
		off = s->offset + sizeof(void *);
	else
		off = s->inuse;

	if (s->flags & SLAB_STORE_USER)
		off += 2 * sizeof(struct track);

	if (off != s->size)
		/* Beginning of the filler is the free pointer */
		print_section("Padding", p + off, s->size - off);

	dump_stack();
}

static void object_err(struct kmem_cache *s, struct page *page,
			u8 *object, char *reason)
{
	slab_bug(s, "%s", reason);
	print_trailer(s, page, object);
}

static void slab_err(struct kmem_cache *s, struct page *page, char *fmt, ...)
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

static void init_object(struct kmem_cache *s, void *object, int active)
{
	u8 *p = object;

	if (s->flags & __OBJECT_POISON) {
		memset(p, POISON_FREE, s->objsize - 1);
		p[s->objsize - 1] = POISON_END;
	}

	if (s->flags & SLAB_RED_ZONE)
		memset(p + s->objsize,
			active ? SLUB_RED_ACTIVE : SLUB_RED_INACTIVE,
			s->inuse - s->objsize);
}

static u8 *check_bytes(u8 *start, unsigned int value, unsigned int bytes)
{
	while (bytes) {
		if (*start != (u8)value)
			return start;
		start++;
		bytes--;
	}
	return NULL;
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

	fault = check_bytes(start, value, bytes);
	if (!fault)
		return 1;

	end = start + bytes;
	while (end > fault && end[-1] == value)
		end--;

	slab_bug(s, "%s overwritten", what);
	printk(KERN_ERR "INFO: 0x%p-0x%p. First byte 0x%x instead of 0x%x\n",
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
 * object + s->objsize
 * 	Padding to reach word boundary. This is also used for Redzoning.
 * 	Padding is extended by another word if Redzoning is enabled and
 * 	objsize == inuse.
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
 * If slabcaches are merged then the objsize and inuse boundaries are mostly
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
	length = (PAGE_SIZE << compound_order(page));
	end = start + length;
	remainder = length % s->size;
	if (!remainder)
		return 1;

	fault = check_bytes(end - remainder, POISON_INUSE, remainder);
	if (!fault)
		return 1;
	while (end > fault && end[-1] == POISON_INUSE)
		end--;

	slab_err(s, page, "Padding overwritten. 0x%p-0x%p", fault, end - 1);
	print_section("Padding", end - remainder, remainder);

	restore_bytes(s, "slab padding", POISON_INUSE, end - remainder, end);
	return 0;
}

static int check_object(struct kmem_cache *s, struct page *page,
					void *object, int active)
{
	u8 *p = object;
	u8 *endobject = object + s->objsize;

	if (s->flags & SLAB_RED_ZONE) {
		unsigned int red =
			active ? SLUB_RED_ACTIVE : SLUB_RED_INACTIVE;

		if (!check_bytes_and_report(s, page, object, "Redzone",
			endobject, red, s->inuse - s->objsize))
			return 0;
	} else {
		if ((s->flags & SLAB_POISON) && s->objsize < s->inuse) {
			check_bytes_and_report(s, page, p, "Alignment padding",
				endobject, POISON_INUSE, s->inuse - s->objsize);
		}
	}

	if (s->flags & SLAB_POISON) {
		if (!active && (s->flags & __OBJECT_POISON) &&
			(!check_bytes_and_report(s, page, p, "Poison", p,
					POISON_FREE, s->objsize - 1) ||
			 !check_bytes_and_report(s, page, p, "Poison",
				p + s->objsize - 1, POISON_END, 1)))
			return 0;
		/*
		 * check_pad_bytes cleans up on its own.
		 */
		check_pad_bytes(s, page, p);
	}

	if (!s->offset && active)
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

	maxobj = (PAGE_SIZE << compound_order(page)) / s->size;
	if (page->objects > maxobj) {
		slab_err(s, page, "objects %u > max %u",
			s->name, page->objects, maxobj);
		return 0;
	}
	if (page->inuse > page->objects) {
		slab_err(s, page, "inuse %u > max %u",
			s->name, page->inuse, page->objects);
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
	void *fp = page->freelist;
	void *object = NULL;
	unsigned long max_objects;

	while (fp && nr <= page->objects) {
		if (fp == search)
			return 1;
		if (!check_valid_pointer(s, page, fp)) {
			if (object) {
				object_err(s, page, object,
					"Freechain corrupt");
				set_freepointer(s, object, NULL);
				break;
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

	max_objects = (PAGE_SIZE << compound_order(page)) / s->size;
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
		printk(KERN_INFO "TRACE %s %s 0x%p inuse=%d fp=0x%p\n",
			s->name,
			alloc ? "alloc" : "free",
			object, page->inuse,
			page->freelist);

		if (!alloc)
			print_section("Object", (void *)object, s->objsize);

		dump_stack();
	}
}

/*
 * Hooks for other subsystems that check memory allocations. In a typical
 * production configuration these hooks all should produce no code at all.
 */
static inline int slab_pre_alloc_hook(struct kmem_cache *s, gfp_t flags)
{
	lockdep_trace_alloc(flags);
	might_sleep_if(flags & __GFP_WAIT);

	return should_failslab(s->objsize, flags, s->flags);
}

static inline void slab_post_alloc_hook(struct kmem_cache *s, gfp_t flags, void *object)
{
	kmemcheck_slab_alloc(s, flags, object, s->objsize);
	kmemleak_alloc_recursive(object, s->objsize, 1, s->flags, flags);
}

static inline void slab_free_hook(struct kmem_cache *s, void *x)
{
	kmemleak_free_recursive(x, s->flags);
}

static inline void slab_free_hook_irq(struct kmem_cache *s, void *object)
{
	kmemcheck_slab_free(s, object, s->objsize);
	debug_check_no_locks_freed(object, s->objsize);
	if (!(s->flags & SLAB_DEBUG_OBJECTS))
		debug_check_no_obj_freed(object, s->objsize);
}

/*
 * Tracking of fully allocated slabs for debugging purposes.
 */
static void add_full(struct kmem_cache_node *n, struct page *page)
{
	spin_lock(&n->list_lock);
	list_add(&page->lru, &n->full);
	spin_unlock(&n->list_lock);
}

static void remove_full(struct kmem_cache *s, struct page *page)
{
	struct kmem_cache_node *n;

	if (!(s->flags & SLAB_STORE_USER))
		return;

	n = get_node(s, page_to_nid(page));

	spin_lock(&n->list_lock);
	list_del(&page->lru);
	spin_unlock(&n->list_lock);
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
	if (!NUMA_BUILD || n) {
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

	init_object(s, object, 0);
	init_tracking(s, object);
}

static noinline int alloc_debug_processing(struct kmem_cache *s, struct page *page,
					void *object, unsigned long addr)
{
	if (!check_slab(s, page))
		goto bad;

	if (!on_freelist(s, page, object)) {
		object_err(s, page, object, "Object already allocated");
		goto bad;
	}

	if (!check_valid_pointer(s, page, object)) {
		object_err(s, page, object, "Freelist Pointer check fails");
		goto bad;
	}

	if (!check_object(s, page, object, 0))
		goto bad;

	/* Success perform special debug activities for allocs */
	if (s->flags & SLAB_STORE_USER)
		set_track(s, object, TRACK_ALLOC, addr);
	trace(s, page, object, 1);
	init_object(s, object, 1);
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

static noinline int free_debug_processing(struct kmem_cache *s,
		 struct page *page, void *object, unsigned long addr)
{
	if (!check_slab(s, page))
		goto fail;

	if (!check_valid_pointer(s, page, object)) {
		slab_err(s, page, "Invalid object pointer 0x%p", object);
		goto fail;
	}

	if (on_freelist(s, page, object)) {
		object_err(s, page, object, "Object already free");
		goto fail;
	}

	if (!check_object(s, page, object, 1))
		return 0;

	if (unlikely(s != page->slab)) {
		if (!PageSlab(page)) {
			slab_err(s, page, "Attempt to free object(0x%p) "
				"outside of slab", object);
		} else if (!page->slab) {
			printk(KERN_ERR
				"SLUB <none>: no slab for object 0x%p.\n",
						object);
			dump_stack();
		} else
			object_err(s, page, object,
					"page slab pointer corrupt.");
		goto fail;
	}

	/* Special debug activities for freeing objects */
	if (!PageSlubFrozen(page) && !page->freelist)
		remove_full(s, page);
	if (s->flags & SLAB_STORE_USER)
		set_track(s, object, TRACK_FREE, addr);
	trace(s, page, object, 0);
	init_object(s, object, 0);
	return 1;

fail:
	slab_fix(s, "Object at 0x%p not freed", object);
	return 0;
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

	if (tolower(*str) == 'o') {
		/*
		 * Avoid enabling debugging on caches if its minimum order
		 * would increase as a result.
		 */
		disable_higher_order_debug = 1;
		goto out;
	}

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
		default:
			printk(KERN_ERR "slub_debug option '%c' "
				"unknown. skipped\n", *str);
		}
	}

check_slabs:
	if (*str == ',')
		slub_debug_slabs = str + 1;
out:
	return 1;
}

__setup("slub_debug", setup_slub_debug);

static unsigned long kmem_cache_flags(unsigned long objsize,
	unsigned long flags, const char *name,
	void (*ctor)(void *))
{
	/*
	 * Enable debugging if selected on the kernel commandline.
	 */
	if (slub_debug && (!slub_debug_slabs ||
		!strncmp(slub_debug_slabs, name, strlen(slub_debug_slabs))))
		flags |= slub_debug;

	return flags;
}
#else
static inline void setup_object_debug(struct kmem_cache *s,
			struct page *page, void *object) {}

static inline int alloc_debug_processing(struct kmem_cache *s,
	struct page *page, void *object, unsigned long addr) { return 0; }

static inline int free_debug_processing(struct kmem_cache *s,
	struct page *page, void *object, unsigned long addr) { return 0; }

static inline int slab_pad_check(struct kmem_cache *s, struct page *page)
			{ return 1; }
static inline int check_object(struct kmem_cache *s, struct page *page,
			void *object, int active) { return 1; }
static inline void add_full(struct kmem_cache_node *n, struct page *page) {}
static inline unsigned long kmem_cache_flags(unsigned long objsize,
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
#endif

/*
 * Slab allocation and freeing
 */
static inline struct page *alloc_slab_page(gfp_t flags, int node,
					struct kmem_cache_order_objects oo)
{
	int order = oo_order(oo);

	flags |= __GFP_NOTRACK;

	if (node == NUMA_NO_NODE)
		return alloc_pages(flags, order);
	else
		return alloc_pages_exact_node(node, flags, order);
}

static struct page *allocate_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	struct page *page;
	struct kmem_cache_order_objects oo = s->oo;
	gfp_t alloc_gfp;

	flags |= s->allocflags;

	/*
	 * Let the initial higher-order allocation fail under memory pressure
	 * so we fall-back to the minimum order allocation.
	 */
	alloc_gfp = (flags | __GFP_NOWARN | __GFP_NORETRY) & ~__GFP_NOFAIL;

	page = alloc_slab_page(alloc_gfp, node, oo);
	if (unlikely(!page)) {
		oo = s->min;
		/*
		 * Allocation may have failed due to fragmentation.
		 * Try a lower order alloc if possible
		 */
		page = alloc_slab_page(flags, node, oo);
		if (!page)
			return NULL;

		stat(s, ORDER_FALLBACK);
	}

	if (kmemcheck_enabled
		&& !(s->flags & (SLAB_NOTRACK | DEBUG_DEFAULT_FLAGS))) {
		int pages = 1 << oo_order(oo);

		kmemcheck_alloc_shadow(page, oo_order(oo), flags, node);

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
	mod_zone_page_state(page_zone(page),
		(s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE : NR_SLAB_UNRECLAIMABLE,
		1 << oo_order(oo));

	return page;
}

static void setup_object(struct kmem_cache *s, struct page *page,
				void *object)
{
	setup_object_debug(s, page, object);
	if (unlikely(s->ctor))
		s->ctor(object);
}

static struct page *new_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	struct page *page;
	void *start;
	void *last;
	void *p;

	BUG_ON(flags & GFP_SLAB_BUG_MASK);

	page = allocate_slab(s,
		flags & (GFP_RECLAIM_MASK | GFP_CONSTRAINT_MASK), node);
	if (!page)
		goto out;

	inc_slabs_node(s, page_to_nid(page), page->objects);
	page->slab = s;
	page->flags |= 1 << PG_slab;

	start = page_address(page);

	if (unlikely(s->flags & SLAB_POISON))
		memset(start, POISON_INUSE, PAGE_SIZE << compound_order(page));

	last = start;
	for_each_object(p, s, start, page->objects) {
		setup_object(s, page, last);
		set_freepointer(s, last, p);
		last = p;
	}
	setup_object(s, page, last);
	set_freepointer(s, last, NULL);

	page->freelist = start;
	page->inuse = 0;
out:
	return page;
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
			check_object(s, page, p, 0);
	}

	kmemcheck_free_shadow(page, compound_order(page));

	mod_zone_page_state(page_zone(page),
		(s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE : NR_SLAB_UNRECLAIMABLE,
		-pages);

	__ClearPageSlab(page);
	reset_page_mapcount(page);
	if (current->reclaim_state)
		current->reclaim_state->reclaimed_slab += pages;
	__free_pages(page, order);
}

static void rcu_free_slab(struct rcu_head *h)
{
	struct page *page;

	page = container_of((struct list_head *)h, struct page, lru);
	__free_slab(page->slab, page);
}

static void free_slab(struct kmem_cache *s, struct page *page)
{
	if (unlikely(s->flags & SLAB_DESTROY_BY_RCU)) {
		/*
		 * RCU free overloads the RCU head over the LRU
		 */
		struct rcu_head *head = (void *)&page->lru;

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

static __always_inline int slab_trylock(struct page *page)
{
	int rc = 1;

	rc = bit_spin_trylock(PG_locked, &page->flags);
	return rc;
}

/*
 * Management of partially allocated slabs
 */
static void add_partial(struct kmem_cache_node *n,
				struct page *page, int tail)
{
	spin_lock(&n->list_lock);
	n->nr_partial++;
	if (tail)
		list_add_tail(&page->lru, &n->partial);
	else
		list_add(&page->lru, &n->partial);
	spin_unlock(&n->list_lock);
}

static void remove_partial(struct kmem_cache *s, struct page *page)
{
	struct kmem_cache_node *n = get_node(s, page_to_nid(page));

	spin_lock(&n->list_lock);
	list_del(&page->lru);
	n->nr_partial--;
	spin_unlock(&n->list_lock);
}

/*
 * Lock slab and remove from the partial list.
 *
 * Must hold list_lock.
 */
static inline int lock_and_freeze_slab(struct kmem_cache_node *n,
							struct page *page)
{
	if (slab_trylock(page)) {
		list_del(&page->lru);
		n->nr_partial--;
		__SetPageSlubFrozen(page);
		return 1;
	}
	return 0;
}

/*
 * Try to allocate a partial slab from a specific node.
 */
static struct page *get_partial_node(struct kmem_cache_node *n)
{
	struct page *page;

	/*
	 * Racy check. If we mistakenly see no partial slabs then we
	 * just allocate an empty slab. If we mistakenly try to get a
	 * partial slab and there is none available then get_partials()
	 * will return NULL.
	 */
	if (!n || !n->nr_partial)
		return NULL;

	spin_lock(&n->list_lock);
	list_for_each_entry(page, &n->partial, lru)
		if (lock_and_freeze_slab(n, page))
			goto out;
	page = NULL;
out:
	spin_unlock(&n->list_lock);
	return page;
}

/*
 * Get a page from somewhere. Search in increasing NUMA distances.
 */
static struct page *get_any_partial(struct kmem_cache *s, gfp_t flags)
{
#ifdef CONFIG_NUMA
	struct zonelist *zonelist;
	struct zoneref *z;
	struct zone *zone;
	enum zone_type high_zoneidx = gfp_zone(flags);
	struct page *page;

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

	get_mems_allowed();
	zonelist = node_zonelist(slab_node(current->mempolicy), flags);
	for_each_zone_zonelist(zone, z, zonelist, high_zoneidx) {
		struct kmem_cache_node *n;

		n = get_node(s, zone_to_nid(zone));

		if (n && cpuset_zone_allowed_hardwall(zone, flags) &&
				n->nr_partial > s->min_partial) {
			page = get_partial_node(n);
			if (page) {
				put_mems_allowed();
				return page;
			}
		}
	}
	put_mems_allowed();
#endif
	return NULL;
}

/*
 * Get a partial page, lock it and return it.
 */
static struct page *get_partial(struct kmem_cache *s, gfp_t flags, int node)
{
	struct page *page;
	int searchnode = (node == NUMA_NO_NODE) ? numa_node_id() : node;

	page = get_partial_node(get_node(s, searchnode));
	if (page || node != -1)
		return page;

	return get_any_partial(s, flags);
}

/*
 * Move a page back to the lists.
 *
 * Must be called with the slab lock held.
 *
 * On exit the slab lock will have been dropped.
 */
static void unfreeze_slab(struct kmem_cache *s, struct page *page, int tail)
{
	struct kmem_cache_node *n = get_node(s, page_to_nid(page));

	__ClearPageSlubFrozen(page);
	if (page->inuse) {

		if (page->freelist) {
			add_partial(n, page, tail);
			stat(s, tail ? DEACTIVATE_TO_TAIL : DEACTIVATE_TO_HEAD);
		} else {
			stat(s, DEACTIVATE_FULL);
			if (kmem_cache_debug(s) && (s->flags & SLAB_STORE_USER))
				add_full(n, page);
		}
		slab_unlock(page);
	} else {
		stat(s, DEACTIVATE_EMPTY);
		if (n->nr_partial < s->min_partial) {
			/*
			 * Adding an empty slab to the partial slabs in order
			 * to avoid page allocator overhead. This slab needs
			 * to come after the other slabs with objects in
			 * so that the others get filled first. That way the
			 * size of the partial list stays small.
			 *
			 * kmem_cache_shrink can reclaim any empty slabs from
			 * the partial list.
			 */
			add_partial(n, page, 1);
			slab_unlock(page);
		} else {
			slab_unlock(page);
			stat(s, FREE_SLAB);
			discard_slab(s, page);
		}
	}
}

/*
 * Remove the cpu slab
 */
static void deactivate_slab(struct kmem_cache *s, struct kmem_cache_cpu *c)
{
	struct page *page = c->page;
	int tail = 1;

	if (page->freelist)
		stat(s, DEACTIVATE_REMOTE_FREES);
	/*
	 * Merge cpu freelist into slab freelist. Typically we get here
	 * because both freelists are empty. So this is unlikely
	 * to occur.
	 */
	while (unlikely(c->freelist)) {
		void **object;

		tail = 0;	/* Hot objects. Put the slab first */

		/* Retrieve object from cpu_freelist */
		object = c->freelist;
		c->freelist = get_freepointer(s, c->freelist);

		/* And put onto the regular freelist */
		set_freepointer(s, object, page->freelist);
		page->freelist = object;
		page->inuse--;
	}
	c->page = NULL;
	unfreeze_slab(s, page, tail);
}

static inline void flush_slab(struct kmem_cache *s, struct kmem_cache_cpu *c)
{
	stat(s, CPUSLAB_FLUSH);
	slab_lock(c->page);
	deactivate_slab(s, c);
}

/*
 * Flush cpu slab.
 *
 * Called from IPI handler with interrupts disabled.
 */
static inline void __flush_cpu_slab(struct kmem_cache *s, int cpu)
{
	struct kmem_cache_cpu *c = per_cpu_ptr(s->cpu_slab, cpu);

	if (likely(c && c->page))
		flush_slab(s, c);
}

static void flush_cpu_slab(void *d)
{
	struct kmem_cache *s = d;

	__flush_cpu_slab(s, smp_processor_id());
}

static void flush_all(struct kmem_cache *s)
{
	on_each_cpu(flush_cpu_slab, s, 1);
}

/*
 * Check if the objects in a per cpu structure fit numa
 * locality expectations.
 */
static inline int node_match(struct kmem_cache_cpu *c, int node)
{
#ifdef CONFIG_NUMA
	if (node != NUMA_NO_NODE && c->node != node)
		return 0;
#endif
	return 1;
}

static int count_free(struct page *page)
{
	return page->objects - page->inuse;
}

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

static inline unsigned long node_nr_objs(struct kmem_cache_node *n)
{
#ifdef CONFIG_SLUB_DEBUG
	return atomic_long_read(&n->total_objects);
#else
	return 0;
#endif
}

static noinline void
slab_out_of_memory(struct kmem_cache *s, gfp_t gfpflags, int nid)
{
	int node;

	printk(KERN_WARNING
		"SLUB: Unable to allocate memory on node %d (gfp=0x%x)\n",
		nid, gfpflags);
	printk(KERN_WARNING "  cache: %s, object size: %d, buffer size: %d, "
		"default order: %d, min order: %d\n", s->name, s->objsize,
		s->size, oo_order(s->oo), oo_order(s->min));

	if (oo_order(s->min) > get_order(s->objsize))
		printk(KERN_WARNING "  %s debugging increased min order, use "
		       "slub_debug=O to disable.\n", s->name);

	for_each_online_node(node) {
		struct kmem_cache_node *n = get_node(s, node);
		unsigned long nr_slabs;
		unsigned long nr_objs;
		unsigned long nr_free;

		if (!n)
			continue;

		nr_free  = count_partial(n, count_free);
		nr_slabs = node_nr_slabs(n);
		nr_objs  = node_nr_objs(n);

		printk(KERN_WARNING
			"  node %d: slabs: %ld, objs: %ld, free: %ld\n",
			node, nr_slabs, nr_objs, nr_free);
	}
}

/*
 * Slow path. The lockless freelist is empty or we need to perform
 * debugging duties.
 *
 * Interrupts are disabled.
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
 */
static void *__slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node,
			  unsigned long addr, struct kmem_cache_cpu *c)
{
	void **object;
	struct page *new;

	/* We handle __GFP_ZERO in the caller */
	gfpflags &= ~__GFP_ZERO;

	if (!c->page)
		goto new_slab;

	slab_lock(c->page);
	if (unlikely(!node_match(c, node)))
		goto another_slab;

	stat(s, ALLOC_REFILL);

load_freelist:
	object = c->page->freelist;
	if (unlikely(!object))
		goto another_slab;
	if (kmem_cache_debug(s))
		goto debug;

	c->freelist = get_freepointer(s, object);
	c->page->inuse = c->page->objects;
	c->page->freelist = NULL;
	c->node = page_to_nid(c->page);
unlock_out:
	slab_unlock(c->page);
	stat(s, ALLOC_SLOWPATH);
	return object;

another_slab:
	deactivate_slab(s, c);

new_slab:
	new = get_partial(s, gfpflags, node);
	if (new) {
		c->page = new;
		stat(s, ALLOC_FROM_PARTIAL);
		goto load_freelist;
	}

	if (gfpflags & __GFP_WAIT)
		local_irq_enable();

	new = new_slab(s, gfpflags, node);

	if (gfpflags & __GFP_WAIT)
		local_irq_disable();

	if (new) {
		c = __this_cpu_ptr(s->cpu_slab);
		stat(s, ALLOC_SLAB);
		if (c->page)
			flush_slab(s, c);
		slab_lock(new);
		__SetPageSlubFrozen(new);
		c->page = new;
		goto load_freelist;
	}
	if (!(gfpflags & __GFP_NOWARN) && printk_ratelimit())
		slab_out_of_memory(s, gfpflags, node);
	return NULL;
debug:
	if (!alloc_debug_processing(s, c->page, object, addr))
		goto another_slab;

	c->page->inuse++;
	c->page->freelist = get_freepointer(s, object);
	c->node = -1;
	goto unlock_out;
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
static __always_inline void *slab_alloc(struct kmem_cache *s,
		gfp_t gfpflags, int node, unsigned long addr)
{
	void **object;
	struct kmem_cache_cpu *c;
	unsigned long flags;

	gfpflags &= gfp_allowed_mask;

	if (slab_pre_alloc_hook(s, gfpflags))
		return NULL;

	local_irq_save(flags);
	c = __this_cpu_ptr(s->cpu_slab);
	object = c->freelist;
	if (unlikely(!object || !node_match(c, node)))

		object = __slab_alloc(s, gfpflags, node, addr, c);

	else {
		c->freelist = get_freepointer(s, object);
		stat(s, ALLOC_FASTPATH);
	}
	local_irq_restore(flags);

	if (unlikely(gfpflags & __GFP_ZERO) && object)
		memset(object, 0, s->objsize);

	slab_post_alloc_hook(s, gfpflags, object);

	return object;
}

void *kmem_cache_alloc(struct kmem_cache *s, gfp_t gfpflags)
{
	void *ret = slab_alloc(s, gfpflags, NUMA_NO_NODE, _RET_IP_);

	trace_kmem_cache_alloc(_RET_IP_, ret, s->objsize, s->size, gfpflags);

	return ret;
}
EXPORT_SYMBOL(kmem_cache_alloc);

#ifdef CONFIG_TRACING
void *kmem_cache_alloc_notrace(struct kmem_cache *s, gfp_t gfpflags)
{
	return slab_alloc(s, gfpflags, NUMA_NO_NODE, _RET_IP_);
}
EXPORT_SYMBOL(kmem_cache_alloc_notrace);
#endif

#ifdef CONFIG_NUMA
void *kmem_cache_alloc_node(struct kmem_cache *s, gfp_t gfpflags, int node)
{
	void *ret = slab_alloc(s, gfpflags, node, _RET_IP_);

	trace_kmem_cache_alloc_node(_RET_IP_, ret,
				    s->objsize, s->size, gfpflags, node);

	return ret;
}
EXPORT_SYMBOL(kmem_cache_alloc_node);
#endif

#ifdef CONFIG_TRACING
void *kmem_cache_alloc_node_notrace(struct kmem_cache *s,
				    gfp_t gfpflags,
				    int node)
{
	return slab_alloc(s, gfpflags, node, _RET_IP_);
}
EXPORT_SYMBOL(kmem_cache_alloc_node_notrace);
#endif

/*
 * Slow patch handling. This may still be called frequently since objects
 * have a longer lifetime than the cpu slabs in most processing loads.
 *
 * So we still attempt to reduce cache line usage. Just take the slab
 * lock and free the item. If there is no additional partial page
 * handling required then we can return immediately.
 */
static void __slab_free(struct kmem_cache *s, struct page *page,
			void *x, unsigned long addr)
{
	void *prior;
	void **object = (void *)x;

	stat(s, FREE_SLOWPATH);
	slab_lock(page);

	if (kmem_cache_debug(s))
		goto debug;

checks_ok:
	prior = page->freelist;
	set_freepointer(s, object, prior);
	page->freelist = object;
	page->inuse--;

	if (unlikely(PageSlubFrozen(page))) {
		stat(s, FREE_FROZEN);
		goto out_unlock;
	}

	if (unlikely(!page->inuse))
		goto slab_empty;

	/*
	 * Objects left in the slab. If it was not on the partial list before
	 * then add it.
	 */
	if (unlikely(!prior)) {
		add_partial(get_node(s, page_to_nid(page)), page, 1);
		stat(s, FREE_ADD_PARTIAL);
	}

out_unlock:
	slab_unlock(page);
	return;

slab_empty:
	if (prior) {
		/*
		 * Slab still on the partial list.
		 */
		remove_partial(s, page);
		stat(s, FREE_REMOVE_PARTIAL);
	}
	slab_unlock(page);
	stat(s, FREE_SLAB);
	discard_slab(s, page);
	return;

debug:
	if (!free_debug_processing(s, page, x, addr))
		goto out_unlock;
	goto checks_ok;
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
 */
static __always_inline void slab_free(struct kmem_cache *s,
			struct page *page, void *x, unsigned long addr)
{
	void **object = (void *)x;
	struct kmem_cache_cpu *c;
	unsigned long flags;

	slab_free_hook(s, x);

	local_irq_save(flags);
	c = __this_cpu_ptr(s->cpu_slab);

	slab_free_hook_irq(s, x);

	if (likely(page == c->page && c->node >= 0)) {
		set_freepointer(s, object, c->freelist);
		c->freelist = object;
		stat(s, FREE_FASTPATH);
	} else
		__slab_free(s, page, x, addr);

	local_irq_restore(flags);
}

void kmem_cache_free(struct kmem_cache *s, void *x)
{
	struct page *page;

	page = virt_to_head_page(x);

	slab_free(s, page, x, _RET_IP_);

	trace_kmem_cache_free(_RET_IP_, x);
}
EXPORT_SYMBOL(kmem_cache_free);

/* Figure out on which slab page the object resides */
static struct page *get_object_page(const void *x)
{
	struct page *page = virt_to_head_page(x);

	if (!PageSlab(page))
		return NULL;

	return page;
}

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
 * Merge control. If this is set then no merging of slab caches will occur.
 * (Could be removed. This was introduced to pacify the merge skeptics.)
 */
static int slub_nomerge;

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
				int max_order, int fract_leftover)
{
	int order;
	int rem;
	int min_order = slub_min_order;

	if ((PAGE_SIZE << min_order) / size > MAX_OBJS_PER_PAGE)
		return get_order(size * MAX_OBJS_PER_PAGE) - 1;

	for (order = max(min_order,
				fls(min_objects * size - 1) - PAGE_SHIFT);
			order <= max_order; order++) {

		unsigned long slab_size = PAGE_SIZE << order;

		if (slab_size < min_objects * size)
			continue;

		rem = slab_size % size;

		if (rem <= slab_size / fract_leftover)
			break;

	}

	return order;
}

static inline int calculate_order(int size)
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
	 * First we reduce the acceptable waste in a slab. Then
	 * we reduce the minimum objects required in a slab.
	 */
	min_objects = slub_min_objects;
	if (!min_objects)
		min_objects = 4 * (fls(nr_cpu_ids) + 1);
	max_objects = (PAGE_SIZE << slub_max_order)/size;
	min_objects = min(min_objects, max_objects);

	while (min_objects > 1) {
		fraction = 16;
		while (fraction >= 4) {
			order = slab_order(size, min_objects,
						slub_max_order, fraction);
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
	order = slab_order(size, 1, slub_max_order, 1);
	if (order <= slub_max_order)
		return order;

	/*
	 * Doh this slab cannot be placed using slub_max_order.
	 */
	order = slab_order(size, 1, MAX_ORDER, 1);
	if (order < MAX_ORDER)
		return order;
	return -ENOSYS;
}

/*
 * Figure out what the alignment of the objects will be.
 */
static unsigned long calculate_alignment(unsigned long flags,
		unsigned long align, unsigned long size)
{
	/*
	 * If the user wants hardware cache aligned objects then follow that
	 * suggestion if the object is sufficiently large.
	 *
	 * The hardware cache alignment cannot override the specified
	 * alignment though. If that is greater then use it.
	 */
	if (flags & SLAB_HWCACHE_ALIGN) {
		unsigned long ralign = cache_line_size();
		while (size <= ralign / 2)
			ralign /= 2;
		align = max(align, ralign);
	}

	if (align < ARCH_SLAB_MINALIGN)
		align = ARCH_SLAB_MINALIGN;

	return ALIGN(align, sizeof(void *));
}

static void
init_kmem_cache_node(struct kmem_cache_node *n, struct kmem_cache *s)
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
			SLUB_PAGE_SHIFT * sizeof(struct kmem_cache_cpu));

	s->cpu_slab = alloc_percpu(struct kmem_cache_cpu);

	return s->cpu_slab != NULL;
}

#ifdef CONFIG_NUMA
static struct kmem_cache *kmem_cache_node;

/*
 * No kmalloc_node yet so do it by hand. We know that this is the first
 * slab on the node for this slabcache. There are no concurrent accesses
 * possible.
 *
 * Note that this function only works on the kmalloc_node_cache
 * when allocating for the kmalloc_node_cache. This is used for bootstrapping
 * memory on a fresh node that has no slab structures yet.
 */
static void early_kmem_cache_node_alloc(int node)
{
	struct page *page;
	struct kmem_cache_node *n;
	unsigned long flags;

	BUG_ON(kmem_cache_node->size < sizeof(struct kmem_cache_node));

	page = new_slab(kmem_cache_node, GFP_NOWAIT, node);

	BUG_ON(!page);
	if (page_to_nid(page) != node) {
		printk(KERN_ERR "SLUB: Unable to allocate memory from "
				"node %d\n", node);
		printk(KERN_ERR "SLUB: Allocating a useless per node structure "
				"in order to be able to continue\n");
	}

	n = page->freelist;
	BUG_ON(!n);
	page->freelist = get_freepointer(kmem_cache_node, n);
	page->inuse++;
	kmem_cache_node->node[node] = n;
#ifdef CONFIG_SLUB_DEBUG
	init_object(kmem_cache_node, n, 1);
	init_tracking(kmem_cache_node, n);
#endif
	init_kmem_cache_node(n, kmem_cache_node);
	inc_slabs_node(kmem_cache_node, node, page->objects);

	/*
	 * lockdep requires consistent irq usage for each lock
	 * so even though there cannot be a race this early in
	 * the boot sequence, we still disable irqs.
	 */
	local_irq_save(flags);
	add_partial(n, page, 0);
	local_irq_restore(flags);
}

static void free_kmem_cache_nodes(struct kmem_cache *s)
{
	int node;

	for_each_node_state(node, N_NORMAL_MEMORY) {
		struct kmem_cache_node *n = s->node[node];

		if (n)
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
		init_kmem_cache_node(n, s);
	}
	return 1;
}
#else
static void free_kmem_cache_nodes(struct kmem_cache *s)
{
}

static int init_kmem_cache_nodes(struct kmem_cache *s)
{
	init_kmem_cache_node(&s->local_node, s);
	return 1;
}
#endif

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
	unsigned long size = s->objsize;
	unsigned long align = s->align;
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
	if ((flags & SLAB_RED_ZONE) && size == s->objsize)
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
	 * Determine the alignment based on various parameters that the
	 * user specified and the dynamic determination of cache line size
	 * on bootup.
	 */
	align = calculate_alignment(flags, align, s->objsize);
	s->align = align;

	/*
	 * SLUB stores one object immediately after another beginning from
	 * offset 0. In order to align the objects we have to simply size
	 * each object to conform to the alignment.
	 */
	size = ALIGN(size, align);
	s->size = size;
	if (forced_order >= 0)
		order = forced_order;
	else
		order = calculate_order(size);

	if (order < 0)
		return 0;

	s->allocflags = 0;
	if (order)
		s->allocflags |= __GFP_COMP;

	if (s->flags & SLAB_CACHE_DMA)
		s->allocflags |= SLUB_DMA;

	if (s->flags & SLAB_RECLAIM_ACCOUNT)
		s->allocflags |= __GFP_RECLAIMABLE;

	/*
	 * Determine the number of objects per slab
	 */
	s->oo = oo_make(order, size);
	s->min = oo_make(get_order(size), size);
	if (oo_objects(s->oo) > oo_objects(s->max))
		s->max = s->oo;

	return !!oo_objects(s->oo);

}

static int kmem_cache_open(struct kmem_cache *s,
		const char *name, size_t size,
		size_t align, unsigned long flags,
		void (*ctor)(void *))
{
	memset(s, 0, kmem_size);
	s->name = name;
	s->ctor = ctor;
	s->objsize = size;
	s->align = align;
	s->flags = kmem_cache_flags(size, flags, name, ctor);

	if (!calculate_sizes(s, -1))
		goto error;
	if (disable_higher_order_debug) {
		/*
		 * Disable debugging flags that store metadata if the min slab
		 * order increased.
		 */
		if (get_order(s->size) > get_order(s->objsize)) {
			s->flags &= ~DEBUG_METADATA_FLAGS;
			s->offset = 0;
			if (!calculate_sizes(s, -1))
				goto error;
		}
	}

	/*
	 * The larger the object size is, the more pages we want on the partial
	 * list to avoid pounding the page allocator excessively.
	 */
	set_min_partial(s, ilog2(s->size));
	s->refcount = 1;
#ifdef CONFIG_NUMA
	s->remote_node_defrag_ratio = 1000;
#endif
	if (!init_kmem_cache_nodes(s))
		goto error;

	if (alloc_kmem_cache_cpus(s))
		return 1;

	free_kmem_cache_nodes(s);
error:
	if (flags & SLAB_PANIC)
		panic("Cannot create slab %s size=%lu realsize=%u "
			"order=%u offset=%u flags=%lx\n",
			s->name, (unsigned long)size, s->size, oo_order(s->oo),
			s->offset, flags);
	return 0;
}

/*
 * Check if a given pointer is valid
 */
int kmem_ptr_validate(struct kmem_cache *s, const void *object)
{
	struct page *page;

	if (!kern_ptr_validate(object, s->size))
		return 0;

	page = get_object_page(object);

	if (!page || s != page->slab)
		/* No slab or wrong slab */
		return 0;

	if (!check_valid_pointer(s, page, object))
		return 0;

	/*
	 * We could also check if the object is on the slabs freelist.
	 * But this would be too expensive and it seems that the main
	 * purpose of kmem_ptr_valid() is to check if the object belongs
	 * to a certain slab.
	 */
	return 1;
}
EXPORT_SYMBOL(kmem_ptr_validate);

/*
 * Determine the size of a slab object
 */
unsigned int kmem_cache_size(struct kmem_cache *s)
{
	return s->objsize;
}
EXPORT_SYMBOL(kmem_cache_size);

const char *kmem_cache_name(struct kmem_cache *s)
{
	return s->name;
}
EXPORT_SYMBOL(kmem_cache_name);

static void list_slab_objects(struct kmem_cache *s, struct page *page,
							const char *text)
{
#ifdef CONFIG_SLUB_DEBUG
	void *addr = page_address(page);
	void *p;
	long *map = kzalloc(BITS_TO_LONGS(page->objects) * sizeof(long),
			    GFP_ATOMIC);

	if (!map)
		return;
	slab_err(s, page, "%s", text);
	slab_lock(page);
	for_each_free_object(p, s, page->freelist)
		set_bit(slab_index(p, s, addr), map);

	for_each_object(p, s, addr, page->objects) {

		if (!test_bit(slab_index(p, s, addr), map)) {
			printk(KERN_ERR "INFO: Object 0x%p @offset=%tu\n",
							p, p - addr);
			print_tracking(s, p);
		}
	}
	slab_unlock(page);
	kfree(map);
#endif
}

/*
 * Attempt to free all partial slabs on a node.
 */
static void free_partial(struct kmem_cache *s, struct kmem_cache_node *n)
{
	unsigned long flags;
	struct page *page, *h;

	spin_lock_irqsave(&n->list_lock, flags);
	list_for_each_entry_safe(page, h, &n->partial, lru) {
		if (!page->inuse) {
			list_del(&page->lru);
			discard_slab(s, page);
			n->nr_partial--;
		} else {
			list_slab_objects(s, page,
				"Objects remaining on kmem_cache_close()");
		}
	}
	spin_unlock_irqrestore(&n->list_lock, flags);
}

/*
 * Release all resources used by a slab cache.
 */
static inline int kmem_cache_close(struct kmem_cache *s)
{
	int node;

	flush_all(s);
	free_percpu(s->cpu_slab);
	/* Attempt to free all objects */
	for_each_node_state(node, N_NORMAL_MEMORY) {
		struct kmem_cache_node *n = get_node(s, node);

		free_partial(s, n);
		if (n->nr_partial || slabs_node(s, node))
			return 1;
	}
	free_kmem_cache_nodes(s);
	return 0;
}

/*
 * Close a cache and release the kmem_cache structure
 * (must be used for caches created using kmem_cache_create)
 */
void kmem_cache_destroy(struct kmem_cache *s)
{
	down_write(&slub_lock);
	s->refcount--;
	if (!s->refcount) {
		list_del(&s->list);
		if (kmem_cache_close(s)) {
			printk(KERN_ERR "SLUB %s: %s called for cache that "
				"still has objects.\n", s->name, __func__);
			dump_stack();
		}
		if (s->flags & SLAB_DESTROY_BY_RCU)
			rcu_barrier();
		sysfs_slab_remove(s);
	}
	up_write(&slub_lock);
}
EXPORT_SYMBOL(kmem_cache_destroy);

/********************************************************************
 *		Kmalloc subsystem
 *******************************************************************/

struct kmem_cache *kmalloc_caches[SLUB_PAGE_SHIFT];
EXPORT_SYMBOL(kmalloc_caches);

static struct kmem_cache *kmem_cache;

#ifdef CONFIG_ZONE_DMA
static struct kmem_cache *kmalloc_dma_caches[SLUB_PAGE_SHIFT];
#endif

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

static int __init setup_slub_nomerge(char *str)
{
	slub_nomerge = 1;
	return 1;
}

__setup("slub_nomerge", setup_slub_nomerge);

static struct kmem_cache *__init create_kmalloc_cache(const char *name,
						int size, unsigned int flags)
{
	struct kmem_cache *s;

	s = kmem_cache_alloc(kmem_cache, GFP_NOWAIT);

	/*
	 * This function is called with IRQs disabled during early-boot on
	 * single CPU so there's no need to take slub_lock here.
	 */
	if (!kmem_cache_open(s, name, size, ARCH_KMALLOC_MINALIGN,
								flags, NULL))
		goto panic;

	list_add(&s->list, &slab_caches);
	return s;

panic:
	panic("Creation of kmalloc slab %s size=%d failed.\n", name, size);
	return NULL;
}

/*
 * Conversion table for small slabs sizes / 8 to the index in the
 * kmalloc array. This is necessary for slabs < 192 since we have non power
 * of two cache sizes there. The size of larger slabs can be determined using
 * fls.
 */
static s8 size_index[24] = {
	3,	/* 8 */
	4,	/* 16 */
	5,	/* 24 */
	5,	/* 32 */
	6,	/* 40 */
	6,	/* 48 */
	6,	/* 56 */
	6,	/* 64 */
	1,	/* 72 */
	1,	/* 80 */
	1,	/* 88 */
	1,	/* 96 */
	7,	/* 104 */
	7,	/* 112 */
	7,	/* 120 */
	7,	/* 128 */
	2,	/* 136 */
	2,	/* 144 */
	2,	/* 152 */
	2,	/* 160 */
	2,	/* 168 */
	2,	/* 176 */
	2,	/* 184 */
	2	/* 192 */
};

static inline int size_index_elem(size_t bytes)
{
	return (bytes - 1) / 8;
}

static struct kmem_cache *get_slab(size_t size, gfp_t flags)
{
	int index;

	if (size <= 192) {
		if (!size)
			return ZERO_SIZE_PTR;

		index = size_index[size_index_elem(size)];
	} else
		index = fls(size - 1);

#ifdef CONFIG_ZONE_DMA
	if (unlikely((flags & SLUB_DMA)))
		return kmalloc_dma_caches[index];

#endif
	return kmalloc_caches[index];
}

void *__kmalloc(size_t size, gfp_t flags)
{
	struct kmem_cache *s;
	void *ret;

	if (unlikely(size > SLUB_MAX_SIZE))
		return kmalloc_large(size, flags);

	s = get_slab(size, flags);

	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	ret = slab_alloc(s, flags, NUMA_NO_NODE, _RET_IP_);

	trace_kmalloc(_RET_IP_, ret, size, s->size, flags);

	return ret;
}
EXPORT_SYMBOL(__kmalloc);

static void *kmalloc_large_node(size_t size, gfp_t flags, int node)
{
	struct page *page;
	void *ptr = NULL;

	flags |= __GFP_COMP | __GFP_NOTRACK;
	page = alloc_pages_node(node, flags, get_order(size));
	if (page)
		ptr = page_address(page);

	kmemleak_alloc(ptr, size, 1, flags);
	return ptr;
}

#ifdef CONFIG_NUMA
void *__kmalloc_node(size_t size, gfp_t flags, int node)
{
	struct kmem_cache *s;
	void *ret;

	if (unlikely(size > SLUB_MAX_SIZE)) {
		ret = kmalloc_large_node(size, flags, node);

		trace_kmalloc_node(_RET_IP_, ret,
				   size, PAGE_SIZE << get_order(size),
				   flags, node);

		return ret;
	}

	s = get_slab(size, flags);

	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	ret = slab_alloc(s, flags, node, _RET_IP_);

	trace_kmalloc_node(_RET_IP_, ret, size, s->size, flags, node);

	return ret;
}
EXPORT_SYMBOL(__kmalloc_node);
#endif

size_t ksize(const void *object)
{
	struct page *page;
	struct kmem_cache *s;

	if (unlikely(object == ZERO_SIZE_PTR))
		return 0;

	page = virt_to_head_page(object);

	if (unlikely(!PageSlab(page))) {
		WARN_ON(!PageCompound(page));
		return PAGE_SIZE << compound_order(page);
	}
	s = page->slab;

#ifdef CONFIG_SLUB_DEBUG
	/*
	 * Debugging requires use of the padding between object
	 * and whatever may come after it.
	 */
	if (s->flags & (SLAB_RED_ZONE | SLAB_POISON))
		return s->objsize;

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
		kmemleak_free(x);
		put_page(page);
		return;
	}
	slab_free(page->slab, page, object, _RET_IP_);
}
EXPORT_SYMBOL(kfree);

/*
 * kmem_cache_shrink removes empty slabs from the partial lists and sorts
 * the remaining slabs by the number of items in use. The slabs with the
 * most items in use come first. New allocations will then fill those up
 * and thus they can be removed from the partial lists.
 *
 * The slabs with the least items are placed last. This results in them
 * being allocated from last increasing the chance that the last objects
 * are freed in them.
 */
int kmem_cache_shrink(struct kmem_cache *s)
{
	int node;
	int i;
	struct kmem_cache_node *n;
	struct page *page;
	struct page *t;
	int objects = oo_objects(s->max);
	struct list_head *slabs_by_inuse =
		kmalloc(sizeof(struct list_head) * objects, GFP_KERNEL);
	unsigned long flags;

	if (!slabs_by_inuse)
		return -ENOMEM;

	flush_all(s);
	for_each_node_state(node, N_NORMAL_MEMORY) {
		n = get_node(s, node);

		if (!n->nr_partial)
			continue;

		for (i = 0; i < objects; i++)
			INIT_LIST_HEAD(slabs_by_inuse + i);

		spin_lock_irqsave(&n->list_lock, flags);

		/*
		 * Build lists indexed by the items in use in each slab.
		 *
		 * Note that concurrent frees may occur while we hold the
		 * list_lock. page->inuse here is the upper limit.
		 */
		list_for_each_entry_safe(page, t, &n->partial, lru) {
			if (!page->inuse && slab_trylock(page)) {
				/*
				 * Must hold slab lock here because slab_free
				 * may have freed the last object and be
				 * waiting to release the slab.
				 */
				list_del(&page->lru);
				n->nr_partial--;
				slab_unlock(page);
				discard_slab(s, page);
			} else {
				list_move(&page->lru,
				slabs_by_inuse + page->inuse);
			}
		}

		/*
		 * Rebuild the partial list with the slabs filled up most
		 * first and the least used slabs at the end.
		 */
		for (i = objects - 1; i >= 0; i--)
			list_splice(slabs_by_inuse + i, n->partial.prev);

		spin_unlock_irqrestore(&n->list_lock, flags);
	}

	kfree(slabs_by_inuse);
	return 0;
}
EXPORT_SYMBOL(kmem_cache_shrink);

#if defined(CONFIG_NUMA) && defined(CONFIG_MEMORY_HOTPLUG)
static int slab_mem_going_offline_callback(void *arg)
{
	struct kmem_cache *s;

	down_read(&slub_lock);
	list_for_each_entry(s, &slab_caches, list)
		kmem_cache_shrink(s);
	up_read(&slub_lock);

	return 0;
}

static void slab_mem_offline_callback(void *arg)
{
	struct kmem_cache_node *n;
	struct kmem_cache *s;
	struct memory_notify *marg = arg;
	int offline_node;

	offline_node = marg->status_change_nid;

	/*
	 * If the node still has available memory. we need kmem_cache_node
	 * for it yet.
	 */
	if (offline_node < 0)
		return;

	down_read(&slub_lock);
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
			kmem_cache_free(kmalloc_caches, n);
		}
	}
	up_read(&slub_lock);
}

static int slab_mem_going_online_callback(void *arg)
{
	struct kmem_cache_node *n;
	struct kmem_cache *s;
	struct memory_notify *marg = arg;
	int nid = marg->status_change_nid;
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
	down_read(&slub_lock);
	list_for_each_entry(s, &slab_caches, list) {
		/*
		 * XXX: kmem_cache_alloc_node will fallback to other nodes
		 *      since memory is not yet available from the node that
		 *      is brought up.
		 */
		n = kmem_cache_alloc(kmalloc_caches, GFP_KERNEL);
		if (!n) {
			ret = -ENOMEM;
			goto out;
		}
		init_kmem_cache_node(n, s);
		s->node[nid] = n;
	}
out:
	up_read(&slub_lock);
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

#endif /* CONFIG_MEMORY_HOTPLUG */

/********************************************************************
 *			Basic setup of slabs
 *******************************************************************/

/*
 * Used for early kmem_cache structures that were allocated using
 * the page allocator
 */

static void __init kmem_cache_bootstrap_fixup(struct kmem_cache *s)
{
	int node;

	list_add(&s->list, &slab_caches);
	s->refcount = -1;

	for_each_node_state(node, N_NORMAL_MEMORY) {
		struct kmem_cache_node *n = get_node(s, node);
		struct page *p;

		if (n) {
			list_for_each_entry(p, &n->partial, lru)
				p->slab = s;

#ifdef CONFIG_SLAB_DEBUG
			list_for_each_entry(p, &n->full, lru)
				p->slab = s;
#endif
		}
	}
}

void __init kmem_cache_init(void)
{
	int i;
	int caches = 0;
	struct kmem_cache *temp_kmem_cache;
	int order;

#ifdef CONFIG_NUMA
	struct kmem_cache *temp_kmem_cache_node;
	unsigned long kmalloc_size;

	kmem_size = offsetof(struct kmem_cache, node) +
				nr_node_ids * sizeof(struct kmem_cache_node *);

	/* Allocate two kmem_caches from the page allocator */
	kmalloc_size = ALIGN(kmem_size, cache_line_size());
	order = get_order(2 * kmalloc_size);
	kmem_cache = (void *)__get_free_pages(GFP_NOWAIT, order);

	/*
	 * Must first have the slab cache available for the allocations of the
	 * struct kmem_cache_node's. There is special bootstrap code in
	 * kmem_cache_open for slab_state == DOWN.
	 */
	kmem_cache_node = (void *)kmem_cache + kmalloc_size;

	kmem_cache_open(kmem_cache_node, "kmem_cache_node",
		sizeof(struct kmem_cache_node),
		0, SLAB_HWCACHE_ALIGN | SLAB_PANIC, NULL);

	hotplug_memory_notifier(slab_memory_callback, SLAB_CALLBACK_PRI);
#else
	/* Allocate a single kmem_cache from the page allocator */
	kmem_size = sizeof(struct kmem_cache);
	order = get_order(kmem_size);
	kmem_cache = (void *)__get_free_pages(GFP_NOWAIT, order);
#endif

	/* Able to allocate the per node structures */
	slab_state = PARTIAL;

	temp_kmem_cache = kmem_cache;
	kmem_cache_open(kmem_cache, "kmem_cache", kmem_size,
		0, SLAB_HWCACHE_ALIGN | SLAB_PANIC, NULL);
	kmem_cache = kmem_cache_alloc(kmem_cache, GFP_NOWAIT);
	memcpy(kmem_cache, temp_kmem_cache, kmem_size);

#ifdef CONFIG_NUMA
	/*
	 * Allocate kmem_cache_node properly from the kmem_cache slab.
	 * kmem_cache_node is separately allocated so no need to
	 * update any list pointers.
	 */
	temp_kmem_cache_node = kmem_cache_node;

	kmem_cache_node = kmem_cache_alloc(kmem_cache, GFP_NOWAIT);
	memcpy(kmem_cache_node, temp_kmem_cache_node, kmem_size);

	kmem_cache_bootstrap_fixup(kmem_cache_node);

	caches++;
#else
	/*
	 * kmem_cache has kmem_cache_node embedded and we moved it!
	 * Update the list heads
	 */
	INIT_LIST_HEAD(&kmem_cache->local_node.partial);
	list_splice(&temp_kmem_cache->local_node.partial, &kmem_cache->local_node.partial);
#ifdef CONFIG_SLUB_DEBUG
	INIT_LIST_HEAD(&kmem_cache->local_node.full);
	list_splice(&temp_kmem_cache->local_node.full, &kmem_cache->local_node.full);
#endif
#endif
	kmem_cache_bootstrap_fixup(kmem_cache);
	caches++;
	/* Free temporary boot structure */
	free_pages((unsigned long)temp_kmem_cache, order);

	/* Now we can use the kmem_cache to allocate kmalloc slabs */

	/*
	 * Patch up the size_index table if we have strange large alignment
	 * requirements for the kmalloc array. This is only the case for
	 * MIPS it seems. The standard arches will not generate any code here.
	 *
	 * Largest permitted alignment is 256 bytes due to the way we
	 * handle the index determination for the smaller caches.
	 *
	 * Make sure that nothing crazy happens if someone starts tinkering
	 * around with ARCH_KMALLOC_MINALIGN
	 */
	BUILD_BUG_ON(KMALLOC_MIN_SIZE > 256 ||
		(KMALLOC_MIN_SIZE & (KMALLOC_MIN_SIZE - 1)));

	for (i = 8; i < KMALLOC_MIN_SIZE; i += 8) {
		int elem = size_index_elem(i);
		if (elem >= ARRAY_SIZE(size_index))
			break;
		size_index[elem] = KMALLOC_SHIFT_LOW;
	}

	if (KMALLOC_MIN_SIZE == 64) {
		/*
		 * The 96 byte size cache is not used if the alignment
		 * is 64 byte.
		 */
		for (i = 64 + 8; i <= 96; i += 8)
			size_index[size_index_elem(i)] = 7;
	} else if (KMALLOC_MIN_SIZE == 128) {
		/*
		 * The 192 byte sized cache is not used if the alignment
		 * is 128 byte. Redirect kmalloc to use the 256 byte cache
		 * instead.
		 */
		for (i = 128 + 8; i <= 192; i += 8)
			size_index[size_index_elem(i)] = 8;
	}

	/* Caches that are not of the two-to-the-power-of size */
	if (KMALLOC_MIN_SIZE <= 32) {
		kmalloc_caches[1] = create_kmalloc_cache("kmalloc-96", 96, 0);
		caches++;
	}

	if (KMALLOC_MIN_SIZE <= 64) {
		kmalloc_caches[2] = create_kmalloc_cache("kmalloc-192", 192, 0);
		caches++;
	}

	for (i = KMALLOC_SHIFT_LOW; i < SLUB_PAGE_SHIFT; i++) {
		kmalloc_caches[i] = create_kmalloc_cache("kmalloc", 1 << i, 0);
		caches++;
	}

	slab_state = UP;

	/* Provide the correct kmalloc names now that the caches are up */
	for (i = KMALLOC_SHIFT_LOW; i < SLUB_PAGE_SHIFT; i++) {
		char *s = kasprintf(GFP_NOWAIT, "kmalloc-%d", 1 << i);

		BUG_ON(!s);
		kmalloc_caches[i]->name = s;
	}

#ifdef CONFIG_SMP
	register_cpu_notifier(&slab_notifier);
#endif

#ifdef CONFIG_ZONE_DMA
	for (i = 0; i < SLUB_PAGE_SHIFT; i++) {
		struct kmem_cache *s = kmalloc_caches[i];

		if (s && s->size) {
			char *name = kasprintf(GFP_NOWAIT,
				 "dma-kmalloc-%d", s->objsize);

			BUG_ON(!name);
			kmalloc_dma_caches[i] = create_kmalloc_cache(name,
				s->objsize, SLAB_CACHE_DMA);
		}
	}
#endif
	printk(KERN_INFO
		"SLUB: Genslabs=%d, HWalign=%d, Order=%d-%d, MinObjects=%d,"
		" CPUs=%d, Nodes=%d\n",
		caches, cache_line_size(),
		slub_min_order, slub_max_order, slub_min_objects,
		nr_cpu_ids, nr_node_ids);
}

void __init kmem_cache_init_late(void)
{
}

/*
 * Find a mergeable slab cache
 */
static int slab_unmergeable(struct kmem_cache *s)
{
	if (slub_nomerge || (s->flags & SLUB_NEVER_MERGE))
		return 1;

	if (s->ctor)
		return 1;

	/*
	 * We may have set a slab to be unmergeable during bootstrap.
	 */
	if (s->refcount < 0)
		return 1;

	return 0;
}

static struct kmem_cache *find_mergeable(size_t size,
		size_t align, unsigned long flags, const char *name,
		void (*ctor)(void *))
{
	struct kmem_cache *s;

	if (slub_nomerge || (flags & SLUB_NEVER_MERGE))
		return NULL;

	if (ctor)
		return NULL;

	size = ALIGN(size, sizeof(void *));
	align = calculate_alignment(flags, align, size);
	size = ALIGN(size, align);
	flags = kmem_cache_flags(size, flags, name, NULL);

	list_for_each_entry(s, &slab_caches, list) {
		if (slab_unmergeable(s))
			continue;

		if (size > s->size)
			continue;

		if ((flags & SLUB_MERGE_SAME) != (s->flags & SLUB_MERGE_SAME))
				continue;
		/*
		 * Check if alignment is compatible.
		 * Courtesy of Adrian Drzewiecki
		 */
		if ((s->size & ~(align - 1)) != s->size)
			continue;

		if (s->size - size >= sizeof(void *))
			continue;

		return s;
	}
	return NULL;
}

struct kmem_cache *kmem_cache_create(const char *name, size_t size,
		size_t align, unsigned long flags, void (*ctor)(void *))
{
	struct kmem_cache *s;

	if (WARN_ON(!name))
		return NULL;

	down_write(&slub_lock);
	s = find_mergeable(size, align, flags, name, ctor);
	if (s) {
		s->refcount++;
		/*
		 * Adjust the object sizes so that we clear
		 * the complete object on kzalloc.
		 */
		s->objsize = max(s->objsize, (int)size);
		s->inuse = max_t(int, s->inuse, ALIGN(size, sizeof(void *)));

		if (sysfs_slab_alias(s, name)) {
			s->refcount--;
			goto err;
		}
		up_write(&slub_lock);
		return s;
	}

	s = kmalloc(kmem_size, GFP_KERNEL);
	if (s) {
		if (kmem_cache_open(s, name,
				size, align, flags, ctor)) {
			list_add(&s->list, &slab_caches);
			if (sysfs_slab_add(s)) {
				list_del(&s->list);
				kfree(s);
				goto err;
			}
			up_write(&slub_lock);
			return s;
		}
		kfree(s);
	}
	up_write(&slub_lock);

err:
	if (flags & SLAB_PANIC)
		panic("Cannot create slabcache %s\n", name);
	else
		s = NULL;
	return s;
}
EXPORT_SYMBOL(kmem_cache_create);

#ifdef CONFIG_SMP
/*
 * Use the cpu notifier to insure that the cpu slabs are flushed when
 * necessary.
 */
static int __cpuinit slab_cpuup_callback(struct notifier_block *nfb,
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
		down_read(&slub_lock);
		list_for_each_entry(s, &slab_caches, list) {
			local_irq_save(flags);
			__flush_cpu_slab(s, cpu);
			local_irq_restore(flags);
		}
		up_read(&slub_lock);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata slab_notifier = {
	.notifier_call = slab_cpuup_callback
};

#endif

void *__kmalloc_track_caller(size_t size, gfp_t gfpflags, unsigned long caller)
{
	struct kmem_cache *s;
	void *ret;

	if (unlikely(size > SLUB_MAX_SIZE))
		return kmalloc_large(size, gfpflags);

	s = get_slab(size, gfpflags);

	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	ret = slab_alloc(s, gfpflags, NUMA_NO_NODE, caller);

	/* Honor the call site pointer we recieved. */
	trace_kmalloc(caller, ret, size, s->size, gfpflags);

	return ret;
}

void *__kmalloc_node_track_caller(size_t size, gfp_t gfpflags,
					int node, unsigned long caller)
{
	struct kmem_cache *s;
	void *ret;

	if (unlikely(size > SLUB_MAX_SIZE)) {
		ret = kmalloc_large_node(size, gfpflags, node);

		trace_kmalloc_node(caller, ret,
				   size, PAGE_SIZE << get_order(size),
				   gfpflags, node);

		return ret;
	}

	s = get_slab(size, gfpflags);

	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	ret = slab_alloc(s, gfpflags, node, caller);

	/* Honor the call site pointer we recieved. */
	trace_kmalloc_node(caller, ret, size, s->size, gfpflags, node);

	return ret;
}

#ifdef CONFIG_SLUB_DEBUG
static int count_inuse(struct page *page)
{
	return page->inuse;
}

static int count_total(struct page *page)
{
	return page->objects;
}

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

	for_each_free_object(p, s, page->freelist) {
		set_bit(slab_index(p, s, addr), map);
		if (!check_object(s, page, p, 0))
			return 0;
	}

	for_each_object(p, s, addr, page->objects)
		if (!test_bit(slab_index(p, s, addr), map))
			if (!check_object(s, page, p, 1))
				return 0;
	return 1;
}

static void validate_slab_slab(struct kmem_cache *s, struct page *page,
						unsigned long *map)
{
	if (slab_trylock(page)) {
		validate_slab(s, page, map);
		slab_unlock(page);
	} else
		printk(KERN_INFO "SLUB %s: Skipped busy slab 0x%p\n",
			s->name, page);
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
		printk(KERN_ERR "SLUB %s: %ld partial slabs counted but "
			"counter=%ld\n", s->name, count, n->nr_partial);

	if (!(s->flags & SLAB_STORE_USER))
		goto out;

	list_for_each_entry(page, &n->full, lru) {
		validate_slab_slab(s, page, map);
		count++;
	}
	if (count != atomic_long_read(&n->nr_slabs))
		printk(KERN_ERR "SLUB: %s %ld slabs counted but "
			"counter=%ld\n", s->name, count,
			atomic_long_read(&n->nr_slabs));

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

	if (!map)
		return -ENOMEM;

	flush_all(s);
	for_each_node_state(node, N_NORMAL_MEMORY) {
		struct kmem_cache_node *n = get_node(s, node);

		count += validate_slab_node(s, n, map);
	}
	kfree(map);
	return count;
}

#ifdef SLUB_RESILIENCY_TEST
static void resiliency_test(void)
{
	u8 *p;

	printk(KERN_ERR "SLUB resiliency testing\n");
	printk(KERN_ERR "-----------------------\n");
	printk(KERN_ERR "A. Corruption after allocation\n");

	p = kzalloc(16, GFP_KERNEL);
	p[16] = 0x12;
	printk(KERN_ERR "\n1. kmalloc-16: Clobber Redzone/next pointer"
			" 0x12->0x%p\n\n", p + 16);

	validate_slab_cache(kmalloc_caches + 4);

	/* Hmmm... The next two are dangerous */
	p = kzalloc(32, GFP_KERNEL);
	p[32 + sizeof(void *)] = 0x34;
	printk(KERN_ERR "\n2. kmalloc-32: Clobber next pointer/next slab"
			" 0x34 -> -0x%p\n", p);
	printk(KERN_ERR
		"If allocated object is overwritten then not detectable\n\n");

	validate_slab_cache(kmalloc_caches + 5);
	p = kzalloc(64, GFP_KERNEL);
	p += 64 + (get_cycles() & 0xff) * sizeof(void *);
	*p = 0x56;
	printk(KERN_ERR "\n3. kmalloc-64: corrupting random byte 0x56->0x%p\n",
									p);
	printk(KERN_ERR
		"If allocated object is overwritten then not detectable\n\n");
	validate_slab_cache(kmalloc_caches + 6);

	printk(KERN_ERR "\nB. Corruption after free\n");
	p = kzalloc(128, GFP_KERNEL);
	kfree(p);
	*p = 0x78;
	printk(KERN_ERR "1. kmalloc-128: Clobber first word 0x78->0x%p\n\n", p);
	validate_slab_cache(kmalloc_caches + 7);

	p = kzalloc(256, GFP_KERNEL);
	kfree(p);
	p[50] = 0x9a;
	printk(KERN_ERR "\n2. kmalloc-256: Clobber 50th byte 0x9a->0x%p\n\n",
			p);
	validate_slab_cache(kmalloc_caches + 8);

	p = kzalloc(512, GFP_KERNEL);
	kfree(p);
	p[512] = 0xab;
	printk(KERN_ERR "\n3. kmalloc-512: Clobber redzone 0xab->0x%p\n\n", p);
	validate_slab_cache(kmalloc_caches + 9);
}
#else
static void resiliency_test(void) {};
#endif

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
		long *map)
{
	void *addr = page_address(page);
	void *p;

	bitmap_zero(map, page->objects);
	for_each_free_object(p, s, page->freelist)
		set_bit(slab_index(p, s, addr), map);

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

	if (!map || !alloc_loc_track(&t, PAGE_SIZE / sizeof(struct location),
				     GFP_TEMPORARY)) {
		kfree(map);
		return sprintf(buf, "Out of memory\n");
	}
	/* Push back cpu slabs */
	flush_all(s);

	for_each_node_state(node, N_NORMAL_MEMORY) {
		struct kmem_cache_node *n = get_node(s, node);
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
			len += sprint_symbol(buf + len, (unsigned long)l->addr);
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
				len < PAGE_SIZE - 60) {
			len += sprintf(buf + len, " cpus=");
			len += cpulist_scnprintf(buf + len, PAGE_SIZE - len - 50,
						 to_cpumask(l->cpus));
		}

		if (nr_online_nodes > 1 && !nodes_empty(l->nodes) &&
				len < PAGE_SIZE - 60) {
			len += sprintf(buf + len, " nodes=");
			len += nodelist_scnprintf(buf + len, PAGE_SIZE - len - 50,
					l->nodes);
		}

		len += sprintf(buf + len, "\n");
	}

	free_loc_track(&t);
	kfree(map);
	if (!t.count)
		len += sprintf(buf, "No data\n");
	return len;
}

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
	unsigned long *per_cpu;

	nodes = kzalloc(2 * sizeof(unsigned long) * nr_node_ids, GFP_KERNEL);
	if (!nodes)
		return -ENOMEM;
	per_cpu = nodes + nr_node_ids;

	if (flags & SO_CPU) {
		int cpu;

		for_each_possible_cpu(cpu) {
			struct kmem_cache_cpu *c = per_cpu_ptr(s->cpu_slab, cpu);

			if (!c || c->node < 0)
				continue;

			if (c->page) {
					if (flags & SO_TOTAL)
						x = c->page->objects;
				else if (flags & SO_OBJECTS)
					x = c->page->inuse;
				else
					x = 1;

				total += x;
				nodes[c->node] += x;
			}
			per_cpu[c->node]++;
		}
	}

	if (flags & SO_ALL) {
		for_each_node_state(node, N_NORMAL_MEMORY) {
			struct kmem_cache_node *n = get_node(s, node);

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

	} else if (flags & SO_PARTIAL) {
		for_each_node_state(node, N_NORMAL_MEMORY) {
			struct kmem_cache_node *n = get_node(s, node);

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
	for_each_node_state(node, N_NORMAL_MEMORY)
		if (nodes[node])
			x += sprintf(buf + x, " N%d=%lu",
					node, nodes[node]);
#endif
	kfree(nodes);
	return x + sprintf(buf + x, "\n");
}

static int any_slab_objects(struct kmem_cache *s)
{
	int node;

	for_each_online_node(node) {
		struct kmem_cache_node *n = get_node(s, node);

		if (!n)
			continue;

		if (atomic_long_read(&n->total_objects))
			return 1;
	}
	return 0;
}

#define to_slab_attr(n) container_of(n, struct slab_attribute, attr)
#define to_slab(n) container_of(n, struct kmem_cache, kobj);

struct slab_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kmem_cache *s, char *buf);
	ssize_t (*store)(struct kmem_cache *s, const char *x, size_t count);
};

#define SLAB_ATTR_RO(_name) \
	static struct slab_attribute _name##_attr = __ATTR_RO(_name)

#define SLAB_ATTR(_name) \
	static struct slab_attribute _name##_attr =  \
	__ATTR(_name, 0644, _name##_show, _name##_store)

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
	return sprintf(buf, "%d\n", s->objsize);
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

	err = strict_strtoul(buf, 10, &order);
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

	err = strict_strtoul(buf, 10, &min);
	if (err)
		return err;

	set_min_partial(s, min);
	return length;
}
SLAB_ATTR(min_partial);

static ssize_t ctor_show(struct kmem_cache *s, char *buf)
{
	if (s->ctor) {
		int n = sprint_symbol(buf, (unsigned long)s->ctor);

		return n + sprintf(buf + n, "\n");
	}
	return 0;
}
SLAB_ATTR_RO(ctor);

static ssize_t aliases_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", s->refcount - 1);
}
SLAB_ATTR_RO(aliases);

static ssize_t slabs_show(struct kmem_cache *s, char *buf)
{
	return show_slab_objects(s, buf, SO_ALL);
}
SLAB_ATTR_RO(slabs);

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
	if (buf[0] == '1')
		s->flags |= SLAB_DEBUG_FREE;
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
	s->flags &= ~SLAB_TRACE;
	if (buf[0] == '1')
		s->flags |= SLAB_TRACE;
	return length;
}
SLAB_ATTR(trace);

#ifdef CONFIG_FAILSLAB
static ssize_t failslab_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_FAILSLAB));
}

static ssize_t failslab_store(struct kmem_cache *s, const char *buf,
							size_t length)
{
	s->flags &= ~SLAB_FAILSLAB;
	if (buf[0] == '1')
		s->flags |= SLAB_FAILSLAB;
	return length;
}
SLAB_ATTR(failslab);
#endif

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
	if (buf[0] == '1')
		s->flags |= SLAB_RED_ZONE;
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
	if (buf[0] == '1')
		s->flags |= SLAB_POISON;
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
	if (buf[0] == '1')
		s->flags |= SLAB_STORE_USER;
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

static ssize_t shrink_show(struct kmem_cache *s, char *buf)
{
	return 0;
}

static ssize_t shrink_store(struct kmem_cache *s,
			const char *buf, size_t length)
{
	if (buf[0] == '1') {
		int rc = kmem_cache_shrink(s);

		if (rc)
			return rc;
	} else
		return -EINVAL;
	return length;
}
SLAB_ATTR(shrink);

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

	err = strict_strtoul(buf, 10, &ratio);
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
STAT_ATTR(FREE_SLAB, free_slab);
STAT_ATTR(CPUSLAB_FLUSH, cpuslab_flush);
STAT_ATTR(DEACTIVATE_FULL, deactivate_full);
STAT_ATTR(DEACTIVATE_EMPTY, deactivate_empty);
STAT_ATTR(DEACTIVATE_TO_HEAD, deactivate_to_head);
STAT_ATTR(DEACTIVATE_TO_TAIL, deactivate_to_tail);
STAT_ATTR(DEACTIVATE_REMOTE_FREES, deactivate_remote_frees);
STAT_ATTR(ORDER_FALLBACK, order_fallback);
#endif

static struct attribute *slab_attrs[] = {
	&slab_size_attr.attr,
	&object_size_attr.attr,
	&objs_per_slab_attr.attr,
	&order_attr.attr,
	&min_partial_attr.attr,
	&objects_attr.attr,
	&objects_partial_attr.attr,
	&total_objects_attr.attr,
	&slabs_attr.attr,
	&partial_attr.attr,
	&cpu_slabs_attr.attr,
	&ctor_attr.attr,
	&aliases_attr.attr,
	&align_attr.attr,
	&sanity_checks_attr.attr,
	&trace_attr.attr,
	&hwcache_align_attr.attr,
	&reclaim_account_attr.attr,
	&destroy_by_rcu_attr.attr,
	&red_zone_attr.attr,
	&poison_attr.attr,
	&store_user_attr.attr,
	&validate_attr.attr,
	&shrink_attr.attr,
	&alloc_calls_attr.attr,
	&free_calls_attr.attr,
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
	&free_slab_attr.attr,
	&cpuslab_flush_attr.attr,
	&deactivate_full_attr.attr,
	&deactivate_empty_attr.attr,
	&deactivate_to_head_attr.attr,
	&deactivate_to_tail_attr.attr,
	&deactivate_remote_frees_attr.attr,
	&order_fallback_attr.attr,
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

	return err;
}

static void kmem_cache_release(struct kobject *kobj)
{
	struct kmem_cache *s = to_slab(kobj);

	kfree(s);
}

static const struct sysfs_ops slab_sysfs_ops = {
	.show = slab_attr_show,
	.store = slab_attr_store,
};

static struct kobj_type slab_ktype = {
	.sysfs_ops = &slab_sysfs_ops,
	.release = kmem_cache_release
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
	int unmergeable;

	if (slab_state < SYSFS)
		/* Defer until later */
		return 0;

	unmergeable = slab_unmergeable(s);
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

	s->kobj.kset = slab_kset;
	err = kobject_init_and_add(&s->kobj, &slab_ktype, NULL, name);
	if (err) {
		kobject_put(&s->kobj);
		return err;
	}

	err = sysfs_create_group(&s->kobj, &slab_attr_group);
	if (err) {
		kobject_del(&s->kobj);
		kobject_put(&s->kobj);
		return err;
	}
	kobject_uevent(&s->kobj, KOBJ_ADD);
	if (!unmergeable) {
		/* Setup first alias */
		sysfs_slab_alias(s, s->name);
		kfree(name);
	}
	return 0;
}

static void sysfs_slab_remove(struct kmem_cache *s)
{
	if (slab_state < SYSFS)
		/*
		 * Sysfs has not been setup yet so no need to remove the
		 * cache from sysfs.
		 */
		return;

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

	if (slab_state == SYSFS) {
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

	down_write(&slub_lock);

	slab_kset = kset_create_and_add("slab", &slab_uevent_ops, kernel_kobj);
	if (!slab_kset) {
		up_write(&slub_lock);
		printk(KERN_ERR "Cannot register slab subsystem.\n");
		return -ENOSYS;
	}

	slab_state = SYSFS;

	list_for_each_entry(s, &slab_caches, list) {
		err = sysfs_slab_add(s);
		if (err)
			printk(KERN_ERR "SLUB: Unable to add boot slab %s"
						" to sysfs\n", s->name);
	}

	while (alias_list) {
		struct saved_alias *al = alias_list;

		alias_list = alias_list->next;
		err = sysfs_slab_alias(al->s, al->name);
		if (err)
			printk(KERN_ERR "SLUB: Unable to add boot slab alias"
					" %s to sysfs\n", s->name);
		kfree(al);
	}

	up_write(&slub_lock);
	resiliency_test();
	return 0;
}

__initcall(slab_sysfs_init);
#endif

/*
 * The /proc/slabinfo ABI
 */
#ifdef CONFIG_SLABINFO
static void print_slabinfo_header(struct seq_file *m)
{
	seq_puts(m, "slabinfo - version: 2.1\n");
	seq_puts(m, "# name            <active_objs> <num_objs> <objsize> "
		 "<objperslab> <pagesperslab>");
	seq_puts(m, " : tunables <limit> <batchcount> <sharedfactor>");
	seq_puts(m, " : slabdata <active_slabs> <num_slabs> <sharedavail>");
	seq_putc(m, '\n');
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	loff_t n = *pos;

	down_read(&slub_lock);
	if (!n)
		print_slabinfo_header(m);

	return seq_list_start(&slab_caches, *pos);
}

static void *s_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &slab_caches, pos);
}

static void s_stop(struct seq_file *m, void *p)
{
	up_read(&slub_lock);
}

static int s_show(struct seq_file *m, void *p)
{
	unsigned long nr_partials = 0;
	unsigned long nr_slabs = 0;
	unsigned long nr_inuse = 0;
	unsigned long nr_objs = 0;
	unsigned long nr_free = 0;
	struct kmem_cache *s;
	int node;

	s = list_entry(p, struct kmem_cache, list);

	for_each_online_node(node) {
		struct kmem_cache_node *n = get_node(s, node);

		if (!n)
			continue;

		nr_partials += n->nr_partial;
		nr_slabs += atomic_long_read(&n->nr_slabs);
		nr_objs += atomic_long_read(&n->total_objects);
		nr_free += count_partial(n, count_free);
	}

	nr_inuse = nr_objs - nr_free;

	seq_printf(m, "%-17s %6lu %6lu %6u %4u %4d", s->name, nr_inuse,
		   nr_objs, s->size, oo_objects(s->oo),
		   (1 << oo_order(s->oo)));
	seq_printf(m, " : tunables %4u %4u %4u", 0, 0, 0);
	seq_printf(m, " : slabdata %6lu %6lu %6lu", nr_slabs, nr_slabs,
		   0UL);
	seq_putc(m, '\n');
	return 0;
}

static const struct seq_operations slabinfo_op = {
	.start = s_start,
	.next = s_next,
	.stop = s_stop,
	.show = s_show,
};

static int slabinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &slabinfo_op);
}

static const struct file_operations proc_slabinfo_operations = {
	.open		= slabinfo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init slab_proc_init(void)
{
	proc_create("slabinfo", S_IRUGO, NULL, &proc_slabinfo_operations);
	return 0;
}
module_init(slab_proc_init);
#endif /* CONFIG_SLABINFO */
