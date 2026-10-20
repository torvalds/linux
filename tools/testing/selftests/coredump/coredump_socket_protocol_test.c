// SPDX-License-Identifier: GPL-2.0

#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "coredump_test.h"

#define NUM_CRASHING_COREDUMPS 5

FIXTURE_SETUP(coredump)
{
	FILE *file;
	int ret;

	self->pid_coredump_server = -ESRCH;
	self->fd_tmpfs_detached = -1;
	file = fopen("/proc/sys/kernel/core_pattern", "r");
	ASSERT_NE(NULL, file);

	ret = fread(self->original_core_pattern, 1, sizeof(self->original_core_pattern), file);
	ASSERT_TRUE(ret || feof(file));
	ASSERT_LT(ret, sizeof(self->original_core_pattern));

	self->original_core_pattern[ret] = '\0';
	self->fd_tmpfs_detached = create_detached_tmpfs();
	ASSERT_GE(self->fd_tmpfs_detached, 0);

	ret = fclose(file);
	ASSERT_EQ(0, ret);
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
	unlink("/tmp/coredump.file");
	unlink("/tmp/coredump.socket");

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

TEST_F(coredump, socket_request_kernel)
{
	int pidfd, ret, status;
	pid_t pid, pid_coredump_server;
	struct stat st;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;

	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		struct coredump_req req = {};
		int fd_server = -1, fd_coredump = -1, fd_core_file = -1, fd_peer_pidfd = -1;
		int exit_code = EXIT_FAILURE;

		close(ipc_sockets[0]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0) {
			fprintf(stderr, "socket_request_kernel: create_and_listen_unix_socket failed: %m\n");
			goto out;
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			fprintf(stderr, "socket_request_kernel: write_nointr to ipc socket failed: %m\n");
			goto out;
		}

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "socket_request_kernel: accept4 failed: %m\n");
			goto out;
		}

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0) {
			fprintf(stderr, "socket_request_kernel: get_peer_pidfd failed\n");
			goto out;
		}

		if (!get_pidfd_info(fd_peer_pidfd, &info)) {
			fprintf(stderr, "socket_request_kernel: get_pidfd_info failed\n");
			goto out;
		}

		if (!(info.mask & PIDFD_INFO_COREDUMP)) {
			fprintf(stderr, "socket_request_kernel: PIDFD_INFO_COREDUMP not set in mask\n");
			goto out;
		}

		if (!(info.coredump_mask & PIDFD_COREDUMPED)) {
			fprintf(stderr, "socket_request_kernel: PIDFD_COREDUMPED not set in coredump_mask\n");
			goto out;
		}

		fd_core_file = creat("/tmp/coredump.file", 0644);
		if (fd_core_file < 0) {
			fprintf(stderr, "socket_request_kernel: creat coredump file failed: %m\n");
			goto out;
		}

		if (!read_coredump_req(fd_coredump, &req)) {
			fprintf(stderr, "socket_request_kernel: read_coredump_req failed\n");
			goto out;
		}

		if (!check_coredump_req(&req)) {
			fprintf(stderr, "socket_request_kernel: check_coredump_req failed\n");
			goto out;
		}

		if (!send_coredump_ack(fd_coredump, &req,
				       COREDUMP_KERNEL | COREDUMP_WAIT, 0)) {
			fprintf(stderr, "socket_request_kernel: send_coredump_ack failed\n");
			goto out;
		}

		if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK)) {
			fprintf(stderr, "socket_request_kernel: read_marker COREDUMP_MARK_REQACK failed\n");
			goto out;
		}

		for (;;) {
			char buffer[4096];
			ssize_t bytes_read, bytes_write;

			bytes_read = read(fd_coredump, buffer, sizeof(buffer));
			if (bytes_read < 0) {
				fprintf(stderr, "socket_request_kernel: read from coredump socket failed: %m\n");
				goto out;
			}

			if (bytes_read == 0)
				break;

			bytes_write = write(fd_core_file, buffer, bytes_read);
			if (bytes_read != bytes_write) {
				if (bytes_write < 0 && errno == ENOSPC)
					continue;
				fprintf(stderr, "socket_request_kernel: write to core file failed (read=%zd, write=%zd): %m\n",
					bytes_read, bytes_write);
				goto out;
			}
		}

		exit_code = EXIT_SUCCESS;
		fprintf(stderr, "socket_request_kernel: completed successfully\n");
out:
		if (fd_core_file >= 0)
			close(fd_core_file);
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
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
		crashing_child();

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_TRUE(WCOREDUMP(status));

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);

	ASSERT_EQ(stat("/tmp/coredump.file", &st), 0);
	ASSERT_GT(st.st_size, 0);
	system("file /tmp/coredump.file");
}

TEST_F(coredump, socket_request_userspace)
{
	int pidfd, ret, status;
	pid_t pid, pid_coredump_server;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;

	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		struct coredump_req req = {};
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1;
		int exit_code = EXIT_FAILURE;

		close(ipc_sockets[0]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0) {
			fprintf(stderr, "socket_request_userspace: create_and_listen_unix_socket failed: %m\n");
			goto out;
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			fprintf(stderr, "socket_request_userspace: write_nointr to ipc socket failed: %m\n");
			goto out;
		}

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "socket_request_userspace: accept4 failed: %m\n");
			goto out;
		}

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0) {
			fprintf(stderr, "socket_request_userspace: get_peer_pidfd failed\n");
			goto out;
		}

		if (!get_pidfd_info(fd_peer_pidfd, &info)) {
			fprintf(stderr, "socket_request_userspace: get_pidfd_info failed\n");
			goto out;
		}

		if (!(info.mask & PIDFD_INFO_COREDUMP)) {
			fprintf(stderr, "socket_request_userspace: PIDFD_INFO_COREDUMP not set in mask\n");
			goto out;
		}

		if (!(info.coredump_mask & PIDFD_COREDUMPED)) {
			fprintf(stderr, "socket_request_userspace: PIDFD_COREDUMPED not set in coredump_mask\n");
			goto out;
		}

		if (!read_coredump_req(fd_coredump, &req)) {
			fprintf(stderr, "socket_request_userspace: read_coredump_req failed\n");
			goto out;
		}

		if (!check_coredump_req(&req)) {
			fprintf(stderr, "socket_request_userspace: check_coredump_req failed\n");
			goto out;
		}

		if (!send_coredump_ack(fd_coredump, &req,
				       COREDUMP_USERSPACE | COREDUMP_WAIT, 0)) {
			fprintf(stderr, "socket_request_userspace: send_coredump_ack failed\n");
			goto out;
		}

		if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK)) {
			fprintf(stderr, "socket_request_userspace: read_marker COREDUMP_MARK_REQACK failed\n");
			goto out;
		}

		for (;;) {
			char buffer[4096];
			ssize_t bytes_read;

			bytes_read = read(fd_coredump, buffer, sizeof(buffer));
			if (bytes_read > 0) {
				fprintf(stderr, "socket_request_userspace: unexpected data received (expected no coredump data)\n");
				goto out;
			}

			if (bytes_read < 0) {
				fprintf(stderr, "socket_request_userspace: read from coredump socket failed: %m\n");
				goto out;
			}

			if (bytes_read == 0)
				break;
		}

		exit_code = EXIT_SUCCESS;
		fprintf(stderr, "socket_request_userspace: completed successfully\n");
out:
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
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
		crashing_child();

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_TRUE(WCOREDUMP(status));

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

TEST_F(coredump, socket_request_reject)
{
	int pidfd, ret, status;
	pid_t pid, pid_coredump_server;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;

	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		struct coredump_req req = {};
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1;
		int exit_code = EXIT_FAILURE;

		close(ipc_sockets[0]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0) {
			fprintf(stderr, "socket_request_reject: create_and_listen_unix_socket failed: %m\n");
			goto out;
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			fprintf(stderr, "socket_request_reject: write_nointr to ipc socket failed: %m\n");
			goto out;
		}

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "socket_request_reject: accept4 failed: %m\n");
			goto out;
		}

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0) {
			fprintf(stderr, "socket_request_reject: get_peer_pidfd failed\n");
			goto out;
		}

		if (!get_pidfd_info(fd_peer_pidfd, &info)) {
			fprintf(stderr, "socket_request_reject: get_pidfd_info failed\n");
			goto out;
		}

		if (!(info.mask & PIDFD_INFO_COREDUMP)) {
			fprintf(stderr, "socket_request_reject: PIDFD_INFO_COREDUMP not set in mask\n");
			goto out;
		}

		if (!(info.coredump_mask & PIDFD_COREDUMPED)) {
			fprintf(stderr, "socket_request_reject: PIDFD_COREDUMPED not set in coredump_mask\n");
			goto out;
		}

		if (!read_coredump_req(fd_coredump, &req)) {
			fprintf(stderr, "socket_request_reject: read_coredump_req failed\n");
			goto out;
		}

		if (!check_coredump_req(&req)) {
			fprintf(stderr, "socket_request_reject: check_coredump_req failed\n");
			goto out;
		}

		if (!send_coredump_ack(fd_coredump, &req,
				       COREDUMP_REJECT | COREDUMP_WAIT, 0)) {
			fprintf(stderr, "socket_request_reject: send_coredump_ack failed\n");
			goto out;
		}

		if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK)) {
			fprintf(stderr, "socket_request_reject: read_marker COREDUMP_MARK_REQACK failed\n");
			goto out;
		}

		for (;;) {
			char buffer[4096];
			ssize_t bytes_read;

			bytes_read = read(fd_coredump, buffer, sizeof(buffer));
			if (bytes_read > 0) {
				fprintf(stderr, "socket_request_reject: unexpected data received (expected no coredump data for REJECT)\n");
				goto out;
			}

			if (bytes_read < 0) {
				fprintf(stderr, "socket_request_reject: read from coredump socket failed: %m\n");
				goto out;
			}

			if (bytes_read == 0)
				break;
		}

		exit_code = EXIT_SUCCESS;
		fprintf(stderr, "socket_request_reject: completed successfully\n");
out:
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
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
		crashing_child();

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_FALSE(WCOREDUMP(status));

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

/* An ack the kernel must refuse and how. */
struct refused_ack {
	/* The ack, and how many bytes of it the server sends before it hangs up. */
	struct coredump_ack ack;
	size_t bytes;
	/* The marker the kernel answers with, or none if @no_marker. */
	enum coredump_mark mark;
	bool no_marker;
};

/* Send @refused, expect the kernel to refuse it and hang up. */
static void check_refused_ack(struct __test_metadata *const _metadata,
			      FIXTURE_DATA(coredump) *self,
			      const struct refused_ack *refused)
{
	int pidfd, status;
	pid_t pid, pid_coredump_server;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);
	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1;
		int exit_code = EXIT_FAILURE;
		struct coredump_req req = {};

		close(ipc_sockets[0]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0)
			goto out;

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			goto out;

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0)
			goto out;

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0)
			goto out;

		/* The task shows as dumping while it waits for the ack. */
		if (!get_pidfd_info(fd_peer_pidfd, &info))
			goto out;

		if (!(info.mask & PIDFD_INFO_COREDUMP) ||
		    !(info.coredump_mask & PIDFD_COREDUMPED)) {
			fprintf(stderr, "Peer isn't marked as dumping\n");
			goto out;
		}

		if (!read_coredump_req(fd_coredump, &req))
			goto out;

		if (!check_coredump_req(&req))
			goto out;

		if (!send_coredump_ack_bytes(fd_coredump, &refused->ack,
					     refused->bytes))
			goto out;

		/* Nothing more to say. A server that died looks the same. */
		if (shutdown(fd_coredump, SHUT_WR))
			goto out;

		if (!refused->no_marker &&
		    !read_marker(fd_coredump, refused->mark))
			goto out;

		/* The kernel hangs up after a refusal, marker or not. */
		if (!read_hangup(fd_coredump))
			goto out;

		exit_code = EXIT_SUCCESS;
out:
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
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
		crashing_child();

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_FALSE(WCOREDUMP(status));

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

/* Ack @ack_mask, expect the kernel to refuse it as conflicting. */
static void check_conflicting_ack(struct __test_metadata *const _metadata,
				  FIXTURE_DATA(coredump) *self, __u64 ack_mask)
{
	struct refused_ack refused = {
		.ack = {
			.size = sizeof(struct coredump_ack),
			.mask = ack_mask,
		},
		.bytes = sizeof(struct coredump_ack),
		.mark = COREDUMP_MARK_CONFLICTING,
	};

	check_refused_ack(_metadata, self, &refused);
}

/* More than one of KERNEL, USERSPACE and REJECT. */
TEST_F(coredump, socket_request_invalid_flag_combination)
{
	check_conflicting_ack(_metadata, self,
			      COREDUMP_KERNEL | COREDUMP_REJECT | COREDUMP_WAIT);
}

/* A flag the kernel didn't advertise in coredump_req->mask. */
TEST_F(coredump, socket_request_unknown_flag)
{
	struct refused_ack refused = {
		.ack = {
			.size = sizeof(struct coredump_ack),
			.mask = 1ULL << 63,
		},
		.bytes = sizeof(struct coredump_ack),
		.mark = COREDUMP_MARK_UNSUPPORTED,
	};

	check_refused_ack(_metadata, self, &refused);
}

/* An ack smaller than the first published struct. */
TEST_F(coredump, socket_request_invalid_size_small)
{
	struct refused_ack refused = {
		.ack = {
			.size = COREDUMP_ACK_SIZE_VER0 / 2,
			.mask = COREDUMP_REJECT | COREDUMP_WAIT,
		},
		.bytes = COREDUMP_ACK_SIZE_VER0 / 2,
		.mark = COREDUMP_MARK_MINSIZE,
	};

	check_refused_ack(_metadata, self, &refused);
}

/* An ack bigger than the kernel said it accepts. */
TEST_F(coredump, socket_request_invalid_size_large)
{
	struct refused_ack refused = {
		.ack = {
			.size = COREDUMP_ACK_SIZE_VER0 + PAGE_SIZE,
			.mask = COREDUMP_REJECT | COREDUMP_WAIT,
		},
		.bytes = COREDUMP_ACK_SIZE_VER0 + PAGE_SIZE,
		.mark = COREDUMP_MARK_MAXSIZE,
	};

	check_refused_ack(_metadata, self, &refused);
}

/*
 * Test: PIDFD_INFO_COREDUMP_SIGNAL via socket coredump with SIGSEGV
 *
 * Verify that when using socket-based coredump protocol,
 * the coredump_signal field is correctly exposed as SIGSEGV.
 * Also check that the coredump_code field is correctly exposed
 * as SEGV_MAPERR.
 */
TEST_F(coredump, socket_coredump_signal_sigsegv)
{
	int pidfd, ret, status;
	pid_t pid, pid_coredump_server;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;

	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		struct coredump_req req = {};
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1;
		int exit_code = EXIT_FAILURE;

		close(ipc_sockets[0]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: create_and_listen_unix_socket failed: %m\n");
			goto out;
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: write_nointr to ipc socket failed: %m\n");
			goto out;
		}

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: accept4 failed: %m\n");
			goto out;
		}

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: get_peer_pidfd failed\n");
			goto out;
		}

		if (!get_pidfd_info(fd_peer_pidfd, &info)) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: get_pidfd_info failed\n");
			goto out;
		}

		if (!(info.mask & PIDFD_INFO_COREDUMP)) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: PIDFD_INFO_COREDUMP not set in mask\n");
			goto out;
		}

		if (!(info.coredump_mask & PIDFD_COREDUMPED)) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: PIDFD_COREDUMPED not set in coredump_mask\n");
			goto out;
		}

		/* Verify coredump_signal is available and correct */
		if (!(info.mask & PIDFD_INFO_COREDUMP_SIGNAL)) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: PIDFD_INFO_COREDUMP_SIGNAL not set in mask\n");
			goto out;
		}

		if (info.coredump_signal != SIGSEGV) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: coredump_signal=%d, expected SIGSEGV=%d\n",
				info.coredump_signal, SIGSEGV);
			goto out;
		}

		/* Verify coredump_code is available and correct */
		if (!(info.mask & PIDFD_INFO_COREDUMP_CODE)) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: PIDFD_INFO_COREDUMP_CODE not set in mask\n");
			goto out;
		}

		if (info.coredump_code != SEGV_MAPERR) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: coredump_code=%d, expected SEGV_MAPERR=%d\n",
				info.coredump_code, SEGV_MAPERR);
			goto out;
		}

		if (!read_coredump_req(fd_coredump, &req)) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: read_coredump_req failed\n");
			goto out;
		}

		if (!send_coredump_ack(fd_coredump, &req,
				       COREDUMP_REJECT | COREDUMP_WAIT, 0)) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: send_coredump_ack failed\n");
			goto out;
		}

		if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK)) {
			fprintf(stderr, "socket_coredump_signal_sigsegv: read_marker COREDUMP_MARK_REQACK failed\n");
			goto out;
		}

		exit_code = EXIT_SUCCESS;
		fprintf(stderr, "socket_coredump_signal_sigsegv: completed successfully\n");
out:
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
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
		crashing_child();

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_EQ(WTERMSIG(status), SIGSEGV);

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_COREDUMP));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_COREDUMP_SIGNAL));
	ASSERT_EQ(info.coredump_signal, SIGSEGV);
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_COREDUMP_CODE));
	ASSERT_EQ(info.coredump_code, SEGV_MAPERR);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

/*
 * Test: PIDFD_INFO_COREDUMP_SIGNAL via socket coredump with SIGABRT
 *
 * Verify that when using socket-based coredump protocol,
 * the coredump_signal field is correctly exposed as SIGABRT.
 * Also check that the coredump_code field is correctly exposed
 * as SI_TKILL.
 */
TEST_F(coredump, socket_coredump_signal_sigabrt)
{
	int pidfd, ret, status;
	pid_t pid, pid_coredump_server;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;

	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		struct coredump_req req = {};
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1;
		int exit_code = EXIT_FAILURE;

		close(ipc_sockets[0]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: create_and_listen_unix_socket failed: %m\n");
			goto out;
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: write_nointr to ipc socket failed: %m\n");
			goto out;
		}

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: accept4 failed: %m\n");
			goto out;
		}

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: get_peer_pidfd failed\n");
			goto out;
		}

		if (!get_pidfd_info(fd_peer_pidfd, &info)) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: get_pidfd_info failed\n");
			goto out;
		}

		if (!(info.mask & PIDFD_INFO_COREDUMP)) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: PIDFD_INFO_COREDUMP not set in mask\n");
			goto out;
		}

		if (!(info.coredump_mask & PIDFD_COREDUMPED)) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: PIDFD_COREDUMPED not set in coredump_mask\n");
			goto out;
		}

		/* Verify coredump_signal is available and correct */
		if (!(info.mask & PIDFD_INFO_COREDUMP_SIGNAL)) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: PIDFD_INFO_COREDUMP_SIGNAL not set in mask\n");
			goto out;
		}

		if (info.coredump_signal != SIGABRT) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: coredump_signal=%d, expected SIGABRT=%d\n",
				info.coredump_signal, SIGABRT);
			goto out;
		}

		if (info.coredump_code != SI_TKILL) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: coredump_code=%d, expected SI_TKILL=%d\n",
				info.coredump_code, SI_TKILL);
			goto out;
		}

		if (!read_coredump_req(fd_coredump, &req)) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: read_coredump_req failed\n");
			goto out;
		}

		if (!send_coredump_ack(fd_coredump, &req,
				       COREDUMP_REJECT | COREDUMP_WAIT, 0)) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: send_coredump_ack failed\n");
			goto out;
		}

		if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK)) {
			fprintf(stderr, "socket_coredump_signal_sigabrt: read_marker COREDUMP_MARK_REQACK failed\n");
			goto out;
		}

		exit_code = EXIT_SUCCESS;
		fprintf(stderr, "socket_coredump_signal_sigabrt: completed successfully\n");
out:
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
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
		abort();

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_EQ(WTERMSIG(status), SIGABRT);

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_COREDUMP));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_COREDUMP_SIGNAL));
	ASSERT_EQ(info.coredump_signal, SIGABRT);
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_COREDUMP_CODE));
	ASSERT_EQ(info.coredump_code, SI_TKILL);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

TEST_F_TIMEOUT(coredump, socket_multiple_crashing_coredumps, 500)
{
	int pidfd[NUM_CRASHING_COREDUMPS], status[NUM_CRASHING_COREDUMPS];
	pid_t pid[NUM_CRASHING_COREDUMPS], pid_coredump_server;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;

	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1, fd_core_file = -1;
		int exit_code = EXIT_FAILURE;
		struct coredump_req req = {};

		close(ipc_sockets[0]);
		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0) {
			fprintf(stderr, "Failed to create and listen on unix socket\n");
			goto out;
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			fprintf(stderr, "Failed to notify parent via ipc socket\n");
			goto out;
		}
		close(ipc_sockets[1]);

		for (int i = 0; i < NUM_CRASHING_COREDUMPS; i++) {
			fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
			if (fd_coredump < 0) {
				fprintf(stderr, "accept4 failed: %m\n");
				goto out;
			}

			fd_peer_pidfd = get_peer_pidfd(fd_coredump);
			if (fd_peer_pidfd < 0) {
				fprintf(stderr, "get_peer_pidfd failed for fd %d: %m\n", fd_coredump);
				goto out;
			}

			if (!get_pidfd_info(fd_peer_pidfd, &info)) {
				fprintf(stderr, "get_pidfd_info failed for fd %d\n", fd_peer_pidfd);
				goto out;
			}

			if (!(info.mask & PIDFD_INFO_COREDUMP)) {
				fprintf(stderr, "pidfd info missing PIDFD_INFO_COREDUMP for fd %d\n", fd_peer_pidfd);
				goto out;
			}
			if (!(info.coredump_mask & PIDFD_COREDUMPED)) {
				fprintf(stderr, "pidfd info missing PIDFD_COREDUMPED for fd %d\n", fd_peer_pidfd);
				goto out;
			}

			if (!read_coredump_req(fd_coredump, &req)) {
				fprintf(stderr, "read_coredump_req failed for fd %d\n", fd_coredump);
				goto out;
			}

			if (!check_coredump_req(&req)) {
				fprintf(stderr, "check_coredump_req failed for fd %d\n", fd_coredump);
				goto out;
			}

			if (!send_coredump_ack(fd_coredump, &req,
					       COREDUMP_KERNEL | COREDUMP_WAIT, 0)) {
				fprintf(stderr, "send_coredump_ack failed for fd %d\n", fd_coredump);
				goto out;
			}

			if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK)) {
				fprintf(stderr, "read_marker failed for fd %d\n", fd_coredump);
				goto out;
			}

			fd_core_file = open_coredump_tmpfile(self->fd_tmpfs_detached);
			if (fd_core_file < 0) {
				fprintf(stderr, "%m - open_coredump_tmpfile failed for fd %d\n", fd_coredump);
				goto out;
			}

			for (;;) {
				char buffer[4096];
				ssize_t bytes_read, bytes_write;

				bytes_read = read(fd_coredump, buffer, sizeof(buffer));
				if (bytes_read < 0) {
					fprintf(stderr, "read failed for fd %d: %m\n", fd_coredump);
					goto out;
				}

				if (bytes_read == 0)
					break;

				bytes_write = write(fd_core_file, buffer, bytes_read);
				if (bytes_read != bytes_write) {
					if (bytes_write < 0 && errno == ENOSPC)
						continue;
					fprintf(stderr, "write failed for fd %d: %m\n", fd_core_file);
					goto out;
				}
			}

			close(fd_core_file);
			close(fd_peer_pidfd);
			close(fd_coredump);
			fd_peer_pidfd = -1;
			fd_coredump = -1;
		}

		exit_code = EXIT_SUCCESS;
out:
		if (fd_core_file >= 0)
			close(fd_core_file);
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
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

	for (int i = 0; i < NUM_CRASHING_COREDUMPS; i++) {
		pid[i] = fork();
		ASSERT_GE(pid[i], 0);
		if (pid[i] == 0)
			crashing_child();
		pidfd[i] = sys_pidfd_open(pid[i], 0);
		ASSERT_GE(pidfd[i], 0);
	}

	for (int i = 0; i < NUM_CRASHING_COREDUMPS; i++) {
		waitpid(pid[i], &status[i], 0);
		ASSERT_TRUE(WIFSIGNALED(status[i]));
		ASSERT_TRUE(WCOREDUMP(status[i]));
	}

	for (int i = 0; i < NUM_CRASHING_COREDUMPS; i++) {
		info.mask = PIDFD_INFO_EXIT | PIDFD_INFO_COREDUMP;
		ASSERT_EQ(ioctl(pidfd[i], PIDFD_GET_INFO, &info), 0);
		ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
		ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);
	}

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

TEST_F_TIMEOUT(coredump, socket_multiple_crashing_coredumps_epoll_workers, 500)
{
	int pidfd[NUM_CRASHING_COREDUMPS], status[NUM_CRASHING_COREDUMPS];
	pid_t pid[NUM_CRASHING_COREDUMPS], pid_coredump_server, worker_pids[NUM_CRASHING_COREDUMPS];
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;

	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, exit_code = EXIT_FAILURE, n_conns = 0;
		fd_server = -1;
		exit_code = EXIT_FAILURE;
		n_conns = 0;
		close(ipc_sockets[0]);
		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0) {
			fprintf(stderr, "socket_multiple_crashing_coredumps_epoll_workers: create_and_listen_unix_socket failed: %m\n");
			goto out;
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			fprintf(stderr, "socket_multiple_crashing_coredumps_epoll_workers: write_nointr to ipc socket failed: %m\n");
			goto out;
		}
		close(ipc_sockets[1]);

		while (n_conns < NUM_CRASHING_COREDUMPS) {
			int fd_coredump = -1, fd_peer_pidfd = -1, fd_core_file = -1;
			struct coredump_req req = {};
			fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
			if (fd_coredump < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					continue;
				fprintf(stderr, "socket_multiple_crashing_coredumps_epoll_workers: accept4 failed: %m\n");
				goto out;
			}
			fd_peer_pidfd = get_peer_pidfd(fd_coredump);
			if (fd_peer_pidfd < 0) {
				fprintf(stderr, "socket_multiple_crashing_coredumps_epoll_workers: get_peer_pidfd failed\n");
				goto out;
			}
			if (!get_pidfd_info(fd_peer_pidfd, &info)) {
				fprintf(stderr, "socket_multiple_crashing_coredumps_epoll_workers: get_pidfd_info failed\n");
				goto out;
			}
			if (!(info.mask & PIDFD_INFO_COREDUMP) || !(info.coredump_mask & PIDFD_COREDUMPED)) {
				fprintf(stderr, "socket_multiple_crashing_coredumps_epoll_workers: missing PIDFD_INFO_COREDUMP or PIDFD_COREDUMPED\n");
				goto out;
			}
			if (!read_coredump_req(fd_coredump, &req)) {
				fprintf(stderr, "socket_multiple_crashing_coredumps_epoll_workers: read_coredump_req failed\n");
				goto out;
			}
			if (!check_coredump_req(&req)) {
				fprintf(stderr, "socket_multiple_crashing_coredumps_epoll_workers: check_coredump_req failed\n");
				goto out;
			}
			if (!send_coredump_ack(fd_coredump, &req, COREDUMP_KERNEL | COREDUMP_WAIT, 0)) {
				fprintf(stderr, "socket_multiple_crashing_coredumps_epoll_workers: send_coredump_ack failed\n");
				goto out;
			}
			if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK)) {
				fprintf(stderr, "socket_multiple_crashing_coredumps_epoll_workers: read_marker failed\n");
				goto out;
			}
			fd_core_file = open_coredump_tmpfile(self->fd_tmpfs_detached);
			if (fd_core_file < 0) {
				fprintf(stderr, "socket_multiple_crashing_coredumps_epoll_workers: open_coredump_tmpfile failed: %m\n");
				goto out;
			}
			pid_t worker = fork();
			if (worker == 0) {
				close(fd_server);
				process_coredump_worker(fd_coredump, fd_peer_pidfd, fd_core_file);
			}
			worker_pids[n_conns] = worker;
			if (fd_coredump >= 0)
				close(fd_coredump);
			if (fd_peer_pidfd >= 0)
				close(fd_peer_pidfd);
			if (fd_core_file >= 0)
				close(fd_core_file);
			n_conns++;
		}
		exit_code = EXIT_SUCCESS;
out:
		if (fd_server >= 0)
			close(fd_server);

		// Reap all worker processes
		for (int i = 0; i < n_conns; i++) {
			int wstatus;
			if (waitpid(worker_pids[i], &wstatus, 0) < 0) {
				fprintf(stderr, "Failed to wait for worker %d: %m\n", worker_pids[i]);
			} else if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != EXIT_SUCCESS) {
				fprintf(stderr, "Worker %d exited with error code %d\n", worker_pids[i], WEXITSTATUS(wstatus));
				exit_code = EXIT_FAILURE;
			}
		}

		_exit(exit_code);
	}
	self->pid_coredump_server = pid_coredump_server;

	EXPECT_EQ(close(ipc_sockets[1]), 0);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	EXPECT_EQ(close(ipc_sockets[0]), 0);

	for (int i = 0; i < NUM_CRASHING_COREDUMPS; i++) {
		pid[i] = fork();
		ASSERT_GE(pid[i], 0);
		if (pid[i] == 0)
			crashing_child();
		pidfd[i] = sys_pidfd_open(pid[i], 0);
		ASSERT_GE(pidfd[i], 0);
	}

	for (int i = 0; i < NUM_CRASHING_COREDUMPS; i++) {
		ASSERT_GE(waitpid(pid[i], &status[i], 0), 0);
		ASSERT_TRUE(WIFSIGNALED(status[i]));
		ASSERT_TRUE(WCOREDUMP(status[i]));
	}

	for (int i = 0; i < NUM_CRASHING_COREDUMPS; i++) {
		info.mask = PIDFD_INFO_EXIT | PIDFD_INFO_COREDUMP;
		ASSERT_EQ(ioctl(pidfd[i], PIDFD_GET_INFO, &info), 0);
		ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
		ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);
	}

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

/*
 * Reassemble a record stream and check that what comes out is an ELF
 * core file. The records themselves are validated by recv_coredump_records().
 */
TEST_F(coredump, socket_request_sparse_reassemble)
{
	int fd_core_file, pidfd, status;
	pid_t pid, pid_coredump_server;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);
	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1;
		int fd_file = -1;
		int exit_code = EXIT_FAILURE;
		struct coredump_req req = {};

		close(ipc_sockets[0]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0)
			goto out;

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			goto out;

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0)
			goto out;

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0)
			goto out;

		fd_file = creat("/tmp/coredump.file", 0644);
		if (fd_file < 0)
			goto out;

		if (!read_coredump_req(fd_coredump, &req))
			goto out;

		if (!check_coredump_req(&req))
			goto out;

		if (!send_coredump_ack(fd_coredump, &req,
				       COREDUMP_KERNEL | COREDUMP_RECORDS |
				       COREDUMP_SPARSE | COREDUMP_WAIT, 0))
			goto out;

		if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK))
			goto out;

		if (recv_coredump_records(fd_coredump, fd_file, NULL, NULL, -1) < 0)
			goto out;

		exit_code = EXIT_SUCCESS;
out:
		if (fd_file >= 0)
			close(fd_file);
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
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
		crashing_child();

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_TRUE(WCOREDUMP(status));

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);

	/* What the records reassemble into has to be an ELF core file. */
	fd_core_file = open("/tmp/coredump.file", O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd_core_file, 0);
	ASSERT_TRUE(is_elf_core(fd_core_file));
	EXPECT_EQ(close(fd_core_file), 0);
}

/*
 * Crash a child with a mostly-unpopulated mapping and reassemble its
 * record stream, reporting what crossed the socket and the coredump
 * size the records describe. With @kill_peer the server kills the task
 * once the coredump is under way so the kernel has to cut it short.
 */
static void check_record_dump(struct __test_metadata *const _metadata,
			      FIXTURE_DATA(coredump) *self, __u64 ack_mask,
			      bool kill_peer, ssize_t *received,
			      off_t *coredump_size)
{
	bool truncated = false;
	int pidfd, status;
	pid_t pid, pid_coredump_server;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	int pipefds[2];
	char c;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);
	ASSERT_EQ(pipe(pipefds), 0);
	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1;
		int fd_file = -1;
		int exit_code = EXIT_FAILURE;
		struct coredump_req req = {};
		bool is_truncated = false;
		off_t size = 0;
		ssize_t ret;

		close(ipc_sockets[0]);
		close(pipefds[0]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0)
			goto out;

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			goto out;

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0)
			goto out;

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0)
			goto out;

		/*
		 * The reassembled coredump is bigger than the mapping the
		 * child made, so keep it on the detached tmpfs and sparse.
		 */
		fd_file = open_coredump_tmpfile(self->fd_tmpfs_detached);
		if (fd_file < 0)
			goto out;

		if (!read_coredump_req(fd_coredump, &req))
			goto out;

		if (!check_coredump_req(&req))
			goto out;

		if (!send_coredump_ack(fd_coredump, &req, ack_mask, 0))
			goto out;

		if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK))
			goto out;

		ret = recv_coredump_records(fd_coredump, fd_file, &size, &is_truncated,
					    kill_peer ? fd_peer_pidfd : -1);
		if (ret < 0)
			goto out;

		if (write_nointr(pipefds[1], &ret, sizeof(ret)) != sizeof(ret))
			goto out;
		if (write_nointr(pipefds[1], &size, sizeof(size)) != sizeof(size))
			goto out;
		if (write_nointr(pipefds[1], &is_truncated,
				 sizeof(is_truncated)) != sizeof(is_truncated))
			goto out;

		exit_code = EXIT_SUCCESS;
out:
		close(pipefds[1]);
		if (fd_file >= 0)
			close(fd_file);
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
		if (fd_coredump >= 0)
			close(fd_coredump);
		if (fd_server >= 0)
			close(fd_server);
		_exit(exit_code);
	}
	self->pid_coredump_server = pid_coredump_server;

	EXPECT_EQ(close(ipc_sockets[1]), 0);
	EXPECT_EQ(close(pipefds[1]), 0);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	EXPECT_EQ(close(ipc_sockets[0]), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		crashing_child_sparse(SPARSE_MAPPING_SIZE);

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));

	ASSERT_EQ(read_nointr(pipefds[0], received, sizeof(*received)),
		  sizeof(*received));
	ASSERT_EQ(read_nointr(pipefds[0], coredump_size, sizeof(*coredump_size)),
		  sizeof(*coredump_size));
	ASSERT_EQ(read_nointr(pipefds[0], &truncated, sizeof(truncated)),
		  sizeof(truncated));
	EXPECT_EQ(close(pipefds[0]), 0);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);

	if (kill_peer) {
		/* The kernel gave up partway, so no end record closed the stream. */
		ASSERT_TRUE(truncated);
		ASSERT_FALSE(WCOREDUMP(status));
		ASSERT_LT(*coredump_size, (off_t)SPARSE_MAPPING_SIZE);
		return;
	}

	ASSERT_FALSE(truncated);
	ASSERT_TRUE(WCOREDUMP(status));

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);

	/* The mapping is in the coredump, holes included. */
	ASSERT_GT(*coredump_size, (off_t)SPARSE_MAPPING_SIZE);
}

/*
 * A mapping that has been written to is dumped whole, including the parts
 * of it that were never faulted in. With COREDUMP_SPARSE the holes stay
 * off the wire.
 */
TEST_F(coredump, socket_request_sparse_hole)
{
	off_t coredump_size = 0;
	ssize_t received = 0;

	check_record_dump(_metadata, self,
			  COREDUMP_KERNEL | COREDUMP_RECORDS |
			  COREDUMP_SPARSE | COREDUMP_WAIT,
			  false, &received, &coredump_size);

	/* The holes didn't have to go over the socket. */
	ASSERT_LT(received, coredump_size / 8);
}

/*
 * COREDUMP_RECORDS alone splits the stream into records but elides
 * nothing: the holes cross the socket as data records.
 */
TEST_F(coredump, socket_request_records_hole)
{
	off_t coredump_size = 0;
	ssize_t received = 0;

	check_record_dump(_metadata, self,
			  COREDUMP_KERNEL | COREDUMP_RECORDS | COREDUMP_WAIT,
			  false, &received, &coredump_size);

	/* Records alone elide nothing, so everything crossed the socket. */
	ASSERT_GT(received, coredump_size);
}

/*
 * A coredump the kernel gives up on halfway still ends in an end record,
 * and that record says the coredump is incomplete. COREDUMP_SPARSE is left
 * out on purpose: the holes have to cross the socket so the coredump is
 * far larger than the socket buffer and the kernel is still writing it
 * when the kill lands.
 */
TEST_F(coredump, socket_request_records_truncated)
{
	off_t coredump_size = 0;
	ssize_t received = 0;

	check_record_dump(_metadata, self,
			  COREDUMP_KERNEL | COREDUMP_RECORDS | COREDUMP_WAIT,
			  true, &received, &coredump_size);

	/* The end record crossed the socket even though the task was killed. */
	ASSERT_GT(received, 0);
}

/*
 * A coredump server that uploads to a blob store can't upload a sparse
 * file. It doesn't have to: it streams the data records into the object
 * as they arrive, leaves the holes out, and uploads the corrected
 * program header table last. What it ends up with is an ordinary ELF
 * core file that describes the same memory as the coredump the records
 * came from, minus the holes.
 */
TEST_F(coredump, socket_request_sparse_blob_upload)
{
	int fd_core_file, pidfd, status;
	pid_t pid, pid_coredump_server;
	struct pidfd_info info = {};
	off_t coredump_size = 0;
	ssize_t received = 0;
	int ipc_sockets[2];
	int pipefds[2];
	struct stat st;
	char c;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);
	ASSERT_EQ(pipe(pipefds), 0);
	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1;
		int fd_object = -1, fd_reference = -1;
		int exit_code = EXIT_FAILURE;
		struct coredump_req req = {};
		off_t size = 0;
		ssize_t ret;

		close(ipc_sockets[0]);
		close(pipefds[0]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0)
			goto out;

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			goto out;

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0)
			goto out;

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0)
			goto out;

		/* The object is a plain file. It never sees a hole. */
		fd_object = open("/tmp/coredump.file",
				 O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
		if (fd_object < 0)
			goto out;

		/*
		 * The coredump with its holes still in it is bigger than
		 * the mapping the child made, so keep it on the detached
		 * tmpfs and sparse.
		 */
		fd_reference = open_coredump_tmpfile(self->fd_tmpfs_detached);
		if (fd_reference < 0)
			goto out;

		if (!read_coredump_req(fd_coredump, &req))
			goto out;

		if (!check_coredump_req(&req))
			goto out;

		if (!send_coredump_ack(fd_coredump, &req,
				       COREDUMP_KERNEL | COREDUMP_RECORDS |
				       COREDUMP_SPARSE | COREDUMP_WAIT, 0))
			goto out;

		if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK))
			goto out;

		ret = recv_coredump_compact(fd_coredump, fd_object,
					    fd_reference, &size);
		if (ret < 0)
			goto out;

		if (check_compact_coredump(fd_object, fd_reference))
			goto out;

		if (write_nointr(pipefds[1], &ret, sizeof(ret)) != sizeof(ret))
			goto out;
		if (write_nointr(pipefds[1], &size, sizeof(size)) != sizeof(size))
			goto out;

		exit_code = EXIT_SUCCESS;
out:
		close(pipefds[1]);
		if (fd_reference >= 0)
			close(fd_reference);
		if (fd_object >= 0)
			close(fd_object);
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
		if (fd_coredump >= 0)
			close(fd_coredump);
		if (fd_server >= 0)
			close(fd_server);
		_exit(exit_code);
	}
	self->pid_coredump_server = pid_coredump_server;

	EXPECT_EQ(close(ipc_sockets[1]), 0);
	EXPECT_EQ(close(pipefds[1]), 0);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	EXPECT_EQ(close(ipc_sockets[0]), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		crashing_child_sparse(SPARSE_MAPPING_SIZE);

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_TRUE(WCOREDUMP(status));

	ASSERT_EQ(read_nointr(pipefds[0], &received, sizeof(received)),
		  sizeof(received));
	ASSERT_EQ(read_nointr(pipefds[0], &coredump_size, sizeof(coredump_size)),
		  sizeof(coredump_size));
	EXPECT_EQ(close(pipefds[0]), 0);

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);

	/* The mapping is in the coredump, holes included. */
	ASSERT_GT(coredump_size, (off_t)SPARSE_MAPPING_SIZE);

	/* The object isn't sparse and doesn't carry them. */
	ASSERT_EQ(stat("/tmp/coredump.file", &st), 0);
	ASSERT_LT(st.st_size, coredump_size / 8);

	/* And a debugger still sees an ordinary ELF core file. */
	fd_core_file = open("/tmp/coredump.file", O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd_core_file, 0);
	ASSERT_TRUE(is_elf_core(fd_core_file));
	EXPECT_EQ(close(fd_core_file), 0);
}

/* COREDUMP_RECORDS applies to a coredump the kernel writes, nothing else. */
TEST_F(coredump, socket_request_records_without_kernel)
{
	check_conflicting_ack(_metadata, self, COREDUMP_USERSPACE | COREDUMP_RECORDS);
}

/* A zero record can't exist outside a record stream. */
TEST_F(coredump, socket_request_sparse_without_records)
{
	check_conflicting_ack(_metadata, self, COREDUMP_KERNEL | COREDUMP_SPARSE);
}

/* What the server reports back about the coredump it decided to take. */
struct stream_choice {
	bool sparse;
	ssize_t received;
	off_t size;
	ssize_t vm_size;
};

/*
 * The kernel blocks in the coredump request until the ack arrives, so a
 * coredump server gets to look at the task before it commits to a
 * stream. Take the record stream only for a task whose mappings are
 * worth it and the plain byte stream for everything else.
 */
static void check_stream_choice(struct __test_metadata *const _metadata,
				FIXTURE_DATA(coredump) *self, bool big,
				struct stream_choice *choice)
{
	int pidfd, status;
	pid_t pid, pid_coredump_server;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	int pipefds[2];
	char c;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);
	ASSERT_EQ(pipe(pipefds), 0);
	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1;
		int fd_file = -1;
		int exit_code = EXIT_FAILURE;
		struct coredump_req req = {};
		struct stream_choice got = {};
		__u64 mask;

		close(ipc_sockets[0]);
		close(pipefds[0]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0)
			goto out;

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			goto out;

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0)
			goto out;

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0)
			goto out;

		/*
		 * The reassembled coredump is bigger than the mapping the
		 * child made, so keep it on the detached tmpfs and sparse.
		 */
		fd_file = open_coredump_tmpfile(self->fd_tmpfs_detached);
		if (fd_file < 0)
			goto out;

		if (!read_coredump_req(fd_coredump, &req))
			goto out;

		if (!check_coredump_req(&req))
			goto out;

		/*
		 * Nothing is on the wire yet and the kernel is waiting for
		 * the ack, so there is all the time in the world to look at
		 * the task and decide what to ask it for.
		 */
		got.vm_size = peer_vm_size(fd_peer_pidfd);
		if (got.vm_size < 0)
			goto out;
		got.sparse = got.vm_size >= SPARSE_STREAM_THRESHOLD;

		fprintf(stderr, "Peer maps %zd bytes, asking for %s\n",
			got.vm_size,
			got.sparse ? "a sparse record stream" : "a byte stream");

		mask = COREDUMP_KERNEL | COREDUMP_WAIT;
		if (got.sparse)
			mask |= COREDUMP_RECORDS | COREDUMP_SPARSE;

		if (!send_coredump_ack(fd_coredump, &req, mask, 0))
			goto out;

		if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK))
			goto out;

		if (got.sparse) {
			got.received = recv_coredump_records(fd_coredump, fd_file,
							     &got.size, NULL, -1);
		} else {
			got.received = recv_coredump_bytes(fd_coredump, fd_file);
			got.size = got.received;
		}
		if (got.received < 0)
			goto out;

		/* Either way a debugger has to see an ordinary core file. */
		if (!is_elf_core(fd_file))
			goto out;

		if (write_nointr(pipefds[1], &got, sizeof(got)) != sizeof(got))
			goto out;

		exit_code = EXIT_SUCCESS;
out:
		close(pipefds[1]);
		if (fd_file >= 0)
			close(fd_file);
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
		if (fd_coredump >= 0)
			close(fd_coredump);
		if (fd_server >= 0)
			close(fd_server);
		_exit(exit_code);
	}
	self->pid_coredump_server = pid_coredump_server;

	EXPECT_EQ(close(ipc_sockets[1]), 0);
	EXPECT_EQ(close(pipefds[1]), 0);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	EXPECT_EQ(close(ipc_sockets[0]), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		crashing_child_sparse(big ? SPARSE_MAPPING_SIZE : PAGE_SIZE);

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_TRUE(WCOREDUMP(status));

	ASSERT_EQ(read_nointr(pipefds[0], choice, sizeof(*choice)),
		  sizeof(*choice));
	EXPECT_EQ(close(pipefds[0]), 0);

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

/* A task with little mapped isn't worth a record stream. */
TEST_F(coredump, socket_request_stream_choice_small)
{
	struct stream_choice choice = {};

	check_stream_choice(_metadata, self, false, &choice);

	ASSERT_LT(choice.vm_size, (ssize_t)SPARSE_STREAM_THRESHOLD);
	ASSERT_FALSE(choice.sparse);
	ASSERT_GT(choice.received, 0);
}

/* A task sitting on a big mapping is. */
TEST_F(coredump, socket_request_stream_choice_large)
{
	struct stream_choice choice = {};

	check_stream_choice(_metadata, self, true, &choice);

	ASSERT_GE(choice.vm_size, (ssize_t)SPARSE_STREAM_THRESHOLD);
	ASSERT_TRUE(choice.sparse);
	ASSERT_GT(choice.size, (off_t)SPARSE_MAPPING_SIZE);

	/* The holes didn't have to go over the socket. */
	ASSERT_LT(choice.received, choice.size / 8);
}

/* What a coredump server was built with. */
struct server_build {
	/* sizeof(struct coredump_req) and sizeof(struct coredump_ack) back then. */
	size_t req_size;
	size_t ack_size;
	/* The features it raises if the kernel offers them. */
	__u64 wants;
	/* Its policy: what it drops from and adds to the task's selection. */
	__u64 drop;
	__u64 add;
};

/* A server from when the structs were first published: kernel-written dumps. */
static const struct server_build server_build_ver0 = {
	.req_size	= COREDUMP_REQ_SIZE_VER0,
	.ack_size	= COREDUMP_ACK_SIZE_VER0,
	.wants		= COREDUMP_KERNEL,
};

/* A server built against this header: no shared memory, always the ELF headers. */
static const struct server_build server_build_ver1 = {
	.req_size	= sizeof(struct coredump_req),
	.ack_size	= sizeof(struct coredump_ack),
	.wants		= COREDUMP_KERNEL | COREDUMP_RECORDS | COREDUMP_SPARSE |
			  COREDUMP_MEMORY_TYPES,
	.drop		= COREDUMP_MEMORY_ANON_SHARED | COREDUMP_MEMORY_FILE_SHARED,
	.add		= COREDUMP_MEMORY_ELF_HEADERS,
};

/*
 * Build the ack the way a server does: from what the kernel offers, what
 * this build implements, and what fits in the ack the kernel accepts.
 * Fields the build never read are zero and never consulted.
 */
static void negotiate(const struct coredump_req *req,
		      const struct server_build *build,
		      struct coredump_ack *ack)
{
	__u64 offered = req->mask & build->wants;

	memset(ack, 0, sizeof(*ack));
	ack->size = build->ack_size < req->size_ack ? build->ack_size : req->size_ack;
	/* These builds only ever have the kernel write the coredump. */
	ack->mask = COREDUMP_KERNEL;

	/* Sparse needs records, records need the kernel to write. */
	if (offered & COREDUMP_RECORDS) {
		ack->mask |= COREDUMP_RECORDS;
		if (offered & COREDUMP_SPARSE)
			ack->mask |= COREDUMP_SPARSE;
	}

	/* The memory types need an ack that carries them. */
	if ((offered & COREDUMP_MEMORY_TYPES) && ack->size >= COREDUMP_ACK_SIZE_VER1) {
		ack->mask |= COREDUMP_MEMORY_TYPES;
		/* Start from the task's selection; only advertised types pass. */
		ack->memory_types = (req->memory_types & ~build->drop) | build->add;
		ack->memory_types &= req->memory_types_mask;
	}
}

/* What a memory types test asks of the kernel and what it expects back. */
struct memory_choice {
	/* Memory types the crashing child selects, or FILTER_TASK_INHERIT. */
	__u64 task_filter;
	/* Negotiate the ack as this server build, NULL to send it as given. */
	const struct server_build *build;
	/* The ack, or what the negotiation must arrive at. */
	__u64 mask;
	__u64 memory_types;
	size_t size_ack;
	/* The shared mapping is in the coredump with all of its memory. */
	bool shared_dumped;
	/* No memory at all. Pull a page from /proc/<pid>/mem instead. */
	bool skeleton;
};

/* A skeleton still carries the vdso and friends, nothing bigger. */
#define SKELETON_DATA_PAGES 16

/*
 * The crashing child maps shared anonymous memory and tells the server
 * where. The server acks with @choice and checks whether that mapping's
 * segment in the coredump carries its memory.
 */
static void check_memory_dump(struct __test_metadata *const _metadata,
			      FIXTURE_DATA(coredump) *self,
			      const struct memory_choice *choice)
{
	int pidfd, status;
	pid_t pid, pid_coredump_server;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	int addr_pipe[2];
	char c;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);
	ASSERT_EQ(pipe(addr_pipe), 0);
	ASSERT_TRUE(set_core_pattern("@@/tmp/coredump.socket"));

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1;
		int fd_file = -1;
		int exit_code = EXIT_FAILURE;
		struct coredump_req req = {};
		struct coredump_ack ack = {
			.size = choice->size_ack,
			.mask = choice->mask,
			.memory_types = choice->memory_types,
		};
		/* How much of the request this server reads. */
		size_t req_size = choice->build ? choice->build->req_size : sizeof(req);
		__u64 task_filter;
		ElfW(Phdr) segment;
		ssize_t received;
		off_t size;
		char *addr;

		close(ipc_sockets[0]);
		close(addr_pipe[1]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0)
			goto out;

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			goto out;

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0)
			goto out;

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0)
			goto out;

		fd_file = open_coredump_tmpfile(self->fd_tmpfs_detached);
		if (fd_file < 0)
			goto out;

		if (!read_coredump_req_sized(fd_coredump, &req, req_size))
			goto out;

		if (!peer_coredump_filter(fd_peer_pidfd, &task_filter))
			goto out;

		/* A build from before the memory types never read that far. */
		if (req_size >= COREDUMP_REQ_SIZE_VER1) {
			if (!check_coredump_req(&req))
				goto out;

			/* The request reports the memory types the task selected. */
			if (req.memory_types != task_filter) {
				fprintf(stderr, "Request reports 0x%llx, task selected 0x%llx\n",
					(unsigned long long)req.memory_types,
					(unsigned long long)task_filter);
				goto out;
			}
		}

		if (choice->task_filter != FILTER_TASK_INHERIT &&
		    task_filter != choice->task_filter) {
			fprintf(stderr, "Task selected 0x%llx, child asked for 0x%llx\n",
				(unsigned long long)task_filter,
				(unsigned long long)choice->task_filter);
			goto out;
		}

		/* The child sent the address of its mapping before it crashed. */
		if (read_nointr(addr_pipe[0], &addr, sizeof(addr)) != sizeof(addr))
			goto out;

		/* A server build negotiates its ack and must arrive at the choice. */
		if (choice->build) {
			negotiate(&req, choice->build, &ack);

			if (ack.size != choice->size_ack || ack.mask != choice->mask ||
			    ack.memory_types != choice->memory_types) {
				fprintf(stderr,
					"Negotiated %u bytes, mask 0x%llx, types 0x%llx\n",
					ack.size, (unsigned long long)ack.mask,
					(unsigned long long)ack.memory_types);
				goto out;
			}
		}

		if (!send_coredump_ack_types(fd_coredump, &req, ack.mask,
					      ack.memory_types, ack.size))
			goto out;

		if (!read_marker(fd_coredump, COREDUMP_MARK_REQACK))
			goto out;

		if (ack.mask & COREDUMP_RECORDS)
			received = recv_coredump_records(fd_coredump, fd_file,
							 &size, NULL, -1);
		else
			received = recv_coredump_bytes(fd_coredump, fd_file);
		if (received < 0)
			goto out;

		if (!is_elf_core(fd_file))
			goto out;

		/* A dump ending in holes or empty segments must still be whole. */
		if (!check_coredump_extent(fd_file))
			goto out;

		if (!find_coredump_segment(fd_file, (__u64)(uintptr_t)addr, &segment))
			goto out;

		if (segment.p_memsz != MEMORY_MAPPING_SIZE) {
			fprintf(stderr, "Segment spans %llu bytes, the mapping %u\n",
				(unsigned long long)segment.p_memsz,
				MEMORY_MAPPING_SIZE);
			goto out;
		}

		if (segment.p_filesz != (choice->shared_dumped ? segment.p_memsz : 0)) {
			fprintf(stderr, "Segment carries %llu bytes, expected %s of them\n",
				(unsigned long long)segment.p_filesz,
				choice->shared_dumped ? "all" : "none");
			goto out;
		}

		if (choice->skeleton) {
			__u64 data, notes, data_max;
			char buf[PAGE_SIZE];

			if (!sum_coredump_segments(fd_file, &data, &notes))
				goto out;

			data_max = SKELETON_DATA_PAGES * sysconf(_SC_PAGESIZE);
			if (!notes || data > data_max) {
				fprintf(stderr, "Skeleton has %llu note and %llu memory bytes\n",
					(unsigned long long)notes,
					(unsigned long long)data);
				goto out;
			}

			/* The task is parked in COREDUMP_WAIT with its memory. */
			if (peer_read_mem(fd_peer_pidfd, (__u64)(uintptr_t)addr,
					  buf, sizeof(buf)) != sizeof(buf))
				goto out;

			if (buf[0] != 'x') {
				fprintf(stderr, "Pulled memory lacks the child's mark\n");
				goto out;
			}

			fprintf(stderr, "Skeleton of %zd bytes, pulled %zu bytes of memory\n",
				received, sizeof(buf));
		}

		exit_code = EXIT_SUCCESS;
out:
		close(addr_pipe[0]);
		if (fd_file >= 0)
			close(fd_file);
		if (fd_peer_pidfd >= 0)
			close(fd_peer_pidfd);
		if (fd_coredump >= 0)
			close(fd_coredump);
		if (fd_server >= 0)
			close(fd_server);
		_exit(exit_code);
	}
	self->pid_coredump_server = pid_coredump_server;

	EXPECT_EQ(close(ipc_sockets[1]), 0);
	EXPECT_EQ(close(addr_pipe[0]), 0);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	EXPECT_EQ(close(ipc_sockets[0]), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		crashing_child_memory(choice->task_filter, addr_pipe[1]);
	EXPECT_EQ(close(addr_pipe[1]), 0);

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_TRUE(WCOREDUMP(status));

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

/* Without COREDUMP_MEMORY_TYPES the task's own selection decides. */
TEST_F(coredump, socket_request_memory_types_task_includes)
{
	struct memory_choice choice = {
		.task_filter = COREDUMP_MEMORY_ANON_PRIVATE |
			       COREDUMP_MEMORY_ANON_SHARED,
		.mask = COREDUMP_KERNEL,
		.shared_dumped = true,
	};

	check_memory_dump(_metadata, self, &choice);
}

TEST_F(coredump, socket_request_memory_types_task_excludes)
{
	struct memory_choice choice = {
		.task_filter = 0,
		.mask = COREDUMP_KERNEL,
		.shared_dumped = false,
	};

	check_memory_dump(_metadata, self, &choice);
}

/* The server drops a memory type the task would have dumped. */
TEST_F(coredump, socket_request_memory_types_restricts)
{
	struct memory_choice choice = {
		.task_filter = COREDUMP_MEMORY_ANON_PRIVATE |
			       COREDUMP_MEMORY_ANON_SHARED,
		.mask = COREDUMP_KERNEL | COREDUMP_MEMORY_TYPES,
		.memory_types = COREDUMP_MEMORY_ANON_PRIVATE,
		.shared_dumped = false,
	};

	check_memory_dump(_metadata, self, &choice);
}

/* The server adds a memory type the task had excluded. */
TEST_F(coredump, socket_request_memory_types_widens)
{
	struct memory_choice choice = {
		.task_filter = 0,
		.mask = COREDUMP_KERNEL | COREDUMP_MEMORY_TYPES,
		.memory_types = COREDUMP_MEMORY_ANON_PRIVATE |
			  COREDUMP_MEMORY_ANON_SHARED,
		.shared_dumped = true,
	};

	check_memory_dump(_metadata, self, &choice);
}

/* The memory types decide what goes into a record stream just the same. */
TEST_F(coredump, socket_request_memory_types_records)
{
	struct memory_choice choice = {
		.task_filter = FILTER_TASK_INHERIT,
		.mask = COREDUMP_KERNEL | COREDUMP_RECORDS | COREDUMP_SPARSE |
			COREDUMP_MEMORY_TYPES,
		.memory_types = COREDUMP_MEMORY_ANON_PRIVATE,
		.shared_dumped = false,
	};

	check_memory_dump(_metadata, self, &choice);
}

/*
 * An empty selection leaves a skeleton: every program header and every note
 * but no memory. A server that wants to pick the memory itself reads it
 * from /proc/<pid>/mem while the task waits for it to finish.
 */
TEST_F(coredump, socket_request_memory_types_skeleton)
{
	struct memory_choice choice = {
		.task_filter = FILTER_TASK_INHERIT,
		.mask = COREDUMP_KERNEL | COREDUMP_WAIT | COREDUMP_MEMORY_TYPES,
		.memory_types = 0,
		.shared_dumped = false,
		.skeleton = true,
	};

	check_memory_dump(_metadata, self, &choice);
}

/* A memory type the kernel didn't advertise in memory_types_mask. */
TEST_F(coredump, socket_request_memory_types_unknown_bit)
{
	struct refused_ack refused = {
		.ack = {
			.size = sizeof(struct coredump_ack),
			.mask = COREDUMP_KERNEL | COREDUMP_MEMORY_TYPES,
			.memory_types = 1ULL << 63,
		},
		.bytes = sizeof(struct coredump_ack),
		.mark = COREDUMP_MARK_UNSUPPORTED,
	};

	check_refused_ack(_metadata, self, &refused);
}

/* The memory types must be zero unless COREDUMP_MEMORY_TYPES is raised. */
TEST_F(coredump, socket_request_memory_types_stale_field)
{
	struct refused_ack refused = {
		.ack = {
			.size = sizeof(struct coredump_ack),
			.mask = COREDUMP_KERNEL,
			.memory_types = COREDUMP_MEMORY_ANON_PRIVATE,
		},
		.bytes = sizeof(struct coredump_ack),
		.mark = COREDUMP_MARK_UNSUPPORTED,
	};

	check_refused_ack(_metadata, self, &refused);
}

/* COREDUMP_MEMORY_TYPES needs an ack that has the memory types. */
TEST_F(coredump, socket_request_memory_types_short_ack)
{
	struct refused_ack refused = {
		.ack = {
			.size = COREDUMP_ACK_SIZE_VER0,
			.mask = COREDUMP_KERNEL | COREDUMP_MEMORY_TYPES,
		},
		.bytes = COREDUMP_ACK_SIZE_VER0,
		.mark = COREDUMP_MARK_MINSIZE,
	};

	check_refused_ack(_metadata, self, &refused);
}

/* The memory types select what the kernel writes, nothing else. */
TEST_F(coredump, socket_request_memory_types_without_kernel)
{
	check_conflicting_ack(_metadata, self, COREDUMP_USERSPACE | COREDUMP_MEMORY_TYPES);
}

/*
 * A server built with the first structs reads the request it knows,
 * discards the rest and acks with the ack it knows. It raises nothing
 * it wasn't built for and the kernel dumps what the task selected.
 */
TEST_F(coredump, socket_request_negotiate_ver0)
{
	struct memory_choice choice = {
		.task_filter = COREDUMP_MEMORY_ANON_PRIVATE |
			       COREDUMP_MEMORY_ANON_SHARED,
		.build = &server_build_ver0,
		.mask = COREDUMP_KERNEL,
		.memory_types = 0,
		.size_ack = COREDUMP_ACK_SIZE_VER0,
		.shared_dumped = true,
	};

	check_memory_dump(_metadata, self, &choice);
}

/*
 * A server built against this header takes every feature the kernel
 * offers, drops shared memory from what the task selected and adds the
 * ELF headers.
 */
TEST_F(coredump, socket_request_negotiate_ver1)
{
	struct memory_choice choice = {
		.task_filter = COREDUMP_MEMORY_ANON_PRIVATE |
			       COREDUMP_MEMORY_ANON_SHARED,
		.build = &server_build_ver1,
		.mask = COREDUMP_KERNEL | COREDUMP_RECORDS | COREDUMP_SPARSE |
			COREDUMP_MEMORY_TYPES,
		.memory_types = COREDUMP_MEMORY_ANON_PRIVATE |
				 COREDUMP_MEMORY_ELF_HEADERS,
		.size_ack = COREDUMP_ACK_SIZE_VER1,
		.shared_dumped = false,
	};

	check_memory_dump(_metadata, self, &choice);
}

/* An ack that picks none of KERNEL, USERSPACE and REJECT. */
TEST_F(coredump, socket_request_no_mode)
{
	struct refused_ack refused = {
		.ack = {
			.size = sizeof(struct coredump_ack),
			.mask = COREDUMP_WAIT,
		},
		.bytes = sizeof(struct coredump_ack),
		.mark = COREDUMP_MARK_CONFLICTING,
	};

	check_refused_ack(_metadata, self, &refused);
}

/* @spare must be zero, like every field that isn't in use. */
TEST_F(coredump, socket_request_spare)
{
	struct refused_ack refused = {
		.ack = {
			.size = sizeof(struct coredump_ack),
			.spare = 1,
			.mask = COREDUMP_KERNEL,
		},
		.bytes = sizeof(struct coredump_ack),
		.mark = COREDUMP_MARK_UNSUPPORTED,
	};

	check_refused_ack(_metadata, self, &refused);
}

/* An ack size is a byte count. One that ends inside a field is valid. */
#define ACK_SIZE_BETWEEN (COREDUMP_ACK_SIZE_VER0 + sizeof(__u32))

/* Any size from VER0 up to what the kernel accepts works without memory types. */
TEST_F(coredump, socket_request_ack_size_between)
{
	struct memory_choice choice = {
		.task_filter = COREDUMP_MEMORY_ANON_PRIVATE |
			       COREDUMP_MEMORY_ANON_SHARED,
		.mask = COREDUMP_KERNEL,
		.size_ack = ACK_SIZE_BETWEEN,
		.shared_dumped = true,
	};

	check_memory_dump(_metadata, self, &choice);
}

/* The memory types need the whole field, not the part that happens to fit. */
TEST_F(coredump, socket_request_memory_types_ack_size_between)
{
	struct refused_ack refused = {
		.ack = {
			.size = ACK_SIZE_BETWEEN,
			.mask = COREDUMP_KERNEL | COREDUMP_MEMORY_TYPES,
		},
		.bytes = ACK_SIZE_BETWEEN,
		.mark = COREDUMP_MARK_MINSIZE,
	};

	check_refused_ack(_metadata, self, &refused);
}

/* A server that hangs up without acking gets no marker and no coredump. */
TEST_F(coredump, socket_request_server_hangs_up)
{
	struct refused_ack refused = {
		.bytes = 0,
		.no_marker = true,
	};

	check_refused_ack(_metadata, self, &refused);
}

/* A server that hangs up in the middle of its ack looks the same. */
TEST_F(coredump, socket_request_ack_truncated)
{
	struct refused_ack refused = {
		.ack = {
			.size = COREDUMP_ACK_SIZE_VER0,
			.mask = COREDUMP_KERNEL,
		},
		.bytes = COREDUMP_ACK_SIZE_VER0 / 2,
		.no_marker = true,
	};

	check_refused_ack(_metadata, self, &refused);
}

/*
 * The kernels a server built against this header can't meet here:
 * negotiate() against their requests, no coredump involved.
 */

/* The request of a kernel with the first structs and features. */
static const struct coredump_req req_ver0 = {
	.size			= COREDUMP_REQ_SIZE_VER0,
	.size_ack		= COREDUMP_ACK_SIZE_VER0,
	.mask			= COREDUMP_KERNEL | COREDUMP_USERSPACE |
				  COREDUMP_REJECT | COREDUMP_WAIT,
};

/* The request of this kernel. */
static const struct coredump_req req_ver1 = {
	.size			= COREDUMP_REQ_SIZE_VER1,
	.size_ack		= COREDUMP_ACK_SIZE_VER1,
	.mask			= COREDUMP_KERNEL | COREDUMP_USERSPACE |
				  COREDUMP_REJECT | COREDUMP_WAIT |
				  COREDUMP_RECORDS | COREDUMP_SPARSE |
				  COREDUMP_MEMORY_TYPES,
	.memory_types		= COREDUMP_MEMORY_ANON_PRIVATE |
				  COREDUMP_MEMORY_ANON_SHARED,
	.memory_types_mask	= TEST_MEMORY_ALL,
};

/* A kernel with the first structs gets the first ack and nothing newer. */
TEST(negotiate_ver0_kernel)
{
	struct coredump_ack ack;

	negotiate(&req_ver0, &server_build_ver1, &ack);
	ASSERT_EQ(ack.size, COREDUMP_ACK_SIZE_VER0);
	ASSERT_EQ(ack.mask, COREDUMP_KERNEL);
	ASSERT_EQ(ack.memory_types, 0);
}

/* A kernel with records and sparse but the first structs: both, no types. */
TEST(negotiate_sparse_kernel)
{
	struct coredump_req req = req_ver0;
	struct coredump_ack ack;

	req.mask |= COREDUMP_RECORDS | COREDUMP_SPARSE;
	negotiate(&req, &server_build_ver1, &ack);
	ASSERT_EQ(ack.size, COREDUMP_ACK_SIZE_VER0);
	ASSERT_EQ(ack.mask, COREDUMP_KERNEL | COREDUMP_RECORDS | COREDUMP_SPARSE);
	ASSERT_EQ(ack.memory_types, 0);
}

/* Records without sparse: sparse isn't raised on its own. */
TEST(negotiate_records_without_sparse)
{
	struct coredump_req req = req_ver0;
	struct coredump_ack ack;

	req.mask |= COREDUMP_RECORDS;
	negotiate(&req, &server_build_ver1, &ack);
	ASSERT_EQ(ack.mask, COREDUMP_KERNEL | COREDUMP_RECORDS);
}

/*
 * A feature whose ack field lies past what the kernel accepts can't be
 * raised. No kernel offers the memory types without the room for them, so a
 * request that does stands in for a feature newer than this header.
 */
TEST(negotiate_types_need_room)
{
	struct coredump_req req = req_ver0;
	struct coredump_ack ack;

	req.mask |= COREDUMP_MEMORY_TYPES;
	negotiate(&req, &server_build_ver1, &ack);
	ASSERT_EQ(ack.size, COREDUMP_ACK_SIZE_VER0);
	ASSERT_EQ(ack.mask, COREDUMP_KERNEL);
	ASSERT_EQ(ack.memory_types, 0);
}

/* This kernel: the policy applied to the task's selection. */
TEST(negotiate_ver1_kernel)
{
	struct coredump_ack ack;

	negotiate(&req_ver1, &server_build_ver1, &ack);
	ASSERT_EQ(ack.size, COREDUMP_ACK_SIZE_VER1);
	ASSERT_EQ(ack.mask, COREDUMP_KERNEL | COREDUMP_RECORDS | COREDUMP_SPARSE |
			    COREDUMP_MEMORY_TYPES);
	ASSERT_EQ(ack.memory_types, COREDUMP_MEMORY_ANON_PRIVATE |
				     COREDUMP_MEMORY_ELF_HEADERS);
}

/* A kernel that doesn't know a type the policy adds isn't asked for it. */
TEST(negotiate_unknown_type)
{
	struct coredump_req req = req_ver1;
	struct coredump_ack ack;

	req.memory_types_mask &= ~(__u64)COREDUMP_MEMORY_ELF_HEADERS;
	negotiate(&req, &server_build_ver1, &ack);
	ASSERT_EQ(ack.mask, COREDUMP_KERNEL | COREDUMP_RECORDS | COREDUMP_SPARSE |
			    COREDUMP_MEMORY_TYPES);
	ASSERT_EQ(ack.memory_types, COREDUMP_MEMORY_ANON_PRIVATE);
}

TEST_HARNESS_MAIN
