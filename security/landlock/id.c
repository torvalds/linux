// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Unique identification number generator
 *
 * Copyright © 2024-2025 Microsoft Corporation
 */

#include <kunit/test.h>
#include <linux/atomic.h>
#include <linux/random.h>
#include <linux/spinlock.h>

#include "common.h"
#include "id.h"

#define COUNTER_PRE_INIT 0

static atomic64_t next_id = ATOMIC64_INIT(COUNTER_PRE_INIT);

static void __init init_id(atomic64_t *const counter, const u32 random_32bits)
{
	u64 init;

	/*
	 * Ensures sure 64-bit values are always used by user space (or may
	 * fail with -EOVERFLOW), and makes this testable.
	 */
	init = 1ULL << 32;

	/*
	 * Makes a large (2^32) boot-time value to limit ID collision in logs
	 * from different boots, and to limit info leak about the number of
	 * initially (relative to the reader) created elements (e.g. domains).
	 */
	init += random_32bits;

	/* Sets first or ignores.  This will be the first ID. */
	atomic64_cmpxchg(counter, COUNTER_PRE_INIT, init);
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static void __init test_init_min(struct kunit *const test)
{
	atomic64_t counter = ATOMIC64_INIT(COUNTER_PRE_INIT);

	init_id(&counter, 0);
	KUNIT_EXPECT_EQ(test, atomic64_read(&counter), 1ULL + U32_MAX);
}

static void __init test_init_max(struct kunit *const test)
{
	atomic64_t counter = ATOMIC64_INIT(COUNTER_PRE_INIT);

	init_id(&counter, ~0);
	KUNIT_EXPECT_EQ(test, atomic64_read(&counter), 1 + (2ULL * U32_MAX));
}

static void __init test_init_once(struct kunit *const test)
{
	const u64 first_init = 1ULL + U32_MAX;
	atomic64_t counter = ATOMIC64_INIT(COUNTER_PRE_INIT);

	init_id(&counter, 0);
	KUNIT_EXPECT_EQ(test, atomic64_read(&counter), first_init);

	init_id(&counter, ~0);
	KUNIT_EXPECT_EQ_MSG(
		test, atomic64_read(&counter), first_init,
		"Should still have the same value after the subsequent init_id()");
}

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */

void __init landlock_init_id(void)
{
	return init_id(&next_id, get_random_u32());
}

/*
 * It's not worth it to try to hide the monotonic counter because it can still
 * be inferred (with N counter ranges), and if we are allowed to read the inode
 * number we should also be allowed to read the time creation anyway, and it
 * can be handy to store and sort domain IDs for user space.
 *
 * Returns the value of next_id and increment it to let some space for the next
 * one.
 */
static u64 get_id_range(size_t number_of_ids, atomic64_t *const counter,
			u8 random_4bits)
{
	u64 id, step;

	/*
	 * We should return at least 1 ID, and we may need a set of consecutive
	 * ones (e.g. to generate a set of inodes).
	 */
	if (WARN_ON_ONCE(number_of_ids <= 0))
		number_of_ids = 1;

	/*
	 * Blurs the next ID guess with 1/16 ratio.  We get 2^(64 - 4) -
	 * (2 * 2^32), so a bit less than 2^60 available IDs, which should be
	 * much more than enough considering the number of CPU cycles required
	 * to get a new ID (e.g. a full landlock_restrict_self() call), and the
	 * cost of draining all available IDs during the system's uptime.
	 */
	random_4bits = random_4bits % (1 << 4);
	step = number_of_ids + random_4bits;

	/* It is safe to cast a signed atomic to an unsigned value. */
	id = atomic64_fetch_add(step, counter);

	/* Warns if landlock_init_id() was not called. */
	WARN_ON_ONCE(id == COUNTER_PRE_INIT);
	return id;
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static void test_range1_rand0(struct kunit *const test)
{
	atomic64_t counter;
	u64 init;

	init = get_random_u32();
	atomic64_set(&counter, init);
	KUNIT_EXPECT_EQ(test, get_id_range(1, &counter, 0), init);
	KUNIT_EXPECT_EQ(
		test, get_id_range(get_random_u8(), &counter, get_random_u8()),
		init + 1);
}

static void test_range1_rand1(struct kunit *const test)
{
	atomic64_t counter;
	u64 init;

	init = get_random_u32();
	atomic64_set(&counter, init);
	KUNIT_EXPECT_EQ(test, get_id_range(1, &counter, 1), init);
	KUNIT_EXPECT_EQ(
		test, get_id_range(get_random_u8(), &counter, get_random_u8()),
		init + 2);
}

static void test_range1_rand16(struct kunit *const test)
{
	atomic64_t counter;
	u64 init;

	init = get_random_u32();
	atomic64_set(&counter, init);
	KUNIT_EXPECT_EQ(test, get_id_range(1, &counter, 16), init);
	KUNIT_EXPECT_EQ(
		test, get_id_range(get_random_u8(), &counter, get_random_u8()),
		init + 1);
}

static void test_range2_rand0(struct kunit *const test)
{
	atomic64_t counter;
	u64 init;

	init = get_random_u32();
	atomic64_set(&counter, init);
	KUNIT_EXPECT_EQ(test, get_id_range(2, &counter, 0), init);
	KUNIT_EXPECT_EQ(
		test, get_id_range(get_random_u8(), &counter, get_random_u8()),
		init + 2);
}

static void test_range2_rand1(struct kunit *const test)
{
	atomic64_t counter;
	u64 init;

	init = get_random_u32();
	atomic64_set(&counter, init);
	KUNIT_EXPECT_EQ(test, get_id_range(2, &counter, 1), init);
	KUNIT_EXPECT_EQ(
		test, get_id_range(get_random_u8(), &counter, get_random_u8()),
		init + 3);
}

static void test_range2_rand2(struct kunit *const test)
{
	atomic64_t counter;
	u64 init;

	init = get_random_u32();
	atomic64_set(&counter, init);
	KUNIT_EXPECT_EQ(test, get_id_range(2, &counter, 2), init);
	KUNIT_EXPECT_EQ(
		test, get_id_range(get_random_u8(), &counter, get_random_u8()),
		init + 4);
}

static void test_range2_rand16(struct kunit *const test)
{
	atomic64_t counter;
	u64 init;

	init = get_random_u32();
	atomic64_set(&counter, init);
	KUNIT_EXPECT_EQ(test, get_id_range(2, &counter, 16), init);
	KUNIT_EXPECT_EQ(
		test, get_id_range(get_random_u8(), &counter, get_random_u8()),
		init + 2);
}

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */

/**
 * landlock_get_id_range - Get a range of unique IDs
 *
 * @number_of_ids: Number of IDs to hold.  Must be greater than one.
 *
 * Returns: The first ID in the range.
 */
u64 landlock_get_id_range(size_t number_of_ids)
{
	return get_id_range(number_of_ids, &next_id, get_random_u8());
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static struct kunit_case __refdata test_cases[] = {
	/* clang-format off */
	KUNIT_CASE(test_init_min),
	KUNIT_CASE(test_init_max),
	KUNIT_CASE(test_init_once),
	KUNIT_CASE(test_range1_rand0),
	KUNIT_CASE(test_range1_rand1),
	KUNIT_CASE(test_range1_rand16),
	KUNIT_CASE(test_range2_rand0),
	KUNIT_CASE(test_range2_rand1),
	KUNIT_CASE(test_range2_rand2),
	KUNIT_CASE(test_range2_rand16),
	{}
	/* clang-format on */
};

static struct kunit_suite test_suite = {
	.name = "landlock_id",
	.test_cases = test_cases,
};

kunit_test_init_section_suite(test_suite);

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */
