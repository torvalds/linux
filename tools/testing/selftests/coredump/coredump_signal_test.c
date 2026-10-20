// SPDX-License-Identifier: GPL-2.0

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

#include "coredump_test.h"

/* Big enough to fill the socket buffer many times over. */
#define CRASH_MAPPING_SIZE (32 * 1024 * 1024)

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

	return;
fail:
	/* This should never happen */
	fprintf(stderr, "Failed to cleanup coredump test: %s\n", reason);
}

static volatile int waiter_ready;

static void usr1_handler(int sig)
{
}

/* Sleeps in sigtimedwait() with SIGUSR1 unblocked only inside the kernel. */
static void *sigwaiter(void *arg)
{
	sigset_t set;
	siginfo_t info;

	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	__atomic_store_n(&waiter_ready, 1, __ATOMIC_RELEASE);
	for (;;)
		sigtimedwait(&set, &info, NULL);
	return NULL;
}

/*
 * Crash with a shared SIGUSR1 still queued for this thread. The vfork()
 * child queues it while we sleep killably and then the SIGSEGV that is
 * dequeued first. The zap wakes the sibling out of sigtimedwait() and its
 * mask restore retargets SIGUSR1 to the dumper.
 */
static void crashing_child_retarget(bool queue_shared)
{
	sigset_t all, old;
	pthread_t thread;
	pid_t pid, tid;
	char *p;

	p = mmap(NULL, CRASH_MAPPING_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		_exit(EXIT_FAILURE);
	memset(p, 0x5a, CRASH_MAPPING_SIZE);

	signal(SIGUSR1, usr1_handler);
	sigfillset(&all);
	pthread_sigmask(SIG_BLOCK, &all, &old);
	/* One waiter only, retarget stops at the first thread not blocking it. */
	if (pthread_create(&thread, NULL, sigwaiter, NULL))
		_exit(EXIT_FAILURE);
	pthread_sigmask(SIG_SETMASK, &old, NULL);
	while (!__atomic_load_n(&waiter_ready, __ATOMIC_ACQUIRE))
		usleep(1000);
	usleep(50 * 1000);

	pid = getpid();
	tid = syscall(SYS_gettid);
	if (vfork() == 0) {
		if (queue_shared)
			syscall(SYS_kill, pid, SIGUSR1);
		syscall(SYS_tgkill, pid, tid, SIGSEGV);
		syscall(SYS_exit, 0);
	}

	/* Not reached, the pending SIGSEGV dumps core. */
	for (;;)
		pause();
}

/*
 * Dump the crashing child into /tmp/coredump.file through a server that
 * holds the read back so the dumper blocks on the full socket buffer.
 */
static void run_coredump(struct __test_metadata *const _metadata,
			 FIXTURE_DATA(coredump) *self, bool queue_shared)
{
	pid_t pid, pid_coredump_server;
	int ipc_sockets[2];
	int status;
	char c;

	unlink("/tmp/coredump.file");
	unlink("/tmp/coredump.socket");
	ASSERT_TRUE(set_core_pattern("@/tmp/coredump.socket"));
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);

	pid_coredump_server = fork();
	ASSERT_GE(pid_coredump_server, 0);
	if (pid_coredump_server == 0) {
		int fd_server = -1, fd_coredump = -1, fd_core_file = -1;
		int exit_code = EXIT_FAILURE;

		close(ipc_sockets[0]);

		fd_server = create_and_listen_unix_socket("/tmp/coredump.socket");
		if (fd_server < 0)
			goto out;

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			goto out;
		close(ipc_sockets[1]);

		fd_coredump = accept4(fd_server, NULL, NULL, SOCK_CLOEXEC);
		if (fd_coredump < 0) {
			fprintf(stderr, "%s: accept4 failed: %m\n", __func__);
			goto out;
		}

		/* Let the dumper run into the full socket buffer first. */
		sleep(1);

		fd_core_file = creat("/tmp/coredump.file", 0644);
		if (fd_core_file < 0) {
			fprintf(stderr, "%s: creat failed: %m\n", __func__);
			goto out;
		}

		if (recv_coredump_bytes(fd_coredump, fd_core_file) < 0)
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
		crashing_child_retarget(queue_shared);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_EQ(WTERMSIG(status), SIGSEGV);
	ASSERT_TRUE(WCOREDUMP(status));

	wait_and_check_coredump_server(pid_coredump_server, _metadata, self);
}

static void check_coredump_complete(struct __test_metadata *const _metadata)
{
	int fd;

	fd = open("/tmp/coredump.file", O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_TRUE(check_coredump_extent(fd));
	close(fd);
}

TEST_F(coredump, retarget_shared_pending)
{
	run_coredump(_metadata, self, false);
	check_coredump_complete(_metadata);

	run_coredump(_metadata, self, true);
	check_coredump_complete(_metadata);
}

TEST_HARNESS_MAIN
