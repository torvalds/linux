// SPDX-License-Identifier: GPL-2.0-or-later

static bool test_mmap_region_basic(void)
{
	const vma_flags_t vma_flags = mk_vma_flags(VMA_READ_BIT, VMA_WRITE_BIT,
			VMA_MAYREAD_BIT, VMA_MAYWRITE_BIT);
	struct mm_struct mm = {};
	unsigned long addr;
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, &mm, 0);

	current->mm = &mm;

	/* Map at 0x300000, length 0x3000. */
	addr = __mmap_region(NULL, 0x300000, 0x3000, vma_flags, 0x300, NULL);
	ASSERT_EQ(addr, 0x300000);

	/* Map at 0x250000, length 0x3000. */
	addr = __mmap_region(NULL, 0x250000, 0x3000, vma_flags, 0x250, NULL);
	ASSERT_EQ(addr, 0x250000);

	/* Map at 0x303000, merging to 0x300000 of length 0x6000. */
	addr = __mmap_region(NULL, 0x303000, 0x3000, vma_flags, 0x303, NULL);
	ASSERT_EQ(addr, 0x303000);

	/* Map at 0x24d000, merging to 0x250000 of length 0x6000. */
	addr = __mmap_region(NULL, 0x24d000, 0x3000, vma_flags, 0x24d, NULL);
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

static bool mmap_region_fill_hole(bool merge)
{
	const vma_flags_t vma_flags = mk_vma_flags(VMA_READ_BIT, VMA_WRITE_BIT,
			VMA_MAYREAD_BIT, VMA_MAYWRITE_BIT, VMA_MAYEXEC_BIT);
	vma_flags_t middle_flags = vma_flags;
	struct mm_struct mm = {};
	struct vm_area_struct *vma;
	unsigned long addr;
	int count = 0;
	VMA_ITERATOR(vmi, &mm, 0);

	current->mm = &mm;
	if (!merge)
		vma_flags_set(&middle_flags, VMA_EXEC_BIT);

	/* Map at 0x300000, length 0x3000. */
	addr = __mmap_region(NULL, 0x300000, 0x3000, vma_flags, 0x300, NULL);
	ASSERT_EQ(addr, 0x300000);

	/* Map at 0x306000, length 0x3000, leaving a hole. */
	addr = __mmap_region(NULL, 0x306000, 0x3000, vma_flags, 0x306, NULL);
	ASSERT_EQ(addr, 0x306000);
	ASSERT_EQ(mm.map_count, 2);

	/* Map at 0x303000, length 0x3000, filling the hole. */
	addr = __mmap_region(NULL, 0x303000, 0x3000, middle_flags, 0x303, NULL);
	ASSERT_EQ(addr, 0x303000);
	ASSERT_EQ(mm.map_count, merge ? 1 : 3);

	vma_iter_set(&vmi, 0);
	for_each_vma(vmi, vma) {
		const unsigned long start = 0x300000 + count * 0x3000;
		const unsigned long end = merge ? 0x309000 : start + 0x3000;
		/* Only the middle VMA in the non-merge case has VMA_EXEC. */
		const bool is_middle_vma = count == 1;
		const bool expect_exec_vma = is_middle_vma && !merge;

		ASSERT_EQ(vma->vm_start, start);
		ASSERT_EQ(vma->vm_end, end);
		ASSERT_EQ(vma_start_pgoff(vma), start >> PAGE_SHIFT);
		ASSERT_EQ(vma_start_anon_pgoff(vma), start >> PAGE_SHIFT);

		ASSERT_TRUE(vma_test_all(vma, VMA_READ_BIT, VMA_WRITE_BIT,
					 VMA_MAYREAD_BIT, VMA_MAYWRITE_BIT,
					 VMA_MAYEXEC_BIT));
		ASSERT_EQ(vma_test(vma, VMA_EXEC_BIT), expect_exec_vma);

		count++;
	}

	ASSERT_EQ(count, mm.map_count);

	ASSERT_EQ(cleanup_mm(&mm, &vmi), count);
	return true;
}

static bool test_mmap_region_fill_hole_merge(void)
{
	return mmap_region_fill_hole(true);
}

static bool test_mmap_region_fill_hole_flags_mismatch(void)
{
	return mmap_region_fill_hole(false);
}

static bool test_pure_anon_dev_zero(void)
{
	const vma_flags_t vma_flags = mk_vma_flags(VMA_READ_BIT, VMA_WRITE_BIT,
			VMA_MAYREAD_BIT, VMA_MAYWRITE_BIT);
	struct file file = {
		.f_op = &zero_fops,
	};
	struct mm_struct mm = {};
	struct vm_area_struct *vma;
	unsigned long addr;
	VMA_ITERATOR(vmi, &mm, 0);

	current->mm = &mm;

	/*
	 * Map a MAP_PRIVATE-/dev/zero mapping at address 0x300000 with a page
	 * offset of 0x10, which we expect to be reset to the anonymous page
	 * offset.
	 */
	addr = __mmap_region(&file, 0x300000, 0x3000, vma_flags, 0x10, NULL);
	ASSERT_EQ(addr, 0x300000);

	/* Assert that it truly is an anonymous mapping. */
	vma = vma_lookup(&mm, addr);
	ASSERT_NE(vma, NULL);
	ASSERT_TRUE(vma_is_anonymous(vma));
	ASSERT_EQ(vma->vm_file, NULL);
	ASSERT_EQ(vma->vm_private_data, NULL);
	/* Expect anonymous page offsets. */
	ASSERT_EQ(vma->vm_pgoff, 0x300);
	ASSERT_EQ(vma_start_anon_pgoff(vma), 0x300);

	cleanup_mm(&mm, &vmi);
	return true;
}

static void run_mmap_tests(int *num_tests, int *num_fail)
{
	TEST(mmap_region_basic);
	TEST(mmap_region_fill_hole_merge);
	TEST(mmap_region_fill_hole_flags_mismatch);
	TEST(pure_anon_dev_zero);
}
