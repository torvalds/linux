// SPDX-License-Identifier: GPL-2.0

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

static void *do_nothing(void *)
{
	while (1)
		pause();
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
	char buf[PATH_MAX];
	FILE *file;
	char *dir;
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
	struct sigaction action = {};
	unsigned long long stack;
	char *test_dir, *line;
	size_t line_length;
	char buf[PATH_MAX];
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

TEST_F(coredump, socket)
{
	int fd, pidfd, ret, status;
	FILE *file;
	pid_t pid, pid_coredump_server;
	struct stat st;
	char core_file[PATH_MAX];
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;
	const struct sockaddr_un coredump_sk = {
		.sun_family = AF_UNIX,
		.sun_path = "/tmp/coredump.socket",
	};
	size_t coredump_sk_len = offsetof(struct sockaddr_un, sun_path) +
				 sizeof("/tmp/coredump.socket");

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	file = fopen("/proc/sys/kernel/core_pattern", "w");
	ASSERT_NE(file, NULL);

	ret = fprintf(file, "@/tmp/coredump.socket");
	ASSERT_EQ(ret, strlen("@/tmp/coredump.socket"));
	ASSERT_EQ(fclose(file), 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server, fd_coredump, fd_peer_pidfd, fd_core_file;
		socklen_t fd_peer_pidfd_len;

		close(ipc_sockets[0]);

		fd_server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (fd_server < 0)
			_exit(EXIT_FAILURE);

		ret = bind(fd_server, (const struct sockaddr *)&coredump_sk, coredump_sk_len);
		if (ret < 0) {
			fprintf(stderr, "Failed to bind coredump socket\n");
			close(fd_server);
			close(ipc_sockets[1]);
			_exit(EXIT_FAILURE);
		}

		ret = listen(fd_server, 1);
		if (ret < 0) {
			fprintf(stderr, "Failed to listen on coredump socket\n");
			close(fd_server);
			close(ipc_sockets[1]);
			_exit(EXIT_FAILURE);
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			close(fd_server);
			close(ipc_sockets[1]);
			_exit(EXIT_FAILURE);
		}

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "Failed to accept coredump socket connection\n");
			close(fd_server);
			_exit(EXIT_FAILURE);
		}

		fd_peer_pidfd_len = sizeof(fd_peer_pidfd);
		ret = getsockopt(fd_coredump, SOL_SOCKET, SO_PEERPIDFD,
				 &fd_peer_pidfd, &fd_peer_pidfd_len);
		if (ret < 0) {
			fprintf(stderr, "%m - Failed to retrieve peer pidfd for coredump socket connection\n");
			close(fd_coredump);
			close(fd_server);
			_exit(EXIT_FAILURE);
		}

		memset(&info, 0, sizeof(info));
		info.mask = PIDFD_INFO_EXIT | PIDFD_INFO_COREDUMP;
		ret = ioctl(fd_peer_pidfd, PIDFD_GET_INFO, &info);
		if (ret < 0) {
			fprintf(stderr, "Failed to retrieve pidfd info from peer pidfd for coredump socket connection\n");
			close(fd_coredump);
			close(fd_server);
			close(fd_peer_pidfd);
			_exit(EXIT_FAILURE);
		}

		if (!(info.mask & PIDFD_INFO_COREDUMP)) {
			fprintf(stderr, "Missing coredump information from coredumping task\n");
			close(fd_coredump);
			close(fd_server);
			close(fd_peer_pidfd);
			_exit(EXIT_FAILURE);
		}

		if (!(info.coredump_mask & PIDFD_COREDUMPED)) {
			fprintf(stderr, "Received connection from non-coredumping task\n");
			close(fd_coredump);
			close(fd_server);
			close(fd_peer_pidfd);
			_exit(EXIT_FAILURE);
		}

		fd_core_file = creat("/tmp/coredump.file", 0644);
		if (fd_core_file < 0) {
			fprintf(stderr, "Failed to create coredump file\n");
			close(fd_coredump);
			close(fd_server);
			close(fd_peer_pidfd);
			_exit(EXIT_FAILURE);
		}

		for (;;) {
			char buffer[4096];
			ssize_t bytes_read, bytes_write;

			bytes_read = read(fd_coredump, buffer, sizeof(buffer));
			if (bytes_read < 0) {
				close(fd_coredump);
				close(fd_server);
				close(fd_peer_pidfd);
				close(fd_core_file);
				_exit(EXIT_FAILURE);
			}

			if (bytes_read == 0)
				break;

			bytes_write = write(fd_core_file, buffer, bytes_read);
			if (bytes_read != bytes_write) {
				close(fd_coredump);
				close(fd_server);
				close(fd_peer_pidfd);
				close(fd_core_file);
				_exit(EXIT_FAILURE);
			}
		}

		close(fd_coredump);
		close(fd_server);
		close(fd_peer_pidfd);
		close(fd_core_file);
		_exit(EXIT_SUCCESS);
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

	info.mask = PIDFD_INFO_EXIT | PIDFD_INFO_COREDUMP;
	ASSERT_EQ(ioctl(pidfd, PIDFD_GET_INFO, &info), 0);
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_GT((info.coredump_mask & PIDFD_COREDUMPED), 0);

	waitpid(pid_coredump_server, &status, 0);
	self->pid_coredump_server = -ESRCH;
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	ASSERT_EQ(stat("/tmp/coredump.file", &st), 0);
	ASSERT_GT(st.st_size, 0);
	/*
	 * We should somehow validate the produced core file.
	 * For now just allow for visual inspection
	 */
	system("file /tmp/coredump.file");
}

TEST_F(coredump, socket_detect_userspace_client)
{
	int fd, pidfd, ret, status;
	FILE *file;
	pid_t pid, pid_coredump_server;
	struct stat st;
	char core_file[PATH_MAX];
	struct pidfd_info info = {};
	int ipc_sockets[2];
	char c;
	const struct sockaddr_un coredump_sk = {
		.sun_family = AF_UNIX,
		.sun_path = "/tmp/coredump.socket",
	};
	size_t coredump_sk_len = offsetof(struct sockaddr_un, sun_path) +
				 sizeof("/tmp/coredump.socket");

	file = fopen("/proc/sys/kernel/core_pattern", "w");
	ASSERT_NE(file, NULL);

	ret = fprintf(file, "@/tmp/coredump.socket");
	ASSERT_EQ(ret, strlen("@/tmp/coredump.socket"));
	ASSERT_EQ(fclose(file), 0);

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server, fd_coredump, fd_peer_pidfd, fd_core_file;
		socklen_t fd_peer_pidfd_len;

		close(ipc_sockets[0]);

		fd_server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (fd_server < 0)
			_exit(EXIT_FAILURE);

		ret = bind(fd_server, (const struct sockaddr *)&coredump_sk, coredump_sk_len);
		if (ret < 0) {
			fprintf(stderr, "Failed to bind coredump socket\n");
			close(fd_server);
			close(ipc_sockets[1]);
			_exit(EXIT_FAILURE);
		}

		ret = listen(fd_server, 1);
		if (ret < 0) {
			fprintf(stderr, "Failed to listen on coredump socket\n");
			close(fd_server);
			close(ipc_sockets[1]);
			_exit(EXIT_FAILURE);
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			close(fd_server);
			close(ipc_sockets[1]);
			_exit(EXIT_FAILURE);
		}

		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "Failed to accept coredump socket connection\n");
			close(fd_server);
			_exit(EXIT_FAILURE);
		}

		fd_peer_pidfd_len = sizeof(fd_peer_pidfd);
		ret = getsockopt(fd_coredump, SOL_SOCKET, SO_PEERPIDFD,
				 &fd_peer_pidfd, &fd_peer_pidfd_len);
		if (ret < 0) {
			fprintf(stderr, "%m - Failed to retrieve peer pidfd for coredump socket connection\n");
			close(fd_coredump);
			close(fd_server);
			_exit(EXIT_FAILURE);
		}

		memset(&info, 0, sizeof(info));
		info.mask = PIDFD_INFO_EXIT | PIDFD_INFO_COREDUMP;
		ret = ioctl(fd_peer_pidfd, PIDFD_GET_INFO, &info);
		if (ret < 0) {
			fprintf(stderr, "Failed to retrieve pidfd info from peer pidfd for coredump socket connection\n");
			close(fd_coredump);
			close(fd_server);
			close(fd_peer_pidfd);
			_exit(EXIT_FAILURE);
		}

		if (!(info.mask & PIDFD_INFO_COREDUMP)) {
			fprintf(stderr, "Missing coredump information from coredumping task\n");
			close(fd_coredump);
			close(fd_server);
			close(fd_peer_pidfd);
			_exit(EXIT_FAILURE);
		}

		if (info.coredump_mask & PIDFD_COREDUMPED) {
			fprintf(stderr, "Received unexpected connection from coredumping task\n");
			close(fd_coredump);
			close(fd_server);
			close(fd_peer_pidfd);
			_exit(EXIT_FAILURE);
		}

		close(fd_coredump);
		close(fd_server);
		close(fd_peer_pidfd);
		close(fd_core_file);
		_exit(EXIT_SUCCESS);
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

		fd_socket = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd_socket < 0)
			_exit(EXIT_FAILURE);


		ret = connect(fd_socket, (const struct sockaddr *)&coredump_sk, coredump_sk_len);
		if (ret < 0)
			_exit(EXIT_FAILURE);

		(void *)write(fd_socket, &(char){ 0 }, 1);
		close(fd_socket);
		_exit(EXIT_SUCCESS);
	}

	pidfd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pidfd, 0);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	info.mask = PIDFD_INFO_EXIT | PIDFD_INFO_COREDUMP;
	ASSERT_EQ(ioctl(pidfd, PIDFD_GET_INFO, &info), 0);
	ASSERT_GT((info.mask & PIDFD_INFO_COREDUMP), 0);
	ASSERT_EQ((info.coredump_mask & PIDFD_COREDUMPED), 0);

	waitpid(pid_coredump_server, &status, 0);
	self->pid_coredump_server = -ESRCH;
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	ASSERT_NE(stat("/tmp/coredump.file", &st), 0);
	ASSERT_EQ(errno, ENOENT);
}

TEST_F(coredump, socket_enoent)
{
	int pidfd, ret, status;
	FILE *file;
	pid_t pid;
	char core_file[PATH_MAX];

	file = fopen("/proc/sys/kernel/core_pattern", "w");
	ASSERT_NE(file, NULL);

	ret = fprintf(file, "@/tmp/coredump.socket");
	ASSERT_EQ(ret, strlen("@/tmp/coredump.socket"));
	ASSERT_EQ(fclose(file), 0);

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
	FILE *file;
	pid_t pid, pid_coredump_server;
	int ipc_sockets[2];
	char c;
	const struct sockaddr_un coredump_sk = {
		.sun_family = AF_UNIX,
		.sun_path = "/tmp/coredump.socket",
	};
	size_t coredump_sk_len = offsetof(struct sockaddr_un, sun_path) +
				 sizeof("/tmp/coredump.socket");

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	file = fopen("/proc/sys/kernel/core_pattern", "w");
	ASSERT_NE(file, NULL);

	ret = fprintf(file, "@/tmp/coredump.socket");
	ASSERT_EQ(ret, strlen("@/tmp/coredump.socket"));
	ASSERT_EQ(fclose(file), 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server;
		socklen_t fd_peer_pidfd_len;

		close(ipc_sockets[0]);

		fd_server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (fd_server < 0)
			_exit(EXIT_FAILURE);

		ret = bind(fd_server, (const struct sockaddr *)&coredump_sk, coredump_sk_len);
		if (ret < 0) {
			fprintf(stderr, "Failed to bind coredump socket\n");
			close(fd_server);
			close(ipc_sockets[1]);
			_exit(EXIT_FAILURE);
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0) {
			close(fd_server);
			close(ipc_sockets[1]);
			_exit(EXIT_FAILURE);
		}

		close(fd_server);
		close(ipc_sockets[1]);
		_exit(EXIT_SUCCESS);
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

	waitpid(pid_coredump_server, &status, 0);
	self->pid_coredump_server = -ESRCH;
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);
}

TEST_HARNESS_MAIN
