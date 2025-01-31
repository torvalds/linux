// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "generated/bit-length.h"

#include "maple-shared.h"
#include "vma_internal.h"

/* Include so header guard set. */
#include "../../../mm/vma.h"

static bool fail_prealloc;

/* Then override vma_iter_prealloc() so we can choose to fail it. */
#define vma_iter_prealloc(vmi, vma)					\
	(fail_prealloc ? -ENOMEM : mas_preallocate(&(vmi)->mas, (vma), GFP_KERNEL))

#define CONFIG_DEFAULT_MMAP_MIN_ADDR 65536

unsigned long mmap_min_addr = CONFIG_DEFAULT_MMAP_MIN_ADDR;
unsigned long dac_mmap_min_addr = CONFIG_DEFAULT_MMAP_MIN_ADDR;
unsigned long stack_guard_gap = 256UL<<PAGE_SHIFT;

/*
 * Directly import the VMA implementation here. Our vma_internal.h wrapper
 * provides userland-equivalent functionality for everything vma.c uses.
 */
#include "../../../mm/vma.c"

const struct vm_operations_struct vma_dummy_vm_ops;
static struct anon_vma dummy_anon_vma;

#define ASSERT_TRUE(_expr)						\
	do {								\
		if (!(_expr)) {						\
			fprintf(stderr,					\
				"Assert FAILED at %s:%d:%s(): %s is FALSE.\n", \
				__FILE__, __LINE__, __FUNCTION__, #_expr); \
			return false;					\
		}							\
	} while (0)
#define ASSERT_FALSE(_expr) ASSERT_TRUE(!(_expr))
#define ASSERT_EQ(_val1, _val2) ASSERT_TRUE((_val1) == (_val2))
#define ASSERT_NE(_val1, _val2) ASSERT_TRUE((_val1) != (_val2))

static struct task_struct __current;

struct task_struct *get_current(void)
{
	return &__current;
}

unsigned long rlimit(unsigned int limit)
{
	return (unsigned long)-1;
}

/* Helper function to simply allocate a VMA. */
static struct vm_area_struct *alloc_vma(struct mm_struct *mm,
					unsigned long start,
					unsigned long end,
					pgoff_t pgoff,
					vm_flags_t flags)
{
	struct vm_area_struct *ret = vm_area_alloc(mm);

	if (ret == NULL)
		return NULL;

	ret->vm_start = start;
	ret->vm_end = end;
	ret->vm_pgoff = pgoff;
	ret->__vm_flags = flags;

	return ret;
}

/* Helper function to allocate a VMA and link it to the tree. */
static struct vm_area_struct *alloc_and_link_vma(struct mm_struct *mm,
						 unsigned long start,
						 unsigned long end,
						 pgoff_t pgoff,
						 vm_flags_t flags)
{
	struct vm_area_struct *vma = alloc_vma(mm, start, end, pgoff, flags);

	if (vma == NULL)
		return NULL;

	if (vma_link(mm, vma)) {
		vm_area_free(vma);
		return NULL;
	}

	/*
	 * Reset this counter which we use to track whether writes have
	 * begun. Linking to the tree will have caused this to be incremented,
	 * which means we will get a false positive otherwise.
	 */
	vma->vm_lock_seq = UINT_MAX;

	return vma;
}

/* Helper function which provides a wrapper around a merge new VMA operation. */
static struct vm_area_struct *merge_new(struct vma_merge_struct *vmg)
{
	/*
	 * For convenience, get prev and next VMAs. Which the new VMA operation
	 * requires.
	 */
	vmg->next = vma_next(vmg->vmi);
	vmg->prev = vma_prev(vmg->vmi);
	vma_iter_next_range(vmg->vmi);

	return vma_merge_new_range(vmg);
}

/*
 * Helper function which provides a wrapper around a merge existing VMA
 * operation.
 */
static struct vm_area_struct *merge_existing(struct vma_merge_struct *vmg)
{
	return vma_merge_existing_range(vmg);
}

/*
 * Helper function which provides a wrapper around the expansion of an existing
 * VMA.
 */
static int expand_existing(struct vma_merge_struct *vmg)
{
	return vma_expand(vmg);
}

/*
 * Helper function to reset merge state the associated VMA iterator to a
 * specified new range.
 */
static void vmg_set_range(struct vma_merge_struct *vmg, unsigned long start,
			  unsigned long end, pgoff_t pgoff, vm_flags_t flags)
{
	vma_iter_set(vmg->vmi, start);

	vmg->prev = NULL;
	vmg->middle = NULL;
	vmg->next = NULL;
	vmg->target = NULL;

	vmg->start = start;
	vmg->end = end;
	vmg->pgoff = pgoff;
	vmg->flags = flags;

	vmg->just_expand = false;
	vmg->__remove_middle = false;
	vmg->__remove_next = false;
}

/*
 * Helper function to try to merge a new VMA.
 *
 * Update vmg and the iterator for it and try to merge, otherwise allocate a new
 * VMA, link it to the maple tree and return it.
 */
static struct vm_area_struct *try_merge_new_vma(struct mm_struct *mm,
						struct vma_merge_struct *vmg,
						unsigned long start, unsigned long end,
						pgoff_t pgoff, vm_flags_t flags,
						bool *was_merged)
{
	struct vm_area_struct *merged;

	vmg_set_range(vmg, start, end, pgoff, flags);

	merged = merge_new(vmg);
	if (merged) {
		*was_merged = true;
		ASSERT_EQ(vmg->state, VMA_MERGE_SUCCESS);
		return merged;
	}

	*was_merged = false;

	ASSERT_EQ(vmg->state, VMA_MERGE_NOMERGE);

	return alloc_and_link_vma(mm, start, end, pgoff, flags);
}

/*
 * Helper function to reset the dummy anon_vma to indicate it has not been
 * duplicated.
 */
static void reset_dummy_anon_vma(void)
{
	dummy_anon_vma.was_cloned = false;
	dummy_anon_vma.was_unlinked = false;
}

/*
 * Helper function to remove all VMAs and destroy the maple tree associated with
 * a virtual address space. Returns a count of VMAs in the tree.
 */
static int cleanup_mm(struct mm_struct *mm, struct vma_iterator *vmi)
{
	struct vm_area_struct *vma;
	int count = 0;

	fail_prealloc = false;
	reset_dummy_anon_vma();

	vma_iter_set(vmi, 0);
	for_each_vma(*vmi, vma) {
		vm_area_free(vma);
		count++;
	}

	mtree_destroy(&mm->mm_mt);
	mm->map_count = 0;
	return count;
}

/* Helper function to determine if VMA has had vma_start_write() performed. */
static bool vma_write_started(struct vm_area_struct *vma)
{
	int seq = vma->vm_lock_seq;

	/* We reset after each check. */
	vma->vm_lock_seq = UINT_MAX;

	/* The vma_start_write() stub simply increments this value. */
	return seq > -1;
}

/* Helper function providing a dummy vm_ops->close() method.*/
static void dummy_close(struct vm_area_struct *)
{
}

static bool test_simple_merge(void)
{
	struct vm_area_struct *vma;
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	struct vm_area_struct *vma_left = alloc_vma(&mm, 0, 0x1000, 0, flags);
	struct vm_area_struct *vma_right = alloc_vma(&mm, 0x2000, 0x3000, 2, flags);
	VMA_ITERATOR(vmi, &mm, 0x1000);
	struct vma_merge_struct vmg = {
		.mm = &mm,
		.vmi = &vmi,
		.start = 0x1000,
		.end = 0x2000,
		.flags = flags,
		.pgoff = 1,
	};

	ASSERT_FALSE(vma_link(&mm, vma_left));
	ASSERT_FALSE(vma_link(&mm, vma_right));

	vma = merge_new(&vmg);
	ASSERT_NE(vma, NULL);

	ASSERT_EQ(vma->vm_start, 0);
	ASSERT_EQ(vma->vm_end, 0x3000);
	ASSERT_EQ(vma->vm_pgoff, 0);
	ASSERT_EQ(vma->vm_flags, flags);

	vm_area_free(vma);
	mtree_destroy(&mm.mm_mt);

	return true;
}

static bool test_simple_modify(void)
{
	struct vm_area_struct *vma;
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	struct vm_area_struct *init_vma = alloc_vma(&mm, 0, 0x3000, 0, flags);
	VMA_ITERATOR(vmi, &mm, 0x1000);

	ASSERT_FALSE(vma_link(&mm, init_vma));

	/*
	 * The flags will not be changed, the vma_modify_flags() function
	 * performs the merge/split only.
	 */
	vma = vma_modify_flags(&vmi, init_vma, init_vma,
			       0x1000, 0x2000, VM_READ | VM_MAYREAD);
	ASSERT_NE(vma, NULL);
	/* We modify the provided VMA, and on split allocate new VMAs. */
	ASSERT_EQ(vma, init_vma);

	ASSERT_EQ(vma->vm_start, 0x1000);
	ASSERT_EQ(vma->vm_end, 0x2000);
	ASSERT_EQ(vma->vm_pgoff, 1);

	/*
	 * Now walk through the three split VMAs and make sure they are as
	 * expected.
	 */

	vma_iter_set(&vmi, 0);
	vma = vma_iter_load(&vmi);

	ASSERT_EQ(vma->vm_start, 0);
	ASSERT_EQ(vma->vm_end, 0x1000);
	ASSERT_EQ(vma->vm_pgoff, 0);

	vm_area_free(vma);
	vma_iter_clear(&vmi);

	vma = vma_next(&vmi);

	ASSERT_EQ(vma->vm_start, 0x1000);
	ASSERT_EQ(vma->vm_end, 0x2000);
	ASSERT_EQ(vma->vm_pgoff, 1);

	vm_area_free(vma);
	vma_iter_clear(&vmi);

	vma = vma_next(&vmi);

	ASSERT_EQ(vma->vm_start, 0x2000);
	ASSERT_EQ(vma->vm_end, 0x3000);
	ASSERT_EQ(vma->vm_pgoff, 2);

	vm_area_free(vma);
	mtree_destroy(&mm.mm_mt);

	return true;
}

static bool test_simple_expand(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	struct vm_area_struct *vma = alloc_vma(&mm, 0, 0x1000, 0, flags);
	VMA_ITERATOR(vmi, &mm, 0);
	struct vma_merge_struct vmg = {
		.vmi = &vmi,
		.middle = vma,
		.start = 0,
		.end = 0x3000,
		.pgoff = 0,
	};

	ASSERT_FALSE(vma_link(&mm, vma));

	ASSERT_FALSE(expand_existing(&vmg));

	ASSERT_EQ(vma->vm_start, 0);
	ASSERT_EQ(vma->vm_end, 0x3000);
	ASSERT_EQ(vma->vm_pgoff, 0);

	vm_area_free(vma);
	mtree_destroy(&mm.mm_mt);

	return true;
}

static bool test_simple_shrink(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	struct vm_area_struct *vma = alloc_vma(&mm, 0, 0x3000, 0, flags);
	VMA_ITERATOR(vmi, &mm, 0);

	ASSERT_FALSE(vma_link(&mm, vma));

	ASSERT_FALSE(vma_shrink(&vmi, vma, 0, 0x1000, 0));

	ASSERT_EQ(vma->vm_start, 0);
	ASSERT_EQ(vma->vm_end, 0x1000);
	ASSERT_EQ(vma->vm_pgoff, 0);

	vm_area_free(vma);
	mtree_destroy(&mm.mm_mt);

	return true;
}

static bool test_merge_new(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	VMA_ITERATOR(vmi, &mm, 0);
	struct vma_merge_struct vmg = {
		.mm = &mm,
		.vmi = &vmi,
	};
	struct anon_vma_chain dummy_anon_vma_chain_a = {
		.anon_vma = &dummy_anon_vma,
	};
	struct anon_vma_chain dummy_anon_vma_chain_b = {
		.anon_vma = &dummy_anon_vma,
	};
	struct anon_vma_chain dummy_anon_vma_chain_c = {
		.anon_vma = &dummy_anon_vma,
	};
	struct anon_vma_chain dummy_anon_vma_chain_d = {
		.anon_vma = &dummy_anon_vma,
	};
	const struct vm_operations_struct vm_ops = {
		.close = dummy_close,
	};
	int count;
	struct vm_area_struct *vma, *vma_a, *vma_b, *vma_c, *vma_d;
	bool merged;

	/*
	 * 0123456789abc
	 * AA B       CC
	 */
	vma_a = alloc_and_link_vma(&mm, 0, 0x2000, 0, flags);
	ASSERT_NE(vma_a, NULL);
	/* We give each VMA a single avc so we can test anon_vma duplication. */
	INIT_LIST_HEAD(&vma_a->anon_vma_chain);
	list_add(&dummy_anon_vma_chain_a.same_vma, &vma_a->anon_vma_chain);

	vma_b = alloc_and_link_vma(&mm, 0x3000, 0x4000, 3, flags);
	ASSERT_NE(vma_b, NULL);
	INIT_LIST_HEAD(&vma_b->anon_vma_chain);
	list_add(&dummy_anon_vma_chain_b.same_vma, &vma_b->anon_vma_chain);

	vma_c = alloc_and_link_vma(&mm, 0xb000, 0xc000, 0xb, flags);
	ASSERT_NE(vma_c, NULL);
	INIT_LIST_HEAD(&vma_c->anon_vma_chain);
	list_add(&dummy_anon_vma_chain_c.same_vma, &vma_c->anon_vma_chain);

	/*
	 * NO merge.
	 *
	 * 0123456789abc
	 * AA B   **  CC
	 */
	vma_d = try_merge_new_vma(&mm, &vmg, 0x7000, 0x9000, 7, flags, &merged);
	ASSERT_NE(vma_d, NULL);
	INIT_LIST_HEAD(&vma_d->anon_vma_chain);
	list_add(&dummy_anon_vma_chain_d.same_vma, &vma_d->anon_vma_chain);
	ASSERT_FALSE(merged);
	ASSERT_EQ(mm.map_count, 4);

	/*
	 * Merge BOTH sides.
	 *
	 * 0123456789abc
	 * AA*B   DD  CC
	 */
	vma_a->vm_ops = &vm_ops; /* This should have no impact. */
	vma_b->anon_vma = &dummy_anon_vma;
	vma = try_merge_new_vma(&mm, &vmg, 0x2000, 0x3000, 2, flags, &merged);
	ASSERT_EQ(vma, vma_a);
	/* Merge with A, delete B. */
	ASSERT_TRUE(merged);
	ASSERT_EQ(vma->vm_start, 0);
	ASSERT_EQ(vma->vm_end, 0x4000);
	ASSERT_EQ(vma->vm_pgoff, 0);
	ASSERT_EQ(vma->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_write_started(vma));
	ASSERT_EQ(mm.map_count, 3);

	/*
	 * Merge to PREVIOUS VMA.
	 *
	 * 0123456789abc
	 * AAAA*  DD  CC
	 */
	vma = try_merge_new_vma(&mm, &vmg, 0x4000, 0x5000, 4, flags, &merged);
	ASSERT_EQ(vma, vma_a);
	/* Extend A. */
	ASSERT_TRUE(merged);
	ASSERT_EQ(vma->vm_start, 0);
	ASSERT_EQ(vma->vm_end, 0x5000);
	ASSERT_EQ(vma->vm_pgoff, 0);
	ASSERT_EQ(vma->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_write_started(vma));
	ASSERT_EQ(mm.map_count, 3);

	/*
	 * Merge to NEXT VMA.
	 *
	 * 0123456789abc
	 * AAAAA *DD  CC
	 */
	vma_d->anon_vma = &dummy_anon_vma;
	vma_d->vm_ops = &vm_ops; /* This should have no impact. */
	vma = try_merge_new_vma(&mm, &vmg, 0x6000, 0x7000, 6, flags, &merged);
	ASSERT_EQ(vma, vma_d);
	/* Prepend. */
	ASSERT_TRUE(merged);
	ASSERT_EQ(vma->vm_start, 0x6000);
	ASSERT_EQ(vma->vm_end, 0x9000);
	ASSERT_EQ(vma->vm_pgoff, 6);
	ASSERT_EQ(vma->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_write_started(vma));
	ASSERT_EQ(mm.map_count, 3);

	/*
	 * Merge BOTH sides.
	 *
	 * 0123456789abc
	 * AAAAA*DDD  CC
	 */
	vma_d->vm_ops = NULL; /* This would otherwise degrade the merge. */
	vma = try_merge_new_vma(&mm, &vmg, 0x5000, 0x6000, 5, flags, &merged);
	ASSERT_EQ(vma, vma_a);
	/* Merge with A, delete D. */
	ASSERT_TRUE(merged);
	ASSERT_EQ(vma->vm_start, 0);
	ASSERT_EQ(vma->vm_end, 0x9000);
	ASSERT_EQ(vma->vm_pgoff, 0);
	ASSERT_EQ(vma->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_write_started(vma));
	ASSERT_EQ(mm.map_count, 2);

	/*
	 * Merge to NEXT VMA.
	 *
	 * 0123456789abc
	 * AAAAAAAAA *CC
	 */
	vma_c->anon_vma = &dummy_anon_vma;
	vma = try_merge_new_vma(&mm, &vmg, 0xa000, 0xb000, 0xa, flags, &merged);
	ASSERT_EQ(vma, vma_c);
	/* Prepend C. */
	ASSERT_TRUE(merged);
	ASSERT_EQ(vma->vm_start, 0xa000);
	ASSERT_EQ(vma->vm_end, 0xc000);
	ASSERT_EQ(vma->vm_pgoff, 0xa);
	ASSERT_EQ(vma->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_write_started(vma));
	ASSERT_EQ(mm.map_count, 2);

	/*
	 * Merge BOTH sides.
	 *
	 * 0123456789abc
	 * AAAAAAAAA*CCC
	 */
	vma = try_merge_new_vma(&mm, &vmg, 0x9000, 0xa000, 0x9, flags, &merged);
	ASSERT_EQ(vma, vma_a);
	/* Extend A and delete C. */
	ASSERT_TRUE(merged);
	ASSERT_EQ(vma->vm_start, 0);
	ASSERT_EQ(vma->vm_end, 0xc000);
	ASSERT_EQ(vma->vm_pgoff, 0);
	ASSERT_EQ(vma->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_write_started(vma));
	ASSERT_EQ(mm.map_count, 1);

	/*
	 * Final state.
	 *
	 * 0123456789abc
	 * AAAAAAAAAAAAA
	 */

	count = 0;
	vma_iter_set(&vmi, 0);
	for_each_vma(vmi, vma) {
		ASSERT_NE(vma, NULL);
		ASSERT_EQ(vma->vm_start, 0);
		ASSERT_EQ(vma->vm_end, 0xc000);
		ASSERT_EQ(vma->vm_pgoff, 0);
		ASSERT_EQ(vma->anon_vma, &dummy_anon_vma);

		vm_area_free(vma);
		count++;
	}

	/* Should only have one VMA left (though freed) after all is done.*/
	ASSERT_EQ(count, 1);

	mtree_destroy(&mm.mm_mt);
	return true;
}

static bool test_vma_merge_special_flags(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	VMA_ITERATOR(vmi, &mm, 0);
	struct vma_merge_struct vmg = {
		.mm = &mm,
		.vmi = &vmi,
	};
	vm_flags_t special_flags[] = { VM_IO, VM_DONTEXPAND, VM_PFNMAP, VM_MIXEDMAP };
	vm_flags_t all_special_flags = 0;
	int i;
	struct vm_area_struct *vma_left, *vma;

	/* Make sure there aren't new VM_SPECIAL flags. */
	for (i = 0; i < ARRAY_SIZE(special_flags); i++) {
		all_special_flags |= special_flags[i];
	}
	ASSERT_EQ(all_special_flags, VM_SPECIAL);

	/*
	 * 01234
	 * AAA
	 */
	vma_left = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	ASSERT_NE(vma_left, NULL);

	/* 1. Set up new VMA with special flag that would otherwise merge. */

	/*
	 * 01234
	 * AAA*
	 *
	 * This should merge if not for the VM_SPECIAL flag.
	 */
	vmg_set_range(&vmg, 0x3000, 0x4000, 3, flags);
	for (i = 0; i < ARRAY_SIZE(special_flags); i++) {
		vm_flags_t special_flag = special_flags[i];

		vma_left->__vm_flags = flags | special_flag;
		vmg.flags = flags | special_flag;
		vma = merge_new(&vmg);
		ASSERT_EQ(vma, NULL);
		ASSERT_EQ(vmg.state, VMA_MERGE_NOMERGE);
	}

	/* 2. Modify VMA with special flag that would otherwise merge. */

	/*
	 * 01234
	 * AAAB
	 *
	 * Create a VMA to modify.
	 */
	vma = alloc_and_link_vma(&mm, 0x3000, 0x4000, 3, flags);
	ASSERT_NE(vma, NULL);
	vmg.middle = vma;

	for (i = 0; i < ARRAY_SIZE(special_flags); i++) {
		vm_flags_t special_flag = special_flags[i];

		vma_left->__vm_flags = flags | special_flag;
		vmg.flags = flags | special_flag;
		vma = merge_existing(&vmg);
		ASSERT_EQ(vma, NULL);
		ASSERT_EQ(vmg.state, VMA_MERGE_NOMERGE);
	}

	cleanup_mm(&mm, &vmi);
	return true;
}

static bool test_vma_merge_with_close(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	VMA_ITERATOR(vmi, &mm, 0);
	struct vma_merge_struct vmg = {
		.mm = &mm,
		.vmi = &vmi,
	};
	const struct vm_operations_struct vm_ops = {
		.close = dummy_close,
	};
	struct vm_area_struct *vma_prev, *vma_next, *vma;

	/*
	 * When merging VMAs we are not permitted to remove any VMA that has a
	 * vm_ops->close() hook.
	 *
	 * Considering the two possible adjacent VMAs to which a VMA can be
	 * merged:
	 *
	 * [ prev ][ vma ][ next ]
	 *
	 * In no case will we need to delete prev. If the operation is
	 * mergeable, then prev will be extended with one or both of vma and
	 * next deleted.
	 *
	 * As a result, during initial mergeability checks, only
	 * can_vma_merge_before() (which implies the VMA being merged with is
	 * 'next' as shown above) bothers to check to see whether the next VMA
	 * has a vm_ops->close() callback that will need to be called when
	 * removed.
	 *
	 * If it does, then we cannot merge as the resources that the close()
	 * operation potentially clears down are tied only to the existing VMA
	 * range and we have no way of extending those to the nearly merged one.
	 *
	 * We must consider two scenarios:
	 *
	 * A.
	 *
	 * vm_ops->close:     -       -    !NULL
	 *                 [ prev ][ vma ][ next ]
	 *
	 * Where prev may or may not be present/mergeable.
	 *
	 * This is picked up by a specific check in can_vma_merge_before().
	 *
	 * B.
	 *
	 * vm_ops->close:     -     !NULL
	 *                 [ prev ][ vma ]
	 *
	 * Where prev and vma are present and mergeable.
	 *
	 * This is picked up by a specific check in the modified VMA merge.
	 *
	 * IMPORTANT NOTE: We make the assumption that the following case:
	 *
	 *    -     !NULL   NULL
	 * [ prev ][ vma ][ next ]
	 *
	 * Cannot occur, because vma->vm_ops being the same implies the same
	 * vma->vm_file, and therefore this would mean that next->vm_ops->close
	 * would be set too, and thus scenario A would pick this up.
	 */

	/*
	 * The only case of a new VMA merge that results in a VMA being deleted
	 * is one where both the previous and next VMAs are merged - in this
	 * instance the next VMA is deleted, and the previous VMA is extended.
	 *
	 * If we are unable to do so, we reduce the operation to simply
	 * extending the prev VMA and not merging next.
	 *
	 * 0123456789
	 * PPP**NNNN
	 *             ->
	 * 0123456789
	 * PPPPPPNNN
	 */

	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma_next = alloc_and_link_vma(&mm, 0x5000, 0x9000, 5, flags);
	vma_next->vm_ops = &vm_ops;

	vmg_set_range(&vmg, 0x3000, 0x5000, 3, flags);
	ASSERT_EQ(merge_new(&vmg), vma_prev);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);
	ASSERT_EQ(vma_prev->vm_start, 0);
	ASSERT_EQ(vma_prev->vm_end, 0x5000);
	ASSERT_EQ(vma_prev->vm_pgoff, 0);

	ASSERT_EQ(cleanup_mm(&mm, &vmi), 2);

	/*
	 * When modifying an existing VMA there are further cases where we
	 * delete VMAs.
	 *
	 *    <>
	 * 0123456789
	 * PPPVV
	 *
	 * In this instance, if vma has a close hook, the merge simply cannot
	 * proceed.
	 */

	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, flags);
	vma->vm_ops = &vm_ops;

	vmg_set_range(&vmg, 0x3000, 0x5000, 3, flags);
	vmg.prev = vma_prev;
	vmg.middle = vma;

	/*
	 * The VMA being modified in a way that would otherwise merge should
	 * also fail.
	 */
	ASSERT_EQ(merge_existing(&vmg), NULL);
	ASSERT_EQ(vmg.state, VMA_MERGE_NOMERGE);

	ASSERT_EQ(cleanup_mm(&mm, &vmi), 2);

	/*
	 * This case is mirrored if merging with next.
	 *
	 *    <>
	 * 0123456789
	 *    VVNNNN
	 *
	 * In this instance, if vma has a close hook, the merge simply cannot
	 * proceed.
	 */

	vma = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, flags);
	vma_next = alloc_and_link_vma(&mm, 0x5000, 0x9000, 5, flags);
	vma->vm_ops = &vm_ops;

	vmg_set_range(&vmg, 0x3000, 0x5000, 3, flags);
	vmg.middle = vma;
	ASSERT_EQ(merge_existing(&vmg), NULL);
	/*
	 * Initially this is misapprehended as an out of memory report, as the
	 * close() check is handled in the same way as anon_vma duplication
	 * failures, however a subsequent patch resolves this.
	 */
	ASSERT_EQ(vmg.state, VMA_MERGE_NOMERGE);

	ASSERT_EQ(cleanup_mm(&mm, &vmi), 2);

	/*
	 * Finally, we consider two variants of the case where we modify a VMA
	 * to merge with both the previous and next VMAs.
	 *
	 * The first variant is where vma has a close hook. In this instance, no
	 * merge can proceed.
	 *
	 *    <>
	 * 0123456789
	 * PPPVVNNNN
	 */

	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, flags);
	vma_next = alloc_and_link_vma(&mm, 0x5000, 0x9000, 5, flags);
	vma->vm_ops = &vm_ops;

	vmg_set_range(&vmg, 0x3000, 0x5000, 3, flags);
	vmg.prev = vma_prev;
	vmg.middle = vma;

	ASSERT_EQ(merge_existing(&vmg), NULL);
	ASSERT_EQ(vmg.state, VMA_MERGE_NOMERGE);

	ASSERT_EQ(cleanup_mm(&mm, &vmi), 3);

	/*
	 * The second variant is where next has a close hook. In this instance,
	 * we reduce the operation to a merge between prev and vma.
	 *
	 *    <>
	 * 0123456789
	 * PPPVVNNNN
	 *            ->
	 * 0123456789
	 * PPPPPNNNN
	 */

	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, flags);
	vma_next = alloc_and_link_vma(&mm, 0x5000, 0x9000, 5, flags);
	vma_next->vm_ops = &vm_ops;

	vmg_set_range(&vmg, 0x3000, 0x5000, 3, flags);
	vmg.prev = vma_prev;
	vmg.middle = vma;

	ASSERT_EQ(merge_existing(&vmg), vma_prev);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);
	ASSERT_EQ(vma_prev->vm_start, 0);
	ASSERT_EQ(vma_prev->vm_end, 0x5000);
	ASSERT_EQ(vma_prev->vm_pgoff, 0);

	ASSERT_EQ(cleanup_mm(&mm, &vmi), 2);

	return true;
}

static bool test_vma_merge_new_with_close(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	VMA_ITERATOR(vmi, &mm, 0);
	struct vma_merge_struct vmg = {
		.mm = &mm,
		.vmi = &vmi,
	};
	struct vm_area_struct *vma_prev = alloc_and_link_vma(&mm, 0, 0x2000, 0, flags);
	struct vm_area_struct *vma_next = alloc_and_link_vma(&mm, 0x5000, 0x7000, 5, flags);
	const struct vm_operations_struct vm_ops = {
		.close = dummy_close,
	};
	struct vm_area_struct *vma;

	/*
	 * We should allow the partial merge of a proposed new VMA if the
	 * surrounding VMAs have vm_ops->close() hooks (but are otherwise
	 * compatible), e.g.:
	 *
	 *        New VMA
	 *    A  v-------v  B
	 * |-----|       |-----|
	 *  close         close
	 *
	 * Since the rule is to not DELETE a VMA with a close operation, this
	 * should be permitted, only rather than expanding A and deleting B, we
	 * should simply expand A and leave B intact, e.g.:
	 *
	 *        New VMA
	 *       A          B
	 * |------------||-----|
	 *  close         close
	 */

	/* Have prev and next have a vm_ops->close() hook. */
	vma_prev->vm_ops = &vm_ops;
	vma_next->vm_ops = &vm_ops;

	vmg_set_range(&vmg, 0x2000, 0x5000, 2, flags);
	vma = merge_new(&vmg);
	ASSERT_NE(vma, NULL);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);
	ASSERT_EQ(vma->vm_start, 0);
	ASSERT_EQ(vma->vm_end, 0x5000);
	ASSERT_EQ(vma->vm_pgoff, 0);
	ASSERT_EQ(vma->vm_ops, &vm_ops);
	ASSERT_TRUE(vma_write_started(vma));
	ASSERT_EQ(mm.map_count, 2);

	cleanup_mm(&mm, &vmi);
	return true;
}

static bool test_merge_existing(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	VMA_ITERATOR(vmi, &mm, 0);
	struct vm_area_struct *vma, *vma_prev, *vma_next;
	struct vma_merge_struct vmg = {
		.mm = &mm,
		.vmi = &vmi,
	};
	const struct vm_operations_struct vm_ops = {
		.close = dummy_close,
	};

	/*
	 * Merge right case - partial span.
	 *
	 *    <->
	 * 0123456789
	 *   VVVVNNN
	 *            ->
	 * 0123456789
	 *   VNNNNNN
	 */
	vma = alloc_and_link_vma(&mm, 0x2000, 0x6000, 2, flags);
	vma->vm_ops = &vm_ops; /* This should have no impact. */
	vma_next = alloc_and_link_vma(&mm, 0x6000, 0x9000, 6, flags);
	vma_next->vm_ops = &vm_ops; /* This should have no impact. */
	vmg_set_range(&vmg, 0x3000, 0x6000, 3, flags);
	vmg.middle = vma;
	vmg.prev = vma;
	vma->anon_vma = &dummy_anon_vma;
	ASSERT_EQ(merge_existing(&vmg), vma_next);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);
	ASSERT_EQ(vma_next->vm_start, 0x3000);
	ASSERT_EQ(vma_next->vm_end, 0x9000);
	ASSERT_EQ(vma_next->vm_pgoff, 3);
	ASSERT_EQ(vma_next->anon_vma, &dummy_anon_vma);
	ASSERT_EQ(vma->vm_start, 0x2000);
	ASSERT_EQ(vma->vm_end, 0x3000);
	ASSERT_EQ(vma->vm_pgoff, 2);
	ASSERT_TRUE(vma_write_started(vma));
	ASSERT_TRUE(vma_write_started(vma_next));
	ASSERT_EQ(mm.map_count, 2);

	/* Clear down and reset. */
	ASSERT_EQ(cleanup_mm(&mm, &vmi), 2);

	/*
	 * Merge right case - full span.
	 *
	 *   <-->
	 * 0123456789
	 *   VVVVNNN
	 *            ->
	 * 0123456789
	 *   NNNNNNN
	 */
	vma = alloc_and_link_vma(&mm, 0x2000, 0x6000, 2, flags);
	vma_next = alloc_and_link_vma(&mm, 0x6000, 0x9000, 6, flags);
	vma_next->vm_ops = &vm_ops; /* This should have no impact. */
	vmg_set_range(&vmg, 0x2000, 0x6000, 2, flags);
	vmg.middle = vma;
	vma->anon_vma = &dummy_anon_vma;
	ASSERT_EQ(merge_existing(&vmg), vma_next);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);
	ASSERT_EQ(vma_next->vm_start, 0x2000);
	ASSERT_EQ(vma_next->vm_end, 0x9000);
	ASSERT_EQ(vma_next->vm_pgoff, 2);
	ASSERT_EQ(vma_next->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_write_started(vma_next));
	ASSERT_EQ(mm.map_count, 1);

	/* Clear down and reset. We should have deleted vma. */
	ASSERT_EQ(cleanup_mm(&mm, &vmi), 1);

	/*
	 * Merge left case - partial span.
	 *
	 *    <->
	 * 0123456789
	 * PPPVVVV
	 *            ->
	 * 0123456789
	 * PPPPPPV
	 */
	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma_prev->vm_ops = &vm_ops; /* This should have no impact. */
	vma = alloc_and_link_vma(&mm, 0x3000, 0x7000, 3, flags);
	vma->vm_ops = &vm_ops; /* This should have no impact. */
	vmg_set_range(&vmg, 0x3000, 0x6000, 3, flags);
	vmg.prev = vma_prev;
	vmg.middle = vma;
	vma->anon_vma = &dummy_anon_vma;

	ASSERT_EQ(merge_existing(&vmg), vma_prev);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);
	ASSERT_EQ(vma_prev->vm_start, 0);
	ASSERT_EQ(vma_prev->vm_end, 0x6000);
	ASSERT_EQ(vma_prev->vm_pgoff, 0);
	ASSERT_EQ(vma_prev->anon_vma, &dummy_anon_vma);
	ASSERT_EQ(vma->vm_start, 0x6000);
	ASSERT_EQ(vma->vm_end, 0x7000);
	ASSERT_EQ(vma->vm_pgoff, 6);
	ASSERT_TRUE(vma_write_started(vma_prev));
	ASSERT_TRUE(vma_write_started(vma));
	ASSERT_EQ(mm.map_count, 2);

	/* Clear down and reset. */
	ASSERT_EQ(cleanup_mm(&mm, &vmi), 2);

	/*
	 * Merge left case - full span.
	 *
	 *    <-->
	 * 0123456789
	 * PPPVVVV
	 *            ->
	 * 0123456789
	 * PPPPPPP
	 */
	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma_prev->vm_ops = &vm_ops; /* This should have no impact. */
	vma = alloc_and_link_vma(&mm, 0x3000, 0x7000, 3, flags);
	vmg_set_range(&vmg, 0x3000, 0x7000, 3, flags);
	vmg.prev = vma_prev;
	vmg.middle = vma;
	vma->anon_vma = &dummy_anon_vma;
	ASSERT_EQ(merge_existing(&vmg), vma_prev);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);
	ASSERT_EQ(vma_prev->vm_start, 0);
	ASSERT_EQ(vma_prev->vm_end, 0x7000);
	ASSERT_EQ(vma_prev->vm_pgoff, 0);
	ASSERT_EQ(vma_prev->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_write_started(vma_prev));
	ASSERT_EQ(mm.map_count, 1);

	/* Clear down and reset. We should have deleted vma. */
	ASSERT_EQ(cleanup_mm(&mm, &vmi), 1);

	/*
	 * Merge both case.
	 *
	 *    <-->
	 * 0123456789
	 * PPPVVVVNNN
	 *             ->
	 * 0123456789
	 * PPPPPPPPPP
	 */
	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma_prev->vm_ops = &vm_ops; /* This should have no impact. */
	vma = alloc_and_link_vma(&mm, 0x3000, 0x7000, 3, flags);
	vma_next = alloc_and_link_vma(&mm, 0x7000, 0x9000, 7, flags);
	vmg_set_range(&vmg, 0x3000, 0x7000, 3, flags);
	vmg.prev = vma_prev;
	vmg.middle = vma;
	vma->anon_vma = &dummy_anon_vma;
	ASSERT_EQ(merge_existing(&vmg), vma_prev);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);
	ASSERT_EQ(vma_prev->vm_start, 0);
	ASSERT_EQ(vma_prev->vm_end, 0x9000);
	ASSERT_EQ(vma_prev->vm_pgoff, 0);
	ASSERT_EQ(vma_prev->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_write_started(vma_prev));
	ASSERT_EQ(mm.map_count, 1);

	/* Clear down and reset. We should have deleted prev and next. */
	ASSERT_EQ(cleanup_mm(&mm, &vmi), 1);

	/*
	 * Non-merge ranges. the modified VMA merge operation assumes that the
	 * caller always specifies ranges within the input VMA so we need only
	 * examine these cases.
	 *
	 *     -
	 *      -
	 *       -
	 *     <->
	 *     <>
	 *      <>
	 * 0123456789a
	 * PPPVVVVVNNN
	 */

	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma = alloc_and_link_vma(&mm, 0x3000, 0x8000, 3, flags);
	vma_next = alloc_and_link_vma(&mm, 0x8000, 0xa000, 8, flags);

	vmg_set_range(&vmg, 0x4000, 0x5000, 4, flags);
	vmg.prev = vma;
	vmg.middle = vma;
	ASSERT_EQ(merge_existing(&vmg), NULL);
	ASSERT_EQ(vmg.state, VMA_MERGE_NOMERGE);

	vmg_set_range(&vmg, 0x5000, 0x6000, 5, flags);
	vmg.prev = vma;
	vmg.middle = vma;
	ASSERT_EQ(merge_existing(&vmg), NULL);
	ASSERT_EQ(vmg.state, VMA_MERGE_NOMERGE);

	vmg_set_range(&vmg, 0x6000, 0x7000, 6, flags);
	vmg.prev = vma;
	vmg.middle = vma;
	ASSERT_EQ(merge_existing(&vmg), NULL);
	ASSERT_EQ(vmg.state, VMA_MERGE_NOMERGE);

	vmg_set_range(&vmg, 0x4000, 0x7000, 4, flags);
	vmg.prev = vma;
	vmg.middle = vma;
	ASSERT_EQ(merge_existing(&vmg), NULL);
	ASSERT_EQ(vmg.state, VMA_MERGE_NOMERGE);

	vmg_set_range(&vmg, 0x4000, 0x6000, 4, flags);
	vmg.prev = vma;
	vmg.middle = vma;
	ASSERT_EQ(merge_existing(&vmg), NULL);
	ASSERT_EQ(vmg.state, VMA_MERGE_NOMERGE);

	vmg_set_range(&vmg, 0x5000, 0x6000, 5, flags);
	vmg.prev = vma;
	vmg.middle = vma;
	ASSERT_EQ(merge_existing(&vmg), NULL);
	ASSERT_EQ(vmg.state, VMA_MERGE_NOMERGE);

	ASSERT_EQ(cleanup_mm(&mm, &vmi), 3);

	return true;
}

static bool test_anon_vma_non_mergeable(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	VMA_ITERATOR(vmi, &mm, 0);
	struct vm_area_struct *vma, *vma_prev, *vma_next;
	struct vma_merge_struct vmg = {
		.mm = &mm,
		.vmi = &vmi,
	};
	struct anon_vma_chain dummy_anon_vma_chain1 = {
		.anon_vma = &dummy_anon_vma,
	};
	struct anon_vma_chain dummy_anon_vma_chain2 = {
		.anon_vma = &dummy_anon_vma,
	};

	/*
	 * In the case of modified VMA merge, merging both left and right VMAs
	 * but where prev and next have incompatible anon_vma objects, we revert
	 * to a merge of prev and VMA:
	 *
	 *    <-->
	 * 0123456789
	 * PPPVVVVNNN
	 *            ->
	 * 0123456789
	 * PPPPPPPNNN
	 */
	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma = alloc_and_link_vma(&mm, 0x3000, 0x7000, 3, flags);
	vma_next = alloc_and_link_vma(&mm, 0x7000, 0x9000, 7, flags);

	/*
	 * Give both prev and next single anon_vma_chain fields, so they will
	 * merge with the NULL vmg->anon_vma.
	 *
	 * However, when prev is compared to next, the merge should fail.
	 */

	INIT_LIST_HEAD(&vma_prev->anon_vma_chain);
	list_add(&dummy_anon_vma_chain1.same_vma, &vma_prev->anon_vma_chain);
	ASSERT_TRUE(list_is_singular(&vma_prev->anon_vma_chain));
	vma_prev->anon_vma = &dummy_anon_vma;
	ASSERT_TRUE(is_mergeable_anon_vma(NULL, vma_prev->anon_vma, vma_prev));

	INIT_LIST_HEAD(&vma_next->anon_vma_chain);
	list_add(&dummy_anon_vma_chain2.same_vma, &vma_next->anon_vma_chain);
	ASSERT_TRUE(list_is_singular(&vma_next->anon_vma_chain));
	vma_next->anon_vma = (struct anon_vma *)2;
	ASSERT_TRUE(is_mergeable_anon_vma(NULL, vma_next->anon_vma, vma_next));

	ASSERT_FALSE(is_mergeable_anon_vma(vma_prev->anon_vma, vma_next->anon_vma, NULL));

	vmg_set_range(&vmg, 0x3000, 0x7000, 3, flags);
	vmg.prev = vma_prev;
	vmg.middle = vma;

	ASSERT_EQ(merge_existing(&vmg), vma_prev);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);
	ASSERT_EQ(vma_prev->vm_start, 0);
	ASSERT_EQ(vma_prev->vm_end, 0x7000);
	ASSERT_EQ(vma_prev->vm_pgoff, 0);
	ASSERT_TRUE(vma_write_started(vma_prev));
	ASSERT_FALSE(vma_write_started(vma_next));

	/* Clear down and reset. */
	ASSERT_EQ(cleanup_mm(&mm, &vmi), 2);

	/*
	 * Now consider the new VMA case. This is equivalent, only adding a new
	 * VMA in a gap between prev and next.
	 *
	 *    <-->
	 * 0123456789
	 * PPP****NNN
	 *            ->
	 * 0123456789
	 * PPPPPPPNNN
	 */
	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma_next = alloc_and_link_vma(&mm, 0x7000, 0x9000, 7, flags);

	INIT_LIST_HEAD(&vma_prev->anon_vma_chain);
	list_add(&dummy_anon_vma_chain1.same_vma, &vma_prev->anon_vma_chain);
	vma_prev->anon_vma = (struct anon_vma *)1;

	INIT_LIST_HEAD(&vma_next->anon_vma_chain);
	list_add(&dummy_anon_vma_chain2.same_vma, &vma_next->anon_vma_chain);
	vma_next->anon_vma = (struct anon_vma *)2;

	vmg_set_range(&vmg, 0x3000, 0x7000, 3, flags);
	vmg.prev = vma_prev;

	ASSERT_EQ(merge_new(&vmg), vma_prev);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);
	ASSERT_EQ(vma_prev->vm_start, 0);
	ASSERT_EQ(vma_prev->vm_end, 0x7000);
	ASSERT_EQ(vma_prev->vm_pgoff, 0);
	ASSERT_TRUE(vma_write_started(vma_prev));
	ASSERT_FALSE(vma_write_started(vma_next));

	/* Final cleanup. */
	ASSERT_EQ(cleanup_mm(&mm, &vmi), 2);

	return true;
}

static bool test_dup_anon_vma(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	VMA_ITERATOR(vmi, &mm, 0);
	struct vma_merge_struct vmg = {
		.mm = &mm,
		.vmi = &vmi,
	};
	struct anon_vma_chain dummy_anon_vma_chain = {
		.anon_vma = &dummy_anon_vma,
	};
	struct vm_area_struct *vma_prev, *vma_next, *vma;

	reset_dummy_anon_vma();

	/*
	 * Expanding a VMA delete the next one duplicates next's anon_vma and
	 * assigns it to the expanded VMA.
	 *
	 * This covers new VMA merging, as these operations amount to a VMA
	 * expand.
	 */
	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma_next = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, flags);
	vma_next->anon_vma = &dummy_anon_vma;

	vmg_set_range(&vmg, 0, 0x5000, 0, flags);
	vmg.middle = vma_prev;
	vmg.next = vma_next;

	ASSERT_EQ(expand_existing(&vmg), 0);

	/* Will have been cloned. */
	ASSERT_EQ(vma_prev->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_prev->anon_vma->was_cloned);

	/* Cleanup ready for next run. */
	cleanup_mm(&mm, &vmi);

	/*
	 * next has anon_vma, we assign to prev.
	 *
	 *         |<----->|
	 * |-------*********-------|
	 *   prev     vma     next
	 *  extend   delete  delete
	 */

	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, flags);
	vma_next = alloc_and_link_vma(&mm, 0x5000, 0x8000, 5, flags);

	/* Initialise avc so mergeability check passes. */
	INIT_LIST_HEAD(&vma_next->anon_vma_chain);
	list_add(&dummy_anon_vma_chain.same_vma, &vma_next->anon_vma_chain);

	vma_next->anon_vma = &dummy_anon_vma;
	vmg_set_range(&vmg, 0x3000, 0x5000, 3, flags);
	vmg.prev = vma_prev;
	vmg.middle = vma;

	ASSERT_EQ(merge_existing(&vmg), vma_prev);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);

	ASSERT_EQ(vma_prev->vm_start, 0);
	ASSERT_EQ(vma_prev->vm_end, 0x8000);

	ASSERT_EQ(vma_prev->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_prev->anon_vma->was_cloned);

	cleanup_mm(&mm, &vmi);

	/*
	 * vma has anon_vma, we assign to prev.
	 *
	 *         |<----->|
	 * |-------*********-------|
	 *   prev     vma     next
	 *  extend   delete  delete
	 */

	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, flags);
	vma_next = alloc_and_link_vma(&mm, 0x5000, 0x8000, 5, flags);

	vma->anon_vma = &dummy_anon_vma;
	vmg_set_range(&vmg, 0x3000, 0x5000, 3, flags);
	vmg.prev = vma_prev;
	vmg.middle = vma;

	ASSERT_EQ(merge_existing(&vmg), vma_prev);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);

	ASSERT_EQ(vma_prev->vm_start, 0);
	ASSERT_EQ(vma_prev->vm_end, 0x8000);

	ASSERT_EQ(vma_prev->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_prev->anon_vma->was_cloned);

	cleanup_mm(&mm, &vmi);

	/*
	 * vma has anon_vma, we assign to prev.
	 *
	 *         |<----->|
	 * |-------*************
	 *   prev       vma
	 *  extend shrink/delete
	 */

	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma = alloc_and_link_vma(&mm, 0x3000, 0x8000, 3, flags);

	vma->anon_vma = &dummy_anon_vma;
	vmg_set_range(&vmg, 0x3000, 0x5000, 3, flags);
	vmg.prev = vma_prev;
	vmg.middle = vma;

	ASSERT_EQ(merge_existing(&vmg), vma_prev);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);

	ASSERT_EQ(vma_prev->vm_start, 0);
	ASSERT_EQ(vma_prev->vm_end, 0x5000);

	ASSERT_EQ(vma_prev->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_prev->anon_vma->was_cloned);

	cleanup_mm(&mm, &vmi);

	/*
	 * vma has anon_vma, we assign to next.
	 *
	 *     |<----->|
	 * *************-------|
	 *      vma       next
	 * shrink/delete extend
	 */

	vma = alloc_and_link_vma(&mm, 0, 0x5000, 0, flags);
	vma_next = alloc_and_link_vma(&mm, 0x5000, 0x8000, 5, flags);

	vma->anon_vma = &dummy_anon_vma;
	vmg_set_range(&vmg, 0x3000, 0x5000, 3, flags);
	vmg.prev = vma;
	vmg.middle = vma;

	ASSERT_EQ(merge_existing(&vmg), vma_next);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);

	ASSERT_EQ(vma_next->vm_start, 0x3000);
	ASSERT_EQ(vma_next->vm_end, 0x8000);

	ASSERT_EQ(vma_next->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(vma_next->anon_vma->was_cloned);

	cleanup_mm(&mm, &vmi);
	return true;
}

static bool test_vmi_prealloc_fail(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	VMA_ITERATOR(vmi, &mm, 0);
	struct vma_merge_struct vmg = {
		.mm = &mm,
		.vmi = &vmi,
	};
	struct vm_area_struct *vma_prev, *vma;

	/*
	 * We are merging vma into prev, with vma possessing an anon_vma, which
	 * will be duplicated. We cause the vmi preallocation to fail and assert
	 * the duplicated anon_vma is unlinked.
	 */

	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, flags);
	vma->anon_vma = &dummy_anon_vma;

	vmg_set_range(&vmg, 0x3000, 0x5000, 3, flags);
	vmg.prev = vma_prev;
	vmg.middle = vma;

	fail_prealloc = true;

	/* This will cause the merge to fail. */
	ASSERT_EQ(merge_existing(&vmg), NULL);
	ASSERT_EQ(vmg.state, VMA_MERGE_ERROR_NOMEM);
	/* We will already have assigned the anon_vma. */
	ASSERT_EQ(vma_prev->anon_vma, &dummy_anon_vma);
	/* And it was both cloned and unlinked. */
	ASSERT_TRUE(dummy_anon_vma.was_cloned);
	ASSERT_TRUE(dummy_anon_vma.was_unlinked);

	cleanup_mm(&mm, &vmi); /* Resets fail_prealloc too. */

	/*
	 * We repeat the same operation for expanding a VMA, which is what new
	 * VMA merging ultimately uses too. This asserts that unlinking is
	 * performed in this case too.
	 */

	vma_prev = alloc_and_link_vma(&mm, 0, 0x3000, 0, flags);
	vma = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, flags);
	vma->anon_vma = &dummy_anon_vma;

	vmg_set_range(&vmg, 0, 0x5000, 3, flags);
	vmg.middle = vma_prev;
	vmg.next = vma;

	fail_prealloc = true;
	ASSERT_EQ(expand_existing(&vmg), -ENOMEM);
	ASSERT_EQ(vmg.state, VMA_MERGE_ERROR_NOMEM);

	ASSERT_EQ(vma_prev->anon_vma, &dummy_anon_vma);
	ASSERT_TRUE(dummy_anon_vma.was_cloned);
	ASSERT_TRUE(dummy_anon_vma.was_unlinked);

	cleanup_mm(&mm, &vmi);
	return true;
}

static bool test_merge_extend(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	VMA_ITERATOR(vmi, &mm, 0x1000);
	struct vm_area_struct *vma;

	vma = alloc_and_link_vma(&mm, 0, 0x1000, 0, flags);
	alloc_and_link_vma(&mm, 0x3000, 0x4000, 3, flags);

	/*
	 * Extend a VMA into the gap between itself and the following VMA.
	 * This should result in a merge.
	 *
	 * <->
	 * *  *
	 *
	 */

	ASSERT_EQ(vma_merge_extend(&vmi, vma, 0x2000), vma);
	ASSERT_EQ(vma->vm_start, 0);
	ASSERT_EQ(vma->vm_end, 0x4000);
	ASSERT_EQ(vma->vm_pgoff, 0);
	ASSERT_TRUE(vma_write_started(vma));
	ASSERT_EQ(mm.map_count, 1);

	cleanup_mm(&mm, &vmi);
	return true;
}

static bool test_copy_vma(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	bool need_locks = false;
	VMA_ITERATOR(vmi, &mm, 0);
	struct vm_area_struct *vma, *vma_new, *vma_next;

	/* Move backwards and do not merge. */

	vma = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, flags);
	vma_new = copy_vma(&vma, 0, 0x2000, 0, &need_locks);

	ASSERT_NE(vma_new, vma);
	ASSERT_EQ(vma_new->vm_start, 0);
	ASSERT_EQ(vma_new->vm_end, 0x2000);
	ASSERT_EQ(vma_new->vm_pgoff, 0);

	cleanup_mm(&mm, &vmi);

	/* Move a VMA into position next to another and merge the two. */

	vma = alloc_and_link_vma(&mm, 0, 0x2000, 0, flags);
	vma_next = alloc_and_link_vma(&mm, 0x6000, 0x8000, 6, flags);
	vma_new = copy_vma(&vma, 0x4000, 0x2000, 4, &need_locks);

	ASSERT_EQ(vma_new, vma_next);

	cleanup_mm(&mm, &vmi);
	return true;
}

static bool test_expand_only_mode(void)
{
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	VMA_ITERATOR(vmi, &mm, 0);
	struct vm_area_struct *vma_prev, *vma;
	VMG_STATE(vmg, &mm, &vmi, 0x5000, 0x9000, flags, 5);

	/*
	 * Place a VMA prior to the one we're expanding so we assert that we do
	 * not erroneously try to traverse to the previous VMA even though we
	 * have, through the use of the just_expand flag, indicated we do not
	 * need to do so.
	 */
	alloc_and_link_vma(&mm, 0, 0x2000, 0, flags);

	/*
	 * We will be positioned at the prev VMA, but looking to expand to
	 * 0x9000.
	 */
	vma_iter_set(&vmi, 0x3000);
	vma_prev = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, flags);
	vmg.prev = vma_prev;
	vmg.just_expand = true;

	vma = vma_merge_new_range(&vmg);
	ASSERT_NE(vma, NULL);
	ASSERT_EQ(vma, vma_prev);
	ASSERT_EQ(vmg.state, VMA_MERGE_SUCCESS);
	ASSERT_EQ(vma->vm_start, 0x3000);
	ASSERT_EQ(vma->vm_end, 0x9000);
	ASSERT_EQ(vma->vm_pgoff, 3);
	ASSERT_TRUE(vma_write_started(vma));
	ASSERT_EQ(vma_iter_addr(&vmi), 0x3000);

	cleanup_mm(&mm, &vmi);
	return true;
}

static bool test_mmap_region_basic(void)
{
	struct mm_struct mm = {};
	unsigned long addr;
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, &mm, 0);

	current->mm = &mm;

	/* Map at 0x300000, length 0x3000. */
	addr = __mmap_region(NULL, 0x300000, 0x3000,
			     VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE,
			     0x300, NULL);
	ASSERT_EQ(addr, 0x300000);

	/* Map at 0x250000, length 0x3000. */
	addr = __mmap_region(NULL, 0x250000, 0x3000,
			     VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE,
			     0x250, NULL);
	ASSERT_EQ(addr, 0x250000);

	/* Map at 0x303000, merging to 0x300000 of length 0x6000. */
	addr = __mmap_region(NULL, 0x303000, 0x3000,
			     VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE,
			     0x303, NULL);
	ASSERT_EQ(addr, 0x303000);

	/* Map at 0x24d000, merging to 0x250000 of length 0x6000. */
	addr = __mmap_region(NULL, 0x24d000, 0x3000,
			     VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE,
			     0x24d, NULL);
	ASSERT_EQ(addr, 0x24d000);

	ASSERT_EQ(mm.map_count, 2);

	for_each_vma(vmi, vma) {
		if (vma->vm_start == 0x300000) {
			ASSERT_EQ(vma->vm_end, 0x306000);
			ASSERT_EQ(vma->vm_pgoff, 0x300);
		} else if (vma->vm_start == 0x24d000) {
			ASSERT_EQ(vma->vm_end, 0x253000);
			ASSERT_EQ(vma->vm_pgoff, 0x24d);
		} else {
			ASSERT_FALSE(true);
		}
	}

	cleanup_mm(&mm, &vmi);
	return true;
}

int main(void)
{
	int num_tests = 0, num_fail = 0;

	maple_tree_init();

#define TEST(name)							\
	do {								\
		num_tests++;						\
		if (!test_##name()) {					\
			num_fail++;					\
			fprintf(stderr, "Test " #name " FAILED\n");	\
		}							\
	} while (0)

	/* Very simple tests to kick the tyres. */
	TEST(simple_merge);
	TEST(simple_modify);
	TEST(simple_expand);
	TEST(simple_shrink);

	TEST(merge_new);
	TEST(vma_merge_special_flags);
	TEST(vma_merge_with_close);
	TEST(vma_merge_new_with_close);
	TEST(merge_existing);
	TEST(anon_vma_non_mergeable);
	TEST(dup_anon_vma);
	TEST(vmi_prealloc_fail);
	TEST(merge_extend);
	TEST(copy_vma);
	TEST(expand_only_mode);

	TEST(mmap_region_basic);

#undef TEST

	printf("%d tests run, %d passed, %d failed.\n",
	       num_tests, num_tests - num_fail, num_fail);

	return num_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
