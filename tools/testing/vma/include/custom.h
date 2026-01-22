/* SPDX-License-Identifier: GPL-2.0+ */

#pragma once

/*
 * Contains declarations that exist in the kernel which have been CUSTOMISED for
 * testing purposes to faciliate userland VMA testing.
 */

#ifdef CONFIG_MMU
extern unsigned long mmap_min_addr;
extern unsigned long dac_mmap_min_addr;
#else
#define mmap_min_addr		0UL
#define dac_mmap_min_addr	0UL
#endif

#define VM_WARN_ON(_expr) (WARN_ON(_expr))
#define VM_WARN_ON_ONCE(_expr) (WARN_ON_ONCE(_expr))
#define VM_WARN_ON_VMG(_expr, _vmg) (WARN_ON(_expr))
#define VM_BUG_ON(_expr) (BUG_ON(_expr))
#define VM_BUG_ON_VMA(_expr, _vma) (BUG_ON(_expr))

/* We hardcode this for now. */
#define sysctl_max_map_count 0x1000000UL

#define TASK_SIZE ((1ul << 47)-PAGE_SIZE)

/*
 * The shared stubs do not implement this, it amounts to an fprintf(STDERR,...)
 * either way :)
 */
#define pr_warn_once pr_err

#define pgtable_supports_soft_dirty() 1

struct anon_vma {
	struct anon_vma *root;
	struct rb_root_cached rb_root;

	/* Test fields. */
	bool was_cloned;
	bool was_unlinked;
};

static inline void unlink_anon_vmas(struct vm_area_struct *vma)
{
	/* For testing purposes, indicate that the anon_vma was unlinked. */
	vma->anon_vma->was_unlinked = true;
}

static inline void vma_start_write(struct vm_area_struct *vma)
{
	/* Used to indicate to tests that a write operation has begun. */
	vma->vm_lock_seq++;
}

static inline __must_check
int vma_start_write_killable(struct vm_area_struct *vma)
{
	/* Used to indicate to tests that a write operation has begun. */
	vma->vm_lock_seq++;
	return 0;
}

static inline int anon_vma_clone(struct vm_area_struct *dst, struct vm_area_struct *src,
				 enum vma_operation operation)
{
	/* For testing purposes. We indicate that an anon_vma has been cloned. */
	if (src->anon_vma != NULL) {
		dst->anon_vma = src->anon_vma;
		dst->anon_vma->was_cloned = true;
	}

	return 0;
}

static inline int __anon_vma_prepare(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma = calloc(1, sizeof(struct anon_vma));

	if (!anon_vma)
		return -ENOMEM;

	anon_vma->root = anon_vma;
	vma->anon_vma = anon_vma;

	return 0;
}

static inline int anon_vma_prepare(struct vm_area_struct *vma)
{
	if (likely(vma->anon_vma))
		return 0;

	return __anon_vma_prepare(vma);
}

static inline void vma_lock_init(struct vm_area_struct *vma, bool reset_refcnt)
{
	if (reset_refcnt)
		refcount_set(&vma->vm_refcnt, 0);
}

static inline vma_flags_t __mk_vma_flags(size_t count, const vma_flag_t *bits)
{
	vma_flags_t flags;
	int i;

	/*
	 * For testing purposes: allow invalid bit specification so we can
	 * easily test.
	 */
	vma_flags_clear_all(&flags);
	for (i = 0; i < count; i++)
		if (bits[i] < NUM_VMA_FLAG_BITS)
			vma_flag_set(&flags, bits[i]);
	return flags;
}
