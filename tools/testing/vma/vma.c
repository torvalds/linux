// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "maple-shared.h"
#include "vma_internal.h"

/*
 * Directly import the VMA implementation here. Our vma_internal.h wrapper
 * provides userland-equivalent functionality for everything vma.c uses.
 */
#include "../../../mm/vma.c"

const struct vm_operations_struct vma_dummy_vm_ops;

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

static bool test_simple_merge(void)
{
	struct vm_area_struct *vma;
	unsigned long flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	struct vm_area_struct *vma_left = alloc_vma(&mm, 0, 0x1000, 0, flags);
	struct vm_area_struct *vma_middle = alloc_vma(&mm, 0x1000, 0x2000, 1, flags);
	struct vm_area_struct *vma_right = alloc_vma(&mm, 0x2000, 0x3000, 2, flags);
	VMA_ITERATOR(vmi, &mm, 0x1000);

	ASSERT_FALSE(vma_link(&mm, vma_left));
	ASSERT_FALSE(vma_link(&mm, vma_middle));
	ASSERT_FALSE(vma_link(&mm, vma_right));

	vma = vma_merge_new_vma(&vmi, vma_left, vma_middle, 0x1000,
				0x2000, 1);
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

	ASSERT_FALSE(vma_link(&mm, vma));

	ASSERT_FALSE(vma_expand(&vmi, vma, 0, 0x3000, 0, NULL));

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

	TEST(simple_merge);
	TEST(simple_modify);
	TEST(simple_expand);
	TEST(simple_shrink);

#undef TEST

	printf("%d tests run, %d passed, %d failed.\n",
	       num_tests, num_tests - num_fail, num_fail);

	return num_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
