// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

struct bprm_stack_limits_result {
	struct linux_binprm bprm;
	int expected_rc;
	unsigned long expected_argmin;
};

static const struct bprm_stack_limits_result bprm_stack_limits_results[] = {
	/* Negative argc/envc counts produce -E2BIG */
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ULONG_MAX,
	    .argc = INT_MIN, .envc = INT_MIN }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ULONG_MAX,
	    .argc = 5, .envc = -1 }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ULONG_MAX,
	    .argc = -1, .envc = 10 }, .expected_rc = -E2BIG },
	/* The max value of argc or envc is MAX_ARG_STRINGS. */
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ULONG_MAX,
	    .argc = INT_MAX, .envc = INT_MAX }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ULONG_MAX,
	    .argc = MAX_ARG_STRINGS, .envc = MAX_ARG_STRINGS }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ULONG_MAX,
	    .argc = 0, .envc = MAX_ARG_STRINGS }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ULONG_MAX,
	    .argc = MAX_ARG_STRINGS, .envc = 0 }, .expected_rc = -E2BIG },
	/*
	 * On 32-bit system these argc and envc counts, while likely impossible
	 * to represent within the associated TASK_SIZE, could overflow the
	 * limit calculation, and bypass the ptr_size <= limit check.
	 */
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ULONG_MAX,
	    .argc = 0x20000001, .envc = 0x20000001 }, .expected_rc = -E2BIG },
#ifdef CONFIG_MMU
	/* Make sure a pathological bprm->p doesn't cause an overflow. */
	{ { .p = sizeof(void *), .rlim_stack.rlim_cur = ULONG_MAX,
	    .argc = 10, .envc = 10 }, .expected_rc = -E2BIG },
#endif
	/*
	 * 0 rlim_stack will get raised to ARG_MAX. With 1 string pointer,
	 * we should see p - ARG_MAX + sizeof(void *).
	 */
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 0,
	    .argc = 1, .envc = 0 }, .expected_argmin = ULONG_MAX - ARG_MAX + sizeof(void *)},
	/* Validate that argc is always raised to a minimum of 1. */
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 0,
	    .argc = 0, .envc = 0 }, .expected_argmin = ULONG_MAX - ARG_MAX + sizeof(void *)},
	/*
	 * 0 rlim_stack will get raised to ARG_MAX. With pointers filling ARG_MAX,
	 * we should see -E2BIG. (Note argc is always raised to at least 1.)
	 */
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 0,
	    .argc = ARG_MAX / sizeof(void *), .envc = 0 }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 0,
	    .argc = 0, .envc = ARG_MAX / sizeof(void *) - 1 }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 0,
	    .argc = ARG_MAX / sizeof(void *) + 1, .envc = 0 }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 0,
	    .argc = 0, .envc = ARG_MAX / sizeof(void *) }, .expected_rc = -E2BIG },
	/* And with one less, we see space for exactly 1 pointer. */
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 0,
	    .argc = (ARG_MAX / sizeof(void *)) - 1, .envc = 0 },
	  .expected_argmin = ULONG_MAX - sizeof(void *) },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 0,
	    .argc = 0, .envc = (ARG_MAX / sizeof(void *)) - 2, },
	  .expected_argmin = ULONG_MAX - sizeof(void *) },
	/* If we raise rlim_stack / 4 to exactly ARG_MAX, nothing changes. */
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ARG_MAX * 4,
	    .argc = ARG_MAX / sizeof(void *), .envc = 0 }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ARG_MAX * 4,
	    .argc = 0, .envc = ARG_MAX / sizeof(void *) - 1 }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ARG_MAX * 4,
	    .argc = ARG_MAX / sizeof(void *) + 1, .envc = 0 }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ARG_MAX * 4,
	    .argc = 0, .envc = ARG_MAX / sizeof(void *) }, .expected_rc = -E2BIG },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ARG_MAX * 4,
	    .argc = (ARG_MAX / sizeof(void *)) - 1, .envc = 0 },
	  .expected_argmin = ULONG_MAX - sizeof(void *) },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = ARG_MAX * 4,
	    .argc = 0, .envc = (ARG_MAX / sizeof(void *)) - 2, },
	  .expected_argmin = ULONG_MAX - sizeof(void *) },
	/* But raising it another pointer * 4 will provide space for 1 more pointer. */
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = (ARG_MAX + sizeof(void *)) * 4,
	    .argc = ARG_MAX / sizeof(void *), .envc = 0 },
	  .expected_argmin = ULONG_MAX - sizeof(void *) },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = (ARG_MAX + sizeof(void *)) * 4,
	    .argc = 0, .envc = ARG_MAX / sizeof(void *) - 1 },
	  .expected_argmin = ULONG_MAX - sizeof(void *) },
	/* Raising rlim_stack / 4 to _STK_LIM / 4 * 3 will see more space. */
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 4 * (_STK_LIM / 4 * 3),
	    .argc = 0, .envc = 0 },
	  .expected_argmin = ULONG_MAX - (_STK_LIM / 4 * 3) + sizeof(void *) },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 4 * (_STK_LIM / 4 * 3),
	    .argc = 0, .envc = 0 },
	  .expected_argmin = ULONG_MAX - (_STK_LIM / 4 * 3) + sizeof(void *) },
	/* But raising it any further will see no increase. */
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 4 * (_STK_LIM / 4 * 3 + sizeof(void *)),
	    .argc = 0, .envc = 0 },
	  .expected_argmin = ULONG_MAX - (_STK_LIM / 4 * 3) + sizeof(void *) },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 4 * (_STK_LIM / 4 *  + sizeof(void *)),
	    .argc = 0, .envc = 0 },
	  .expected_argmin = ULONG_MAX - (_STK_LIM / 4 * 3) + sizeof(void *) },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 4 * _STK_LIM,
	    .argc = 0, .envc = 0 },
	  .expected_argmin = ULONG_MAX - (_STK_LIM / 4 * 3) + sizeof(void *) },
	{ { .p = ULONG_MAX, .rlim_stack.rlim_cur = 4 * _STK_LIM,
	    .argc = 0, .envc = 0 },
	  .expected_argmin = ULONG_MAX - (_STK_LIM / 4 * 3) + sizeof(void *) },
};

static void exec_test_bprm_stack_limits(struct kunit *test)
{
	/* Double-check the constants. */
	KUNIT_EXPECT_EQ(test, _STK_LIM, SZ_8M);
	KUNIT_EXPECT_EQ(test, ARG_MAX, 32 * SZ_4K);
	KUNIT_EXPECT_EQ(test, MAX_ARG_STRINGS, 0x7FFFFFFF);

	for (int i = 0; i < ARRAY_SIZE(bprm_stack_limits_results); i++) {
		const struct bprm_stack_limits_result *result = &bprm_stack_limits_results[i];
		struct linux_binprm bprm = result->bprm;
		int rc;

		rc = bprm_stack_limits(&bprm);
		KUNIT_EXPECT_EQ_MSG(test, rc, result->expected_rc, "on loop %d", i);
#ifdef CONFIG_MMU
		KUNIT_EXPECT_EQ_MSG(test, bprm.argmin, result->expected_argmin, "on loop %d", i);
#endif
	}
}

static struct kunit_case exec_test_cases[] = {
	KUNIT_CASE(exec_test_bprm_stack_limits),
	{},
};

static struct kunit_suite exec_test_suite = {
	.name = "exec",
	.test_cases = exec_test_cases,
};

kunit_test_suite(exec_test_suite);
