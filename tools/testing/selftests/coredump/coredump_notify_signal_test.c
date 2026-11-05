// SPDX-License-Identifier: GPL-2.0

/*
 * A coredump is meant to be interrupted by SIGKILL and by the freezer and
 * by nothing else. dump_interrupted() says so, but the blocking waits
 * underneath it test signal_pending(), which is also true for
 * TIF_NOTIFY_SIGNAL. A crashing task that has an io_uring completion land
 * on it mid-dump therefore keeps dumping while every wait it enters bails
 * out at once, and the dump is silently cut short. Nothing reports it:
 * binfmt_elf sets has_dumped before it writes anything, so WCOREDUMP()
 * says the dump worked.
 *
 * A coredump note is the only dump_emit() that exceeds what the transport
 * takes in one go, so it is the one write that is certain to block. Both
 * tests crash a child holding enough file backed mappings for its NT_FILE
 * note to run past that, arm an io_uring poll on it, and trip the poll
 * while the note is being written. What comes out has to be the whole
 * dump.
 */

#include <fcntl.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "coredump_test.h"

FIXTURE_SETUP(coredump)
{
	FILE *file;
	int ret;

	self->pid_coredump_server = -ESRCH;
	self->fd_tmpfs_detached = -1;
	file = fopen("/proc/sys/kernel/core_pattern", "r");
	ASSERT_NE(NULL, file);

	ret = fread(self->original_core_pattern, 1,
		    sizeof(self->original_core_pattern), file);
	ASSERT_TRUE(ret || feof(file));
	ASSERT_LT(ret, sizeof(self->original_core_pattern));

	self->original_core_pattern[ret] = '\0';
	self->fd_tmpfs_detached = create_detached_tmpfs();
	ASSERT_GE(self->fd_tmpfs_detached, 0);

	ret = fclose(file);
	ASSERT_EQ(0, ret);

	/* A stale core file from a killed previous run would fake a pass. */
	unlink(NOTIFY_SIGNAL_CORE_FILE);
	/* And a stale socket would fail the server's bind. */
	unlink(NOTIFY_SIGNAL_SOCKET);
	unlink(NOTIFY_SIGNAL_TRIGGER);
	ASSERT_EQ(mkfifo(NOTIFY_SIGNAL_TRIGGER, 0600), 0);
}

FIXTURE_TEARDOWN(coredump)
{
	const char *reason;
	FILE *file;
	int ret, status;

	if (self->pid_coredump_server > 0) {
		kill(self->pid_coredump_server, SIGTERM);
		waitpid(self->pid_coredump_server, &status, 0);
	}
	unlink(NOTIFY_SIGNAL_CORE_FILE);
	unlink(NOTIFY_SIGNAL_CORE_TMPFILE);
	unlink(NOTIFY_SIGNAL_SOCKET);
	unlink(NOTIFY_SIGNAL_TRIGGER);
	unlink(NOTIFY_SIGNAL_MAPFILE);

	file = fopen("/proc/sys/kernel/core_pattern", "w");
	if (!file) {
		reason = "Unable to open core_pattern";
		goto fail;
	}

	ret = fprintf(file, "%s", self->original_core_pattern);
	if (ret < 0) {
		reason = "Unable to write to core_pattern";
		goto fail;
	}

	ret = fclose(file);
	if (ret) {
		reason = "Unable to close core_pattern";
		goto fail;
	}

	if (self->fd_tmpfs_detached >= 0) {
		ret = close(self->fd_tmpfs_detached);
		if (ret < 0) {
			reason = "Unable to close detached tmpfs";
			goto fail;
		}
		self->fd_tmpfs_detached = -1;
	}

	return;
fail:
	/* This should never happen */
	fprintf(stderr, "Failed to cleanup coredump test: %s\n", reason);
}

/* Check that what the helper or the server saved is the whole dump. */
static void check_whole_coredump(struct __test_metadata *const _metadata)
{
	long long expected;
	struct stat st;

	expected = coredump_expected_size(NOTIFY_SIGNAL_CORE_FILE);
	ASSERT_GT(expected, 0);
	ASSERT_EQ(stat(NOTIFY_SIGNAL_CORE_FILE, &st), 0);
	ASSERT_EQ((long long)st.st_size, expected);
}

/*
 * The dump goes to a |helper, so the reader is a separate program the
 * kernel spawns. It saves what it received to NOTIFY_SIGNAL_CORE_FILE.
 */
TEST_F(coredump, notify_signal_pipe)
{
	char pattern[PATH_MAX], helper[PATH_MAX], *p;
	struct stat st;
	int status, i;
	pid_t pid;
	ssize_t n;

	if (!coredump_io_uring_available())
		SKIP(return, "io_uring not available");

	n = readlink("/proc/self/exe", helper, sizeof(helper) - 1);
	ASSERT_GT(n, 0);
	helper[n] = '\0';
	p = strstr(helper, "coredump_notify_signal_test");
	ASSERT_NE(p, NULL);
	ASSERT_LE((size_t)(p - helper) + sizeof("coredump_notify_signal_helper"),
		  sizeof(helper));
	strcpy(p, "coredump_notify_signal_helper");
	if (access(helper, X_OK))
		SKIP(return, "coredump_notify_signal_helper not built");

	ASSERT_LT(snprintf(pattern, sizeof(pattern), "|%s", helper),
		  (int)sizeof(pattern));
	ASSERT_TRUE(set_core_pattern(pattern));

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		crashing_child_notify_signal();

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFSIGNALED(status));

	/* The kernel does not wait for the helper, so poll for it. */
	for (i = 0; i < 100; i++) {
		if (!stat(NOTIFY_SIGNAL_CORE_FILE, &st) && st.st_size)
			break;
		usleep(100000);
	}

	check_whole_coredump(_metadata);
}

/* The same thing with the dump going to a coredump socket. */
TEST_F(coredump, notify_signal_socket)
{
	pid_t pid, pid_coredump_server;
	int ipc_sockets[2], status;
	char pattern[PATH_MAX];
	char c;

	if (!coredump_io_uring_available())
		SKIP(return, "io_uring not available");

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0,
			     ipc_sockets), 0);
	ASSERT_LT(snprintf(pattern, sizeof(pattern), "@%s",
			   NOTIFY_SIGNAL_SOCKET), (int)sizeof(pattern));
	ASSERT_TRUE(set_core_pattern(pattern));

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, fd_coredump = -1, fd_core_file = -1;
		int exit_code = EXIT_FAILURE;

		close(ipc_sockets[0]);

		fd_server = create_and_listen_unix_socket(NOTIFY_SIGNAL_SOCKET);
		if (fd_server < 0)
			goto out;
		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			goto out;
		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0)
			goto out;

		fd_core_file = open(NOTIFY_SIGNAL_CORE_FILE,
				    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
				    0600);
		if (fd_core_file < 0)
			goto out;

		if (recv_coredump_notify_signal(fd_coredump, fd_core_file,
						true) < 0)
			goto out;

		exit_code = EXIT_SUCCESS;
out:
		if (fd_core_file >= 0)
			close(fd_core_file);
		if (fd_coredump >= 0)
			close(fd_coredump);
		if (fd_server >= 0)
			close(fd_server);
		_exit(exit_code);
	}
	self->pid_coredump_server = pid_coredump_server;

	EXPECT_EQ(close(ipc_sockets[1]), 0);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	EXPECT_EQ(close(ipc_sockets[0]), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		crashing_child_notify_signal();

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFSIGNALED(status));

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);

	check_whole_coredump(_metadata);
}

TEST_HARNESS_MAIN
