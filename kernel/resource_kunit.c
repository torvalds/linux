// SPDX-License-Identifier: GPL-2.0+
/*
 * Test cases for API provided by resource.c and ioport.h
 */

#include <kunit/test.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/string.h>

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

static struct kunit_case resource_test_cases[] = {
	KUNIT_CASE(resource_test_union),
	KUNIT_CASE(resource_test_intersection),
	{}
};

static struct kunit_suite resource_test_suite = {
	.name = "resource",
	.test_cases = resource_test_cases,
};
kunit_test_suite(resource_test_suite);

MODULE_DESCRIPTION("I/O Port & Memory Resource manager unit tests");
MODULE_LICENSE("GPL");
