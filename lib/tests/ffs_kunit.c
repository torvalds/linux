// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for ffs()-family functions
 */
#include <kunit/test.h>
#include <linux/bitops.h>

/*
 * Test data structures
 */
struct ffs_test_case {
	unsigned long input;
	int expected_ffs;	/* ffs() result (1-based) */
	int expected_fls;	/* fls() result (1-based) */
	const char *description;
};

struct ffs64_test_case {
	u64 input;
	int expected_fls64;		    /* fls64() result (1-based) */
	unsigned int expected_ffs64_0based; /* __ffs64() result (0-based) */
	const char *description;
};

/*
 * Basic edge cases - core functionality validation
 */
static const struct ffs_test_case basic_test_cases[] = {
	/* Zero case - special handling */
	{0x00000000, 0, 0, "zero value"},

	/* Single bit patterns - powers of 2 */
	{0x00000001, 1, 1, "bit 0 set"},
	{0x00000002, 2, 2, "bit 1 set"},
	{0x00000004, 3, 3, "bit 2 set"},
	{0x00000008, 4, 4, "bit 3 set"},
	{0x00000010, 5, 5, "bit 4 set"},
	{0x00000020, 6, 6, "bit 5 set"},
	{0x00000040, 7, 7, "bit 6 set"},
	{0x00000080, 8, 8, "bit 7 set"},
	{0x00000100, 9, 9, "bit 8 set"},
	{0x00008000, 16, 16, "bit 15 set"},
	{0x00010000, 17, 17, "bit 16 set"},
	{0x40000000, 31, 31, "bit 30 set"},
	{0x80000000, 32, 32, "bit 31 set (sign bit)"},

	/* Maximum values */
	{0xFFFFFFFF, 1, 32, "all bits set"},

	/* Multiple bit patterns */
	{0x00000003, 1, 2, "bits 0-1 set"},
	{0x00000007, 1, 3, "bits 0-2 set"},
	{0x0000000F, 1, 4, "bits 0-3 set"},
	{0x000000FF, 1, 8, "bits 0-7 set"},
	{0x0000FFFF, 1, 16, "bits 0-15 set"},
	{0x7FFFFFFF, 1, 31, "bits 0-30 set"},

	/* Sparse patterns */
	{0x00000101, 1, 9, "bits 0,8 set"},
	{0x00001001, 1, 13, "bits 0,12 set"},
	{0x80000001, 1, 32, "bits 0,31 set"},
	{0x40000002, 2, 31, "bits 1,30 set"},
};

/*
 * 64-bit test cases
 */
static const struct ffs64_test_case ffs64_test_cases[] = {
	/* Zero case */
	{0x0000000000000000ULL, 0, 0, "zero value"},

	/* Single bit patterns */
	{0x0000000000000001ULL, 1, 0, "bit 0 set"},
	{0x0000000000000002ULL, 2, 1, "bit 1 set"},
	{0x0000000000000004ULL, 3, 2, "bit 2 set"},
	{0x0000000000000008ULL, 4, 3, "bit 3 set"},
	{0x0000000000008000ULL, 16, 15, "bit 15 set"},
	{0x0000000000010000ULL, 17, 16, "bit 16 set"},
	{0x0000000080000000ULL, 32, 31, "bit 31 set"},
	{0x0000000100000000ULL, 33, 32, "bit 32 set"},
	{0x0000000200000000ULL, 34, 33, "bit 33 set"},
	{0x4000000000000000ULL, 63, 62, "bit 62 set"},
	{0x8000000000000000ULL, 64, 63, "bit 63 set (sign bit)"},

	/* Maximum values */
	{0xFFFFFFFFFFFFFFFFULL, 64, 0, "all bits set"},

	/* Cross 32-bit boundary patterns */
	{0x00000000FFFFFFFFULL, 32, 0, "lower 32 bits set"},
	{0xFFFFFFFF00000000ULL, 64, 32, "upper 32 bits set"},
	{0x8000000000000001ULL, 64, 0, "bits 0,63 set"},
	{0x4000000000000002ULL, 63, 1, "bits 1,62 set"},

	/* Mixed patterns */
	{0x00000001FFFFFFFFULL, 33, 0, "bit 32 + lower 32 bits"},
	{0xFFFFFFFF80000000ULL, 64, 31, "upper 32 bits + bit 31"},
};

/*
 * Helper function to validate ffs results with detailed error messages
 */
static void validate_ffs_result(struct kunit *test, unsigned long input,
				int actual, int expected, const char *func_name,
				const char *description)
{
	KUNIT_EXPECT_EQ_MSG(test, actual, expected,
			    "%s(0x%08lx) [%s]: expected %d, got %d",
			    func_name, input, description, expected, actual);
}

/*
 * Helper function to validate 64-bit ffs results
 */
static void validate_ffs64_result(struct kunit *test, u64 input,
				  int actual, int expected, const char *func_name,
				  const char *description)
{
	KUNIT_EXPECT_EQ_MSG(test, actual, expected,
			    "%s(0x%016llx) [%s]: expected %d, got %d",
			    func_name, input, description, expected, actual);
}

/*
 * Helper function to validate mathematical relationships between functions
 */
static void validate_ffs_relationships(struct kunit *test, unsigned long input)
{
	int ffs_result;
	int fls_result;
	unsigned int ffs_0based;
	unsigned int fls_0based;

	if (input == 0) {
		/* Special case: zero input */
		KUNIT_EXPECT_EQ(test, ffs(input), 0);
		KUNIT_EXPECT_EQ(test, fls(input), 0);
		/* __ffs and __fls are undefined for 0, but often return specific values */
		return;
	}

	ffs_result = ffs(input);
	fls_result = fls(input);
	ffs_0based = __ffs(input);
	fls_0based = __fls(input);

	/* Relationship: ffs(x) == __ffs(x) + 1 for x != 0 */
	KUNIT_EXPECT_EQ_MSG(test, ffs_result, ffs_0based + 1,
			    "ffs(0x%08lx) != __ffs(0x%08lx) + 1: %d != %u + 1",
			    input, input, ffs_result, ffs_0based);

	/* Relationship: fls(x) == __fls(x) + 1 for x != 0 */
	KUNIT_EXPECT_EQ_MSG(test, fls_result, fls_0based + 1,
			    "fls(0x%08lx) != __fls(0x%08lx) + 1: %d != %u + 1",
			    input, input, fls_result, fls_0based);

	/* Range validation */
	KUNIT_EXPECT_GE(test, ffs_result, 1);
	KUNIT_EXPECT_LE(test, ffs_result, BITS_PER_LONG);
	KUNIT_EXPECT_GE(test, fls_result, 1);
	KUNIT_EXPECT_LE(test, fls_result, BITS_PER_LONG);
}

/*
 * Helper function to validate 64-bit relationships
 */
static void validate_ffs64_relationships(struct kunit *test, u64 input)
{
	int fls64_result;
	unsigned int ffs64_0based;

	if (input == 0) {
		KUNIT_EXPECT_EQ(test, fls64(input), 0);
		return;
	}

	fls64_result = fls64(input);
	ffs64_0based = __ffs64(input);

	/* Range validation */
	KUNIT_EXPECT_GE(test, fls64_result, 1);
	KUNIT_EXPECT_LE(test, fls64_result, 64);
	KUNIT_EXPECT_LT(test, ffs64_0based, 64);

	/*
	 * Relationships with 32-bit functions should hold for small values
	 * on all architectures.
	 */
	if (input <= 0xFFFFFFFFULL) {
		unsigned long input_32 = (unsigned long)input;
		KUNIT_EXPECT_EQ_MSG(test, fls64(input), fls(input_32),
				    "fls64(0x%llx) != fls(0x%lx): %d != %d",
				    input, input_32, fls64(input), fls(input_32));

		if (input != 0) {
			KUNIT_EXPECT_EQ_MSG(test, __ffs64(input), __ffs(input_32),
					    "__ffs64(0x%llx) != __ffs(0x%lx): %lu != %lu",
					    input, input_32,
					    (unsigned long)__ffs64(input),
					    (unsigned long)__ffs(input_32));
		}
	}
}

/*
 * Test basic correctness of all ffs-family functions
 */
static void ffs_basic_correctness_test(struct kunit *test)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(basic_test_cases); i++) {
		const struct ffs_test_case *tc = &basic_test_cases[i];

		/* Test ffs() */
		validate_ffs_result(test, tc->input, ffs(tc->input),
				    tc->expected_ffs, "ffs", tc->description);

		/* Test fls() */
		validate_ffs_result(test, tc->input, fls(tc->input),
				    tc->expected_fls, "fls", tc->description);

		/* Test __ffs() - skip zero case as it's undefined */
		if (tc->input != 0) {
			/* Calculate expected __ffs() result: __ffs(x) == ffs(x) - 1 */
			unsigned int expected_ffs_0based = tc->expected_ffs - 1;
			validate_ffs_result(test, tc->input, __ffs(tc->input),
					    expected_ffs_0based, "__ffs", tc->description);
		}

		/* Test __fls() - skip zero case as it's undefined */
		if (tc->input != 0) {
			/* Calculate expected __fls() result: __fls(x) == fls(x) - 1 */
			unsigned int expected_fls_0based = tc->expected_fls - 1;
			validate_ffs_result(test, tc->input, __fls(tc->input),
					    expected_fls_0based, "__fls", tc->description);
		}
	}
}

/*
 * Test 64-bit function correctness
 */
static void ffs64_correctness_test(struct kunit *test)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ffs64_test_cases); i++) {
		const struct ffs64_test_case *tc = &ffs64_test_cases[i];

		/* Test fls64() */
		validate_ffs64_result(test, tc->input, fls64(tc->input),
				      tc->expected_fls64, "fls64", tc->description);

		/* Test __ffs64() - skip zero case as it's undefined */
		if (tc->input != 0) {
			validate_ffs64_result(test, tc->input, __ffs64(tc->input),
					      tc->expected_ffs64_0based, "__ffs64",
					      tc->description);
		}
	}
}

/*
 * Test mathematical relationships between functions
 */
static void ffs_mathematical_relationships_test(struct kunit *test)
{
	int i;

	/* Test basic cases */
	for (i = 0; i < ARRAY_SIZE(basic_test_cases); i++) {
		validate_ffs_relationships(test, basic_test_cases[i].input);
	}

	/* Test 64-bit cases */
	for (i = 0; i < ARRAY_SIZE(ffs64_test_cases); i++) {
		validate_ffs64_relationships(test, ffs64_test_cases[i].input);
	}
}

/*
 * Test edge cases and boundary conditions
 */
static void ffs_edge_cases_test(struct kunit *test)
{
	unsigned long test_patterns[] = {
		/* Powers of 2 */
		1UL, 2UL, 4UL, 8UL, 16UL, 32UL, 64UL, 128UL,
		256UL, 512UL, 1024UL, 2048UL, 4096UL, 8192UL,

		/* Powers of 2 minus 1 */
		1UL, 3UL, 7UL, 15UL, 31UL, 63UL, 127UL, 255UL,
		511UL, 1023UL, 2047UL, 4095UL, 8191UL,

		/* Boundary values */
		0x7FFFFFFFUL,	/* Maximum positive 32-bit */
		0x80000000UL,	/* Minimum negative 32-bit */
		0xFFFFFFFFUL,	/* Maximum 32-bit unsigned */
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(test_patterns); i++) {
		validate_ffs_relationships(test, test_patterns[i]);
	}
}

/*
 * Test 64-bit edge cases
 */
static void ffs64_edge_cases_test(struct kunit *test)
{
	u64 test_patterns_64[] = {
		/* 64-bit powers of 2 */
		0x0000000100000000ULL,	/* 2^32 */
		0x0000000200000000ULL,	/* 2^33 */
		0x0000000400000000ULL,	/* 2^34 */
		0x0000001000000000ULL,	/* 2^36 */
		0x0000010000000000ULL,	/* 2^40 */
		0x0001000000000000ULL,	/* 2^48 */
		0x0100000000000000ULL,	/* 2^56 */
		0x4000000000000000ULL,	/* 2^62 */
		0x8000000000000000ULL,	/* 2^63 */

		/* Cross-boundary patterns */
		0x00000000FFFFFFFFULL,	/* Lower 32 bits */
		0xFFFFFFFF00000000ULL,	/* Upper 32 bits */
		0x7FFFFFFFFFFFFFFFULL,	/* Maximum positive 64-bit */
		0xFFFFFFFFFFFFFFFFULL,	/* Maximum 64-bit unsigned */
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(test_patterns_64); i++) {
		validate_ffs64_relationships(test, test_patterns_64[i]);
	}
}

/*
 * ffz() test data - Find First Zero bit test cases
 */
struct ffz_test_case {
	unsigned long input;
	unsigned long expected_ffz;
	const char *description;
};

static const struct ffz_test_case ffz_test_cases[] = {
	/* Zero bits in specific positions */
	{0xFFFFFFFE, 0, "bit 0 is zero"},      /* ...11111110 */
	{0xFFFFFFFD, 1, "bit 1 is zero"},      /* ...11111101 */
	{0xFFFFFFFB, 2, "bit 2 is zero"},      /* ...11111011 */
	{0xFFFFFFF7, 3, "bit 3 is zero"},      /* ...11110111 */
	{0xFFFFFFEF, 4, "bit 4 is zero"},      /* ...11101111 */
	{0xFFFFFFDF, 5, "bit 5 is zero"},      /* ...11011111 */
	{0xFFFFFFBF, 6, "bit 6 is zero"},      /* ...10111111 */
	{0xFFFFFF7F, 7, "bit 7 is zero"},      /* ...01111111 */
	{0xFFFFFEFF, 8, "bit 8 is zero"},      /* Gap in bit 8 */
	{0xFFFF7FFF, 15, "bit 15 is zero"},    /* Gap in bit 15 */
	{0xFFFEFFFF, 16, "bit 16 is zero"},    /* Gap in bit 16 */
	{0xBFFFFFFF, 30, "bit 30 is zero"},    /* Gap in bit 30 */
	{0x7FFFFFFF, 31, "bit 31 is zero"},    /* 01111111... */

	/* Multiple zero patterns */
	{0xFFFFFFFC, 0, "bits 0-1 are zero"},  /* ...11111100 */
	{0xFFFFFFF8, 0, "bits 0-2 are zero"},  /* ...11111000 */
	{0xFFFFFFF0, 0, "bits 0-3 are zero"},  /* ...11110000 */
	{0xFFFFFF00, 0, "bits 0-7 are zero"},  /* ...00000000 */
	{0xFFFF0000, 0, "bits 0-15 are zero"}, /* Lower 16 bits zero */

	/* All zeros (special case) */
	{0x00000000, 0, "all bits zero"},

	/* Complex patterns */
	{0xFFFDFFFF, 17, "bit 17 is zero"},    /* Gap in bit 17 */
	{0xFFF7FFFF, 19, "bit 19 is zero"},    /* Gap in bit 19 */
	{0xF7FFFFFF, 27, "bit 27 is zero"},    /* Gap in bit 27 */
	{0xDFFFFFFF, 29, "bit 29 is zero"},    /* Gap in bit 29 */
};

/*
 * Test basic correctness of ffz() function
 */
static void ffz_basic_correctness_test(struct kunit *test)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ffz_test_cases); i++) {
		const struct ffz_test_case *tc = &ffz_test_cases[i];
		unsigned long result = ffz(tc->input);

		KUNIT_EXPECT_EQ_MSG(test, result, tc->expected_ffz,
				    "ffz(0x%08lx) [%s]: expected %lu, got %lu",
				    tc->input, tc->description, tc->expected_ffz, result);
	}
}

/*
 * Test mathematical relationships between ffz() and other functions
 */
static void validate_ffz_relationships(struct kunit *test, unsigned long input)
{
	unsigned long ffz_result;

	if (input == 0) {
		/* ffz(0) should return 0 (first zero bit is at position 0) */
		KUNIT_EXPECT_EQ(test, ffz(input), 0);
		return;
	}

	if (input == ~0UL) {
		/* ffz(~0) is undefined (no zero bits) - just verify it doesn't crash */
		ffz_result = ffz(input);
		/* Implementation-defined behavior, just ensure it completes */
		return;
	}

	ffz_result = ffz(input);

	/* Range validation - result should be within valid bit range */
	KUNIT_EXPECT_LT(test, ffz_result, BITS_PER_LONG);

	/* Verify the bit at ffz_result position is actually zero */
	KUNIT_EXPECT_EQ_MSG(test, (input >> ffz_result) & 1, 0,
			    "ffz(0x%08lx) = %lu, but bit %lu is not zero",
			    input, ffz_result, ffz_result);

	/* Core relationship: if we set the ffz bit, ffz should find a different bit */
	if (ffz_result < BITS_PER_LONG - 1) {
		unsigned long modified = input | (1UL << ffz_result);
		if (modified != ~0UL) { /* Skip if all bits would be set */
			unsigned long new_ffz = ffz(modified);
			KUNIT_EXPECT_NE_MSG(test, new_ffz, ffz_result,
					    "ffz(0x%08lx) = %lu, but setting that bit doesn't change ffz result",
					    input, ffz_result);
		}
	}
}

static void ffz_mathematical_relationships_test(struct kunit *test)
{
	unsigned long test_patterns[] = {
		/* Powers of 2 with one bit clear */
		0xFFFFFFFE, 0xFFFFFFFD, 0xFFFFFFFB, 0xFFFFFFF7,
		0xFFFFFFEF, 0xFFFFFFDF, 0xFFFFFFBF, 0xFFFFFF7F,

		/* Multiple patterns */
		0xFFFFFF00, 0xFFFFF000, 0xFFFF0000, 0xFFF00000,
		0x7FFFFFFF, 0x3FFFFFFF, 0x1FFFFFFF, 0x0FFFFFFF,

		/* Complex bit patterns */
		0xAAAAAAAA, 0x55555555, 0xCCCCCCCC, 0x33333333,
		0xF0F0F0F0, 0x0F0F0F0F, 0xFF00FF00, 0x00FF00FF,
	};
	int i;

	/* Test basic test cases */
	for (i = 0; i < ARRAY_SIZE(ffz_test_cases); i++) {
		validate_ffz_relationships(test, ffz_test_cases[i].input);
	}

	/* Test additional patterns */
	for (i = 0; i < ARRAY_SIZE(test_patterns); i++) {
		validate_ffz_relationships(test, test_patterns[i]);
	}
}

/*
 * Test edge cases and boundary conditions for ffz()
 */
static void ffz_edge_cases_test(struct kunit *test)
{
	unsigned long edge_patterns[] = {
		/* Boundary values */
		0x00000000,  /* All zeros */
		0x80000000,  /* Only MSB set */
		0x00000001,  /* Only LSB set */
		0x7FFFFFFF,  /* MSB clear */
		0xFFFFFFFE,  /* LSB clear */

		/* Powers of 2 complement patterns (one zero bit each) */
		~(1UL << 0),  ~(1UL << 1),  ~(1UL << 2),  ~(1UL << 3),
		~(1UL << 4),  ~(1UL << 8),  ~(1UL << 16), ~(1UL << 31),

		/* Walking zero patterns */
		0xFFFFFFFE, 0xFFFFFFFD, 0xFFFFFFFB, 0xFFFFFFF7,
		0xFFFFFFEF, 0xFFFFFFDF, 0xFFFFFFBF, 0xFFFFFF7F,
		0xFFFFFEFF, 0xFFFFFDFF, 0xFFFFFBFF, 0xFFFFF7FF,

		/* Multiple zeros */
		0xFFFFFF00, 0xFFFFF000, 0xFFFF0000, 0xFFF00000,
		0xFF000000, 0xF0000000, 0x00000000,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(edge_patterns); i++) {
		validate_ffz_relationships(test, edge_patterns[i]);
	}
}

/*
 * To have useful build error output, split the tests into separate
 * functions so it's clear which are missing __attribute_const__.
 */
#define CREATE_WRAPPER(func)						\
static noinline bool build_test_##func(void)				\
{									\
	int init_##func = 32;						\
	int result_##func = func(6);					\
									\
	/* Does the static initializer vanish after calling func? */	\
	BUILD_BUG_ON(init_##func < 32);					\
									\
	/* "Consume" the results so optimizer doesn't drop them. */	\
	barrier_data(&init_##func);					\
	barrier_data(&result_##func);					\
									\
	return true;							\
}
CREATE_WRAPPER(ffs)
CREATE_WRAPPER(fls)
CREATE_WRAPPER(__ffs)
CREATE_WRAPPER(__fls)
CREATE_WRAPPER(ffz)
#undef CREATE_WRAPPER

/*
 * Make sure that __attribute_const__ has be applied to all the
 * functions. This is a regression test for:
 * https://github.com/KSPP/linux/issues/364
 */
static void ffs_attribute_const_test(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, build_test_ffs());
	KUNIT_EXPECT_TRUE(test, build_test_fls());
	KUNIT_EXPECT_TRUE(test, build_test___ffs());
	KUNIT_EXPECT_TRUE(test, build_test___fls());
	KUNIT_EXPECT_TRUE(test, build_test_ffz());
}

/*
 * KUnit test case definitions
 */
static struct kunit_case ffs_test_cases[] = {
	KUNIT_CASE(ffs_basic_correctness_test),
	KUNIT_CASE(ffs64_correctness_test),
	KUNIT_CASE(ffs_mathematical_relationships_test),
	KUNIT_CASE(ffs_edge_cases_test),
	KUNIT_CASE(ffs64_edge_cases_test),
	KUNIT_CASE(ffz_basic_correctness_test),
	KUNIT_CASE(ffz_mathematical_relationships_test),
	KUNIT_CASE(ffz_edge_cases_test),
	KUNIT_CASE(ffs_attribute_const_test),
	{}
};

/*
 * KUnit test suite definition
 */
static struct kunit_suite ffs_test_suite = {
	.name = "ffs",
	.test_cases = ffs_test_cases,
};

kunit_test_suites(&ffs_test_suite);

MODULE_DESCRIPTION("KUnit tests for ffs()-family functions");
MODULE_LICENSE("GPL");
