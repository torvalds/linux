// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the errseq_t error-tracking infrastructure.
 *
 * Most cases exercise the documented single-threaded semantics of the
 * errseq API (see Documentation/core-api/errseq.rst and lib/errseq.c): error
 * recording and overwriting, the "seen" handoff between errseq_sample()
 * and errseq_check_and_advance(), and the re-reporting of an error that
 * is recorded again after it has been seen.
 *
 * The WARN path for invalid error values is not tested.  The last case
 * races errseq_set() against errseq_check_and_advance() on another CPU.
 */
#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/errseq.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/sched.h>

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

/*
 * errseq_check_and_advance() racing with errseq_set() on another CPU.
 *
 * Each round, cursor @f checks an unseen -EIO while another CPU records
 * -ENOSPC.  Afterwards -EIO is recorded again and cursor @g checks.  The
 * second -EIO is recorded after @f's check returned, so @f's next check
 * must report an error, whichever way the race with -ENOSPC went.
 */
struct errseq_race {
	errseq_t eseq;
	atomic_t go;		/* round the writer should run */
	atomic_t done;		/* last round the writer completed */
	unsigned int wdelay;
};

static void errseq_race_spin(unsigned int n)
{
	while (n--)
		cpu_relax();
}

static int errseq_race_writer(void *data)
{
	struct errseq_race *r = data;
	int seen = 0;

	while (!kthread_should_stop()) {
		int round = atomic_read_acquire(&r->go);

		if (round == seen) {
			cond_resched();
			continue;
		}
		seen = round;
		errseq_race_spin(READ_ONCE(r->wdelay));
		errseq_set(&r->eseq, -ENOSPC);
		atomic_set_release(&r->done, round);
	}
	return 0;
}

#define ERRSEQ_RACE_ROUNDS	2000000
#define ERRSEQ_RACE_SECONDS	5

static void errseq_test_check_and_advance_race(struct kunit *test)
{
	struct errseq_race *r;
	struct task_struct *writer;
	unsigned long deadline;
	int round, unreported = 0, missed = 0;

	if (num_online_cpus() < 2)
		kunit_skip(test, "needs at least two CPUs");

	r = kunit_kzalloc(test, sizeof(*r), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, r);

	writer = kthread_run(errseq_race_writer, r, "errseq_race");
	KUNIT_ASSERT_FALSE(test, IS_ERR(writer));

	deadline = jiffies + ERRSEQ_RACE_SECONDS * HZ;
	for (round = 1; round <= ERRSEQ_RACE_ROUNDS; round++) {
		errseq_t f = 0, g = 0;

		WRITE_ONCE(r->eseq, 0);
		errseq_set(&r->eseq, -EIO);
		WRITE_ONCE(r->wdelay, get_random_u32_below(64));
		atomic_set_release(&r->go, round);

		errseq_race_spin(get_random_u32_below(64));
		if (!errseq_check_and_advance(&r->eseq, &f))
			unreported++;

		while (atomic_read_acquire(&r->done) != round)
			cond_resched();

		/* Recorded after f's check returned: f must hear about it. */
		errseq_set(&r->eseq, -EIO);
		errseq_check_and_advance(&r->eseq, &g);
		if (!errseq_check_and_advance(&r->eseq, &f))
			missed++;

		if (time_after(jiffies, deadline))
			break;
		if (!(round & 1023))
			cond_resched();
	}
	kthread_stop(writer);

	kunit_info(test, "%d rounds, %d missed errors\n", round - 1, missed);
	KUNIT_EXPECT_EQ(test, unreported, 0);
	KUNIT_EXPECT_EQ(test, missed, 0);
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
	KUNIT_CASE_SLOW(errseq_test_check_and_advance_race),
	{}
};

static struct kunit_suite errseq_test_suite = {
	.name = "errseq",
	.test_cases = errseq_test_cases,
};

kunit_test_suite(errseq_test_suite);

MODULE_DESCRIPTION("KUnit tests for the errseq infrastructure");
MODULE_LICENSE("GPL");
