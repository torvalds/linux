#ifndef _LINUX_MIGRATE_H
#define _LINUX_MIGRATE_H

#include <linux/mm.h>
#include <linux/mempolicy.h>
#include <linux/migrate_mode.h>

typedef struct page *new_page_t(struct page *, unsigned long private, int **);

/*
 * Return values from addresss_space_operations.migratepage():
 * - negative errno on page migration failure;
 * - zero on page migration success;
 *
 * The balloon page migration introduces this special case where a 'distinct'
 * return code is used to flag a successful page migration to unmap_and_move().
 * This approach is necessary because page migration can race against balloon
 * deflation procedure, and for such case we could introduce a nasty page leak
 * if a successfully migrated balloon page gets released concurrently with
 * migration's unmap_and_move() wrap-up steps.
 */
#define MIGRATEPAGE_SUCCESS		0
#define MIGRATEPAGE_BALLOON_SUCCESS	1 /* special ret code for balloon page
					   * sucessful migration case.
					   */
enum migrate_reason {
	MR_COMPACTION,
	MR_MEMORY_FAILURE,
	MR_MEMORY_HOTPLUG,
	MR_SYSCALL,		/* also applies to cpusets */
	MR_MEMPOLICY_MBIND,
	MR_NUMA_MISPLACED,
	MR_CMA
};

#ifdef CONFIG_MIGRATION

extern void putback_lru_pages(struct list_head *l);
extern void putback_movable_pages(struct list_head *l);
extern int migrate_page(struct address_space *,
			struct page *, struct page *, enum migrate_mode);
extern int migrate_pages(struct list_head *l, new_page_t x,
		unsigned long private, enum migrate_mode mode, int reason);
extern int migrate_huge_page(struct page *, new_page_t x,
		unsigned long private, enum migrate_mode mode);

extern int fail_migrate_page(struct address_space *,
			struct page *, struct page *);

extern int migrate_prep(void);
extern int migrate_prep_local(void);
extern int migrate_vmas(struct mm_struct *mm,
		const nodemask_t *from, const nodemask_t *to,
		unsigned long flags);
extern void migrate_page_copy(struct page *newpage, struct page *page);
extern int migrate_huge_page_move_mapping(struct address_space *mapping,
				  struct page *newpage, struct page *page);
extern int migrate_page_move_mapping(struct address_space *mapping,
               struct page *newpage, struct page *page,
               struct buffer_head *head, enum migrate_mode mode);
#else

static inline void putback_lru_pages(struct list_head *l) {}
static inline void putback_movable_pages(struct list_head *l) {}
static inline int migrate_pages(struct list_head *l, new_page_t x,
		unsigned long private, enum migrate_mode mode, int reason)
	{ return -ENOSYS; }
static inline int migrate_huge_page(struct page *page, new_page_t x,
		unsigned long private, enum migrate_mode mode)
	{ return -ENOSYS; }

static inline int migrate_prep(void) { return -ENOSYS; }
static inline int migrate_prep_local(void) { return -ENOSYS; }

static inline int migrate_vmas(struct mm_struct *mm,
		const nodemask_t *from, const nodemask_t *to,
		unsigned long flags)
{
	return -ENOSYS;
}

static inline void migrate_page_copy(struct page *newpage,
				     struct page *page) {}

static inline int migrate_huge_page_move_mapping(struct address_space *mapping,
				  struct page *newpage, struct page *page)
{
	return -ENOSYS;
}

/* Possible settings for the migrate_page() method in address_operations */
#define migrate_page NULL
#define fail_migrate_page NULL

#endif /* CONFIG_MIGRATION */

#ifdef CONFIG_NUMA_BALANCING
extern int migrate_misplaced_page(struct page *page, int node);
extern int migrate_misplaced_page(struct page *page, int node);
extern bool migrate_ratelimited(int node);
#else
static inline int migrate_misplaced_page(struct page *page, int node)
{
	return -EAGAIN; /* can't migrate now */
}
static inline bool migrate_ratelimited(int node)
{
	return false;
}
#endif /* CONFIG_NUMA_BALANCING */

#if defined(CONFIG_NUMA_BALANCING) && defined(CONFIG_TRANSPARENT_HUGEPAGE)
extern int migrate_misplaced_transhuge_page(struct mm_struct *mm,
			struct vm_area_struct *vma,
			pmd_t *pmd, pmd_t entry,
			unsigned long address,
			struct page *page, int node);
#else
static inline int migrate_misplaced_transhuge_page(struct mm_struct *mm,
			struct vm_area_struct *vma,
			pmd_t *pmd, pmd_t entry,
			unsigned long address,
			struct page *page, int node)
{
	return -EAGAIN;
}
#endif /* CONFIG_NUMA_BALANCING && CONFIG_TRANSPARENT_HUGEPAGE*/

#endif /* _LINUX_MIGRATE_H */
