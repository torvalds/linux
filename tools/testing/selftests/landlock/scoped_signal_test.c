// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Signal Scoping
 *
 * Copyright © 2024 Tahera Fahimi <fahimitahera@gmail.com>
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "scoped_common.h"

/* This variable is used for handling several signals. */
static volatile sig_atomic_t is_signaled;

/* clang-format off */
FIXTURE(scoping_signals) {};
/* clang-format on */

FIXTURE_VARIANT(scoping_signals)
{
	int sig;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(scoping_signals, sigtrap) {
	/* clang-format on */
	.sig = SIGTRAP,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(scoping_signals, sigurg) {
	/* clang-format on */
	.sig = SIGURG,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(scoping_signals, sighup) {
	/* clang-format on */
	.sig = SIGHUP,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(scoping_signals, sigtstp) {
	/* clang-format on */
	.sig = SIGTSTP,
};

FIXTURE_SETUP(scoping_signals)
{
	drop_caps(_metadata);

	is_signaled = 0;
}

FIXTURE_TEARDOWN(scoping_signals)
{
}

static void scope_signal_handler(int sig, siginfo_t *info, void *ucontext)
{
	if (sig == SIGTRAP || sig == SIGURG || sig == SIGHUP || sig == SIGTSTP)
		is_signaled = 1;
}

/*
 * In this test, a child process sends a signal to parent before and
 * after getting scoped.
 */
TEST_F(scoping_signals, send_sig_to_parent)
{
	int pipe_parent[2];
	int status;
	pid_t child;
	pid_t parent = getpid();
	struct sigaction action = {
		.sa_sigaction = scope_signal_handler,
		.sa_flags = SA_SIGINFO,

	};

	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));
	ASSERT_LE(0, sigaction(variant->sig, &action, NULL));

	/* The process should not have already been signaled. */
	EXPECT_EQ(0, is_signaled);

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		char buf_child;
		int err;

		EXPECT_EQ(0, close(pipe_parent[1]));

		/*
		 * The child process can send signal to parent when
		 * domain is not scoped.
		 */
		err = kill(parent, variant->sig);
		ASSERT_EQ(0, err);
		ASSERT_EQ(1, read(pipe_parent[0], &buf_child, 1));
		EXPECT_EQ(0, close(pipe_parent[0]));

		create_scoped_domain(_metadata, LANDLOCK_SCOPE_SIGNAL);

		/*
		 * The child process cannot send signal to the parent
		 * anymore.
		 */
		err = kill(parent, variant->sig);
		ASSERT_EQ(-1, err);
		ASSERT_EQ(EPERM, errno);

		/*
		 * No matter of the domain, a process should be able to
		 * send a signal to itself.
		 */
		ASSERT_EQ(0, is_signaled);
		ASSERT_EQ(0, raise(variant->sig));
		ASSERT_EQ(1, is_signaled);

		_exit(_metadata->exit_code);
		return;
	}
	EXPECT_EQ(0, close(pipe_parent[0]));

	/* Waits for a first signal to be received, without race condition. */
	while (!is_signaled && !usleep(1))
		;
	ASSERT_EQ(1, is_signaled);
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	EXPECT_EQ(0, close(pipe_parent[1]));
	is_signaled = 0;

	ASSERT_EQ(child, waitpid(child, &status, 0));
	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->exit_code = KSFT_FAIL;

	EXPECT_EQ(0, is_signaled);
}

/* clang-format off */
FIXTURE(scoped_domains) {};
/* clang-format on */

#include "scoped_base_variants.h"

FIXTURE_SETUP(scoped_domains)
{
	drop_caps(_metadata);
}

FIXTURE_TEARDOWN(scoped_domains)
{
}

/*
 * This test ensures that a scoped process cannot send signal out of
 * scoped domain.
 */
TEST_F(scoped_domains, check_access_signal)
{
	pid_t child;
	pid_t parent = getpid();
	int status;
	bool can_signal_child, can_signal_parent;
	int pipe_parent[2], pipe_child[2];
	char buf_parent;
	int err;

	can_signal_parent = !variant->domain_child;
	can_signal_child = !variant->domain_parent;

	if (variant->domain_both)
		create_scoped_domain(_metadata, LANDLOCK_SCOPE_SIGNAL);

	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));
	ASSERT_EQ(0, pipe2(pipe_child, O_CLOEXEC));

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		char buf_child;

		EXPECT_EQ(0, close(pipe_child[0]));
		EXPECT_EQ(0, close(pipe_parent[1]));

		if (variant->domain_child)
			create_scoped_domain(_metadata, LANDLOCK_SCOPE_SIGNAL);

		ASSERT_EQ(1, write(pipe_child[1], ".", 1));
		EXPECT_EQ(0, close(pipe_child[1]));

		/* Waits for the parent to send signals. */
		ASSERT_EQ(1, read(pipe_parent[0], &buf_child, 1));
		EXPECT_EQ(0, close(pipe_parent[0]));

		err = kill(parent, 0);
		if (can_signal_parent) {
			ASSERT_EQ(0, err);
		} else {
			ASSERT_EQ(-1, err);
			ASSERT_EQ(EPERM, errno);
		}
		/*
		 * No matter of the domain, a process should be able to
		 * send a signal to itself.
		 */
		ASSERT_EQ(0, raise(0));

		_exit(_metadata->exit_code);
		return;
	}
	EXPECT_EQ(0, close(pipe_parent[0]));
	EXPECT_EQ(0, close(pipe_child[1]));

	if (variant->domain_parent)
		create_scoped_domain(_metadata, LANDLOCK_SCOPE_SIGNAL);

	ASSERT_EQ(1, read(pipe_child[0], &buf_parent, 1));
	EXPECT_EQ(0, close(pipe_child[0]));

	err = kill(child, 0);
	if (can_signal_child) {
		ASSERT_EQ(0, err);
	} else {
		ASSERT_EQ(-1, err);
		ASSERT_EQ(EPERM, errno);
	}
	ASSERT_EQ(0, raise(0));

	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	EXPECT_EQ(0, close(pipe_parent[1]));
	ASSERT_EQ(child, waitpid(child, &status, 0));

	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->exit_code = KSFT_FAIL;
}

TEST_HARNESS_MAIN
