// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/crash_core.h> // For struct crash_mem and struct range if defined there

// Helper to create and initialize crash_mem
static struct crash_mem *create_crash_mem(struct kunit *test, unsigned int max_ranges,
					  unsigned int nr_initial_ranges,
					  const struct range *initial_ranges)
{
	struct crash_mem *mem;
	size_t alloc_size;

	// Check if max_ranges can even hold initial_ranges
	if (max_ranges < nr_initial_ranges) {
		kunit_err(test, "max_ranges (%u) < nr_initial_ranges (%u)\n",
			  max_ranges, nr_initial_ranges);
		return NULL;
	}

	alloc_size = sizeof(struct crash_mem) + (size_t)max_ranges * sizeof(struct range);
	mem = kunit_kzalloc(test, alloc_size, GFP_KERNEL);
	if (!mem) {
		kunit_err(test, "Failed to allocate crash_mem\n");
		return NULL;
	}

	mem->max_nr_ranges = max_ranges;
	mem->nr_ranges = nr_initial_ranges;
	if (initial_ranges && nr_initial_ranges > 0) {
		memcpy(mem->ranges, initial_ranges,
		       nr_initial_ranges * sizeof(struct range));
	}

	return mem;
}

// Helper to compare ranges for assertions
static void assert_ranges_equal(struct kunit *test,
				const struct range *actual_ranges,
				unsigned int actual_nr_ranges,
				const struct range *expected_ranges,
				unsigned int expected_nr_ranges,
				const char *case_name)
{
	unsigned int i;

	KUNIT_ASSERT_EQ_MSG(test, expected_nr_ranges, actual_nr_ranges,
			    "%s: Number of ranges mismatch.", case_name);

	for (i = 0; i < expected_nr_ranges; i++) {
		KUNIT_ASSERT_EQ_MSG(test, expected_ranges[i].start, actual_ranges[i].start,
				    "%s: Range %u start mismatch.", case_name, i);
		KUNIT_ASSERT_EQ_MSG(test, expected_ranges[i].end, actual_ranges[i].end,
				    "%s: Range %u end mismatch.", case_name, i);
	}
}

// Structure for test parameters
struct exclude_test_param {
	const char *description;
	unsigned long long exclude_start;
	unsigned long long exclude_end;
	unsigned int initial_max_ranges;
	const struct range *initial_ranges;
	unsigned int initial_nr_ranges;
	const struct range *expected_ranges;
	unsigned int expected_nr_ranges;
	int expected_ret;
};

static void run_exclude_test_case(struct kunit *test, const struct exclude_test_param *params)
{
	struct crash_mem *mem;
	int ret;

	kunit_info(test, "%s", params->description);

	mem = create_crash_mem(test, params->initial_max_ranges,
			       params->initial_nr_ranges, params->initial_ranges);
	if (!mem)
		return; // Error already logged by create_crash_mem or kunit_kzalloc

	ret = crash_exclude_mem_range(mem, params->exclude_start, params->exclude_end);

	KUNIT_ASSERT_EQ_MSG(test, params->expected_ret, ret,
			    "%s: Return value mismatch.", params->description);

	if (params->expected_ret == 0) {
		assert_ranges_equal(test, mem->ranges, mem->nr_ranges,
				    params->expected_ranges, params->expected_nr_ranges,
				    params->description);
	} else {
		// If an error is expected, nr_ranges might still be relevant to check
		// depending on the exact point of failure. For ENOMEM on split,
		// nr_ranges shouldn't have changed.
		KUNIT_ASSERT_EQ_MSG(test, params->initial_nr_ranges,
				    mem->nr_ranges,
				    "%s: Number of ranges mismatch on error.",
				    params->description);
	}
}

/*
 * Test Strategy 1: One to-be-excluded range A and one existing range B.
 *
 * Exhaust all possibilities of the position of A regarding B.
 */

static const struct range single_range_b = { .start = 100, .end = 199 };

static const struct exclude_test_param exclude_single_range_test_data[] = {
	{
		.description = "1.1: A is left of B, no overlap",
		.exclude_start = 10, .exclude_end = 50,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = &single_range_b, .expected_nr_ranges = 1,
		.expected_ret = 0,
	},
	{
		.description = "1.2: A's right boundary touches B's left boundary",
		.exclude_start = 10, .exclude_end = 99,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = &single_range_b, .expected_nr_ranges = 1,
		.expected_ret = 0,
	},
	{
		.description = "1.3: A overlaps B's left part",
		.exclude_start = 50, .exclude_end = 149,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = (const struct range[]){{ .start = 150, .end = 199 }},
		.expected_nr_ranges = 1,
		.expected_ret = 0,
	},
	{
		.description = "1.4: A is completely inside B",
		.exclude_start = 120, .exclude_end = 179,
		.initial_max_ranges = 2, // Needs space for split
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = (const struct range[]){
			{ .start = 100, .end = 119 },
			{ .start = 180, .end = 199 }
		},
		.expected_nr_ranges = 2,
		.expected_ret = 0,
	},
	{
		.description = "1.5: A overlaps B's right part",
		.exclude_start = 150, .exclude_end = 249,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = (const struct range[]){{ .start = 100, .end = 149 }},
		.expected_nr_ranges = 1,
		.expected_ret = 0,
	},
	{
		.description = "1.6: A's left boundary touches B's right boundary",
		.exclude_start = 200, .exclude_end = 250,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = &single_range_b, .expected_nr_ranges = 1,
		.expected_ret = 0,
	},
	{
		.description = "1.7: A is right of B, no overlap",
		.exclude_start = 250, .exclude_end = 300,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = &single_range_b, .expected_nr_ranges = 1,
		.expected_ret = 0,
	},
	{
		.description = "1.8: A completely covers B and extends beyond",
		.exclude_start = 50, .exclude_end = 250,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = NULL, .expected_nr_ranges = 0,
		.expected_ret = 0,
	},
	{
		.description = "1.9: A covers B and extends to the left",
		.exclude_start = 50, .exclude_end = 199, // A ends exactly where B ends
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = NULL, .expected_nr_ranges = 0,
		.expected_ret = 0,
	},
	{
		.description = "1.10: A covers B and extends to the right",
		.exclude_start = 100, .exclude_end = 250, // A starts exactly where B starts
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = NULL, .expected_nr_ranges = 0,
		.expected_ret = 0,
	},
	{
		.description = "1.11: A is identical to B",
		.exclude_start = 100, .exclude_end = 199,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = NULL, .expected_nr_ranges = 0,
		.expected_ret = 0,
	},
	{
		.description = "1.12: A is a point, left of B, no overlap",
		.exclude_start = 10, .exclude_end = 10,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = &single_range_b, .expected_nr_ranges = 1,
		.expected_ret = 0,
	},
	{
		.description = "1.13: A is a point, at start of B",
		.exclude_start = 100, .exclude_end = 100,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = (const struct range[]){{ .start = 101, .end = 199 }},
		.expected_nr_ranges = 1,
		.expected_ret = 0,
	},
	{
		.description = "1.14: A is a point, in middle of B (causes split)",
		.exclude_start = 150, .exclude_end = 150,
		.initial_max_ranges = 2, // Needs space for split
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = (const struct range[]){
			{ .start = 100, .end = 149 },
			{ .start = 151, .end = 199 }
		},
		.expected_nr_ranges = 2,
		.expected_ret = 0,
	},
	{
		.description = "1.15: A is a point, at end of B",
		.exclude_start = 199, .exclude_end = 199,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = (const struct range[]){{ .start = 100, .end = 198 }},
		.expected_nr_ranges = 1,
		.expected_ret = 0,
	},
	{
		.description = "1.16: A is a point, right of B, no overlap",
		.exclude_start = 250, .exclude_end = 250,
		.initial_max_ranges = 1,
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = &single_range_b, .expected_nr_ranges = 1,
		.expected_ret = 0,
	},
	// ENOMEM case for single range split
	{
		.description = "1.17: A completely inside B (split), no space (ENOMEM)",
		.exclude_start = 120, .exclude_end = 179,
		.initial_max_ranges = 1, // Not enough for split
		.initial_ranges = &single_range_b, .initial_nr_ranges = 1,
		.expected_ranges = NULL, // Not checked on error by assert_ranges_equal for content
		.expected_nr_ranges = 1, // Should remain unchanged
		.expected_ret = -ENOMEM,
	},
};


static void exclude_single_range_test(struct kunit *test)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(exclude_single_range_test_data); i++) {
		kunit_log(KERN_INFO, test, "Running: %s", exclude_single_range_test_data[i].description);
		run_exclude_test_case(test, &exclude_single_range_test_data[i]);
		// KUnit will stop on first KUNIT_ASSERT failure within run_exclude_test_case
	}
}

/*
 * Test Strategy 2: Regression test.
 */

static const struct exclude_test_param exclude_range_regression_test_data[] = {
	// Test data from commit a2e9a95d2190
	{
		.description = "2.1: exclude low 1M",
		.exclude_start = 0, .exclude_end = (1 << 20) - 1,
		.initial_max_ranges = 3,
		.initial_ranges = (const struct range[]){
			{ .start = 0, .end = 0x3efff },
			{ .start = 0x3f000, .end = 0x3ffff },
			{ .start = 0x40000, .end = 0x9ffff }
		},
		.initial_nr_ranges = 3,
		.expected_nr_ranges = 0,
		.expected_ret = 0,
	},
	// Test data from https://lore.kernel.org/all/ZXrY7QbXAlxydsSC@MiWiFi-R3L-srv/T/#u
	{
		.description = "2.2: when range out of bound",
		.exclude_start = 100, .exclude_end = 200,
		.initial_max_ranges = 3,
		.initial_ranges = (const struct range[]){
			{ .start = 1, .end = 299 },
			{ .start = 401, .end = 1000 },
			{ .start = 1001, .end = 2000 }
		},
		.initial_nr_ranges = 3,
		.expected_ranges = NULL, // Not checked on error by assert_ranges_equal for content
		.expected_nr_ranges = 3, // Should remain unchanged
		.expected_ret = -ENOMEM
	},

};


static void exclude_range_regression_test(struct kunit *test)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(exclude_range_regression_test_data); i++) {
		kunit_log(KERN_INFO, test, "Running: %s", exclude_range_regression_test_data[i].description);
		run_exclude_test_case(test, &exclude_range_regression_test_data[i]);
		// KUnit will stop on first KUNIT_ASSERT failure within run_exclude_test_case
	}
}

/*
 * KUnit Test Suite
 */
static struct kunit_case crash_exclude_mem_range_test_cases[] = {
	KUNIT_CASE(exclude_single_range_test),
	KUNIT_CASE(exclude_range_regression_test),
	{}
};

static struct kunit_suite crash_exclude_mem_range_suite = {
	.name = "crash_exclude_mem_range_tests",
	.test_cases = crash_exclude_mem_range_test_cases,
	// .init and .exit can be NULL if not needed globally for the suite
};

kunit_test_suite(crash_exclude_mem_range_suite);

MODULE_DESCRIPTION("crash dump KUnit test suite");
MODULE_LICENSE("GPL");
