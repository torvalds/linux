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

		if (!check_coredump_req(&req, COREDUMP_ACK_SIZE_VER0,
					COREDUMP_KERNEL | COREDUMP_USERSPACE |
					COREDUMP_REJECT | COREDUMP_WAIT)) {
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

		if (!check_coredump_req(&req, COREDUMP_ACK_SIZE_VER0,
					COREDUMP_KERNEL | COREDUMP_USERSPACE |
					COREDUMP_REJECT | COREDUMP_WAIT)) {
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

		if (!check_coredump_req(&req, COREDUMP_ACK_SIZE_VER0,
					COREDUMP_KERNEL | COREDUMP_USERSPACE |
					COREDUMP_REJECT | COREDUMP_WAIT)) {
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

TEST_F(coredump, socket_request_invalid_flag_combination)
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
			fprintf(stderr, "socket_request_invalid_flag_combination: create_and_listen_unix_socket failed: %m\n");
			goto out;
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			fprintf(stderr, "socket_request_invalid_flag_combination: write_nointr to ipc socket failed: %m\n");
			goto out;
		}

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "socket_request_invalid_flag_combination: accept4 failed: %m\n");
			goto out;
		}

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0) {
			fprintf(stderr, "socket_request_invalid_flag_combination: get_peer_pidfd failed\n");
			goto out;
		}

		if (!get_pidfd_info(fd_peer_pidfd, &info)) {
			fprintf(stderr, "socket_request_invalid_flag_combination: get_pidfd_info failed\n");
			goto out;
		}

		if (!(info.mask & PIDFD_INFO_COREDUMP)) {
			fprintf(stderr, "socket_request_invalid_flag_combination: PIDFD_INFO_COREDUMP not set in mask\n");
			goto out;
		}

		if (!(info.coredump_mask & PIDFD_COREDUMPED)) {
			fprintf(stderr, "socket_request_invalid_flag_combination: PIDFD_COREDUMPED not set in coredump_mask\n");
			goto out;
		}

		if (!read_coredump_req(fd_coredump, &req)) {
			fprintf(stderr, "socket_request_invalid_flag_combination: read_coredump_req failed\n");
			goto out;
		}

		if (!check_coredump_req(&req, COREDUMP_ACK_SIZE_VER0,
					COREDUMP_KERNEL | COREDUMP_USERSPACE |
					COREDUMP_REJECT | COREDUMP_WAIT)) {
			fprintf(stderr, "socket_request_invalid_flag_combination: check_coredump_req failed\n");
			goto out;
		}

		if (!send_coredump_ack(fd_coredump, &req,
				       COREDUMP_KERNEL | COREDUMP_REJECT | COREDUMP_WAIT, 0)) {
			fprintf(stderr, "socket_request_invalid_flag_combination: send_coredump_ack failed\n");
			goto out;
		}

		if (!read_marker(fd_coredump, COREDUMP_MARK_CONFLICTING)) {
			fprintf(stderr, "socket_request_invalid_flag_combination: read_marker COREDUMP_MARK_CONFLICTING failed\n");
			goto out;
		}

		exit_code = EXIT_SUCCESS;
		fprintf(stderr, "socket_request_invalid_flag_combination: completed successfully\n");
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

TEST_F(coredump, socket_request_unknown_flag)
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
			fprintf(stderr, "socket_request_unknown_flag: create_and_listen_unix_socket failed: %m\n");
			goto out;
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			fprintf(stderr, "socket_request_unknown_flag: write_nointr to ipc socket failed: %m\n");
			goto out;
		}

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "socket_request_unknown_flag: accept4 failed: %m\n");
			goto out;
		}

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0) {
			fprintf(stderr, "socket_request_unknown_flag: get_peer_pidfd failed\n");
			goto out;
		}

		if (!get_pidfd_info(fd_peer_pidfd, &info)) {
			fprintf(stderr, "socket_request_unknown_flag: get_pidfd_info failed\n");
			goto out;
		}

		if (!(info.mask & PIDFD_INFO_COREDUMP)) {
			fprintf(stderr, "socket_request_unknown_flag: PIDFD_INFO_COREDUMP not set in mask\n");
			goto out;
		}

		if (!(info.coredump_mask & PIDFD_COREDUMPED)) {
			fprintf(stderr, "socket_request_unknown_flag: PIDFD_COREDUMPED not set in coredump_mask\n");
			goto out;
		}

		if (!read_coredump_req(fd_coredump, &req)) {
			fprintf(stderr, "socket_request_unknown_flag: read_coredump_req failed\n");
			goto out;
		}

		if (!check_coredump_req(&req, COREDUMP_ACK_SIZE_VER0,
					COREDUMP_KERNEL | COREDUMP_USERSPACE |
					COREDUMP_REJECT | COREDUMP_WAIT)) {
			fprintf(stderr, "socket_request_unknown_flag: check_coredump_req failed\n");
			goto out;
		}

		if (!send_coredump_ack(fd_coredump, &req, (1ULL << 63), 0)) {
			fprintf(stderr, "socket_request_unknown_flag: send_coredump_ack failed\n");
			goto out;
		}

		if (!read_marker(fd_coredump, COREDUMP_MARK_UNSUPPORTED)) {
			fprintf(stderr, "socket_request_unknown_flag: read_marker COREDUMP_MARK_UNSUPPORTED failed\n");
			goto out;
		}

		exit_code = EXIT_SUCCESS;
		fprintf(stderr, "socket_request_unknown_flag: completed successfully\n");
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

TEST_F(coredump, socket_request_invalid_size_small)
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
			fprintf(stderr, "socket_request_invalid_size_small: create_and_listen_unix_socket failed: %m\n");
			goto out;
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			fprintf(stderr, "socket_request_invalid_size_small: write_nointr to ipc socket failed: %m\n");
			goto out;
		}

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "socket_request_invalid_size_small: accept4 failed: %m\n");
			goto out;
		}

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0) {
			fprintf(stderr, "socket_request_invalid_size_small: get_peer_pidfd failed\n");
			goto out;
		}

		if (!get_pidfd_info(fd_peer_pidfd, &info)) {
			fprintf(stderr, "socket_request_invalid_size_small: get_pidfd_info failed\n");
			goto out;
		}

		if (!(info.mask & PIDFD_INFO_COREDUMP)) {
			fprintf(stderr, "socket_request_invalid_size_small: PIDFD_INFO_COREDUMP not set in mask\n");
			goto out;
		}

		if (!(info.coredump_mask & PIDFD_COREDUMPED)) {
			fprintf(stderr, "socket_request_invalid_size_small: PIDFD_COREDUMPED not set in coredump_mask\n");
			goto out;
		}

		if (!read_coredump_req(fd_coredump, &req)) {
			fprintf(stderr, "socket_request_invalid_size_small: read_coredump_req failed\n");
			goto out;
		}

		if (!check_coredump_req(&req, COREDUMP_ACK_SIZE_VER0,
					COREDUMP_KERNEL | COREDUMP_USERSPACE |
					COREDUMP_REJECT | COREDUMP_WAIT)) {
			fprintf(stderr, "socket_request_invalid_size_small: check_coredump_req failed\n");
			goto out;
		}

		if (!send_coredump_ack(fd_coredump, &req,
				       COREDUMP_REJECT | COREDUMP_WAIT,
				       COREDUMP_ACK_SIZE_VER0 / 2)) {
			fprintf(stderr, "socket_request_invalid_size_small: send_coredump_ack failed\n");
			goto out;
		}

		if (!read_marker(fd_coredump, COREDUMP_MARK_MINSIZE)) {
			fprintf(stderr, "socket_request_invalid_size_small: read_marker COREDUMP_MARK_MINSIZE failed\n");
			goto out;
		}

		exit_code = EXIT_SUCCESS;
		fprintf(stderr, "socket_request_invalid_size_small: completed successfully\n");
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

TEST_F(coredump, socket_request_invalid_size_large)
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
			fprintf(stderr, "socket_request_invalid_size_large: create_and_listen_unix_socket failed: %m\n");
			goto out;
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			fprintf(stderr, "socket_request_invalid_size_large: write_nointr to ipc socket failed: %m\n");
			goto out;
		}

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "socket_request_invalid_size_large: accept4 failed: %m\n");
			goto out;
		}

		fd_peer_pidfd = get_peer_pidfd(fd_coredump);
		if (fd_peer_pidfd < 0) {
			fprintf(stderr, "socket_request_invalid_size_large: get_peer_pidfd failed\n");
			goto out;
		}

		if (!get_pidfd_info(fd_peer_pidfd, &info)) {
			fprintf(stderr, "socket_request_invalid_size_large: get_pidfd_info failed\n");
			goto out;
		}

		if (!(info.mask & PIDFD_INFO_COREDUMP)) {
			fprintf(stderr, "socket_request_invalid_size_large: PIDFD_INFO_COREDUMP not set in mask\n");
			goto out;
		}

		if (!(info.coredump_mask & PIDFD_COREDUMPED)) {
			fprintf(stderr, "socket_request_invalid_size_large: PIDFD_COREDUMPED not set in coredump_mask\n");
			goto out;
		}

		if (!read_coredump_req(fd_coredump, &req)) {
			fprintf(stderr, "socket_request_invalid_size_large: read_coredump_req failed\n");
			goto out;
		}

		if (!check_coredump_req(&req, COREDUMP_ACK_SIZE_VER0,
					COREDUMP_KERNEL | COREDUMP_USERSPACE |
					COREDUMP_REJECT | COREDUMP_WAIT)) {
			fprintf(stderr, "socket_request_invalid_size_large: check_coredump_req failed\n");
			goto out;
		}

		if (!send_coredump_ack(fd_coredump, &req,
				       COREDUMP_REJECT | COREDUMP_WAIT,
				       COREDUMP_ACK_SIZE_VER0 + PAGE_SIZE)) {
			fprintf(stderr, "socket_request_invalid_size_large: send_coredump_ack failed\n");
			goto out;
		}

		if (!read_marker(fd_coredump, COREDUMP_MARK_MAXSIZE)) {
			fprintf(stderr, "socket_request_invalid_size_large: read_marker COREDUMP_MARK_MAXSIZE failed\n");
			goto out;
		}

		exit_code = EXIT_SUCCESS;
		fprintf(stderr, "socket_request_invalid_size_large: completed successfully\n");
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

/*
 * Test: PIDFD_INFO_COREDUMP_SIGNAL via socket coredump with SIGSEGV
 *
 * Verify that when using socket-based coredump protocol,
 * the coredump_signal field is correctly exposed as SIGSEGV.
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

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

/*
 * Test: PIDFD_INFO_COREDUMP_SIGNAL via socket coredump with SIGABRT
 *
 * Verify that when using socket-based coredump protocol,
 * the coredump_signal field is correctly exposed as SIGABRT.
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

			if (!check_coredump_req(&req, COREDUMP_ACK_SIZE_VER0,
						COREDUMP_KERNEL | COREDUMP_USERSPACE |
						COREDUMP_REJECT | COREDUMP_WAIT)) {
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
			if (!check_coredump_req(&req, COREDUMP_ACK_SIZE_VER0,
						COREDUMP_KERNEL | COREDUMP_USERSPACE |
						COREDUMP_REJECT | COREDUMP_WAIT)) {
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

TEST_HARNESS_MAIN
