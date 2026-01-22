// SPDX-License-Identifier: GPL-2.0-or-later

static bool compare_legacy_flags(vm_flags_t legacy_flags, vma_flags_t flags)
{
	const unsigned long legacy_val = legacy_flags;
	/* The lower word should contain the precise same value. */
	const unsigned long flags_lower = flags.__vma_flags[0];
#if NUM_VMA_FLAGS > BITS_PER_LONG
	int i;

	/* All bits in higher flag values should be zero. */
	for (i = 1; i < NUM_VMA_FLAGS / BITS_PER_LONG; i++) {
		if (flags.__vma_flags[i] != 0)
			return false;
	}
#endif

	static_assert(sizeof(legacy_flags) == sizeof(unsigned long));

	return legacy_val == flags_lower;
}

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

static bool test_vma_flags_unchanged(void)
{
	vma_flags_t flags = EMPTY_VMA_FLAGS;
	vm_flags_t legacy_flags = 0;
	int bit;
	struct vm_area_struct vma;
	struct vm_area_desc desc;


	vma.flags = EMPTY_VMA_FLAGS;
	desc.vma_flags = EMPTY_VMA_FLAGS;

	for (bit = 0; bit < BITS_PER_LONG; bit++) {
		vma_flags_t mask = mk_vma_flags(bit);

		legacy_flags |= (1UL << bit);

		/* Individual flags. */
		vma_flags_set(&flags, bit);
		ASSERT_TRUE(compare_legacy_flags(legacy_flags, flags));

		/* Via mask. */
		vma_flags_set_mask(&flags, mask);
		ASSERT_TRUE(compare_legacy_flags(legacy_flags, flags));

		/* Same for VMA. */
		vma_set_flags(&vma, bit);
		ASSERT_TRUE(compare_legacy_flags(legacy_flags, vma.flags));
		vma_set_flags_mask(&vma, mask);
		ASSERT_TRUE(compare_legacy_flags(legacy_flags, vma.flags));

		/* Same for VMA descriptor. */
		vma_desc_set_flags(&desc, bit);
		ASSERT_TRUE(compare_legacy_flags(legacy_flags, desc.vma_flags));
		vma_desc_set_flags_mask(&desc, mask);
		ASSERT_TRUE(compare_legacy_flags(legacy_flags, desc.vma_flags));
	}

	return true;
}

static bool test_vma_flags_cleared(void)
{
	const vma_flags_t empty = EMPTY_VMA_FLAGS;
	vma_flags_t flags;
	int i;

	/* Set all bits high. */
	memset(&flags, 1, sizeof(flags));
	/* Try to clear. */
	vma_flags_clear_all(&flags);
	/* Equal to EMPTY_VMA_FLAGS? */
	ASSERT_EQ(memcmp(&empty, &flags, sizeof(flags)), 0);
	/* Make sure every unsigned long entry in bitmap array zero. */
	for (i = 0; i < sizeof(flags) / BITS_PER_LONG; i++) {
		const unsigned long val = flags.__vma_flags[i];

		ASSERT_EQ(val, 0);
	}

	return true;
}

/*
 * Assert that VMA flag functions that operate at the system word level function
 * correctly.
 */
static bool test_vma_flags_word(void)
{
	vma_flags_t flags = EMPTY_VMA_FLAGS;
	const vma_flags_t comparison =
		mk_vma_flags(VMA_READ_BIT, VMA_WRITE_BIT, 64, 65);

	/* Set some custom high flags. */
	vma_flags_set(&flags, 64, 65);
	/* Now overwrite the first word. */
	vma_flags_overwrite_word(&flags, VM_READ | VM_WRITE);
	/* Ensure they are equal. */
	ASSERT_EQ(memcmp(&flags, &comparison, sizeof(flags)), 0);

	flags = EMPTY_VMA_FLAGS;
	vma_flags_set(&flags, 64, 65);

	/* Do the same with the _once() equivalent. */
	vma_flags_overwrite_word_once(&flags, VM_READ | VM_WRITE);
	ASSERT_EQ(memcmp(&flags, &comparison, sizeof(flags)), 0);

	flags = EMPTY_VMA_FLAGS;
	vma_flags_set(&flags, 64, 65);

	/* Make sure we can set a word without disturbing other bits. */
	vma_flags_set(&flags, VMA_WRITE_BIT);
	vma_flags_set_word(&flags, VM_READ);
	ASSERT_EQ(memcmp(&flags, &comparison, sizeof(flags)), 0);

	flags = EMPTY_VMA_FLAGS;
	vma_flags_set(&flags, 64, 65);

	/* Make sure we can clear a word without disturbing other bits. */
	vma_flags_set(&flags, VMA_READ_BIT, VMA_WRITE_BIT, VMA_EXEC_BIT);
	vma_flags_clear_word(&flags, VM_EXEC);
	ASSERT_EQ(memcmp(&flags, &comparison, sizeof(flags)), 0);

	return true;
}

/* Ensure that vma_flags_test() and friends works correctly. */
static bool test_vma_flags_test(void)
{
	const vma_flags_t flags = mk_vma_flags(VMA_READ_BIT, VMA_WRITE_BIT,
					       VMA_EXEC_BIT, 64, 65);
	struct vm_area_struct vma;
	struct vm_area_desc desc;

	vma.flags = flags;
	desc.vma_flags = flags;

#define do_test(...)						\
	ASSERT_TRUE(vma_flags_test(&flags, __VA_ARGS__));	\
	ASSERT_TRUE(vma_desc_test_flags(&desc, __VA_ARGS__))

#define do_test_all_true(...)					\
	ASSERT_TRUE(vma_flags_test_all(&flags, __VA_ARGS__));	\
	ASSERT_TRUE(vma_test_all_flags(&vma, __VA_ARGS__))

#define do_test_all_false(...)					\
	ASSERT_FALSE(vma_flags_test_all(&flags, __VA_ARGS__));	\
	ASSERT_FALSE(vma_test_all_flags(&vma, __VA_ARGS__))

	/*
	 * Testing for some flags that are present, some that are not - should
	 * pass. ANY flags matching should work.
	 */
	do_test(VMA_READ_BIT, VMA_MAYREAD_BIT, VMA_SEQ_READ_BIT);
	/* However, the ...test_all() variant should NOT pass. */
	do_test_all_false(VMA_READ_BIT, VMA_MAYREAD_BIT, VMA_SEQ_READ_BIT);
	/* But should pass for flags present. */
	do_test_all_true(VMA_READ_BIT, VMA_WRITE_BIT, VMA_EXEC_BIT, 64, 65);
	/* Also subsets... */
	do_test_all_true(VMA_READ_BIT, VMA_WRITE_BIT, VMA_EXEC_BIT, 64);
	do_test_all_true(VMA_READ_BIT, VMA_WRITE_BIT, VMA_EXEC_BIT);
	do_test_all_true(VMA_READ_BIT, VMA_WRITE_BIT);
	do_test_all_true(VMA_READ_BIT);
	/*
	 * Check _mask variant. We don't need to test extensively as macro
	 * helper is the equivalent.
	 */
	ASSERT_TRUE(vma_flags_test_mask(&flags, flags));
	ASSERT_TRUE(vma_flags_test_all_mask(&flags, flags));

	/* Single bits. */
	do_test(VMA_READ_BIT);
	do_test(VMA_WRITE_BIT);
	do_test(VMA_EXEC_BIT);
#if NUM_VMA_FLAG_BITS > 64
	do_test(64);
	do_test(65);
#endif

	/* Two bits. */
	do_test(VMA_READ_BIT, VMA_WRITE_BIT);
	do_test(VMA_READ_BIT, VMA_EXEC_BIT);
	do_test(VMA_WRITE_BIT, VMA_EXEC_BIT);
	/* Ordering shouldn't matter. */
	do_test(VMA_WRITE_BIT, VMA_READ_BIT);
	do_test(VMA_EXEC_BIT, VMA_READ_BIT);
	do_test(VMA_EXEC_BIT, VMA_WRITE_BIT);
#if NUM_VMA_FLAG_BITS > 64
	do_test(VMA_READ_BIT, 64);
	do_test(VMA_WRITE_BIT, 64);
	do_test(64, VMA_READ_BIT);
	do_test(64, VMA_WRITE_BIT);
	do_test(VMA_READ_BIT, 65);
	do_test(VMA_WRITE_BIT, 65);
	do_test(65, VMA_READ_BIT);
	do_test(65, VMA_WRITE_BIT);
#endif
	/* Three bits. */
	do_test(VMA_READ_BIT, VMA_WRITE_BIT, VMA_EXEC_BIT);
#if NUM_VMA_FLAG_BITS > 64
	/* No need to consider every single permutation. */
	do_test(VMA_READ_BIT, VMA_WRITE_BIT, 64);
	do_test(VMA_READ_BIT, VMA_WRITE_BIT, 65);

	/* Four bits. */
	do_test(VMA_READ_BIT, VMA_WRITE_BIT, VMA_EXEC_BIT, 64);
	do_test(VMA_READ_BIT, VMA_WRITE_BIT, VMA_EXEC_BIT, 65);

	/* Five bits. */
	do_test(VMA_READ_BIT, VMA_WRITE_BIT, VMA_EXEC_BIT, 64, 65);
#endif

#undef do_test
#undef do_test_all_true
#undef do_test_all_false

	return true;
}

/* Ensure that vma_flags_clear() and friends works correctly. */
static bool test_vma_flags_clear(void)
{
	vma_flags_t flags = mk_vma_flags(VMA_READ_BIT, VMA_WRITE_BIT,
					 VMA_EXEC_BIT, 64, 65);
	vma_flags_t mask = mk_vma_flags(VMA_EXEC_BIT, 64);
	struct vm_area_struct vma;
	struct vm_area_desc desc;

	vma.flags = flags;
	desc.vma_flags = flags;

	/* Cursory check of _mask() variant, as the helper macros imply. */
	vma_flags_clear_mask(&flags, mask);
	vma_flags_clear_mask(&vma.flags, mask);
	vma_desc_clear_flags_mask(&desc, mask);
	ASSERT_FALSE(vma_flags_test(&flags, VMA_EXEC_BIT, 64));
	ASSERT_FALSE(vma_flags_test(&vma.flags, VMA_EXEC_BIT, 64));
	ASSERT_FALSE(vma_desc_test_flags(&desc, VMA_EXEC_BIT, 64));
	/* Reset. */
	vma_flags_set(&flags, VMA_EXEC_BIT, 64);
	vma_set_flags(&vma, VMA_EXEC_BIT, 64);
	vma_desc_set_flags(&desc, VMA_EXEC_BIT, 64);

	/*
	 * Clear the flags and assert clear worked, then reset flags back to
	 * include specified flags.
	 */
#define do_test_and_reset(...)					\
	vma_flags_clear(&flags, __VA_ARGS__);			\
	vma_flags_clear(&vma.flags, __VA_ARGS__);		\
	vma_desc_clear_flags(&desc, __VA_ARGS__);		\
	ASSERT_FALSE(vma_flags_test(&flags, __VA_ARGS__));	\
	ASSERT_FALSE(vma_flags_test(&vma.flags, __VA_ARGS__));	\
	ASSERT_FALSE(vma_desc_test_flags(&desc, __VA_ARGS__));	\
	vma_flags_set(&flags, __VA_ARGS__);			\
	vma_set_flags(&vma, __VA_ARGS__);			\
	vma_desc_set_flags(&desc, __VA_ARGS__)

	/* Single flags. */
	do_test_and_reset(VMA_READ_BIT);
	do_test_and_reset(VMA_WRITE_BIT);
	do_test_and_reset(VMA_EXEC_BIT);
	do_test_and_reset(64);
	do_test_and_reset(65);

	/* Two flags, in different orders. */
	do_test_and_reset(VMA_READ_BIT, VMA_WRITE_BIT);
	do_test_and_reset(VMA_READ_BIT, VMA_EXEC_BIT);
	do_test_and_reset(VMA_READ_BIT, 64);
	do_test_and_reset(VMA_READ_BIT, 65);
	do_test_and_reset(VMA_WRITE_BIT, VMA_READ_BIT);
	do_test_and_reset(VMA_WRITE_BIT, VMA_EXEC_BIT);
	do_test_and_reset(VMA_WRITE_BIT, 64);
	do_test_and_reset(VMA_WRITE_BIT, 65);
	do_test_and_reset(VMA_EXEC_BIT, VMA_READ_BIT);
	do_test_and_reset(VMA_EXEC_BIT, VMA_WRITE_BIT);
	do_test_and_reset(VMA_EXEC_BIT, 64);
	do_test_and_reset(VMA_EXEC_BIT, 65);
	do_test_and_reset(64, VMA_READ_BIT);
	do_test_and_reset(64, VMA_WRITE_BIT);
	do_test_and_reset(64, VMA_EXEC_BIT);
	do_test_and_reset(64, 65);
	do_test_and_reset(65, VMA_READ_BIT);
	do_test_and_reset(65, VMA_WRITE_BIT);
	do_test_and_reset(65, VMA_EXEC_BIT);
	do_test_and_reset(65, 64);

	/* Three flags. */

#undef do_test_some_missing
#undef do_test_and_reset

	return true;
}

static void run_vma_tests(int *num_tests, int *num_fail)
{
	TEST(copy_vma);
	TEST(vma_flags_unchanged);
	TEST(vma_flags_cleared);
	TEST(vma_flags_word);
	TEST(vma_flags_test);
	TEST(vma_flags_clear);
}
