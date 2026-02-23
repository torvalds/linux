/* SPDX-License-Identifier: GPL-2.0+ */

#pragma once

/*
 * Contains declarations that are STUBBED, that is that are rendered no-ops, in
 * order to faciliate userland VMA testing.
 */

/* Forward declarations. */
struct mm_struct;
struct vm_area_struct;
struct vm_area_desc;
struct pagetable_move_control;
struct mmap_action;
struct file;
struct anon_vma;
struct anon_vma_chain;
struct address_space;
struct unmap_desc;

#define __bitwise
#define __randomize_layout

#define FIRST_USER_ADDRESS	0UL
#define USER_PGTABLES_CEILING	0UL

#define vma_policy(vma) NULL

#define down_write_nest_lock(sem, nest_lock)

#define data_race(expr) expr

#define ASSERT_EXCLUSIVE_WRITER(x)

struct vm_userfaultfd_ctx {};
struct mempolicy {};
struct mmu_gather {};
struct mutex {};
struct vm_fault {};

static inline void userfaultfd_unmap_complete(struct mm_struct *mm,
					      struct list_head *uf)
{
}

static inline unsigned long move_page_tables(struct pagetable_move_control *pmc)
{
	return 0;
}

static inline void free_pgd_range(struct mmu_gather *tlb,
			unsigned long addr, unsigned long end,
			unsigned long floor, unsigned long ceiling)
{
}

static inline int ksm_execve(struct mm_struct *mm)
{
	return 0;
}

static inline void ksm_exit(struct mm_struct *mm)
{
}

static inline void vma_numab_state_init(struct vm_area_struct *vma)
{
}

static inline void vma_numab_state_free(struct vm_area_struct *vma)
{
}

static inline void dup_anon_vma_name(struct vm_area_struct *orig_vma,
				     struct vm_area_struct *new_vma)
{
}

static inline void free_anon_vma_name(struct vm_area_struct *vma)
{
}

static inline void mmap_action_prepare(struct mmap_action *action,
					   struct vm_area_desc *desc)
{
}

static inline int mmap_action_complete(struct mmap_action *action,
					   struct vm_area_struct *vma)
{
	return 0;
}

static inline void fixup_hugetlb_reservations(struct vm_area_struct *vma)
{
}

static inline bool shmem_file(struct file *file)
{
	return false;
}

static inline vm_flags_t ksm_vma_flags(const struct mm_struct *mm,
		const struct file *file, vm_flags_t vm_flags)
{
	return vm_flags;
}

static inline void remap_pfn_range_prepare(struct vm_area_desc *desc, unsigned long pfn)
{
}

static inline int remap_pfn_range_complete(struct vm_area_struct *vma, unsigned long addr,
		unsigned long pfn, unsigned long size, pgprot_t pgprot)
{
	return 0;
}

static inline int do_munmap(struct mm_struct *, unsigned long, size_t,
		struct list_head *uf)
{
	return 0;
}

/* Currently stubbed but we may later wish to un-stub. */
static inline void vm_acct_memory(long pages);

static inline void mmap_assert_locked(struct mm_struct *mm)
{
}


static inline void anon_vma_unlock_write(struct anon_vma *anon_vma)
{
}

static inline void i_mmap_unlock_write(struct address_space *mapping)
{
}

static inline int userfaultfd_unmap_prep(struct vm_area_struct *vma,
					 unsigned long start,
					 unsigned long end,
					 struct list_head *unmaps)
{
	return 0;
}

static inline void mmap_write_downgrade(struct mm_struct *mm)
{
}

static inline void mmap_read_unlock(struct mm_struct *mm)
{
}

static inline void mmap_write_unlock(struct mm_struct *mm)
{
}

static inline int mmap_write_lock_killable(struct mm_struct *mm)
{
	return 0;
}

static inline bool can_modify_mm(struct mm_struct *mm,
				 unsigned long start,
				 unsigned long end)
{
	return true;
}

static inline void arch_unmap(struct mm_struct *mm,
				 unsigned long start,
				 unsigned long end)
{
}

static inline bool mpol_equal(struct mempolicy *a, struct mempolicy *b)
{
	return true;
}

static inline void khugepaged_enter_vma(struct vm_area_struct *vma,
			  vm_flags_t vm_flags)
{
}

static inline bool mapping_can_writeback(struct address_space *mapping)
{
	return true;
}

static inline bool is_vm_hugetlb_page(struct vm_area_struct *vma)
{
	return false;
}

static inline bool vma_soft_dirty_enabled(struct vm_area_struct *vma)
{
	return false;
}

static inline bool userfaultfd_wp(struct vm_area_struct *vma)
{
	return false;
}

static inline void mmap_assert_write_locked(struct mm_struct *mm)
{
}

static inline void mutex_lock(struct mutex *lock)
{
}

static inline void mutex_unlock(struct mutex *lock)
{
}

static inline bool mutex_is_locked(struct mutex *lock)
{
	return true;
}

static inline bool signal_pending(void *p)
{
	return false;
}

static inline bool is_file_hugepages(struct file *file)
{
	return false;
}

static inline int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	return 0;
}

static inline bool may_expand_vm(struct mm_struct *mm, vm_flags_t flags,
				 unsigned long npages)
{
	return true;
}

static inline int shmem_zero_setup(struct vm_area_struct *vma)
{
	return 0;
}


static inline void vm_acct_memory(long pages)
{
}

static inline void vma_interval_tree_insert(struct vm_area_struct *vma,
					    struct rb_root_cached *rb)
{
}

static inline void vma_interval_tree_remove(struct vm_area_struct *vma,
					    struct rb_root_cached *rb)
{
}

static inline void flush_dcache_mmap_unlock(struct address_space *mapping)
{
}

static inline void anon_vma_interval_tree_insert(struct anon_vma_chain *avc,
						 struct rb_root_cached *rb)
{
}

static inline void anon_vma_interval_tree_remove(struct anon_vma_chain *avc,
						 struct rb_root_cached *rb)
{
}

static inline void uprobe_mmap(struct vm_area_struct *vma)
{
}

static inline void uprobe_munmap(struct vm_area_struct *vma,
				 unsigned long start, unsigned long end)
{
}

static inline void i_mmap_lock_write(struct address_space *mapping)
{
}

static inline void anon_vma_lock_write(struct anon_vma *anon_vma)
{
}

static inline void vma_assert_write_locked(struct vm_area_struct *vma)
{
}

static inline void ksm_add_vma(struct vm_area_struct *vma)
{
}

static inline void perf_event_mmap(struct vm_area_struct *vma)
{
}

static inline bool vma_is_dax(struct vm_area_struct *vma)
{
	return false;
}

static inline struct vm_area_struct *get_gate_vma(struct mm_struct *mm)
{
	return NULL;
}

static inline bool arch_validate_flags(vm_flags_t flags)
{
	return true;
}

static inline void vma_close(struct vm_area_struct *vma)
{
}

static inline int mmap_file(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

static inline int is_hugepage_only_range(struct mm_struct *mm,
					unsigned long addr, unsigned long len)
{
	return 0;
}

static inline bool capable(int cap)
{
	return true;
}

static inline struct anon_vma_name *anon_vma_name(struct vm_area_struct *vma)
{
	return NULL;
}

static inline bool is_mergeable_vm_userfaultfd_ctx(struct vm_area_struct *vma,
					struct vm_userfaultfd_ctx vm_ctx)
{
	return true;
}

static inline bool anon_vma_name_eq(struct anon_vma_name *anon_name1,
				    struct anon_vma_name *anon_name2)
{
	return true;
}

static inline void might_sleep(void)
{
}

static inline void fput(struct file *file)
{
}

static inline void mpol_put(struct mempolicy *pol)
{
}

static inline void lru_add_drain(void)
{
}

static inline void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm)
{
}

static inline void update_hiwater_rss(struct mm_struct *mm)
{
}

static inline void update_hiwater_vm(struct mm_struct *mm)
{
}

static inline void unmap_vmas(struct mmu_gather *tlb, struct unmap_desc *unmap)
{
}

static inline void free_pgtables(struct mmu_gather *tlb, struct unmap_desc *unmap)
{
}

static inline void mapping_unmap_writable(struct address_space *mapping)
{
}

static inline void flush_dcache_mmap_lock(struct address_space *mapping)
{
}

static inline void tlb_finish_mmu(struct mmu_gather *tlb)
{
}

static inline struct file *get_file(struct file *f)
{
	return f;
}

static inline int vma_dup_policy(struct vm_area_struct *src, struct vm_area_struct *dst)
{
	return 0;
}

static inline void vma_adjust_trans_huge(struct vm_area_struct *vma,
					 unsigned long start,
					 unsigned long end,
					 struct vm_area_struct *next)
{
}

static inline void hugetlb_split(struct vm_area_struct *, unsigned long) {}
