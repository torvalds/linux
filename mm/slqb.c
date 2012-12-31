/*
 * SLQB: A slab allocator that focuses on per-CPU scaling, and good performance
 * with order-0 allocations. Fastpaths emphasis is placed on local allocaiton
 * and freeing, but with a secondary goal of good remote freeing (freeing on
 * another CPU from that which allocated).
 *
 * Using ideas and code from mm/slab.c, mm/slob.c, and mm/slub.c.
 */

#include <linux/mm.h>
#include <linux/swap.h> /* struct reclaim_state */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/mempolicy.h>
#include <linux/ctype.h>
#include <linux/kallsyms.h>
#include <linux/memory.h>
#include <linux/fault-inject.h>

/*
 * TODO
 * - fix up releasing of offlined data structures. Not a big deal because
 *   they don't get cumulatively leaked with successive online/offline cycles
 * - allow OOM conditions to flush back per-CPU pages to common lists to be
 *   reused by other CPUs.
 * - investiage performance with memoryless nodes. Perhaps CPUs can be given
 *   a default closest home node via which it can use fastpath functions.
 *   Perhaps it is not a big problem.
 */

/*
 * slqb_page overloads struct page, and is used to manage some slob allocation
 * aspects, however to avoid the horrible mess in include/linux/mm_types.h,
 * we'll just define our own struct slqb_page type variant here.
 */
struct slqb_page {
	union {
		struct {
			unsigned long	flags;		/* mandatory */
			atomic_t	_count;		/* mandatory */
			unsigned int	inuse;		/* Nr of objects */
			struct kmem_cache_list *list;	/* Pointer to list */
			void		 **freelist;	/* LIFO freelist */
			union {
				struct list_head lru;	/* misc. list */
				struct rcu_head rcu_head; /* for rcu freeing */
			};
		};
		struct page page;
	};
};
static inline void struct_slqb_page_wrong_size(void)
{ BUILD_BUG_ON(sizeof(struct slqb_page) != sizeof(struct page)); }

#define PG_SLQB_BIT (1 << PG_slab)

/*
 * slqb_min_order: minimum allocation order for slabs
 */
static int slqb_min_order;

/*
 * slqb_min_objects: minimum number of objects per slab. Increasing this
 * will increase the allocation order for slabs with larger objects
 */
static int slqb_min_objects = 1;

#ifdef CONFIG_NUMA
static inline int slab_numa(struct kmem_cache *s)
{
	return s->flags & SLAB_NUMA;
}
#else
static inline int slab_numa(struct kmem_cache *s)
{
	return 0;
}
#endif

static inline int slab_hiwater(struct kmem_cache *s)
{
	return s->hiwater;
}

static inline int slab_freebatch(struct kmem_cache *s)
{
	return s->freebatch;
}

/*
 * Lock order:
 * kmem_cache_node->list_lock
 *   kmem_cache_remote_free->lock
 *
 * Data structures:
 * SLQB is primarily per-cpu. For each kmem_cache, each CPU has:
 *
 * - A LIFO list of node-local objects. Allocation and freeing of node local
 *   objects goes first to this list.
 *
 * - 2 Lists of slab pages, free and partial pages. If an allocation misses
 *   the object list, it tries from the partial list, then the free list.
 *   After freeing an object to the object list, if it is over a watermark,
 *   some objects are freed back to pages. If an allocation misses these lists,
 *   a new slab page is allocated from the page allocator. If the free list
 *   reaches a watermark, some of its pages are returned to the page allocator.
 *
 * - A remote free queue, where objects freed that did not come from the local
 *   node are queued to. When this reaches a watermark, the objects are
 *   flushed.
 *
 * - A remotely freed queue, where objects allocated from this CPU are flushed
 *   to from other CPUs' remote free queues. kmem_cache_remote_free->lock is
 *   used to protect access to this queue.
 *
 *   When the remotely freed queue reaches a watermark, a flag is set to tell
 *   the owner CPU to check it. The owner CPU will then check the queue on the
 *   next allocation that misses the object list. It will move all objects from
 *   this list onto the object list and then allocate one.
 *
 *   This system of remote queueing is intended to reduce lock and remote
 *   cacheline acquisitions, and give a cooling off period for remotely freed
 *   objects before they are re-allocated.
 *
 * node specific allocations from somewhere other than the local node are
 * handled by a per-node list which is the same as the above per-CPU data
 * structures except for the following differences:
 *
 * - kmem_cache_node->list_lock is used to protect access for multiple CPUs to
 *   allocate from a given node.
 *
 * - There is no remote free queue. Nodes don't free objects, CPUs do.
 */

static inline void slqb_stat_inc(struct kmem_cache_list *list,
				enum stat_item si)
{
#ifdef CONFIG_SLQB_STATS
	list->stats[si]++;
#endif
}

static inline void slqb_stat_add(struct kmem_cache_list *list,
				enum stat_item si, unsigned long nr)
{
#ifdef CONFIG_SLQB_STATS
	list->stats[si] += nr;
#endif
}

static inline int slqb_page_to_nid(struct slqb_page *page)
{
	return page_to_nid(&page->page);
}

static inline void *slqb_page_address(struct slqb_page *page)
{
	return page_address(&page->page);
}

static inline struct zone *slqb_page_zone(struct slqb_page *page)
{
	return page_zone(&page->page);
}

static inline int virt_to_nid(const void *addr)
{
	return page_to_nid(virt_to_page(addr));
}

static inline struct slqb_page *virt_to_head_slqb_page(const void *addr)
{
	struct page *p;

	p = virt_to_head_page(addr);
	return (struct slqb_page *)p;
}

static inline void __free_slqb_pages(struct slqb_page *page, unsigned int order,
					int pages)
{
	struct page *p = &page->page;

	reset_page_mapcount(p);
	p->mapping = NULL;
	VM_BUG_ON(!(p->flags & PG_SLQB_BIT));
	p->flags &= ~PG_SLQB_BIT;

	if (current->reclaim_state)
		current->reclaim_state->reclaimed_slab += pages;
	__free_pages(p, order);
}

#ifdef CONFIG_SLQB_DEBUG
static inline int slab_debug(struct kmem_cache *s)
{
	return s->flags &
			(SLAB_DEBUG_FREE |
			 SLAB_RED_ZONE |
			 SLAB_POISON |
			 SLAB_STORE_USER |
			 SLAB_TRACE);
}
static inline int slab_poison(struct kmem_cache *s)
{
	return s->flags & SLAB_POISON;
}
#else
static inline int slab_debug(struct kmem_cache *s)
{
	return 0;
}
static inline int slab_poison(struct kmem_cache *s)
{
	return 0;
}
#endif

#define DEBUG_DEFAULT_FLAGS (SLAB_DEBUG_FREE | SLAB_RED_ZONE | \
				SLAB_POISON | SLAB_STORE_USER)

/* Internal SLQB flags */
#define __OBJECT_POISON		0x80000000 /* Poison object */

/* Not all arches define cache_line_size */
#ifndef cache_line_size
#define cache_line_size()	L1_CACHE_BYTES
#endif

#ifdef CONFIG_SMP
static struct notifier_block slab_notifier;
#endif

/*
 * slqb_lock protects slab_caches list and serialises hotplug operations.
 * hotplug operations take lock for write, other operations can hold off
 * hotplug by taking it for read (or write).
 */
static DECLARE_RWSEM(slqb_lock);

/*
 * A list of all slab caches on the system
 */
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

static struct kmem_cache kmem_cache_cache;

#ifdef CONFIG_SLQB_SYSFS
static int sysfs_slab_add(struct kmem_cache *s);
static void sysfs_slab_remove(struct kmem_cache *s);
#else
static inline int sysfs_slab_add(struct kmem_cache *s)
{
	return 0;
}
static inline void sysfs_slab_remove(struct kmem_cache *s)
{
	kmem_cache_free(&kmem_cache_cache, s);
}
#endif

/********************************************************************
 * 			Core slab cache functions
 *******************************************************************/

static int __slab_is_available __read_mostly;
int slab_is_available(void)
{
	return __slab_is_available;
}

static inline struct kmem_cache_cpu *get_cpu_slab(struct kmem_cache *s, int cpu)
{
#ifdef CONFIG_SMP
	VM_BUG_ON(!s->cpu_slab[cpu]);
	return s->cpu_slab[cpu];
#else
	return &s->cpu_slab;
#endif
}

static inline int check_valid_pointer(struct kmem_cache *s,
				struct slqb_page *page, const void *object)
{
	void *base;

	base = slqb_page_address(page);
	if (object < base || object >= base + s->objects * s->size ||
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
#define for_each_object(__p, __s, __addr) \
	for (__p = (__addr); __p < (__addr) + (__s)->objects * (__s)->size;\
			__p += (__s)->size)

/* Scan freelist */
#define for_each_free_object(__p, __s, __free) \
	for (__p = (__free); (__p) != NULL; __p = get_freepointer((__s),\
		__p))

#ifdef CONFIG_SLQB_DEBUG
/*
 * Debug settings:
 */
#ifdef CONFIG_SLQB_DEBUG_ON
static int slqb_debug __read_mostly = DEBUG_DEFAULT_FLAGS;
#else
static int slqb_debug __read_mostly;
#endif

static char *slqb_debug_slabs;

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
	struct track *p;

	if (s->offset)
		p = object + s->offset + sizeof(void *);
	else
		p = object + s->inuse;

	p += alloc;
	if (addr) {
		p->addr = addr;
		p->cpu = raw_smp_processor_id();
		p->pid = current ? current->pid : -1;
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

	printk(KERN_ERR "INFO: %s in ", s);
	__print_symbol("%s", (unsigned long)t->addr);
	printk(" age=%lu cpu=%u pid=%d\n", jiffies - t->when, t->cpu, t->pid);
}

static void print_tracking(struct kmem_cache *s, void *object)
{
	if (!(s->flags & SLAB_STORE_USER))
		return;

	print_track("Allocated", get_track(s, object, TRACK_ALLOC));
	print_track("Freed", get_track(s, object, TRACK_FREE));
}

static void print_page_info(struct slqb_page *page)
{
	printk(KERN_ERR "INFO: Slab 0x%p used=%u fp=0x%p flags=0x%04lx\n",
		page, page->inuse, page->freelist, page->flags);

}

#define MAX_ERR_STR 100
static void slab_bug(struct kmem_cache *s, char *fmt, ...)
{
	va_list args;
	char buf[MAX_ERR_STR];

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

static void print_trailer(struct kmem_cache *s, struct slqb_page *page, u8 *p)
{
	unsigned int off;	/* Offset of last byte */
	u8 *addr = slqb_page_address(page);

	print_tracking(s, p);

	print_page_info(page);

	printk(KERN_ERR "INFO: Object 0x%p @offset=%tu fp=0x%p\n\n",
			p, p - addr, get_freepointer(s, p));

	if (p > addr + 16)
		print_section("Bytes b4", p - 16, 16);

	print_section("Object", p, min(s->objsize, 128));

	if (s->flags & SLAB_RED_ZONE)
		print_section("Redzone", p + s->objsize, s->inuse - s->objsize);

	if (s->offset)
		off = s->offset + sizeof(void *);
	else
		off = s->inuse;

	if (s->flags & SLAB_STORE_USER)
		off += 2 * sizeof(struct track);

	if (off != s->size) {
		/* Beginning of the filler is the free pointer */
		print_section("Padding", p + off, s->size - off);
	}

	dump_stack();
}

static void object_err(struct kmem_cache *s, struct slqb_page *page,
			u8 *object, char *reason)
{
	slab_bug(s, reason);
	print_trailer(s, page, object);
}

static void slab_err(struct kmem_cache *s, struct slqb_page *page,
			char *fmt, ...)
{
	slab_bug(s, fmt);
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

	if (s->flags & SLAB_RED_ZONE) {
		memset(p + s->objsize,
			active ? SLUB_RED_ACTIVE : SLUB_RED_INACTIVE,
			s->inuse - s->objsize);
	}
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

static int check_bytes_and_report(struct kmem_cache *s, struct slqb_page *page,
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
 * 		one word if debuggin is on to be able to detect writes
 * 		before the word boundary.
 *
 *	Padding is done using 0x5a (POISON_INUSE)
 *
 * object + s->size
 * 	Nothing is used beyond s->size.
 */

static int check_pad_bytes(struct kmem_cache *s, struct slqb_page *page, u8 *p)
{
	unsigned long off = s->inuse;	/* The end of info */

	if (s->offset) {
		/* Freepointer is placed after the object. */
		off += sizeof(void *);
	}

	if (s->flags & SLAB_STORE_USER) {
		/* We also have user information there */
		off += 2 * sizeof(struct track);
	}

	if (s->size == off)
		return 1;

	return check_bytes_and_report(s, page, p, "Object padding",
				p + off, POISON_INUSE, s->size - off);
}

static int slab_pad_check(struct kmem_cache *s, struct slqb_page *page)
{
	u8 *start;
	u8 *fault;
	u8 *end;
	int length;
	int remainder;

	if (!(s->flags & SLAB_POISON))
		return 1;

	start = slqb_page_address(page);
	end = start + (PAGE_SIZE << s->order);
	length = s->objects * s->size;
	remainder = end - (start + length);
	if (!remainder)
		return 1;

	fault = check_bytes(start + length, POISON_INUSE, remainder);
	if (!fault)
		return 1;

	while (end > fault && end[-1] == POISON_INUSE)
		end--;

	slab_err(s, page, "Padding overwritten. 0x%p-0x%p", fault, end - 1);
	print_section("Padding", start, length);

	restore_bytes(s, "slab padding", POISON_INUSE, start, end);
	return 0;
}

static int check_object(struct kmem_cache *s, struct slqb_page *page,
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
		if (!active && (s->flags & __OBJECT_POISON)) {
			if (!check_bytes_and_report(s, page, p, "Poison", p,
					POISON_FREE, s->objsize - 1))
				return 0;

			if (!check_bytes_and_report(s, page, p, "Poison",
					p + s->objsize - 1, POISON_END, 1))
				return 0;
		}

		/*
		 * check_pad_bytes cleans up on its own.
		 */
		check_pad_bytes(s, page, p);
	}

	return 1;
}

static int check_slab(struct kmem_cache *s, struct slqb_page *page)
{
	if (!(page->flags & PG_SLQB_BIT)) {
		slab_err(s, page, "Not a valid slab page");
		return 0;
	}
	if (page->inuse == 0) {
		slab_err(s, page, "inuse before free / after alloc", s->name);
		return 0;
	}
	if (page->inuse > s->objects) {
		slab_err(s, page, "inuse %u > max %u",
			s->name, page->inuse, s->objects);
		return 0;
	}
	/* Slab_pad_check fixes things up after itself */
	slab_pad_check(s, page);
	return 1;
}

static void trace(struct kmem_cache *s, struct slqb_page *page,
			void *object, int alloc)
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

static void setup_object_debug(struct kmem_cache *s, struct slqb_page *page,
				void *object)
{
	if (!slab_debug(s))
		return;

	if (!(s->flags & (SLAB_STORE_USER|SLAB_RED_ZONE|__OBJECT_POISON)))
		return;

	init_object(s, object, 0);
	init_tracking(s, object);
}

static int alloc_debug_processing(struct kmem_cache *s,
					void *object, unsigned long addr)
{
	struct slqb_page *page;
	page = virt_to_head_slqb_page(object);

	if (!check_slab(s, page))
		goto bad;

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
	return 0;
}

static int free_debug_processing(struct kmem_cache *s,
					void *object, unsigned long addr)
{
	struct slqb_page *page;
	page = virt_to_head_slqb_page(object);

	if (!check_slab(s, page))
		goto fail;

	if (!check_valid_pointer(s, page, object)) {
		slab_err(s, page, "Invalid object pointer 0x%p", object);
		goto fail;
	}

	if (!check_object(s, page, object, 1))
		return 0;

	/* Special debug activities for freeing objects */
	if (s->flags & SLAB_STORE_USER)
		set_track(s, object, TRACK_FREE, addr);
	trace(s, page, object, 0);
	init_object(s, object, 0);
	return 1;

fail:
	slab_fix(s, "Object at 0x%p not freed", object);
	return 0;
}

static int __init setup_slqb_debug(char *str)
{
	slqb_debug = DEBUG_DEFAULT_FLAGS;
	if (*str++ != '=' || !*str) {
		/*
		 * No options specified. Switch on full debugging.
		 */
		goto out;
	}

	if (*str == ',') {
		/*
		 * No options but restriction on slabs. This means full
		 * debugging for slabs matching a pattern.
		 */
		goto check_slabs;
	}

	slqb_debug = 0;
	if (*str == '-') {
		/*
		 * Switch off all debugging measures.
		 */
		goto out;
	}

	/*
	 * Determine which debug features should be switched on
	 */
	for (; *str && *str != ','; str++) {
		switch (tolower(*str)) {
		case 'f':
			slqb_debug |= SLAB_DEBUG_FREE;
			break;
		case 'z':
			slqb_debug |= SLAB_RED_ZONE;
			break;
		case 'p':
			slqb_debug |= SLAB_POISON;
			break;
		case 'u':
			slqb_debug |= SLAB_STORE_USER;
			break;
		case 't':
			slqb_debug |= SLAB_TRACE;
			break;
		case 'a':
			slqb_debug |= SLAB_FAILSLAB;
			break;
		default:
			printk(KERN_ERR "slqb_debug option '%c' "
				"unknown. skipped\n", *str);
		}
	}

check_slabs:
	if (*str == ',')
		slqb_debug_slabs = str + 1;
out:
	return 1;
}
__setup("slqb_debug", setup_slqb_debug);

static int __init setup_slqb_min_order(char *str)
{
	get_option(&str, &slqb_min_order);
	slqb_min_order = min(slqb_min_order, MAX_ORDER - 1);

	return 1;
}
__setup("slqb_min_order=", setup_slqb_min_order);

static int __init setup_slqb_min_objects(char *str)
{
	get_option(&str, &slqb_min_objects);

	return 1;
}

__setup("slqb_min_objects=", setup_slqb_min_objects);

static unsigned long kmem_cache_flags(unsigned long objsize,
				unsigned long flags, const char *name,
				void (*ctor)(void *))
{
	/*
	 * Enable debugging if selected on the kernel commandline.
	 */
	if (slqb_debug && (!slqb_debug_slabs ||
	    strncmp(slqb_debug_slabs, name,
		strlen(slqb_debug_slabs)) == 0))
			flags |= slqb_debug;

	if (num_possible_nodes() > 1)
		flags |= SLAB_NUMA;

	return flags;
}
#else
static inline void setup_object_debug(struct kmem_cache *s,
			struct slqb_page *page, void *object)
{
}

static inline int alloc_debug_processing(struct kmem_cache *s,
			void *object, unsigned long addr)
{
	return 0;
}

static inline int free_debug_processing(struct kmem_cache *s,
			void *object, unsigned long addr)
{
	return 0;
}

static inline int slab_pad_check(struct kmem_cache *s, struct slqb_page *page)
{
	return 1;
}

static inline int check_object(struct kmem_cache *s, struct slqb_page *page,
			void *object, int active)
{
	return 1;
}

static inline void add_full(struct kmem_cache_node *n, struct slqb_page *page)
{
}

static inline unsigned long kmem_cache_flags(unsigned long objsize,
	unsigned long flags, const char *name, void (*ctor)(void *))
{
	if (num_possible_nodes() > 1)
		flags |= SLAB_NUMA;
	return flags;
}

static const int slqb_debug;
#endif

/*
 * allocate a new slab (return its corresponding struct slqb_page)
 */
static struct slqb_page *allocate_slab(struct kmem_cache *s,
					gfp_t flags, int node)
{
	struct slqb_page *page;
	int pages = 1 << s->order;

	flags |= s->allocflags;

	page = (struct slqb_page *)alloc_pages_node(node, flags, s->order);
	if (!page)
		return NULL;

	mod_zone_page_state(slqb_page_zone(page),
		(s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE : NR_SLAB_UNRECLAIMABLE,
		pages);

	return page;
}

/*
 * Called once for each object on a new slab page
 */
static void setup_object(struct kmem_cache *s,
				struct slqb_page *page, void *object)
{
	setup_object_debug(s, page, object);
	if (unlikely(s->ctor))
		s->ctor(object);
}

/*
 * Allocate a new slab, set up its object list.
 */
static struct slqb_page *new_slab_page(struct kmem_cache *s,
				gfp_t flags, int node, unsigned int colour)
{
	struct slqb_page *page;
	void *start;
	void *last;
	void *p;

	BUG_ON(flags & GFP_SLAB_BUG_MASK);

	page = allocate_slab(s,
		flags & (GFP_RECLAIM_MASK | GFP_CONSTRAINT_MASK), node);
	if (!page)
		goto out;

	page->flags |= PG_SLQB_BIT;

	start = page_address(&page->page);

	if (unlikely(slab_poison(s)))
		memset(start, POISON_INUSE, PAGE_SIZE << s->order);

	start += colour;

	last = start;
	for_each_object(p, s, start) {
		setup_object(s, page, p);
		set_freepointer(s, last, p);
		last = p;
	}
	set_freepointer(s, last, NULL);

	page->freelist = start;
	page->inuse = 0;
out:
	return page;
}

/*
 * Free a slab page back to the page allocator
 */
static void __free_slab(struct kmem_cache *s, struct slqb_page *page)
{
	int pages = 1 << s->order;

	if (unlikely(slab_debug(s))) {
		void *p;

		slab_pad_check(s, page);
		for_each_free_object(p, s, page->freelist)
			check_object(s, page, p, 0);
	}

	mod_zone_page_state(slqb_page_zone(page),
		(s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE : NR_SLAB_UNRECLAIMABLE,
		-pages);

	__free_slqb_pages(page, s->order, pages);
}

static void rcu_free_slab(struct rcu_head *h)
{
	struct slqb_page *page;

	page = container_of(h, struct slqb_page, rcu_head);
	__free_slab(page->list->cache, page);
}

static void free_slab(struct kmem_cache *s, struct slqb_page *page)
{
	VM_BUG_ON(page->inuse);
	if (unlikely(s->flags & SLAB_DESTROY_BY_RCU))
		call_rcu(&page->rcu_head, rcu_free_slab);
	else
		__free_slab(s, page);
}

/*
 * Return an object to its slab.
 *
 * Caller must be the owner CPU in the case of per-CPU list, or hold the node's
 * list_lock in the case of per-node list.
 */
static int free_object_to_page(struct kmem_cache *s,
			struct kmem_cache_list *l, struct slqb_page *page,
			void *object)
{
	VM_BUG_ON(page->list != l);

	set_freepointer(s, object, page->freelist);
	page->freelist = object;
	page->inuse--;

	if (!page->inuse) {
		if (likely(s->objects > 1)) {
			l->nr_partial--;
			list_del(&page->lru);
		}
		l->nr_slabs--;
		free_slab(s, page);
		slqb_stat_inc(l, FLUSH_SLAB_FREE);
		return 1;

	} else if (page->inuse + 1 == s->objects) {
		l->nr_partial++;
		list_add(&page->lru, &l->partial);
		slqb_stat_inc(l, FLUSH_SLAB_PARTIAL);
		return 0;
	}
	return 0;
}

#ifdef CONFIG_SMP
static void slab_free_to_remote(struct kmem_cache *s, struct slqb_page *page,
				void *object, struct kmem_cache_cpu *c);
#endif

/*
 * Flush the LIFO list of objects on a list. They are sent back to their pages
 * in case the pages also belong to the list, or to our CPU's remote-free list
 * in the case they do not.
 *
 * Doesn't flush the entire list. flush_free_list_all does.
 *
 * Caller must be the owner CPU in the case of per-CPU list, or hold the node's
 * list_lock in the case of per-node list.
 */
static void flush_free_list(struct kmem_cache *s, struct kmem_cache_list *l)
{
	void **head;
	int nr;
	int locked = 0;

	nr = l->freelist.nr;
	if (unlikely(!nr))
		return;

	nr = min(slab_freebatch(s), nr);

	slqb_stat_inc(l, FLUSH_FREE_LIST);
	slqb_stat_add(l, FLUSH_FREE_LIST_OBJECTS, nr);

	l->freelist.nr -= nr;
	head = l->freelist.head;

	do {
		struct slqb_page *page;
		void **object;

		object = head;
		VM_BUG_ON(!object);
		head = get_freepointer(s, object);
		page = virt_to_head_slqb_page(object);

#ifdef CONFIG_SMP
		if (page->list != l) {
			struct kmem_cache_cpu *c;

			if (locked) {
				spin_unlock(&l->page_lock);
				locked = 0;
			}

			c = get_cpu_slab(s, smp_processor_id());

			slab_free_to_remote(s, page, object, c);
			slqb_stat_inc(l, FLUSH_FREE_LIST_REMOTE);
		} else
#endif
		{
			if (!locked) {
				spin_lock(&l->page_lock);
				locked = 1;
			}
			free_object_to_page(s, l, page, object);
		}

		nr--;
	} while (nr);

	if (locked)
		spin_unlock(&l->page_lock);

	l->freelist.head = head;
	if (!l->freelist.nr)
		l->freelist.tail = NULL;
}

static void flush_free_list_all(struct kmem_cache *s, struct kmem_cache_list *l)
{
	while (l->freelist.nr)
		flush_free_list(s, l);
}

#ifdef CONFIG_SMP
/*
 * If enough objects have been remotely freed back to this list,
 * remote_free_check will be set. In which case, we'll eventually come here
 * to take those objects off our remote_free list and onto our LIFO freelist.
 *
 * Caller must be the owner CPU in the case of per-CPU list, or hold the node's
 * list_lock in the case of per-node list.
 */
static void claim_remote_free_list(struct kmem_cache *s,
					struct kmem_cache_list *l)
{
	void **head, **tail;
	int nr;

	if (!l->remote_free.list.nr)
		return;

	spin_lock(&l->remote_free.lock);

	l->remote_free_check = 0;
	head = l->remote_free.list.head;
	l->remote_free.list.head = NULL;
	tail = l->remote_free.list.tail;
	l->remote_free.list.tail = NULL;
	nr = l->remote_free.list.nr;
	l->remote_free.list.nr = 0;

	spin_unlock(&l->remote_free.lock);

	VM_BUG_ON(!nr);

	if (!l->freelist.nr) {
		/* Get head hot for likely subsequent allocation or flush */
		prefetchw(head);
		l->freelist.head = head;
	} else
		set_freepointer(s, l->freelist.tail, head);
	l->freelist.tail = tail;

	l->freelist.nr += nr;

	slqb_stat_inc(l, CLAIM_REMOTE_LIST);
	slqb_stat_add(l, CLAIM_REMOTE_LIST_OBJECTS, nr);
}
#else
static inline void claim_remote_free_list(struct kmem_cache *s,
					struct kmem_cache_list *l)
{
}
#endif

/*
 * Allocation fastpath. Get an object from the list's LIFO freelist, or
 * return NULL if it is empty.
 *
 * Caller must be the owner CPU in the case of per-CPU list, or hold the node's
 * list_lock in the case of per-node list.
 */
static __always_inline void *__cache_list_get_object(struct kmem_cache *s,
						struct kmem_cache_list *l)
{
	void *object;

	object = l->freelist.head;
	if (likely(object)) {
		void *next = get_freepointer(s, object);

		VM_BUG_ON(!l->freelist.nr);
		l->freelist.nr--;
		l->freelist.head = next;

		return object;
	}
	VM_BUG_ON(l->freelist.nr);

#ifdef CONFIG_SMP
	if (unlikely(l->remote_free_check)) {
		claim_remote_free_list(s, l);

		if (l->freelist.nr > slab_hiwater(s))
			flush_free_list(s, l);

		/* repetition here helps gcc :( */
		object = l->freelist.head;
		if (likely(object)) {
			void *next = get_freepointer(s, object);

			VM_BUG_ON(!l->freelist.nr);
			l->freelist.nr--;
			l->freelist.head = next;

			return object;
		}
		VM_BUG_ON(l->freelist.nr);
	}
#endif

	return NULL;
}

/*
 * Slow(er) path. Get a page from this list's existing pages. Will be a
 * new empty page in the case that __slab_alloc_page has just been called
 * (empty pages otherwise never get queued up on the lists), or a partial page
 * already on the list.
 *
 * Caller must be the owner CPU in the case of per-CPU list, or hold the node's
 * list_lock in the case of per-node list.
 */
static noinline void *__cache_list_get_page(struct kmem_cache *s,
				struct kmem_cache_list *l)
{
	struct slqb_page *page;
	void *object;

	if (unlikely(!l->nr_partial))
		return NULL;

	page = list_first_entry(&l->partial, struct slqb_page, lru);
	VM_BUG_ON(page->inuse == s->objects);
	if (page->inuse + 1 == s->objects) {
		l->nr_partial--;
		list_del(&page->lru);
	}

	VM_BUG_ON(!page->freelist);

	page->inuse++;

	object = page->freelist;
	page->freelist = get_freepointer(s, object);
	if (page->freelist)
		prefetchw(page->freelist);
	VM_BUG_ON((page->inuse == s->objects) != (page->freelist == NULL));
	slqb_stat_inc(l, ALLOC_SLAB_FILL);

	return object;
}

static void *cache_list_get_page(struct kmem_cache *s,
				struct kmem_cache_list *l)
{
	void *object;

	if (unlikely(!l->nr_partial))
		return NULL;

	spin_lock(&l->page_lock);
	object = __cache_list_get_page(s, l);
	spin_unlock(&l->page_lock);

	return object;
}

/*
 * Allocation slowpath. Allocate a new slab page from the page allocator, and
 * put it on the list's partial list. Must be followed by an allocation so
 * that we don't have dangling empty pages on the partial list.
 *
 * Returns 0 on allocation failure.
 *
 * Must be called with interrupts disabled.
 */
static noinline void *__slab_alloc_page(struct kmem_cache *s,
				gfp_t gfpflags, int node)
{
	struct slqb_page *page;
	struct kmem_cache_list *l;
	struct kmem_cache_cpu *c;
	unsigned int colour;
	void *object;

	c = get_cpu_slab(s, smp_processor_id());
	colour = c->colour_next;
	c->colour_next += s->colour_off;
	if (c->colour_next >= s->colour_range)
		c->colour_next = 0;

	/* Caller handles __GFP_ZERO */
	gfpflags &= ~__GFP_ZERO;

	if (gfpflags & __GFP_WAIT)
		local_irq_enable();
	page = new_slab_page(s, gfpflags, node, colour);
	if (gfpflags & __GFP_WAIT)
		local_irq_disable();
	if (unlikely(!page))
		return page;

	if (!NUMA_BUILD || likely(slqb_page_to_nid(page) == numa_node_id())) {
		struct kmem_cache_cpu *c;
		int cpu = smp_processor_id();

		c = get_cpu_slab(s, cpu);
		l = &c->list;
		page->list = l;

		spin_lock(&l->page_lock);
		l->nr_slabs++;
		l->nr_partial++;
		list_add(&page->lru, &l->partial);
		slqb_stat_inc(l, ALLOC);
		slqb_stat_inc(l, ALLOC_SLAB_NEW);
		object = __cache_list_get_page(s, l);
		spin_unlock(&l->page_lock);
	} else {
#ifdef CONFIG_NUMA
		struct kmem_cache_node *n;

		n = s->node_slab[slqb_page_to_nid(page)];
		l = &n->list;
		page->list = l;

		spin_lock(&n->list_lock);
		spin_lock(&l->page_lock);
		l->nr_slabs++;
		l->nr_partial++;
		list_add(&page->lru, &l->partial);
		slqb_stat_inc(l, ALLOC);
		slqb_stat_inc(l, ALLOC_SLAB_NEW);
		object = __cache_list_get_page(s, l);
		spin_unlock(&l->page_lock);
		spin_unlock(&n->list_lock);
#endif
	}
	VM_BUG_ON(!object);
	return object;
}

#ifdef CONFIG_NUMA
static noinline int alternate_nid(struct kmem_cache *s,
				gfp_t gfpflags, int node)
{
	if (in_interrupt() || (gfpflags & __GFP_THISNODE))
		return node;
	if (cpuset_do_slab_mem_spread() && (s->flags & SLAB_MEM_SPREAD))
		return cpuset_mem_spread_node();
	else if (current->mempolicy)
		return slab_node(current->mempolicy);
	return node;
}

/*
 * Allocate an object from a remote node. Return NULL if none could be found
 * (in which case, caller should allocate a new slab)
 *
 * Must be called with interrupts disabled.
 */
static void *__remote_slab_alloc_node(struct kmem_cache *s,
				gfp_t gfpflags, int node)
{
	struct kmem_cache_node *n;
	struct kmem_cache_list *l;
	void *object;

	n = s->node_slab[node];
	if (unlikely(!n)) /* node has no memory */
		return NULL;
	l = &n->list;

	spin_lock(&n->list_lock);

	object = __cache_list_get_object(s, l);
	if (unlikely(!object)) {
		object = cache_list_get_page(s, l);
		if (unlikely(!object)) {
			spin_unlock(&n->list_lock);
			return __slab_alloc_page(s, gfpflags, node);
		}
	}
	if (likely(object))
		slqb_stat_inc(l, ALLOC);
	spin_unlock(&n->list_lock);
	return object;
}

static noinline void *__remote_slab_alloc(struct kmem_cache *s,
				gfp_t gfpflags, int node)
{
	void *object;
	struct zonelist *zonelist;
	struct zoneref *z;
	struct zone *zone;
	enum zone_type high_zoneidx = gfp_zone(gfpflags);

	object = __remote_slab_alloc_node(s, gfpflags, node);
	if (likely(object || (gfpflags & __GFP_THISNODE)))
		return object;

	zonelist = node_zonelist(slab_node(current->mempolicy), gfpflags);
	for_each_zone_zonelist(zone, z, zonelist, high_zoneidx) {
		if (!cpuset_zone_allowed_hardwall(zone, gfpflags))
			continue;

		node = zone_to_nid(zone);
		object = __remote_slab_alloc_node(s, gfpflags, node);
		if (likely(object))
			return object;
	}
	return NULL;
}
#endif

/*
 * Main allocation path. Return an object, or NULL on allocation failure.
 *
 * Must be called with interrupts disabled.
 */
static __always_inline void *__slab_alloc(struct kmem_cache *s,
				gfp_t gfpflags, int node)
{
	void *object;
	struct kmem_cache_cpu *c;
	struct kmem_cache_list *l;

#ifdef CONFIG_NUMA
	if (unlikely(node != -1) && unlikely(node != numa_node_id())) {
try_remote:
		return __remote_slab_alloc(s, gfpflags, node);
	}
#endif

	c = get_cpu_slab(s, smp_processor_id());
	VM_BUG_ON(!c);
	l = &c->list;
	object = __cache_list_get_object(s, l);
	if (unlikely(!object)) {
#ifdef CONFIG_NUMA
		int thisnode = numa_node_id();

		/*
		 * If the local node is memoryless, try remote alloc before
		 * trying the page allocator. Otherwise, what happens is
		 * objects are always freed to remote lists but the allocation
		 * side always allocates a new page with only one object
		 * used in each page
		 */
		if (unlikely(!node_state(thisnode, N_HIGH_MEMORY)))
			object = __remote_slab_alloc(s, gfpflags, thisnode);
#endif

		if (!object) {
			object = cache_list_get_page(s, l);
			if (unlikely(!object)) {
				object = __slab_alloc_page(s, gfpflags, node);
#ifdef CONFIG_NUMA
				if (unlikely(!object)) {
					node = numa_node_id();
					goto try_remote;
				}
#endif
				return object;
			}
		}
	}
	if (likely(object))
		slqb_stat_inc(l, ALLOC);
	return object;
}

/*
 * Perform some interrupts-on processing around the main allocation path
 * (debug checking and memset()ing).
 */
static __always_inline void *slab_alloc(struct kmem_cache *s,
				gfp_t gfpflags, int node, unsigned long addr)
{
	void *object;
	unsigned long flags;

	gfpflags &= gfp_allowed_mask;

	lockdep_trace_alloc(gfpflags);
	might_sleep_if(gfpflags & __GFP_WAIT);

	if (should_failslab(s->objsize, gfpflags, s->flags))
		return NULL;

again:
	local_irq_save(flags);
	object = __slab_alloc(s, gfpflags, node);
	local_irq_restore(flags);

	if (unlikely(slab_debug(s)) && likely(object)) {
		if (unlikely(!alloc_debug_processing(s, object, addr)))
			goto again;
	}

	if (unlikely(gfpflags & __GFP_ZERO) && likely(object))
		memset(object, 0, s->objsize);

	return object;
}

static __always_inline void *__kmem_cache_alloc(struct kmem_cache *s,
				gfp_t gfpflags, unsigned long caller)
{
	int node = -1;

#ifdef CONFIG_NUMA
	if (unlikely(current->flags & (PF_SPREAD_SLAB | PF_MEMPOLICY)))
		node = alternate_nid(s, gfpflags, node);
#endif
	return slab_alloc(s, gfpflags, node, caller);
}

void *kmem_cache_alloc(struct kmem_cache *s, gfp_t gfpflags)
{
	return __kmem_cache_alloc(s, gfpflags, _RET_IP_);
}
EXPORT_SYMBOL(kmem_cache_alloc);

#ifdef CONFIG_NUMA
void *kmem_cache_alloc_node(struct kmem_cache *s, gfp_t gfpflags, int node)
{
	return slab_alloc(s, gfpflags, node, _RET_IP_);
}
EXPORT_SYMBOL(kmem_cache_alloc_node);
#endif

#ifdef CONFIG_SMP
/*
 * Flush this CPU's remote free list of objects back to the list from where
 * they originate. They end up on that list's remotely freed list, and
 * eventually we set it's remote_free_check if there are enough objects on it.
 *
 * This seems convoluted, but it keeps is from stomping on the target CPU's
 * fastpath cachelines.
 *
 * Must be called with interrupts disabled.
 */
static void flush_remote_free_cache(struct kmem_cache *s,
				struct kmem_cache_cpu *c)
{
	struct kmlist *src;
	struct kmem_cache_list *dst;
	unsigned int nr;
	int set;

	src = &c->rlist;
	nr = src->nr;
	if (unlikely(!nr))
		return;

#ifdef CONFIG_SLQB_STATS
	{
		struct kmem_cache_list *l = &c->list;

		slqb_stat_inc(l, FLUSH_RFREE_LIST);
		slqb_stat_add(l, FLUSH_RFREE_LIST_OBJECTS, nr);
	}
#endif

	dst = c->remote_cache_list;

	/*
	 * Less common case, dst is filling up so free synchronously.
	 * No point in having remote CPU free thse as it will just
	 * free them back to the page list anyway.
	 */
	if (unlikely(dst->remote_free.list.nr > (slab_hiwater(s) >> 1))) {
		void **head;

		head = src->head;
		spin_lock(&dst->page_lock);
		do {
			struct slqb_page *page;
			void **object;

			object = head;
			VM_BUG_ON(!object);
			head = get_freepointer(s, object);
			page = virt_to_head_slqb_page(object);

			free_object_to_page(s, dst, page, object);
			nr--;
		} while (nr);
		spin_unlock(&dst->page_lock);

		src->head = NULL;
		src->tail = NULL;
		src->nr = 0;

		return;
	}

	spin_lock(&dst->remote_free.lock);

	if (!dst->remote_free.list.head)
		dst->remote_free.list.head = src->head;
	else
		set_freepointer(s, dst->remote_free.list.tail, src->head);
	dst->remote_free.list.tail = src->tail;

	src->head = NULL;
	src->tail = NULL;
	src->nr = 0;

	if (dst->remote_free.list.nr < slab_freebatch(s))
		set = 1;
	else
		set = 0;

	dst->remote_free.list.nr += nr;

	if (unlikely(dst->remote_free.list.nr >= slab_freebatch(s) && set))
		dst->remote_free_check = 1;

	spin_unlock(&dst->remote_free.lock);
}

/*
 * Free an object to this CPU's remote free list.
 *
 * Must be called with interrupts disabled.
 */
static noinline void slab_free_to_remote(struct kmem_cache *s,
				struct slqb_page *page, void *object,
				struct kmem_cache_cpu *c)
{
	struct kmlist *r;

	/*
	 * Our remote free list corresponds to a different list. Must
	 * flush it and switch.
	 */
	if (page->list != c->remote_cache_list) {
		flush_remote_free_cache(s, c);
		c->remote_cache_list = page->list;
	}

	r = &c->rlist;
	if (!r->head)
		r->head = object;
	else
		set_freepointer(s, r->tail, object);
	set_freepointer(s, object, NULL);
	r->tail = object;
	r->nr++;

	if (unlikely(r->nr >= slab_freebatch(s)))
		flush_remote_free_cache(s, c);
}
#endif

/*
 * Main freeing path. Return an object, or NULL on allocation failure.
 *
 * Must be called with interrupts disabled.
 */
static __always_inline void __slab_free(struct kmem_cache *s,
				struct slqb_page *page, void *object)
{
	struct kmem_cache_cpu *c;
	struct kmem_cache_list *l;
	int thiscpu = smp_processor_id();

	c = get_cpu_slab(s, thiscpu);
	l = &c->list;

	slqb_stat_inc(l, FREE);

	if (!NUMA_BUILD || !slab_numa(s) ||
			likely(slqb_page_to_nid(page) == numa_node_id())) {
		/*
		 * Freeing fastpath. Collects all local-node objects, not
		 * just those allocated from our per-CPU list. This allows
		 * fast transfer of objects from one CPU to another within
		 * a given node.
		 */
		set_freepointer(s, object, l->freelist.head);
		l->freelist.head = object;
		if (!l->freelist.nr)
			l->freelist.tail = object;
		l->freelist.nr++;

		if (unlikely(l->freelist.nr > slab_hiwater(s)))
			flush_free_list(s, l);

	} else {
#ifdef CONFIG_SMP
		/*
		 * Freeing an object that was allocated on a remote node.
		 */
		slab_free_to_remote(s, page, object, c);
		slqb_stat_inc(l, FREE_REMOTE);
#endif
	}
}

/*
 * Perform some interrupts-on processing around the main freeing path
 * (debug checking).
 */
static __always_inline void slab_free(struct kmem_cache *s,
				struct slqb_page *page, void *object)
{
	unsigned long flags;

	prefetchw(object);

	debug_check_no_locks_freed(object, s->objsize);
	if (likely(object) && unlikely(slab_debug(s))) {
		if (unlikely(!free_debug_processing(s, object, _RET_IP_)))
			return;
	}

	local_irq_save(flags);
	__slab_free(s, page, object);
	local_irq_restore(flags);
}

void kmem_cache_free(struct kmem_cache *s, void *object)
{
	struct slqb_page *page = NULL;

	if (slab_numa(s))
		page = virt_to_head_slqb_page(object);
	slab_free(s, page, object);
}
EXPORT_SYMBOL(kmem_cache_free);

/*
 * Calculate the order of allocation given an slab object size.
 *
 * Order 0 allocations are preferred since order 0 does not cause fragmentation
 * in the page allocator, and they have fastpaths in the page allocator. But
 * also minimise external fragmentation with large objects.
 */
static int slab_order(int size, int max_order, int frac)
{
	int order;

	if (fls(size - 1) <= PAGE_SHIFT)
		order = 0;
	else
		order = fls(size - 1) - PAGE_SHIFT;
	if (order < slqb_min_order)
		order = slqb_min_order;

	while (order <= max_order) {
		unsigned long slab_size = PAGE_SIZE << order;
		unsigned long objects;
		unsigned long waste;

		objects = slab_size / size;
		if (!objects)
			goto next;

		if (order < MAX_ORDER && objects < slqb_min_objects) {
			/*
			 * if we don't have enough objects for min_objects,
			 * then try the next size up. Unless we have reached
			 * our maximum possible page size.
			 */
			goto next;
		}

		waste = slab_size - (objects * size);

		if (waste * frac <= slab_size)
			break;

next:
		order++;
	}

	return order;
}

static int calculate_order(int size)
{
	int order;

	/*
	 * Attempt to find best configuration for a slab. This
	 * works by first attempting to generate a layout with
	 * the best configuration and backing off gradually.
	 */
	order = slab_order(size, 1, 4);
	if (order <= 1)
		return order;

	/*
	 * This size cannot fit in order-1. Allow bigger orders, but
	 * forget about trying to save space.
	 */
	order = slab_order(size, MAX_ORDER - 1, 0);
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

static void init_kmem_cache_list(struct kmem_cache *s,
				struct kmem_cache_list *l)
{
	l->cache		= s;
	l->freelist.nr		= 0;
	l->freelist.head	= NULL;
	l->freelist.tail	= NULL;
	l->nr_partial		= 0;
	l->nr_slabs		= 0;
	INIT_LIST_HEAD(&l->partial);
	spin_lock_init(&l->page_lock);

#ifdef CONFIG_SMP
	l->remote_free_check	= 0;
	spin_lock_init(&l->remote_free.lock);
	l->remote_free.list.nr	= 0;
	l->remote_free.list.head = NULL;
	l->remote_free.list.tail = NULL;
#endif

#ifdef CONFIG_SLQB_STATS
	memset(l->stats, 0, sizeof(l->stats));
#endif
}

static void init_kmem_cache_cpu(struct kmem_cache *s,
				struct kmem_cache_cpu *c)
{
	init_kmem_cache_list(s, &c->list);

	c->colour_next		= 0;
#ifdef CONFIG_SMP
	c->rlist.nr		= 0;
	c->rlist.head		= NULL;
	c->rlist.tail		= NULL;
	c->remote_cache_list	= NULL;
#endif
}

#ifdef CONFIG_NUMA
static void init_kmem_cache_node(struct kmem_cache *s,
				struct kmem_cache_node *n)
{
	spin_lock_init(&n->list_lock);
	init_kmem_cache_list(s, &n->list);
}
#endif

/* Initial slabs. */
#ifdef CONFIG_SMP
static DEFINE_PER_CPU(struct kmem_cache_cpu, kmem_cache_cpus);
#endif
#ifdef CONFIG_NUMA
/* XXX: really need a DEFINE_PER_NODE for per-node data because a static
 *      array is wasteful */
static struct kmem_cache_node kmem_cache_nodes[MAX_NUMNODES];
#endif

#ifdef CONFIG_SMP
static struct kmem_cache kmem_cpu_cache;
static DEFINE_PER_CPU(struct kmem_cache_cpu, kmem_cpu_cpus);
#ifdef CONFIG_NUMA
static struct kmem_cache_node kmem_cpu_nodes[MAX_NUMNODES]; /* XXX per-nid */
#endif
#endif

#ifdef CONFIG_NUMA
static struct kmem_cache kmem_node_cache;
#ifdef CONFIG_SMP
static DEFINE_PER_CPU(struct kmem_cache_cpu, kmem_node_cpus);
#endif
static struct kmem_cache_node kmem_node_nodes[MAX_NUMNODES]; /*XXX per-nid */
#endif

#ifdef CONFIG_SMP
static struct kmem_cache_cpu *alloc_kmem_cache_cpu(struct kmem_cache *s,
				int cpu)
{
	struct kmem_cache_cpu *c;
	int node;

	node = cpu_to_node(cpu);

	c = kmem_cache_alloc_node(&kmem_cpu_cache, GFP_KERNEL, node);
	if (!c)
		return NULL;

	init_kmem_cache_cpu(s, c);
	return c;
}

static void free_kmem_cache_cpus(struct kmem_cache *s)
{
	int cpu;

	for_each_online_cpu(cpu) {
		struct kmem_cache_cpu *c;

		c = s->cpu_slab[cpu];
		if (c) {
			kmem_cache_free(&kmem_cpu_cache, c);
			s->cpu_slab[cpu] = NULL;
		}
	}
}

static int alloc_kmem_cache_cpus(struct kmem_cache *s)
{
	int cpu;

	for_each_online_cpu(cpu) {
		struct kmem_cache_cpu *c;

		c = s->cpu_slab[cpu];
		if (c)
			continue;

		c = alloc_kmem_cache_cpu(s, cpu);
		if (!c) {
			free_kmem_cache_cpus(s);
			return 0;
		}
		s->cpu_slab[cpu] = c;
	}
	return 1;
}

#else
static inline void free_kmem_cache_cpus(struct kmem_cache *s)
{
}

static inline int alloc_kmem_cache_cpus(struct kmem_cache *s)
{
	init_kmem_cache_cpu(s, &s->cpu_slab);
	return 1;
}
#endif

#ifdef CONFIG_NUMA
static void free_kmem_cache_nodes(struct kmem_cache *s)
{
	int node;

	for_each_node_state(node, N_NORMAL_MEMORY) {
		struct kmem_cache_node *n;

		n = s->node_slab[node];
		if (n) {
			kmem_cache_free(&kmem_node_cache, n);
			s->node_slab[node] = NULL;
		}
	}
}

static int alloc_kmem_cache_nodes(struct kmem_cache *s)
{
	int node;

	for_each_node_state(node, N_NORMAL_MEMORY) {
		struct kmem_cache_node *n;

		n = kmem_cache_alloc_node(&kmem_node_cache, GFP_KERNEL, node);
		if (!n) {
			free_kmem_cache_nodes(s);
			return 0;
		}
		init_kmem_cache_node(s, n);
		s->node_slab[node] = n;
	}
	return 1;
}
#else
static void free_kmem_cache_nodes(struct kmem_cache *s)
{
}

static int alloc_kmem_cache_nodes(struct kmem_cache *s)
{
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
	if (slab_poison(s) && !(flags & SLAB_DESTROY_BY_RCU) && !s->ctor)
		s->flags |= __OBJECT_POISON;
	else
		s->flags &= ~__OBJECT_POISON;

	/*
	 * Round up object size to the next word boundary. We can only
	 * place the free pointer at word boundaries and this determines
	 * the possible location of the free pointer.
	 */
	size = ALIGN(size, sizeof(void *));

#ifdef CONFIG_SLQB_DEBUG
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

	if (((flags & (SLAB_DESTROY_BY_RCU | SLAB_POISON)) || s->ctor)) {
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

#ifdef CONFIG_SLQB_DEBUG
	if (flags & SLAB_STORE_USER) {
		/*
		 * Need to store information about allocs and frees after
		 * the object.
		 */
		size += 2 * sizeof(struct track);
	}

	if (flags & SLAB_RED_ZONE) {
		/*
		 * Add some empty padding so that we can catch
		 * overwrites from earlier objects rather than let
		 * tracking information or the free pointer be
		 * corrupted if an user writes before the start
		 * of the object.
		 */
		size += sizeof(void *);
	}
#endif

	/*
	 * Determine the alignment based on various parameters that the
	 * user specified and the dynamic determination of cache line size
	 * on bootup.
	 */
	align = calculate_alignment(flags, align, s->objsize);

	/*
	 * SLQB stores one object immediately after another beginning from
	 * offset 0. In order to align the objects we have to simply size
	 * each object to conform to the alignment.
	 */
	size = ALIGN(size, align);
	s->size = size;
	s->order = calculate_order(size);

	if (s->order < 0)
		return 0;

	s->allocflags = 0;
	if (s->order)
		s->allocflags |= __GFP_COMP;

	if (s->flags & SLAB_CACHE_DMA)
		s->allocflags |= SLQB_DMA;

	if (s->flags & SLAB_RECLAIM_ACCOUNT)
		s->allocflags |= __GFP_RECLAIMABLE;

	/*
	 * Determine the number of objects per slab
	 */
	s->objects = (PAGE_SIZE << s->order) / size;

	s->freebatch = max(4UL*PAGE_SIZE / size,
				min(256UL, 64*PAGE_SIZE / size));
	if (!s->freebatch)
		s->freebatch = 1;
	s->hiwater = s->freebatch << 2;

	return !!s->objects;

}

#ifdef CONFIG_SMP
/*
 * Per-cpu allocator can't be used because it always uses slab allocator,
 * and it can't do per-node allocations.
 */
static void *kmem_cache_dyn_array_alloc(int ids)
{
	size_t size = sizeof(void *) * ids;

	BUG_ON(!size);

	if (unlikely(!slab_is_available())) {
		static void *nextmem;
		static size_t nextleft;
		void *ret;

		/*
		 * Special case for setting up initial caches. These will
		 * never get freed by definition so we can do it rather
		 * simply.
		 */
		if (size > nextleft) {
			nextmem = alloc_pages_exact(size, GFP_KERNEL);
			if (!nextmem)
				return NULL;
			nextleft = roundup(size, PAGE_SIZE);
		}

		ret = nextmem;
		nextleft -= size;
		nextmem += size;
		memset(ret, 0, size);
		return ret;
	} else {
		return kzalloc(size, GFP_KERNEL);
	}
}

static void kmem_cache_dyn_array_free(void *array)
{
	if (unlikely(!slab_is_available()))
		return; /* error case without crashing here (will panic soon) */
	kfree(array);
}
#endif

/*
 * Except in early boot, this should be called with slqb_lock held for write
 * to lock out hotplug, and protect list modifications.
 */
static int kmem_cache_open(struct kmem_cache *s,
			const char *name, size_t size, size_t align,
			unsigned long flags, void (*ctor)(void *), int alloc)
{
	unsigned int left_over;

	memset(s, 0, sizeof(struct kmem_cache));
	s->name = name;
	s->ctor = ctor;
	s->objsize = size;
	s->align = align;
	s->flags = kmem_cache_flags(size, flags, name, ctor);

	if (!calculate_sizes(s))
		goto error;

	if (!slab_debug(s)) {
		left_over = (PAGE_SIZE << s->order) - (s->objects * s->size);
		s->colour_off = max(cache_line_size(), s->align);
		s->colour_range = left_over;
	} else {
		s->colour_off = 0;
		s->colour_range = 0;
	}

#ifdef CONFIG_SMP
	s->cpu_slab = kmem_cache_dyn_array_alloc(nr_cpu_ids);
	if (!s->cpu_slab)
		goto error;
# ifdef CONFIG_NUMA
	s->node_slab = kmem_cache_dyn_array_alloc(nr_node_ids);
	if (!s->node_slab)
		goto error_cpu_array;
# endif
#endif

	if (likely(alloc)) {
		if (!alloc_kmem_cache_nodes(s))
			goto error_node_array;

		if (!alloc_kmem_cache_cpus(s))
			goto error_nodes;
	}

	sysfs_slab_add(s);
	list_add(&s->list, &slab_caches);

	return 1;

error_nodes:
	free_kmem_cache_nodes(s);
error_node_array:
#if defined(CONFIG_NUMA) && defined(CONFIG_SMP)
	kmem_cache_dyn_array_free(s->node_slab);
error_cpu_array:
#endif
#ifdef CONFIG_SMP
	kmem_cache_dyn_array_free(s->cpu_slab);
#endif
error:
	if (flags & SLAB_PANIC)
		panic("%s: failed to create slab `%s'\n", __func__, name);
	return 0;
}

/**
 * kmem_ptr_validate - check if an untrusted pointer might be a slab entry.
 * @s: the cache we're checking against
 * @ptr: pointer to validate
 *
 * This verifies that the untrusted pointer looks sane;
 * it is _not_ a guarantee that the pointer is actually
 * part of the slab cache in question, but it at least
 * validates that the pointer can be dereferenced and
 * looks half-way sane.
 *
 * Currently only used for dentry validation.
 */
int kmem_ptr_validate(struct kmem_cache *s, const void *ptr)
{
	unsigned long addr = (unsigned long)ptr;
	struct slqb_page *page;

	if (unlikely(addr < PAGE_OFFSET))
		goto out;
	if (unlikely(addr > (unsigned long)high_memory - s->size))
		goto out;
	if (unlikely(!IS_ALIGNED(addr, s->align)))
		goto out;
	if (unlikely(!kern_addr_valid(addr)))
		goto out;
	if (unlikely(!kern_addr_valid(addr + s->size - 1)))
		goto out;
	if (unlikely(!pfn_valid(addr >> PAGE_SHIFT)))
		goto out;
	page = virt_to_head_slqb_page(ptr);
	if (unlikely(!(page->flags & PG_SLQB_BIT)))
		goto out;
	if (unlikely(page->list->cache != s)) /* XXX: ouch, racy */
		goto out;
	return 1;
out:
	return 0;
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
 * Release all resources used by a slab cache. No more concurrency on the
 * slab, so we can touch remote kmem_cache_cpu structures.
 */
void kmem_cache_destroy(struct kmem_cache *s)
{
#ifdef CONFIG_NUMA
	int node;
#endif
	int cpu;

	down_write(&slqb_lock);
	list_del(&s->list);

	local_irq_disable();
#ifdef CONFIG_SMP
	for_each_online_cpu(cpu) {
		struct kmem_cache_cpu *c = get_cpu_slab(s, cpu);
		struct kmem_cache_list *l = &c->list;

		flush_free_list_all(s, l);
		flush_remote_free_cache(s, c);
	}
#endif

	for_each_online_cpu(cpu) {
		struct kmem_cache_cpu *c = get_cpu_slab(s, cpu);
		struct kmem_cache_list *l = &c->list;

		claim_remote_free_list(s, l);
		flush_free_list_all(s, l);

		WARN_ON(l->freelist.nr);
		WARN_ON(l->nr_slabs);
		WARN_ON(l->nr_partial);
	}

	free_kmem_cache_cpus(s);

#ifdef CONFIG_NUMA
	for_each_node_state(node, N_NORMAL_MEMORY) {
		struct kmem_cache_node *n;
		struct kmem_cache_list *l;

		n = s->node_slab[node];
		if (!n)
			continue;
		l = &n->list;

		claim_remote_free_list(s, l);
		flush_free_list_all(s, l);

		WARN_ON(l->freelist.nr);
		WARN_ON(l->nr_slabs);
		WARN_ON(l->nr_partial);
	}

	free_kmem_cache_nodes(s);
#endif
	local_irq_enable();

	sysfs_slab_remove(s);
	up_write(&slqb_lock);
}
EXPORT_SYMBOL(kmem_cache_destroy);

/********************************************************************
 *		Kmalloc subsystem
 *******************************************************************/

struct kmem_cache kmalloc_caches[KMALLOC_SHIFT_SLQB_HIGH + 1] __cacheline_aligned;
EXPORT_SYMBOL(kmalloc_caches);

#ifdef CONFIG_ZONE_DMA
struct kmem_cache kmalloc_caches_dma[KMALLOC_SHIFT_SLQB_HIGH + 1] __cacheline_aligned;
EXPORT_SYMBOL(kmalloc_caches_dma);
#endif

#ifndef ARCH_KMALLOC_FLAGS
#define ARCH_KMALLOC_FLAGS SLAB_HWCACHE_ALIGN
#endif

static struct kmem_cache *open_kmalloc_cache(struct kmem_cache *s,
				const char *name, int size, gfp_t gfp_flags)
{
	unsigned int flags = ARCH_KMALLOC_FLAGS | SLAB_PANIC;

	if (gfp_flags & SLQB_DMA)
		flags |= SLAB_CACHE_DMA;

	kmem_cache_open(s, name, size, ARCH_KMALLOC_MINALIGN, flags, NULL, 1);

	return s;
}

/*
 * Conversion table for small slabs sizes / 8 to the index in the
 * kmalloc array. This is necessary for slabs < 192 since we have non power
 * of two cache sizes there. The size of larger slabs can be determined using
 * fls.
 */
static s8 size_index[24] __cacheline_aligned = {
	3,	/* 8 */
	4,	/* 16 */
	5,	/* 24 */
	5,	/* 32 */
	6,	/* 40 */
	6,	/* 48 */
	6,	/* 56 */
	6,	/* 64 */
#if L1_CACHE_BYTES < 64
	1,	/* 72 */
	1,	/* 80 */
	1,	/* 88 */
	1,	/* 96 */
#else
	7,
	7,
	7,
	7,
#endif
	7,	/* 104 */
	7,	/* 112 */
	7,	/* 120 */
	7,	/* 128 */
#if L1_CACHE_BYTES < 128
	2,	/* 136 */
	2,	/* 144 */
	2,	/* 152 */
	2,	/* 160 */
	2,	/* 168 */
	2,	/* 176 */
	2,	/* 184 */
	2	/* 192 */
#else
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1
#endif
};

static struct kmem_cache *get_slab(size_t size, gfp_t flags)
{
	int index;

	if (unlikely(size <= KMALLOC_MIN_SIZE)) {
		if (unlikely(!size))
			return ZERO_SIZE_PTR;

		index = KMALLOC_SHIFT_LOW;
		goto got_index;
	}

#if L1_CACHE_BYTES >= 128
	if (size <= 128) {
#else
	if (size <= 192) {
#endif
		index = size_index[(size - 1) / 8];
	} else {
		if (unlikely(size > 1UL << KMALLOC_SHIFT_SLQB_HIGH))
			return NULL;

		index = fls(size - 1);
	}

got_index:
	if (unlikely((flags & SLQB_DMA)))
		return &kmalloc_caches_dma[index];
	else
		return &kmalloc_caches[index];
}

void *__kmalloc(size_t size, gfp_t flags)
{
	struct kmem_cache *s;

	s = get_slab(size, flags);
	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	return __kmem_cache_alloc(s, flags, _RET_IP_);
}
EXPORT_SYMBOL(__kmalloc);

#ifdef CONFIG_NUMA
void *__kmalloc_node(size_t size, gfp_t flags, int node)
{
	struct kmem_cache *s;

	s = get_slab(size, flags);
	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	return kmem_cache_alloc_node(s, flags, node);
}
EXPORT_SYMBOL(__kmalloc_node);
#endif

size_t ksize(const void *object)
{
	struct slqb_page *page;
	struct kmem_cache *s;

	BUG_ON(!object);
	if (unlikely(object == ZERO_SIZE_PTR))
		return 0;

	page = virt_to_head_slqb_page(object);
	BUG_ON(!(page->flags & PG_SLQB_BIT));

	s = page->list->cache;

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

void kfree(const void *object)
{
	struct kmem_cache *s;
	struct slqb_page *page;

	if (unlikely(ZERO_OR_NULL_PTR(object)))
		return;

	page = virt_to_head_slqb_page(object);
	s = page->list->cache;

	slab_free(s, page, (void *)object);
}
EXPORT_SYMBOL(kfree);

static void kmem_cache_trim_percpu(void *arg)
{
	int cpu = smp_processor_id();
	struct kmem_cache *s = arg;
	struct kmem_cache_cpu *c = get_cpu_slab(s, cpu);
	struct kmem_cache_list *l = &c->list;

	claim_remote_free_list(s, l);
	flush_free_list(s, l);
#ifdef CONFIG_SMP
	flush_remote_free_cache(s, c);
#endif
}

int kmem_cache_shrink(struct kmem_cache *s)
{
#ifdef CONFIG_NUMA
	int node;
#endif

	on_each_cpu(kmem_cache_trim_percpu, s, 1);

#ifdef CONFIG_NUMA
	for_each_node_state(node, N_NORMAL_MEMORY) {
		struct kmem_cache_node *n;
		struct kmem_cache_list *l;

		n = s->node_slab[node];
		if (!n)
			continue;
		l = &n->list;

		spin_lock_irq(&n->list_lock);
		claim_remote_free_list(s, l);
		flush_free_list(s, l);
		spin_unlock_irq(&n->list_lock);
	}
#endif

	return 0;
}
EXPORT_SYMBOL(kmem_cache_shrink);

#if defined(CONFIG_NUMA) && defined(CONFIG_MEMORY_HOTPLUG)
static void kmem_cache_reap_percpu(void *arg)
{
	int cpu = smp_processor_id();
	struct kmem_cache *s;
	long phase = (long)arg;

	list_for_each_entry(s, &slab_caches, list) {
		struct kmem_cache_cpu *c = get_cpu_slab(s, cpu);
		struct kmem_cache_list *l = &c->list;

		if (phase == 0) {
			flush_free_list_all(s, l);
			flush_remote_free_cache(s, c);
		}

		if (phase == 1) {
			claim_remote_free_list(s, l);
			flush_free_list_all(s, l);
		}
	}
}

static void kmem_cache_reap(void)
{
	struct kmem_cache *s;
	int node;

	down_read(&slqb_lock);
	on_each_cpu(kmem_cache_reap_percpu, (void *)0, 1);
	on_each_cpu(kmem_cache_reap_percpu, (void *)1, 1);

	list_for_each_entry(s, &slab_caches, list) {
		for_each_node_state(node, N_NORMAL_MEMORY) {
			struct kmem_cache_node *n;
			struct kmem_cache_list *l;

			n = s->node_slab[node];
			if (!n)
				continue;
			l = &n->list;

			spin_lock_irq(&n->list_lock);
			claim_remote_free_list(s, l);
			flush_free_list_all(s, l);
			spin_unlock_irq(&n->list_lock);
		}
	}
	up_read(&slqb_lock);
}
#endif

static void cache_trim_worker(struct work_struct *w)
{
	struct delayed_work *work =
		container_of(w, struct delayed_work, work);
	struct kmem_cache *s;

	if (!down_read_trylock(&slqb_lock))
		goto out;

	list_for_each_entry(s, &slab_caches, list) {
#ifdef CONFIG_NUMA
		int node = numa_node_id();
		struct kmem_cache_node *n = s->node_slab[node];

		if (n) {
			struct kmem_cache_list *l = &n->list;

			spin_lock_irq(&n->list_lock);
			claim_remote_free_list(s, l);
			flush_free_list(s, l);
			spin_unlock_irq(&n->list_lock);
		}
#endif

		local_irq_disable();
		kmem_cache_trim_percpu(s);
		local_irq_enable();
	}

	up_read(&slqb_lock);
out:
	schedule_delayed_work(work, round_jiffies_relative(3*HZ));
}

static DEFINE_PER_CPU(struct delayed_work, slqb_cache_trim_work);

static void __cpuinit start_cpu_timer(int cpu)
{
	struct delayed_work *cache_trim_work = &per_cpu(slqb_cache_trim_work,
			cpu);

	/*
	 * When this gets called from do_initcalls via cpucache_init(),
	 * init_workqueues() has already run, so keventd will be setup
	 * at that time.
	 */
	if (keventd_up() && cache_trim_work->work.func == NULL) {
		INIT_DELAYED_WORK(cache_trim_work, cache_trim_worker);
		schedule_delayed_work_on(cpu, cache_trim_work,
					__round_jiffies_relative(HZ, cpu));
	}
}

static int __init cpucache_init(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		start_cpu_timer(cpu);

	return 0;
}
device_initcall(cpucache_init);

#if defined(CONFIG_NUMA) && defined(CONFIG_MEMORY_HOTPLUG)
static void slab_mem_going_offline_callback(void *arg)
{
	kmem_cache_reap();
}

static void slab_mem_offline_callback(void *arg)
{
	/* XXX: should release structures, see CPU offline comment */
}

static int slab_mem_going_online_callback(void *arg)
{
	struct kmem_cache *s;
	struct kmem_cache_node *n;
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
	 * We are bringing a node online. No memory is availabe yet. We must
	 * allocate a kmem_cache_node structure in order to bring the node
	 * online.
	 */
	down_write(&slqb_lock);
	list_for_each_entry(s, &slab_caches, list) {
		/*
		 * XXX: kmem_cache_alloc_node will fallback to other nodes
		 *      since memory is not yet available from the node that
		 *      is brought up.
		 */
		if (s->node_slab[nid]) /* could be lefover from last online */
			continue;
		n = kmem_cache_alloc(&kmem_node_cache, GFP_KERNEL);
		if (!n) {
			ret = -ENOMEM;
			goto out;
		}
		init_kmem_cache_node(s, n);
		s->node_slab[nid] = n;
	}
out:
	up_write(&slqb_lock);
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
		slab_mem_going_offline_callback(arg);
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

void __init kmem_cache_init(void)
{
	int i;
	unsigned int flags = SLAB_HWCACHE_ALIGN|SLAB_PANIC;

	/*
	 * All the ifdefs are rather ugly here, but it's just the setup code,
	 * so it doesn't have to be too readable :)
	 */

	/*
	 * No need to take slqb_lock here: there should be no concurrency
	 * anyway, and spin_unlock_irq in rwsem code could enable interrupts
	 * too early.
	 */
	kmem_cache_open(&kmem_cache_cache, "kmem_cache",
			sizeof(struct kmem_cache), 0, flags, NULL, 0);
#ifdef CONFIG_SMP
	kmem_cache_open(&kmem_cpu_cache, "kmem_cache_cpu",
			sizeof(struct kmem_cache_cpu), 0, flags, NULL, 0);
#endif
#ifdef CONFIG_NUMA
	kmem_cache_open(&kmem_node_cache, "kmem_cache_node",
			sizeof(struct kmem_cache_node), 0, flags, NULL, 0);
#endif

#ifdef CONFIG_SMP
	for_each_possible_cpu(i) {
		struct kmem_cache_cpu *c;

		c = &per_cpu(kmem_cache_cpus, i);
		init_kmem_cache_cpu(&kmem_cache_cache, c);
		kmem_cache_cache.cpu_slab[i] = c;

		c = &per_cpu(kmem_cpu_cpus, i);
		init_kmem_cache_cpu(&kmem_cpu_cache, c);
		kmem_cpu_cache.cpu_slab[i] = c;

#ifdef CONFIG_NUMA
		c = &per_cpu(kmem_node_cpus, i);
		init_kmem_cache_cpu(&kmem_node_cache, c);
		kmem_node_cache.cpu_slab[i] = c;
#endif
	}
#else
	init_kmem_cache_cpu(&kmem_cache_cache, &kmem_cache_cache.cpu_slab);
#endif

#ifdef CONFIG_NUMA
	for_each_node_state(i, N_NORMAL_MEMORY) {
		struct kmem_cache_node *n;

		n = &kmem_cache_nodes[i];
		init_kmem_cache_node(&kmem_cache_cache, n);
		kmem_cache_cache.node_slab[i] = n;
#ifdef CONFIG_SMP
		n = &kmem_cpu_nodes[i];
		init_kmem_cache_node(&kmem_cpu_cache, n);
		kmem_cpu_cache.node_slab[i] = n;
#endif
		n = &kmem_node_nodes[i];
		init_kmem_cache_node(&kmem_node_cache, n);
		kmem_node_cache.node_slab[i] = n;
	}
#endif

	/* Caches that are not of the two-to-the-power-of size */
	if (L1_CACHE_BYTES < 64 && KMALLOC_MIN_SIZE <= 64) {
		open_kmalloc_cache(&kmalloc_caches[1],
				"kmalloc-96", 96, GFP_KERNEL);
#ifdef CONFIG_ZONE_DMA
		open_kmalloc_cache(&kmalloc_caches_dma[1],
				"kmalloc_dma-96", 96, GFP_KERNEL|SLQB_DMA);
#endif
	}
	if (L1_CACHE_BYTES < 128 && KMALLOC_MIN_SIZE <= 128) {
		open_kmalloc_cache(&kmalloc_caches[2],
				"kmalloc-192", 192, GFP_KERNEL);
#ifdef CONFIG_ZONE_DMA
		open_kmalloc_cache(&kmalloc_caches_dma[2],
				"kmalloc_dma-192", 192, GFP_KERNEL|SLQB_DMA);
#endif
	}

	for (i = KMALLOC_SHIFT_LOW; i <= KMALLOC_SHIFT_SLQB_HIGH; i++) {
		open_kmalloc_cache(&kmalloc_caches[i],
				"kmalloc", 1 << i, GFP_KERNEL);
#ifdef CONFIG_ZONE_DMA
		open_kmalloc_cache(&kmalloc_caches_dma[i],
				"kmalloc_dma", 1 << i, GFP_KERNEL|SLQB_DMA);
#endif
	}

	/*
	 * Patch up the size_index table if we have strange large alignment
	 * requirements for the kmalloc array. This is only the case for
	 * mips it seems. The standard arches will not generate any code here.
	 *
	 * Largest permitted alignment is 256 bytes due to the way we
	 * handle the index determination for the smaller caches.
	 *
	 * Make sure that nothing crazy happens if someone starts tinkering
	 * around with ARCH_KMALLOC_MINALIGN
	 */
	BUILD_BUG_ON(KMALLOC_MIN_SIZE > 256 ||
		(KMALLOC_MIN_SIZE & (KMALLOC_MIN_SIZE - 1)));

	for (i = 8; i < KMALLOC_MIN_SIZE; i += 8)
		size_index[(i - 1) / 8] = KMALLOC_SHIFT_LOW;

	/* Provide the correct kmalloc names now that the caches are up */
	for (i = KMALLOC_SHIFT_LOW; i <= KMALLOC_SHIFT_SLQB_HIGH; i++) {
		kmalloc_caches[i].name =
			kasprintf(GFP_KERNEL, "kmalloc-%d", 1 << i);
#ifdef CONFIG_ZONE_DMA
		kmalloc_caches_dma[i].name =
			kasprintf(GFP_KERNEL, "kmalloc_dma-%d", 1 << i);
#endif
	}

#ifdef CONFIG_SMP
	register_cpu_notifier(&slab_notifier);
#endif
#ifdef CONFIG_NUMA
	hotplug_memory_notifier(slab_memory_callback, 1);
#endif
	/*
	 * smp_init() has not yet been called, so no worries about memory
	 * ordering with __slab_is_available.
	 */
	__slab_is_available = 1;
}

void __init kmem_cache_init_late(void)
{
}

/*
 * Some basic slab creation sanity checks
 */
static int kmem_cache_create_ok(const char *name, size_t size,
		size_t align, unsigned long flags)
{
	struct kmem_cache *tmp;

	/*
	 * Sanity checks... these are all serious usage bugs.
	 */
	if (!name || in_interrupt() || (size < sizeof(void *))) {
		printk(KERN_ERR "kmem_cache_create(): early error in slab %s\n",
				name);
		dump_stack();

		return 0;
	}

	list_for_each_entry(tmp, &slab_caches, list) {
		char x;
		int res;

		/*
		 * This happens when the module gets unloaded and doesn't
		 * destroy its slab cache and no-one else reuses the vmalloc
		 * area of the module.  Print a warning.
		 */
		res = probe_kernel_address(tmp->name, x);
		if (res) {
			printk(KERN_ERR
			       "SLAB: cache with size %d has lost its name\n",
			       tmp->size);
			continue;
		}

		if (!strcmp(tmp->name, name)) {
			printk(KERN_ERR
			       "SLAB: duplicate cache %s\n", name);
			dump_stack();

			return 0;
		}
	}

	WARN_ON(strchr(name, ' '));	/* It confuses parsers */
	if (flags & SLAB_DESTROY_BY_RCU)
		WARN_ON(flags & SLAB_POISON);

	return 1;
}

struct kmem_cache *kmem_cache_create(const char *name, size_t size,
		size_t align, unsigned long flags, void (*ctor)(void *))
{
	struct kmem_cache *s;

	down_write(&slqb_lock);
	if (!kmem_cache_create_ok(name, size, align, flags))
		goto err;

	s = kmem_cache_alloc(&kmem_cache_cache, GFP_KERNEL);
	if (!s)
		goto err;

	if (kmem_cache_open(s, name, size, align, flags, ctor, 1)) {
		up_write(&slqb_lock);
		return s;
	}

	kmem_cache_free(&kmem_cache_cache, s);

err:
	up_write(&slqb_lock);
	if (flags & SLAB_PANIC)
		panic("%s: failed to create slab `%s'\n", __func__, name);

	return NULL;
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

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		down_write(&slqb_lock);
		list_for_each_entry(s, &slab_caches, list) {
			if (s->cpu_slab[cpu]) /* could be lefover last online */
				continue;
			s->cpu_slab[cpu] = alloc_kmem_cache_cpu(s, cpu);
			if (!s->cpu_slab[cpu]) {
				up_read(&slqb_lock);
				return NOTIFY_BAD;
			}
		}
		up_write(&slqb_lock);
		break;

	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
		start_cpu_timer(cpu);
		break;

	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		cancel_delayed_work_sync(&per_cpu(slqb_cache_trim_work,
					cpu));
		per_cpu(slqb_cache_trim_work, cpu).work.func = NULL;
		break;

	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		/*
		 * XXX: Freeing here doesn't work because objects can still be
		 * on this CPU's list. periodic timer needs to check if a CPU
		 * is offline and then try to cleanup from there. Same for node
		 * offline.
		 */
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata slab_notifier = {
	.notifier_call = slab_cpuup_callback
};

#endif

#ifdef CONFIG_SLQB_DEBUG
void *__kmalloc_track_caller(size_t size, gfp_t flags, unsigned long caller)
{
	struct kmem_cache *s;
	int node = -1;

	s = get_slab(size, flags);
	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

#ifdef CONFIG_NUMA
	if (unlikely(current->flags & (PF_SPREAD_SLAB | PF_MEMPOLICY)))
		node = alternate_nid(s, flags, node);
#endif
	return slab_alloc(s, flags, node, caller);
}

void *__kmalloc_node_track_caller(size_t size, gfp_t flags, int node,
				unsigned long caller)
{
	struct kmem_cache *s;

	s = get_slab(size, flags);
	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	return slab_alloc(s, flags, node, caller);
}
#endif

#if defined(CONFIG_SLQB_SYSFS) || defined(CONFIG_SLABINFO)
struct stats_gather {
	struct kmem_cache *s;
	spinlock_t lock;
	unsigned long nr_slabs;
	unsigned long nr_partial;
	unsigned long nr_inuse;
	unsigned long nr_objects;

#ifdef CONFIG_SLQB_STATS
	unsigned long stats[NR_SLQB_STAT_ITEMS];
#endif
};

static void __gather_stats(void *arg)
{
	unsigned long nr_slabs;
	unsigned long nr_partial;
	unsigned long nr_inuse;
	struct stats_gather *gather = arg;
	int cpu = smp_processor_id();
	struct kmem_cache *s = gather->s;
	struct kmem_cache_cpu *c = get_cpu_slab(s, cpu);
	struct kmem_cache_list *l = &c->list;
	struct slqb_page *page;
#ifdef CONFIG_SLQB_STATS
	int i;
#endif

	spin_lock(&l->page_lock);
	nr_slabs = l->nr_slabs;
	nr_partial = l->nr_partial;
	nr_inuse = (nr_slabs - nr_partial) * s->objects;

	list_for_each_entry(page, &l->partial, lru) {
		nr_inuse += page->inuse;
	}
	spin_unlock(&l->page_lock);

	spin_lock(&gather->lock);
	gather->nr_slabs += nr_slabs;
	gather->nr_partial += nr_partial;
	gather->nr_inuse += nr_inuse;
#ifdef CONFIG_SLQB_STATS
	for (i = 0; i < NR_SLQB_STAT_ITEMS; i++)
		gather->stats[i] += l->stats[i];
#endif
	spin_unlock(&gather->lock);
}

/* must be called with slqb_lock held */
static void gather_stats_locked(struct kmem_cache *s,
				struct stats_gather *stats)
{
#ifdef CONFIG_NUMA
	int node;
#endif

	memset(stats, 0, sizeof(struct stats_gather));
	stats->s = s;
	spin_lock_init(&stats->lock);

	on_each_cpu(__gather_stats, stats, 1);

#ifdef CONFIG_NUMA
	for_each_online_node(node) {
		struct kmem_cache_node *n = s->node_slab[node];
		struct kmem_cache_list *l = &n->list;
		struct slqb_page *page;
		unsigned long flags;
#ifdef CONFIG_SLQB_STATS
		int i;
#endif

		spin_lock_irqsave(&n->list_lock, flags);
#ifdef CONFIG_SLQB_STATS
		for (i = 0; i < NR_SLQB_STAT_ITEMS; i++)
			stats->stats[i] += l->stats[i];
#endif
		stats->nr_slabs += l->nr_slabs;
		stats->nr_partial += l->nr_partial;
		stats->nr_inuse += (l->nr_slabs - l->nr_partial) * s->objects;

		list_for_each_entry(page, &l->partial, lru) {
			stats->nr_inuse += page->inuse;
		}
		spin_unlock_irqrestore(&n->list_lock, flags);
	}
#endif

	stats->nr_objects = stats->nr_slabs * s->objects;
}

#ifdef CONFIG_SLQB_SYSFS
static void gather_stats(struct kmem_cache *s, struct stats_gather *stats)
{
	down_read(&slqb_lock); /* hold off hotplug */
	gather_stats_locked(s, stats);
	up_read(&slqb_lock);
}
#endif
#endif

/*
 * The /proc/slabinfo ABI
 */
#ifdef CONFIG_SLABINFO
#include <linux/proc_fs.h>
ssize_t slabinfo_write(struct file *file, const char __user * buffer,
		       size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static void print_slabinfo_header(struct seq_file *m)
{
	seq_puts(m, "slabinfo - version: 2.1\n");
	seq_puts(m, "# name	    <active_objs> <num_objs> <objsize> "
		 "<objperslab> <pagesperslab>");
	seq_puts(m, " : tunables <limit> <batchcount> <sharedfactor>");
	seq_puts(m, " : slabdata <active_slabs> <num_slabs> <sharedavail>");
	seq_putc(m, '\n');
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	loff_t n = *pos;

	down_read(&slqb_lock);
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
	up_read(&slqb_lock);
}

static int s_show(struct seq_file *m, void *p)
{
	struct stats_gather stats;
	struct kmem_cache *s;

	s = list_entry(p, struct kmem_cache, list);

	gather_stats_locked(s, &stats);

	seq_printf(m, "%-17s %6lu %6lu %6u %4u %4d", s->name, stats.nr_inuse,
			stats.nr_objects, s->size, s->objects, (1 << s->order));
	seq_printf(m, " : tunables %4u %4u %4u", slab_hiwater(s),
			slab_freebatch(s), 0);
	seq_printf(m, " : slabdata %6lu %6lu %6lu", stats.nr_slabs,
			stats.nr_slabs, 0UL);
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
	proc_create("slabinfo", S_IWUSR|S_IRUGO, NULL,
			&proc_slabinfo_operations);
	return 0;
}
module_init(slab_proc_init);
#endif /* CONFIG_SLABINFO */

#ifdef CONFIG_SLQB_SYSFS
/*
 * sysfs API
 */
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

static ssize_t slabs_show(struct kmem_cache *s, char *buf)
{
	struct stats_gather stats;

	gather_stats(s, &stats);

	return sprintf(buf, "%lu\n", stats.nr_slabs);
}
SLAB_ATTR_RO(slabs);

static ssize_t objects_show(struct kmem_cache *s, char *buf)
{
	struct stats_gather stats;

	gather_stats(s, &stats);

	return sprintf(buf, "%lu\n", stats.nr_inuse);
}
SLAB_ATTR_RO(objects);

static ssize_t total_objects_show(struct kmem_cache *s, char *buf)
{
	struct stats_gather stats;

	gather_stats(s, &stats);

	return sprintf(buf, "%lu\n", stats.nr_objects);
}
SLAB_ATTR_RO(total_objects);

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
SLAB_ATTR_RO(reclaim_account);

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
SLAB_ATTR_RO(red_zone);

static ssize_t poison_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_POISON));
}
SLAB_ATTR_RO(poison);

static ssize_t store_user_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", !!(s->flags & SLAB_STORE_USER));
}
SLAB_ATTR_RO(store_user);

static ssize_t hiwater_store(struct kmem_cache *s,
				const char *buf, size_t length)
{
	long hiwater;
	int err;

	err = strict_strtol(buf, 10, &hiwater);
	if (err)
		return err;

	if (hiwater < 0)
		return -EINVAL;

	s->hiwater = hiwater;

	return length;
}

static ssize_t hiwater_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", slab_hiwater(s));
}
SLAB_ATTR(hiwater);

static ssize_t freebatch_store(struct kmem_cache *s,
				const char *buf, size_t length)
{
	long freebatch;
	int err;

	err = strict_strtol(buf, 10, &freebatch);
	if (err)
		return err;

	if (freebatch <= 0 || freebatch - 1 > s->hiwater)
		return -EINVAL;

	s->freebatch = freebatch;

	return length;
}

static ssize_t freebatch_show(struct kmem_cache *s, char *buf)
{
	return sprintf(buf, "%d\n", slab_freebatch(s));
}
SLAB_ATTR(freebatch);

#ifdef CONFIG_SLQB_STATS
static int show_stat(struct kmem_cache *s, char *buf, enum stat_item si)
{
	struct stats_gather stats;
	int len;
#ifdef CONFIG_SMP
	int cpu;
#endif

	gather_stats(s, &stats);

	len = sprintf(buf, "%lu", stats.stats[si]);

#ifdef CONFIG_SMP
	for_each_online_cpu(cpu) {
		struct kmem_cache_cpu *c = get_cpu_slab(s, cpu);
		struct kmem_cache_list *l = &c->list;

		if (len < PAGE_SIZE - 20)
			len += sprintf(buf+len, " C%d=%lu", cpu, l->stats[si]);
	}
#endif
	return len + sprintf(buf + len, "\n");
}

#define STAT_ATTR(si, text) 					\
static ssize_t text##_show(struct kmem_cache *s, char *buf)	\
{								\
	return show_stat(s, buf, si);				\
}								\
SLAB_ATTR_RO(text);						\

STAT_ATTR(ALLOC, alloc);
STAT_ATTR(ALLOC_SLAB_FILL, alloc_slab_fill);
STAT_ATTR(ALLOC_SLAB_NEW, alloc_slab_new);
STAT_ATTR(FREE, free);
STAT_ATTR(FREE_REMOTE, free_remote);
STAT_ATTR(FLUSH_FREE_LIST, flush_free_list);
STAT_ATTR(FLUSH_FREE_LIST_OBJECTS, flush_free_list_objects);
STAT_ATTR(FLUSH_FREE_LIST_REMOTE, flush_free_list_remote);
STAT_ATTR(FLUSH_SLAB_PARTIAL, flush_slab_partial);
STAT_ATTR(FLUSH_SLAB_FREE, flush_slab_free);
STAT_ATTR(FLUSH_RFREE_LIST, flush_rfree_list);
STAT_ATTR(FLUSH_RFREE_LIST_OBJECTS, flush_rfree_list_objects);
STAT_ATTR(CLAIM_REMOTE_LIST, claim_remote_list);
STAT_ATTR(CLAIM_REMOTE_LIST_OBJECTS, claim_remote_list_objects);
#endif

static struct attribute *slab_attrs[] = {
	&slab_size_attr.attr,
	&object_size_attr.attr,
	&objs_per_slab_attr.attr,
	&order_attr.attr,
	&objects_attr.attr,
	&total_objects_attr.attr,
	&slabs_attr.attr,
	&ctor_attr.attr,
	&align_attr.attr,
	&hwcache_align_attr.attr,
	&reclaim_account_attr.attr,
	&destroy_by_rcu_attr.attr,
	&red_zone_attr.attr,
	&poison_attr.attr,
	&store_user_attr.attr,
	&hiwater_attr.attr,
	&freebatch_attr.attr,
#ifdef CONFIG_ZONE_DMA
	&cache_dma_attr.attr,
#endif
#ifdef CONFIG_SLQB_STATS
	&alloc_attr.attr,
	&alloc_slab_fill_attr.attr,
	&alloc_slab_new_attr.attr,
	&free_attr.attr,
	&free_remote_attr.attr,
	&flush_free_list_attr.attr,
	&flush_free_list_objects_attr.attr,
	&flush_free_list_remote_attr.attr,
	&flush_slab_partial_attr.attr,
	&flush_slab_free_attr.attr,
	&flush_rfree_list_attr.attr,
	&flush_rfree_list_objects_attr.attr,
	&claim_remote_list_attr.attr,
	&claim_remote_list_objects_attr.attr,
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
				struct attribute *attr, char *buf)
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
			struct attribute *attr, const char *buf, size_t len)
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

	kmem_cache_free(&kmem_cache_cache, s);
}

static struct sysfs_ops slab_sysfs_ops = {
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

static struct kset_uevent_ops slab_uevent_ops = {
	.filter = uevent_filter,
};

static struct kset *slab_kset;

static int sysfs_available __read_mostly;

static int sysfs_slab_add(struct kmem_cache *s)
{
	int err;

	if (!sysfs_available)
		return 0;

	s->kobj.kset = slab_kset;
	err = kobject_init_and_add(&s->kobj, &slab_ktype, NULL, s->name);
	if (err) {
		kobject_put(&s->kobj);
		return err;
	}

	err = sysfs_create_group(&s->kobj, &slab_attr_group);
	if (err)
		return err;

	kobject_uevent(&s->kobj, KOBJ_ADD);

	return 0;
}

static void sysfs_slab_remove(struct kmem_cache *s)
{
	kobject_uevent(&s->kobj, KOBJ_REMOVE);
	kobject_del(&s->kobj);
	kobject_put(&s->kobj);
}

static int __init slab_sysfs_init(void)
{
	struct kmem_cache *s;
	int err;

	slab_kset = kset_create_and_add("slab", &slab_uevent_ops, kernel_kobj);
	if (!slab_kset) {
		printk(KERN_ERR "Cannot register slab subsystem.\n");
		return -ENOSYS;
	}

	down_write(&slqb_lock);

	sysfs_available = 1;

	list_for_each_entry(s, &slab_caches, list) {
		err = sysfs_slab_add(s);
		if (err)
			printk(KERN_ERR "SLQB: Unable to add boot slab %s"
						" to sysfs\n", s->name);
	}

	up_write(&slqb_lock);

	return 0;
}
device_initcall(slab_sysfs_init);

#endif
