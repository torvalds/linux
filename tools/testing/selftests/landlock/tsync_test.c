// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Enforcing the same restrictions across multiple threads
 *
 * Copyright © 2025 Günther Noack <gnoack3000@gmail.com>
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <sys/prctl.h>
#include <linux/landlock.h>

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

TEST_HARNESS_MAIN
