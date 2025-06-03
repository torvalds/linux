// SPDX-License-Identifier: GPL-2.0

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <linux/limits.h>
#include <pthread.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../kselftest_harness.h"
#include "../pidfd/pidfd.h"

#define STACKDUMP_FILE "stack_values"
#define STACKDUMP_SCRIPT "stackdump"
#define NUM_THREAD_SPAWN 128

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

static void *do_nothing(void *)
{
	while (1)
		pause();

	return NULL;
}

static void crashing_child(void)
{
	pthread_t thread;
	int i;

	for (i = 0; i < NUM_THREAD_SPAWN; ++i)
		pthread_create(&thread, NULL, do_nothing, NULL);

	/* crash on purpose */
	i = *(int *)NULL;
}

FIXTURE(coredump)
{
	char original_core_pattern[256];
	pid_t pid_coredump_server;
};

FIXTURE_SETUP(coredump)
{
	FILE *file;
	int ret;

	self->pid_coredump_server = -ESRCH;
	file = fopen("/proc/sys/kernel/core_pattern", "r");
	ASSERT_NE(NULL, file);

	ret = fread(self->original_core_pattern, 1, sizeof(self->original_core_pattern), file);
	ASSERT_TRUE(ret || feof(file));
	ASSERT_LT(ret, sizeof(self->original_core_pattern));

	self->original_core_pattern[ret] = '\0';

	ret = fclose(file);
	ASSERT_EQ(0, ret);
}

FIXTURE_TEARDOWN(coredump)
{
	const char *reason;
	FILE *file;
	int ret, status;

	unlink(STACKDUMP_FILE);

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

	return;
fail:
	/* This should never happen */
	fprintf(stderr, "Failed to cleanup stackdump test: %s\n", reason);
}

TEST_F_TIMEOUT(coredump, stackdump, 120)
{
	unsigned long long stack;
	char *test_dir, *line;
	size_t line_length;
	char buf[PAGE_SIZE];
	int ret, i, status;
	FILE *file;
	pid_t pid;

	/*
	 * Step 1: Setup core_pattern so that the stackdump script is executed when the child
	 * process crashes
	 */
	ret = readlink("/proc/self/exe", buf, sizeof(buf));
	ASSERT_NE(-1, ret);
	ASSERT_LT(ret, sizeof(buf));
	buf[ret] = '\0';

	test_dir = dirname(buf);

	file = fopen("/proc/sys/kernel/core_pattern", "w");
	ASSERT_NE(NULL, file);

	ret = fprintf(file, "|%1$s/%2$s %%P %1$s/%3$s", test_dir, STACKDUMP_SCRIPT, STACKDUMP_FILE);
	ASSERT_LT(0, ret);

	ret = fclose(file);
	ASSERT_EQ(0, ret);

	/* Step 2: Create a process who spawns some threads then crashes */
	pid = fork();
	ASSERT_TRUE(pid >= 0);
	if (pid == 0)
		crashing_child();

	/*
	 * Step 3: Wait for the stackdump script to write the stack pointers to the stackdump file
	 */
	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_TRUE(WCOREDUMP(status));

	for (i = 0; i < 10; ++i) {
		file = fopen(STACKDUMP_FILE, "r");
		if (file)
			break;
		sleep(1);
	}
	ASSERT_NE(file, NULL);

	/* Step 4: Make sure all stack pointer values are non-zero */
	line = NULL;
	for (i = 0; -1 != getline(&line, &line_length, file); ++i) {
		stack = strtoull(line, NULL, 10);
		ASSERT_NE(stack, 0);
	}
	free(line);

	ASSERT_EQ(i, 1 + NUM_THREAD_SPAWN);

	fclose(file);
}

static int create_and_listen_unix_socket(const char *path)
{
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	assert(strlen(path) < sizeof(addr.sun_path) - 1);
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	size_t addr_len =
		offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1;
	int fd, ret;

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		goto out;

	ret = bind(fd, (const struct sockaddr *)&addr, addr_len);
	if (ret < 0)
		goto out;

	ret = listen(fd, 1);
	if (ret < 0)
		goto out;

	return fd;

out:
	if (fd >= 0)
		close(fd);
	return -1;
}

static bool set_core_pattern(const char *pattern)
{
	FILE *file;
	int ret;

	file = fopen("/proc/sys/kernel/core_pattern", "w");
	if (!file)
		return false;

	ret = fprintf(file, "%s", pattern);
	fclose(file);

	return ret == strlen(pattern);
}

static int get_peer_pidfd(int fd)
{
	int fd_peer_pidfd;
	socklen_t fd_peer_pidfd_len = sizeof(fd_peer_pidfd);
	int ret = getsockopt(fd, SOL_SOCKET, SO_PEERPIDFD, &fd_peer_pidfd,
			     &fd_peer_pidfd_len);
	if (ret < 0) {
		fprintf(stderr, "%m - Failed to retrieve peer pidfd for coredump socket connection\n");
		return -1;
	}
	return fd_peer_pidfd;
}

static bool get_pidfd_info(int fd_peer_pidfd, struct pidfd_info *info)
{
	memset(info, 0, sizeof(*info));
	info->mask = PIDFD_INFO_EXIT | PIDFD_INFO_COREDUMP;
	return ioctl(fd_peer_pidfd, PIDFD_GET_INFO, info) == 0;
}

static void
wait_and_check_coredump_server(pid_t pid_coredump_server,
			       struct __test_metadata *const _metadata,
			       FIXTURE_DATA(coredump)* self)
{
	int status;
	waitpid(pid_coredump_server, &status, 0);
	self->pid_coredump_server = -ESRCH;
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);
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
	system("file /tmp/coredump.file");
}

TEST_F(coredump, socket_detect_userspace_client)
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

TEST_HARNESS_MAIN
