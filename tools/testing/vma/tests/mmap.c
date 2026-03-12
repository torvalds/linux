// SPDX-License-Identifier: GPL-2.0-or-later

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

static void run_mmap_tests(int *num_tests, int *num_fail)
{
	TEST(mmap_region_basic);
}
