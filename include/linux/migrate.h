/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MIGRATE_H
#define _LINUX_MIGRATE_H

#include <linux/mm.h>
#include <linux/mempolicy.h>
#include <linux/migrate_mode.h>
#include <linux/hugetlb.h>

typedef struct folio *new_folio_t(struct folio *folio, unsigned long private);
typedef void free_folio_t(struct folio *folio, unsigned long private);

struct migration_target_control;

/*
 * Return values from addresss_space_operations.migratepage():
 * - negative errno on page migration failure;
 * - zero on page migration success;
 */
#define MIGRATEPAGE_SUCCESS		0
#define MIGRATEPAGE_UNMAP		1

/**
 * struct movable_operations - Driver page migration
 * @isolate_page:
 * The VM calls this function to prepare the page to be moved.  The page
 * is locked and the driver should not unlock it.  The driver should
 * return ``true`` if the page is movable and ``false`` if it is not
 * currently movable.  After this function returns, the VM uses the
 * page->lru field, so the driver must preserve any information which
 * is usually stored here.
 *
 * @migrate_page:
 * After isolation, the VM calls this function with the isolated
 * @src page.  The driver should copy the contents of the
 * @src page to the @dst page and set up the fields of @dst page.
 * Both pages are locked.
 * If page migration is successful, the driver should call
 * __ClearPageMovable(@src) and return MIGRATEPAGE_SUCCESS.
 * If the driver cannot migrate the page at the moment, it can return
 * -EAGAIN.  The VM interprets this as a temporary migration failure and
 * will retry it later.  Any other error value is a permanent migration
 * failure and migration will not be retried.
 * The driver shouldn't touch the @src->lru field while in the
 * migrate_page() function.  It may write to @dst->lru.
 *
 * @putback_page:
 * If migration fails on the isolated page, the VM informs the driver
 * that the page is no longer a candidate for migration by calling
 * this function.  The driver should put the isolated page back into
 * its own data structure.
 */
struct movable_operations {
	bool (*isolate_page)(struct page *, isolate_mode_t);
	int (*migrate_page)(struct page *dst, struct page *src,
			enum migrate_mode);
	void (*putback_page)(struct page *);
};

/* Defined in mm/debug.c: */
extern const char *migrate_reason_names[MR_TYPES];

#ifdef CONFIG_MIGRATION

void putback_movable_pages(struct list_head *l);
int migrate_folio(struct address_space *mapping, struct folio *dst,
		struct folio *src, enum migrate_mode mode);
int migrate_pages(struct list_head *l, new_folio_t new, free_folio_t free,
		  unsigned long private, enum migrate_mode mode, int reason,
		  unsigned int *ret_succeeded);
struct folio *alloc_migration_target(struct folio *src, unsigned long private);
bool isolate_movable_page(struct page *page, isolate_mode_t mode);

int migrate_huge_page_move_mapping(struct address_space *mapping,
		struct folio *dst, struct folio *src);
void migration_entry_wait_on_locked(swp_entry_t entry, spinlock_t *ptl)
		__releases(ptl);
void folio_migrate_flags(struct folio *newfolio, struct folio *folio);
int folio_migrate_mapping(struct address_space *mapping,
		struct folio *newfolio, struct folio *folio, int extra_count);

#else

static inline void putback_movable_pages(struct list_head *l) {}
static inline int migrate_pages(struct list_head *l, new_folio_t new,
		free_folio_t free, unsigned long private,
		enum migrate_mode mode, int reason, unsigned int *ret_succeeded)
	{ return -ENOSYS; }
static inline struct folio *alloc_migration_target(struct folio *src,
		unsigned long private)
	{ return NULL; }
static inline bool isolate_movable_page(struct page *page, isolate_mode_t mode)
	{ return false; }

static inline int migrate_huge_page_move_mapping(struct address_space *mapping,
				  struct folio *dst, struct folio *src)
{
	return -ENOSYS;
}

#endif /* CONFIG_MIGRATION */

#ifdef CONFIG_COMPACTION
bool PageMovable(struct page *page);
void __SetPageMovable(struct page *page, const struct movable_operations *ops);
void __ClearPageMovable(struct page *page);
#else
static inline bool PageMovable(struct page *page) { return false; }
static inline void __SetPageMovable(struct page *page,
		const struct movable_operations *ops)
{
}
static inline void __ClearPageMovable(struct page *page)
{
}
#endif

static inline bool folio_test_movable(struct folio *folio)
{
	return PageMovable(&folio->page);
}

static inline
const struct movable_operations *folio_movable_ops(struct folio *folio)
{
	VM_BUG_ON(!__folio_test_movable(folio));

	return (const struct movable_operations *)
		((unsigned long)folio->mapping - PAGE_MAPPING_MOVABLE);
}

static inline
const struct movable_operations *page_movable_ops(struct page *page)
{
	VM_BUG_ON(!__PageMovable(page));

	return (const struct movable_operations *)
		((unsigned long)page->mapping - PAGE_MAPPING_MOVABLE);
}

#ifdef CONFIG_NUMA_BALANCING
int migrate_misplaced_folio_prepare(struct folio *folio,
		struct vm_area_struct *vma, int node);
int migrate_misplaced_folio(struct folio *folio, struct vm_area_struct *vma,
			   int node);
#else
static inline int migrate_misplaced_folio_prepare(struct folio *folio,
		struct vm_area_struct *vma, int node)
{
	return -EAGAIN; /* can't migrate now */
}
static inline int migrate_misplaced_folio(struct folio *folio,
					 struct vm_area_struct *vma, int node)
{
	return -EAGAIN; /* can't migrate now */
}
#endif /* CONFIG_NUMA_BALANCING */

#ifdef CONFIG_MIGRATION

/*
 * Watch out for PAE architecture, which has an unsigned long, and might not
 * have enough bits to store all physical address and flags. So far we have
 * enough room for all our flags.
 */
#define MIGRATE_PFN_VALID	(1UL << 0)
#define MIGRATE_PFN_MIGRATE	(1UL << 1)
#define MIGRATE_PFN_WRITE	(1UL << 3)
#define MIGRATE_PFN_SHIFT	6

static inline struct page *migrate_pfn_to_page(unsigned long mpfn)
{
	if (!(mpfn & MIGRATE_PFN_VALID))
		return NULL;
	return pfn_to_page(mpfn >> MIGRATE_PFN_SHIFT);
}

static inline unsigned long migrate_pfn(unsigned long pfn)
{
	return (pfn << MIGRATE_PFN_SHIFT) | MIGRATE_PFN_VALID;
}

enum migrate_vma_direction {
	MIGRATE_VMA_SELECT_SYSTEM = 1 << 0,
	MIGRATE_VMA_SELECT_DEVICE_PRIVATE = 1 << 1,
	MIGRATE_VMA_SELECT_DEVICE_COHERENT = 1 << 2,
};

struct migrate_vma {
	struct vm_area_struct	*vma;
	/*
	 * Both src and dst array must be big enough for
	 * (end - start) >> PAGE_SHIFT entries.
	 *
	 * The src array must not be modified by the caller after
	 * migrate_vma_setup(), and must not change the dst array after
	 * migrate_vma_pages() returns.
	 */
	unsigned long		*dst;
	unsigned long		*src;
	unsigned long		cpages;
	unsigned long		npages;
	unsigned long		start;
	unsigned long		end;

	/*
	 * Set to the owner value also stored in page->pgmap->owner for
	 * migrating out of device private memory. The flags also need to
	 * be set to MIGRATE_VMA_SELECT_DEVICE_PRIVATE.
	 * The caller should always set this field when using mmu notifier
	 * callbacks to avoid device MMU invalidations for device private
	 * pages that are not being migrated.
	 */
	void			*pgmap_owner;
	unsigned long		flags;

	/*
	 * Set to vmf->page if this is being called to migrate a page as part of
	 * a migrate_to_ram() callback.
	 */
	struct page		*fault_page;
};

int migrate_vma_setup(struct migrate_vma *args);
void migrate_vma_pages(struct migrate_vma *migrate);
void migrate_vma_finalize(struct migrate_vma *migrate);
int migrate_device_range(unsigned long *src_pfns, unsigned long start,
			unsigned long npages);
void migrate_device_pages(unsigned long *src_pfns, unsigned long *dst_pfns,
			unsigned long npages);
void migrate_device_finalize(unsigned long *src_pfns,
			unsigned long *dst_pfns, unsigned long npages);

#endif /* CONFIG_MIGRATION */

#endif /* _LINUX_MIGRATE_H */
