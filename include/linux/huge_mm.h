/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HUGE_MM_H
#define _LINUX_HUGE_MM_H

#include <linux/sched/coredump.h>
#include <linux/mm_types.h>

#include <linux/fs.h> /* only for vma_is_dax() */

vm_fault_t do_huge_pmd_anonymous_page(struct vm_fault *vmf);
int copy_huge_pmd(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
		  struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma);
void huge_pmd_set_accessed(struct vm_fault *vmf);
int copy_huge_pud(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pud_t *dst_pud, pud_t *src_pud, unsigned long addr,
		  struct vm_area_struct *vma);

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
void huge_pud_set_accessed(struct vm_fault *vmf, pud_t orig_pud);
#else
static inline void huge_pud_set_accessed(struct vm_fault *vmf, pud_t orig_pud)
{
}
#endif

vm_fault_t do_huge_pmd_wp_page(struct vm_fault *vmf);
struct page *follow_trans_huge_pmd(struct vm_area_struct *vma,
				   unsigned long addr, pmd_t *pmd,
				   unsigned int flags);
bool madvise_free_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
			   pmd_t *pmd, unsigned long addr, unsigned long next);
int zap_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma, pmd_t *pmd,
		 unsigned long addr);
int zap_huge_pud(struct mmu_gather *tlb, struct vm_area_struct *vma, pud_t *pud,
		 unsigned long addr);
bool move_huge_pmd(struct vm_area_struct *vma, unsigned long old_addr,
		   unsigned long new_addr, pmd_t *old_pmd, pmd_t *new_pmd);
int change_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		    pmd_t *pmd, unsigned long addr, pgprot_t newprot,
		    unsigned long cp_flags);
vm_fault_t vmf_insert_pfn_pmd_prot(struct vm_fault *vmf, pfn_t pfn,
				   pgprot_t pgprot, bool write);

/**
 * vmf_insert_pfn_pmd - insert a pmd size pfn
 * @vmf: Structure describing the fault
 * @pfn: pfn to insert
 * @pgprot: page protection to use
 * @write: whether it's a write fault
 *
 * Insert a pmd size pfn. See vmf_insert_pfn() for additional info.
 *
 * Return: vm_fault_t value.
 */
static inline vm_fault_t vmf_insert_pfn_pmd(struct vm_fault *vmf, pfn_t pfn,
					    bool write)
{
	return vmf_insert_pfn_pmd_prot(vmf, pfn, vmf->vma->vm_page_prot, write);
}
vm_fault_t vmf_insert_pfn_pud_prot(struct vm_fault *vmf, pfn_t pfn,
				   pgprot_t pgprot, bool write);

/**
 * vmf_insert_pfn_pud - insert a pud size pfn
 * @vmf: Structure describing the fault
 * @pfn: pfn to insert
 * @pgprot: page protection to use
 * @write: whether it's a write fault
 *
 * Insert a pud size pfn. See vmf_insert_pfn() for additional info.
 *
 * Return: vm_fault_t value.
 */
static inline vm_fault_t vmf_insert_pfn_pud(struct vm_fault *vmf, pfn_t pfn,
					    bool write)
{
	return vmf_insert_pfn_pud_prot(vmf, pfn, vmf->vma->vm_page_prot, write);
}

enum transparent_hugepage_flag {
	TRANSPARENT_HUGEPAGE_NEVER_DAX,
	TRANSPARENT_HUGEPAGE_FLAG,
	TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
	TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG,
	TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG,
	TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG,
	TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG,
	TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG,
	TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG,
};

struct kobject;
struct kobj_attribute;

ssize_t single_hugepage_flag_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count,
				   enum transparent_hugepage_flag flag);
ssize_t single_hugepage_flag_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf,
				  enum transparent_hugepage_flag flag);
extern struct kobj_attribute shmem_enabled_attr;

#define HPAGE_PMD_ORDER (HPAGE_PMD_SHIFT-PAGE_SHIFT)
#define HPAGE_PMD_NR (1<<HPAGE_PMD_ORDER)

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define HPAGE_PMD_SHIFT PMD_SHIFT
#define HPAGE_PMD_SIZE	((1UL) << HPAGE_PMD_SHIFT)
#define HPAGE_PMD_MASK	(~(HPAGE_PMD_SIZE - 1))

#define HPAGE_PUD_SHIFT PUD_SHIFT
#define HPAGE_PUD_SIZE	((1UL) << HPAGE_PUD_SHIFT)
#define HPAGE_PUD_MASK	(~(HPAGE_PUD_SIZE - 1))

extern unsigned long transparent_hugepage_flags;

#define hugepage_flags_enabled()					       \
	(transparent_hugepage_flags &				       \
	 ((1<<TRANSPARENT_HUGEPAGE_FLAG) |		       \
	  (1<<TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG)))
#define hugepage_flags_always()				\
	(transparent_hugepage_flags &			\
	 (1<<TRANSPARENT_HUGEPAGE_FLAG))

/*
 * Do the below checks:
 *   - For file vma, check if the linear page offset of vma is
 *     HPAGE_PMD_NR aligned within the file.  The hugepage is
 *     guaranteed to be hugepage-aligned within the file, but we must
 *     check that the PMD-aligned addresses in the VMA map to
 *     PMD-aligned offsets within the file, else the hugepage will
 *     not be PMD-mappable.
 *   - For all vmas, check if the haddr is in an aligned HPAGE_PMD_SIZE
 *     area.
 */
static inline bool transhuge_vma_suitable(struct vm_area_struct *vma,
		unsigned long addr)
{
	unsigned long haddr;

	/* Don't have to check pgoff for anonymous vma */
	if (!vma_is_anonymous(vma)) {
		if (!IS_ALIGNED((vma->vm_start >> PAGE_SHIFT) - vma->vm_pgoff,
				HPAGE_PMD_NR))
			return false;
	}

	haddr = addr & HPAGE_PMD_MASK;

	if (haddr < vma->vm_start || haddr + HPAGE_PMD_SIZE > vma->vm_end)
		return false;
	return true;
}

static inline bool file_thp_enabled(struct vm_area_struct *vma)
{
	struct inode *inode;

	if (!vma->vm_file)
		return false;

	inode = vma->vm_file->f_inode;

	return (IS_ENABLED(CONFIG_READ_ONLY_THP_FOR_FS)) &&
	       (vma->vm_flags & VM_EXEC) &&
	       !inode_is_open_for_write(inode) && S_ISREG(inode->i_mode);
}

bool hugepage_vma_check(struct vm_area_struct *vma, unsigned long vm_flags,
			bool smaps, bool in_pf, bool enforce_sysfs);

#define transparent_hugepage_use_zero_page()				\
	(transparent_hugepage_flags &					\
	 (1<<TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG))

unsigned long thp_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags);

void prep_transhuge_page(struct page *page);
void free_transhuge_page(struct page *page);

bool can_split_folio(struct folio *folio, int *pextra_pins);
int split_huge_page_to_list(struct page *page, struct list_head *list);
static inline int split_huge_page(struct page *page)
{
	return split_huge_page_to_list(page, NULL);
}
void deferred_split_folio(struct folio *folio);

void __split_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long address, bool freeze, struct folio *folio);

#define split_huge_pmd(__vma, __pmd, __address)				\
	do {								\
		pmd_t *____pmd = (__pmd);				\
		if (is_swap_pmd(*____pmd) || pmd_trans_huge(*____pmd)	\
					|| pmd_devmap(*____pmd))	\
			__split_huge_pmd(__vma, __pmd, __address,	\
						false, NULL);		\
	}  while (0)


void split_huge_pmd_address(struct vm_area_struct *vma, unsigned long address,
		bool freeze, struct folio *folio);

void __split_huge_pud(struct vm_area_struct *vma, pud_t *pud,
		unsigned long address);

#define split_huge_pud(__vma, __pud, __address)				\
	do {								\
		pud_t *____pud = (__pud);				\
		if (pud_trans_huge(*____pud)				\
					|| pud_devmap(*____pud))	\
			__split_huge_pud(__vma, __pud, __address);	\
	}  while (0)

int hugepage_madvise(struct vm_area_struct *vma, unsigned long *vm_flags,
		     int advice);
int madvise_collapse(struct vm_area_struct *vma,
		     struct vm_area_struct **prev,
		     unsigned long start, unsigned long end);
void vma_adjust_trans_huge(struct vm_area_struct *vma, unsigned long start,
			   unsigned long end, long adjust_next);
spinlock_t *__pmd_trans_huge_lock(pmd_t *pmd, struct vm_area_struct *vma);
spinlock_t *__pud_trans_huge_lock(pud_t *pud, struct vm_area_struct *vma);

static inline int is_swap_pmd(pmd_t pmd)
{
	return !pmd_none(pmd) && !pmd_present(pmd);
}

/* mmap_lock must be held on entry */
static inline spinlock_t *pmd_trans_huge_lock(pmd_t *pmd,
		struct vm_area_struct *vma)
{
	if (is_swap_pmd(*pmd) || pmd_trans_huge(*pmd) || pmd_devmap(*pmd))
		return __pmd_trans_huge_lock(pmd, vma);
	else
		return NULL;
}
static inline spinlock_t *pud_trans_huge_lock(pud_t *pud,
		struct vm_area_struct *vma)
{
	if (pud_trans_huge(*pud) || pud_devmap(*pud))
		return __pud_trans_huge_lock(pud, vma);
	else
		return NULL;
}

/**
 * folio_test_pmd_mappable - Can we map this folio with a PMD?
 * @folio: The folio to test
 */
static inline bool folio_test_pmd_mappable(struct folio *folio)
{
	return folio_order(folio) >= HPAGE_PMD_ORDER;
}

struct page *follow_devmap_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd, int flags, struct dev_pagemap **pgmap);
struct page *follow_devmap_pud(struct vm_area_struct *vma, unsigned long addr,
		pud_t *pud, int flags, struct dev_pagemap **pgmap);

vm_fault_t do_huge_pmd_numa_page(struct vm_fault *vmf);

extern struct page *huge_zero_page;
extern unsigned long huge_zero_pfn;

static inline bool is_huge_zero_page(struct page *page)
{
	return READ_ONCE(huge_zero_page) == page;
}

static inline bool is_huge_zero_pmd(pmd_t pmd)
{
	return pmd_present(pmd) && READ_ONCE(huge_zero_pfn) == pmd_pfn(pmd);
}

static inline bool is_huge_zero_pud(pud_t pud)
{
	return false;
}

struct page *mm_get_huge_zero_page(struct mm_struct *mm);
void mm_put_huge_zero_page(struct mm_struct *mm);

#define mk_huge_pmd(page, prot) pmd_mkhuge(mk_pmd(page, prot))

static inline bool thp_migration_supported(void)
{
	return IS_ENABLED(CONFIG_ARCH_ENABLE_THP_MIGRATION);
}

#else /* CONFIG_TRANSPARENT_HUGEPAGE */
#define HPAGE_PMD_SHIFT ({ BUILD_BUG(); 0; })
#define HPAGE_PMD_MASK ({ BUILD_BUG(); 0; })
#define HPAGE_PMD_SIZE ({ BUILD_BUG(); 0; })

#define HPAGE_PUD_SHIFT ({ BUILD_BUG(); 0; })
#define HPAGE_PUD_MASK ({ BUILD_BUG(); 0; })
#define HPAGE_PUD_SIZE ({ BUILD_BUG(); 0; })

static inline bool folio_test_pmd_mappable(struct folio *folio)
{
	return false;
}

static inline bool transhuge_vma_suitable(struct vm_area_struct *vma,
		unsigned long addr)
{
	return false;
}

static inline bool hugepage_vma_check(struct vm_area_struct *vma,
				      unsigned long vm_flags, bool smaps,
				      bool in_pf, bool enforce_sysfs)
{
	return false;
}

static inline void prep_transhuge_page(struct page *page) {}

#define transparent_hugepage_flags 0UL

#define thp_get_unmapped_area	NULL

static inline bool
can_split_folio(struct folio *folio, int *pextra_pins)
{
	return false;
}
static inline int
split_huge_page_to_list(struct page *page, struct list_head *list)
{
	return 0;
}
static inline int split_huge_page(struct page *page)
{
	return 0;
}
static inline void deferred_split_folio(struct folio *folio) {}
#define split_huge_pmd(__vma, __pmd, __address)	\
	do { } while (0)

static inline void __split_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long address, bool freeze, struct folio *folio) {}
static inline void split_huge_pmd_address(struct vm_area_struct *vma,
		unsigned long address, bool freeze, struct folio *folio) {}

#define split_huge_pud(__vma, __pmd, __address)	\
	do { } while (0)

static inline int hugepage_madvise(struct vm_area_struct *vma,
				   unsigned long *vm_flags, int advice)
{
	return -EINVAL;
}

static inline int madvise_collapse(struct vm_area_struct *vma,
				   struct vm_area_struct **prev,
				   unsigned long start, unsigned long end)
{
	return -EINVAL;
}

static inline void vma_adjust_trans_huge(struct vm_area_struct *vma,
					 unsigned long start,
					 unsigned long end,
					 long adjust_next)
{
}
static inline int is_swap_pmd(pmd_t pmd)
{
	return 0;
}
static inline spinlock_t *pmd_trans_huge_lock(pmd_t *pmd,
		struct vm_area_struct *vma)
{
	return NULL;
}
static inline spinlock_t *pud_trans_huge_lock(pud_t *pud,
		struct vm_area_struct *vma)
{
	return NULL;
}

static inline vm_fault_t do_huge_pmd_numa_page(struct vm_fault *vmf)
{
	return 0;
}

static inline bool is_huge_zero_page(struct page *page)
{
	return false;
}

static inline bool is_huge_zero_pmd(pmd_t pmd)
{
	return false;
}

static inline bool is_huge_zero_pud(pud_t pud)
{
	return false;
}

static inline void mm_put_huge_zero_page(struct mm_struct *mm)
{
	return;
}

static inline struct page *follow_devmap_pmd(struct vm_area_struct *vma,
	unsigned long addr, pmd_t *pmd, int flags, struct dev_pagemap **pgmap)
{
	return NULL;
}

static inline struct page *follow_devmap_pud(struct vm_area_struct *vma,
	unsigned long addr, pud_t *pud, int flags, struct dev_pagemap **pgmap)
{
	return NULL;
}

static inline bool thp_migration_supported(void)
{
	return false;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static inline int split_folio_to_list(struct folio *folio,
		struct list_head *list)
{
	return split_huge_page_to_list(&folio->page, list);
}

static inline int split_folio(struct folio *folio)
{
	return split_folio_to_list(folio, NULL);
}

/*
 * archs that select ARCH_WANTS_THP_SWAP but don't support THP_SWP due to
 * limitations in the implementation like arm64 MTE can override this to
 * false
 */
#ifndef arch_thp_swp_supported
static inline bool arch_thp_swp_supported(void)
{
	return true;
}
#endif

#endif /* _LINUX_HUGE_MM_H */
