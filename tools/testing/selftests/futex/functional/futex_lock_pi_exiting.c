// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 * futex_lock_pi_exiting.c
 *
 * Coverage for the FUTEX_LOCK_PI owner-exiting path.  futex_wait_timeout.c
 * already covers FUTEX_LOCK_PI timeout semantics and robust_list.c covers
 * owner death via the robust list, but nothing exercises FUTEX_LOCK_PI when a
 * non-robust PI owner exits while holding the lock, nor the basic ownership /
 * EDEADLK / unlock word semantics.
 *
 * DESCRIPTION
 *      Three tests:
 *
 *      1. lock_unlock_basic - uncontended FUTEX_LOCK_PI semantics: the futex
 *         word carries the owner TID, a recursive lock by the owner returns
 *         EDEADLK, and FUTEX_UNLOCK_PI clears the word.
 *
 *      2. owner_dies_with_blocked_waiter - a thread acquires a PI futex and
 *         exits while holding it.  do_exit() runs futex_cleanup_begin() (which
 *         flips the task's futex state to FUTEX_STATE_EXITING) and
 *         exit_pi_state_list() (which hands off / tears down the pi_state).  A
 *         contending FUTEX_LOCK_PI waiter must end up in one of:
 *
 *           0          - ownership was transferred to / acquired by the waiter
 *           EOWNERDEAD - previous owner died holding the lock; the caller is
 *                        now the owner and must acknowledge by unlocking
 *           ESRCH      - the owner encoded in the futex word is already gone
 *
 *         and on the first two it must actually own the lock afterwards.
 *
 *      3. stress_owner_exits - hammer that same exiting-owner path.  This is
 *         where the following bug lived: the 'exiting' task pointer was not
 *         reset at the retry label, so after wait_for_owner_exiting() dropped
 *         its reference a subsequent retry that returned a non-EBUSY error fed
 *         the stale pointer back in and tripped WARN_ON_ONCE(exiting).  That
 *         warning is invisible to user space, so this test cannot observe it
 *         through a syscall return value; it only becomes a visible failure
 *         (crash) on a kernel booted with panic_on_warn=1 (or built with
 *         CONFIG_BUG_ON_DATA_CORRUPTION).  The loop drives the path so that
 *         such a kernel trips on it - the canonical way fuzz/CI catch these.
 *
 *        Fix:    210d36d892de ("futex: Clear stale exiting pointer in
 *                              futex_lock_pi() retry path")
 *        Fixes:  3ef240eaff36 ("futex: Prevent exit livelock")
 *
 * AUTHOR
 *      Based on futex test boilerplate by Darren Hart <dvhart@linux.intel.com>
 *
 *****************************************************************************/

#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "futextest.h"
#include "kselftest_harness.h"

/*
 * Iterations for the stress variant.  Enough to repeatedly land in the narrow
 * EXITING window while keeping the test fast.
 */
#define STRESS_ITERS 1000

static futex_t pi_futex;
static pthread_barrier_t locked_barrier;
static pthread_barrier_t release_barrier;

static pid_t sys_gettid(void)
{
	return syscall(SYS_gettid);
}

/*
 * Owner thread: acquire the PI futex and exit while still holding it.  Two
 * modes:
 *   park == 0: signal that we hold the lock, then exit immediately (racy; the
 *              waiter races against our exit path).
 *   park == 1: signal that we hold the lock and keep holding until released
 *              via release_barrier, so a waiter has time to contend as a real
 *              PI waiter before we die.
 */
static void *owner_thread(void *arg)
{
	long park = (long)arg;

	if (futex_lock_pi(&pi_futex, NULL, 0, FUTEX_PRIVATE_FLAG) != 0)
		return (void *)(intptr_t)-errno;

	pthread_barrier_wait(&locked_barrier);

	if (park)
		pthread_barrier_wait(&release_barrier);

	/* Die while still holding the lock. */
	pthread_exit((void *)0);
}

/*
 * Block on the PI futex as a waiter.  Returns 0 on acquisition, otherwise the
 * positive errno.
 */
static int waiter_lock_pi(void)
{
	int ret = futex_lock_pi(&pi_futex, NULL, 0, FUTEX_PRIVATE_FLAG);

	return ret == 0 ? 0 : errno;
}

static int outcome_ok(int outcome)
{
	return outcome == 0 || outcome == EOWNERDEAD || outcome == ESRCH;
}

/* Results published by waiter_thread() for the owning thread to assert on. */
static int waiter_outcome;
static int waiter_owns;

/*
 * Waiter thread for the blocked-waiter test.  Contends for the lock and, when
 * it acquires, records whether the futex word actually carries its TID and
 * releases the lock itself (FUTEX_UNLOCK_PI must run in the owning thread).
 */
static void *waiter_thread(void *arg)
{
	pid_t tid = sys_gettid();

	waiter_outcome = waiter_lock_pi();
	if (waiter_outcome == 0 || waiter_outcome == EOWNERDEAD) {
		waiter_owns = (pi_futex & FUTEX_TID_MASK) == (futex_t)tid;
		futex_unlock_pi(&pi_futex, FUTEX_PRIVATE_FLAG);
	}
	return NULL;
}

FIXTURE(lock_pi_exiting) {
};

FIXTURE_SETUP(lock_pi_exiting) {
}

FIXTURE_TEARDOWN(lock_pi_exiting) {
}

/*
 * Uncontended FUTEX_LOCK_PI semantics, fully deterministic.
 */
TEST_F(lock_pi_exiting, lock_unlock_basic)
{
	pid_t tid = sys_gettid();
	int ret;

	pi_futex = FUTEX_INITIALIZER;

	/* Acquire: we become the owner, our TID lands in the futex word. */
	ret = futex_lock_pi(&pi_futex, NULL, 0, FUTEX_PRIVATE_FLAG);
	ASSERT_EQ(ret, 0)
		TH_LOG("lock failed: errno=%d (%s)", errno, strerror(errno));
	ASSERT_EQ(pi_futex & FUTEX_TID_MASK, (futex_t)tid)
		TH_LOG("owner TID not in futex word: 0x%08x", pi_futex);

	/* A recursive lock by the owner must be refused, not deadlock. */
	errno = 0;
	ret = futex_lock_pi(&pi_futex, NULL, 0, FUTEX_PRIVATE_FLAG);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EDEADLK)
		TH_LOG("recursive lock: expected EDEADLK, got errno=%d", errno);

	/* Release: the futex word is handed back clean. */
	ret = futex_unlock_pi(&pi_futex, FUTEX_PRIVATE_FLAG);
	ASSERT_EQ(ret, 0)
		TH_LOG("unlock failed: errno=%d", errno);
	ASSERT_EQ(pi_futex, (futex_t)0)
		TH_LOG("futex word not cleared after unlock: 0x%08x", pi_futex);
}

/*
 * A PI waiter inherits the lock when the owner dies holding it.
 *
 * The owner parks while holding the lock, this thread contends for it, then
 * the owner exits.  The waiter must come out cleanly (no hang, no unexpected
 * error) and, when it acquires, must actually own the lock.
 */
TEST_F(lock_pi_exiting, owner_dies_with_blocked_waiter)
{
	pthread_t owner, waiter;

	pthread_barrier_init(&locked_barrier, NULL, 2);
	pthread_barrier_init(&release_barrier, NULL, 2);
	pi_futex = FUTEX_INITIALIZER;
	waiter_outcome = -1;
	waiter_owns = 0;

	ASSERT_EQ(pthread_create(&owner, NULL, owner_thread, (void *)1), 0);

	/* Wait until the owner actually holds the lock. */
	pthread_barrier_wait(&locked_barrier);

	/* Start the waiter and give it time to block as a real PI waiter. */
	ASSERT_EQ(pthread_create(&waiter, NULL, waiter_thread, NULL), 0);
	usleep(1000);

	/* Release the owner so it dies while the waiter is queued on it. */
	pthread_barrier_wait(&release_barrier);

	pthread_join(waiter, NULL);
	pthread_join(owner, NULL);

	ASSERT_TRUE(outcome_ok(waiter_outcome)) {
		TH_LOG("unexpected FUTEX_LOCK_PI outcome: %d (%s)",
		       waiter_outcome, strerror(waiter_outcome));
	}
	if (waiter_outcome == 0 || waiter_outcome == EOWNERDEAD) {
		ASSERT_TRUE(waiter_owns)
			TH_LOG("waiter acquired but futex word lacks its TID");
	}

	pthread_barrier_destroy(&locked_barrier);
	pthread_barrier_destroy(&release_barrier);
}

/*
 * Stress: repeatedly let an owner exit while a waiter contends for the lock.
 *
 * Each iteration drives the FUTEX_STATE_EXITING -> -EBUSY -> retry path that
 * the stale-'exiting'-pointer bug lived on (210d36d892de).  The warning it
 * fixed is invisible to user space, so on a normally-configured kernel both
 * the buggy and fixed kernels pass here; the point is to make a kernel booted
 * with panic_on_warn=1 trip during one of these iterations.
 */
TEST_F(lock_pi_exiting, stress_owner_exits)
{
	for (int i = 0; i < STRESS_ITERS; i++) {
		pthread_t owner;
		int outcome;

		pthread_barrier_init(&locked_barrier, NULL, 2);
		pi_futex = FUTEX_INITIALIZER;

		ASSERT_EQ(pthread_create(&owner, NULL, owner_thread, (void *)0), 0);

		/* Owner holds the lock; race FUTEX_LOCK_PI against its exit. */
		pthread_barrier_wait(&locked_barrier);

		outcome = waiter_lock_pi();
		ASSERT_TRUE(outcome_ok(outcome)) {
			TH_LOG("iter %d: unexpected outcome %d (%s)",
			       i, outcome, strerror(outcome));
		}
		if (outcome == 0 || outcome == EOWNERDEAD)
			futex_unlock_pi(&pi_futex, FUTEX_PRIVATE_FLAG);

		pthread_join(owner, NULL);
		pthread_barrier_destroy(&locked_barrier);
	}
}

TEST_HARNESS_MAIN
