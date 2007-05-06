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
 * Slabs with free elements are kept on a partial list.
 * There is no list for full slabs. If an object in a full slab is
 * freed then the slab will show up again on the partial lists.
 * Otherwise there is no need to track full slabs unless we have to
 * track full slabs for debugging purposes.
 *
 * Slabs are freed when they become empty. Teardown and setup is
 * minimal so we rely on the page allocators per cpu caches for
 * fast frees and allocs.
 *
 * Overloading of page flags that are otherwise used for LRU management.
 *
 * PageActive 		The slab is used as a cpu cache. Allocations
 * 			may be performed from the slab. The slab is not
 * 			on any slab list and cannot be moved onto one.
 *
 * PageError		Slab requires special handling due to debug
 * 			options set. This moves	slab handling out of
 * 			the fast path.
 */

/*
 * Issues still to be resolved:
 *
 * - The per cpu array is updated for each new slab and and is a remote
 *   cacheline for most nodes. This could become a bouncing cacheline given
 *   enough frequent updates. There are 16 pointers in a cacheline.so at
 *   max 16 cpus could compete. Likely okay.
 *
 * - Support PAGE_ALLOC_DEBUG. Should be easy to do.
 *
 * - Support DEBUG_SLAB_LEAK. Trouble is we do not know where the full
 *   slabs are in SLUB.
 *
 * - SLAB_DEBUG_INITIAL is not supported but I have never seen a use of
 *   it.
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
 * Flags from the regular SLAB that SLUB does not support:
 */
#define SLUB_UNIMPLEMENTED (SLAB_DEBUG_INITIAL)

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

static int kmem_size = sizeof(struct kmem_cache);

#ifdef CONFIG_SMP
static struct notifier_block slab_notifier;
#endif

static enum {
	DOWN,		/* No slab functionality available */
	PARTIAL,	/* kmem_cache_open() works but kmalloc does not */
	UP,		/* Everything works */
	SYSFS		/* Sysfs up */
} slab_state = DOWN;

/* A list of all slab caches on the system */
static DECLARE_RWSEM(slub_lock);
LIST_HEAD(slab_caches);

#ifdef CONFIG_SYSFS
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

/*
 * Slow version of get and set free pointer.
 *
 * This requires touching the cache lines of kmem_cache.
 * The offset can also be obtained from the page. In that
 * case it is in the cacheline that we already need to touch.
 */
static void *get_freepointer(struct kmem_cache *s, void *object)
{
	return *(void **)(object + s->offset);
}

static void set_freepointer(struct kmem_cache *s, void *object, void *fp)
{
	*(void **)(object + s->offset) = fp;
}

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

#define set_tracking(__s, __o, __a) set_track(__s, __o, __a, \
			__builtin_return_address(0))

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


static int check_valid_pointer(struct kmem_cache *s, struct page *page,
					 void *object)
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
 * Object layout:
 *
 * object address
 * 	Bytes of the object to be managed.
 * 	If the freepointer may overlay the object then the free
 * 	pointer is the first word of the object.
 * 	Poisoning uses 0x6b (POISON_FREE) and the last byte is
 * 	0xa5 (POISON_END)
 *
 * object + s->objsize
 * 	Padding to reach word boundary. This is also used for Redzoning.
 * 	Padding is extended to word size if Redzoning is enabled
 * 	and objsize == inuse.
 * 	We fill with 0xbb (RED_INACTIVE) for inactive objects and with
 * 	0xcc (RED_ACTIVE) for objects in use.
 *
 * object + s->inuse
 * 	A. Free pointer (if we cannot overwrite object on free)
 * 	B. Tracking data for SLAB_STORE_USER
 * 	C. Padding to reach required alignment boundary
 * 		Padding is done using 0x5a (POISON_INUSE)
 *
 * object + s->size
 *
 * If slabcaches are merged then the objsize and inuse boundaries are to
 * be ignored. And therefore no slab options that rely on these boundaries
 * may be used with merged slabcaches.
 */

static void restore_bytes(struct kmem_cache *s, char *message, u8 data,
						void *from, void *to)
{
	printk(KERN_ERR "@@@ SLUB: %s Restoring %s (0x%x) from 0x%p-0x%p\n",
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
		printk(KERN_ERR "SLUB: %s slab 0x%p: Padding fails check\n",
			s->name, p);
		dump_stack();
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
		 * another error because the object count maybe
		 * wrong now.
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
		printk(KERN_ERR "SLUB: %s Not a valid slab page @0x%p "
			"flags=%lx mapping=0x%p count=%d \n",
			s->name, page, page->flags, page->mapping,
			page_count(page));
		return 0;
	}
	if (page->offset * sizeof(void *) != s->offset) {
		printk(KERN_ERR "SLUB: %s Corrupted offset %lu in slab @0x%p"
			" flags=0x%lx mapping=0x%p count=%d\n",
			s->name,
			(unsigned long)(page->offset * sizeof(void *)),
			page,
			page->flags,
			page->mapping,
			page_count(page));
		dump_stack();
		return 0;
	}
	if (page->inuse > s->objects) {
		printk(KERN_ERR "SLUB: %s Inuse %u > max %u in slab "
			"page @0x%p flags=%lx mapping=0x%p count=%d\n",
			s->name, page->inuse, s->objects, page, page->flags,
			page->mapping, page_count(page));
		dump_stack();
		return 0;
	}
	/* Slab_pad_check fixes things up after itself */
	slab_pad_check(s, page);
	return 1;
}

/*
 * Determine if a certain object on a page is on the freelist and
 * therefore free. Must hold the slab lock for cpu slabs to
 * guarantee that the chains are consistent.
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
				printk(KERN_ERR "SLUB: %s slab 0x%p "
					"freepointer 0x%p corrupted.\n",
					s->name, page, fp);
				dump_stack();
				page->freelist = NULL;
				page->inuse = s->objects;
				return 0;
			}
			break;
		}
		object = fp;
		fp = get_freepointer(s, object);
		nr++;
	}

	if (page->inuse != s->objects - nr) {
		printk(KERN_ERR "slab %s: page 0x%p wrong object count."
			" counter is %d but counted were %d\n",
			s->name, page, page->inuse,
			s->objects - nr);
		page->inuse = s->objects - nr;
	}
	return search == NULL;
}

static int alloc_object_checks(struct kmem_cache *s, struct page *page,
							void *object)
{
	if (!check_slab(s, page))
		goto bad;

	if (object && !on_freelist(s, page, object)) {
		printk(KERN_ERR "SLUB: %s Object 0x%p@0x%p "
			"already allocated.\n",
			s->name, object, page);
		goto dump;
	}

	if (!check_valid_pointer(s, page, object)) {
		object_err(s, page, object, "Freelist Pointer check fails");
		goto dump;
	}

	if (!object)
		return 1;

	if (!check_object(s, page, object, 0))
		goto bad;
	init_object(s, object, 1);

	if (s->flags & SLAB_TRACE) {
		printk(KERN_INFO "TRACE %s alloc 0x%p inuse=%d fp=0x%p\n",
			s->name, object, page->inuse,
			page->freelist);
		dump_stack();
	}
	return 1;
dump:
	dump_stack();
bad:
	if (PageSlab(page)) {
		/*
		 * If this is a slab page then lets do the best we can
		 * to avoid issues in the future. Marking all objects
		 * as used avoids touching the remainder.
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

static int free_object_checks(struct kmem_cache *s, struct page *page,
							void *object)
{
	if (!check_slab(s, page))
		goto fail;

	if (!check_valid_pointer(s, page, object)) {
		printk(KERN_ERR "SLUB: %s slab 0x%p invalid "
			"object pointer 0x%p\n",
			s->name, page, object);
		goto fail;
	}

	if (on_freelist(s, page, object)) {
		printk(KERN_ERR "SLUB: %s slab 0x%p object "
			"0x%p already free.\n", s->name, page, object);
		goto fail;
	}

	if (!check_object(s, page, object, 1))
		return 0;

	if (unlikely(s != page->slab)) {
		if (!PageSlab(page))
			printk(KERN_ERR "slab_free %s size %d: attempt to"
				"free object(0x%p) outside of slab.\n",
				s->name, s->size, object);
		else
		if (!page->slab)
			printk(KERN_ERR
				"slab_free : no slab(NULL) for object 0x%p.\n",
						object);
		else
		printk(KERN_ERR "slab_free %s(%d): object at 0x%p"
				" belongs to slab %s(%d)\n",
				s->name, s->size, object,
				page->slab->name, page->slab->size);
		goto fail;
	}
	if (s->flags & SLAB_TRACE) {
		printk(KERN_INFO "TRACE %s free 0x%p inuse=%d fp=0x%p\n",
			s->name, object, page->inuse,
			page->freelist);
		print_section("Object", object, s->objsize);
		dump_stack();
	}
	init_object(s, object, 0);
	return 1;
fail:
	dump_stack();
	printk(KERN_ERR "@@@ SLUB: %s slab 0x%p object at 0x%p not freed.\n",
		s->name, page, object);
	return 0;
}

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
	if (PageError(page)) {
		init_object(s, object, 0);
		init_tracking(s, object);
	}

	if (unlikely(s->ctor)) {
		int mode = SLAB_CTOR_CONSTRUCTOR;

		if (!(s->flags & __GFP_WAIT))
			mode |= SLAB_CTOR_ATOMIC;

		s->ctor(object, s, mode);
	}
}

static struct page *new_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	struct page *page;
	struct kmem_cache_node *n;
	void *start;
	void *end;
	void *last;
	void *p;

	if (flags & __GFP_NO_GROW)
		return NULL;

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
		page->flags |= 1 << PG_error;

	start = page_address(page);
	end = start + s->objects * s->size;

	if (unlikely(s->flags & SLAB_POISON))
		memset(start, POISON_INUSE, PAGE_SIZE << s->order);

	last = start;
	for (p = start + s->size; p < end; p += s->size) {
		setup_object(s, page, last);
		set_freepointer(s, last, p);
		last = p;
	}
	setup_object(s, page, last);
	set_freepointer(s, last, NULL);

	page->freelist = start;
	page->inuse = 0;
out:
	if (flags & __GFP_WAIT)
		local_irq_disable();
	return page;
}

static void __free_slab(struct kmem_cache *s, struct page *page)
{
	int pages = 1 << s->order;

	if (unlikely(PageError(page) || s->dtor)) {
		void *start = page_address(page);
		void *end = start + (pages << PAGE_SHIFT);
		void *p;

		slab_pad_check(s, page);
		for (p = start; p <= end - s->size; p += s->size) {
			if (s->dtor)
				s->dtor(p, s, 0);
			check_object(s, page, p, 0);
		}
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
	page->flags &= ~(1 << PG_slab | 1 << PG_error);
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
static void add_partial(struct kmem_cache *s, struct page *page)
{
	struct kmem_cache_node *n = get_node(s, page_to_nid(page));

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
 * Lock page and remove it from the partial list
 *
 * Must hold list_lock
 */
static int lock_and_del_slab(struct kmem_cache_node *n, struct page *page)
{
	if (slab_trylock(page)) {
		list_del(&page->lru);
		n->nr_partial--;
		return 1;
	}
	return 0;
}

/*
 * Try to get a partial slab from a specific node
 */
static struct page *get_partial_node(struct kmem_cache_node *n)
{
	struct page *page;

	/*
	 * Racy check. If we mistakenly see no partial slabs then we
	 * just allocate an empty slab. If we mistakenly try to get a
	 * partial slab then get_partials() will return NULL.
	 */
	if (!n || !n->nr_partial)
		return NULL;

	spin_lock(&n->list_lock);
	list_for_each_entry(page, &n->partial, lru)
		if (lock_and_del_slab(n, page))
			goto out;
	page = NULL;
out:
	spin_unlock(&n->list_lock);
	return page;
}

/*
 * Get a page from somewhere. Search in increasing NUMA
 * distances.
 */
static struct page *get_any_partial(struct kmem_cache *s, gfp_t flags)
{
#ifdef CONFIG_NUMA
	struct zonelist *zonelist;
	struct zone **z;
	struct page *page;

	/*
	 * The defrag ratio allows to configure the tradeoffs between
	 * inter node defragmentation and node local allocations.
	 * A lower defrag_ratio increases the tendency to do local
	 * allocations instead of scanning throught the partial
	 * lists on other nodes.
	 *
	 * If defrag_ratio is set to 0 then kmalloc() always
	 * returns node local objects. If its higher then kmalloc()
	 * may return off node objects in order to avoid fragmentation.
	 *
	 * A higher ratio means slabs may be taken from other nodes
	 * thus reducing the number of partial slabs on those nodes.
	 *
	 * If /sys/slab/xx/defrag_ratio is set to 100 (which makes
	 * defrag_ratio = 1000) then every (well almost) allocation
	 * will first attempt to defrag slab caches on other nodes. This
	 * means scanning over all nodes to look for partial slabs which
	 * may be a bit expensive to do on every slab allocation.
	 */
	if (!s->defrag_ratio || get_cycles() % 1024 > s->defrag_ratio)
		return NULL;

	zonelist = &NODE_DATA(slab_node(current->mempolicy))
					->node_zonelists[gfp_zone(flags)];
	for (z = zonelist->zones; *z; z++) {
		struct kmem_cache_node *n;

		n = get_node(s, zone_to_nid(*z));

		if (n && cpuset_zone_allowed_hardwall(*z, flags) &&
				n->nr_partial > 2) {
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
static void putback_slab(struct kmem_cache *s, struct page *page)
{
	if (page->inuse) {
		if (page->freelist)
			add_partial(s, page);
		slab_unlock(page);
	} else {
		slab_unlock(page);
		discard_slab(s, page);
	}
}

/*
 * Remove the cpu slab
 */
static void deactivate_slab(struct kmem_cache *s, struct page *page, int cpu)
{
	s->cpu_slab[cpu] = NULL;
	ClearPageActive(page);

	putback_slab(s, page);
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
 * slab_alloc is optimized to only modify two cachelines on the fast path
 * (aside from the stack):
 *
 * 1. The page struct
 * 2. The first cacheline of the object to be allocated.
 *
 * The only cache lines that are read (apart from code) is the
 * per cpu array in the kmem_cache struct.
 *
 * Fastpath is not possible if we need to get a new slab or have
 * debugging enabled (which means all slabs are marked with PageError)
 */
static __always_inline void *slab_alloc(struct kmem_cache *s,
					gfp_t gfpflags, int node)
{
	struct page *page;
	void **object;
	unsigned long flags;
	int cpu;

	local_irq_save(flags);
	cpu = smp_processor_id();
	page = s->cpu_slab[cpu];
	if (!page)
		goto new_slab;

	slab_lock(page);
	if (unlikely(node != -1 && page_to_nid(page) != node))
		goto another_slab;
redo:
	object = page->freelist;
	if (unlikely(!object))
		goto another_slab;
	if (unlikely(PageError(page)))
		goto debug;

have_object:
	page->inuse++;
	page->freelist = object[page->offset];
	slab_unlock(page);
	local_irq_restore(flags);
	return object;

another_slab:
	deactivate_slab(s, page, cpu);

new_slab:
	page = get_partial(s, gfpflags, node);
	if (likely(page)) {
have_slab:
		s->cpu_slab[cpu] = page;
		SetPageActive(page);
		goto redo;
	}

	page = new_slab(s, gfpflags, node);
	if (page) {
		cpu = smp_processor_id();
		if (s->cpu_slab[cpu]) {
			/*
			 * Someone else populated the cpu_slab while we enabled
			 * interrupts, or we have got scheduled on another cpu.
			 * The page may not be on the requested node.
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
				goto redo;
			}
			/* Dump the current slab */
			flush_slab(s, s->cpu_slab[cpu], cpu);
		}
		slab_lock(page);
		goto have_slab;
	}
	local_irq_restore(flags);
	return NULL;
debug:
	if (!alloc_object_checks(s, page, object))
		goto another_slab;
	if (s->flags & SLAB_STORE_USER)
		set_tracking(s, object, TRACK_ALLOC);
	goto have_object;
}

void *kmem_cache_alloc(struct kmem_cache *s, gfp_t gfpflags)
{
	return slab_alloc(s, gfpflags, -1);
}
EXPORT_SYMBOL(kmem_cache_alloc);

#ifdef CONFIG_NUMA
void *kmem_cache_alloc_node(struct kmem_cache *s, gfp_t gfpflags, int node)
{
	return slab_alloc(s, gfpflags, node);
}
EXPORT_SYMBOL(kmem_cache_alloc_node);
#endif

/*
 * The fastpath only writes the cacheline of the page struct and the first
 * cacheline of the object.
 *
 * No special cachelines need to be read
 */
static void slab_free(struct kmem_cache *s, struct page *page, void *x)
{
	void *prior;
	void **object = (void *)x;
	unsigned long flags;

	local_irq_save(flags);
	slab_lock(page);

	if (unlikely(PageError(page)))
		goto debug;
checks_ok:
	prior = object[page->offset] = page->freelist;
	page->freelist = object;
	page->inuse--;

	if (unlikely(PageActive(page)))
		/*
		 * Cpu slabs are never on partial lists and are
		 * never freed.
		 */
		goto out_unlock;

	if (unlikely(!page->inuse))
		goto slab_empty;

	/*
	 * Objects left in the slab. If it
	 * was not on the partial list before
	 * then add it.
	 */
	if (unlikely(!prior))
		add_partial(s, page);

out_unlock:
	slab_unlock(page);
	local_irq_restore(flags);
	return;

slab_empty:
	if (prior)
		/*
		 * Partially used slab that is on the partial list.
		 */
		remove_partial(s, page);

	slab_unlock(page);
	discard_slab(s, page);
	local_irq_restore(flags);
	return;

debug:
	if (free_object_checks(s, page, x))
		goto checks_ok;
	goto out_unlock;
}

void kmem_cache_free(struct kmem_cache *s, void *x)
{
	struct page * page;

	page = virt_to_page(x);

	page = compound_head(page);

	if (unlikely(PageError(page) && (s->flags & SLAB_STORE_USER)))
		set_tracking(s, x, TRACK_FREE);
	slab_free(s, page, x);
}
EXPORT_SYMBOL(kmem_cache_free);

/* Figure out on which slab object the object resides */
static struct page *get_object_page(const void *x)
{
	struct page *page = compound_head(virt_to_page(x));

	if (!PageSlab(page))
		return NULL;

	return page;
}

/*
 * kmem_cache_open produces objects aligned at "size" and the first object
 * is placed at offset 0 in the slab (We have no metainformation on the
 * slab, all slabs are in essence "off slab").
 *
 * In order to get the desired alignment one just needs to align the
 * size.
 *
 * Notice that the allocation order determines the sizes of the per cpu
 * caches. Each processor has always one slab available for allocations.
 * Increasing the allocation order reduces the number of times that slabs
 * must be moved on and off the partial lists and therefore may influence
 * locking overhead.
 *
 * The offset is used to relocate the free list link in each object. It is
 * therefore possible to move the free list link behind the object. This
 * is necessary for RCU to work properly and also useful for debugging.
 */

/*
 * Mininum / Maximum order of slab pages. This influences locking overhead
 * and slab fragmentation. A higher order reduces the number of partial slabs
 * and increases the number of allocations possible without having to
 * take the list_lock.
 */
static int slub_min_order;
static int slub_max_order = DEFAULT_MAX_ORDER;

/*
 * Minimum number of objects per slab. This is necessary in order to
 * reduce locking overhead. Similar to the queue size in SLAB.
 */
static int slub_min_objects = DEFAULT_MIN_OBJECTS;

/*
 * Merge control. If this is set then no merging of slab caches will occur.
 */
static int slub_nomerge;

/*
 * Debug settings:
 */
static int slub_debug;

static char *slub_debug_slabs;

/*
 * Calculate the order of allocation given an slab object size.
 *
 * The order of allocation has significant impact on other elements
 * of the system. Generally order 0 allocations should be preferred
 * since they do not cause fragmentation in the page allocator. Larger
 * objects may have problems with order 0 because there may be too much
 * space left unused in a slab. We go to a higher order if more than 1/8th
 * of the slab would be wasted.
 *
 * In order to reach satisfactory performance we must ensure that
 * a minimum number of objects is in one slab. Otherwise we may
 * generate too much activity on the partial lists. This is less a
 * concern for large slabs though. slub_max_order specifies the order
 * where we begin to stop considering the number of objects in a slab.
 *
 * Higher order allocations also allow the placement of more objects
 * in a slab and thereby reduce object handling overhead. If the user
 * has requested a higher mininum order then we start with that one
 * instead of zero.
 */
static int calculate_order(int size)
{
	int order;
	int rem;

	for (order = max(slub_min_order, fls(size - 1) - PAGE_SHIFT);
			order < MAX_ORDER; order++) {
		unsigned long slab_size = PAGE_SIZE << order;

		if (slub_max_order > order &&
				slab_size < slub_min_objects * size)
			continue;

		if (slab_size < size)
			continue;

		rem = slab_size % size;

		if (rem <= (PAGE_SIZE << order) / 8)
			break;

	}
	if (order >= MAX_ORDER)
		return -E2BIG;
	return order;
}

/*
 * Function to figure out which alignment to use from the
 * various ways of specifying it.
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
	if ((flags & (SLAB_MUST_HWCACHE_ALIGN | SLAB_HWCACHE_ALIGN)) &&
			size > L1_CACHE_BYTES / 2)
		return max_t(unsigned long, align, L1_CACHE_BYTES);

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
	init_object(kmalloc_caches, n, 1);
	init_kmem_cache_node(n);
	atomic_long_inc(&n->nr_slabs);
	add_partial(kmalloc_caches, page);
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
			!s->ctor && !s->dtor)
		s->flags |= __OBJECT_POISON;
	else
		s->flags &= ~__OBJECT_POISON;

	/*
	 * Round up object size to the next word boundary. We can only
	 * place the free pointer at word boundaries and this determines
	 * the possible location of the free pointer.
	 */
	size = ALIGN(size, sizeof(void *));

	/*
	 * If we are redzoning then check if there is some space between the
	 * end of the object and the free pointer. If not then add an
	 * additional word, so that we can establish a redzone between
	 * the object and the freepointer to be able to check for overwrites.
	 */
	if ((flags & SLAB_RED_ZONE) && size == s->objsize)
		size += sizeof(void *);

	/*
	 * With that we have determined how much of the slab is in actual
	 * use by the object. This is the potential offset to the free
	 * pointer.
	 */
	s->inuse = size;

	if (((flags & (SLAB_DESTROY_BY_RCU | SLAB_POISON)) ||
		s->ctor || s->dtor)) {
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

	if (flags & SLAB_STORE_USER)
		/*
		 * Need to store information about allocs and frees after
		 * the object.
		 */
		size += 2 * sizeof(struct track);

	if (flags & DEBUG_DEFAULT_FLAGS)
		/*
		 * Add some empty padding so that we can catch
		 * overwrites from earlier objects rather than let
		 * tracking information or the free pointer be
		 * corrupted if an user writes before the start
		 * of the object.
		 */
		size += sizeof(void *);
	/*
	 * Determine the alignment based on various parameters that the
	 * user specified (this is unecessarily complex due to the attempt
	 * to be compatible with SLAB. Should be cleaned up some day).
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

static int __init finish_bootstrap(void)
{
	struct list_head *h;
	int err;

	slab_state = SYSFS;

	list_for_each(h, &slab_caches) {
		struct kmem_cache *s =
			container_of(h, struct kmem_cache, list);

		err = sysfs_slab_add(s);
		BUG_ON(err);
	}
	return 0;
}

static int kmem_cache_open(struct kmem_cache *s, gfp_t gfpflags,
		const char *name, size_t size,
		size_t align, unsigned long flags,
		void (*ctor)(void *, struct kmem_cache *, unsigned long),
		void (*dtor)(void *, struct kmem_cache *, unsigned long))
{
	memset(s, 0, kmem_size);
	s->name = name;
	s->ctor = ctor;
	s->dtor = dtor;
	s->objsize = size;
	s->flags = flags;
	s->align = align;

	BUG_ON(flags & SLUB_UNIMPLEMENTED);

	/*
	 * The page->offset field is only 16 bit wide. This is an offset
	 * in units of words from the beginning of an object. If the slab
	 * size is bigger then we cannot move the free pointer behind the
	 * object anymore.
	 *
	 * On 32 bit platforms the limit is 256k. On 64bit platforms
	 * the limit is 512k.
	 *
	 * Debugging or ctor/dtors may create a need to move the free
	 * pointer. Fail if this happens.
	 */
	if (s->size >= 65535 * sizeof(void *)) {
		BUG_ON(flags & (SLAB_RED_ZONE | SLAB_POISON |
				SLAB_STORE_USER | SLAB_DESTROY_BY_RCU));
		BUG_ON(ctor || dtor);
	}
	else
		/*
		 * Enable debugging if selected on the kernel commandline.
		 */
		if (slub_debug && (!slub_debug_slabs ||
		    strncmp(slub_debug_slabs, name,
		    	strlen(slub_debug_slabs)) == 0))
				s->flags |= slub_debug;

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
	void *addr;

	page = get_object_page(object);

	if (!page || s != page->slab)
		/* No slab or wrong slab */
		return 0;

	addr = page_address(page);
	if (object < addr || object >= addr + s->objects * s->size)
		/* Out of bounds */
		return 0;

	if ((object - addr) % s->size)
		/* Improperly aligned */
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
 * Attempt to free all slabs on a node
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
 * Release all resources used by slab cache
 */
static int kmem_cache_close(struct kmem_cache *s)
{
	int node;

	flush_all(s);

	/* Attempt to free all objects */
	for_each_online_node(node) {
		struct kmem_cache_node *n = get_node(s, node);

		free_list(s, n, &n->partial);
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

static struct kmem_cache *create_kmalloc_cache(struct kmem_cache *s,
		const char *name, int size, gfp_t gfp_flags)
{
	unsigned int flags = 0;

	if (gfp_flags & SLUB_DMA)
		flags = SLAB_CACHE_DMA;

	down_write(&slub_lock);
	if (!kmem_cache_open(s, gfp_flags, name, size, ARCH_KMALLOC_MINALIGN,
			flags, NULL, NULL))
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
		return kmem_cache_alloc(s, flags);
	return NULL;
}
EXPORT_SYMBOL(__kmalloc);

#ifdef CONFIG_NUMA
void *__kmalloc_node(size_t size, gfp_t flags, int node)
{
	struct kmem_cache *s = get_slab(size, flags);

	if (s)
		return kmem_cache_alloc_node(s, flags, node);
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

	page = compound_head(virt_to_page(x));

	s = page->slab;

	if (unlikely(PageError(page) && (s->flags & SLAB_STORE_USER)))
		set_tracking(s, (void *)x, TRACK_FREE);
	slab_free(s, page, (void *)x);
}
EXPORT_SYMBOL(kfree);

/**
 * krealloc - reallocate memory. The contents will remain unchanged.
 *
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
	struct kmem_cache *new_cache;
	void *ret;
	struct page *page;

	if (unlikely(!p))
		return kmalloc(new_size, flags);

	if (unlikely(!new_size)) {
		kfree(p);
		return NULL;
	}

	page = compound_head(virt_to_page(p));

	new_cache = get_slab(new_size, flags);

	/*
 	 * If new size fits in the current cache, bail out.
 	 */
	if (likely(page->slab == new_cache))
		return (void *)p;

	ret = kmalloc(new_size, flags);
	if (ret) {
		memcpy(ret, p, min(new_size, ksize(p)));
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
	 * struct kmalloc_cache_node's. There is special bootstrap code in
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

	if (nr_cpu_ids)	/* Remove when nr_cpu_ids is fixed upstream ! */
		kmem_size = offsetof(struct kmem_cache, cpu_slab)
			 + nr_cpu_ids * sizeof(struct page *);

	printk(KERN_INFO "SLUB: Genslabs=%d, HWalign=%d, Order=%d-%d, MinObjects=%d,"
		" Processors=%d, Nodes=%d\n",
		KMALLOC_SHIFT_HIGH, L1_CACHE_BYTES,
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

	if (s->ctor || s->dtor)
		return 1;

	return 0;
}

static struct kmem_cache *find_mergeable(size_t size,
		size_t align, unsigned long flags,
		void (*ctor)(void *, struct kmem_cache *, unsigned long),
		void (*dtor)(void *, struct kmem_cache *, unsigned long))
{
	struct list_head *h;

	if (slub_nomerge || (flags & SLUB_NEVER_MERGE))
		return NULL;

	if (ctor || dtor)
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

	down_write(&slub_lock);
	s = find_mergeable(size, align, flags, dtor, ctor);
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
				size, align, flags, ctor, dtor)) {
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

	x = kmem_cache_alloc(s, flags);
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
 * Use the cpu notifier to insure that the slab are flushed
 * when necessary.
 */
static int __cpuinit slab_cpuup_callback(struct notifier_block *nfb,
		unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
	case CPU_UP_CANCELED:
	case CPU_DEAD:
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

/***************************************************************
 *	Compatiblility definitions
 **************************************************************/

int kmem_cache_shrink(struct kmem_cache *s)
{
	flush_all(s);
	return 0;
}
EXPORT_SYMBOL(kmem_cache_shrink);

#ifdef CONFIG_NUMA

/*****************************************************************
 * Generic reaper used to support the page allocator
 * (the cpu slabs are reaped by a per slab workqueue).
 *
 * Maybe move this to the page allocator?
 ****************************************************************/

static DEFINE_PER_CPU(unsigned long, reap_node);

static void init_reap_node(int cpu)
{
	int node;

	node = next_node(cpu_to_node(cpu), node_online_map);
	if (node == MAX_NUMNODES)
		node = first_node(node_online_map);

	__get_cpu_var(reap_node) = node;
}

static void next_reap_node(void)
{
	int node = __get_cpu_var(reap_node);

	/*
	 * Also drain per cpu pages on remote zones
	 */
	if (node != numa_node_id())
		drain_node_pages(node);

	node = next_node(node, node_online_map);
	if (unlikely(node >= MAX_NUMNODES))
		node = first_node(node_online_map);
	__get_cpu_var(reap_node) = node;
}
#else
#define init_reap_node(cpu) do { } while (0)
#define next_reap_node(void) do { } while (0)
#endif

#define REAPTIMEOUT_CPUC	(2*HZ)

#ifdef CONFIG_SMP
static DEFINE_PER_CPU(struct delayed_work, reap_work);

static void cache_reap(struct work_struct *unused)
{
	next_reap_node();
	refresh_cpu_vm_stats(smp_processor_id());
	schedule_delayed_work(&__get_cpu_var(reap_work),
				      REAPTIMEOUT_CPUC);
}

static void __devinit start_cpu_timer(int cpu)
{
	struct delayed_work *reap_work = &per_cpu(reap_work, cpu);

	/*
	 * When this gets called from do_initcalls via cpucache_init(),
	 * init_workqueues() has already run, so keventd will be setup
	 * at that time.
	 */
	if (keventd_up() && reap_work->work.func == NULL) {
		init_reap_node(cpu);
		INIT_DELAYED_WORK(reap_work, cache_reap);
		schedule_delayed_work_on(cpu, reap_work, HZ + 3 * cpu);
	}
}

static int __init cpucache_init(void)
{
	int cpu;

	/*
	 * Register the timers that drain pcp pages and update vm statistics
	 */
	for_each_online_cpu(cpu)
		start_cpu_timer(cpu);
	return 0;
}
__initcall(cpucache_init);
#endif

#ifdef SLUB_RESILIENCY_TEST
static unsigned long validate_slab_cache(struct kmem_cache *s);

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
 * These are not as efficient as kmalloc for the non debug case.
 * We do not have the page struct available so we have to touch one
 * cacheline in struct kmem_cache to check slab flags.
 */
void *__kmalloc_track_caller(size_t size, gfp_t gfpflags, void *caller)
{
	struct kmem_cache *s = get_slab(size, gfpflags);
	void *object;

	if (!s)
		return NULL;

	object = kmem_cache_alloc(s, gfpflags);

	if (object && (s->flags & SLAB_STORE_USER))
		set_track(s, object, TRACK_ALLOC, caller);

	return object;
}

void *__kmalloc_node_track_caller(size_t size, gfp_t gfpflags,
					int node, void *caller)
{
	struct kmem_cache *s = get_slab(size, gfpflags);
	void *object;

	if (!s)
		return NULL;

	object = kmem_cache_alloc_node(s, gfpflags, node);

	if (object && (s->flags & SLAB_STORE_USER))
		set_track(s, object, TRACK_ALLOC, caller);

	return object;
}

#ifdef CONFIG_SYSFS

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

static ssize_t dtor_show(struct kmem_cache *s, char *buf)
{
	if (s->dtor) {
		int n = sprint_symbol(buf, (unsigned long)s->dtor);

		return n + sprintf(buf + n, "\n");
	}
	return 0;
}
SLAB_ATTR_RO(dtor);

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
	return sprintf(buf, "%d\n", !!(s->flags &
		(SLAB_HWCACHE_ALIGN|SLAB_MUST_HWCACHE_ALIGN)));
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
	&dtor_attr.attr,
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
		sysfs_remove_link(&slab_subsys.kset.kobj, s->name);
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
		sysfs_remove_link(&slab_subsys.kset.kobj, name);
		return sysfs_create_link(&slab_subsys.kset.kobj,
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
	int err;

	err = subsystem_register(&slab_subsys);
	if (err) {
		printk(KERN_ERR "Cannot register slab subsystem.\n");
		return -ENOSYS;
	}

	finish_bootstrap();

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
#else
__initcall(finish_bootstrap);
#endif
