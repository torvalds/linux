#ifndef _LINUX_MIGRATE_H
#define _LINUX_MIGRATE_H

#include <linux/mm.h>
#include <linux/mempolicy.h>
#include <linux/pagemap.h>

typedef struct page *new_page_t(struct page *, unsigned long private, int **);

#ifdef CONFIG_MIGRATION
/* Check if a vma is migratable */
static inline int vma_migratable(struct vm_area_struct *vma)
{
	if (vma->vm_flags & (VM_IO|VM_HUGETLB|VM_PFNMAP|VM_RESERVED))
		return 0;
	/*
	 * Migration allocates pages in the highest zone. If we cannot
	 * do so then migration (at least from node to node) is not
	 * possible.
	 */
	if (vma->vm_file &&
		gfp_zone(mapping_gfp_mask(vma->vm_file->f_mapping))
								< policy_zone)
			return 0;
	return 1;
}

extern int isolate_lru_page(struct page *p, struct list_head *pagelist);
extern int putback_lru_pages(struct list_head *l);
extern int migrate_page(struct address_space *,
			struct page *, struct page *);
extern int migrate_pages(struct list_head *l, new_page_t x, unsigned long);

extern int fail_migrate_page(struct address_space *,
			struct page *, struct page *);

extern int migrate_prep(void);
extern int migrate_vmas(struct mm_struct *mm,
		const nodemask_t *from, const nodemask_t *to,
		unsigned long flags);
#else
static inline int vma_migratable(struct vm_area_struct *vma)
					{ return 0; }

static inline int isolate_lru_page(struct page *p, struct list_head *list)
					{ return -ENOSYS; }
static inline int putback_lru_pages(struct list_head *l) { return 0; }
static inline int migrate_pages(struct list_head *l, new_page_t x,
		unsigned long private) { return -ENOSYS; }

static inline int migrate_pages_to(struct list_head *pagelist,
			struct vm_area_struct *vma, int dest) { return 0; }

static inline int migrate_prep(void) { return -ENOSYS; }

static inline int migrate_vmas(struct mm_struct *mm,
		const nodemask_t *from, const nodemask_t *to,
		unsigned long flags)
{
	return -ENOSYS;
}

/* Possible settings for the migrate_page() method in address_operations */
#define migrate_page NULL
#define fail_migrate_page NULL

#endif /* CONFIG_MIGRATION */
#endif /* _LINUX_MIGRATE_H */
