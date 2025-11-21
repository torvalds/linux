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

	bool skip_vma_uprobe :1;
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
	struct vm_area_struct *vma;     /* The first vma to munmap */
	struct vm_area_struct *prev;    /* vma before the munmap area */
	struct vm_area_struct *next;    /* vma after the munmap area */
	struct list_head *uf;           /* Userfaultfd list_head */
	unsigned long start;            /* Aligned start addr (inclusive) */
	unsigned long end;              /* Aligned end addr (exclusive) */
	unsigned long unmap_start;      /* Unmap PTE start */
	unsigned long unmap_end;        /* Unmap PTE end */
	int vma_count;                  /* Number of vmas that will be removed */
	bool unlock;                    /* Unlock after the munmap */
	bool clear_ptes;                /* If there are outstanding PTE to be cleared */
	/* 2 byte hole */
	unsigned long nr_pages;         /* Number of pages being removed */
	unsigned long locked_vm;        /* Number of locked pages */
	unsigned long nr_accounted;     /* Number of VM_ACCOUNT pages */
	unsigned long exec_vm;
	unsigned long stack_vm;
	unsigned long data_vm;
};

enum vma_merge_state {
	VMA_MERGE_START,
	VMA_MERGE_ERROR_NOMEM,
	VMA_MERGE_NOMERGE,
	VMA_MERGE_SUCCESS,
};

/*
 * Describes a VMA merge operation and is threaded throughout it.
 *
 * Any of the fields may be mutated by the merge operation, so no guarantees are
 * made to the contents of this structure after a merge operation has completed.
 */
struct vma_merge_struct {
	struct mm_struct *mm;
	struct vma_iterator *vmi;
	/*
	 * Adjacent VMAs, any of which may be NULL if not present:
	 *
	 * |------|--------|------|
	 * | prev | middle | next |
	 * |------|--------|------|
	 *
	 * middle may not yet exist in the case of a proposed new VMA being
	 * merged, or it may be an existing VMA.
	 *
	 * next may be assigned by the caller.
	 */
	struct vm_area_struct *prev;
	struct vm_area_struct *middle;
	struct vm_area_struct *next;
	/* This is the VMA we ultimately target to become the merged VMA. */
	struct vm_area_struct *target;
	/*
	 * Initially, the start, end, pgoff fields are provided by the caller
	 * and describe the proposed new VMA range, whether modifying an
	 * existing VMA (which will be 'middle'), or adding a new one.
	 *
	 * During the merge process these fields are updated to describe the new
	 * range _including those VMAs which will be merged_.
	 */
	unsigned long start;
	unsigned long end;
	pgoff_t pgoff;

	vm_flags_t vm_flags;
	struct file *file;
	struct anon_vma *anon_vma;
	struct mempolicy *policy;
	struct vm_userfaultfd_ctx uffd_ctx;
	struct anon_vma_name *anon_name;
	enum vma_merge_state state;

	/* Flags which callers can use to modify merge behaviour: */

	/*
	 * If we can expand, simply do so. We know there is nothing to merge to
	 * the right. Does not reset state upon failure to merge. The VMA
	 * iterator is assumed to be positioned at the previous VMA, rather than
	 * at the gap.
	 */
	bool just_expand :1;

	/*
	 * If a merge is possible, but an OOM error occurs, give up and don't
	 * execute the merge, returning NULL.
	 */
	bool give_up_on_oom :1;

	/*
	 * If set, skip uprobe_mmap upon merged vma.
	 */
	bool skip_vma_uprobe :1;

	/* Internal flags set during merge process: */

	/*
	 * Internal flag indicating the merge increases vmg->middle->vm_start
	 * (and thereby, vmg->prev->vm_end).
	 */
	bool __adjust_middle_start :1;
	/*
	 * Internal flag indicating the merge decreases vmg->next->vm_start
	 * (and thereby, vmg->middle->vm_end).
	 */
	bool __adjust_next_start :1;
	/*
	 * Internal flag used during the merge operation to indicate we will
	 * remove vmg->middle.
	 */
	bool __remove_middle :1;
	/*
	 * Internal flag used during the merge operation to indicate we will
	 * remove vmg->next.
	 */
	bool __remove_next :1;

};

static inline bool vmg_nomem(struct vma_merge_struct *vmg)
{
	return vmg->state == VMA_MERGE_ERROR_NOMEM;
}

/* Assumes addr >= vma->vm_start. */
static inline pgoff_t vma_pgoff_offset(struct vm_area_struct *vma,
				       unsigned long addr)
{
	return vma->vm_pgoff + PHYS_PFN(addr - vma->vm_start);
}

#define VMG_STATE(name, mm_, vmi_, start_, end_, vm_flags_, pgoff_)	\
	struct vma_merge_struct name = {				\
		.mm = mm_,						\
		.vmi = vmi_,						\
		.start = start_,					\
		.end = end_,						\
		.vm_flags = vm_flags_,					\
		.pgoff = pgoff_,					\
		.state = VMA_MERGE_START,				\
	}

#define VMG_VMA_STATE(name, vmi_, prev_, vma_, start_, end_)	\
	struct vma_merge_struct name = {			\
		.mm = vma_->vm_mm,				\
		.vmi = vmi_,					\
		.prev = prev_,					\
		.middle = vma_,					\
		.next = NULL,					\
		.start = start_,				\
		.end = end_,					\
		.vm_flags = vma_->vm_flags,			\
		.pgoff = vma_pgoff_offset(vma_, start_),	\
		.file = vma_->vm_file,				\
		.anon_vma = vma_->anon_vma,			\
		.policy = vma_policy(vma_),			\
		.uffd_ctx = vma_->vm_userfaultfd_ctx,		\
		.anon_name = anon_vma_name(vma_),		\
		.state = VMA_MERGE_START,			\
	}

#ifdef CONFIG_DEBUG_VM_MAPLE_TREE
void validate_mm(struct mm_struct *mm);
#else
#define validate_mm(mm) do { } while (0)
#endif

__must_check int vma_expand(struct vma_merge_struct *vmg);
__must_check int vma_shrink(struct vma_iterator *vmi,
		struct vm_area_struct *vma,
		unsigned long start, unsigned long end, pgoff_t pgoff);

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

	vma_mark_attached(vma);
	return 0;
}

/*
 * Temporary helper function for stacked mmap handlers which specify
 * f_op->mmap() but which might have an underlying file system which implements
 * f_op->mmap_prepare().
 */
static inline void set_vma_from_desc(struct vm_area_struct *vma,
		struct vm_area_desc *desc)
{
	/*
	 * Since we're invoking .mmap_prepare() despite having a partially
	 * established VMA, we must take care to handle setting fields
	 * correctly.
	 */

	/* Mutable fields. Populated with initial state. */
	vma->vm_pgoff = desc->pgoff;
	if (desc->vm_file != vma->vm_file)
		vma_set_file(vma, desc->vm_file);
	if (desc->vm_flags != vma->vm_flags)
		vm_flags_set(vma, desc->vm_flags);
	vma->vm_page_prot = desc->page_prot;

	/* User-defined fields. */
	vma->vm_ops = desc->vm_ops;
	vma->vm_private_data = desc->private_data;
}

int
do_vmi_align_munmap(struct vma_iterator *vmi, struct vm_area_struct *vma,
		    struct mm_struct *mm, unsigned long start,
		    unsigned long end, struct list_head *uf, bool unlock);

int do_vmi_munmap(struct vma_iterator *vmi, struct mm_struct *mm,
		  unsigned long start, size_t len, struct list_head *uf,
		  bool unlock);

void remove_vma(struct vm_area_struct *vma);

void unmap_region(struct ma_state *mas, struct vm_area_struct *vma,
		struct vm_area_struct *prev, struct vm_area_struct *next);

/* We are about to modify the VMA's flags. */
__must_check struct vm_area_struct
*vma_modify_flags(struct vma_iterator *vmi,
		struct vm_area_struct *prev, struct vm_area_struct *vma,
		unsigned long start, unsigned long end,
		vm_flags_t vm_flags);

/* We are about to modify the VMA's anon_name. */
__must_check struct vm_area_struct
*vma_modify_name(struct vma_iterator *vmi,
		 struct vm_area_struct *prev,
		 struct vm_area_struct *vma,
		 unsigned long start,
		 unsigned long end,
		 struct anon_vma_name *new_name);

/* We are about to modify the VMA's memory policy. */
__must_check struct vm_area_struct
*vma_modify_policy(struct vma_iterator *vmi,
		   struct vm_area_struct *prev,
		   struct vm_area_struct *vma,
		   unsigned long start, unsigned long end,
		   struct mempolicy *new_pol);

/* We are about to modify the VMA's flags and/or uffd context. */
__must_check struct vm_area_struct
*vma_modify_flags_uffd(struct vma_iterator *vmi,
		       struct vm_area_struct *prev,
		       struct vm_area_struct *vma,
		       unsigned long start, unsigned long end,
		       vm_flags_t vm_flags,
		       struct vm_userfaultfd_ctx new_ctx,
		       bool give_up_on_oom);

__must_check struct vm_area_struct
*vma_merge_new_range(struct vma_merge_struct *vmg);

__must_check struct vm_area_struct
*vma_merge_extend(struct vma_iterator *vmi,
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

unsigned long mmap_region(struct file *file, unsigned long addr,
		unsigned long len, vm_flags_t vm_flags, unsigned long pgoff,
		struct list_head *uf);

int do_brk_flags(struct vma_iterator *vmi, struct vm_area_struct *brkvma,
		 unsigned long addr, unsigned long request, unsigned long flags);

unsigned long unmapped_area(struct vm_unmapped_area_info *info);
unsigned long unmapped_area_topdown(struct vm_unmapped_area_info *info);

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
static inline pgprot_t vm_pgprot_modify(pgprot_t oldprot, vm_flags_t vm_flags)
{
	return pgprot_modify(oldprot, vm_get_page_prot(vm_flags));
}
#endif

static inline struct vm_area_struct *vma_prev_limit(struct vma_iterator *vmi,
						    unsigned long min)
{
	return mas_prev(&vmi->mas, min);
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
static inline void vma_iter_store_overwrite(struct vma_iterator *vmi,
					    struct vm_area_struct *vma)
{
	vma_assert_attached(vma);

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

static inline void vma_iter_store_new(struct vma_iterator *vmi,
				      struct vm_area_struct *vma)
{
	vma_mark_attached(vma);
	vma_iter_store_overwrite(vmi, vma);
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

/*
 * Retrieve the next VMA and rewind the iterator to end of the previous VMA, or
 * if no previous VMA, to index 0.
 */
static inline
struct vm_area_struct *vma_iter_next_rewind(struct vma_iterator *vmi,
		struct vm_area_struct **pprev)
{
	struct vm_area_struct *next = vma_next(vmi);
	struct vm_area_struct *prev = vma_prev(vmi);

	/*
	 * Consider the case where no previous VMA exists. We advance to the
	 * next VMA, skipping any gap, then rewind to the start of the range.
	 *
	 * If we were to unconditionally advance to the next range we'd wind up
	 * at the next VMA again, so we check to ensure there is a previous VMA
	 * to skip over.
	 */
	if (prev)
		vma_iter_next_range(vmi);

	if (pprev)
		*pprev = prev;

	return next;
}

#ifdef CONFIG_64BIT
static inline bool vma_is_sealed(struct vm_area_struct *vma)
{
	return (vma->vm_flags & VM_SEALED);
}
#else
static inline bool vma_is_sealed(struct vm_area_struct *vma)
{
	return false;
}
#endif

#if defined(CONFIG_STACK_GROWSUP)
int expand_upwards(struct vm_area_struct *vma, unsigned long address);
#endif

int expand_downwards(struct vm_area_struct *vma, unsigned long address);

int __vm_munmap(unsigned long start, size_t len, bool unlock);

int insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vma);

/* vma_init.h, shared between CONFIG_MMU and nommu. */
void __init vma_state_init(void);
struct vm_area_struct *vm_area_alloc(struct mm_struct *mm);
struct vm_area_struct *vm_area_dup(struct vm_area_struct *orig);
void vm_area_free(struct vm_area_struct *vma);

/* vma_exec.c */
#ifdef CONFIG_MMU
int create_init_stack_vma(struct mm_struct *mm, struct vm_area_struct **vmap,
			  unsigned long *top_mem_p);
int relocate_vma_down(struct vm_area_struct *vma, unsigned long shift);
#endif

#endif	/* __MM_VMA_H */
