// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Enforcing the same restrictions across multiple threads
 *
 * Copyright © 2025 Günther Noack <gnoack3000@gmail.com>
 */

#define _GNU_SOURCE
#include <linux/landlock.h>
#include <pthread.h>
#include <signal.h>
#include <sys/prctl.h>

#include "common.h"

/* create_ruleset - Create a simple ruleset FD common to all tests */
static int create_ruleset(struct __test_metadata *const _metadata)
{
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = (LANDLOCK_ACCESS_FS_WRITE_FILE |
				      LANDLOCK_ACCESS_FS_TRUNCATE),
	};
	const int ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);

	ASSERT_LE(0, ruleset_fd)
	{
		TH_LOG("landlock_create_ruleset: %s", strerror(errno));
	}
	return ruleset_fd;
}

TEST(single_threaded_success)
{
	const int ruleset_fd = create_ruleset(_metadata);

	disable_caps(_metadata);

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
	ASSERT_EQ(0, landlock_restrict_self(ruleset_fd,
					    LANDLOCK_RESTRICT_SELF_TSYNC));

	EXPECT_EQ(0, close(ruleset_fd));
}

static void store_no_new_privs(void *data)
{
	bool *nnp = data;

	if (!nnp)
		return;
	*nnp = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
}

static void *idle(void *data)
{
	pthread_cleanup_push(store_no_new_privs, data);

	while (true)
		sleep(1);

	pthread_cleanup_pop(1);
}

TEST(multi_threaded_success)
{
	pthread_t t1, t2;
	bool no_new_privs1, no_new_privs2;
	const int ruleset_fd = create_ruleset(_metadata);

	disable_caps(_metadata);

	ASSERT_EQ(0, pthread_create(&t1, NULL, idle, &no_new_privs1));
	ASSERT_EQ(0, pthread_create(&t2, NULL, idle, &no_new_privs2));

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));

	EXPECT_EQ(0, landlock_restrict_self(ruleset_fd,
					    LANDLOCK_RESTRICT_SELF_TSYNC));

	ASSERT_EQ(0, pthread_cancel(t1));
	ASSERT_EQ(0, pthread_cancel(t2));
	ASSERT_EQ(0, pthread_join(t1, NULL));
	ASSERT_EQ(0, pthread_join(t2, NULL));

	/* The no_new_privs flag was implicitly enabled on all threads. */
	EXPECT_TRUE(no_new_privs1);
	EXPECT_TRUE(no_new_privs2);

	EXPECT_EQ(0, close(ruleset_fd));
}

TEST(multi_threaded_success_despite_diverging_domains)
{
	pthread_t t1, t2;
	const int ruleset_fd = create_ruleset(_metadata);

	disable_caps(_metadata);

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));

	ASSERT_EQ(0, pthread_create(&t1, NULL, idle, NULL));
	ASSERT_EQ(0, pthread_create(&t2, NULL, idle, NULL));

	/*
	 * The main thread enforces a ruleset,
	 * thereby bringing the threads' Landlock domains out of sync.
	 */
	EXPECT_EQ(0, landlock_restrict_self(ruleset_fd, 0));

	/* Still, TSYNC succeeds, bringing the threads in sync again. */
	EXPECT_EQ(0, landlock_restrict_self(ruleset_fd,
					    LANDLOCK_RESTRICT_SELF_TSYNC));

	ASSERT_EQ(0, pthread_cancel(t1));
	ASSERT_EQ(0, pthread_cancel(t2));
	ASSERT_EQ(0, pthread_join(t1, NULL));
	ASSERT_EQ(0, pthread_join(t2, NULL));
	EXPECT_EQ(0, close(ruleset_fd));
}

struct thread_restrict_data {
	pthread_t t;
	int ruleset_fd;
	int result;
};

static void *thread_restrict(void *data)
{
	struct thread_restrict_data *d = data;

	d->result = landlock_restrict_self(d->ruleset_fd,
					   LANDLOCK_RESTRICT_SELF_TSYNC);
	return NULL;
}

TEST(competing_enablement)
{
	const int ruleset_fd = create_ruleset(_metadata);
	struct thread_restrict_data d[] = {
		{ .ruleset_fd = ruleset_fd },
		{ .ruleset_fd = ruleset_fd },
	};

	disable_caps(_metadata);

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
	ASSERT_EQ(0, pthread_create(&d[0].t, NULL, thread_restrict, &d[0]));
	ASSERT_EQ(0, pthread_create(&d[1].t, NULL, thread_restrict, &d[1]));

	/* Wait for threads to finish. */
	ASSERT_EQ(0, pthread_join(d[0].t, NULL));
	ASSERT_EQ(0, pthread_join(d[1].t, NULL));

	/* Expect that both succeeded. */
	EXPECT_EQ(0, d[0].result);
	EXPECT_EQ(0, d[1].result);

	EXPECT_EQ(0, close(ruleset_fd));
}

static void signal_nop_handler(int sig)
{
}

struct signaler_data {
	pthread_t target;
	volatile bool stop;
};

static void *signaler_thread(void *data)
{
	struct signaler_data *sd = data;

	while (!sd->stop)
		pthread_kill(sd->target, SIGUSR1);

	return NULL;
}

/*
 * Number of idle sibling threads.  This must be large enough that even on
 * machines with many cores, the sibling threads cannot all complete their
 * credential preparation in a single parallel wave, otherwise the signaler
 * thread has no window to interrupt wait_for_completion_interruptible().
 * 200 threads on a 64-core machine yields ~3 serialized waves, giving the
 * tight signal loop enough time to land an interruption.
 */
#define NUM_IDLE_THREADS 200

/*
 * Exercises the tsync interruption and cancellation paths in tsync.c.
 *
 * When a signal interrupts the calling thread while it waits for sibling
 * threads to finish their credential preparation
 * (wait_for_completion_interruptible in landlock_restrict_sibling_threads),
 * the kernel sets ERESTARTNOINTR, cancels queued task works that have not
 * started yet (cancel_tsync_works), then waits for the remaining works to
 * finish.  On the error return, syscalls.c aborts the prepared credentials.
 * The kernel automatically restarts the syscall, so userspace sees success.
 */
TEST(tsync_interrupt)
{
	size_t i;
	pthread_t threads[NUM_IDLE_THREADS];
	pthread_t signaler;
	struct signaler_data sd;
	struct sigaction sa = {};
	const int ruleset_fd = create_ruleset(_metadata);

	disable_caps(_metadata);

	/* Install a no-op SIGUSR1 handler so the signal does not kill us. */
	sa.sa_handler = signal_nop_handler;
	sigemptyset(&sa.sa_mask);
	ASSERT_EQ(0, sigaction(SIGUSR1, &sa, NULL));

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));

	for (i = 0; i < NUM_IDLE_THREADS; i++)
		ASSERT_EQ(0, pthread_create(&threads[i], NULL, idle, NULL));

	/*
	 * Start a signaler thread that continuously sends SIGUSR1 to the
	 * calling thread.  This maximizes the chance of interrupting
	 * wait_for_completion_interruptible() in the kernel's tsync path.
	 */
	sd.target = pthread_self();
	sd.stop = false;
	ASSERT_EQ(0, pthread_create(&signaler, NULL, signaler_thread, &sd));

	/*
	 * The syscall may be interrupted and transparently restarted by the
	 * kernel (ERESTARTNOINTR).  From userspace, it should always succeed.
	 */
	EXPECT_EQ(0, landlock_restrict_self(ruleset_fd,
					    LANDLOCK_RESTRICT_SELF_TSYNC));

	sd.stop = true;
	ASSERT_EQ(0, pthread_join(signaler, NULL));

	for (i = 0; i < NUM_IDLE_THREADS; i++) {
		ASSERT_EQ(0, pthread_cancel(threads[i]));
		ASSERT_EQ(0, pthread_join(threads[i], NULL));
	}

	EXPECT_EQ(0, close(ruleset_fd));
}

/* clang-format off */
FIXTURE(tsync_without_ruleset) {};
/* clang-format on */

FIXTURE_VARIANT(tsync_without_ruleset)
{
	const __u32 flags;
	const int expected_errno;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tsync_without_ruleset, tsync_only) {
	/* clang-format on */
	.flags = LANDLOCK_RESTRICT_SELF_TSYNC,
	.expected_errno = EBADF,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tsync_without_ruleset, subdomains_off_same_exec_off) {
	/* clang-format on */
	.flags = LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF |
		 LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF |
		 LANDLOCK_RESTRICT_SELF_TSYNC,
	.expected_errno = EBADF,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tsync_without_ruleset, subdomains_off_new_exec_on) {
	/* clang-format on */
	.flags = LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF |
		 LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON |
		 LANDLOCK_RESTRICT_SELF_TSYNC,
	.expected_errno = EBADF,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tsync_without_ruleset, all_flags) {
	/* clang-format on */
	.flags = LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF |
		 LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON |
		 LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF |
		 LANDLOCK_RESTRICT_SELF_TSYNC,
	.expected_errno = EBADF,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tsync_without_ruleset, subdomains_off) {
	/* clang-format on */
	.flags = LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF |
		 LANDLOCK_RESTRICT_SELF_TSYNC,
	.expected_errno = 0,
};

FIXTURE_SETUP(tsync_without_ruleset)
{
	disable_caps(_metadata);
}

FIXTURE_TEARDOWN(tsync_without_ruleset)
{
}

TEST_F(tsync_without_ruleset, check)
{
	int ret;

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));

	ret = landlock_restrict_self(-1, variant->flags);
	if (variant->expected_errno) {
		EXPECT_EQ(-1, ret);
		EXPECT_EQ(variant->expected_errno, errno);
	} else {
		EXPECT_EQ(0, ret);
	}
}

TEST_HARNESS_MAIN
