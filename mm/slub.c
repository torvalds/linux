/*
 * SLUB: A slab allocator that limits cache line use instead of queuing
 * objects in per cpu and per node lists.
 *
 * The allocator synchronizes using per slab locks and only
 * uses a centralized lock to manage a pool of partial slabs.
 *
 * (C) 2007 SGI, Christoph Lameter <clameter@sgi.com>
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/bit_spinlock.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/mempolicy.h>
#include <linux/ctype.h>
#include <linux/kallsyms.h>

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
 * 			lockless_freelist that allows lockless access to
 * 			free objects in addition to the regular freelist
 * 			that requires the slab lock.
 *
 * PageError		Slab requires special handling due to debug
 * 			options set. This moves	slab handling out of
 * 			the fast path and disables lockless freelists.
 */

#define FROZEN (1 << PG_active)

#ifdef CONFIG_SLUB_DEBUG
#define SLABDEBUG (1 << PG_error)
#else
#define SLABDEBUG 0
#endif

static inline int SlabFrozen(struct page *page)
{
	return page->flags & FROZEN;
}

static inline void SetSlabFrozen(struct page *page)
{
	page->flags |= FROZEN;
}

static inline void ClearSlabFrozen(struct page *page)
{
	page->flags &= ~FROZEN;
}

static inline int SlabDebug(struct page *page)
{
	return page->flags & SLABDEBUG;
}

static inline void SetSlabDebug(struct page *page)
{
	page->flags |= SLABDEBUG;
}

static inline void ClearSlabDebug(struct page *page)
{
	page->flags &= ~SLABDEBUG;
}

/*
 * Issues still to be resolved:
 *
 * - The per cpu array is updated for each new slab and and is a remote
 *   cacheline for most nodes. This could become a bouncing cacheline given
 *   enough frequent updates. There are 16 pointers in a cacheline, so at
 *   max 16 cpus could compete for the cacheline which may be okay.
 *
 * - Support PAGE_ALLOC_DEBUG. Should be easy to do.
 *
 * - Variable sizing of the per node arrays
 */

/* Enable to test recovery from slab corruption on boot */
#undef SLUB_RESILIENCY_TEST

#if PAGE_SHIFT <= 12

/*
 * Small page size. Make sure that we do not fragment memory
 */
#define DEFAULT_MAX_ORDER 1
#define DEFAULT_MIN_OBJECTS 4

#else

/*
 * Large page machines are customarily able to handle larger
 * page orders.
 */
#define DEFAULT_MAX_ORDER 2
#define DEFAULT_MIN_OBJECTS 8

#endif

/*
 * Mininum number of partial slabs. These will be left on the partial
 * lists even if they are empty. kmem_cache_shrink may reclaim them.
 */
#define MIN_PARTIAL 2

/*
 * Maximum number of desirable partial slabs.
 * The existence of more partial slabs makes kmem_cache_shrink
 * sort the partial list by the number of objects in the.
 */
#define MAX_PARTIAL 10

#define DEBUG_DEFAULT_FLAGS (SLAB_DEBUG_FREE | SLAB_RED_ZONE | \
				SLAB_POISON | SLAB_STORE_USER)

/*
 * Set of flags that will prevent slab merging
 */
#define SLUB_NEVER_MERGE (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER | \
		SLAB_TRACE | SLAB_DESTROY_BY_RCU)

#define SLUB_MERGE_SAME (SLAB_DEBUG_FREE | SLAB_RECLAIM_ACCOUNT | \
		SLAB_CACHE_DMA)

#ifndef ARCH_KMALLOC_MINALIGN
#define ARCH_KMALLOC_MINALIGN __alignof__(unsigned long long)
#endif

#ifndef ARCH_SLAB_MINALIGN
#define ARCH_SLAB_MINALIGN __alignof__(unsigned long long)
#endif

/* Internal SLUB flags */
#define __OBJECT_POISON 0x80000000	/* Poison object */

/* Not all arches define cache_line_size */
#ifndef cache_line_size
#define cache_line_size()	L1_CACHE_BYTES
#endif

static int kmem_size = sizeof(struct kmem_cache);

#ifdef CONFIG_SMP
static struct notifier_block slab_notifier;
#endif

static enum {
	DOWN,		/* No slab functionality available */
	PARTIAL,	/* kmem_cache_open() works but kmalloc does not */
	UP,		/* Everything works but does not show up in sysfs */
	SYSFS		/* Sysfs up */
} slab_state = DOWN;

/* A list of all slab caches on the system */
static DECLARE_RWSEM(slub_lock);
LIST_HEAD(slab_caches);

/*
 * Tracking user of a slab.
 */
struct track {
	void *addr;		/* Called from address */
	int cpu;		/* Was running on cpu */
	int pid;		/* Pid context */
	unsigned long when;	/* When did the operation occur */
};

enum track_item { TRACK_ALLOC, TRACK_FREE };

#if defined(CONFIG_SYSFS) && defined(CONFIG_SLUB_DEBUG)
static int sysfs_slab_add(struct kmem_cache *);
static int sysfs_slab_alias(struct kmem_cache *, const char *);
static void sysfs_slab_remove(struct kmem_cache *);
#else
static int sysfs_slab_add(struct kmem_cache *s) { return 0; }
static int sysfs_slab_alias(struct kmem_cache *s, const char *p) { return 0; }
static void sysfs_slab_remove(struct kmem_cache *s) {}
#endif

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

static inline int check_valid_pointer(struct kmem_cache *s,
				struct page *page, const void *object)
{
	void *base;

	if (!object)
		return 1;

	base = page_address(page);
	if (object < base || object >= base + s->objects * s->size ||
		(object - base) % s->size) {
		return 0;
	}

	return 1;
}

/*
 * Slow version of get and set free pointer.
 *
 * This version requires touching the cache lines of kmem_cache which
 * we avoid to do in the fast alloc free paths. There we obtain the offset
 * from the page struct.
 */
static inline void *get_freepointer(struct kmem_cache *s, void *object)
{
	return *(void **)(object + s->offset);
}

static inline void set_freepointer(struct kmem_cache *s, void *object, void *fp)
{
	*(void **)(object + s->offset) = fp;
}

/* Loop over all objects in a slab */
#define for_each_object(__p, __s, __addr) \
	for (__p = (__addr); __p < (__addr) + (__s)->objects * (__s)->size;\
			__p += (__s)->size)

/* Scan freelist */
#define for_each_free_object(__p, __s, __free) \
	for (__p = (__free); __p; __p = get_freepointer((__s), __p))

/* Determine object index from a given position */
static inline int slab_index(void *p, struct kmem_cache *s, void *addr)
{
	return (p - addr) / s->size;
}

#ifdef CONFIG_SLUB_DEBUG
/*
 * Debug settings:
 */
static int slub_debug;

static char *slub_debug_slabs;

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
			printk(KERN_ERR "%10s 0x%p: ", text, addr + i);
			newline = 0;
		}
		printk(" %02x", addr[i]);
		offset = i % 16;
		ascii[offset] = isgraph(addr[i]) ? addr[i] : '.';
		if (offset == 15) {
			printk(" %s\n",ascii);
			newline = 1;
		}
	}
	if (!newline) {
		i %= 16;
		while (i < 16) {
			printk("   ");
			ascii[i] = ' ';
			i++;
		}
		printk(" %s\n", ascii);
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
				enum track_item alloc, void *addr)
{
	struct track *p;

	if (s->offset)
		p = object + s->offset + sizeof(void *);
	else
		p = object + s->inuse;

	p += alloc;
	if (addr) {
		p->addr = addr;
		p->cpu = smp_processor_id();
		p->pid = current ? current->pid : -1;
		p->when = jiffies;
	} else
		memset(p, 0, sizeof(struct track));
}

static void init_tracking(struct kmem_cache *s, void *object)
{
	if (s->flags & SLAB_STORE_USER) {
		set_track(s, object, TRACK_FREE, NULL);
		set_track(s, object, TRACK_ALLOC, NULL);
	}
}

static void print_track(const char *s, struct track *t)
{
	if (!t->addr)
		return;

	printk(KERN_ERR "%s: ", s);
	__print_symbol("%s", (unsigned long)t->addr);
	printk(" jiffies_ago=%lu cpu=%u pid=%d\n", jiffies - t->when, t->cpu, t->pid);
}

static void print_trailer(struct kmem_cache *s, u8 *p)
{
	unsigned int off;	/* Offset of last byte */

	if (s->flags & SLAB_RED_ZONE)
		print_section("Redzone", p + s->objsize,
			s->inuse - s->objsize);

	printk(KERN_ERR "FreePointer 0x%p -> 0x%p\n",
			p + s->offset,
			get_freepointer(s, p));

	if (s->offset)
		off = s->offset + sizeof(void *);
	else
		off = s->inuse;

	if (s->flags & SLAB_STORE_USER) {
		print_track("Last alloc", get_track(s, p, TRACK_ALLOC));
		print_track("Last free ", get_track(s, p, TRACK_FREE));
		off += 2 * sizeof(struct track);
	}

	if (off != s->size)
		/* Beginning of the filler is the free pointer */
		print_section("Filler", p + off, s->size - off);
}

static void object_err(struct kmem_cache *s, struct page *page,
			u8 *object, char *reason)
{
	u8 *addr = page_address(page);

	printk(KERN_ERR "*** SLUB %s: %s@0x%p slab 0x%p\n",
			s->name, reason, object, page);
	printk(KERN_ERR "    offset=%tu flags=0x%04lx inuse=%u freelist=0x%p\n",
		object - addr, page->flags, page->inuse, page->freelist);
	if (object > addr + 16)
		print_section("Bytes b4", object - 16, 16);
	print_section("Object", object, min(s->objsize, 128));
	print_trailer(s, object);
	dump_stack();
}

static void slab_err(struct kmem_cache *s, struct page *page, char *reason, ...)
{
	va_list args;
	char buf[100];

	va_start(args, reason);
	vsnprintf(buf, sizeof(buf), reason, args);
	va_end(args);
	printk(KERN_ERR "*** SLUB %s: %s in slab @0x%p\n", s->name, buf,
		page);
	dump_stack();
}

static void init_object(struct kmem_cache *s, void *object, int active)
{
	u8 *p = object;

	if (s->flags & __OBJECT_POISON) {
		memset(p, POISON_FREE, s->objsize - 1);
		p[s->objsize -1] = POISON_END;
	}

	if (s->flags & SLAB_RED_ZONE)
		memset(p + s->objsize,
			active ? SLUB_RED_ACTIVE : SLUB_RED_INACTIVE,
			s->inuse - s->objsize);
}

static int check_bytes(u8 *start, unsigned int value, unsigned int bytes)
{
	while (bytes) {
		if (*start != (u8)value)
			return 0;
		start++;
		bytes--;
	}
	return 1;
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
 * 		one word if debuggin is on to be able to detect writes
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

static void restore_bytes(struct kmem_cache *s, char *message, u8 data,
						void *from, void *to)
{
	printk(KERN_ERR "@@@ SLUB %s: Restoring %s (0x%x) from 0x%p-0x%p\n",
		s->name, message, data, from, to - 1);
	memset(from, data, to - from);
}

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

	if (check_bytes(p + off, POISON_INUSE, s->size - off))
		return 1;

	object_err(s, page, p, "Object padding check fails");

	/*
	 * Restore padding
	 */
	restore_bytes(s, "object padding", POISON_INUSE, p + off, p + s->size);
	return 0;
}

static int slab_pad_check(struct kmem_cache *s, struct page *page)
{
	u8 *p;
	int length, remainder;

	if (!(s->flags & SLAB_POISON))
		return 1;

	p = page_address(page);
	length = s->objects * s->size;
	remainder = (PAGE_SIZE << s->order) - length;
	if (!remainder)
		return 1;

	if (!check_bytes(p + length, POISON_INUSE, remainder)) {
		slab_err(s, page, "Padding check failed");
		restore_bytes(s, "slab padding", POISON_INUSE, p + length,
			p + length + remainder);
		return 0;
	}
	return 1;
}

static int check_object(struct kmem_cache *s, struct page *page,
					void *object, int active)
{
	u8 *p = object;
	u8 *endobject = object + s->objsize;

	if (s->flags & SLAB_RED_ZONE) {
		unsigned int red =
			active ? SLUB_RED_ACTIVE : SLUB_RED_INACTIVE;

		if (!check_bytes(endobject, red, s->inuse - s->objsize)) {
			object_err(s, page, object,
			active ? "Redzone Active" : "Redzone Inactive");
			restore_bytes(s, "redzone", red,
				endobject, object + s->inuse);
			return 0;
		}
	} else {
		if ((s->flags & SLAB_POISON) && s->objsize < s->inuse &&
			!check_bytes(endobject, POISON_INUSE,
					s->inuse - s->objsize)) {
		object_err(s, page, p, "Alignment padding check fails");
		/*
		 * Fix it so that there will not be another report.
		 *
		 * Hmmm... We may be corrupting an object that now expects
		 * to be longer than allowed.
		 */
		restore_bytes(s, "alignment padding", POISON_INUSE,
			endobject, object + s->inuse);
		}
	}

	if (s->flags & SLAB_POISON) {
		if (!active && (s->flags & __OBJECT_POISON) &&
			(!check_bytes(p, POISON_FREE, s->objsize - 1) ||
				p[s->objsize - 1] != POISON_END)) {

			object_err(s, page, p, "Poison check failed");
			restore_bytes(s, "Poison", POISON_FREE,
						p, p + s->objsize -1);
			restore_bytes(s, "Poison", POISON_END,
					p + s->objsize - 1, p + s->objsize);
			return 0;
		}
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
		 * No choice but to zap it and thus loose the remainder
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
	VM_BUG_ON(!irqs_disabled());

	if (!PageSlab(page)) {
		slab_err(s, page, "Not a valid slab page flags=%lx "
			"mapping=0x%p count=%d", page->flags, page->mapping,
			page_count(page));
		return 0;
	}
	if (page->offset * sizeof(void *) != s->offset) {
		slab_err(s, page, "Corrupted offset %lu flags=0x%lx "
			"mapping=0x%p count=%d",
			(unsigned long)(page->offset * sizeof(void *)),
			page->flags,
			page->mapping,
			page_count(page));
		return 0;
	}
	if (page->inuse > s->objects) {
		slab_err(s, page, "inuse %u > max %u @0x%p flags=%lx "
			"mapping=0x%p count=%d",
			s->name, page->inuse, s->objects, page->flags,
			page->mapping, page_count(page));
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

	while (fp && nr <= s->objects) {
		if (fp == search)
			return 1;
		if (!check_valid_pointer(s, page, fp)) {
			if (object) {
				object_err(s, page, object,
					"Freechain corrupt");
				set_freepointer(s, object, NULL);
				break;
			} else {
				slab_err(s, page, "Freepointer 0x%p corrupt",
									fp);
				page->freelist = NULL;
				page->inuse = s->objects;
				printk(KERN_ERR "@@@ SLUB %s: Freelist "
					"cleared. Slab 0x%p\n",
					s->name, page);
				return 0;
			}
			break;
		}
		object = fp;
		fp = get_freepointer(s, object);
		nr++;
	}

	if (page->inuse != s->objects - nr) {
		slab_err(s, page, "Wrong object count. Counter is %d but "
			"counted were %d", s, page, page->inuse,
							s->objects - nr);
		page->inuse = s->objects - nr;
		printk(KERN_ERR "@@@ SLUB %s: Object count adjusted. "
			"Slab @0x%p\n", s->name, page);
	}
	return search == NULL;
}

static void trace(struct kmem_cache *s, struct page *page, void *object, int alloc)
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

static void setup_object_debug(struct kmem_cache *s, struct page *page,
								void *object)
{
	if (!(s->flags & (SLAB_STORE_USER|SLAB_RED_ZONE|__OBJECT_POISON)))
		return;

	init_object(s, object, 0);
	init_tracking(s, object);
}

static int alloc_debug_processing(struct kmem_cache *s, struct page *page,
						void *object, void *addr)
{
	if (!check_slab(s, page))
		goto bad;

	if (object && !on_freelist(s, page, object)) {
		slab_err(s, page, "Object 0x%p already allocated", object);
		goto bad;
	}

	if (!check_valid_pointer(s, page, object)) {
		object_err(s, page, object, "Freelist Pointer check fails");
		goto bad;
	}

	if (object && !check_object(s, page, object, 0))
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
		printk(KERN_ERR "@@@ SLUB: %s slab 0x%p. Marking all objects used.\n",
			s->name, page);
		page->inuse = s->objects;
		page->freelist = NULL;
		/* Fix up fields that may be corrupted */
		page->offset = s->offset / sizeof(void *);
	}
	return 0;
}

static int free_debug_processing(struct kmem_cache *s, struct page *page,
						void *object, void *addr)
{
	if (!check_slab(s, page))
		goto fail;

	if (!check_valid_pointer(s, page, object)) {
		slab_err(s, page, "Invalid object pointer 0x%p", object);
		goto fail;
	}

	if (on_freelist(s, page, object)) {
		slab_err(s, page, "Object 0x%p already free", object);
		goto fail;
	}

	if (!check_object(s, page, object, 1))
		return 0;

	if (unlikely(s != page->slab)) {
		if (!PageSlab(page))
			slab_err(s, page, "Attempt to free object(0x%p) "
				"outside of slab", object);
		else
		if (!page->slab) {
			printk(KERN_ERR
				"SLUB <none>: no slab for object 0x%p.\n",
						object);
			dump_stack();
		}
		else
			slab_err(s, page, "object at 0x%p belongs "
				"to slab %s", object, page->slab->name);
		goto fail;
	}

	/* Special debug activities for freeing objects */
	if (!SlabFrozen(page) && !page->freelist)
		remove_full(s, page);
	if (s->flags & SLAB_STORE_USER)
		set_track(s, object, TRACK_FREE, addr);
	trace(s, page, object, 0);
	init_object(s, object, 0);
	return 1;

fail:
	printk(KERN_ERR "@@@ SLUB: %s slab 0x%p object at 0x%p not freed.\n",
		s->name, page, object);
	return 0;
}

static int __init setup_slub_debug(char *str)
{
	if (!str || *str != '=')
		slub_debug = DEBUG_DEFAULT_FLAGS;
	else {
		str++;
		if (*str == 0 || *str == ',')
			slub_debug = DEBUG_DEFAULT_FLAGS;
		else
		for( ;*str && *str != ','; str++)
			switch (*str) {
			case 'f' : case 'F' :
				slub_debug |= SLAB_DEBUG_FREE;
				break;
			case 'z' : case 'Z' :
				slub_debug |= SLAB_RED_ZONE;
				break;
			case 'p' : case 'P' :
				slub_debug |= SLAB_POISON;
				break;
			case 'u' : case 'U' :
				slub_debug |= SLAB_STORE_USER;
				break;
			case 't' : case 'T' :
				slub_debug |= SLAB_TRACE;
				break;
			default:
				printk(KERN_ERR "slub_debug option '%c' "
					"unknown. skipped\n",*str);
			}
	}

	if (*str == ',')
		slub_debug_slabs = str + 1;
	return 1;
}

__setup("slub_debug", setup_slub_debug);

static void kmem_cache_open_debug_check(struct kmem_cache *s)
{
	/*
	 * The page->offset field is only 16 bit wide. This is an offset
	 * in units of words from the beginning of an object. If the slab
	 * size is bigger then we cannot move the free pointer behind the
	 * object anymore.
	 *
	 * On 32 bit platforms the limit is 256k. On 64bit platforms
	 * the limit is 512k.
	 *
	 * Debugging or ctor may create a need to move the free
	 * pointer. Fail if this happens.
	 */
	if (s->size >= 65535 * sizeof(void *)) {
		BUG_ON(s->flags & (SLAB_RED_ZONE | SLAB_POISON |
				SLAB_STORE_USER | SLAB_DESTROY_BY_RCU));
		BUG_ON(s->ctor);
	}
	else
		/*
		 * Enable debugging if selected on the kernel commandline.
		 */
		if (slub_debug && (!slub_debug_slabs ||
		    strncmp(slub_debug_slabs, s->name,
		    	strlen(slub_debug_slabs)) == 0))
				s->flags |= slub_debug;
}
#else
static inline void setup_object_debug(struct kmem_cache *s,
			struct page *page, void *object) {}

static inline int alloc_debug_processing(struct kmem_cache *s,
	struct page *page, void *object, void *addr) { return 0; }

static inline int free_debug_processing(struct kmem_cache *s,
	struct page *page, void *object, void *addr) { return 0; }

static inline int slab_pad_check(struct kmem_cache *s, struct page *page)
			{ return 1; }
static inline int check_object(struct kmem_cache *s, struct page *page,
			void *object, int active) { return 1; }
static inline void add_full(struct kmem_cache_node *n, struct page *page) {}
static inline void kmem_cache_open_debug_check(struct kmem_cache *s) {}
#define slub_debug 0
#endif
/*
 * Slab allocation and freeing
 */
static struct page *allocate_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	struct page * page;
	int pages = 1 << s->order;

	if (s->order)
		flags |= __GFP_COMP;

	if (s->flags & SLAB_CACHE_DMA)
		flags |= SLUB_DMA;

	if (node == -1)
		page = alloc_pages(flags, s->order);
	else
		page = alloc_pages_node(node, flags, s->order);

	if (!page)
		return NULL;

	mod_zone_page_state(page_zone(page),
		(s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE : NR_SLAB_UNRECLAIMABLE,
		pages);

	return page;
}

static void setup_object(struct kmem_cache *s, struct page *page,
				void *object)
{
	setup_object_debug(s, page, object);
	if (unlikely(s->ctor))
		s->ctor(object, s, 0);
}

static struct page *new_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	struct page *page;
	struct kmem_cache_node *n;
	void *start;
	void *end;
	void *last;
	void *p;

	BUG_ON(flags & ~(GFP_DMA | GFP_LEVEL_MASK));

	if (flags & __GFP_WAIT)
		local_irq_enable();

	page = allocate_slab(s, flags & GFP_LEVEL_MASK, node);
	if (!page)
		goto out;

	n = get_node(s, page_to_nid(page));
	if (n)
		atomic_long_inc(&n->nr_slabs);
	page->offset = s->offset / sizeof(void *);
	page->slab = s;
	page->flags |= 1 << PG_slab;
	if (s->flags & (SLAB_DEBUG_FREE | SLAB_RED_ZONE | SLAB_POISON |
			SLAB_STORE_USER | SLAB_TRACE))
		SetSlabDebug(page);

	start = page_address(page);
	end = start + s->objects * s->size;

	if (unlikely(s->flags & SLAB_POISON))
		memset(start, POISON_INUSE, PAGE_SIZE << s->order);

	last = start;
	for_each_object(p, s, start) {
		setup_object(s, page, last);
		set_freepointer(s, last, p);
		last = p;
	}
	setup_object(s, page, last);
	set_freepointer(s, last, NULL);

	page->freelist = start;
	page->lockless_freelist = NULL;
	page->inuse = 0;
out:
	if (flags & __GFP_WAIT)
		local_irq_disable();
	return page;
}

static void __free_slab(struct kmem_cache *s, struct page *page)
{
	int pages = 1 << s->order;

	if (unlikely(SlabDebug(page))) {
		void *p;

		slab_pad_check(s, page);
		for_each_object(p, s, page_address(page))
			check_object(s, page, p, 0);
	}

	mod_zone_page_state(page_zone(page),
		(s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE : NR_SLAB_UNRECLAIMABLE,
		- pages);

	page->mapping = NULL;
	__free_pages(page, s->order);
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
	struct kmem_cache_node *n = get_node(s, page_to_nid(page));

	atomic_long_dec(&n->nr_slabs);
	reset_page_mapcount(page);
	ClearSlabDebug(page);
	__ClearPageSlab(page);
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
	bit_spin_unlock(PG_locked, &page->flags);
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
static void add_partial_tail(struct kmem_cache_node *n, struct page *page)
{
	spin_lock(&n->list_lock);
	n->nr_partial++;
	list_add_tail(&page->lru, &n->partial);
	spin_unlock(&n->list_lock);
}

static void add_partial(struct kmem_cache_node *n, struct page *page)
{
	spin_lock(&n->list_lock);
	n->nr_partial++;
	list_add(&page->lru, &n->partial);
	spin_unlock(&n->list_lock);
}

static void remove_partial(struct kmem_cache *s,
						struct page *page)
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
static inline int lock_and_freeze_slab(struct kmem_cache_node *n, struct page *page)
{
	if (slab_trylock(page)) {
		list_del(&page->lru);
		n->nr_partial--;
		SetSlabFrozen(page);
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
	struct zone **z;
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
	 * If /sys/slab/xx/defrag_ratio is set to 100 (which makes
	 * defrag_ratio = 1000) then every (well almost) allocation will
	 * first attempt to defrag slab caches on other nodes. This means
	 * scanning over all nodes to look for partial slabs which may be
	 * expensive if we do it every time we are trying to find a slab
	 * with available objects.
	 */
	if (!s->defrag_ratio || get_cycles() % 1024 > s->defrag_ratio)
		return NULL;

	zonelist = &NODE_DATA(slab_node(current->mempolicy))
					->node_zonelists[gfp_zone(flags)];
	for (z = zonelist->zones; *z; z++) {
		struct kmem_cache_node *n;

		n = get_node(s, zone_to_nid(*z));

		if (n && cpuset_zone_allowed_hardwall(*z, flags) &&
				n->nr_partial > MIN_PARTIAL) {
			page = get_partial_node(n);
			if (page)
				return page;
		}
	}
#endif
	return NULL;
}

/*
 * Get a partial page, lock it and return it.
 */
static struct page *get_partial(struct kmem_cache *s, gfp_t flags, int node)
{
	struct page *page;
	int searchnode = (node == -1) ? numa_node_id() : node;

	page = get_partial_node(get_node(s, searchnode));
	if (page || (flags & __GFP_THISNODE))
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
static void unfreeze_slab(struct kmem_cache *s, struct page *page)
{
	struct kmem_cache_node *n = get_node(s, page_to_nid(page));

	ClearSlabFrozen(page);
	if (page->inuse) {

		if (page->freelist)
			add_partial(n, page);
		else if (SlabDebug(page) && (s->flags & SLAB_STORE_USER))
			add_full(n, page);
		slab_unlock(page);

	} else {
		if (n->nr_partial < MIN_PARTIAL) {
			/*
			 * Adding an empty slab to the partial slabs in order
			 * to avoid page allocator overhead. This slab needs
			 * to come after the other slabs with objects in
			 * order to fill them up. That way the size of the
			 * partial list stays small. kmem_cache_shrink can
			 * reclaim empty slabs from the partial list.
			 */
			add_partial_tail(n, page);
			slab_unlock(page);
		} else {
			slab_unlock(page);
			discard_slab(s, page);
		}
	}
}

/*
 * Remove the cpu slab
 */
static void deactivate_slab(struct kmem_cache *s, struct page *page, int cpu)
{
	/*
	 * Merge cpu freelist into freelist. Typically we get here
	 * because both freelists are empty. So this is unlikely
	 * to occur.
	 */
	while (unlikely(page->lockless_freelist)) {
		void **object;

		/* Retrieve object from cpu_freelist */
		object = page->lockless_freelist;
		page->lockless_freelist = page->lockless_freelist[page->offset];

		/* And put onto the regular freelist */
		object[page->offset] = page->freelist;
		page->freelist = object;
		page->inuse--;
	}
	s->cpu_slab[cpu] = NULL;
	unfreeze_slab(s, page);
}

static void flush_slab(struct kmem_cache *s, struct page *page, int cpu)
{
	slab_lock(page);
	deactivate_slab(s, page, cpu);
}

/*
 * Flush cpu slab.
 * Called from IPI handler with interrupts disabled.
 */
static void __flush_cpu_slab(struct kmem_cache *s, int cpu)
{
	struct page *page = s->cpu_slab[cpu];

	if (likely(page))
		flush_slab(s, page, cpu);
}

static void flush_cpu_slab(void *d)
{
	struct kmem_cache *s = d;
	int cpu = smp_processor_id();

	__flush_cpu_slab(s, cpu);
}

static void flush_all(struct kmem_cache *s)
{
#ifdef CONFIG_SMP
	on_each_cpu(flush_cpu_slab, s, 1, 1);
#else
	unsigned long flags;

	local_irq_save(flags);
	flush_cpu_slab(s);
	local_irq_restore(flags);
#endif
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
 * we need to allocate a new slab. This is slowest path since we may sleep.
 */
static void *__slab_alloc(struct kmem_cache *s,
		gfp_t gfpflags, int node, void *addr, struct page *page)
{
	void **object;
	int cpu = smp_processor_id();

	if (!page)
		goto new_slab;

	slab_lock(page);
	if (unlikely(node != -1 && page_to_nid(page) != node))
		goto another_slab;
load_freelist:
	object = page->freelist;
	if (unlikely(!object))
		goto another_slab;
	if (unlikely(SlabDebug(page)))
		goto debug;

	object = page->freelist;
	page->lockless_freelist = object[page->offset];
	page->inuse = s->objects;
	page->freelist = NULL;
	slab_unlock(page);
	return object;

another_slab:
	deactivate_slab(s, page, cpu);

new_slab:
	page = get_partial(s, gfpflags, node);
	if (page) {
		s->cpu_slab[cpu] = page;
		goto load_freelist;
	}

	page = new_slab(s, gfpflags, node);
	if (page) {
		cpu = smp_processor_id();
		if (s->cpu_slab[cpu]) {
			/*
			 * Someone else populated the cpu_slab while we
			 * enabled interrupts, or we have gotten scheduled
			 * on another cpu. The page may not be on the
			 * requested node even if __GFP_THISNODE was
			 * specified. So we need to recheck.
			 */
			if (node == -1 ||
				page_to_nid(s->cpu_slab[cpu]) == node) {
				/*
				 * Current cpuslab is acceptable and we
				 * want the current one since its cache hot
				 */
				discard_slab(s, page);
				page = s->cpu_slab[cpu];
				slab_lock(page);
				goto load_freelist;
			}
			/* New slab does not fit our expectations */
			flush_slab(s, s->cpu_slab[cpu], cpu);
		}
		slab_lock(page);
		SetSlabFrozen(page);
		s->cpu_slab[cpu] = page;
		goto load_freelist;
	}
	return NULL;
debug:
	object = page->freelist;
	if (!alloc_debug_processing(s, page, object, addr))
		goto another_slab;

	page->inuse++;
	page->freelist = object[page->offset];
	slab_unlock(page);
	return object;
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
static void __always_inline *slab_alloc(struct kmem_cache *s,
				gfp_t gfpflags, int node, void *addr)
{
	struct page *page;
	void **object;
	unsigned long flags;

	local_irq_save(flags);
	page = s->cpu_slab[smp_processor_id()];
	if (unlikely(!page || !page->lockless_freelist ||
			(node != -1 && page_to_nid(page) != node)))

		object = __slab_alloc(s, gfpflags, node, addr, page);

	else {
		object = page->lockless_freelist;
		page->lockless_freelist = object[page->offset];
	}
	local_irq_restore(flags);
	return object;
}

void *kmem_cache_alloc(struct kmem_cache *s, gfp_t gfpflags)
{
	return slab_alloc(s, gfpflags, -1, __builtin_return_address(0));
}
EXPORT_SYMBOL(kmem_cache_alloc);

#ifdef CONFIG_NUMA
void *kmem_cache_alloc_node(struct kmem_cache *s, gfp_t gfpflags, int node)
{
	return slab_alloc(s, gfpflags, node, __builtin_return_address(0));
}
EXPORT_SYMBOL(kmem_cache_alloc_node);
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
					void *x, void *addr)
{
	void *prior;
	void **object = (void *)x;

	slab_lock(page);

	if (unlikely(SlabDebug(page)))
		goto debug;
checks_ok:
	prior = object[page->offset] = page->freelist;
	page->freelist = object;
	page->inuse--;

	if (unlikely(SlabFrozen(page)))
		goto out_unlock;

	if (unlikely(!page->inuse))
		goto slab_empty;

	/*
	 * Objects left in the slab. If it
	 * was not on the partial list before
	 * then add it.
	 */
	if (unlikely(!prior))
		add_partial(get_node(s, page_to_nid(page)), page);

out_unlock:
	slab_unlock(page);
	return;

slab_empty:
	if (prior)
		/*
		 * Slab still on the partial list.
		 */
		remove_partial(s, page);

	slab_unlock(page);
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
static void __always_inline slab_free(struct kmem_cache *s,
			struct page *page, void *x, void *addr)
{
	void **object = (void *)x;
	unsigned long flags;

	local_irq_save(flags);
	if (likely(page == s->cpu_slab[smp_processor_id()] &&
						!SlabDebug(page))) {
		object[page->offset] = page->lockless_freelist;
		page->lockless_freelist = object;
	} else
		__slab_free(s, page, x, addr);

	local_irq_restore(flags);
}

void kmem_cache_free(struct kmem_cache *s, void *x)
{
	struct page *page;

	page = virt_to_head_page(x);

	slab_free(s, page, x, __builtin_return_address(0));
}
EXPORT_SYMBOL(kmem_cache_free);

/* Figure out on which slab object the object resides */
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
static int slub_max_order = DEFAULT_MAX_ORDER;
static int slub_min_objects = DEFAULT_MIN_OBJECTS;

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
 * unused space left. We go to a higher order if more than 1/8th of the slab
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

	for (order = max(slub_min_order,
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

	/*
	 * Attempt to find best configuration for a slab. This
	 * works by first attempting to generate a layout with
	 * the best configuration and backing off gradually.
	 *
	 * First we reduce the acceptable waste in a slab. Then
	 * we reduce the minimum objects required in a slab.
	 */
	min_objects = slub_min_objects;
	while (min_objects > 1) {
		fraction = 8;
		while (fraction >= 4) {
			order = slab_order(size, min_objects,
						slub_max_order, fraction);
			if (order <= slub_max_order)
				return order;
			fraction /= 2;
		}
		min_objects /= 2;
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
	if (order <= MAX_ORDER)
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
	 * If the user wants hardware cache aligned objects then
	 * follow that suggestion if the object is sufficiently
	 * large.
	 *
	 * The hardware cache alignment cannot override the
	 * specified alignment though. If that is greater
	 * then use it.
	 */
	if ((flags & SLAB_HWCACHE_ALIGN) &&
			size > cache_line_size() / 2)
		return max_t(unsigned long, align, cache_line_size());

	if (align < ARCH_SLAB_MINALIGN)
		return ARCH_SLAB_MINALIGN;

	return ALIGN(align, sizeof(void *));
}

static void init_kmem_cache_node(struct kmem_cache_node *n)
{
	n->nr_partial = 0;
	atomic_long_set(&n->nr_slabs, 0);
	spin_lock_init(&n->list_lock);
	INIT_LIST_HEAD(&n->partial);
	INIT_LIST_HEAD(&n->full);
}

#ifdef CONFIG_NUMA
/*
 * No kmalloc_node yet so do it by hand. We know that this is the first
 * slab on the node for this slabcache. There are no concurrent accesses
 * possible.
 *
 * Note that this function only works on the kmalloc_node_cache
 * when allocating for the kmalloc_node_cache.
 */
static struct kmem_cache_node * __init early_kmem_cache_node_alloc(gfp_t gfpflags,
								int node)
{
	struct page *page;
	struct kmem_cache_node *n;

	BUG_ON(kmalloc_caches->size < sizeof(struct kmem_cache_node));

	page = new_slab(kmalloc_caches, gfpflags | GFP_THISNODE, node);
	/* new_slab() disables interupts */
	local_irq_enable();

	BUG_ON(!page);
	n = page->freelist;
	BUG_ON(!n);
	page->freelist = get_freepointer(kmalloc_caches, n);
	page->inuse++;
	kmalloc_caches->node[node] = n;
	setup_object_debug(kmalloc_caches, page, n);
	init_kmem_cache_node(n);
	atomic_long_inc(&n->nr_slabs);
	add_partial(n, page);
	return n;
}

static void free_kmem_cache_nodes(struct kmem_cache *s)
{
	int node;

	for_each_online_node(node) {
		struct kmem_cache_node *n = s->node[node];
		if (n && n != &s->local_node)
			kmem_cache_free(kmalloc_caches, n);
		s->node[node] = NULL;
	}
}

static int init_kmem_cache_nodes(struct kmem_cache *s, gfp_t gfpflags)
{
	int node;
	int local_node;

	if (slab_state >= UP)
		local_node = page_to_nid(virt_to_page(s));
	else
		local_node = 0;

	for_each_online_node(node) {
		struct kmem_cache_node *n;

		if (local_node == node)
			n = &s->local_node;
		else {
			if (slab_state == DOWN) {
				n = early_kmem_cache_node_alloc(gfpflags,
								node);
				continue;
			}
			n = kmem_cache_alloc_node(kmalloc_caches,
							gfpflags, node);

			if (!n) {
				free_kmem_cache_nodes(s);
				return 0;
			}

		}
		s->node[node] = n;
		init_kmem_cache_node(n);
	}
	return 1;
}
#else
static void free_kmem_cache_nodes(struct kmem_cache *s)
{
}

static int init_kmem_cache_nodes(struct kmem_cache *s, gfp_t gfpflags)
{
	init_kmem_cache_node(&s->local_node);
	return 1;
}
#endif

/*
 * calculate_sizes() determines the order and the distribution of data within
 * a slab object.
 */
static int calculate_sizes(struct kmem_cache *s)
{
	unsigned long flags = s->flags;
	unsigned long size = s->objsize;
	unsigned long align = s->align;

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
	 * Round up object size to the next word boundary. We can only
	 * place the free pointer at word boundaries and this determines
	 * the possible location of the free pointer.
	 */
	size = ALIGN(size, sizeof(void *));

#ifdef CONFIG_SLUB_DEBUG
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
		 * corrupted if an user writes before the start
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

	/*
	 * SLUB stores one object immediately after another beginning from
	 * offset 0. In order to align the objects we have to simply size
	 * each object to conform to the alignment.
	 */
	size = ALIGN(size, align);
	s->size = size;

	s->order = calculate_order(size);
	if (s->order < 0)
		return 0;

	/*
	 * Determine the number of objects per slab
	 */
	s->objects = (PAGE_SIZE << s->order) / size;

	/*
	 * Verify that the number of objects is within permitted limits.
	 * The page->inuse field is only 16 bit wide! So we cannot have
	 * more than 64k objects per slab.
	 */
	if (!s->objects || s->objects > 65535)
		return 0;
	return 1;

}

static int kmem_cache_open(struct kmem_cache *s, gfp_t gfpflags,
		const char *name, size_t size,
		size_t align, unsigned long flags,
		void (*ctor)(void *, struct kmem_cache *, unsigned long))
{
	memset(s, 0, kmem_size);
	s->name = name;
	s->ctor = ctor;
	s->objsize = size;
	s->flags = flags;
	s->align = align;
	kmem_cache_open_debug_check(s);

	if (!calculate_sizes(s))
		goto error;

	s->refcount = 1;
#ifdef CONFIG_NUMA
	s->defrag_ratio = 100;
#endif

	if (init_kmem_cache_nodes(s, gfpflags & ~SLUB_DMA))
		return 1;
error:
	if (flags & SLAB_PANIC)
		panic("Cannot create slab %s size=%lu realsize=%u "
			"order=%u offset=%u flags=%lx\n",
			s->name, (unsigned long)size, s->size, s->order,
			s->offset, flags);
	return 0;
}
EXPORT_SYMBOL(kmem_cache_open);

/*
 * Check if a given pointer is valid
 */
int kmem_ptr_validate(struct kmem_cache *s, const void *object)
{
	struct page * page;

	page = get_object_page(object);

	if (!page || s != page->slab)
		/* No slab or wrong slab */
		return 0;

	if (!check_valid_pointer(s, page, object))
		return 0;

	/*
	 * We could also check if the object is on the slabs freelist.
	 * But this would be too expensive and it seems that the main
	 * purpose of kmem_ptr_valid is to check if the object belongs
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

/*
 * Attempt to free all slabs on a node. Return the number of slabs we
 * were unable to free.
 */
static int free_list(struct kmem_cache *s, struct kmem_cache_node *n,
			struct list_head *list)
{
	int slabs_inuse = 0;
	unsigned long flags;
	struct page *page, *h;

	spin_lock_irqsave(&n->list_lock, flags);
	list_for_each_entry_safe(page, h, list, lru)
		if (!page->inuse) {
			list_del(&page->lru);
			discard_slab(s, page);
		} else
			slabs_inuse++;
	spin_unlock_irqrestore(&n->list_lock, flags);
	return slabs_inuse;
}

/*
 * Release all resources used by a slab cache.
 */
static int kmem_cache_close(struct kmem_cache *s)
{
	int node;

	flush_all(s);

	/* Attempt to free all objects */
	for_each_online_node(node) {
		struct kmem_cache_node *n = get_node(s, node);

		n->nr_partial -= free_list(s, n, &n->partial);
		if (atomic_long_read(&n->nr_slabs))
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
		if (kmem_cache_close(s))
			WARN_ON(1);
		sysfs_slab_remove(s);
		kfree(s);
	}
	up_write(&slub_lock);
}
EXPORT_SYMBOL(kmem_cache_destroy);

/********************************************************************
 *		Kmalloc subsystem
 *******************************************************************/

struct kmem_cache kmalloc_caches[KMALLOC_SHIFT_HIGH + 1] __cacheline_aligned;
EXPORT_SYMBOL(kmalloc_caches);

#ifdef CONFIG_ZONE_DMA
static struct kmem_cache *kmalloc_caches_dma[KMALLOC_SHIFT_HIGH + 1];
#endif

static int __init setup_slub_min_order(char *str)
{
	get_option (&str, &slub_min_order);

	return 1;
}

__setup("slub_min_order=", setup_slub_min_order);

static int __init setup_slub_max_order(char *str)
{
	get_option (&str, &slub_max_order);

	return 1;
}

__setup("slub_max_order=", setup_slub_max_order);

static int __init setup_slub_min_objects(char *str)
{
	get_option (&str, &slub_min_objects);

	return 1;
}

__setup("slub_min_objects=", setup_slub_min_objects);

static int __init setup_slub_nomerge(char *str)
{
	slub_nomerge = 1;
	return 1;
}

__setup("slub_nomerge", setup_slub_nomerge);

static struct kmem_cache *create_kmalloc_cache(struct kmem_cache *s,
		const char *name, int size, gfp_t gfp_flags)
{
	unsigned int flags = 0;

	if (gfp_flags & SLUB_DMA)
		flags = SLAB_CACHE_DMA;

	down_write(&slub_lock);
	if (!kmem_cache_open(s, gfp_flags, name, size, ARCH_KMALLOC_MINALIGN,
			flags, NULL))
		goto panic;

	list_add(&s->list, &slab_caches);
	up_write(&slub_lock);
	if (sysfs_slab_add(s))
		goto panic;
	return s;

panic:
	panic("Creation of kmalloc slab %s size=%d failed.\n", name, size);
}

static struct kmem_cache *get_slab(size_t size, gfp_t flags)
{
	int index = kmalloc_index(size);

	if (!index)
		return NULL;

	/* Allocation too large? */
	BUG_ON(index < 0);

#ifdef CONFIG_ZONE_DMA
	if ((flags & SLUB_DMA)) {
		struct kmem_cache *s;
		struct kmem_cache *x;
		char *text;
		size_t realsize;

		s = kmalloc_caches_dma[index];
		if (s)
			return s;

		/* Dynamically create dma cache */
		x = kmalloc(kmem_size, flags & ~SLUB_DMA);
		if (!x)
			panic("Unable to allocate memory for dma cache\n");

		if (index <= KMALLOC_SHIFT_HIGH)
			realsize = 1 << index;
		else {
			if (index == 1)
				realsize = 96;
			else
				realsize = 192;
		}

		text = kasprintf(flags & ~SLUB_DMA, "kmalloc_dma-%d",
				(unsigned int)realsize);
		s = create_kmalloc_cache(x, text, realsize, flags);
		kmalloc_caches_dma[index] = s;
		return s;
	}
#endif
	return &kmalloc_caches[index];
}

void *__kmalloc(size_t size, gfp_t flags)
{
	struct kmem_cache *s = get_slab(size, flags);

	if (s)
		return slab_alloc(s, flags, -1, __builtin_return_address(0));
	return NULL;
}
EXPORT_SYMBOL(__kmalloc);

#ifdef CONFIG_NUMA
void *__kmalloc_node(size_t size, gfp_t flags, int node)
{
	struct kmem_cache *s = get_slab(size, flags);

	if (s)
		return slab_alloc(s, flags, node, __builtin_return_address(0));
	return NULL;
}
EXPORT_SYMBOL(__kmalloc_node);
#endif

size_t ksize(const void *object)
{
	struct page *page = get_object_page(object);
	struct kmem_cache *s;

	BUG_ON(!page);
	s = page->slab;
	BUG_ON(!s);

	/*
	 * Debugging requires use of the padding between object
	 * and whatever may come after it.
	 */
	if (s->flags & (SLAB_RED_ZONE | SLAB_POISON))
		return s->objsize;

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
	struct kmem_cache *s;
	struct page *page;

	if (!x)
		return;

	page = virt_to_head_page(x);
	s = page->slab;

	slab_free(s, page, (void *)x, __builtin_return_address(0));
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
	struct list_head *slabs_by_inuse =
		kmalloc(sizeof(struct list_head) * s->objects, GFP_KERNEL);
	unsigned long flags;

	if (!slabs_by_inuse)
		return -ENOMEM;

	flush_all(s);
	for_each_online_node(node) {
		n = get_node(s, node);

		if (!n->nr_partial)
			continue;

		for (i = 0; i < s->objects; i++)
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
				if (n->nr_partial > MAX_PARTIAL)
					list_move(&page->lru,
					slabs_by_inuse + page->inuse);
			}
		}

		if (n->nr_partial <= MAX_PARTIAL)
			goto out;

		/*
		 * Rebuild the partial list with the slabs filled up most
		 * first and the least used slabs at the end.
		 */
		for (i = s->objects - 1; i >= 0; i--)
			list_splice(slabs_by_inuse + i, n->partial.prev);

	out:
		spin_unlock_irqrestore(&n->list_lock, flags);
	}

	kfree(slabs_by_inuse);
	return 0;
}
EXPORT_SYMBOL(kmem_cache_shrink);

/**
 * krealloc - reallocate memory. The contents will remain unchanged.
 * @p: object to reallocate memory for.
 * @new_size: how many bytes of memory are required.
 * @flags: the type of memory to allocate.
 *
 * The contents of the object pointed to are preserved up to the
 * lesser of the new and old sizes.  If @p is %NULL, krealloc()
 * behaves exactly like kmalloc().  If @size is 0 and @p is not a
 * %NULL pointer, the object pointed to is freed.
 */
void *krealloc(const void *p, size_t new_size, gfp_t flags)
{
	void *ret;
	size_t ks;

	if (unlikely(!p))
		return kmalloc(new_size, flags);

	if (unlikely(!new_size)) {
		kfree(p);
		return NULL;
	}

	ks = ksize(p);
	if (ks >= new_size)
		return (void *)p;

	ret = kmalloc(new_size, flags);
	if (ret) {
		memcpy(ret, p, min(new_size, ks));
		kfree(p);
	}
	return ret;
}
EXPORT_SYMBOL(krealloc);

/********************************************************************
 *			Basic setup of slabs
 *******************************************************************/

void __init kmem_cache_init(void)
{
	int i;

#ifdef CONFIG_NUMA
	/*
	 * Must first have the slab cache available for the allocations of the
	 * struct kmem_cache_node's. There is special bootstrap code in
	 * kmem_cache_open for slab_state == DOWN.
	 */
	create_kmalloc_cache(&kmalloc_caches[0], "kmem_cache_node",
		sizeof(struct kmem_cache_node), GFP_KERNEL);
#endif

	/* Able to allocate the per node structures */
	slab_state = PARTIAL;

	/* Caches that are not of the two-to-the-power-of size */
	create_kmalloc_cache(&kmalloc_caches[1],
				"kmalloc-96", 96, GFP_KERNEL);
	create_kmalloc_cache(&kmalloc_caches[2],
				"kmalloc-192", 192, GFP_KERNEL);

	for (i = KMALLOC_SHIFT_LOW; i <= KMALLOC_SHIFT_HIGH; i++)
		create_kmalloc_cache(&kmalloc_caches[i],
			"kmalloc", 1 << i, GFP_KERNEL);

	slab_state = UP;

	/* Provide the correct kmalloc names now that the caches are up */
	for (i = KMALLOC_SHIFT_LOW; i <= KMALLOC_SHIFT_HIGH; i++)
		kmalloc_caches[i]. name =
			kasprintf(GFP_KERNEL, "kmalloc-%d", 1 << i);

#ifdef CONFIG_SMP
	register_cpu_notifier(&slab_notifier);
#endif

	kmem_size = offsetof(struct kmem_cache, cpu_slab) +
				nr_cpu_ids * sizeof(struct page *);

	printk(KERN_INFO "SLUB: Genslabs=%d, HWalign=%d, Order=%d-%d, MinObjects=%d,"
		" Processors=%d, Nodes=%d\n",
		KMALLOC_SHIFT_HIGH, cache_line_size(),
		slub_min_order, slub_max_order, slub_min_objects,
		nr_cpu_ids, nr_node_ids);
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

	return 0;
}

static struct kmem_cache *find_mergeable(size_t size,
		size_t align, unsigned long flags,
		void (*ctor)(void *, struct kmem_cache *, unsigned long))
{
	struct list_head *h;

	if (slub_nomerge || (flags & SLUB_NEVER_MERGE))
		return NULL;

	if (ctor)
		return NULL;

	size = ALIGN(size, sizeof(void *));
	align = calculate_alignment(flags, align, size);
	size = ALIGN(size, align);

	list_for_each(h, &slab_caches) {
		struct kmem_cache *s =
			container_of(h, struct kmem_cache, list);

		if (slab_unmergeable(s))
			continue;

		if (size > s->size)
			continue;

		if (((flags | slub_debug) & SLUB_MERGE_SAME) !=
			(s->flags & SLUB_MERGE_SAME))
				continue;
		/*
		 * Check if alignment is compatible.
		 * Courtesy of Adrian Drzewiecki
		 */
		if ((s->size & ~(align -1)) != s->size)
			continue;

		if (s->size - size >= sizeof(void *))
			continue;

		return s;
	}
	return NULL;
}

struct kmem_cache *kmem_cache_create(const char *name, size_t size,
		size_t align, unsigned long flags,
		void (*ctor)(void *, struct kmem_cache *, unsigned long),
		void (*dtor)(void *, struct kmem_cache *, unsigned long))
{
	struct kmem_cache *s;

	BUG_ON(dtor);
	down_write(&slub_lock);
	s = find_mergeable(size, align, flags, ctor);
	if (s) {
		s->refcount++;
		/*
		 * Adjust the object sizes so that we clear
		 * the complete object on kzalloc.
		 */
		s->objsize = max(s->objsize, (int)size);
		s->inuse = max_t(int, s->inuse, ALIGN(size, sizeof(void *)));
		if (sysfs_slab_alias(s, name))
			goto err;
	} else {
		s = kmalloc(kmem_size, GFP_KERNEL);
		if (s && kmem_cache_open(s, GFP_KERNEL, name,
				size, align, flags, ctor)) {
			if (sysfs_slab_add(s)) {
				kfree(s);
				goto err;
			}
			list_add(&s->list, &slab_caches);
		} else
			kfree(s);
	}
	up_write(&slub_lock);
	return s;

err:
	up_write(&slub_lock);
	if (flags & SLAB_PANIC)
		panic("Cannot create slabcache %s\n", name);
	else
		s = NULL;
	return s;
}
EXPORT_SYMBOL(kmem_cache_create);

void *kmem_cache_zalloc(struct kmem_cache *s, gfp_t flags)
{
	void *x;

	x = slab_alloc(s, flags, -1, __builtin_return_address(0));
	if (x)
		memset(x, 0, s->objsize);
	return x;
}
EXPORT_SYMBOL(kmem_cache_zalloc);

#ifdef CONFIG_SMP
static void for_all_slabs(void (*func)(struct kmem_cache *, int), int cpu)
{
	struct list_head *h;

	down_read(&slub_lock);
	list_for_each(h, &slab_caches) {
		struct kmem_cache *s =
			container_of(h, struct kmem_cache, list);

		func(s, cpu);
	}
	up_read(&slub_lock);
}

/*
 * Use the cpu notifier to insure that the cpu slabs are flushed when
 * necessary.
 */
static int __cpuinit slab_cpuup_callback(struct notifier_block *nfb,
		unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		for_all_slabs(__flush_cpu_slab, cpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata slab_notifier =
	{ &slab_cpuup_callback, NULL, 0 };

#endif

void *__kmalloc_track_caller(size_t size, gfp_t gfpflags, void *caller)
{
	struct kmem_cache *s = get_slab(size, gfpflags);

	if (!s)
		return NULL;

	return slab_alloc(s, gfpflags, -1, caller);
}

void *__kmalloc_node_track_caller(size_t size, gfp_t gfpflags,
					int node, void *caller)
{
	struct kmem_cache *s = get_slab(size, gfpflags);

	if (!s)
		return NULL;

	return slab_alloc(s, gfpflags, node, caller);
}

#if defined(CONFIG_SYSFS) && defined(CONFIG_SLUB_DEBUG)
static int validate_slab(struct kmem_cache *s, struct page *page)
{
	void *p;
	void *addr = page_address(page);
	DECLARE_BITMAP(map, s->objects);

	if (!check_slab(s, page) ||
			!on_freelist(s, page, NULL))
		return 0;

	/* Now we know that a valid freelist exists */
	bitmap_zero(map, s->objects);

	for_each_free_object(p, s, page->freelist) {
		set_bit(slab_index(p, s, addr), map);
		if (!check_object(s, page, p, 0))
			return 0;
	}

	for_each_object(p, s, addr)
		if (!test_bit(slab_index(p, s, addr), map))
			if (!check_object(s, page, p, 1))
				return 0;
	return 1;
}

static void validate_slab_slab(struct kmem_cache *s, struct page *page)
{
	if (slab_trylock(page)) {
		validate_slab(s, page);
		slab_unlock(page);
	} else
		printk(KERN_INFO "SLUB %s: Skipped busy slab 0x%p\n",
			s->name, page);

	if (s->flags & DEBUG_DEFAULT_FLAGS) {
		if (!SlabDebug(page))
			printk(KERN_ERR "SLUB %s: SlabDebug not set "
				"on slab 0x%p\n", s->name, page);
	} else {
		if (SlabDebug(page))
			printk(KERN_ERR "SLUB %s: SlabDebug set on "
				"slab 0x%p\n", s->name, page);
	}
}

static int validate_slab_node(struct kmem_cache *s, struct kmem_cache_node *n)
{
	unsigned long count = 0;
	struct page *page;
	unsigned long flags;

	spin_lock_irqsave(&n->list_lock, flags);

	list_for_each_entry(page, &n->partial, lru) {
		validate_slab_slab(s, page);
		count++;
	}
	if (count != n->nr_partial)
		printk(KERN_ERR "SLUB %s: %ld partial slabs counted but "
			"counter=%ld\n", s->name, count, n->nr_partial);

	if (!(s->flags & SLAB_STORE_USER))
		goto out;

	list_for_each_entry(page, &n->full, lru) {
		validate_slab_slab(s, page);
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

static unsigned long validate_slab_cache(struct kmem_cache *s)
{
	int node;
	unsigned long count = 0;

	flush_all(s);
	for_each_online_node(node) {
		struct kmem_cache_node *n = get_node(s, node);

		count += validate_slab_node(s, n);
	}
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
	printk(KERN_ERR "If allocated object is overwritten then not detectable\n\n");

	validate_slab_cache(kmalloc_caches + 5);
	p = kzalloc(64, GFP_KERNEL);
	p += 64 + (get_cycles() & 0xff) * sizeof(void *);
	*p = 0x56;
	printk(KERN_ERR "\n3. kmalloc-64: corrupting random byte 0x56->0x%p\n",
									p);
	printk(KERN_ERR "If allocated object is overwritten then not detectable\n\n");
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
	printk(KERN_ERR "\n2. kmalloc-256: Clobber 50th byte 0x9a->0x%p\n\n", p);
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
	void *addr;
	long long sum_time;
	long min_time;
	long max_time;
	long min_pid;
	long max_pid;
	cpumask_t cpus;
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

static int alloc_loc_track(struct loc_track *t, unsigned long max)
{
	struct location *l;
	int order;

	if (!max)
		max = PAGE_SIZE / sizeof(struct location);

	order = get_order(sizeof(struct location) * max);

	l = (void *)__get_free_pages(GFP_KERNEL, order);

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
	void *caddr;
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

				cpu_set(track->cpu, l->cpus);
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
	if (t->count >= t->max && !alloc_loc_track(t, 2 * t->max))
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
	cpus_clear(l->cpus);
	cpu_set(track->cpu, l->cpus);
	nodes_clear(l->nodes);
	node_set(page_to_nid(virt_to_page(track)), l->nodes);
	return 1;
}

static void process_slab(struct loc_track *t, struct kmem_cache *s,
		struct page *page, enum track_item alloc)
{
	void *addr = page_address(page);
	DECLARE_BITMAP(map, s->objects);
	void *p;

	bitmap_zero(map, s->objects);
	for_each_free_object(p, s, page->freelist)
		set_bit(slab_index(p, s, addr), map);

	for_each_object(p, s, addr)
		if (!test_bit(slab_index(p, s, addr), map))
			add_location(t, s, get_track(s, p, alloc));
}

static int list_locations(struct kmem_cache *s, char *buf,
					enum track_item alloc)
{
	int n = 0;
	unsigned long i;
	struct loc_track t;
	int node;

	t.count = 0;
	t.max = 0;

	/* Push back cpu slabs */
	flush_all(s);

	for_each_online_node(node) {
		struct kmem_cache_node *n = get_node(s, node);
		unsigned long flags;
		struct page *page;

		if (!atomic_read(&n->nr_slabs))
			continue;

		spin_lock_irqsave(&n->list_lock, flags);
		list_for_each_entry(page, &n->partial, lru)
			process_slab(&t, s, page, alloc);
		list_for_each_entry(page, &n->full, lru)
			process_slab(&t, s, page, alloc);
		spin_unlock_irqrestore(&n->list_lock, flags);
	}

	for (i = 0; i < t.count; i++) {
		struct location *l = &t.loc[i];

		if (n > PAGE_SIZE - 100)
			break;
		n += sprintf(buf + n, "%7ld ", l->count);

		if (l->addr)
			n += sprint_symbol(buf + n, (unsigned long)l->addr);
		else
			n += sprintf(buf + n, "<not-available>");

		if (l->sum_time != l->min_time) {
			unsigned long remainder;

			n += sprintf(buf + n, " age=%ld/%ld/%ld",
			l->min_time,
			div_long_long_rem(l->sum_time, l->count, &remainder),
			l->max_time);
		} else
			n += sprintf(buf + n, " age=%ld",
				l->min_time);

		if (l->min_pid != l->max_pid)
			n += sprintf(buf + n, " pid=%ld-%ld",
				l->min_pid, l->max_pid);
		else
			n += sprintf(buf + n, " pid=%ld",
				l->min_pid);

		if (num_online_cpus() > 1 && !cpus_empty(l->cpus)) {
			n += sprintf(buf + n, " cpus=");
			n += cpulist_scnprintf(buf + n, PAGE_SIZE - n - 50,
					l->cpus);
		}

		if (num_online_nodes() > 1 && !nodes_empty(l->nodes)) {
			n += sprintf(buf + n, " nodes=");
			n += nodelist_scnprintf(buf + n, PAGE_SIZE - n - 50,
					l->nodes);
		}

		n += sprintf(buf + n, "\n");
	}

	free_loc_track(&t);
	if (!t.count)
		n += sprintf(buf, "No data\n");
	return n;
}

static unsigned long count_partial(struct kmem_cache_node *n)
{
	unsigned long flags;
	unsigned long x = 0;
	struct page *page;

	spin_lock_irqsave(&n->list_lock, flags);
	list_for_each_entry(page, &n->partial, lru)
		x += page->inuse;
	spin_unlock_irqrestore(&n->list_lock, flags);
	return x;
}

enum slab_stat_type {
	SL_FULL,
	SL_PARTIAL,
	SL_CPU,
	SL_OBJECTS
};

#define SO_FULL		(1 << SL_FULL)
#define SO_PARTIAL	(1 << SL_PARTIAL)
#define SO_CPU		(1 << SL_CPU)
#define SO_OBJECTS	(1 << SL_OBJECTS)

static unsigned long slab_objects(struct kmem_cache *s,
			char *buf, unsigned long flags)
{
	unsigned long total = 0;
	int cpu;
	int node;
	int x;
	unsigned long *nodes;
	unsigned long *per_cpu;

	nodes = kzalloc(2 * sizeof(unsigned long) * nr_node_ids, GFP_KERNEL);
	per_cpu = nodes + nr_node_ids;

	for_each_possible_cpu(cpu) {
		struct page *page = s->cpu_slab[cpu];
		int node;

		if (page) {
			node = page_to_nid(page);
			if (flags & SO_CPU) {
				int x = 0;

				if (flags & SO_OBJECTS)
					x = page->inuse;
				else
					x = 1;
				total += x;
				nodes[node] += x;
			}
			per_cpu[node]++;
		}
	}

	for_each_online_node(node) {
		struct kmem_cache_node *n = get_node(s, node);

		if (flags & SO_PARTIAL) {
			if (flags & SO_OBJECTS)
				x = count_partial(n);
			else
				x = n->nr_partial;
			total += x;
			nodes[node] += x;
		}

		if (flags & SO_FULL) {
			int full_slabs = atomic_read(&n->nr_slabs)
					- per_cpu[node]
					- n->nr_partial;

			if (flags & SO_OBJECTS)
				x = full_slabs * s->objects;
			else
				x = full_slabs;
			total += x;
			nodes[node] += x;
		}
	}

	x = sprintf(buf, "%lu", total);
#ifdef CONFIG_NUMA
	for_each_online_node(node)
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
	int cpu;

	for_each_possible_cpu(cpu)
		if (s->cpu_slab[cpu])
			return 1;

	for_each_node(node) {
		struct kmem_cache_node *n = get_node(s, node);

		if (n->nr_partial || atomic_read(&n->nr_slabs))
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
	return sprintf(buf, "%d\n", s->objects);
}
SLAB_ATTR_RO(objs_per_slab);

static ssize_t order_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", s->order);
}
SLAB_ATTR_RO(order);

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
	return slab_objects(s, buf, SO_FULL|SO_PARTIAL|SO_CPU);
}
SLAB_ATTR_RO(slabs);

static ssize_t partial_show(struct kmem_cache *s, char *buf)
{
	return slab_objects(s, buf, SO_PARTIAL);
}
SLAB_ATTR_RO(partial);

static ssize_t cpu_slabs_show(struct kmem_cache *s, char *buf)
{
	return slab_objects(s, buf, SO_CPU);
}
SLAB_ATTR_RO(cpu_slabs);

static ssize_t objects_show(struct kmem_cache *s, char *buf)
{
	return slab_objects(s, buf, SO_FULL|SO_PARTIAL|SO_CPU|SO_OBJECTS);
}
SLAB_ATTR_RO(objects);

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
	calculate_sizes(s);
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
	calculate_sizes(s);
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
	calculate_sizes(s);
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
	if (buf[0] == '1')
		validate_slab_cache(s);
	else
		return -EINVAL;
	return length;
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
static ssize_t defrag_ratio_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", s->defrag_ratio / 10);
}

static ssize_t defrag_ratio_store(struct kmem_cache *s,
				const char *buf, size_t length)
{
	int n = simple_strtoul(buf, NULL, 10);

	if (n < 100)
		s->defrag_ratio = n * 10;
	return length;
}
SLAB_ATTR(defrag_ratio);
#endif

static struct attribute * slab_attrs[] = {
	&slab_size_attr.attr,
	&object_size_attr.attr,
	&objs_per_slab_attr.attr,
	&order_attr.attr,
	&objects_attr.attr,
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
	&defrag_ratio_attr.attr,
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

static struct sysfs_ops slab_sysfs_ops = {
	.show = slab_attr_show,
	.store = slab_attr_store,
};

static struct kobj_type slab_ktype = {
	.sysfs_ops = &slab_sysfs_ops,
};

static int uevent_filter(struct kset *kset, struct kobject *kobj)
{
	struct kobj_type *ktype = get_ktype(kobj);

	if (ktype == &slab_ktype)
		return 1;
	return 0;
}

static struct kset_uevent_ops slab_uevent_ops = {
	.filter = uevent_filter,
};

decl_subsys(slab, &slab_ktype, &slab_uevent_ops);

#define ID_STR_LENGTH 64

/* Create a unique string id for a slab cache:
 * format
 * :[flags-]size:[memory address of kmemcache]
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
		sysfs_remove_link(&slab_subsys.kobj, s->name);
		name = s->name;
	} else {
		/*
		 * Create a unique name for the slab as a target
		 * for the symlinks.
		 */
		name = create_unique_id(s);
	}

	kobj_set_kset_s(s, slab_subsys);
	kobject_set_name(&s->kobj, name);
	kobject_init(&s->kobj);
	err = kobject_add(&s->kobj);
	if (err)
		return err;

	err = sysfs_create_group(&s->kobj, &slab_attr_group);
	if (err)
		return err;
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
	kobject_uevent(&s->kobj, KOBJ_REMOVE);
	kobject_del(&s->kobj);
}

/*
 * Need to buffer aliases during bootup until sysfs becomes
 * available lest we loose that information.
 */
struct saved_alias {
	struct kmem_cache *s;
	const char *name;
	struct saved_alias *next;
};

struct saved_alias *alias_list;

static int sysfs_slab_alias(struct kmem_cache *s, const char *name)
{
	struct saved_alias *al;

	if (slab_state == SYSFS) {
		/*
		 * If we have a leftover link then remove it.
		 */
		sysfs_remove_link(&slab_subsys.kobj, name);
		return sysfs_create_link(&slab_subsys.kobj,
						&s->kobj, name);
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
	struct list_head *h;
	int err;

	err = subsystem_register(&slab_subsys);
	if (err) {
		printk(KERN_ERR "Cannot register slab subsystem.\n");
		return -ENOSYS;
	}

	slab_state = SYSFS;

	list_for_each(h, &slab_caches) {
		struct kmem_cache *s =
			container_of(h, struct kmem_cache, list);

		err = sysfs_slab_add(s);
		BUG_ON(err);
	}

	while (alias_list) {
		struct saved_alias *al = alias_list;

		alias_list = alias_list->next;
		err = sysfs_slab_alias(al->s, al->name);
		BUG_ON(err);
		kfree(al);
	}

	resiliency_test();
	return 0;
}

__initcall(slab_sysfs_init);
#endif
