// SPDX-License-Identifier: GPL-2.0-or-later

static bool test_copy_vma(void)
{
	vm_flags_t vm_flags = VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE;
	struct mm_struct mm = {};
	bool need_locks = false;
	VMA_ITERATOR(vmi, &mm, 0);
	struct vm_area_struct *vma, *vma_new, *vma_next;

	/* Move backwards and do not merge. */

	vma = alloc_and_link_vma(&mm, 0x3000, 0x5000, 3, vm_flags);
	vma_new = copy_vma(&vma, 0, 0x2000, 0, &need_locks);
	ASSERT_NE(vma_new, vma);
	ASSERT_EQ(vma_new->vm_start, 0);
	ASSERT_EQ(vma_new->vm_end, 0x2000);
	ASSERT_EQ(vma_new->vm_pgoff, 0);
	vma_assert_attached(vma_new);

	cleanup_mm(&mm, &vmi);

	/* Move a VMA into position next to another and merge the two. */

	vma = alloc_and_link_vma(&mm, 0, 0x2000, 0, vm_flags);
	vma_next = alloc_and_link_vma(&mm, 0x6000, 0x8000, 6, vm_flags);
	vma_new = copy_vma(&vma, 0x4000, 0x2000, 4, &need_locks);
	vma_assert_attached(vma_new);

	ASSERT_EQ(vma_new, vma_next);

	cleanup_mm(&mm, &vmi);
	return true;
}

static void run_vma_tests(int *num_tests, int *num_fail)
{
	TEST(copy_vma);
}
