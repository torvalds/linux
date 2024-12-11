// SPDX-License-Identifier: GPL-2.0+
/*
 * Test cases for API provided by resource.c and ioport.h
 */

#include <kunit/test.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sizes.h>
#include <linux/mm.h>

#define R0_START	0x0000
#define R0_END		0xffff
#define R1_START	0x1234
#define R1_END		0x2345
#define R2_START	0x4567
#define R2_END		0x5678
#define R3_START	0x6789
#define R3_END		0x789a
#define R4_START	0x2000
#define R4_END		0x7000

static struct resource r0 = { .start = R0_START, .end = R0_END };
static struct resource r1 = { .start = R1_START, .end = R1_END };
static struct resource r2 = { .start = R2_START, .end = R2_END };
static struct resource r3 = { .start = R3_START, .end = R3_END };
static struct resource r4 = { .start = R4_START, .end = R4_END };

struct result {
	struct resource *r1;
	struct resource *r2;
	struct resource r;
	bool ret;
};

static struct result results_for_union[] = {
	{
		.r1 = &r1, .r2 = &r0, .r.start = R0_START, .r.end = R0_END, .ret = true,
	}, {
		.r1 = &r2, .r2 = &r0, .r.start = R0_START, .r.end = R0_END, .ret = true,
	}, {
		.r1 = &r3, .r2 = &r0, .r.start = R0_START, .r.end = R0_END, .ret = true,
	}, {
		.r1 = &r4, .r2 = &r0, .r.start = R0_START, .r.end = R0_END, .ret = true,
	}, {
		.r1 = &r2, .r2 = &r1, .ret = false,
	}, {
		.r1 = &r3, .r2 = &r1, .ret = false,
	}, {
		.r1 = &r4, .r2 = &r1, .r.start = R1_START, .r.end = R4_END, .ret = true,
	}, {
		.r1 = &r2, .r2 = &r3, .ret = false,
	}, {
		.r1 = &r2, .r2 = &r4, .r.start = R4_START, .r.end = R4_END, .ret = true,
	}, {
		.r1 = &r3, .r2 = &r4, .r.start = R4_START, .r.end = R3_END, .ret = true,
	},
};

static struct result results_for_intersection[] = {
	{
		.r1 = &r1, .r2 = &r0, .r.start = R1_START, .r.end = R1_END, .ret = true,
	}, {
		.r1 = &r2, .r2 = &r0, .r.start = R2_START, .r.end = R2_END, .ret = true,
	}, {
		.r1 = &r3, .r2 = &r0, .r.start = R3_START, .r.end = R3_END, .ret = true,
	}, {
		.r1 = &r4, .r2 = &r0, .r.start = R4_START, .r.end = R4_END, .ret = true,
	}, {
		.r1 = &r2, .r2 = &r1, .ret = false,
	}, {
		.r1 = &r3, .r2 = &r1, .ret = false,
	}, {
		.r1 = &r4, .r2 = &r1, .r.start = R4_START, .r.end = R1_END, .ret = true,
	}, {
		.r1 = &r2, .r2 = &r3, .ret = false,
	}, {
		.r1 = &r2, .r2 = &r4, .r.start = R2_START, .r.end = R2_END, .ret = true,
	}, {
		.r1 = &r3, .r2 = &r4, .r.start = R3_START, .r.end = R4_END, .ret = true,
	},
};

static void resource_do_test(struct kunit *test, bool ret, struct resource *r,
			     bool exp_ret, struct resource *exp_r,
			     struct resource *r1, struct resource *r2)
{
	KUNIT_EXPECT_EQ_MSG(test, ret, exp_ret, "Resources %pR %pR", r1, r2);
	KUNIT_EXPECT_EQ_MSG(test, r->start, exp_r->start, "Start elements are not equal");
	KUNIT_EXPECT_EQ_MSG(test, r->end, exp_r->end, "End elements are not equal");
}

static void resource_do_union_test(struct kunit *test, struct result *r)
{
	struct resource result;
	bool ret;

	memset(&result, 0, sizeof(result));
	ret = resource_union(r->r1, r->r2, &result);
	resource_do_test(test, ret, &result, r->ret, &r->r, r->r1, r->r2);

	memset(&result, 0, sizeof(result));
	ret = resource_union(r->r2, r->r1, &result);
	resource_do_test(test, ret, &result, r->ret, &r->r, r->r2, r->r1);
}

static void resource_test_union(struct kunit *test)
{
	struct result *r = results_for_union;
	unsigned int i = 0;

	do {
		resource_do_union_test(test, &r[i]);
	} while (++i < ARRAY_SIZE(results_for_union));
}

static void resource_do_intersection_test(struct kunit *test, struct result *r)
{
	struct resource result;
	bool ret;

	memset(&result, 0, sizeof(result));
	ret = resource_intersection(r->r1, r->r2, &result);
	resource_do_test(test, ret, &result, r->ret, &r->r, r->r1, r->r2);

	memset(&result, 0, sizeof(result));
	ret = resource_intersection(r->r2, r->r1, &result);
	resource_do_test(test, ret, &result, r->ret, &r->r, r->r2, r->r1);
}

static void resource_test_intersection(struct kunit *test)
{
	struct result *r = results_for_intersection;
	unsigned int i = 0;

	do {
		resource_do_intersection_test(test, &r[i]);
	} while (++i < ARRAY_SIZE(results_for_intersection));
}

/*
 * The test resource tree for region_intersects() test:
 *
 * BASE-BASE+1M-1 : Test System RAM 0
 *		  # hole 0 (BASE+1M-BASE+2M)
 * BASE+2M-BASE+3M-1 : Test CXL Window 0
 * BASE+3M-BASE+4M-1 : Test System RAM 1
 * BASE+4M-BASE+7M-1 : Test CXL Window 1
 *   BASE+4M-BASE+5M-1 : Test System RAM 2
 *     BASE+4M+128K-BASE+4M+256K-1: Test Code
 *   BASE+5M-BASE+6M-1 : Test System RAM 3
 */
#define RES_TEST_RAM0_OFFSET	0
#define RES_TEST_RAM0_SIZE	SZ_1M
#define RES_TEST_HOLE0_OFFSET	(RES_TEST_RAM0_OFFSET + RES_TEST_RAM0_SIZE)
#define RES_TEST_HOLE0_SIZE	SZ_1M
#define RES_TEST_WIN0_OFFSET	(RES_TEST_HOLE0_OFFSET + RES_TEST_HOLE0_SIZE)
#define RES_TEST_WIN0_SIZE	SZ_1M
#define RES_TEST_RAM1_OFFSET	(RES_TEST_WIN0_OFFSET + RES_TEST_WIN0_SIZE)
#define RES_TEST_RAM1_SIZE	SZ_1M
#define RES_TEST_WIN1_OFFSET	(RES_TEST_RAM1_OFFSET + RES_TEST_RAM1_SIZE)
#define RES_TEST_WIN1_SIZE	(SZ_1M * 3)
#define RES_TEST_RAM2_OFFSET	RES_TEST_WIN1_OFFSET
#define RES_TEST_RAM2_SIZE	SZ_1M
#define RES_TEST_CODE_OFFSET	(RES_TEST_RAM2_OFFSET + SZ_128K)
#define RES_TEST_CODE_SIZE	SZ_128K
#define RES_TEST_RAM3_OFFSET	(RES_TEST_RAM2_OFFSET + RES_TEST_RAM2_SIZE)
#define RES_TEST_RAM3_SIZE	SZ_1M
#define RES_TEST_TOTAL_SIZE	((RES_TEST_WIN1_OFFSET + RES_TEST_WIN1_SIZE))

KUNIT_DEFINE_ACTION_WRAPPER(kfree_wrapper, kfree, const void *);

static void remove_free_resource(void *ctx)
{
	struct resource *res = (struct resource *)ctx;

	remove_resource(res);
	kfree(res);
}

static void resource_test_add_action_or_abort(
	struct kunit *test, void (*action)(void *), void *ctx)
{
	KUNIT_ASSERT_EQ_MSG(test, 0,
			    kunit_add_action_or_reset(test, action, ctx),
			    "Fail to add action");
}

static void resource_test_request_region(struct kunit *test, struct resource *parent,
					 resource_size_t start, resource_size_t size,
					 const char *name, unsigned long flags)
{
	struct resource *res;

	res = __request_region(parent, start, size, name, flags);
	KUNIT_ASSERT_NOT_NULL(test, res);
	resource_test_add_action_or_abort(test, remove_free_resource, res);
}

static void resource_test_insert_resource(struct kunit *test, struct resource *parent,
					  resource_size_t start, resource_size_t size,
					  const char *name, unsigned long flags)
{
	struct resource *res;

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, res);

	res->name = name;
	res->start = start;
	res->end = start + size - 1;
	res->flags = flags;
	if (insert_resource(parent, res)) {
		resource_test_add_action_or_abort(test, kfree_wrapper, res);
		KUNIT_FAIL_AND_ABORT(test, "Fail to insert resource %pR\n", res);
	}

	resource_test_add_action_or_abort(test, remove_free_resource, res);
}

static void resource_test_region_intersects(struct kunit *test)
{
	unsigned long flags =  IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
	struct resource *parent;
	resource_size_t start;

	/* Find an iomem_resource hole to hold test resources */
	parent = alloc_free_mem_region(&iomem_resource, RES_TEST_TOTAL_SIZE, SZ_1M,
				       "test resources");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);
	start = parent->start;
	resource_test_add_action_or_abort(test, remove_free_resource, parent);

	resource_test_request_region(test, parent, start + RES_TEST_RAM0_OFFSET,
				     RES_TEST_RAM0_SIZE, "Test System RAM 0", flags);
	resource_test_insert_resource(test, parent, start + RES_TEST_WIN0_OFFSET,
				      RES_TEST_WIN0_SIZE, "Test CXL Window 0",
				      IORESOURCE_MEM);
	resource_test_request_region(test, parent, start + RES_TEST_RAM1_OFFSET,
				     RES_TEST_RAM1_SIZE, "Test System RAM 1", flags);
	resource_test_insert_resource(test, parent, start + RES_TEST_WIN1_OFFSET,
				      RES_TEST_WIN1_SIZE, "Test CXL Window 1",
				      IORESOURCE_MEM);
	resource_test_request_region(test, parent, start + RES_TEST_RAM2_OFFSET,
				     RES_TEST_RAM2_SIZE, "Test System RAM 2", flags);
	resource_test_insert_resource(test, parent, start + RES_TEST_CODE_OFFSET,
				      RES_TEST_CODE_SIZE, "Test Code", flags);
	resource_test_request_region(test, parent, start + RES_TEST_RAM3_OFFSET,
				     RES_TEST_RAM3_SIZE, "Test System RAM 3", flags);
	kunit_release_action(test, remove_free_resource, parent);

	KUNIT_EXPECT_EQ(test, REGION_INTERSECTS,
			region_intersects(start + RES_TEST_RAM0_OFFSET, PAGE_SIZE,
					  IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE));
	KUNIT_EXPECT_EQ(test, REGION_INTERSECTS,
			region_intersects(start + RES_TEST_RAM0_OFFSET +
					  RES_TEST_RAM0_SIZE - PAGE_SIZE, 2 * PAGE_SIZE,
					  IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE));
	KUNIT_EXPECT_EQ(test, REGION_DISJOINT,
			region_intersects(start + RES_TEST_HOLE0_OFFSET, PAGE_SIZE,
					  IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE));
	KUNIT_EXPECT_EQ(test, REGION_DISJOINT,
			region_intersects(start + RES_TEST_HOLE0_OFFSET +
					  RES_TEST_HOLE0_SIZE - PAGE_SIZE, 2 * PAGE_SIZE,
					  IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE));
	KUNIT_EXPECT_EQ(test, REGION_MIXED,
			region_intersects(start + RES_TEST_WIN0_OFFSET +
					  RES_TEST_WIN0_SIZE - PAGE_SIZE, 2 * PAGE_SIZE,
					  IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE));
	KUNIT_EXPECT_EQ(test, REGION_INTERSECTS,
			region_intersects(start + RES_TEST_RAM1_OFFSET +
					  RES_TEST_RAM1_SIZE - PAGE_SIZE, 2 * PAGE_SIZE,
					  IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE));
	KUNIT_EXPECT_EQ(test, REGION_INTERSECTS,
			region_intersects(start + RES_TEST_RAM2_OFFSET +
					  RES_TEST_RAM2_SIZE - PAGE_SIZE, 2 * PAGE_SIZE,
					  IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE));
	KUNIT_EXPECT_EQ(test, REGION_INTERSECTS,
			region_intersects(start + RES_TEST_CODE_OFFSET, PAGE_SIZE,
					  IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE));
	KUNIT_EXPECT_EQ(test, REGION_INTERSECTS,
			region_intersects(start + RES_TEST_RAM2_OFFSET,
					  RES_TEST_RAM2_SIZE + PAGE_SIZE,
					  IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE));
	KUNIT_EXPECT_EQ(test, REGION_MIXED,
			region_intersects(start + RES_TEST_RAM3_OFFSET,
					  RES_TEST_RAM3_SIZE + PAGE_SIZE,
					  IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE));
}

static struct kunit_case resource_test_cases[] = {
	KUNIT_CASE(resource_test_union),
	KUNIT_CASE(resource_test_intersection),
	KUNIT_CASE(resource_test_region_intersects),
	{}
};

static struct kunit_suite resource_test_suite = {
	.name = "resource",
	.test_cases = resource_test_cases,
};
kunit_test_suite(resource_test_suite);

MODULE_DESCRIPTION("I/O Port & Memory Resource manager unit tests");
MODULE_LICENSE("GPL");
