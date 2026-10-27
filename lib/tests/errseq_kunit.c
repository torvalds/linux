// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the errseq_t error-tracking infrastructure.
 *
 * These exercise the documented single-threaded semantics of the errseq
 * API (see Documentation/core-api/errseq.rst and lib/errseq.c): error
 * recording and overwriting, the "seen" handoff between errseq_sample()
 * and errseq_check_and_advance(), and the re-reporting of an error that
 * is recorded again after it has been seen.
 *
 * The lockless properties of errseq_t under concurrent updates are
 * outside the scope of these deterministic tests, as is the WARN path
 * for invalid error values.
 */
#include <kunit/test.h>

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/errseq.h>

/*
 * A zeroed errseq_t is the "no error has ever occurred" epoch: it
 * samples as zero and no check against it reports anything.
 */
static void errseq_test_zero_epoch_reports_no_error(struct kunit *test)
{
	errseq_t eseq = 0;
	errseq_t since = 0;

	KUNIT_EXPECT_EQ(test, errseq_sample(&eseq), 0);
	KUNIT_EXPECT_EQ(test, errseq_check(&eseq, 0), 0);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &since), 0);
	KUNIT_EXPECT_EQ(test, since, 0);
}

static void errseq_test_set_records_error(struct kunit *test)
{
	errseq_t eseq = 0;

	/* errseq_set() returns the previous value; the epoch is zero. */
	KUNIT_EXPECT_EQ(test, errseq_set(&eseq, -EIO), 0);
	KUNIT_EXPECT_EQ(test, errseq_check(&eseq, 0), -EIO);
}

/* Any error set always overwrites an existing error. */
static void errseq_test_set_overwrites_error(struct kunit *test)
{
	errseq_t eseq = 0;

	errseq_set(&eseq, -EIO);
	errseq_set(&eseq, -ENOSPC);
	KUNIT_EXPECT_EQ(test, errseq_check(&eseq, 0), -ENOSPC);
}

/* Both ends of the valid error range are recorded exactly. */
static void errseq_test_errno_range_extremes(struct kunit *test)
{
	errseq_t lo = 0;
	errseq_t hi = 0;

	errseq_set(&lo, -1);
	KUNIT_EXPECT_EQ(test, errseq_check(&lo, 0), -1);

	errseq_set(&hi, -MAX_ERRNO);
	KUNIT_EXPECT_EQ(test, errseq_check(&hi, 0), -MAX_ERRNO);
}

/*
 * An error nobody has seen yet samples as zero, so that a check against
 * the sample still reports it (see commit b4678df184b3 ("errseq: Always
 * report a writeback error once")).
 */
static void errseq_test_sample_of_unseen_error_is_zero(struct kunit *test)
{
	errseq_t eseq = 0;

	errseq_set(&eseq, -EIO);
	KUNIT_EXPECT_EQ(test, errseq_sample(&eseq), 0);
}

static void errseq_test_new_sampler_sees_unseen_error(struct kunit *test)
{
	errseq_t eseq = 0;
	errseq_t since;

	errseq_set(&eseq, -EIO);
	since = errseq_sample(&eseq);
	KUNIT_EXPECT_EQ(test, errseq_check(&eseq, since), -EIO);
}

/* A given error is reported exactly once per advancing cursor. */
static void errseq_test_check_and_advance_reports_once(struct kunit *test)
{
	errseq_t eseq = 0;
	errseq_t since = errseq_sample(&eseq);

	errseq_set(&eseq, -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &since), -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &since), 0);
}

/*
 * Once an error has been seen, a fresh sample is non-zero and checking
 * against it reports nothing: handled errors do not reach new samplers.
 */
static void errseq_test_sample_after_seen_is_current(struct kunit *test)
{
	errseq_t eseq = 0;
	errseq_t since = 0;
	errseq_t sample;

	errseq_set(&eseq, -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &since), -EIO);

	sample = errseq_sample(&eseq);
	KUNIT_EXPECT_NE(test, sample, 0);
	KUNIT_EXPECT_EQ(test, errseq_check(&eseq, sample), 0);
}

static void errseq_test_new_error_after_advance(struct kunit *test)
{
	errseq_t eseq = 0;
	errseq_t since = 0;

	errseq_set(&eseq, -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &since), -EIO);

	errseq_set(&eseq, -ENOSPC);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &since), -ENOSPC);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &since), 0);
}

/*
 * Recording the same error again after it has been seen must bump the
 * sequence, so cursors that consumed the first occurrence see the
 * second one too.
 */
static void errseq_test_same_error_reported_again_after_seen(struct kunit *test)
{
	errseq_t eseq = 0;
	errseq_t since = 0;
	errseq_t seen_cursor;

	errseq_set(&eseq, -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &since), -EIO);

	seen_cursor = since;
	errseq_set(&eseq, -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check(&eseq, since), -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &since), -EIO);
	/* The repeat must advance the sequence, not just re-toggle "seen". */
	KUNIT_EXPECT_NE(test, since, seen_cursor);
}

/*
 * A cursor that consumed an error must still observe a repeat of that
 * error even when another cursor has already marked the repeat seen:
 * recording over a seen value must advance the sequence.
 */
static void errseq_test_repeat_error_visible_to_all_cursors(struct kunit *test)
{
	errseq_t eseq = 0;
	errseq_t cursor_a = 0;
	errseq_t cursor_b = 0;

	errseq_set(&eseq, -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &cursor_a), -EIO);

	errseq_set(&eseq, -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &cursor_b), -EIO);

	KUNIT_EXPECT_EQ(test, errseq_check(&eseq, cursor_a), -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &cursor_a), -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &cursor_a), 0);
}

/* An advance with no new error reports nothing and leaves the cursor put. */
static void errseq_test_advance_stable_when_unchanged(struct kunit *test)
{
	errseq_t eseq = 0;
	errseq_t since = 0;
	errseq_t cursor;

	errseq_set(&eseq, -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &since), -EIO);

	cursor = since;
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &since), 0);
	KUNIT_EXPECT_EQ(test, since, cursor);
}

/*
 * Cursors are independent: one subscriber consuming an error does not
 * consume it for another, and each subscriber sees each error once.
 */
static void errseq_test_two_subscribers_independent(struct kunit *test)
{
	errseq_t eseq = 0;
	errseq_t cursor_a = errseq_sample(&eseq);
	errseq_t cursor_b = errseq_sample(&eseq);

	errseq_set(&eseq, -EIO);

	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &cursor_a), -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check(&eseq, cursor_b), -EIO);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &cursor_b), -EIO);

	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &cursor_a), 0);
	KUNIT_EXPECT_EQ(test, errseq_check_and_advance(&eseq, &cursor_b), 0);
}

static struct kunit_case errseq_test_cases[] = {
	KUNIT_CASE(errseq_test_zero_epoch_reports_no_error),
	KUNIT_CASE(errseq_test_set_records_error),
	KUNIT_CASE(errseq_test_set_overwrites_error),
	KUNIT_CASE(errseq_test_errno_range_extremes),
	KUNIT_CASE(errseq_test_sample_of_unseen_error_is_zero),
	KUNIT_CASE(errseq_test_new_sampler_sees_unseen_error),
	KUNIT_CASE(errseq_test_check_and_advance_reports_once),
	KUNIT_CASE(errseq_test_sample_after_seen_is_current),
	KUNIT_CASE(errseq_test_new_error_after_advance),
	KUNIT_CASE(errseq_test_same_error_reported_again_after_seen),
	KUNIT_CASE(errseq_test_repeat_error_visible_to_all_cursors),
	KUNIT_CASE(errseq_test_advance_stable_when_unchanged),
	KUNIT_CASE(errseq_test_two_subscribers_independent),
	{}
};

static struct kunit_suite errseq_test_suite = {
	.name = "errseq",
	.test_cases = errseq_test_cases,
};

kunit_test_suite(errseq_test_suite);

MODULE_DESCRIPTION("KUnit tests for the errseq infrastructure");
MODULE_LICENSE("GPL");
