// SPDX-License-Identifier: GPL-2.0

#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "coredump_test.h"

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

TEST_F(coredump, socket)
{
	int pidfd, ret, status;
	pid_t pid, pid_coredump_server;
	struct stat st;
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;

	ASSERT_TRUE(set_core_pattern("@/tmp/coredump.socket"));

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1, fd_core_file = -1;
		int exit_code = EXIT_FAILURE;

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

		if (!get_pidfd_info(fd_peer_pidfd, &info))
			goto out;

		if (!(info.mask & PIDFD_INFO_COREDUMP))
			goto out;

		if (!(info.coredump_mask & PIDFD_COREDUMPED))
			goto out;

		fd_core_file = creat("/tmp/coredump.file", 0644);
		if (fd_core_file < 0)
			goto out;

		for (;;) {
			char buffer[4096];
			ssize_t bytes_read, bytes_write;

			bytes_read = read(fd_coredump, buffer, sizeof(buffer));
			if (bytes_read < 0)
				goto out;

			if (bytes_read == 0)
				break;

			bytes_write = write(fd_core_file, buffer, bytes_read);
			if (bytes_read != bytes_write)
				goto out;
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
}

TEST_F(coredump, socket_detect_userspace_client)
{
	int pidfd, ret, status;
	pid_t pid, pid_coredump_server;
	struct stat st;
	struct pidfd_info info = {
		.mask = PIDFD_INFO_COREDUMP,
	};
	int ipc_sockets[2];
	char c;

	ASSERT_TRUE(set_core_pattern("@/tmp/coredump.socket"));

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, fd_coredump = -1, fd_peer_pidfd = -1;
		int exit_code = EXIT_FAILURE;

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

		if (!get_pidfd_info(fd_peer_pidfd, &info))
			goto out;

		if (!(info.mask & PIDFD_INFO_COREDUMP))
			goto out;

		if (info.coredump_mask & PIDFD_COREDUMPED)
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
	if (pid == 0) {
		int fd_socket;
		ssize_t ret;
		const struct sockaddr_un coredump_sk = {
			.sun_family = AF_UNIX,
			.sun_path = "/tmp/coredump.socket",
		};
		size_t coredump_sk_len =
			offsetof(struct sockaddr_un, sun_path) +
			sizeof("/tmp/coredump.socket");

		fd_socket = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd_socket < 0)
			_exit(EXIT_FAILURE);

		ret = connect(fd_socket, (const struct sockaddr *)&coredump_sk, coredump_sk_len);
		if (ret < 0)
			_exit(EXIT_FAILURE);

		close(fd_socket);
		_exit(EXIT_SUCCESS);
	}

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	ASSERT_TRUE(get_pidfd_info(pidfd, &info));
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_EQ((info.coredump_mask & PIDFD_COREDUMPED), 0);

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);

	ASSERT_NE(stat("/tmp/coredump.file", &st), 0);
	ASSERT_EQ(errno, ENOENT);
}

TEST_F(coredump, socket_enoent)
{
	int pidfd, status;
	pid_t pid;

	ASSERT_TRUE(set_core_pattern("@/tmp/coredump.socket"));

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		crashing_child();

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_FALSE(WCOREDUMP(status));
}

TEST_F(coredump, socket_no_listener)
{
	int pidfd, ret, status;
	pid_t pid, pid_coredump_server;
	int ipc_sockets[2];
	char c;
	const struct sockaddr_un coredump_sk = {
		.sun_family = AF_UNIX,
		.sun_path = "/tmp/coredump.socket",
	};
	size_t coredump_sk_len = offsetof(struct sockaddr_un, sun_path) +
				 sizeof("/tmp/coredump.socket");

	ASSERT_TRUE(set_core_pattern("@/tmp/coredump.socket"));

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1;
		int exit_code = EXIT_FAILURE;

		close(ipc_sockets[0]);

		fd_server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (fd_server < 0)
			goto out;

		ret = bind(fd_server, (const struct sockaddr *)&coredump_sk, coredump_sk_len);
		if (ret < 0)
			goto out;

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			goto out;

		exit_code = EXIT_SUCCESS;
out:
		if (fd_server >= 0)
			close(fd_server);
		close(ipc_sockets[1]);
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

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

TEST_F(coredump, socket_invalid_paths)
{
	ASSERT_FALSE(set_core_pattern("@ /tmp/coredump.socket"));
	ASSERT_FALSE(set_core_pattern("@/tmp/../coredump.socket"));
	ASSERT_FALSE(set_core_pattern("@../coredump.socket"));
	ASSERT_FALSE(set_core_pattern("@/tmp/coredump.socket/.."));
	ASSERT_FALSE(set_core_pattern("@.."));

	ASSERT_FALSE(set_core_pattern("@@ /tmp/coredump.socket"));
	ASSERT_FALSE(set_core_pattern("@@/tmp/../coredump.socket"));
	ASSERT_FALSE(set_core_pattern("@@../coredump.socket"));
	ASSERT_FALSE(set_core_pattern("@@/tmp/coredump.socket/.."));
	ASSERT_FALSE(set_core_pattern("@@.."));

	ASSERT_FALSE(set_core_pattern("@@@/tmp/coredump.socket"));
}

TEST_HARNESS_MAIN
