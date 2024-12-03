/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data Access Monitor Unit Tests
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates.  All rights reserved.
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#ifdef CONFIG_DAMON_VADDR_KUNIT_TEST

#ifndef _DAMON_VADDR_TEST_H
#define _DAMON_VADDR_TEST_H

#include <kunit/test.h>

static int __link_vmas(struct maple_tree *mt, struct vm_area_struct *vmas,
			ssize_t nr_vmas)
{
	int i, ret = -ENOMEM;
	MA_STATE(mas, mt, 0, 0);

	if (!nr_vmas)
		return 0;

	mas_lock(&mas);
	for (i = 0; i < nr_vmas; i++) {
		mas_set_range(&mas, vmas[i].vm_start, vmas[i].vm_end - 1);
		if (mas_store_gfp(&mas, &vmas[i], GFP_KERNEL))
			goto failed;
	}

	ret = 0;
failed:
	mas_unlock(&mas);
	return ret;
}

/*
 * Test __damon_va_three_regions() function
 *
 * In case of virtual memory address spaces monitoring, DAMON converts the
 * complex and dynamic memory mappings of each target task to three
 * discontiguous regions which cover every mapped areas.  However, the three
 * regions should not include the two biggest unmapped areas in the original
 * mapping, because the two biggest areas are normally the areas between 1)
 * heap and the mmap()-ed regions, and 2) the mmap()-ed regions and stack.
 * Because these two unmapped areas are very huge but obviously never accessed,
 * covering the region is just a waste.
 *
 * '__damon_va_three_regions() receives an address space of a process.  It
 * first identifies the start of mappings, end of mappings, and the two biggest
 * unmapped areas.  After that, based on the information, it constructs the
 * three regions and returns.  For more detail, refer to the comment of
 * 'damon_init_regions_of()' function definition in 'mm/damon.c' file.
 *
 * For example, suppose virtual address ranges of 10-20, 20-25, 200-210,
 * 210-220, 300-305, and 307-330 (Other comments represent this mappings in
 * more short form: 10-20-25, 200-210-220, 300-305, 307-330) of a process are
 * mapped.  To cover every mappings, the three regions should start with 10,
 * and end with 305.  The process also has three unmapped areas, 25-200,
 * 220-300, and 305-307.  Among those, 25-200 and 220-300 are the biggest two
 * unmapped areas, and thus it should be converted to three regions of 10-25,
 * 200-220, and 300-330.
 */
static void damon_test_three_regions_in_vmas(struct kunit *test)
{
	static struct mm_struct mm;
	struct damon_addr_range regions[3] = {0};
	/* 10-20-25, 200-210-220, 300-305, 307-330 */
	struct vm_area_struct vmas[] = {
		(struct vm_area_struct) {.vm_start = 10, .vm_end = 20},
		(struct vm_area_struct) {.vm_start = 20, .vm_end = 25},
		(struct vm_area_struct) {.vm_start = 200, .vm_end = 210},
		(struct vm_area_struct) {.vm_start = 210, .vm_end = 220},
		(struct vm_area_struct) {.vm_start = 300, .vm_end = 305},
		(struct vm_area_struct) {.vm_start = 307, .vm_end = 330},
	};

	mt_init_flags(&mm.mm_mt, MT_FLAGS_ALLOC_RANGE | MT_FLAGS_USE_RCU);
	if (__link_vmas(&mm.mm_mt, vmas, ARRAY_SIZE(vmas)))
		kunit_skip(test, "Failed to create VMA tree");

	__damon_va_three_regions(&mm, regions);

	KUNIT_EXPECT_EQ(test, 10ul, regions[0].start);
	KUNIT_EXPECT_EQ(test, 25ul, regions[0].end);
	KUNIT_EXPECT_EQ(test, 200ul, regions[1].start);
	KUNIT_EXPECT_EQ(test, 220ul, regions[1].end);
	KUNIT_EXPECT_EQ(test, 300ul, regions[2].start);
	KUNIT_EXPECT_EQ(test, 330ul, regions[2].end);
}

static struct damon_region *__nth_region_of(struct damon_target *t, int idx)
{
	struct damon_region *r;
	unsigned int i = 0;

	damon_for_each_region(r, t) {
		if (i++ == idx)
			return r;
	}

	return NULL;
}

/*
 * Test 'damon_set_regions()'
 *
 * test			kunit object
 * regions		an array containing start/end addresses of current
 *			monitoring target regions
 * nr_regions		the number of the addresses in 'regions'
 * three_regions	The three regions that need to be applied now
 * expected		start/end addresses of monitoring target regions that
 *			'three_regions' are applied
 * nr_expected		the number of addresses in 'expected'
 *
 * The memory mapping of the target processes changes dynamically.  To follow
 * the change, DAMON periodically reads the mappings, simplifies it to the
 * three regions, and updates the monitoring target regions to fit in the three
 * regions.  The update of current target regions is the role of
 * 'damon_set_regions()'.
 *
 * This test passes the given target regions and the new three regions that
 * need to be applied to the function and check whether it updates the regions
 * as expected.
 */
static void damon_do_test_apply_three_regions(struct kunit *test,
				unsigned long *regions, int nr_regions,
				struct damon_addr_range *three_regions,
				unsigned long *expected, int nr_expected)
{
	struct damon_target *t;
	struct damon_region *r;
	int i;

	t = damon_new_target();
	for (i = 0; i < nr_regions / 2; i++) {
		r = damon_new_region(regions[i * 2], regions[i * 2 + 1]);
		damon_add_region(r, t);
	}

	damon_set_regions(t, three_regions, 3);

	for (i = 0; i < nr_expected / 2; i++) {
		r = __nth_region_of(t, i);
		KUNIT_EXPECT_EQ(test, r->ar.start, expected[i * 2]);
		KUNIT_EXPECT_EQ(test, r->ar.end, expected[i * 2 + 1]);
	}

	damon_destroy_target(t);
}

/*
 * This function test most common case where the three big regions are only
 * slightly changed.  Target regions should adjust their boundary (10-20-30,
 * 50-55, 70-80, 90-100) to fit with the new big regions or remove target
 * regions (57-79) that now out of the three regions.
 */
static void damon_test_apply_three_regions1(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-27, 45-55, 73-104 */
	struct damon_addr_range new_three_regions[3] = {
		(struct damon_addr_range){.start = 5, .end = 27},
		(struct damon_addr_range){.start = 45, .end = 55},
		(struct damon_addr_range){.start = 73, .end = 104} };
	/* 5-20-27, 45-55, 73-80-90-104 */
	unsigned long expected[] = {5, 20, 20, 27, 45, 55,
				73, 80, 80, 90, 90, 104};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

/*
 * Test slightly bigger change.  Similar to above, but the second big region
 * now require two target regions (50-55, 57-59) to be removed.
 */
static void damon_test_apply_three_regions2(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-27, 56-57, 65-104 */
	struct damon_addr_range new_three_regions[3] = {
		(struct damon_addr_range){.start = 5, .end = 27},
		(struct damon_addr_range){.start = 56, .end = 57},
		(struct damon_addr_range){.start = 65, .end = 104} };
	/* 5-20-27, 56-57, 65-80-90-104 */
	unsigned long expected[] = {5, 20, 20, 27, 56, 57,
				65, 80, 80, 90, 90, 104};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

/*
 * Test a big change.  The second big region has totally freed and mapped to
 * different area (50-59 -> 61-63).  The target regions which were in the old
 * second big region (50-55-57-59) should be removed and new target region
 * covering the second big region (61-63) should be created.
 */
static void damon_test_apply_three_regions3(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-27, 61-63, 65-104 */
	struct damon_addr_range new_three_regions[3] = {
		(struct damon_addr_range){.start = 5, .end = 27},
		(struct damon_addr_range){.start = 61, .end = 63},
		(struct damon_addr_range){.start = 65, .end = 104} };
	/* 5-20-27, 61-63, 65-80-90-104 */
	unsigned long expected[] = {5, 20, 20, 27, 61, 63,
				65, 80, 80, 90, 90, 104};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

/*
 * Test another big change.  Both of the second and third big regions (50-59
 * and 70-100) has totally freed and mapped to different area (30-32 and
 * 65-68).  The target regions which were in the old second and third big
 * regions should now be removed and new target regions covering the new second
 * and third big regions should be created.
 */
static void damon_test_apply_three_regions4(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-7, 30-32, 65-68 */
	struct damon_addr_range new_three_regions[3] = {
		(struct damon_addr_range){.start = 5, .end = 7},
		(struct damon_addr_range){.start = 30, .end = 32},
		(struct damon_addr_range){.start = 65, .end = 68} };
	/* expect 5-7, 30-32, 65-68 */
	unsigned long expected[] = {5, 7, 30, 32, 65, 68};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

static void damon_test_split_evenly_fail(struct kunit *test,
		unsigned long start, unsigned long end, unsigned int nr_pieces)
{
	struct damon_target *t = damon_new_target();
	struct damon_region *r = damon_new_region(start, end);

	damon_add_region(r, t);
	KUNIT_EXPECT_EQ(test,
			damon_va_evenly_split_region(t, r, nr_pieces), -EINVAL);
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 1u);

	damon_for_each_region(r, t) {
		KUNIT_EXPECT_EQ(test, r->ar.start, start);
		KUNIT_EXPECT_EQ(test, r->ar.end, end);
	}

	damon_free_target(t);
}

static void damon_test_split_evenly_succ(struct kunit *test,
	unsigned long start, unsigned long end, unsigned int nr_pieces)
{
	struct damon_target *t = damon_new_target();
	struct damon_region *r = damon_new_region(start, end);
	unsigned long expected_width = (end - start) / nr_pieces;
	unsigned long i = 0;

	damon_add_region(r, t);
	KUNIT_EXPECT_EQ(test,
			damon_va_evenly_split_region(t, r, nr_pieces), 0);
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), nr_pieces);

	damon_for_each_region(r, t) {
		if (i == nr_pieces - 1) {
			KUNIT_EXPECT_EQ(test,
				r->ar.start, start + i * expected_width);
			KUNIT_EXPECT_EQ(test, r->ar.end, end);
			break;
		}
		KUNIT_EXPECT_EQ(test,
				r->ar.start, start + i++ * expected_width);
		KUNIT_EXPECT_EQ(test, r->ar.end, start + i * expected_width);
	}
	damon_free_target(t);
}

static void damon_test_split_evenly(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, damon_va_evenly_split_region(NULL, NULL, 5),
			-EINVAL);

	damon_test_split_evenly_fail(test, 0, 100, 0);
	damon_test_split_evenly_succ(test, 0, 100, 10);
	damon_test_split_evenly_succ(test, 5, 59, 5);
	damon_test_split_evenly_succ(test, 4, 6, 1);
	damon_test_split_evenly_succ(test, 0, 3, 2);
	damon_test_split_evenly_fail(test, 5, 6, 2);
}

static struct kunit_case damon_test_cases[] = {
	KUNIT_CASE(damon_test_three_regions_in_vmas),
	KUNIT_CASE(damon_test_apply_three_regions1),
	KUNIT_CASE(damon_test_apply_three_regions2),
	KUNIT_CASE(damon_test_apply_three_regions3),
	KUNIT_CASE(damon_test_apply_three_regions4),
	KUNIT_CASE(damon_test_split_evenly),
	{},
};

static struct kunit_suite damon_test_suite = {
	.name = "damon-operations",
	.test_cases = damon_test_cases,
};
kunit_test_suite(damon_test_suite);

#endif /* _DAMON_VADDR_TEST_H */

#endif	/* CONFIG_DAMON_VADDR_KUNIT_TEST */
