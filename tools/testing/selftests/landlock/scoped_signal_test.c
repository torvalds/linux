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
#include <pthread.h>
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

enum thread_return {
	THREAD_INVALID = 0,
	THREAD_SUCCESS = 1,
	THREAD_ERROR = 2,
};

static void *thread_sync(void *arg)
{
	const int pipe_read = *(int *)arg;
	char buf;

	if (read(pipe_read, &buf, 1) != 1)
		return (void *)THREAD_ERROR;

	return (void *)THREAD_SUCCESS;
}

TEST(signal_scoping_thread_before)
{
	pthread_t no_sandbox_thread;
	enum thread_return ret = THREAD_INVALID;
	int thread_pipe[2];

	drop_caps(_metadata);
	ASSERT_EQ(0, pipe2(thread_pipe, O_CLOEXEC));

	ASSERT_EQ(0, pthread_create(&no_sandbox_thread, NULL, thread_sync,
				    &thread_pipe[0]));

	/* Enforces restriction after creating the thread. */
	create_scoped_domain(_metadata, LANDLOCK_SCOPE_SIGNAL);

	EXPECT_EQ(0, pthread_kill(no_sandbox_thread, 0));
	EXPECT_EQ(1, write(thread_pipe[1], ".", 1));

	EXPECT_EQ(0, pthread_join(no_sandbox_thread, (void **)&ret));
	EXPECT_EQ(THREAD_SUCCESS, ret);

	EXPECT_EQ(0, close(thread_pipe[0]));
	EXPECT_EQ(0, close(thread_pipe[1]));
}

TEST(signal_scoping_thread_after)
{
	pthread_t scoped_thread;
	enum thread_return ret = THREAD_INVALID;
	int thread_pipe[2];

	drop_caps(_metadata);
	ASSERT_EQ(0, pipe2(thread_pipe, O_CLOEXEC));

	/* Enforces restriction before creating the thread. */
	create_scoped_domain(_metadata, LANDLOCK_SCOPE_SIGNAL);

	ASSERT_EQ(0, pthread_create(&scoped_thread, NULL, thread_sync,
				    &thread_pipe[0]));

	EXPECT_EQ(0, pthread_kill(scoped_thread, 0));
	EXPECT_EQ(1, write(thread_pipe[1], ".", 1));

	EXPECT_EQ(0, pthread_join(scoped_thread, (void **)&ret));
	EXPECT_EQ(THREAD_SUCCESS, ret);

	EXPECT_EQ(0, close(thread_pipe[0]));
	EXPECT_EQ(0, close(thread_pipe[1]));
}

const short backlog = 10;

static volatile sig_atomic_t signal_received;

static void handle_sigurg(int sig)
{
	if (sig == SIGURG)
		signal_received = 1;
	else
		signal_received = -1;
}

static int setup_signal_handler(int signal)
{
	struct sigaction sa = {
		.sa_handler = handle_sigurg,
	};

	if (sigemptyset(&sa.sa_mask))
		return -1;

	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	return sigaction(SIGURG, &sa, NULL);
}

/* clang-format off */
FIXTURE(fown) {};
/* clang-format on */

enum fown_sandbox {
	SANDBOX_NONE,
	SANDBOX_BEFORE_FORK,
	SANDBOX_BEFORE_SETOWN,
	SANDBOX_AFTER_SETOWN,
};

FIXTURE_VARIANT(fown)
{
	const enum fown_sandbox sandbox_setown;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(fown, no_sandbox) {
	/* clang-format on */
	.sandbox_setown = SANDBOX_NONE,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(fown, sandbox_before_fork) {
	/* clang-format on */
	.sandbox_setown = SANDBOX_BEFORE_FORK,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(fown, sandbox_before_setown) {
	/* clang-format on */
	.sandbox_setown = SANDBOX_BEFORE_SETOWN,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(fown, sandbox_after_setown) {
	/* clang-format on */
	.sandbox_setown = SANDBOX_AFTER_SETOWN,
};

FIXTURE_SETUP(fown)
{
	drop_caps(_metadata);
}

FIXTURE_TEARDOWN(fown)
{
}

/*
 * Sending an out of bound message will trigger the SIGURG signal
 * through file_send_sigiotask.
 */
TEST_F(fown, sigurg_socket)
{
	int server_socket, recv_socket;
	struct service_fixture server_address;
	char buffer_parent;
	int status;
	int pipe_parent[2], pipe_child[2];
	pid_t child;

	memset(&server_address, 0, sizeof(server_address));
	set_unix_address(&server_address, 0);

	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));
	ASSERT_EQ(0, pipe2(pipe_child, O_CLOEXEC));

	if (variant->sandbox_setown == SANDBOX_BEFORE_FORK)
		create_scoped_domain(_metadata, LANDLOCK_SCOPE_SIGNAL);

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		int client_socket;
		char buffer_child;

		EXPECT_EQ(0, close(pipe_parent[1]));
		EXPECT_EQ(0, close(pipe_child[0]));

		ASSERT_EQ(0, setup_signal_handler(SIGURG));
		client_socket = socket(AF_UNIX, SOCK_STREAM, 0);
		ASSERT_LE(0, client_socket);

		/* Waits for the parent to listen. */
		ASSERT_EQ(1, read(pipe_parent[0], &buffer_child, 1));
		ASSERT_EQ(0, connect(client_socket, &server_address.unix_addr,
				     server_address.unix_addr_len));

		/*
		 * Waits for the parent to accept the connection, sandbox
		 * itself, and call fcntl(2).
		 */
		ASSERT_EQ(1, read(pipe_parent[0], &buffer_child, 1));
		/* May signal itself. */
		ASSERT_EQ(1, send(client_socket, ".", 1, MSG_OOB));
		EXPECT_EQ(0, close(client_socket));
		ASSERT_EQ(1, write(pipe_child[1], ".", 1));
		EXPECT_EQ(0, close(pipe_child[1]));

		/* Waits for the message to be received. */
		ASSERT_EQ(1, read(pipe_parent[0], &buffer_child, 1));
		EXPECT_EQ(0, close(pipe_parent[0]));

		if (variant->sandbox_setown == SANDBOX_BEFORE_SETOWN) {
			ASSERT_EQ(0, signal_received);
		} else {
			/*
			 * A signal is only received if fcntl(F_SETOWN) was
			 * called before any sandboxing or if the signal
			 * receiver is in the same domain.
			 */
			ASSERT_EQ(1, signal_received);
		}
		_exit(_metadata->exit_code);
		return;
	}
	EXPECT_EQ(0, close(pipe_parent[0]));
	EXPECT_EQ(0, close(pipe_child[1]));

	server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	ASSERT_LE(0, server_socket);
	ASSERT_EQ(0, bind(server_socket, &server_address.unix_addr,
			  server_address.unix_addr_len));
	ASSERT_EQ(0, listen(server_socket, backlog));
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));

	recv_socket = accept(server_socket, NULL, NULL);
	ASSERT_LE(0, recv_socket);

	if (variant->sandbox_setown == SANDBOX_BEFORE_SETOWN)
		create_scoped_domain(_metadata, LANDLOCK_SCOPE_SIGNAL);

	/*
	 * Sets the child to receive SIGURG for MSG_OOB.  This uncommon use is
	 * a valid attack scenario which also simplifies this test.
	 */
	ASSERT_EQ(0, fcntl(recv_socket, F_SETOWN, child));

	if (variant->sandbox_setown == SANDBOX_AFTER_SETOWN)
		create_scoped_domain(_metadata, LANDLOCK_SCOPE_SIGNAL);

	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));

	/* Waits for the child to send MSG_OOB. */
	ASSERT_EQ(1, read(pipe_child[0], &buffer_parent, 1));
	EXPECT_EQ(0, close(pipe_child[0]));
	ASSERT_EQ(1, recv(recv_socket, &buffer_parent, 1, MSG_OOB));
	EXPECT_EQ(0, close(recv_socket));
	EXPECT_EQ(0, close(server_socket));
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	EXPECT_EQ(0, close(pipe_parent[1]));

	ASSERT_EQ(child, waitpid(child, &status, 0));
	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->exit_code = KSFT_FAIL;
}

TEST_HARNESS_MAIN
