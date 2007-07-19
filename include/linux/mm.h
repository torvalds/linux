#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <linux/errno.h>

#ifdef __KERNEL__

#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/mmzone.h>
#include <linux/rbtree.h>
#include <linux/prio_tree.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/debug_locks.h>
#include <linux/backing-dev.h>
#include <linux/mm_types.h>

struct mempolicy;
struct anon_vma;
struct user_struct;

#ifndef CONFIG_DISCONTIGMEM          /* Don't use mapnrs, do it properly */
extern unsigned long max_mapnr;
#endif

extern unsigned long num_physpages;
extern void * high_memory;
extern int page_cluster;

#ifdef CONFIG_SYSCTL
extern int sysctl_legacy_va_layout;
#else
#define sysctl_legacy_va_layout 0
#endif

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>

#define nth_page(page,n) pfn_to_page(page_to_pfn((page)) + (n))

/*
 * Linux kernel virtual memory manager primitives.
 * The idea being to have a "virtual" mm in the same way
 * we have a virtual fs - giving a cleaner interface to the
 * mm details, and allowing different kinds of memory mappings
 * (from shared memory to executable loading to arbitrary
 * mmap() functions).
 */

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
struct vm_area_struct {
	struct mm_struct * vm_mm;	/* The address space we belong to. */
	unsigned long vm_start;		/* Our start address within vm_mm. */
	unsigned long vm_end;		/* The first byte after our end address
					   within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next;

	pgprot_t vm_page_prot;		/* Access permissions of this VMA. */
	unsigned long vm_flags;		/* Flags, listed below. */

	struct rb_node vm_rb;

	/*
	 * For areas with an address space and backing store,
	 * linkage into the address_space->i_mmap prio tree, or
	 * linkage to the list of like vmas hanging off its node, or
	 * linkage of vma in the address_space->i_mmap_nonlinear list.
	 */
	union {
		struct {
			struct list_head list;
			void *parent;	/* aligns with prio_tree_node parent */
			struct vm_area_struct *head;
		} vm_set;

		struct raw_prio_tree_node prio_tree_node;
	} shared;

	/*
	 * A file's MAP_PRIVATE vma can be in both i_mmap tree and anon_vma
	 * list, after a COW of one of the file pages.  A MAP_SHARED vma
	 * can only be in the i_mmap tree.  An anonymous MAP_PRIVATE, stack
	 * or brk vma (with NULL file) can only be in an anon_vma list.
	 */
	struct list_head anon_vma_node;	/* Serialized by anon_vma->lock */
	struct anon_vma *anon_vma;	/* Serialized by page_table_lock */

	/* Function pointers to deal with this struct. */
	struct vm_operations_struct * vm_ops;

	/* Information about our backing store: */
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					   units, *not* PAGE_CACHE_SIZE */
	struct file * vm_file;		/* File we map to (can be NULL). */
	void * vm_private_data;		/* was vm_pte (shared mem) */
	unsigned long vm_truncate_count;/* truncate_count or restart_addr */

#ifndef CONFIG_MMU
	atomic_t vm_usage;		/* refcount (VMAs shared if !MMU) */
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *vm_policy;	/* NUMA policy for the VMA */
#endif
};

extern struct kmem_cache *vm_area_cachep;

/*
 * This struct defines the per-mm list of VMAs for uClinux. If CONFIG_MMU is
 * disabled, then there's a single shared list of VMAs maintained by the
 * system, and mm's subscribe to these individually
 */
struct vm_list_struct {
	struct vm_list_struct	*next;
	struct vm_area_struct	*vma;
};

#ifndef CONFIG_MMU
extern struct rb_root nommu_vma_tree;
extern struct rw_semaphore nommu_vma_sem;

extern unsigned int kobjsize(const void *objp);
#endif

/*
 * vm_flags..
 */
#define VM_READ		0x00000001	/* currently active flags */
#define VM_WRITE	0x00000002
#define VM_EXEC		0x00000004
#define VM_SHARED	0x00000008

/* mprotect() hardcodes VM_MAYREAD >> 4 == VM_READ, and so for r/w/x bits. */
#define VM_MAYREAD	0x00000010	/* limits for mprotect() etc */
#define VM_MAYWRITE	0x00000020
#define VM_MAYEXEC	0x00000040
#define VM_MAYSHARE	0x00000080

#define VM_GROWSDOWN	0x00000100	/* general info on the segment */
#define VM_GROWSUP	0x00000200
#define VM_PFNMAP	0x00000400	/* Page-ranges managed without "struct page", just pure PFN */
#define VM_DENYWRITE	0x00000800	/* ETXTBSY on write attempts.. */

#define VM_EXECUTABLE	0x00001000
#define VM_LOCKED	0x00002000
#define VM_IO           0x00004000	/* Memory mapped I/O or similar */

					/* Used by sys_madvise() */
#define VM_SEQ_READ	0x00008000	/* App will access data sequentially */
#define VM_RAND_READ	0x00010000	/* App will not benefit from clustered reads */

#define VM_DONTCOPY	0x00020000      /* Do not copy this vma on fork */
#define VM_DONTEXPAND	0x00040000	/* Cannot expand with mremap() */
#define VM_RESERVED	0x00080000	/* Count as reserved_vm like IO */
#define VM_ACCOUNT	0x00100000	/* Is a VM accounted object */
#define VM_HUGETLB	0x00400000	/* Huge TLB Page VM */
#define VM_NONLINEAR	0x00800000	/* Is non-linear (remap_file_pages) */
#define VM_MAPPED_COPY	0x01000000	/* T if mapped copy of data (nommu mmap) */
#define VM_INSERTPAGE	0x02000000	/* The vma has had "vm_insert_page()" done on it */
#define VM_ALWAYSDUMP	0x04000000	/* Always include in core dumps */

#define VM_CAN_NONLINEAR 0x08000000	/* Has ->fault & does nonlinear pages */

#ifndef VM_STACK_DEFAULT_FLAGS		/* arch can override this */
#define VM_STACK_DEFAULT_FLAGS VM_DATA_DEFAULT_FLAGS
#endif

#ifdef CONFIG_STACK_GROWSUP
#define VM_STACK_FLAGS	(VM_GROWSUP | VM_STACK_DEFAULT_FLAGS | VM_ACCOUNT)
#else
#define VM_STACK_FLAGS	(VM_GROWSDOWN | VM_STACK_DEFAULT_FLAGS | VM_ACCOUNT)
#endif

#define VM_READHINTMASK			(VM_SEQ_READ | VM_RAND_READ)
#define VM_ClearReadHint(v)		(v)->vm_flags &= ~VM_READHINTMASK
#define VM_NormalReadHint(v)		(!((v)->vm_flags & VM_READHINTMASK))
#define VM_SequentialReadHint(v)	((v)->vm_flags & VM_SEQ_READ)
#define VM_RandomReadHint(v)		((v)->vm_flags & VM_RAND_READ)

/*
 * mapping from the currently active vm_flags protection bits (the
 * low four bits) to a page protection mask..
 */
extern pgprot_t protection_map[16];

#define FAULT_FLAG_WRITE	0x01	/* Fault was a write access */
#define FAULT_FLAG_NONLINEAR	0x02	/* Fault was via a nonlinear mapping */


/*
 * vm_fault is filled by the the pagefault handler and passed to the vma's
 * ->fault function. The vma's ->fault is responsible for returning a bitmask
 * of VM_FAULT_xxx flags that give details about how the fault was handled.
 *
 * pgoff should be used in favour of virtual_address, if possible. If pgoff
 * is used, one may set VM_CAN_NONLINEAR in the vma->vm_flags to get nonlinear
 * mapping support.
 */
struct vm_fault {
	unsigned int flags;		/* FAULT_FLAG_xxx flags */
	pgoff_t pgoff;			/* Logical page offset based on vma */
	void __user *virtual_address;	/* Faulting virtual address */

	struct page *page;		/* ->fault handlers should return a
					 * page here, unless VM_FAULT_NOPAGE
					 * is set (which is also implied by
					 * VM_FAULT_ERROR).
					 */
};

/*
 * These are the virtual MM functions - opening of an area, closing and
 * unmapping it (needed to keep files on disk up-to-date etc), pointer
 * to the functions called when a no-page or a wp-page exception occurs. 
 */
struct vm_operations_struct {
	void (*open)(struct vm_area_struct * area);
	void (*close)(struct vm_area_struct * area);
	int (*fault)(struct vm_area_struct *vma, struct vm_fault *vmf);
	struct page *(*nopage)(struct vm_area_struct *area,
			unsigned long address, int *type);
	unsigned long (*nopfn)(struct vm_area_struct *area,
			unsigned long address);

	/* notification that a previously read-only page is about to become
	 * writable, if an error is returned it will cause a SIGBUS */
	int (*page_mkwrite)(struct vm_area_struct *vma, struct page *page);
#ifdef CONFIG_NUMA
	int (*set_policy)(struct vm_area_struct *vma, struct mempolicy *new);
	struct mempolicy *(*get_policy)(struct vm_area_struct *vma,
					unsigned long addr);
	int (*migrate)(struct vm_area_struct *vma, const nodemask_t *from,
		const nodemask_t *to, unsigned long flags);
#endif
};

struct mmu_gather;
struct inode;

#define page_private(page)		((page)->private)
#define set_page_private(page, v)	((page)->private = (v))

/*
 * FIXME: take this include out, include page-flags.h in
 * files which need it (119 of them)
 */
#include <linux/page-flags.h>

#ifdef CONFIG_DEBUG_VM
#define VM_BUG_ON(cond) BUG_ON(cond)
#else
#define VM_BUG_ON(condition) do { } while(0)
#endif

/*
 * Methods to modify the page usage count.
 *
 * What counts for a page usage:
 * - cache mapping   (page->mapping)
 * - private data    (page->private)
 * - page mapped in a task's page tables, each mapping
 *   is counted separately
 *
 * Also, many kernel routines increase the page count before a critical
 * routine so they can be sure the page doesn't go away from under them.
 */

/*
 * Drop a ref, return true if the refcount fell to zero (the page has no users)
 */
static inline int put_page_testzero(struct page *page)
{
	VM_BUG_ON(atomic_read(&page->_count) == 0);
	return atomic_dec_and_test(&page->_count);
}

/*
 * Try to grab a ref unless the page has a refcount of zero, return false if
 * that is the case.
 */
static inline int get_page_unless_zero(struct page *page)
{
	VM_BUG_ON(PageCompound(page));
	return atomic_inc_not_zero(&page->_count);
}

static inline struct page *compound_head(struct page *page)
{
	if (unlikely(PageTail(page)))
		return page->first_page;
	return page;
}

static inline int page_count(struct page *page)
{
	return atomic_read(&compound_head(page)->_count);
}

static inline void get_page(struct page *page)
{
	page = compound_head(page);
	VM_BUG_ON(atomic_read(&page->_count) == 0);
	atomic_inc(&page->_count);
}

static inline struct page *virt_to_head_page(const void *x)
{
	struct page *page = virt_to_page(x);
	return compound_head(page);
}

/*
 * Setup the page count before being freed into the page allocator for
 * the first time (boot or memory hotplug)
 */
static inline void init_page_count(struct page *page)
{
	atomic_set(&page->_count, 1);
}

void put_page(struct page *page);
void put_pages_list(struct list_head *pages);

void split_page(struct page *page, unsigned int order);

/*
 * Compound pages have a destructor function.  Provide a
 * prototype for that function and accessor functions.
 * These are _only_ valid on the head of a PG_compound page.
 */
typedef void compound_page_dtor(struct page *);

static inline void set_compound_page_dtor(struct page *page,
						compound_page_dtor *dtor)
{
	page[1].lru.next = (void *)dtor;
}

static inline compound_page_dtor *get_compound_page_dtor(struct page *page)
{
	return (compound_page_dtor *)page[1].lru.next;
}

static inline int compound_order(struct page *page)
{
	if (!PageHead(page))
		return 0;
	return (unsigned long)page[1].lru.prev;
}

static inline void set_compound_order(struct page *page, unsigned long order)
{
	page[1].lru.prev = (void *)order;
}

/*
 * Multiple processes may "see" the same page. E.g. for untouched
 * mappings of /dev/null, all processes see the same page full of
 * zeroes, and text pages of executables and shared libraries have
 * only one copy in memory, at most, normally.
 *
 * For the non-reserved pages, page_count(page) denotes a reference count.
 *   page_count() == 0 means the page is free. page->lru is then used for
 *   freelist management in the buddy allocator.
 *   page_count() > 0  means the page has been allocated.
 *
 * Pages are allocated by the slab allocator in order to provide memory
 * to kmalloc and kmem_cache_alloc. In this case, the management of the
 * page, and the fields in 'struct page' are the responsibility of mm/slab.c
 * unless a particular usage is carefully commented. (the responsibility of
 * freeing the kmalloc memory is the caller's, of course).
 *
 * A page may be used by anyone else who does a __get_free_page().
 * In this case, page_count still tracks the references, and should only
 * be used through the normal accessor functions. The top bits of page->flags
 * and page->virtual store page management information, but all other fields
 * are unused and could be used privately, carefully. The management of this
 * page is the responsibility of the one who allocated it, and those who have
 * subsequently been given references to it.
 *
 * The other pages (we may call them "pagecache pages") are completely
 * managed by the Linux memory manager: I/O, buffers, swapping etc.
 * The following discussion applies only to them.
 *
 * A pagecache page contains an opaque `private' member, which belongs to the
 * page's address_space. Usually, this is the address of a circular list of
 * the page's disk buffers. PG_private must be set to tell the VM to call
 * into the filesystem to release these pages.
 *
 * A page may belong to an inode's memory mapping. In this case, page->mapping
 * is the pointer to the inode, and page->index is the file offset of the page,
 * in units of PAGE_CACHE_SIZE.
 *
 * If pagecache pages are not associated with an inode, they are said to be
 * anonymous pages. These may become associated with the swapcache, and in that
 * case PG_swapcache is set, and page->private is an offset into the swapcache.
 *
 * In either case (swapcache or inode backed), the pagecache itself holds one
 * reference to the page. Setting PG_private should also increment the
 * refcount. The each user mapping also has a reference to the page.
 *
 * The pagecache pages are stored in a per-mapping radix tree, which is
 * rooted at mapping->page_tree, and indexed by offset.
 * Where 2.4 and early 2.6 kernels kept dirty/clean pages in per-address_space
 * lists, we instead now tag pages as dirty/writeback in the radix tree.
 *
 * All pagecache pages may be subject to I/O:
 * - inode pages may need to be read from disk,
 * - inode pages which have been modified and are MAP_SHARED may need
 *   to be written back to the inode on disk,
 * - anonymous pages (including MAP_PRIVATE file mappings) which have been
 *   modified may need to be swapped out to swap space and (later) to be read
 *   back into memory.
 */

/*
 * The zone field is never updated after free_area_init_core()
 * sets it, so none of the operations on it need to be atomic.
 */


/*
 * page->flags layout:
 *
 * There are three possibilities for how page->flags get
 * laid out.  The first is for the normal case, without
 * sparsemem.  The second is for sparsemem when there is
 * plenty of space for node and section.  The last is when
 * we have run out of space and have to fall back to an
 * alternate (slower) way of determining the node.
 *
 *        No sparsemem: |       NODE     | ZONE | ... | FLAGS |
 * with space for node: | SECTION | NODE | ZONE | ... | FLAGS |
 *   no space for node: | SECTION |     ZONE    | ... | FLAGS |
 */
#ifdef CONFIG_SPARSEMEM
#define SECTIONS_WIDTH		SECTIONS_SHIFT
#else
#define SECTIONS_WIDTH		0
#endif

#define ZONES_WIDTH		ZONES_SHIFT

#if SECTIONS_WIDTH+ZONES_WIDTH+NODES_SHIFT <= FLAGS_RESERVED
#define NODES_WIDTH		NODES_SHIFT
#else
#define NODES_WIDTH		0
#endif

/* Page flags: | [SECTION] | [NODE] | ZONE | ... | FLAGS | */
#define SECTIONS_PGOFF		((sizeof(unsigned long)*8) - SECTIONS_WIDTH)
#define NODES_PGOFF		(SECTIONS_PGOFF - NODES_WIDTH)
#define ZONES_PGOFF		(NODES_PGOFF - ZONES_WIDTH)

/*
 * We are going to use the flags for the page to node mapping if its in
 * there.  This includes the case where there is no node, so it is implicit.
 */
#if !(NODES_WIDTH > 0 || NODES_SHIFT == 0)
#define NODE_NOT_IN_PAGE_FLAGS
#endif

#ifndef PFN_SECTION_SHIFT
#define PFN_SECTION_SHIFT 0
#endif

/*
 * Define the bit shifts to access each section.  For non-existant
 * sections we define the shift as 0; that plus a 0 mask ensures
 * the compiler will optimise away reference to them.
 */
#define SECTIONS_PGSHIFT	(SECTIONS_PGOFF * (SECTIONS_WIDTH != 0))
#define NODES_PGSHIFT		(NODES_PGOFF * (NODES_WIDTH != 0))
#define ZONES_PGSHIFT		(ZONES_PGOFF * (ZONES_WIDTH != 0))

/* NODE:ZONE or SECTION:ZONE is used to ID a zone for the buddy allcator */
#ifdef NODE_NOT_IN_PAGEFLAGS
#define ZONEID_SHIFT		(SECTIONS_SHIFT + ZONES_SHIFT)
#define ZONEID_PGOFF		((SECTIONS_PGOFF < ZONES_PGOFF)? \
						SECTIONS_PGOFF : ZONES_PGOFF)
#else
#define ZONEID_SHIFT		(NODES_SHIFT + ZONES_SHIFT)
#define ZONEID_PGOFF		((NODES_PGOFF < ZONES_PGOFF)? \
						NODES_PGOFF : ZONES_PGOFF)
#endif

#define ZONEID_PGSHIFT		(ZONEID_PGOFF * (ZONEID_SHIFT != 0))

#if SECTIONS_WIDTH+NODES_WIDTH+ZONES_WIDTH > FLAGS_RESERVED
#error SECTIONS_WIDTH+NODES_WIDTH+ZONES_WIDTH > FLAGS_RESERVED
#endif

#define ZONES_MASK		((1UL << ZONES_WIDTH) - 1)
#define NODES_MASK		((1UL << NODES_WIDTH) - 1)
#define SECTIONS_MASK		((1UL << SECTIONS_WIDTH) - 1)
#define ZONEID_MASK		((1UL << ZONEID_SHIFT) - 1)

static inline enum zone_type page_zonenum(struct page *page)
{
	return (page->flags >> ZONES_PGSHIFT) & ZONES_MASK;
}

/*
 * The identification function is only used by the buddy allocator for
 * determining if two pages could be buddies. We are not really
 * identifying a zone since we could be using a the section number
 * id if we have not node id available in page flags.
 * We guarantee only that it will return the same value for two
 * combinable pages in a zone.
 */
static inline int page_zone_id(struct page *page)
{
	return (page->flags >> ZONEID_PGSHIFT) & ZONEID_MASK;
}

static inline int zone_to_nid(struct zone *zone)
{
#ifdef CONFIG_NUMA
	return zone->node;
#else
	return 0;
#endif
}

#ifdef NODE_NOT_IN_PAGE_FLAGS
extern int page_to_nid(struct page *page);
#else
static inline int page_to_nid(struct page *page)
{
	return (page->flags >> NODES_PGSHIFT) & NODES_MASK;
}
#endif

static inline struct zone *page_zone(struct page *page)
{
	return &NODE_DATA(page_to_nid(page))->node_zones[page_zonenum(page)];
}

static inline unsigned long page_to_section(struct page *page)
{
	return (page->flags >> SECTIONS_PGSHIFT) & SECTIONS_MASK;
}

static inline void set_page_zone(struct page *page, enum zone_type zone)
{
	page->flags &= ~(ZONES_MASK << ZONES_PGSHIFT);
	page->flags |= (zone & ZONES_MASK) << ZONES_PGSHIFT;
}

static inline void set_page_node(struct page *page, unsigned long node)
{
	page->flags &= ~(NODES_MASK << NODES_PGSHIFT);
	page->flags |= (node & NODES_MASK) << NODES_PGSHIFT;
}

static inline void set_page_section(struct page *page, unsigned long section)
{
	page->flags &= ~(SECTIONS_MASK << SECTIONS_PGSHIFT);
	page->flags |= (section & SECTIONS_MASK) << SECTIONS_PGSHIFT;
}

static inline void set_page_links(struct page *page, enum zone_type zone,
	unsigned long node, unsigned long pfn)
{
	set_page_zone(page, zone);
	set_page_node(page, node);
	set_page_section(page, pfn_to_section_nr(pfn));
}

/*
 * Some inline functions in vmstat.h depend on page_zone()
 */
#include <linux/vmstat.h>

static __always_inline void *lowmem_page_address(struct page *page)
{
	return __va(page_to_pfn(page) << PAGE_SHIFT);
}

#if defined(CONFIG_HIGHMEM) && !defined(WANT_PAGE_VIRTUAL)
#define HASHED_PAGE_VIRTUAL
#endif

#if defined(WANT_PAGE_VIRTUAL)
#define page_address(page) ((page)->virtual)
#define set_page_address(page, address)			\
	do {						\
		(page)->virtual = (address);		\
	} while(0)
#define page_address_init()  do { } while(0)
#endif

#if defined(HASHED_PAGE_VIRTUAL)
void *page_address(struct page *page);
void set_page_address(struct page *page, void *virtual);
void page_address_init(void);
#endif

#if !defined(HASHED_PAGE_VIRTUAL) && !defined(WANT_PAGE_VIRTUAL)
#define page_address(page) lowmem_page_address(page)
#define set_page_address(page, address)  do { } while(0)
#define page_address_init()  do { } while(0)
#endif

/*
 * On an anonymous page mapped into a user virtual memory area,
 * page->mapping points to its anon_vma, not to a struct address_space;
 * with the PAGE_MAPPING_ANON bit set to distinguish it.
 *
 * Please note that, confusingly, "page_mapping" refers to the inode
 * address_space which maps the page from disk; whereas "page_mapped"
 * refers to user virtual address space into which the page is mapped.
 */
#define PAGE_MAPPING_ANON	1

extern struct address_space swapper_space;
static inline struct address_space *page_mapping(struct page *page)
{
	struct address_space *mapping = page->mapping;

	VM_BUG_ON(PageSlab(page));
	if (unlikely(PageSwapCache(page)))
		mapping = &swapper_space;
#ifdef CONFIG_SLUB
	else if (unlikely(PageSlab(page)))
		mapping = NULL;
#endif
	else if (unlikely((unsigned long)mapping & PAGE_MAPPING_ANON))
		mapping = NULL;
	return mapping;
}

static inline int PageAnon(struct page *page)
{
	return ((unsigned long)page->mapping & PAGE_MAPPING_ANON) != 0;
}

/*
 * Return the pagecache index of the passed page.  Regular pagecache pages
 * use ->index whereas swapcache pages use ->private
 */
static inline pgoff_t page_index(struct page *page)
{
	if (unlikely(PageSwapCache(page)))
		return page_private(page);
	return page->index;
}

/*
 * The atomic page->_mapcount, like _count, starts from -1:
 * so that transitions both from it and to it can be tracked,
 * using atomic_inc_and_test and atomic_add_negative(-1).
 */
static inline void reset_page_mapcount(struct page *page)
{
	atomic_set(&(page)->_mapcount, -1);
}

static inline int page_mapcount(struct page *page)
{
	return atomic_read(&(page)->_mapcount) + 1;
}

/*
 * Return true if this page is mapped into pagetables.
 */
static inline int page_mapped(struct page *page)
{
	return atomic_read(&(page)->_mapcount) >= 0;
}

/*
 * Error return values for the *_nopage functions
 */
#define NOPAGE_SIGBUS	(NULL)
#define NOPAGE_OOM	((struct page *) (-1))

/*
 * Error return values for the *_nopfn functions
 */
#define NOPFN_SIGBUS	((unsigned long) -1)
#define NOPFN_OOM	((unsigned long) -2)
#define NOPFN_REFAULT	((unsigned long) -3)

/*
 * Different kinds of faults, as returned by handle_mm_fault().
 * Used to decide whether a process gets delivered SIGBUS or
 * just gets major/minor fault counters bumped up.
 */

#define VM_FAULT_MINOR	0 /* For backwards compat. Remove me quickly. */

#define VM_FAULT_OOM	0x0001
#define VM_FAULT_SIGBUS	0x0002
#define VM_FAULT_MAJOR	0x0004
#define VM_FAULT_WRITE	0x0008	/* Special case for get_user_pages */

#define VM_FAULT_NOPAGE	0x0100	/* ->fault installed the pte, not return page */
#define VM_FAULT_LOCKED	0x0200	/* ->fault locked the returned page */

#define VM_FAULT_ERROR	(VM_FAULT_OOM | VM_FAULT_SIGBUS)

#define offset_in_page(p)	((unsigned long)(p) & ~PAGE_MASK)

extern void show_free_areas(void);

#ifdef CONFIG_SHMEM
int shmem_set_policy(struct vm_area_struct *vma, struct mempolicy *new);
struct mempolicy *shmem_get_policy(struct vm_area_struct *vma,
					unsigned long addr);
int shmem_lock(struct file *file, int lock, struct user_struct *user);
#else
static inline int shmem_lock(struct file *file, int lock,
			     struct user_struct *user)
{
	return 0;
}

static inline int shmem_set_policy(struct vm_area_struct *vma,
				   struct mempolicy *new)
{
	return 0;
}

static inline struct mempolicy *shmem_get_policy(struct vm_area_struct *vma,
						 unsigned long addr)
{
	return NULL;
}
#endif
struct file *shmem_file_setup(char *name, loff_t size, unsigned long flags);

int shmem_zero_setup(struct vm_area_struct *);

#ifndef CONFIG_MMU
extern unsigned long shmem_get_unmapped_area(struct file *file,
					     unsigned long addr,
					     unsigned long len,
					     unsigned long pgoff,
					     unsigned long flags);
#endif

extern int can_do_mlock(void);
extern int user_shm_lock(size_t, struct user_struct *);
extern void user_shm_unlock(size_t, struct user_struct *);

/*
 * Parameter block passed down to zap_pte_range in exceptional cases.
 */
struct zap_details {
	struct vm_area_struct *nonlinear_vma;	/* Check page->index if set */
	struct address_space *check_mapping;	/* Check page->mapping if set */
	pgoff_t	first_index;			/* Lowest page->index to unmap */
	pgoff_t last_index;			/* Highest page->index to unmap */
	spinlock_t *i_mmap_lock;		/* For unmap_mapping_range: */
	unsigned long truncate_count;		/* Compare vm_truncate_count */
};

struct page *vm_normal_page(struct vm_area_struct *, unsigned long, pte_t);
unsigned long zap_page_range(struct vm_area_struct *vma, unsigned long address,
		unsigned long size, struct zap_details *);
unsigned long unmap_vmas(struct mmu_gather **tlb,
		struct vm_area_struct *start_vma, unsigned long start_addr,
		unsigned long end_addr, unsigned long *nr_accounted,
		struct zap_details *);
void free_pgd_range(struct mmu_gather **tlb, unsigned long addr,
		unsigned long end, unsigned long floor, unsigned long ceiling);
void free_pgtables(struct mmu_gather **tlb, struct vm_area_struct *start_vma,
		unsigned long floor, unsigned long ceiling);
int copy_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma);
int zeromap_page_range(struct vm_area_struct *vma, unsigned long from,
			unsigned long size, pgprot_t prot);
void unmap_mapping_range(struct address_space *mapping,
		loff_t const holebegin, loff_t const holelen, int even_cows);

static inline void unmap_shared_mapping_range(struct address_space *mapping,
		loff_t const holebegin, loff_t const holelen)
{
	unmap_mapping_range(mapping, holebegin, holelen, 0);
}

extern int vmtruncate(struct inode * inode, loff_t offset);
extern int vmtruncate_range(struct inode * inode, loff_t offset, loff_t end);

#ifdef CONFIG_MMU
extern int handle_mm_fault(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, int write_access);
#else
static inline int handle_mm_fault(struct mm_struct *mm,
			struct vm_area_struct *vma, unsigned long address,
			int write_access)
{
	/* should never happen if there's no MMU */
	BUG();
	return VM_FAULT_SIGBUS;
}
#endif

extern int make_pages_present(unsigned long addr, unsigned long end);
extern int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write);
void install_arg_page(struct vm_area_struct *, struct page *, unsigned long);

int get_user_pages(struct task_struct *tsk, struct mm_struct *mm, unsigned long start,
		int len, int write, int force, struct page **pages, struct vm_area_struct **vmas);
void print_bad_pte(struct vm_area_struct *, pte_t, unsigned long);

extern int try_to_release_page(struct page * page, gfp_t gfp_mask);
extern void do_invalidatepage(struct page *page, unsigned long offset);

int __set_page_dirty_nobuffers(struct page *page);
int __set_page_dirty_no_writeback(struct page *page);
int redirty_page_for_writepage(struct writeback_control *wbc,
				struct page *page);
int FASTCALL(set_page_dirty(struct page *page));
int set_page_dirty_lock(struct page *page);
int clear_page_dirty_for_io(struct page *page);

extern unsigned long do_mremap(unsigned long addr,
			       unsigned long old_len, unsigned long new_len,
			       unsigned long flags, unsigned long new_addr);

/*
 * A callback you can register to apply pressure to ageable caches.
 *
 * 'shrink' is passed a count 'nr_to_scan' and a 'gfpmask'.  It should
 * look through the least-recently-used 'nr_to_scan' entries and
 * attempt to free them up.  It should return the number of objects
 * which remain in the cache.  If it returns -1, it means it cannot do
 * any scanning at this time (eg. there is a risk of deadlock).
 *
 * The 'gfpmask' refers to the allocation we are currently trying to
 * fulfil.
 *
 * Note that 'shrink' will be passed nr_to_scan == 0 when the VM is
 * querying the cache size, so a fastpath for that case is appropriate.
 */
struct shrinker {
	int (*shrink)(int nr_to_scan, gfp_t gfp_mask);
	int seeks;	/* seeks to recreate an obj */

	/* These are for internal use */
	struct list_head list;
	long nr;	/* objs pending delete */
};
#define DEFAULT_SEEKS 2 /* A good number if you don't know better. */
extern void register_shrinker(struct shrinker *);
extern void unregister_shrinker(struct shrinker *);

/*
 * Some shared mappigns will want the pages marked read-only
 * to track write events. If so, we'll downgrade vm_page_prot
 * to the private version (using protection_map[] without the
 * VM_SHARED bit).
 */
static inline int vma_wants_writenotify(struct vm_area_struct *vma)
{
	unsigned int vm_flags = vma->vm_flags;

	/* If it was private or non-writable, the write bit is already clear */
	if ((vm_flags & (VM_WRITE|VM_SHARED)) != ((VM_WRITE|VM_SHARED)))
		return 0;

	/* The backer wishes to know when pages are first written to? */
	if (vma->vm_ops && vma->vm_ops->page_mkwrite)
		return 1;

	/* The open routine did something to the protections already? */
	if (pgprot_val(vma->vm_page_prot) !=
	    pgprot_val(protection_map[vm_flags &
		    (VM_READ|VM_WRITE|VM_EXEC|VM_SHARED)]))
		return 0;

	/* Specialty mapping? */
	if (vm_flags & (VM_PFNMAP|VM_INSERTPAGE))
		return 0;

	/* Can the mapping track the dirty pages? */
	return vma->vm_file && vma->vm_file->f_mapping &&
		mapping_cap_account_dirty(vma->vm_file->f_mapping);
}

extern pte_t *FASTCALL(get_locked_pte(struct mm_struct *mm, unsigned long addr, spinlock_t **ptl));

#ifdef __PAGETABLE_PUD_FOLDED
static inline int __pud_alloc(struct mm_struct *mm, pgd_t *pgd,
						unsigned long address)
{
	return 0;
}
#else
int __pud_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address);
#endif

#ifdef __PAGETABLE_PMD_FOLDED
static inline int __pmd_alloc(struct mm_struct *mm, pud_t *pud,
						unsigned long address)
{
	return 0;
}
#else
int __pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address);
#endif

int __pte_alloc(struct mm_struct *mm, pmd_t *pmd, unsigned long address);
int __pte_alloc_kernel(pmd_t *pmd, unsigned long address);

/*
 * The following ifdef needed to get the 4level-fixup.h header to work.
 * Remove it when 4level-fixup.h has been removed.
 */
#if defined(CONFIG_MMU) && !defined(__ARCH_HAS_4LEVEL_HACK)
static inline pud_t *pud_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address)
{
	return (unlikely(pgd_none(*pgd)) && __pud_alloc(mm, pgd, address))?
		NULL: pud_offset(pgd, address);
}

static inline pmd_t *pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address)
{
	return (unlikely(pud_none(*pud)) && __pmd_alloc(mm, pud, address))?
		NULL: pmd_offset(pud, address);
}
#endif /* CONFIG_MMU && !__ARCH_HAS_4LEVEL_HACK */

#if NR_CPUS >= CONFIG_SPLIT_PTLOCK_CPUS
/*
 * We tuck a spinlock to guard each pagetable page into its struct page,
 * at page->private, with BUILD_BUG_ON to make sure that this will not
 * overflow into the next struct page (as it might with DEBUG_SPINLOCK).
 * When freeing, reset page->mapping so free_pages_check won't complain.
 */
#define __pte_lockptr(page)	&((page)->ptl)
#define pte_lock_init(_page)	do {					\
	spin_lock_init(__pte_lockptr(_page));				\
} while (0)
#define pte_lock_deinit(page)	((page)->mapping = NULL)
#define pte_lockptr(mm, pmd)	({(void)(mm); __pte_lockptr(pmd_page(*(pmd)));})
#else
/*
 * We use mm->page_table_lock to guard all pagetable pages of the mm.
 */
#define pte_lock_init(page)	do {} while (0)
#define pte_lock_deinit(page)	do {} while (0)
#define pte_lockptr(mm, pmd)	({(void)(pmd); &(mm)->page_table_lock;})
#endif /* NR_CPUS < CONFIG_SPLIT_PTLOCK_CPUS */

#define pte_offset_map_lock(mm, pmd, address, ptlp)	\
({							\
	spinlock_t *__ptl = pte_lockptr(mm, pmd);	\
	pte_t *__pte = pte_offset_map(pmd, address);	\
	*(ptlp) = __ptl;				\
	spin_lock(__ptl);				\
	__pte;						\
})

#define pte_unmap_unlock(pte, ptl)	do {		\
	spin_unlock(ptl);				\
	pte_unmap(pte);					\
} while (0)

#define pte_alloc_map(mm, pmd, address)			\
	((unlikely(!pmd_present(*(pmd))) && __pte_alloc(mm, pmd, address))? \
		NULL: pte_offset_map(pmd, address))

#define pte_alloc_map_lock(mm, pmd, address, ptlp)	\
	((unlikely(!pmd_present(*(pmd))) && __pte_alloc(mm, pmd, address))? \
		NULL: pte_offset_map_lock(mm, pmd, address, ptlp))

#define pte_alloc_kernel(pmd, address)			\
	((unlikely(!pmd_present(*(pmd))) && __pte_alloc_kernel(pmd, address))? \
		NULL: pte_offset_kernel(pmd, address))

extern void free_area_init(unsigned long * zones_size);
extern void free_area_init_node(int nid, pg_data_t *pgdat,
	unsigned long * zones_size, unsigned long zone_start_pfn, 
	unsigned long *zholes_size);
#ifdef CONFIG_ARCH_POPULATES_NODE_MAP
/*
 * With CONFIG_ARCH_POPULATES_NODE_MAP set, an architecture may initialise its
 * zones, allocate the backing mem_map and account for memory holes in a more
 * architecture independent manner. This is a substitute for creating the
 * zone_sizes[] and zholes_size[] arrays and passing them to
 * free_area_init_node()
 *
 * An architecture is expected to register range of page frames backed by
 * physical memory with add_active_range() before calling
 * free_area_init_nodes() passing in the PFN each zone ends at. At a basic
 * usage, an architecture is expected to do something like
 *
 * unsigned long max_zone_pfns[MAX_NR_ZONES] = {max_dma, max_normal_pfn,
 * 							 max_highmem_pfn};
 * for_each_valid_physical_page_range()
 * 	add_active_range(node_id, start_pfn, end_pfn)
 * free_area_init_nodes(max_zone_pfns);
 *
 * If the architecture guarantees that there are no holes in the ranges
 * registered with add_active_range(), free_bootmem_active_regions()
 * will call free_bootmem_node() for each registered physical page range.
 * Similarly sparse_memory_present_with_active_regions() calls
 * memory_present() for each range when SPARSEMEM is enabled.
 *
 * See mm/page_alloc.c for more information on each function exposed by
 * CONFIG_ARCH_POPULATES_NODE_MAP
 */
extern void free_area_init_nodes(unsigned long *max_zone_pfn);
extern void add_active_range(unsigned int nid, unsigned long start_pfn,
					unsigned long end_pfn);
extern void shrink_active_range(unsigned int nid, unsigned long old_end_pfn,
						unsigned long new_end_pfn);
extern void push_node_boundaries(unsigned int nid, unsigned long start_pfn,
					unsigned long end_pfn);
extern void remove_all_active_ranges(void);
extern unsigned long absent_pages_in_range(unsigned long start_pfn,
						unsigned long end_pfn);
extern void get_pfn_range_for_nid(unsigned int nid,
			unsigned long *start_pfn, unsigned long *end_pfn);
extern unsigned long find_min_pfn_with_active_regions(void);
extern unsigned long find_max_pfn_with_active_regions(void);
extern void free_bootmem_with_active_regions(int nid,
						unsigned long max_low_pfn);
extern void sparse_memory_present_with_active_regions(int nid);
#ifndef CONFIG_HAVE_ARCH_EARLY_PFN_TO_NID
extern int early_pfn_to_nid(unsigned long pfn);
#endif /* CONFIG_HAVE_ARCH_EARLY_PFN_TO_NID */
#endif /* CONFIG_ARCH_POPULATES_NODE_MAP */
extern void set_dma_reserve(unsigned long new_dma_reserve);
extern void memmap_init_zone(unsigned long, int, unsigned long,
				unsigned long, enum memmap_context);
extern void setup_per_zone_pages_min(void);
extern void mem_init(void);
extern void show_mem(void);
extern void si_meminfo(struct sysinfo * val);
extern void si_meminfo_node(struct sysinfo *val, int nid);

#ifdef CONFIG_NUMA
extern void setup_per_cpu_pageset(void);
#else
static inline void setup_per_cpu_pageset(void) {}
#endif

/* prio_tree.c */
void vma_prio_tree_add(struct vm_area_struct *, struct vm_area_struct *old);
void vma_prio_tree_insert(struct vm_area_struct *, struct prio_tree_root *);
void vma_prio_tree_remove(struct vm_area_struct *, struct prio_tree_root *);
struct vm_area_struct *vma_prio_tree_next(struct vm_area_struct *vma,
	struct prio_tree_iter *iter);

#define vma_prio_tree_foreach(vma, iter, root, begin, end)	\
	for (prio_tree_iter_init(iter, root, begin, end), vma = NULL;	\
		(vma = vma_prio_tree_next(vma, iter)); )

static inline void vma_nonlinear_insert(struct vm_area_struct *vma,
					struct list_head *list)
{
	vma->shared.vm_set.parent = NULL;
	list_add_tail(&vma->shared.vm_set.list, list);
}

/* mmap.c */
extern int __vm_enough_memory(long pages, int cap_sys_admin);
extern void vma_adjust(struct vm_area_struct *vma, unsigned long start,
	unsigned long end, pgoff_t pgoff, struct vm_area_struct *insert);
extern struct vm_area_struct *vma_merge(struct mm_struct *,
	struct vm_area_struct *prev, unsigned long addr, unsigned long end,
	unsigned long vm_flags, struct anon_vma *, struct file *, pgoff_t,
	struct mempolicy *);
extern struct anon_vma *find_mergeable_anon_vma(struct vm_area_struct *);
extern int split_vma(struct mm_struct *,
	struct vm_area_struct *, unsigned long addr, int new_below);
extern int insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void __vma_link_rb(struct mm_struct *, struct vm_area_struct *,
	struct rb_node **, struct rb_node *);
extern void unlink_file_vma(struct vm_area_struct *);
extern struct vm_area_struct *copy_vma(struct vm_area_struct **,
	unsigned long addr, unsigned long len, pgoff_t pgoff);
extern void exit_mmap(struct mm_struct *);
extern int may_expand_vm(struct mm_struct *mm, unsigned long npages);
extern int install_special_mapping(struct mm_struct *mm,
				   unsigned long addr, unsigned long len,
				   unsigned long flags, struct page **pages);

extern unsigned long get_unmapped_area(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);

extern unsigned long do_mmap_pgoff(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long pgoff);
extern unsigned long mmap_region(struct file *file, unsigned long addr,
	unsigned long len, unsigned long flags,
	unsigned int vm_flags, unsigned long pgoff,
	int accountable);

static inline unsigned long do_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long offset)
{
	unsigned long ret = -EINVAL;
	if ((offset + PAGE_ALIGN(len)) < offset)
		goto out;
	if (!(offset & ~PAGE_MASK))
		ret = do_mmap_pgoff(file, addr, len, prot, flag, offset >> PAGE_SHIFT);
out:
	return ret;
}

extern int do_munmap(struct mm_struct *, unsigned long, size_t);

extern unsigned long do_brk(unsigned long, unsigned long);

/* filemap.c */
extern unsigned long page_unuse(struct page *);
extern void truncate_inode_pages(struct address_space *, loff_t);
extern void truncate_inode_pages_range(struct address_space *,
				       loff_t lstart, loff_t lend);

/* generic vm_area_ops exported for stackable file systems */
extern int filemap_fault(struct vm_area_struct *, struct vm_fault *);

/* mm/page-writeback.c */
int write_one_page(struct page *page, int wait);

/* readahead.c */
#define VM_MAX_READAHEAD	128	/* kbytes */
#define VM_MIN_READAHEAD	16	/* kbytes (includes current page) */
#define VM_MAX_CACHE_HIT    	256	/* max pages in a row in cache before
					 * turning readahead off */

int do_page_cache_readahead(struct address_space *mapping, struct file *filp,
			pgoff_t offset, unsigned long nr_to_read);
int force_page_cache_readahead(struct address_space *mapping, struct file *filp,
			pgoff_t offset, unsigned long nr_to_read);
unsigned long page_cache_readahead_ondemand(struct address_space *mapping,
			  struct file_ra_state *ra,
			  struct file *filp,
			  struct page *page,
			  pgoff_t offset,
			  unsigned long size);
unsigned long page_cache_readahead(struct address_space *mapping,
			  struct file_ra_state *ra,
			  struct file *filp,
			  pgoff_t offset,
			  unsigned long size);
void handle_ra_miss(struct address_space *mapping, 
		    struct file_ra_state *ra, pgoff_t offset);
unsigned long max_sane_readahead(unsigned long nr);

/* Do stack extension */
extern int expand_stack(struct vm_area_struct *vma, unsigned long address);
#ifdef CONFIG_IA64
extern int expand_upwards(struct vm_area_struct *vma, unsigned long address);
#endif

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
extern struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr);
extern struct vm_area_struct * find_vma_prev(struct mm_struct * mm, unsigned long addr,
					     struct vm_area_struct **pprev);

/* Look up the first VMA which intersects the interval start_addr..end_addr-1,
   NULL if none.  Assume start_addr < end_addr. */
static inline struct vm_area_struct * find_vma_intersection(struct mm_struct * mm, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct * vma = find_vma(mm,start_addr);

	if (vma && end_addr <= vma->vm_start)
		vma = NULL;
	return vma;
}

static inline unsigned long vma_pages(struct vm_area_struct *vma)
{
	return (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
}

pgprot_t vm_get_page_prot(unsigned long vm_flags);
struct vm_area_struct *find_extend_vma(struct mm_struct *, unsigned long addr);
struct page *vmalloc_to_page(void *addr);
unsigned long vmalloc_to_pfn(void *addr);
int remap_pfn_range(struct vm_area_struct *, unsigned long addr,
			unsigned long pfn, unsigned long size, pgprot_t);
int vm_insert_page(struct vm_area_struct *, unsigned long addr, struct page *);
int vm_insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn);

struct page *follow_page(struct vm_area_struct *, unsigned long address,
			unsigned int foll_flags);
#define FOLL_WRITE	0x01	/* check pte is writable */
#define FOLL_TOUCH	0x02	/* mark page accessed */
#define FOLL_GET	0x04	/* do get_page on page */
#define FOLL_ANON	0x08	/* give ZERO_PAGE if no pgtable */

typedef int (*pte_fn_t)(pte_t *pte, struct page *pmd_page, unsigned long addr,
			void *data);
extern int apply_to_page_range(struct mm_struct *mm, unsigned long address,
			       unsigned long size, pte_fn_t fn, void *data);

#ifdef CONFIG_PROC_FS
void vm_stat_account(struct mm_struct *, unsigned long, struct file *, long);
#else
static inline void vm_stat_account(struct mm_struct *mm,
			unsigned long flags, struct file *file, long pages)
{
}
#endif /* CONFIG_PROC_FS */

#ifndef CONFIG_DEBUG_PAGEALLOC
static inline void
kernel_map_pages(struct page *page, int numpages, int enable) {}
#endif

extern struct vm_area_struct *get_gate_vma(struct task_struct *tsk);
#ifdef	__HAVE_ARCH_GATE_AREA
int in_gate_area_no_task(unsigned long addr);
int in_gate_area(struct task_struct *task, unsigned long addr);
#else
int in_gate_area_no_task(unsigned long addr);
#define in_gate_area(task, addr) ({(void)task; in_gate_area_no_task(addr);})
#endif	/* __HAVE_ARCH_GATE_AREA */

int drop_caches_sysctl_handler(struct ctl_table *, int, struct file *,
					void __user *, size_t *, loff_t *);
unsigned long shrink_slab(unsigned long scanned, gfp_t gfp_mask,
			unsigned long lru_pages);
void drop_pagecache(void);
void drop_slab(void);

#ifndef CONFIG_MMU
#define randomize_va_space 0
#else
extern int randomize_va_space;
#endif

__attribute__((weak)) const char *arch_vma_name(struct vm_area_struct *vma);

#endif /* __KERNEL__ */
#endif /* _LINUX_MM_H */
