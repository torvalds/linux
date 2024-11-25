/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_GFP_H
#define __LINUX_GFP_H

#include <linux/gfp_types.h>

#include <linux/mmzone.h>
#include <linux/topology.h>
#include <linux/alloc_tag.h>
#include <linux/sched.h>

struct vm_area_struct;
struct mempolicy;

/* Convert GFP flags to their corresponding migrate type */
#define GFP_MOVABLE_MASK (__GFP_RECLAIMABLE|__GFP_MOVABLE)
#define GFP_MOVABLE_SHIFT 3

static inline int gfp_migratetype(const gfp_t gfp_flags)
{
	VM_WARN_ON((gfp_flags & GFP_MOVABLE_MASK) == GFP_MOVABLE_MASK);
	BUILD_BUG_ON((1UL << GFP_MOVABLE_SHIFT) != ___GFP_MOVABLE);
	BUILD_BUG_ON((___GFP_MOVABLE >> GFP_MOVABLE_SHIFT) != MIGRATE_MOVABLE);
	BUILD_BUG_ON((___GFP_RECLAIMABLE >> GFP_MOVABLE_SHIFT) != MIGRATE_RECLAIMABLE);
	BUILD_BUG_ON(((___GFP_MOVABLE | ___GFP_RECLAIMABLE) >>
		      GFP_MOVABLE_SHIFT) != MIGRATE_HIGHATOMIC);

	if (unlikely(page_group_by_mobility_disabled))
		return MIGRATE_UNMOVABLE;

	/* Group based on mobility */
	return (__force unsigned long)(gfp_flags & GFP_MOVABLE_MASK) >> GFP_MOVABLE_SHIFT;
}
#undef GFP_MOVABLE_MASK
#undef GFP_MOVABLE_SHIFT

static inline bool gfpflags_allow_blocking(const gfp_t gfp_flags)
{
	return !!(gfp_flags & __GFP_DIRECT_RECLAIM);
}

#ifdef CONFIG_HIGHMEM
#define OPT_ZONE_HIGHMEM ZONE_HIGHMEM
#else
#define OPT_ZONE_HIGHMEM ZONE_NORMAL
#endif

#ifdef CONFIG_ZONE_DMA
#define OPT_ZONE_DMA ZONE_DMA
#else
#define OPT_ZONE_DMA ZONE_NORMAL
#endif

#ifdef CONFIG_ZONE_DMA32
#define OPT_ZONE_DMA32 ZONE_DMA32
#else
#define OPT_ZONE_DMA32 ZONE_NORMAL
#endif

/*
 * GFP_ZONE_TABLE is a word size bitstring that is used for looking up the
 * zone to use given the lowest 4 bits of gfp_t. Entries are GFP_ZONES_SHIFT
 * bits long and there are 16 of them to cover all possible combinations of
 * __GFP_DMA, __GFP_DMA32, __GFP_MOVABLE and __GFP_HIGHMEM.
 *
 * The zone fallback order is MOVABLE=>HIGHMEM=>NORMAL=>DMA32=>DMA.
 * But GFP_MOVABLE is not only a zone specifier but also an allocation
 * policy. Therefore __GFP_MOVABLE plus another zone selector is valid.
 * Only 1 bit of the lowest 3 bits (DMA,DMA32,HIGHMEM) can be set to "1".
 *
 *       bit       result
 *       =================
 *       0x0    => NORMAL
 *       0x1    => DMA or NORMAL
 *       0x2    => HIGHMEM or NORMAL
 *       0x3    => BAD (DMA+HIGHMEM)
 *       0x4    => DMA32 or NORMAL
 *       0x5    => BAD (DMA+DMA32)
 *       0x6    => BAD (HIGHMEM+DMA32)
 *       0x7    => BAD (HIGHMEM+DMA32+DMA)
 *       0x8    => NORMAL (MOVABLE+0)
 *       0x9    => DMA or NORMAL (MOVABLE+DMA)
 *       0xa    => MOVABLE (Movable is valid only if HIGHMEM is set too)
 *       0xb    => BAD (MOVABLE+HIGHMEM+DMA)
 *       0xc    => DMA32 or NORMAL (MOVABLE+DMA32)
 *       0xd    => BAD (MOVABLE+DMA32+DMA)
 *       0xe    => BAD (MOVABLE+DMA32+HIGHMEM)
 *       0xf    => BAD (MOVABLE+DMA32+HIGHMEM+DMA)
 *
 * GFP_ZONES_SHIFT must be <= 2 on 32 bit platforms.
 */

#if defined(CONFIG_ZONE_DEVICE) && (MAX_NR_ZONES-1) <= 4
/* ZONE_DEVICE is not a valid GFP zone specifier */
#define GFP_ZONES_SHIFT 2
#else
#define GFP_ZONES_SHIFT ZONES_SHIFT
#endif

#if 16 * GFP_ZONES_SHIFT > BITS_PER_LONG
#error GFP_ZONES_SHIFT too large to create GFP_ZONE_TABLE integer
#endif

#define GFP_ZONE_TABLE ( \
	(ZONE_NORMAL << 0 * GFP_ZONES_SHIFT)				       \
	| (OPT_ZONE_DMA << ___GFP_DMA * GFP_ZONES_SHIFT)		       \
	| (OPT_ZONE_HIGHMEM << ___GFP_HIGHMEM * GFP_ZONES_SHIFT)	       \
	| (OPT_ZONE_DMA32 << ___GFP_DMA32 * GFP_ZONES_SHIFT)		       \
	| (ZONE_NORMAL << ___GFP_MOVABLE * GFP_ZONES_SHIFT)		       \
	| (OPT_ZONE_DMA << (___GFP_MOVABLE | ___GFP_DMA) * GFP_ZONES_SHIFT)    \
	| (ZONE_MOVABLE << (___GFP_MOVABLE | ___GFP_HIGHMEM) * GFP_ZONES_SHIFT)\
	| (OPT_ZONE_DMA32 << (___GFP_MOVABLE | ___GFP_DMA32) * GFP_ZONES_SHIFT)\
)

/*
 * GFP_ZONE_BAD is a bitmap for all combinations of __GFP_DMA, __GFP_DMA32
 * __GFP_HIGHMEM and __GFP_MOVABLE that are not permitted. One flag per
 * entry starting with bit 0. Bit is set if the combination is not
 * allowed.
 */
#define GFP_ZONE_BAD ( \
	1 << (___GFP_DMA | ___GFP_HIGHMEM)				      \
	| 1 << (___GFP_DMA | ___GFP_DMA32)				      \
	| 1 << (___GFP_DMA32 | ___GFP_HIGHMEM)				      \
	| 1 << (___GFP_DMA | ___GFP_DMA32 | ___GFP_HIGHMEM)		      \
	| 1 << (___GFP_MOVABLE | ___GFP_HIGHMEM | ___GFP_DMA)		      \
	| 1 << (___GFP_MOVABLE | ___GFP_DMA32 | ___GFP_DMA)		      \
	| 1 << (___GFP_MOVABLE | ___GFP_DMA32 | ___GFP_HIGHMEM)		      \
	| 1 << (___GFP_MOVABLE | ___GFP_DMA32 | ___GFP_DMA | ___GFP_HIGHMEM)  \
)

static inline enum zone_type gfp_zone(gfp_t flags)
{
	enum zone_type z;
	int bit = (__force int) (flags & GFP_ZONEMASK);

	z = (GFP_ZONE_TABLE >> (bit * GFP_ZONES_SHIFT)) &
					 ((1 << GFP_ZONES_SHIFT) - 1);
	VM_BUG_ON((GFP_ZONE_BAD >> bit) & 1);
	return z;
}

/*
 * There is only one page-allocator function, and two main namespaces to
 * it. The alloc_page*() variants return 'struct page *' and as such
 * can allocate highmem pages, the *get*page*() variants return
 * virtual kernel addresses to the allocated page(s).
 */

static inline int gfp_zonelist(gfp_t flags)
{
#ifdef CONFIG_NUMA
	if (unlikely(flags & __GFP_THISNODE))
		return ZONELIST_NOFALLBACK;
#endif
	return ZONELIST_FALLBACK;
}

/*
 * gfp flag masking for nested internal allocations.
 *
 * For code that needs to do allocations inside the public allocation API (e.g.
 * memory allocation tracking code) the allocations need to obey the caller
 * allocation context constrains to prevent allocation context mismatches (e.g.
 * GFP_KERNEL allocations in GFP_NOFS contexts) from potential deadlock
 * situations.
 *
 * It is also assumed that these nested allocations are for internal kernel
 * object storage purposes only and are not going to be used for DMA, etc. Hence
 * we strip out all the zone information and leave just the context information
 * intact.
 *
 * Further, internal allocations must fail before the higher level allocation
 * can fail, so we must make them fail faster and fail silently. We also don't
 * want them to deplete emergency reserves.  Hence nested allocations must be
 * prepared for these allocations to fail.
 */
static inline gfp_t gfp_nested_mask(gfp_t flags)
{
	return ((flags & (GFP_KERNEL | GFP_ATOMIC | __GFP_NOLOCKDEP)) |
		(__GFP_NORETRY | __GFP_NOMEMALLOC | __GFP_NOWARN));
}

/*
 * We get the zone list from the current node and the gfp_mask.
 * This zone list contains a maximum of MAX_NUMNODES*MAX_NR_ZONES zones.
 * There are two zonelists per node, one for all zones with memory and
 * one containing just zones from the node the zonelist belongs to.
 *
 * For the case of non-NUMA systems the NODE_DATA() gets optimized to
 * &contig_page_data at compile-time.
 */
static inline struct zonelist *node_zonelist(int nid, gfp_t flags)
{
	return NODE_DATA(nid)->node_zonelists + gfp_zonelist(flags);
}

#ifndef HAVE_ARCH_FREE_PAGE
static inline void arch_free_page(struct page *page, int order) { }
#endif
#ifndef HAVE_ARCH_ALLOC_PAGE
static inline void arch_alloc_page(struct page *page, int order) { }
#endif

struct page *__alloc_pages_noprof(gfp_t gfp, unsigned int order, int preferred_nid,
		nodemask_t *nodemask);
#define __alloc_pages(...)			alloc_hooks(__alloc_pages_noprof(__VA_ARGS__))

struct folio *__folio_alloc_noprof(gfp_t gfp, unsigned int order, int preferred_nid,
		nodemask_t *nodemask);
#define __folio_alloc(...)			alloc_hooks(__folio_alloc_noprof(__VA_ARGS__))

unsigned long alloc_pages_bulk_noprof(gfp_t gfp, int preferred_nid,
				nodemask_t *nodemask, int nr_pages,
				struct list_head *page_list,
				struct page **page_array);
#define __alloc_pages_bulk(...)			alloc_hooks(alloc_pages_bulk_noprof(__VA_ARGS__))

unsigned long alloc_pages_bulk_array_mempolicy_noprof(gfp_t gfp,
				unsigned long nr_pages,
				struct page **page_array);
#define  alloc_pages_bulk_array_mempolicy(...)				\
	alloc_hooks(alloc_pages_bulk_array_mempolicy_noprof(__VA_ARGS__))

/* Bulk allocate order-0 pages */
#define alloc_pages_bulk_list(_gfp, _nr_pages, _list)			\
	__alloc_pages_bulk(_gfp, numa_mem_id(), NULL, _nr_pages, _list, NULL)

#define alloc_pages_bulk_array(_gfp, _nr_pages, _page_array)		\
	__alloc_pages_bulk(_gfp, numa_mem_id(), NULL, _nr_pages, NULL, _page_array)

static inline unsigned long
alloc_pages_bulk_array_node_noprof(gfp_t gfp, int nid, unsigned long nr_pages,
				   struct page **page_array)
{
	if (nid == NUMA_NO_NODE)
		nid = numa_mem_id();

	return alloc_pages_bulk_noprof(gfp, nid, NULL, nr_pages, NULL, page_array);
}

#define alloc_pages_bulk_array_node(...)				\
	alloc_hooks(alloc_pages_bulk_array_node_noprof(__VA_ARGS__))

static inline void warn_if_node_offline(int this_node, gfp_t gfp_mask)
{
	gfp_t warn_gfp = gfp_mask & (__GFP_THISNODE|__GFP_NOWARN);

	if (warn_gfp != (__GFP_THISNODE|__GFP_NOWARN))
		return;

	if (node_online(this_node))
		return;

	pr_warn("%pGg allocation from offline node %d\n", &gfp_mask, this_node);
	dump_stack();
}

/*
 * Allocate pages, preferring the node given as nid. The node must be valid and
 * online. For more general interface, see alloc_pages_node().
 */
static inline struct page *
__alloc_pages_node_noprof(int nid, gfp_t gfp_mask, unsigned int order)
{
	VM_BUG_ON(nid < 0 || nid >= MAX_NUMNODES);
	warn_if_node_offline(nid, gfp_mask);

	return __alloc_pages_noprof(gfp_mask, order, nid, NULL);
}

#define  __alloc_pages_node(...)		alloc_hooks(__alloc_pages_node_noprof(__VA_ARGS__))

static inline
struct folio *__folio_alloc_node_noprof(gfp_t gfp, unsigned int order, int nid)
{
	VM_BUG_ON(nid < 0 || nid >= MAX_NUMNODES);
	warn_if_node_offline(nid, gfp);

	return __folio_alloc_noprof(gfp, order, nid, NULL);
}

#define  __folio_alloc_node(...)		alloc_hooks(__folio_alloc_node_noprof(__VA_ARGS__))

/*
 * Allocate pages, preferring the node given as nid. When nid == NUMA_NO_NODE,
 * prefer the current CPU's closest node. Otherwise node must be valid and
 * online.
 */
static inline struct page *alloc_pages_node_noprof(int nid, gfp_t gfp_mask,
						   unsigned int order)
{
	if (nid == NUMA_NO_NODE)
		nid = numa_mem_id();

	return __alloc_pages_node_noprof(nid, gfp_mask, order);
}

#define  alloc_pages_node(...)			alloc_hooks(alloc_pages_node_noprof(__VA_ARGS__))

#ifdef CONFIG_NUMA
struct page *alloc_pages_noprof(gfp_t gfp, unsigned int order);
struct folio *folio_alloc_noprof(gfp_t gfp, unsigned int order);
struct folio *folio_alloc_mpol_noprof(gfp_t gfp, unsigned int order,
		struct mempolicy *mpol, pgoff_t ilx, int nid);
struct folio *vma_alloc_folio_noprof(gfp_t gfp, int order, struct vm_area_struct *vma,
		unsigned long addr);
#else
static inline struct page *alloc_pages_noprof(gfp_t gfp_mask, unsigned int order)
{
	return alloc_pages_node_noprof(numa_node_id(), gfp_mask, order);
}
static inline struct folio *folio_alloc_noprof(gfp_t gfp, unsigned int order)
{
	return __folio_alloc_node_noprof(gfp, order, numa_node_id());
}
static inline struct folio *folio_alloc_mpol_noprof(gfp_t gfp, unsigned int order,
		struct mempolicy *mpol, pgoff_t ilx, int nid)
{
	return folio_alloc_noprof(gfp, order);
}
#define vma_alloc_folio_noprof(gfp, order, vma, addr)		\
	folio_alloc_noprof(gfp, order)
#endif

#define alloc_pages(...)			alloc_hooks(alloc_pages_noprof(__VA_ARGS__))
#define folio_alloc(...)			alloc_hooks(folio_alloc_noprof(__VA_ARGS__))
#define folio_alloc_mpol(...)			alloc_hooks(folio_alloc_mpol_noprof(__VA_ARGS__))
#define vma_alloc_folio(...)			alloc_hooks(vma_alloc_folio_noprof(__VA_ARGS__))

#define alloc_page(gfp_mask) alloc_pages(gfp_mask, 0)

static inline struct page *alloc_page_vma_noprof(gfp_t gfp,
		struct vm_area_struct *vma, unsigned long addr)
{
	struct folio *folio = vma_alloc_folio_noprof(gfp, 0, vma, addr);

	return &folio->page;
}
#define alloc_page_vma(...)			alloc_hooks(alloc_page_vma_noprof(__VA_ARGS__))

extern unsigned long get_free_pages_noprof(gfp_t gfp_mask, unsigned int order);
#define __get_free_pages(...)			alloc_hooks(get_free_pages_noprof(__VA_ARGS__))

extern unsigned long get_zeroed_page_noprof(gfp_t gfp_mask);
#define get_zeroed_page(...)			alloc_hooks(get_zeroed_page_noprof(__VA_ARGS__))

void *alloc_pages_exact_noprof(size_t size, gfp_t gfp_mask) __alloc_size(1);
#define alloc_pages_exact(...)			alloc_hooks(alloc_pages_exact_noprof(__VA_ARGS__))

void free_pages_exact(void *virt, size_t size);

__meminit void *alloc_pages_exact_nid_noprof(int nid, size_t size, gfp_t gfp_mask) __alloc_size(2);
#define alloc_pages_exact_nid(...)					\
	alloc_hooks(alloc_pages_exact_nid_noprof(__VA_ARGS__))

#define __get_free_page(gfp_mask)					\
	__get_free_pages((gfp_mask), 0)

#define __get_dma_pages(gfp_mask, order)				\
	__get_free_pages((gfp_mask) | GFP_DMA, (order))

extern void __free_pages(struct page *page, unsigned int order);
extern void free_pages(unsigned long addr, unsigned int order);

#define __free_page(page) __free_pages((page), 0)
#define free_page(addr) free_pages((addr), 0)

void page_alloc_init_cpuhp(void);
int decay_pcp_high(struct zone *zone, struct per_cpu_pages *pcp);
void drain_zone_pages(struct zone *zone, struct per_cpu_pages *pcp);
void drain_all_pages(struct zone *zone);
void drain_local_pages(struct zone *zone);

void page_alloc_init_late(void);
void setup_pcp_cacheinfo(unsigned int cpu);

/*
 * gfp_allowed_mask is set to GFP_BOOT_MASK during early boot to restrict what
 * GFP flags are used before interrupts are enabled. Once interrupts are
 * enabled, it is set to __GFP_BITS_MASK while the system is running. During
 * hibernation, it is used by PM to avoid I/O during memory allocation while
 * devices are suspended.
 */
extern gfp_t gfp_allowed_mask;

/* Returns true if the gfp_mask allows use of ALLOC_NO_WATERMARK */
bool gfp_pfmemalloc_allowed(gfp_t gfp_mask);

static inline bool gfp_has_io_fs(gfp_t gfp)
{
	return (gfp & (__GFP_IO | __GFP_FS)) == (__GFP_IO | __GFP_FS);
}

/*
 * Check if the gfp flags allow compaction - GFP_NOIO is a really
 * tricky context because the migration might require IO.
 */
static inline bool gfp_compaction_allowed(gfp_t gfp_mask)
{
	return IS_ENABLED(CONFIG_COMPACTION) && (gfp_mask & __GFP_IO);
}

extern gfp_t vma_thp_gfp_mask(struct vm_area_struct *vma);

#ifdef CONFIG_CONTIG_ALLOC
/* The below functions must be run on a range from a single zone. */
extern int alloc_contig_range_noprof(unsigned long start, unsigned long end,
			      unsigned migratetype, gfp_t gfp_mask);
#define alloc_contig_range(...)			alloc_hooks(alloc_contig_range_noprof(__VA_ARGS__))

extern struct page *alloc_contig_pages_noprof(unsigned long nr_pages, gfp_t gfp_mask,
					      int nid, nodemask_t *nodemask);
#define alloc_contig_pages(...)			alloc_hooks(alloc_contig_pages_noprof(__VA_ARGS__))

#endif
void free_contig_range(unsigned long pfn, unsigned long nr_pages);

#ifdef CONFIG_CONTIG_ALLOC
static inline struct folio *folio_alloc_gigantic_noprof(int order, gfp_t gfp,
							int nid, nodemask_t *node)
{
	struct page *page;

	if (WARN_ON(!order || !(gfp & __GFP_COMP)))
		return NULL;

	page = alloc_contig_pages_noprof(1 << order, gfp, nid, node);

	return page ? page_folio(page) : NULL;
}
#else
static inline struct folio *folio_alloc_gigantic_noprof(int order, gfp_t gfp,
							int nid, nodemask_t *node)
{
	return NULL;
}
#endif
/* This should be paired with folio_put() rather than free_contig_range(). */
#define folio_alloc_gigantic(...) alloc_hooks(folio_alloc_gigantic_noprof(__VA_ARGS__))

#endif /* __LINUX_GFP_H */
