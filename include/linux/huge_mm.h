#ifndef _LINUX_HUGE_MM_H
#define _LINUX_HUGE_MM_H

extern int do_huge_pmd_anonymous_page(struct mm_struct *mm,
				      struct vm_area_struct *vma,
				      unsigned long address, pmd_t *pmd,
				      unsigned int flags);
extern int copy_huge_pmd(struct mm_struct *dst_mm, struct mm_struct *src_mm,
			 pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
			 struct vm_area_struct *vma);
extern void huge_pmd_set_accessed(struct mm_struct *mm,
				  struct vm_area_struct *vma,
				  unsigned long address, pmd_t *pmd,
				  pmd_t orig_pmd, int dirty);
extern int do_huge_pmd_wp_page(struct mm_struct *mm, struct vm_area_struct *vma,
			       unsigned long address, pmd_t *pmd,
			       pmd_t orig_pmd);
extern struct page *follow_trans_huge_pmd(struct vm_area_struct *vma,
					  unsigned long addr,
					  pmd_t *pmd,
					  unsigned int flags);
extern int zap_huge_pmd(struct mmu_gather *tlb,
			struct vm_area_struct *vma,
			pmd_t *pmd, unsigned long addr);
extern int mincore_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
			unsigned long addr, unsigned long end,
			unsigned char *vec);
extern int move_huge_pmd(struct vm_area_struct *vma,
			 struct vm_area_struct *new_vma,
			 unsigned long old_addr,
			 unsigned long new_addr, unsigned long old_end,
			 pmd_t *old_pmd, pmd_t *new_pmd);
extern int change_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
			unsigned long addr, pgprot_t newprot,
			int prot_numa);

enum transparent_hugepage_flag {
	TRANSPARENT_HUGEPAGE_FLAG,
	TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
	TRANSPARENT_HUGEPAGE_DEFRAG_FLAG,
	TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG,
	TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG,
	TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG,
#ifdef CONFIG_DEBUG_VM
	TRANSPARENT_HUGEPAGE_DEBUG_COW_FLAG,
#endif
};

enum page_check_address_pmd_flag {
	PAGE_CHECK_ADDRESS_PMD_FLAG,
	PAGE_CHECK_ADDRESS_PMD_NOTSPLITTING_FLAG,
	PAGE_CHECK_ADDRESS_PMD_SPLITTING_FLAG,
};
extern pmd_t *page_check_address_pmd(struct page *page,
				     struct mm_struct *mm,
				     unsigned long address,
				     enum page_check_address_pmd_flag flag);

#define HPAGE_PMD_ORDER (HPAGE_PMD_SHIFT-PAGE_SHIFT)
#define HPAGE_PMD_NR (1<<HPAGE_PMD_ORDER)

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define HPAGE_PMD_SHIFT HPAGE_SHIFT
#define HPAGE_PMD_MASK HPAGE_MASK
#define HPAGE_PMD_SIZE HPAGE_SIZE

extern bool is_vma_temporary_stack(struct vm_area_struct *vma);

#define transparent_hugepage_enabled(__vma)				\
	((transparent_hugepage_flags &					\
	  (1<<TRANSPARENT_HUGEPAGE_FLAG) ||				\
	  (transparent_hugepage_flags &					\
	   (1<<TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG) &&			\
	   ((__vma)->vm_flags & VM_HUGEPAGE))) &&			\
	 !((__vma)->vm_flags & VM_NOHUGEPAGE) &&			\
	 !is_vma_temporary_stack(__vma))
#define transparent_hugepage_defrag(__vma)				\
	((transparent_hugepage_flags &					\
	  (1<<TRANSPARENT_HUGEPAGE_DEFRAG_FLAG)) ||			\
	 (transparent_hugepage_flags &					\
	  (1<<TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG) &&		\
	  (__vma)->vm_flags & VM_HUGEPAGE))
#define transparent_hugepage_use_zero_page()				\
	(transparent_hugepage_flags &					\
	 (1<<TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG))
#ifdef CONFIG_DEBUG_VM
#define transparent_hugepage_debug_cow()				\
	(transparent_hugepage_flags &					\
	 (1<<TRANSPARENT_HUGEPAGE_DEBUG_COW_FLAG))
#else /* CONFIG_DEBUG_VM */
#define transparent_hugepage_debug_cow() 0
#endif /* CONFIG_DEBUG_VM */

extern unsigned long transparent_hugepage_flags;
extern int copy_pte_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
			  pmd_t *dst_pmd, pmd_t *src_pmd,
			  struct vm_area_struct *vma,
			  unsigned long addr, unsigned long end);
extern int handle_pte_fault(struct mm_struct *mm,
			    struct vm_area_struct *vma, unsigned long address,
			    pte_t *pte, pmd_t *pmd, unsigned int flags);
extern int split_huge_page_to_list(struct page *page, struct list_head *list);
static inline int split_huge_page(struct page *page)
{
	return split_huge_page_to_list(page, NULL);
}
extern void __split_huge_page_pmd(struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmd);
#define split_huge_page_pmd(__vma, __address, __pmd)			\
	do {								\
		pmd_t *____pmd = (__pmd);				\
		if (unlikely(pmd_trans_huge(*____pmd)))			\
			__split_huge_page_pmd(__vma, __address,		\
					____pmd);			\
	}  while (0)
#define wait_split_huge_page(__anon_vma, __pmd)				\
	do {								\
		pmd_t *____pmd = (__pmd);				\
		anon_vma_lock_write(__anon_vma);			\
		anon_vma_unlock_write(__anon_vma);			\
		BUG_ON(pmd_trans_splitting(*____pmd) ||			\
		       pmd_trans_huge(*____pmd));			\
	} while (0)
extern void split_huge_page_pmd_mm(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd);
#if HPAGE_PMD_ORDER >= MAX_ORDER
#error "hugepages can't be allocated by the buddy allocator"
#endif
extern int hugepage_madvise(struct vm_area_struct *vma,
			    unsigned long *vm_flags, int advice);
extern void __vma_adjust_trans_huge(struct vm_area_struct *vma,
				    unsigned long start,
				    unsigned long end,
				    long adjust_next);
extern int __pmd_trans_huge_lock(pmd_t *pmd,
				 struct vm_area_struct *vma);
/* mmap_sem must be held on entry */
static inline int pmd_trans_huge_lock(pmd_t *pmd,
				      struct vm_area_struct *vma)
{
	VM_BUG_ON(!rwsem_is_locked(&vma->vm_mm->mmap_sem));
	if (pmd_trans_huge(*pmd))
		return __pmd_trans_huge_lock(pmd, vma);
	else
		return 0;
}
static inline void vma_adjust_trans_huge(struct vm_area_struct *vma,
					 unsigned long start,
					 unsigned long end,
					 long adjust_next)
{
	if (!vma->anon_vma || vma->vm_ops)
		return;
	__vma_adjust_trans_huge(vma, start, end, adjust_next);
}
static inline int hpage_nr_pages(struct page *page)
{
	if (unlikely(PageTransHuge(page)))
		return HPAGE_PMD_NR;
	return 1;
}

extern int do_huge_pmd_numa_page(struct mm_struct *mm, struct vm_area_struct *vma,
				unsigned long addr, pmd_t pmd, pmd_t *pmdp);

#else /* CONFIG_TRANSPARENT_HUGEPAGE */
#define HPAGE_PMD_SHIFT ({ BUILD_BUG(); 0; })
#define HPAGE_PMD_MASK ({ BUILD_BUG(); 0; })
#define HPAGE_PMD_SIZE ({ BUILD_BUG(); 0; })

#define hpage_nr_pages(x) 1

#define transparent_hugepage_enabled(__vma) 0

#define transparent_hugepage_flags 0UL
static inline int
split_huge_page_to_list(struct page *page, struct list_head *list)
{
	return 0;
}
static inline int split_huge_page(struct page *page)
{
	return 0;
}
#define split_huge_page_pmd(__vma, __address, __pmd)	\
	do { } while (0)
#define wait_split_huge_page(__anon_vma, __pmd)	\
	do { } while (0)
#define split_huge_page_pmd_mm(__mm, __address, __pmd)	\
	do { } while (0)
static inline int hugepage_madvise(struct vm_area_struct *vma,
				   unsigned long *vm_flags, int advice)
{
	BUG();
	return 0;
}
static inline void vma_adjust_trans_huge(struct vm_area_struct *vma,
					 unsigned long start,
					 unsigned long end,
					 long adjust_next)
{
}
static inline int pmd_trans_huge_lock(pmd_t *pmd,
				      struct vm_area_struct *vma)
{
	return 0;
}

static inline int do_huge_pmd_numa_page(struct mm_struct *mm, struct vm_area_struct *vma,
					unsigned long addr, pmd_t pmd, pmd_t *pmdp)
{
	return 0;
}

#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#endif /* _LINUX_HUGE_MM_H */
