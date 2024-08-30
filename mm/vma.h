/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * vma.h
 *
 * Core VMA manipulation API implemented in vma.c.
 */
#ifndef __MM_VMA_H
#define __MM_VMA_H

/*
 * VMA lock generalization
 */
struct vma_prepare {
	struct vm_area_struct *vma;
	struct vm_area_struct *adj_next;
	struct file *file;
	struct address_space *mapping;
	struct anon_vma *anon_vma;
	struct vm_area_struct *insert;
	struct vm_area_struct *remove;
	struct vm_area_struct *remove2;
};

struct unlink_vma_file_batch {
	int count;
	struct vm_area_struct *vmas[8];
};

/*
 * vma munmap operation
 */
struct vma_munmap_struct {
	struct vma_iterator *vmi;
	struct mm_struct *mm;
	struct vm_area_struct *vma;     /* The first vma to munmap */
	struct vm_area_struct *prev;    /* vma before the munmap area */
	struct vm_area_struct *next;    /* vma after the munmap area */
	struct list_head *uf;           /* Userfaultfd list_head */
	unsigned long start;            /* Aligned start addr (inclusive) */
	unsigned long end;              /* Aligned end addr (exclusive) */
	int vma_count;                  /* Number of vmas that will be removed */
	unsigned long nr_pages;         /* Number of pages being removed */
	unsigned long locked_vm;        /* Number of locked pages */
	unsigned long nr_accounted;     /* Number of VM_ACCOUNT pages */
	unsigned long exec_vm;
	unsigned long stack_vm;
	unsigned long data_vm;
	bool unlock;                    /* Unlock after the munmap */
};

#ifdef CONFIG_DEBUG_VM_MAPLE_TREE
void validate_mm(struct mm_struct *mm);
#else
#define validate_mm(mm) do { } while (0)
#endif

/* Required for expand_downwards(). */
void anon_vma_interval_tree_pre_update_vma(struct vm_area_struct *vma);

/* Required for expand_downwards(). */
void anon_vma_interval_tree_post_update_vma(struct vm_area_struct *vma);

/* Required for do_brk_flags(). */
void vma_prepare(struct vma_prepare *vp);

/* Required for do_brk_flags(). */
void init_vma_prep(struct vma_prepare *vp,
		   struct vm_area_struct *vma);

/* Required for do_brk_flags(). */
void vma_complete(struct vma_prepare *vp,
		  struct vma_iterator *vmi, struct mm_struct *mm);

int vma_expand(struct vma_iterator *vmi, struct vm_area_struct *vma,
	       unsigned long start, unsigned long end, pgoff_t pgoff,
	       struct vm_area_struct *next);

int vma_shrink(struct vma_iterator *vmi, struct vm_area_struct *vma,
	       unsigned long start, unsigned long end, pgoff_t pgoff);

int
do_vmi_align_munmap(struct vma_iterator *vmi, struct vm_area_struct *vma,
		    struct mm_struct *mm, unsigned long start,
		    unsigned long end, struct list_head *uf, bool unlock);

int do_vmi_munmap(struct vma_iterator *vmi, struct mm_struct *mm,
		  unsigned long start, size_t len, struct list_head *uf,
		  bool unlock);

void remove_vma(struct vm_area_struct *vma, bool unreachable);

void unmap_region(struct mm_struct *mm, struct ma_state *mas,
		struct vm_area_struct *vma, struct vm_area_struct *prev,
		struct vm_area_struct *next, unsigned long start,
		unsigned long end, unsigned long tree_end, bool mm_wr_locked);

/* Required by mmap_region(). */
bool
can_vma_merge_before(struct vm_area_struct *vma, unsigned long vm_flags,
		struct anon_vma *anon_vma, struct file *file,
		pgoff_t vm_pgoff, struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
		struct anon_vma_name *anon_name);

/* Required by mmap_region() and do_brk_flags(). */
bool
can_vma_merge_after(struct vm_area_struct *vma, unsigned long vm_flags,
		struct anon_vma *anon_vma, struct file *file,
		pgoff_t vm_pgoff, struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
		struct anon_vma_name *anon_name);

struct vm_area_struct *vma_modify(struct vma_iterator *vmi,
				  struct vm_area_struct *prev,
				  struct vm_area_struct *vma,
				  unsigned long start, unsigned long end,
				  unsigned long vm_flags,
				  struct mempolicy *policy,
				  struct vm_userfaultfd_ctx uffd_ctx,
				  struct anon_vma_name *anon_name);

/* We are about to modify the VMA's flags. */
static inline struct vm_area_struct
*vma_modify_flags(struct vma_iterator *vmi,
		  struct vm_area_struct *prev,
		  struct vm_area_struct *vma,
		  unsigned long start, unsigned long end,
		  unsigned long new_flags)
{
	return vma_modify(vmi, prev, vma, start, end, new_flags,
			  vma_policy(vma), vma->vm_userfaultfd_ctx,
			  anon_vma_name(vma));
}

/* We are about to modify the VMA's flags and/or anon_name. */
static inline struct vm_area_struct
*vma_modify_flags_name(struct vma_iterator *vmi,
		       struct vm_area_struct *prev,
		       struct vm_area_struct *vma,
		       unsigned long start,
		       unsigned long end,
		       unsigned long new_flags,
		       struct anon_vma_name *new_name)
{
	return vma_modify(vmi, prev, vma, start, end, new_flags,
			  vma_policy(vma), vma->vm_userfaultfd_ctx, new_name);
}

/* We are about to modify the VMA's memory policy. */
static inline struct vm_area_struct
*vma_modify_policy(struct vma_iterator *vmi,
		   struct vm_area_struct *prev,
		   struct vm_area_struct *vma,
		   unsigned long start, unsigned long end,
		   struct mempolicy *new_pol)
{
	return vma_modify(vmi, prev, vma, start, end, vma->vm_flags,
			  new_pol, vma->vm_userfaultfd_ctx, anon_vma_name(vma));
}

/* We are about to modify the VMA's flags and/or uffd context. */
static inline struct vm_area_struct
*vma_modify_flags_uffd(struct vma_iterator *vmi,
		       struct vm_area_struct *prev,
		       struct vm_area_struct *vma,
		       unsigned long start, unsigned long end,
		       unsigned long new_flags,
		       struct vm_userfaultfd_ctx new_ctx)
{
	return vma_modify(vmi, prev, vma, start, end, new_flags,
			  vma_policy(vma), new_ctx, anon_vma_name(vma));
}

struct vm_area_struct
*vma_merge_new_vma(struct vma_iterator *vmi, struct vm_area_struct *prev,
		   struct vm_area_struct *vma, unsigned long start,
		   unsigned long end, pgoff_t pgoff);

struct vm_area_struct *vma_merge_extend(struct vma_iterator *vmi,
					struct vm_area_struct *vma,
					unsigned long delta);

void unlink_file_vma_batch_init(struct unlink_vma_file_batch *vb);

void unlink_file_vma_batch_final(struct unlink_vma_file_batch *vb);

void unlink_file_vma_batch_add(struct unlink_vma_file_batch *vb,
			       struct vm_area_struct *vma);

void unlink_file_vma(struct vm_area_struct *vma);

void vma_link_file(struct vm_area_struct *vma);

int vma_link(struct mm_struct *mm, struct vm_area_struct *vma);

struct vm_area_struct *copy_vma(struct vm_area_struct **vmap,
	unsigned long addr, unsigned long len, pgoff_t pgoff,
	bool *need_rmap_locks);

struct anon_vma *find_mergeable_anon_vma(struct vm_area_struct *vma);

bool vma_needs_dirty_tracking(struct vm_area_struct *vma);
bool vma_wants_writenotify(struct vm_area_struct *vma, pgprot_t vm_page_prot);

int mm_take_all_locks(struct mm_struct *mm);
void mm_drop_all_locks(struct mm_struct *mm);
unsigned long count_vma_pages_range(struct mm_struct *mm,
				    unsigned long addr, unsigned long end);

static inline bool vma_wants_manual_pte_write_upgrade(struct vm_area_struct *vma)
{
	/*
	 * We want to check manually if we can change individual PTEs writable
	 * if we can't do that automatically for all PTEs in a mapping. For
	 * private mappings, that's always the case when we have write
	 * permissions as we properly have to handle COW.
	 */
	if (vma->vm_flags & VM_SHARED)
		return vma_wants_writenotify(vma, vma->vm_page_prot);
	return !!(vma->vm_flags & VM_WRITE);
}

#ifdef CONFIG_MMU
static inline pgprot_t vm_pgprot_modify(pgprot_t oldprot, unsigned long vm_flags)
{
	return pgprot_modify(oldprot, vm_get_page_prot(vm_flags));
}
#endif

static inline struct vm_area_struct *vma_prev_limit(struct vma_iterator *vmi,
						    unsigned long min)
{
	return mas_prev(&vmi->mas, min);
}

static inline int vma_iter_store_gfp(struct vma_iterator *vmi,
			struct vm_area_struct *vma, gfp_t gfp)
{
	if (vmi->mas.status != ma_start &&
	    ((vmi->mas.index > vma->vm_start) || (vmi->mas.last < vma->vm_start)))
		vma_iter_invalidate(vmi);

	__mas_set_range(&vmi->mas, vma->vm_start, vma->vm_end - 1);
	mas_store_gfp(&vmi->mas, vma, gfp);
	if (unlikely(mas_is_err(&vmi->mas)))
		return -ENOMEM;

	return 0;
}


/*
 * These three helpers classifies VMAs for virtual memory accounting.
 */

/*
 * Executable code area - executable, not writable, not stack
 */
static inline bool is_exec_mapping(vm_flags_t flags)
{
	return (flags & (VM_EXEC | VM_WRITE | VM_STACK)) == VM_EXEC;
}

/*
 * Stack area (including shadow stacks)
 *
 * VM_GROWSUP / VM_GROWSDOWN VMAs are always private anonymous:
 * do_mmap() forbids all other combinations.
 */
static inline bool is_stack_mapping(vm_flags_t flags)
{
	return ((flags & VM_STACK) == VM_STACK) || (flags & VM_SHADOW_STACK);
}

/*
 * Data area - private, writable, not stack
 */
static inline bool is_data_mapping(vm_flags_t flags)
{
	return (flags & (VM_WRITE | VM_SHARED | VM_STACK)) == VM_WRITE;
}


static inline void vma_iter_config(struct vma_iterator *vmi,
		unsigned long index, unsigned long last)
{
	__mas_set_range(&vmi->mas, index, last - 1);
}

static inline void vma_iter_reset(struct vma_iterator *vmi)
{
	mas_reset(&vmi->mas);
}

static inline
struct vm_area_struct *vma_iter_prev_range_limit(struct vma_iterator *vmi, unsigned long min)
{
	return mas_prev_range(&vmi->mas, min);
}

static inline
struct vm_area_struct *vma_iter_next_range_limit(struct vma_iterator *vmi, unsigned long max)
{
	return mas_next_range(&vmi->mas, max);
}

static inline int vma_iter_area_lowest(struct vma_iterator *vmi, unsigned long min,
				       unsigned long max, unsigned long size)
{
	return mas_empty_area(&vmi->mas, min, max - 1, size);
}

static inline int vma_iter_area_highest(struct vma_iterator *vmi, unsigned long min,
					unsigned long max, unsigned long size)
{
	return mas_empty_area_rev(&vmi->mas, min, max - 1, size);
}

/*
 * VMA Iterator functions shared between nommu and mmap
 */
static inline int vma_iter_prealloc(struct vma_iterator *vmi,
		struct vm_area_struct *vma)
{
	return mas_preallocate(&vmi->mas, vma, GFP_KERNEL);
}

static inline void vma_iter_clear(struct vma_iterator *vmi)
{
	mas_store_prealloc(&vmi->mas, NULL);
}

static inline struct vm_area_struct *vma_iter_load(struct vma_iterator *vmi)
{
	return mas_walk(&vmi->mas);
}

/* Store a VMA with preallocated memory */
static inline void vma_iter_store(struct vma_iterator *vmi,
				  struct vm_area_struct *vma)
{

#if defined(CONFIG_DEBUG_VM_MAPLE_TREE)
	if (MAS_WARN_ON(&vmi->mas, vmi->mas.status != ma_start &&
			vmi->mas.index > vma->vm_start)) {
		pr_warn("%lx > %lx\n store vma %lx-%lx\n into slot %lx-%lx\n",
			vmi->mas.index, vma->vm_start, vma->vm_start,
			vma->vm_end, vmi->mas.index, vmi->mas.last);
	}
	if (MAS_WARN_ON(&vmi->mas, vmi->mas.status != ma_start &&
			vmi->mas.last <  vma->vm_start)) {
		pr_warn("%lx < %lx\nstore vma %lx-%lx\ninto slot %lx-%lx\n",
		       vmi->mas.last, vma->vm_start, vma->vm_start, vma->vm_end,
		       vmi->mas.index, vmi->mas.last);
	}
#endif

	if (vmi->mas.status != ma_start &&
	    ((vmi->mas.index > vma->vm_start) || (vmi->mas.last < vma->vm_start)))
		vma_iter_invalidate(vmi);

	__mas_set_range(&vmi->mas, vma->vm_start, vma->vm_end - 1);
	mas_store_prealloc(&vmi->mas, vma);
}

static inline unsigned long vma_iter_addr(struct vma_iterator *vmi)
{
	return vmi->mas.index;
}

static inline unsigned long vma_iter_end(struct vma_iterator *vmi)
{
	return vmi->mas.last + 1;
}

static inline int vma_iter_bulk_alloc(struct vma_iterator *vmi,
				      unsigned long count)
{
	return mas_expected_entries(&vmi->mas, count);
}

static inline
struct vm_area_struct *vma_iter_prev_range(struct vma_iterator *vmi)
{
	return mas_prev_range(&vmi->mas, 0);
}

#ifdef CONFIG_64BIT

static inline bool vma_is_sealed(struct vm_area_struct *vma)
{
	return (vma->vm_flags & VM_SEALED);
}

/*
 * check if a vma is sealed for modification.
 * return true, if modification is allowed.
 */
static inline bool can_modify_vma(struct vm_area_struct *vma)
{
	if (unlikely(vma_is_sealed(vma)))
		return false;

	return true;
}

bool can_modify_vma_madv(struct vm_area_struct *vma, int behavior);

#else

static inline bool can_modify_vma(struct vm_area_struct *vma)
{
	return true;
}

static inline bool can_modify_vma_madv(struct vm_area_struct *vma, int behavior)
{
	return true;
}

#endif

#endif	/* __MM_VMA_H */
